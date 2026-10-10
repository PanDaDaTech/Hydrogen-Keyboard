# -*- coding: utf-8 -*-
"""量化校验：键面标签的墨迹中心 vs 键中心。
从 01_default.png 实测每个键内部「墨迹」的水平包围盒，和键的几何中心比较，
直接给出「偏了多少像素」，用来判断是否居中 / 图标与文字是否成组对齐。"""
import sys, collections
from PIL import Image

IMG = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\29776\WorkBuddy\2026-09-25-19-32-21\.workbuddy\scratch\v9\shots\01_default.png"
im = Image.open(IMG).convert("RGB")
W, H = im.size
px = im.load()

# ---- 与 hkeyboard.cpp 同源：探针日志给出 u / kax / gap / KH / 行顶 ----
u    = 106.9565217391304
kax  = 32
gap  = 7
KH   = 86
ROWS = [84, 177, 270, 363, 456]      # 行顶（px）

def key_rect(col, w_u, row):
    xa = kax + round(col * u)
    xb = kax + round((col + w_u) * u)
    return xa, ROWS[row], xb - xa - gap, KH

# ---- 行表（col, w, 名字）----
ROW0 = [(0.00,1.00,"Esc")] + \
       [(1.00+i,1.00,ch) for i,ch in enumerate("`1234567890-=")] + \
       [(14.00,1.50,"Backspace")]
ROW1 = [(0.00,1.50,"Tab")] + \
       [(1.50+i,1.00,ch) for i,ch in enumerate("qwertyuiop")] + \
       [(11.50,1.00,"["),(12.50,1.00,"]"),(13.50,1.00,"\\"),(14.50,1.00,"Del")]
ROW2 = [(0.00,2.00,"Caps")] + \
       [(2.00+i,1.00,ch) for i,ch in enumerate("asdfghjkl")] + \
       [(11.00,1.00,";"),(12.00,1.00,"'"),(13.00,2.50,"Enter")]
ROW3 = [(0.00,2.50,"LShift")] + \
       [(2.50+i,1.00,ch) for i,ch in enumerate("zxcvbnm")] + \
       [(9.50,1.00,","),(10.50,1.00,"."),(11.50,1.00,"/"),(12.50,1.00,"Up"),(13.50,2.00,"RShift")]
ROW4 = [(0.00,1.00,"Ctrl"),(1.00,1.00,"Fn"),(2.00,1.00,"Win"),(3.00,1.00,"Alt"),
        (4.00,5.50,"Space"),(9.50,1.00,"Menu"),(10.50,1.00,"Alt"),
        (11.50,1.00,"Left"),(12.50,1.00,"Down"),(13.50,1.00,"Right"),(14.50,1.00,"Ctrl")]
TABLES = [ROW0, ROW1, ROW2, ROW3, ROW4]

def modal_color(x0, y0, w, h):
    c = collections.Counter()
    for yy in range(y0+3, y0+h-3):
        for xx in range(x0+3, x0+w-3):
            c[px[xx, yy]] += 1
    return c.most_common(1)[0][0]

def ink_box(x0, y0, w, h, bg, thresh=70):
    minx, maxx = None, None
    for yy in range(y0+6, y0+h-6):
        for xx in range(x0+6, x0+w-6):
            r, g, b = px[xx, yy]
            d = abs(r-bg[0]) + abs(g-bg[1]) + abs(b-bg[2])
            if d > thresh:
                if minx is None or xx < minx: minx = xx
                if maxx is None or xx > maxx: maxx = xx
    return minx, maxx

print("%-12s %8s %8s %8s %7s   %s" % ("key", "keyCx", "inkL", "inkR", "dCx", "判定"))
print("-" * 78)
rows_out = []
worst = []
for ri, table in enumerate(TABLES):
    for (col, wu, name) in table:
        x0, y0, w, h = key_rect(col, wu, ri)
        if w < 12: continue
        bg = modal_color(x0, y0, w, h)
        minx, maxx = ink_box(x0, y0, w, h, bg)
        if minx is None:
            continue
        keyC = x0 + w / 2.0
        inkC = (minx + maxx) / 2.0
        d = inkC - keyC
        # 居中键：|d| 大 = 没居中；左右对齐键：只报告不做居中判定
        rows_out.append((ri, name, col, wu, w, keyC, minx, maxx, d))
        if abs(d) >= 1.5:
            worst.append((abs(d), ri, name, d, w))
print("%-12s %8s %8s %8s %7s" % ("(全部)", "", "", "", ""))
for (ri, name, col, wu, w, keyC, minx, maxx, d) in rows_out[:0]:
    pass

# 详细打印：居中的字母/数字键单独一组
# 贴边键（KA_LEFT / KA_RIGHT）——必须与 hkeyboard.cpp 的 LayoutKey.align 同源。
# 注意 Backspace（0x08, col 14.00, 1.5u）与 Del（0x2E, col 14.50）都是 KA_RIGHT；
# 早先版本按「名字黑名单」判居中，把 Backspace 误当居中键，报出 +13.5 的假偏差。
PAD_LEFT  = {(0,"Esc"), (1,"Tab"), (2,"Caps"), (3,"LShift")}
PAD_RIGHT = {(0,"Backspace"), (1,"Del"), (2,"Enter"), (3,"RShift")}
PAD_KEYS = PAD_LEFT | PAD_RIGHT

print("\n【居中键】字母/数字（KA_CENTER，理想 dCx = 0）")
for (ri, name, col, wu, w, keyC, minx, maxx, d) in rows_out:
    centered = (ri in (0,1,2,3) and name not in ("Esc","Tab","Del","Caps","Enter","LShift","RShift","Up")) \
               or (ri == 4 and name in ("Fn","Win","Alt","Menu","Left","Down","Right","Ctrl"))
    if (ri, name) in PAD_KEYS: continue
    if not centered: continue
    flag = "OK" if abs(d) < 1.5 else ("=偏右%.1f" % d if d > 0 else "偏左%.1f" % -d)
    print("  r%d %-6s col=%5.2f w=%5.1f  键心=%7.1f  墨迹[%7.1f,%7.1f]  d=%+6.1f  %s"
          % (ri, name, col, w, keyC, minx, maxx, d, flag))

print("\n【对齐键】左右贴边（KA_LEFT / KA_RIGHT）— 贴边留白应两侧一致（≈ edge 6 DIP = 10.5px + 抗锯齿）")
for (ri, name, col, wu, w, keyC, minx, maxx, d) in rows_out:
    if (ri, name) not in PAD_KEYS: continue
    side = "左" if (ri, name) in PAD_LEFT else "右"
    x0 = key_rect(col, wu, ri)[0]
    padL = minx - x0
    padR = (x0 + w) - maxx
    pad  = padL if side == "左" else padR
    print("  r%d %-8s %s对齐  w=%5.1f  左侧空=%6.1f  右侧空=%6.1f  → 贴边留白=%5.1f  %s"
          % (ri, name, side, w, padL, padR, pad,
             "OK" if 8 <= pad <= 18 else "!! 检查"))
