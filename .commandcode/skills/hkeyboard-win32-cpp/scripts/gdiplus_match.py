# -*- coding: utf-8 -*-
# 复现 DrawTextGp 的 GDI+ 字体创建路径：PrivateFontCollection.AddMemoryFont ->
# FontFamily("MiSans", collection) -> Font(family, em, FontStyleRegular, UnitPixel)。
# 看坏子集(380/0x0100)与修复后(500/0x0140)在 GDI+ 里能否创建 Regular 字体。
import ctypes, sys
from ctypes import wintypes

gdip = ctypes.windll.gdiplus
gdip.GdipCreateFont.argtypes = [ctypes.c_void_p, ctypes.c_float, ctypes.c_int, ctypes.c_int, ctypes.POINTER(ctypes.c_void_p)]
gdip.GdipCreateFont.restype = ctypes.c_int

class StartupInput(ctypes.Structure):
    _fields_ = [('GdiplusVersion', ctypes.c_uint32),
                ('DebugEventCallback', ctypes.c_void_p),
                ('SuppressBackgroundThread', ctypes.c_int),
                ('SuppressExternalCodecs', ctypes.c_int)]

tok = ctypes.c_void_p()
si = StartupInput(1, None, 0, 0)
st = gdip.GdiplusStartup(ctypes.byref(tok), ctypes.byref(si), None)
assert st == 0, st

def test(path, label):
    data = open(path, 'rb').read()
    coll = ctypes.c_void_p()
    r = gdip.GdipNewPrivateFontCollection(ctypes.byref(coll))
    buf = ctypes.create_string_buffer(data)
    r2 = gdip.GdipPrivateAddMemoryFont(coll, buf, len(data))

    fam = ctypes.c_void_p()
    r3 = gdip.GdipCreateFontFamilyFromName('MiSans', coll, ctypes.byref(fam))

    font = ctypes.c_void_p()
    r4 = gdip.GdipCreateFont(fam, 33.0, 0, 2, ctypes.byref(font))  # FontStyleRegular=0, UnitPixel=2

    print('%-14s AddMemFont=%d Family(%s)=%d  Font(Regular)=%d  %s' % (
        label, r2,
        'MiSans', r3, r4,
        'GDI+ 可用' if (r3 == 0 and r4 == 0) else '>>> GDI+ 创建失败(会回退 GDI/宋体) <<<'))
    if font: gdip.GdipDeleteFont(font)
    if fam: gdip.GdipDeleteFontFamily(fam)
    if coll: gdip.GdipDeletePrivateFontCollection(coll)

for i in range(1, len(sys.argv), 2):
    test(sys.argv[i + 1], sys.argv[i])

gdip.GdiplusShutdown(tok)
