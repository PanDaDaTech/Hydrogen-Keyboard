# 关于页「整页竖直间距体检」：把页头/tab/卡片边界/卡内墨迹/版权全部量出来。
# 用法: python about_full.py <png> [渲染DPR]
import sys
from PIL import Image

p = sys.argv[1]
dpi = float(sys.argv[2]) if len(sys.argv) > 2 else 1.75
im = Image.open(p).convert("RGB")
W, H = im.size
px = im.load()


def diff(a, b): return abs(a[0]-b[0]) + abs(a[1]-b[1]) + abs(a[2]-b[2])


BG = (239, 237, 244)

# --- 1. 客户区：从中间列找出「非黑」的 y 范围（截图外面是黑） ---
xprobe = W - 120
ys = [y for y in range(H) if diff(px[xprobe, y], (0, 0, 0)) > 20]
CY0, CY1 = ys[0], ys[-1]
print("%s  %dx%d  客户区 y=[%d,%d] h=%d (%.1f DIP)" % (p, W, H, CY0, CY1, CY1-CY0+1, (CY1-CY0+1)/dpi))

# --- 2. 卡片：靠右列上找「白」或「紫」段 ---
segs = []
cur = None
for y in range(CY0, CY1 + 1):
    c = px[xprobe, y]
    isw = c[0] > 250 and c[1] > 250 and c[2] > 250
    isp = c[2] > 240 and c[0] < 245 and c[2] - c[1] > 20      # 浅紫底
    tag = "WHITE" if isw else ("PURPLE" if isp else None)
    if tag:
        if cur is None or cur[2] != tag:
            if cur: segs.append(cur)
            cur = [y, y, tag]
        else:
            cur[1] = y
    else:
        if cur: segs.append(cur); cur = None
if cur: segs.append(cur)
segs = [s for s in segs if s[1] - s[0] > 20]
print("卡片段:")
for s in segs:
    print("   y %4d-%4d  h=%4d (%6.1f DIP)  %s" % (s[0], s[1], s[1]-s[0]+1, (s[1]-s[0]+1)/dpi, s[2]))

# 元素之间的空白
print("\n空白（相邻卡片之间）:")
for i in range(1, len(segs)):
    g = segs[i][0] - segs[i-1][1] - 1
    print("   %s 底 -> %s 顶 : %4d px (%.2f DIP)" % (segs[i-1][2], segs[i][2], g, g/dpi))


# --- 3. 墨迹带：给定 y 区间与 x 区间，列出行带 ---
def ink_bands(y0, y1, xl, xr, tag, th=24, bgr=(255, 255, 255)):
    out = []
    cur = None
    for y in range(y0, y1 + 1):
        n = 0
        for x in range(xl, xr + 1):
            if diff(px[x, y], bgr) > th: n += 1
        if n > 0:
            if cur is None: cur = [y, y, n]
            cur[1] = y; cur[2] = max(cur[2], n)
        else:
            if cur: out.append(cur); cur = None
    if cur: out.append(cur)
    print("  [%s] y=%d..%d" % (tag, y0, y1))
    for s in out:
        print("     墨迹 y %4d-%4d h=%3d max=%3d" % (s[0], s[1], s[1]-s[0]+1, s[2]))
    return out


# 卡片 x 范围（在卡顶下方 8px 处找）
def card_x(y):
    st = None; best = None
    for x in range(W):
        c = px[x, y]
        w = c[0] > 250 and c[1] > 250 and c[2] > 250
        q = c[2] > 240 and c[0] < 245 and c[2]-c[1] > 20
        if w or q:
            if st is None: st = x
        else:
            if st is not None and x - st > 200: best = (st, x-1)
            if st is not None and x - st > 200: return best
            st = None
    return best or (0, W-1)


for s in segs:
    xl, xr = card_x(s[0] + 8)
    print("\n=== %s 卡 y=[%d,%d]  x=[%d,%d] 宽=%d (%6.1f DIP) 高=%d (%6.1f DIP) ==="
          % (s[2], s[0], s[1], xl, xr, xr-xl+1, (xr-xl+1)/dpi, s[1]-s[0]+1, (s[1]-s[0]+1)/dpi))
    # 图标列 / 文字列
    ic_x = (xl + int(20*dpi), xl + int(20*dpi) + int(44*dpi))          # 身份卡图标
    tx_x = (ic_x[1] + int(18*dpi), xr - int(20*dpi))
    if s[2] == "WHITE" and s[1] - s[0] > 120:
        ink_bands(s[0]+3, s[1]-3, ic_x[0], ic_x[1], "图标列 x=%d..%d" % ic_x)
        ink_bands(s[0]+3, s[1]-3, tx_x[0], tx_x[1], "文字列 x=%d..%d" % tx_x)
    else:
        tl = xl + int(20*dpi)
        tt = tl + int(30*dpi) + int(18*dpi)
        ink_bands(s[0]+2, s[1]-2, tl, tl + int(30*dpi), "tile 列 x=%d..%d" % (tl, tl+int(30*dpi)))
        ink_bands(s[0]+2, s[1]-2, tt, xr - int(60*dpi), "文字列 x=%d..%d" % (tt, xr-int(60*dpi)))
