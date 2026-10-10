# -*- coding: utf-8 -*-
"""GDI 文字探针：验证内嵌字体到底有几个字面、字重请求会被满足还是被仿真、
以及仿真加粗对「墨迹覆盖率 / 笔画连通域 / 字宽」的实际影响。

用途：任何「字重看起来不对」「改了字号布局就崩」的问题，先用它拿事实再动手。
不依赖 PIL —— PNG 自己写。

命令行：
    python gdi_text_probe.py family  <font.ttf>
    python gdi_text_probe.py measure <font.ttf> "文字" 12 400
    python gdi_text_probe.py metrics <font.ttf> "文字"        # 列出 12~20pt 的 N/B 对比
    python gdi_text_probe.py widths  <font.ttf> "文字"        # 列出多字号多字重宽度

作为模块：
    from gdi_text_probe import load_font, measure, coverage, render_png
"""
import ctypes, ctypes.wintypes as W, struct, zlib, sys

gdi = ctypes.WinDLL("gdi32")
user = ctypes.WinDLL("user32")
ANTIALIASED, CLEARTYPE = 4, 5

_cache = {}
def load_font(path, face="MiSans"):
    """注册内存字体；返回 (face, error_or_None)。同一路径只注册一次（进程内）。"""
    if path in _cache: return face, None
    data = open(path, "rb").read()
    buf = ctypes.create_string_buffer(data, len(data))
    n = W.DWORD(0)
    h = gdi.AddFontMemResourceEx(ctypes.cast(buf, ctypes.c_void_p), len(data), None, ctypes.byref(n))
    _cache[path] = (buf, h)          # 保住 buffer，否则字体指针悬空
    return face, (None if h and n.value else "AddFontMemResourceEx 失败")

def _font(pt, weight=400, quality=ANTIALIASED, face="MiSans"):
    # 与 hkeyboard.cpp 的 MakeFont 同源：h = -MulDiv(int(size*10+0.5), 96, 720)
    h = -round(pt * 96.0 / 72.0)
    return gdi.CreateFontW(h,0,0,0,weight,0,0,0,1,0,0,quality,0, face)

def measure(text, pt, weight=400, quality=ANTIALIASED, face="MiSans", extra=0):
    """返回 (宽, 高) 像素。extra = SetTextCharacterExtra（单位是像素/字符）。"""
    hdc = gdi.CreateCompatibleDC(0)
    hf = _font(pt, weight, quality, face)
    old = gdi.SelectObject(hdc, hf)
    if extra: gdi.SetTextCharacterExtra(hdc, extra)
    sz = W.SIZE()
    gdi.GetTextExtentPoint32W(hdc, text, len(text), ctypes.byref(sz))
    gdi.SelectObject(hdc, old); gdi.DeleteObject(hf); gdi.DeleteDC(hdc)
    return sz.cx, sz.cy

class _Canvas:
    def __init__(s, w, h, bg):
        s.w, s.h = w, h
        bmi = ctypes.c_buffer(40 + 256*4)
        struct.pack_into('<IiiHHIIiiII', bmi, 0, 40, w, -h, 1, 32, 0, 0, 0,0,0,0)
        s.ppv = ctypes.c_void_p()
        s.dc = gdi.CreateCompatibleDC(0)
        s.bmp = gdi.CreateDIBSection(s.dc, bmi, 0, ctypes.byref(s.ppv), None, 0)
        s.old = gdi.SelectObject(s.dc, s.bmp)
        br = gdi.CreateSolidBrush(bgr(bg)); r = W.RECT(0,0,w,h)
        user.FillRect(s.dc, ctypes.byref(r), br); gdi.DeleteObject(br)
        gdi.SetBkMode(s.dc, 1)
    def text(s, x, y, t, pt, color, weight=400, q=ANTIALIASED, face="MiSans"):
        hf = _font(pt, weight, q, face); o = gdi.SelectObject(s.dc, hf)
        gdi.SetTextColor(s.dc, bgr(color)); gdi.TextOutW(s.dc, x, y, t, len(t))
        gdi.SelectObject(s.dc, o); gdi.DeleteObject(hf)
    def pixels(s):
        return ctypes.string_at(s.ppv, s.w*s.h*4)
    def close(s):
        gdi.SelectObject(s.dc, s.old); gdi.DeleteObject(s.bmp); gdi.DeleteDC(s.dc)

def bgr(t): return (t[2]<<16)|(t[1]<<8)|t[0]

def _write_png(path, w, h, rgb):
    raw = b''.join(b'\x00' + rgb[y*w*3:(y+1)*w*3] for y in range(h))
    def ch(t, d):
        c = t + d
        return struct.pack('>I', len(d)) + c + struct.pack('>I', zlib.crc32(c) & 0xffffffff)
    open(path, 'wb').write(b'\x89PNG\r\n\x1a\n'
        + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
        + ch(b'IDAT', zlib.compress(raw, 9)) + ch(b'IEND', b''))

def render_png(path, items, w, h, bg=(255,255,255), face="MiSans"):
    """items = [(x, y, text, pt, rgb, weight[, quality])]；rgb 传 (r,g,b)。"""
    c = _Canvas(w, h, bg)
    for it in items:
        c.text(*it[:5], weight=it[5] if len(it)>5 else 400, face=face)
    buf = c.pixels()
    c.close()
    rgb = bytes(v for i in range(w*h) for v in (buf[i*4+2], buf[i*4+1], buf[i*4]))
    _write_png(path, w, h, rgb)
    return path

def coverage(buf, w, h, text_rgb, bg_lum=255, thresh=40):
    """返回 (墨迹像素数, 连通域数, 大连通域占比)。
    连通域骤减 = 笔画被填到一起（假粗体的典型症状）。"""
    fg = bytearray(w*h)
    for i in range(w*h):
        r,g,b = buf[i*4+2], buf[i*4+1], buf[i*4]
        if 0.2126*r + 0.7152*g + 0.0722*b < bg_lum - thresh: fg[i] = 1
    total = sum(fg); seen = bytearray(w*h); comps = []
    for s0 in range(w*h):
        if fg[s0] and not seen[s0]:
            st=[s0]; seen[s0]=1; cnt=0
            while st:
                i = st.pop(); cnt += 1
                x, y = i % w, i // w
                for dx,dy in ((1,0),(-1,0),(0,1),(0,-1),(1,1),(-1,-1),(1,-1),(-1,1)):
                    nx, ny = x+dx, y+dy
                    if 0 <= nx < w and 0 <= ny < h:
                        j = ny*w+nx
                        if fg[j] and not seen[j]: seen[j]=1; st.append(j)
            comps.append(cnt)
    comps.sort(reverse=True)
    big = sum(c for c in comps if c >= 12)
    return total, len(comps), (big/total if total else 0)

# ---------------- CLI ----------------
def _family(face, path):
    """请求各种字重，看 GetTextFace 与 lfWeight 回显 —— 判断是否被仿真。"""
    class LF(ctypes.Structure):
        _fields_ = [("lfHeight",ctypes.c_long),("lfWidth",ctypes.c_long),
            ("lfEscapement",ctypes.c_long),("lfOrientation",ctypes.c_long),
            ("lfWeight",ctypes.c_long),("lfItalic",ctypes.c_byte),("lfUnderline",ctypes.c_byte),
            ("lfStrikeOut",ctypes.c_byte),("lfCharSet",ctypes.c_byte),("lfOutPrecision",ctypes.c_byte),
            ("lfClipPrecision",ctypes.c_byte),("lfQuality",ctypes.c_byte),
            ("lfPitchAndFamily",ctypes.c_byte),("lfFaceName",ctypes.c_wchar*32)]
    print("注册：", load_font(path, face))
    print("  %-8s %-18s %s" % ("请求", "实际族名", "lfWeight"))
    for wt in (400,500,600,700,800):
        hf = _font(13, wt, face=face)
        dc = gdi.CreateCompatibleDC(0); gdi.SelectObject(dc, hf)
        b = ctypes.create_unicode_buffer(64); gdi.GetTextFaceW(dc, 64, b)
        lf = LF(); gdi.GetObjectW(hf, ctypes.sizeof(lf), ctypes.byref(lf))
        gdi.DeleteDC(dc); gdi.DeleteObject(hf)
        print("  %-8d %-18s %d" % (wt, b.value, lf.lfWeight))
    print("  ⚠ 族名恒同 + lfWeight 只是回显 = 族里没有多个字面；请求 FW_BOLD 会拿到仿真加粗")

def _metrics(face, text, path):
    C = _Canvas(500, 60, (255,255,255))
    print("%-6s %-8s %-12s %-10s %-12s %-10s" % ("字号","像素高","覆盖率N","连通域N","覆盖率B","连通域B"))
    for pt in (12,13,14,16,18,20):
        out = {}
        for wt, tag in ((400,'N'), (700,'B')):
            C.text(2, 6, text, pt, (32,34,40), wt)
            buf = C.pixels()
            out[tag] = coverage(buf, C.w, C.h, None)
            # 清屏重来
            br = gdi.CreateSolidBrush(bgr((255,255,255))); r = W.RECT(0,0,C.w,C.h)
            user.FillRect(C.dc, ctypes.byref(r), br); gdi.DeleteObject(br)
        a, b = out['N'], out['B']
        print("%-6d %-8d %-12s %-10d %-12s %-10d" % (pt, round(pt*96/72),
            "%.5f" % (a[0]/(C.w*C.h)), a[1], "%.5f" % (b[0]/(C.w*C.h)), b[1]))
    C.close()
    print("  ⚠ 覆盖率暴涨 30%+ 且连通域骤减 = 仿真加粗把中文笔画填死了")
    print("    真 Bold 字面通常只 +15~20%")

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print(__doc__); sys.exit(0)
    cmd, path = sys.argv[1], sys.argv[2]
    face = "MiSans"
    load_font(path, face)
    if cmd == "family":
        _family(face, path)
    elif cmd == "metrics":
        _metrics(face, sys.argv[3], path)
    elif cmd in ("measure", "widths"):
        text = sys.argv[3]
        pts = [float(x) for x in sys.argv[4].split(",")] if len(sys.argv) > 4 else [10.5, 12, 13, 14]
        for wt in (400, 700):
            print("weight %d:" % wt)
            for pt in pts:
                print("   %-6.2f %s → %d px" % (pt, text[:20], measure(text, pt, wt, face=face)[0]))
    else:
        print(__doc__)
