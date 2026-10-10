import ctypes, sys
from ctypes import wintypes

gdi32 = ctypes.windll.gdi32

# AddFontMemResourceEx
gdi32.AddFontMemResourceEx.restype = wintypes.HANDLE
gdi32.AddFontMemResourceEx.argtypes = [ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p, ctypes.POINTER(wintypes.DWORD)]

# LOGFONTW (92 bytes, lfFaceName 32 WCHAR at offset 28)
class LOGFONTW(ctypes.Structure):
    _fields_ = [
        ('lfHeight', ctypes.c_long), ('lfWidth', ctypes.c_long),
        ('lfEscapement', ctypes.c_long), ('lfOrientation', ctypes.c_long),
        ('lfWeight', ctypes.c_long), ('lfItalic', wintypes.BYTE),
        ('lfUnderline', wintypes.BYTE), ('lfStrikeOut', wintypes.BYTE),
        ('lfCharSet', wintypes.BYTE), ('lfOutPrecision', wintypes.BYTE),
        ('lfClipPrecision', wintypes.BYTE), ('lfQuality', wintypes.BYTE),
        ('lfPitchAndFamily', wintypes.BYTE), ('lfFaceName', ctypes.c_wchar * 32),
    ]

def test(path, label):
    data = open(path, 'rb').read()
    n = wintypes.DWORD(0)
    h = gdi32.AddFontMemResourceEx(data, len(data), None, ctypes.byref(n))
    print('%-16s AddFontMemResourceEx: handle=%s fonts=%d' % (label, hex(h or 0), n.value))
    if not h:
        print('%-16s [FAIL] 注册失败' % label); return
    hf = gdi32.CreateFontW(-32, 0, 0, 0, 400, 0, 0, 0, 1, 0, 0, 4, 0, 'MiSans')
    lf = LOGFONTW()
    got = gdi32.GetObjectW(hf, ctypes.sizeof(LOGFONTW), ctypes.byref(lf))
    face = lf.lfFaceName
    ok = ('MiSans' in face)
    print('%-16s CreateFontW(FW_NORMAL=400,"MiSans") -> face=%r weight=%d  %s' % (
        label, face, lf.lfWeight, 'MATCH MiSans' if ok else '>>> 回退到系统字体（宋体）<<<'))

args = sys.argv[1:]
for i in range(0, len(args), 2):
    test(args[i + 1], args[i])
