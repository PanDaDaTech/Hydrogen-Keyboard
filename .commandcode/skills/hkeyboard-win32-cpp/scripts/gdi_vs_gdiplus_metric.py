# -*- coding: utf-8 -*-
"""同一文本分别用 GDI 与 GDI+ 渲染，量出墨迹宽高（物理像素）。

用途
----
1. 反推实机截图里的**真实字号**：把同字号的渲染墨迹与截图墨迹相除，乘回字号。
2. 判断某个字体的 GDI / GDI+ 两条路径**字形宽度是否一致**（本项目实测：几乎一致，
   22pt「设置」= 97 vs 99 px —— 所以"字宽不同"不能用来解释截图偏小）。

用法
----
    python gdi_vs_gdiplus_metric.py <字体路径> [文本:字号 ...]

    python gdi_vs_gdiplus_metric.py "E:/Project/Git/Hydrogen-Keyboard/winres/MiSans-Medium.ttf" \\
        "设置:22" "自动呼出:15" "常规:11"

不带文本参数时用内置的一组默认样本。字号单位 = DIP（pt），
脚本按 175% DPI 复刻 MakeFont 的 `-MulDiv(size*dpi*10+0.5, 96, 720)`。

依赖：PIL（仅 GDI+ 路径需要，用于 DIB 取像素）
"""
import ctypes
import sys
from ctypes import wintypes as wt

gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
gdip = ctypes.WinDLL("gdiplus", use_last_error=True)

DPI = 1.75
PT2PX = 96.0 / 72.0


class BMIH(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", ctypes.c_long), ("biHeight", ctypes.c_long),
                ("biPlanes", wt.WORD), ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wt.DWORD),
                ("biClrImportant", wt.DWORD)]


class BMI(ctypes.Structure):
    _fields_ = [("bmiHeader", BMIH), ("bmiColors", wt.DWORD * 3)]


class RectF(ctypes.Structure):
    _fields_ = [("X", ctypes.c_float), ("Y", ctypes.c_float),
                ("Width", ctypes.c_float), ("Height", ctypes.c_float)]


class GdiplusStartupInput(ctypes.Structure):
    _fields_ = [("GdiplusVersion", ctypes.c_uint32), ("DebugEventCallback", ctypes.c_void_p),
                ("SuppressBackgroundThread", ctypes.c_int), ("SuppressExternalCodecs", ctypes.c_int)]


gdi32.CreateCompatibleDC.argtypes = [wt.HDC]; gdi32.CreateCompatibleDC.restype = wt.HDC
gdi32.CreateDIBSection.argtypes = [wt.HDC, ctypes.POINTER(BMI), wt.UINT,
                                   ctypes.POINTER(ctypes.c_void_p), wt.HANDLE, wt.DWORD]
gdi32.CreateDIBSection.restype = wt.HBITMAP
gdi32.SelectObject.argtypes = [wt.HDC, wt.HGDIOBJ]; gdi32.SelectObject.restype = wt.HGDIOBJ
gdi32.DeleteObject.argtypes = [wt.HGDIOBJ]
gdi32.DeleteDC.argtypes = [wt.HDC]
gdi32.PatBlt.argtypes = [wt.HDC] + [ctypes.c_int] * 4 + [wt.DWORD]
gdi32.SetBkMode.argtypes = [wt.HDC, ctypes.c_int]
gdi32.SetTextColor.argtypes = [wt.HDC, wt.COLORREF]
gdi32.CreateFontW.argtypes = [ctypes.c_int] * 5 + [wt.DWORD] * 6 + [wt.DWORD, wt.DWORD,
                                                                   ctypes.c_wchar_p]
gdi32.CreateFontW.restype = wt.HFONT
gdi32.AddFontResourceExW.argtypes = [ctypes.c_wchar_p, wt.DWORD, ctypes.c_void_p]
gdi32.AddFontResourceExW.restype = ctypes.c_int
user32.DrawTextW.argtypes = [wt.HDC, ctypes.c_wchar_p, ctypes.c_int,
                             ctypes.POINTER(wt.RECT), wt.UINT]

for _fn, _at in (("GdipCreateFromHDC", [wt.HDC, ctypes.POINTER(ctypes.c_void_p)]),
                 ("GdipNewPrivateFontCollection", [ctypes.POINTER(ctypes.c_void_p)]),
                 ("GdipPrivateAddFontFile", [ctypes.c_void_p, ctypes.c_wchar_p]),
                 ("GdipSetTextRenderingHint", [ctypes.c_void_p, ctypes.c_int]),
                 ("GdipSetSmoothingMode", [ctypes.c_void_p, ctypes.c_int]),
                 ("GdipSetPixelOffsetMode", [ctypes.c_void_p, ctypes.c_int]),
                 ("GdipSetTextContrast", [ctypes.c_void_p, ctypes.c_int]),
                 ("GdipCreateFontFamilyFromName", [ctypes.c_wchar_p, ctypes.c_void_p,
                                                   ctypes.POINTER(ctypes.c_void_p)]),
                 ("GdipCreateFont", [ctypes.c_void_p, ctypes.c_float, ctypes.c_int,
                                     ctypes.c_int, ctypes.POINTER(ctypes.c_void_p)]),
                 ("GdipCreateStringFormat", [ctypes.c_int, wt.DWORD,
                                             ctypes.POINTER(ctypes.c_void_p)]),
                 ("GdipCreateSolidFill", [wt.DWORD, ctypes.POINTER(ctypes.c_void_p)]),
                 ("GdipDrawString", [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int,
                                     ctypes.c_void_p, ctypes.POINTER(RectF), ctypes.c_void_p,
                                     ctypes.c_void_p]),
                 ("GdipFlush", [ctypes.c_void_p, ctypes.c_int]),
                 ("GdipDeleteGraphics", [ctypes.c_void_p])):
    getattr(gdip, _fn).argtypes = _at

_tok = ctypes.c_void_p()
gdip.GdiplusStartup(ctypes.byref(_tok), ctypes.byref(GdiplusStartupInput(1, None, 0, 0)), None)


def _newdib(W, H, clear=True):
    hdc = gdi32.CreateCompatibleDC(0)
    bmi = BMI(); bh = bmi.bmiHeader
    bh.biSize = 40; bh.biWidth = W; bh.biHeight = -H
    bh.biPlanes = 1; bh.biBitCount = 32; bh.biCompression = 0
    bits = ctypes.c_void_p()
    hbmp = gdi32.CreateDIBSection(hdc, ctypes.byref(bmi), 0, ctypes.byref(bits), None, 0)
    gdi32.SelectObject(hdc, hbmp)
    if clear:
        gdi32.PatBlt(hdc, 0, 0, W, H, 0x00000042)      # BLACKNESS
    return hdc, hbmp, (ctypes.c_ubyte * (W * H * 4)).from_address(bits.value)


def _bbox(arr, W, H, thr=41):
    x0, y0, x1, y1 = W, H, -1, -1
    for y in range(H):
        b = y * W * 4
        for x in range(W):
            if arr[b + x * 4] > thr:
                x0 = min(x0, x); x1 = max(x1, x)
                y0 = min(y0, y); y1 = max(y1, y)
    return (x1 - x0 + 1, y1 - y0 + 1) if x1 >= 0 else None


def _lfheight(dip):
    """复刻 MakeFont 的高度算法，返回 em 像素（正数）。"""
    return abs(-kernel32.MulDiv(int(dip * DPI * 10 + 0.5), 96, 720))


def gdi_ink(text, dip, face="MiSans", quality=4):
    W, H = 1400, 300
    hdc, hbmp, arr = _newdib(W, H)
    f = gdi32.CreateFontW(-_lfheight(dip), 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, quality, 0,
                          ctypes.c_wchar_p(face))
    gdi32.SelectObject(hdc, f)
    gdi32.SetBkMode(hdc, 1)
    gdi32.SetTextColor(hdc, 0x00FFFFFF)
    r = wt.RECT(0, 0, W, H)
    user32.DrawTextW(hdc, ctypes.c_wchar_p(text), -1, ctypes.byref(r), 0x0025)
    out = _bbox(arr, W, H)
    gdi32.DeleteObject(hbmp); gdi32.DeleteDC(hdc)
    return out


def gdiplus_ink(text, dip, coll, family="MiSans", hint=4):
    W, H = 1400, 300
    hdc, hbmp, arr = _newdib(W, H)
    g = ctypes.c_void_p()
    gdip.GdipCreateFromHDC(hdc, ctypes.byref(g))
    gdip.GdipSetTextRenderingHint(g, hint)
    gdip.GdipSetSmoothingMode(g, 4)
    gdip.GdipSetPixelOffsetMode(g, 2)
    gdip.GdipSetTextContrast(g, 1200)
    fam = ctypes.c_void_p()
    st = gdip.GdipCreateFontFamilyFromName(ctypes.c_wchar_p(family), coll, ctypes.byref(fam))
    if st != 0:
        gdip.GdipDeleteGraphics(g)
        gdi32.DeleteObject(hbmp); gdi32.DeleteDC(hdc)
        return None, "FontFamilyNotFound(err=%d)" % st
    fnt = ctypes.c_void_p()
    gdip.GdipCreateFont(fam, ctypes.c_float(dip * DPI * PT2PX), 0, 2, ctypes.byref(fnt))
    fmt = ctypes.c_void_p()
    gdip.GdipCreateStringFormat(0, 0, ctypes.byref(fmt))
    gdip.GdipSetStringFormatFlags(fmt, 0x1000)          # NoClip
    br = ctypes.c_void_p()
    gdip.GdipCreateSolidFill(0xFFFFFFFF, ctypes.byref(br))
    rc = RectF(0, 0, float(W), float(H))
    gdip.GdipDrawString(g, ctypes.c_wchar_p(text), len(text), fnt, ctypes.byref(rc), fmt, br)
    gdip.GdipFlush(g, 1)
    gdip.GdipDeleteGraphics(g)
    out = _bbox(arr, W, H)
    gdi32.DeleteObject(hbmp); gdi32.DeleteDC(hdc)
    return out, "ok"


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    ttf = sys.argv[1]
    samples = []
    for a in sys.argv[2:]:
        if ":" in a:
            t, d = a.rsplit(":", 1)
            samples.append((t, float(d)))
    if not samples:
        samples = [("设置", 22), ("自动呼出", 15), ("常规", 11), ("点击输入框时自动弹出键盘", 11)]

    n = gdi32.AddFontResourceExW(ctypes.c_wchar_p(ttf), 0x10, None)
    print("AddFontResourceExW(%s) -> %d" % (ttf, n))
    coll = ctypes.c_void_p()
    gdip.GdipNewPrivateFontCollection(ctypes.byref(coll))
    add = gdip.GdipPrivateAddFontFile(coll, ctypes.c_wchar_p(ttf))
    print("GdipPrivateAddFontFile -> %d (0=ok)" % add)
    print("DPI = %s" % DPI)
    print()
    print("%-26s %-6s %-8s %-14s %-14s" % ("文本", "DIP", "em_px", "GDI 墨迹", "GDI+ 墨迹"))
    print("-" * 74)
    for text, dip in samples:
        a = gdi_ink(text, dip)
        b, st = gdiplus_ink(text, dip, coll)
        fa = "%dx%d" % a if a else "-"
        fb = "%dx%d" % b if b else st
        print("%-26s %-6s %-8d %-14s %-14s" % (text, dip, _lfheight(dip), fa, fb))
    return 0


if __name__ == "__main__":
    sys.exit(main())
