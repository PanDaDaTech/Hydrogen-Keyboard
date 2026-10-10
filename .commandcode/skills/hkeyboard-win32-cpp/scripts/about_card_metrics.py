# -*- coding: utf-8 -*-
"""关于页身份卡（card1）的像素级测量：卡片上下留白、图标与「标题+描述」是否居中对齐、
标题与描述的间距。几何与 GetAboutLayout / 绘制代码同源。"""
import sys, collections
from PIL import Image

IMG = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\29776\WorkBuddy\2026-09-25-19-32-21\.workbuddy\scratch\v9\shots_about_before\12_about.png"
im = Image.open(IMG).convert("RGB"); px = im.load()
W, H = im.size
dpi = 1.75
print("image %dx%d  dpi=%.2f" % (W, H, dpi))

# ---- 与 GetSettingsMetrics / GetAboutLayout 同源 ----
margin = int(30*dpi); dpi_i = dpi
contentX = margin; contentW = W - margin*2
titleY = int(20*dpi); headIcon = int(38*dpi)
tabsY = titleY + headIcon + int(10*dpi)
tabH = int(42*dpi)
contentY = tabsY + tabH + int(14*dpi)
mark = int(44*dpi)
c1l = contentX; c1t = contentY; c1r = contentX + contentW
# 卡高与绘制偏移同步到最终代码（207b8da）：卡高 78 DIP、图标 +20、标题 +16、描述 +32
CARD1_DIP = 78; ICON_DY = 20; TITLE_DY = 16; DESC_DY = 32
c1b = c1t + int(CARD1_DIP*dpi)
print("card1 = [%d,%d,%d,%d]  高=%d (%.1f DIP)  mark=%d" % (c1l, c1t, c1r, c1b, c1b-c1t, (c1b-c1t)/dpi, mark))
print("  代码里：图标 top = c1t+%d ; 标题 top = c1t+%d ; 描述 top = c1t+%d"
      % (ICON_DY*dpi, TITLE_DY*dpi, DESC_DY*dpi))

# 真实卡顶/底线：整行扫「卡底色」像素占比。圆角会让靠边的单列误判，
# 但一行里卡内的白像素远多于圆角缺口，占比 > 60% 即认定为卡体所在行。
def white_rows(y0, y1, bgc, need=0.60):
    ys = []
    for yy in range(y0, y1):
        n = 0
        for xx in range(c1l, c1r):
            r, g, b = px[xx, yy]
            if abs(r-bgc[0])+abs(g-bgc[1])+abs(b-bgc[2]) <= 6: n += 1
        if n >= (c1r-c1l)*need: ys.append(yy)
    return ys
_wr = white_rows(c1t-int(10*dpi), min(H, c1t+int(120*dpi)), (255,255,255))
# 取从第一行白开始的**连续**段（卡 1 与卡 2 之间有页面底色间隙，不能一路连到卡 2）
if _wr:
    c1t_real = _wr[0]; c1b_real = c1t_real
    for yy in _wr[1:]:
        if yy == c1b_real + 1: c1b_real = yy
        else: break
else:
    c1t_real, c1b_real = c1t, c1b-1
print("  实测卡体 y=[%d,%d] → 实测卡高 %d px (%.1f DIP)  [公式 %d px]"
      % (c1t_real, c1b_real, c1b_real+1-c1t_real, (c1b_real+1-c1t_real)/dpi, c1b-c1t))

def modal(x0, y0, w, h):
    c = collections.Counter()
    for yy in range(y0, y0+h):
        for xx in range(x0, x0+w): c[px[xx, yy]] += 1
    return c.most_common(1)[0][0]

bg = modal(c1l+8, c1t+8, 60, 30)
print("card 底色 =", bg)

def ink_rows(x0, x1, y0, y1, t=60):
    out = []
    for yy in range(y0, y1):
        n = 0
        for xx in range(x0, x1):
            r, g, b = px[xx, yy]
            if abs(r-bg[0])+abs(g-bg[1])+abs(b-bg[2]) > t: n += 1
        out.append((yy, n))
    return out

def spans(rows, minrun=2):
    segs = []; s = None
    for i, (yy, n) in enumerate(rows):
        if n > 0 and s is None: s = yy
        if n == 0 and s is not None:
            if yy - s >= minrun: segs.append((s, yy-1))
            s = None
    if s is not None: segs.append((s, rows[-1][0]))
    return segs

# 图标：图标盒所在列带
ix0 = c1l + int(20*dpi); ix1 = ix0 + mark
icon_segs = spans(ink_rows(ix0, ix1, c1t, c1b))
# 文字：标题/描述所在列带
tx0 = ix0 + mark + int(18*dpi); tx1 = c1r - int(20*dpi)
txt_segs = spans(ink_rows(tx0, tx1, c1t, c1b))

print("\n图标盒 x=[%d,%d]  墨迹行段 = %s" % (ix0, ix1, icon_segs))
print("文字带 x=[%d,%d]  墨迹行段 = %s" % (tx0, tx1, txt_segs))

def mid(seg): return (seg[0]+seg[1])/2.0

if icon_segs:
    ic = icon_segs[0]
    print("\n图标墨迹 y=[%d,%d]  纵心=%.1f  (键顶+%.1f px = %.2f DIP)  高=%d px (%.1f DIP)"
          % (ic[0], ic[1], mid(ic), mid(ic)-c1t, (mid(ic)-c1t)/dpi, ic[1]-ic[0]+1, (ic[1]-ic[0]+1)/dpi))
if len(txt_segs) >= 2:
    t0, t1 = txt_segs[0], txt_segs[1]
    for k, s in enumerate(txt_segs):
        print("文字段%d y=[%d,%d] 纵心=%.1f 高=%d px (%.1f DIP)" % (k, s[0], s[1], mid(s), s[1]-s[0]+1, (s[1]-s[0]+1)/dpi))
    block_mid = (mid(t0)+mid(t1))/2.0
    print("\n>>> 文字块（标题+描述）纵心 = %.1f" % block_mid)
    print(">>> 图标纵心 - 文字块纵心 = %+.1f px (%.2f DIP)  %s"
          % (mid(icon_segs[0])-block_mid, (mid(icon_segs[0])-block_mid)/dpi,
             "OK" if abs(mid(icon_segs[0])-block_mid) <= 3.5 else "!! 不齐"))
    print(">>> 标题底 - 描述顶 = %d px (%.2f DIP)" % (t1[0]-t0[1]-1, (t1[0]-t0[1]-1)/dpi))
    top_pad = t0[0]-c1t; bot_pad = c1b_real+1-t1[1]
    # 卡片留白按「最靠上/最靠下的墨迹」
    topmost = min(t0[0], icon_segs[0][0]); botmost = max(t1[1], icon_segs[0][1])
    up = topmost - c1t_real; dn = c1b_real + 1 - botmost
    print(">>> 卡内上下留白：上 = %d px (%.2f DIP)  下 = %d px (%.2f DIP)  差 = %d px (%.2f DIP)"
          % (up, up/dpi, dn, dn/dpi, up-dn, (up-dn)/dpi))
