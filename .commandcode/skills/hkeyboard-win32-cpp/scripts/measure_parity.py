# -*- coding: utf-8 -*-
"""GDI / GDI+ 量宽对比 —— 排查「文字只显示前几个字」。

病因（见 references/gdi-hinting-and-gdiplus.md §4.4）：
    量宽走 GDI（GetTextExtentPoint32W），绘字走 GDI+（DrawString）。
    GDI+ 的布局宽度比 GDI 的 advance 之和大 10~18%，而 DrawString 在 NoWrap 下
    就按这个尺寸判定「放不放得下」，放不下**直接丢弃末尾字符**（不是裁切，NoClip 管不到）。
    于是「常规」被画成「常」、「设置」被画成「设」。

用法:
    python measure_parity.py <字体文件> "文本:字号" ...
例:
    python measure_parity.py "E:/Project/Git/Hydrogen-Keyboard/winres/MiSans-Medium.ttf" "常规:11" "设置:12"

字号按 HKeyboard 的约定：DIP 数值（未乘 dpi），内部复刻 MakeFont() 的换算。
"""
import ctypes
import math
import sys
from ctypes import wintypes as wt

gdi32    = ctypes.WinDLL("gdi32",    use_last_error=True)
user32   = ctypes.WinDLL("user32",   use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
gdip     = ctypes.WinDLL("gdiplus",  use_last_error=True)

FACE = "MiSans"
FALLBACK = "Microsoft YaHei"


class GdiplusStartupInput(ctypes.Structure):
    _fields_ = [("GdiplusVersion", ctypes.c_uint32),
                ("DebugEventCallback", ctypes.c_void_p),
                ("SuppressBackgroundThread", ctypes.c_int),
                ("SuppressExternalCodecs", ctypes.c_int)]


class RectF(ctypes.Structure):
    _fields_ = [("X", ctypes.c_float), ("Y", ctypes.c_float),
                ("Width", ctypes.c_float), ("Height", ctypes.c_float)]


# ---------------------------------------------------------------- 基础设施
gdip.GdiplusStartup(ctypes.byref(ctypes.c_void_p()),
                    ctypes.byref(GdiplusStartupInput(1, None, 0, 0)), None)

gdi32.CreateCompatibleDC.argtypes = [wt.HDC]
gdi32.CreateCompatibleDC.restype = wt.HDC
gdi32.DeleteDC.argtypes = [wt.HDC]
gdi32.SelectObject.argtypes = [wt.HDC, wt.HGDIOBJ]
gdi32.SelectObject.restype = wt.HGDIOBJ
gdi32.DeleteObject.argtypes = [wt.HGDIOBJ]
gdi32.CreateFontW.argtypes = ([ctypes.c_int] * 5 + [wt.DWORD] * 6 +
                              [wt.DWORD, wt.DWORD, ctypes.c_wchar_p])
gdi32.CreateFontW.restype = wt.HFONT
gdi32.GetTextExtentPoint32W.argtypes = [wt.HDC, ctypes.c_wchar_p, ctypes.c_int,
                                        ctypes.POINTER(wt.SIZE)]
gdi32.GetTextExtentPoint32W.restype = wt.BOOL
gdi32.GetDeviceCaps.argtypes = [wt.HDC, ctypes.c_int]
gdi32.GetDeviceCaps.restype = ctypes.c_int
gdi32.AddFontResourceExW.argtypes = [ctypes.c_wchar_p, wt.DWORD, ctypes.c_void_p]
gdi32.AddFontResourceExW.restype = ctypes.c_int
gdi32.RemoveFontResourceExW.argtypes = [ctypes.c_wchar_p, wt.DWORD, ctypes.c_void_p]
gdi32.RemoveFontResourceExW.restype = wt.BOOL
kernel32.MulDiv.argtypes = [ctypes.c_int, ctypes.c_int, ctypes.c_int]
kernel32.MulDiv.restype = ctypes.c_int
user32.GetDC.argtypes = [wt.HWND]
user32.GetDC.restype = wt.HDC
user32.ReleaseDC.argtypes = [wt.HWND, wt.HDC]
user32.ReleaseDC.restype = ctypes.c_int

# 用 flat API 时 argtypes 必须**逐参数写全** —— 少一个 ctypes 不报错，
# 原生函数会去读栈上的垃圾指针 → 静默 Access Violation（进程直接死、无 traceback）。
# GdipMeasureString 末尾还有一个 linesFilled，共 9 个参数。
for _name, _at in (
    ("GdipCreateFromHDC", [wt.HDC, ctypes.POINTER(ctypes.c_void_p)]),
    ("GdipNewPrivateFontCollection", [ctypes.POINTER(ctypes.c_void_p)]),
    ("GdipPrivateAddFontFile", [ctypes.c_void_p, ctypes.c_wchar_p]),
    ("GdipCreateFontFamilyFromName", [ctypes.c_wchar_p, ctypes.c_void_p,
                                      ctypes.POINTER(ctypes.c_void_p)]),
    ("GdipCreateFont", [ctypes.c_void_p, ctypes.c_float, ctypes.c_int,
                        ctypes.c_int, ctypes.POINTER(ctypes.c_void_p)]),
    ("GdipCreateStringFormat", [ctypes.c_int, wt.DWORD,
                                ctypes.POINTER(ctypes.c_void_p)]),
    ("GdipSetStringFormatFlags", [ctypes.c_void_p, ctypes.c_int]),
    ("GdipMeasureString", [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int,
                           ctypes.c_void_p, ctypes.POINTER(RectF),
                           ctypes.c_void_p, ctypes.POINTER(RectF),
                           ctypes.POINTER(ctypes.c_int),
                           ctypes.POINTER(ctypes.c_int)]),
    ("GdipDeleteGraphics", [ctypes.c_void_p]),
):
    getattr(gdip, _name).argtypes = _at

# ⚠ 量 DPI 有两个坑，少一个都会算出 scale=1.0（175% 机器上全部偏小 43%）：
#   1) 不能用 CreateCompatibleDC —— 内存 DC 的 DPI 固定 96，必须用屏幕 DC；
#   2) 进程必须先声明 DPI 感知，否则系统做 DPI 虚拟化，屏幕 DC 也只返回 96。
#      HKeyboard 的 manifest 是 DPI 感知的，Python 默认不是。
try:
    ctypes.WinDLL("shcore").SetProcessDpiAwareness(2)      # PER_MONITOR_DPI_AWARE
except Exception:
    try:
        user32.SetProcessDPIAware()
    except Exception:
        pass

_scr = user32.GetDC(0)
DPI = gdi32.GetDeviceCaps(_scr, 90) / 96.0      # LOGPIXELSY = 90
user32.ReleaseDC(0, _scr)
if DPI <= 0:
    DPI = 1.0


def em_px(dip):
    """复刻 MakeFont(double size)：size = dip * dpi，再 MulDiv(size*10, 96, 720)。"""
    return abs(-kernel32.MulDiv(int(dip * DPI * 10 + 0.5), 96, 720))


def load(path):
    n = gdi32.AddFontResourceExW(ctypes.c_wchar_p(path), 0x10, 0)     # FR_PRIVATE
    coll = ctypes.c_void_p()
    gdip.GdipNewPrivateFontCollection(ctypes.byref(coll))
    st = gdip.GdipPrivateAddFontFile(coll, ctypes.c_wchar_p(path))
    return n, coll, st


def unload(path):
    gdi32.RemoveFontResourceExW(ctypes.c_wchar_p(path), 0x10, 0)


def gdi_width(text, dip):
    """GDI 量宽：汉字的 advance 之和（hmtx 里是 1.0em/字，这个值本身没错）。"""
    dc = gdi32.CreateCompatibleDC(0)
    f = gdi32.CreateFontW(-em_px(dip), 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 4, 0,
                          ctypes.c_wchar_p(FACE))
    old = gdi32.SelectObject(dc, f)
    sz = wt.SIZE()
    gdi32.GetTextExtentPoint32W(dc, text, len(text), ctypes.byref(sz))
    gdi32.SelectObject(dc, old)
    gdi32.DeleteObject(f)
    gdi32.DeleteDC(dc)
    return sz.cx


def gdip_width(text, dip, coll):
    """GDI+ 量宽：比 GDI 大 10~18%，这就是 DrawString 判「放不下」用的尺寸。"""
    dc = gdi32.CreateCompatibleDC(0)
    g = ctypes.c_void_p()
    gdip.GdipCreateFromHDC(dc, ctypes.byref(g))
    fam = ctypes.c_void_p()
    if gdip.GdipCreateFontFamilyFromName(ctypes.c_wchar_p(FACE), coll,
                                         ctypes.byref(fam)) != 0:
        gdip.GdipCreateFontFamilyFromName(ctypes.c_wchar_p(FALLBACK), coll,
                                          ctypes.byref(fam))
    fnt = ctypes.c_void_p()
    gdip.GdipCreateFont(fam, ctypes.c_float(em_px(dip)), 0, 2, ctypes.byref(fnt))   # UnitPixel
    fmt = ctypes.c_void_p()
    gdip.GdipCreateStringFormat(0, 0, ctypes.byref(fmt))
    gdip.GdipSetStringFormatFlags(fmt, 0x1000 | 0x4000)          # NoWrap | NoClip
    box = RectF(0, 0, 8192.0, 400.0)
    out = RectF(0, 0, 0, 0)
    ch = ctypes.c_int()
    lf = ctypes.c_int()
    gdip.GdipMeasureString(g, text, len(text), fnt, ctypes.byref(box), fmt,
                           ctypes.byref(out), ctypes.byref(ch), ctypes.byref(lf))
    gdip.GdipDeleteGraphics(g)
    gdi32.DeleteDC(dc)
    return out.Width


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    path = sys.argv[1]
    specs = sys.argv[2:] or ["常规:11"]
    n, coll, st = load(path)
    if n == 0:
        print(f"字体加载失败: {path}")
        return 1
    print(f"字体 = {path}")
    print(f"GDI 注册={n}  GDI+ 添加状态={st} (0=OK)   DPI scale={DPI}\n")
    print(f"{'文本':<12}{'字号':<7}{'em':<6}{'GDI 量':<9}{'GDI+ 量':<10}{'比值':<8}"
          f"{'容器至少要有':<14}{'比 GDI 量多':<12}")
    print("-" * 82)
    for spec in specs:
        if ":" in spec:
            text, dip = spec.rsplit(":", 1)
            dip = float(dip)
        else:
            text, dip = spec, 11.0
        gw = gdi_width(text, dip)
        pw = gdip_width(text, dip, coll)
        need = int(math.ceil(pw)) + 1          # DrawString 要整数宽；实测 +1 才稳
        print(f"{text:<12}{dip:<7}{em_px(dip):<6}{gw:<9}{pw:<10.1f}"
              f"{(pw / gw if gw else 0):<8.3f}{need:<14}{need - gw:<12.0f}")
    unload(path)
    print("\n判读：")
    print("  比值 ≈ 1.10~1.36。凡是用「GDI 量宽」当绘制宽度的控件（Tab / 标题栏 pill /")
    print("  键面『图标+文字』/ 分段），实际容器的『GDI 量』那一列小于『容器至少要有』")
    print("  的，就会丢尾字。修法见 §4.4：让 MeasureTextW 也走 GDI+。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
