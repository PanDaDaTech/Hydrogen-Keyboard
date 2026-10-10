# -*- coding: utf-8 -*-
"""笔画宽度探针：GDI(有网格拟合) vs GDI+(无网格拟合) 的笔画随字号是否线性。

用法:
    python stroke_probe.py <字体路径.ttf> [DPI]
    python stroke_probe.py E:/Project/Git/Hydrogen-Keyboard/winres/MiSans-Medium.ttf

为什么需要它:
    「字体看起来怪怪的 / 该粗的地方不粗、细的地方反而粗」这类争论，
    靠眼睛是吵不出结论的。GDI 会把笔画 snap 到整数像素，导致
    `竖宽/em` 随字号锯齿跳变（实测 13→14 骤降 22%），单字重下无法建立层级。
    本脚本用 alpha 加权（亚像素精度）把这个跳变量化出来。

三个必须照做的实现细节:
    1. 笔画宽度用 alpha 加权 sum(px)/255，不能用 >128 阈值计数 ——
       后者会把 GDI+ 的抗锯齿笔画系统性低估成整数。
    2. PatBlt 的 ROP: BLACKNESS=0x00000042, WHITENESS=0x00FF0062。写反会得到全白 DIB。
    3. 每次只加载一个字体。GDI 在 RemoveFontResourceEx 后仍缓存族映射，
       同族多字重（MiSans-Regular/Medium/Bold 族名都是 MiSans）在同一进程里会串味。
"""
import ctypes
import sys
from ctypes import wintypes as wt

gdi32 = ctypes.WinDLL("gdi32", use_last_error=True)
user32 = ctypes.WinDLL("user32", use_last_error=True)
kernel32 = ctypes.WinDLL("kernel32", use_last_error=True)
gdip = ctypes.WinDLL("gdiplus", use_last_error=True)

BLACKNESS = 0x00000042
WHITENESS = 0x00FF0062


class GdiplusStartupInput(ctypes.Structure):
    _fields_ = [("GdiplusVersion", ctypes.c_uint32),
                ("DebugEventCallback", ctypes.c_void_p),
                ("SuppressBackgroundThread", ctypes.c_int),
                ("SuppressExternalCodecs", ctypes.c_int)]


class RectF(ctypes.Structure):
    _fields_ = [("X", ctypes.c_float), ("Y", ctypes.c_float),
                ("Width", ctypes.c_float), ("Height", ctypes.c_float)]


class BMIH(ctypes.Structure):
    _fields_ = [("biSize", wt.DWORD), ("biWidth", ctypes.c_long),
                ("biHeight", ctypes.c_long), ("biPlanes", wt.WORD),
                ("biBitCount", wt.WORD), ("biCompression", wt.DWORD),
                ("biSizeImage", wt.DWORD), ("biXPelsPerMeter", ctypes.c_long),
                ("biYPelsPerMeter", ctypes.c_long), ("biClrUsed", wt.DWORD),
                ("biClrImportant", wt.DWORD)]


class BMI(ctypes.Structure):
    _fields_ = [("bmiHeader", BMIH), ("bmiColors", wt.DWORD * 3)]


def _setup_api():
    gdi32.CreateCompatibleDC.argtypes = [wt.HDC]
    gdi32.CreateCompatibleDC.restype = wt.HDC
    gdi32.CreateDIBSection.argtypes = [wt.HDC, ctypes.POINTER(BMI), wt.UINT,
                                       ctypes.POINTER(ctypes.c_void_p), wt.HANDLE, wt.DWORD]
    gdi32.CreateDIBSection.restype = wt.HBITMAP
    gdi32.SelectObject.argtypes = [wt.HDC, wt.HGDIOBJ]
    gdi32.SelectObject.restype = wt.HGDIOBJ
    gdi32.PatBlt.argtypes = [wt.HDC] + [ctypes.c_int] * 4 + [wt.DWORD]
    gdi32.SetBkMode.argtypes = [wt.HDC, ctypes.c_int]
    gdi32.SetTextColor.argtypes = [wt.HDC, wt.COLORREF]
    gdi32.CreateFontW.argtypes = ([ctypes.c_int] * 5 + [wt.DWORD] * 6
                                  + [wt.DWORD, wt.DWORD, ctypes.c_wchar_p])
    gdi32.CreateFontW.restype = wt.HFONT
    user32.DrawTextW.argtypes = [wt.HDC, ctypes.c_wchar_p, ctypes.c_int,
                                 ctypes.POINTER(wt.RECT), wt.UINT]
    user32.DrawTextW.restype = ctypes.c_int
    for fn, at in (
        ("GdipCreateFromHDC", [wt.HDC, ctypes.POINTER(ctypes.c_void_p)]),
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
        ("GdipCreateStringFormat", [ctypes.c_int, wt.DWORD, ctypes.POINTER(ctypes.c_void_p)]),
        ("GdipSetStringFormatAlign", [ctypes.c_void_p, ctypes.c_int]),
        ("GdipSetStringFormatLineAlign", [ctypes.c_void_p, ctypes.c_int]),
        ("GdipCreateSolidFill", [wt.DWORD, ctypes.POINTER(ctypes.c_void_p)]),
        ("GdipDrawString", [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int, ctypes.c_void_p,
                            ctypes.POINTER(RectF), ctypes.c_void_p, ctypes.c_void_p]),
        ("GdipFlush", [ctypes.c_void_p, ctypes.c_int]),
        ("GdipDeleteGraphics", [ctypes.c_void_p]),
        ("GdipGetFamilyName", [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_int]),
    ):
        getattr(gdip, fn).argtypes = at


def _em_px(dip, dpi):
    """复刻 hkeyboard.cpp MakeFont 的 height 计算: -MulDiv(size*10, 96, 720)"""
    return abs(-kernel32.MulDiv(int(dip * dpi * 10 + 0.5), 96, 720))


def _new_dib(w, h, bg_white):
    hdc = gdi32.CreateCompatibleDC(0)
    bmi = BMI()
    bh = bmi.bmiHeader
    bh.biSize = 40
    bh.biWidth = w
    bh.biHeight = -h
    bh.biPlanes = 1
    bh.biBitCount = 32
    bh.biCompression = 0
    bits = ctypes.c_void_p()
    hbmp = gdi32.CreateDIBSection(hdc, ctypes.byref(bmi), 0, ctypes.byref(bits), None, 0)
    gdi32.SelectObject(hdc, hbmp)
    gdi32.PatBlt(hdc, 0, 0, w, h, WHITENESS if bg_white else BLACKNESS)
    arr = (ctypes.c_ubyte * (w * h * 4)).from_address(bits.value)
    return hdc, hbmp, arr, w, h


def _to_px(arr, w, h):
    return [[arr[(y * w + x) * 4] for x in range(w)] for y in range(h)]


def draw_gdi(text, dip, dpi, w, h, face, quality=4):
    """GDI DrawTextW（现状路径）。quality: 4=ANTIALIASED, 5=CLEARTYPE"""
    hdc, hbmp, arr, w, h = _new_dib(w, h, bg_white=False)
    lf_h = -kernel32.MulDiv(int(dip * dpi * 10 + 0.5), 96, 720)
    f = gdi32.CreateFontW(lf_h, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, quality, 0,
                          ctypes.c_wchar_p(face))
    gdi32.SelectObject(hdc, f)
    gdi32.SetBkMode(hdc, 1)
    gdi32.SetTextColor(hdc, 0x00FFFFFF)
    r = wt.RECT(0, 0, w, h)
    user32.DrawTextW(hdc, text, -1, ctypes.byref(r), 0x0025)  # SINGLELINE|CENTER|VCENTER|NOPREFIX
    return _to_px(arr, w, h)


def draw_gdiplus(text, em, w, h, ttf, face="MiSans", hint=4, dpi_scale=1.75):
    """GDI+ DrawString。hint: 4=AntiAlias(无gridfit) 5=ClearTypeGridFit 3=AntiAliasGridFit"""
    hdc, hbmp, arr, w, h = _new_dib(w, h, bg_white=False)
    g = ctypes.c_void_p()
    gdip.GdipCreateFromHDC(hdc, ctypes.byref(g))
    coll = ctypes.c_void_p()
    gdip.GdipNewPrivateFontCollection(ctypes.byref(coll))
    if gdip.GdipPrivateAddFontFile(coll, ctypes.c_wchar_p(ttf)) != 0:
        gdip.GdipDeleteGraphics(g)
        return None, "PrivateAddFontFile failed"
    gdip.GdipSetTextRenderingHint(g, hint)
    gdip.GdipSetSmoothingMode(g, 4)
    gdip.GdipSetPixelOffsetMode(g, 2)
    gdip.GdipSetTextContrast(g, 1200)
    fam = ctypes.c_void_p()
    st = gdip.GdipCreateFontFamilyFromName(ctypes.c_wchar_p(face), coll, ctypes.byref(fam))
    if st != 0:
        gdip.GdipDeleteGraphics(g)
        # st==14 => FontFamilyNotFound（多半是把 Graphics 当成了 FontCollection 传参）
        return None, "CreateFontFamily err=%d" % st
    fnt = ctypes.c_void_p()
    if gdip.GdipCreateFont(fam, ctypes.c_float(em), 0, 2, ctypes.byref(fnt)) != 0:
        gdip.GdipDeleteGraphics(g)
        return None, "CreateFont failed"
    fmt = ctypes.c_void_p()
    gdip.GdipCreateStringFormat(0, 0, ctypes.byref(fmt))
    gdip.GdipSetStringFormatAlign(fmt, 1)
    gdip.GdipSetStringFormatLineAlign(fmt, 1)
    br = ctypes.c_void_p()
    gdip.GdipCreateSolidFill(0xFFFFFFFF, ctypes.byref(br))
    rc = RectF(0.0, 0.0, float(w), float(h))
    gdip.GdipDrawString(g, ctypes.c_wchar_p(text), len(text), fnt,
                        ctypes.byref(rc), fmt, br)
    gdip.GdipFlush(g, 1)
    gdip.GdipDeleteGraphics(g)
    return _to_px(arr, w, h), "ok"


def bbox(px, t=64):
    h, w = len(px), len(px[0])
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        row = px[y]
        for x in range(w):
            if row[x] > t:
                if x < x0: x0 = x
                if x > x1: x1 = x
                if y < y0: y0 = y
                if y > y1: y1 = y
    return None if x1 < 0 else (x0, y0, x1, y1)


def stroke_weight(px, glyph="十"):
    """alpha 加权笔画宽度：(竖画宽, 横画高)，亚像素精度。

    原理: 在墨迹顶部 12% 处横切一行，该行只有竖画 —— 整行 alpha 和 / 255 即竖画的等效宽度。
          在墨迹左侧 12% 处纵切一列，该列只有横画 —— 整列 alpha 和 / 255 即横画的等效高度。
    """
    bb = bbox(px)
    if not bb:
        return 0.0, 0.0
    x0, y0, x1, y1 = bb
    if x1 <= x0 or y1 <= y0:
        return 0.0, 0.0
    vw = sum(px[y0 + max(1, int((y1 - y0) * 0.12))]) / 255.0
    hh = sum(px[y][x0 + max(1, int((x1 - x0) * 0.12))] for y in range(len(px))) / 255.0
    return vw, hh


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    ttf = sys.argv[1]
    dpi = float(sys.argv[2]) if len(sys.argv) > 2 else 1.75

    _setup_api()
    tok = ctypes.c_void_p()
    st = gdip.GdiplusStartup(ctypes.byref(tok),
                             ctypes.byref(GdiplusStartupInput(1, None, 0, 0)), None)
    if st != 0:
        print("GdiplusStartup failed:", st)
        return 1

    gdi32.AddFontResourceExW(ctypes.c_wchar_p(ttf), 0x10, 0)

    sizes = [10, 10.5, 11, 12, 13, 14, 15, 16, 18, 20, 22]
    print("=" * 96)
    print(" 笔画宽度探针  DPI=%.2f  %s" % (dpi, ttf))
    print(" alpha 加权（亚像素精度）：竖宽/em 越大 = 视觉越粗")
    print("=" * 96)
    print("%-7s%-7s|%-22s|%-22s" % ("DIP", "em", "GDI (anti-aliased)", "GDI+ (no gridfit)"))
    print("%-7s%-7s|%-10s%-12s|%-10s%-12s" % ("", "", "竖宽", "竖宽/em", "竖宽", "竖宽/em"))
    print("-" * 96)

    rows = []
    for dip in sizes:
        em = _em_px(dip, dpi)
        vg, hg = stroke_weight(draw_gdi("十", dip, dpi, 160, 160, "MiSans"))
        pxp, msg = draw_gdiplus("十", em, 160, 160, ttf)
        if pxp is None:
            print("GDI+ 失败:", msg)
            return 1
        vp, hp = stroke_weight(pxp)
        rows.append((dip, em, vg, vp))
        print("%-7s%-7d|%-10.2f%-12.4f|%-10.2f%-12.4f" % (dip, em, vg, vg / em, vp, vp / em))
    print("-" * 96)

    g_small, g_big = rows[0], rows[-1]

    def spread(idx):
        vals = [r[idx] / r[1] for r in rows]
        return min(vals), max(vals)

    print()
    print("【判定 1】笔画随字号是否平滑（竖宽/em 的波动幅度）")
    for tag, idx in (("GDI ", 2), ("GDI+", 3)):
        lo, hi = spread(idx)
        amp = (hi - lo) / lo * 100
        verdict = "平滑 ✔" if amp < 15 else "锯齿/跳变 ✘"
        print("   %s: %.4f ~ %.4f   波动 %.0f%%   %s" % (tag, lo, hi, amp, verdict))

    print()
    print("【判定 2】字号 %s→%s 增长 %.0f%%，笔画应同比增长"
          % (g_small[0], g_big[0], (g_big[1] / g_small[1] - 1) * 100))
    for tag, idx in (("GDI ", 2), ("GDI+", 3)):
        grow = (g_big[idx] / g_small[idx] - 1) * 100 if g_small[idx] else 0
        print("   %s: %.2f → %.2f   %+.0f%%" % (tag, g_small[idx], g_big[idx], grow))

    print()
    print("【判定 3】大字号必须比小字号粗（层级方向）")
    for tag, idx in (("GDI ", 2), ("GDI+", 3)):
        lo, hi = g_small[idx] / g_small[1], g_big[idx] / g_big[1]
        print("   %s: 最小 %.4f  最大 %.4f   %s"
              % (tag, lo, hi, "✔ 正确" if hi > lo else "✘ 倒挂"))

    gdi32.RemoveFontResourceExW(ctypes.c_wchar_p(ttf), 0x10, 0)
    return 0


if __name__ == "__main__":
    sys.exit(main())
