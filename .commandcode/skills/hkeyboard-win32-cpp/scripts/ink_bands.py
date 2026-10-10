# 在指定的竖直区间里，逐行统计「非卡片底色」像素数，输出墨迹带。
# 用法: python bands.py <png> <cardTopY> <cardBotY> [背景色r,g,b]
import sys
from PIL import Image

p = sys.argv[1]
y0 = int(sys.argv[2]); y1 = int(sys.argv[3])
im = Image.open(p).convert("RGB")
W, H = im.size
px = im.load()

# 卡片左右边界：在卡顶下方 6px 处扫白色段
def is_white(c): return c[0] > 250 and c[1] > 250 and c[2] > 250
yy = y0 + 6
runs = []
st = None
for x in range(W):
    if is_white(px[x, yy]):
        if st is None: st = x
    else:
        if st is not None and x - st > 40: runs.append((st, x - 1))
        st = None
if st is not None and W - st > 40: runs.append((st, W - 1))
print("%s %dx%d  card y=[%d,%d]  h=%d" % (p, W, H, y0, y1, y1 - y0 + 1))
print("  white x-runs @y=%d: %s" % (yy, runs))

if len(sys.argv) > 4:
    bg = tuple(int(v) for v in sys.argv[4].split(","))
else:
    bg = px[runs[0][0] + 30, y0 + 6] if runs else (255, 255, 255)
print("  bg = %s" % (bg,))


def diff(a, b): return abs(a[0]-b[0]) + abs(a[1]-b[1]) + abs(a[2]-b[2])


TH = 24
if runs:
    xl, xr = runs[0][0] + 4, runs[-1][1] - 4
else:
    xl, xr = 0, W - 1
rows = []
for y in range(y0, y1 + 1):
    n = 0
    for x in range(xl, xr + 1):
        if diff(px[x, y], bg) > TH: n += 1
    rows.append((y, n))

band = None
for (y, n) in rows:
    if n > 0:
        if band is None: band = [y, y, 0]
        band[1] = y; band[2] = max(band[2], n)
    else:
        if band:
            print("  ink band y %4d-%4d  h=%3d  max=%d" % (band[0], band[1], band[1]-band[0]+1, band[2]))
            band = None
if band:
    print("  ink band y %4d-%4d  h=%3d  max=%d" % (band[0], band[1], band[1]-band[0]+1, band[2]))
print("  (card top=%d bottom=%d)" % (y0, y1))
