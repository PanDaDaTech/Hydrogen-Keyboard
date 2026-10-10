# 沿指定列扫颜色游程，用来一眼看清「卡片边界 / 墨迹带」的纵向结构。
# 用法: python col_runs.py <png> <x> [组容差] [最少行数]
import sys
from PIL import Image

p = sys.argv[1]
x = int(sys.argv[2])
tol = int(sys.argv[3]) if len(sys.argv) > 3 else 10
minrun = int(sys.argv[4]) if len(sys.argv) > 4 else 2

im = Image.open(p).convert("RGB")
W, H = im.size
px = im.load()
print("%s  %dx%d   column x=%d" % (p, W, H, x))


def d(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])


runs = []
cur = px[x, 0]
start = 0
for y in range(1, H):
    c = px[x, y]
    if d(c, cur) > tol:
        runs.append((start, y - 1, cur))
        cur = c
        start = y
runs.append((start, H - 1, cur))

for (a, b, c) in runs:
    if b - a + 1 < minrun and not (a == 0 or b == H - 1):
        continue
    print("  y %4d - %4d  h=%4d   rgb(%3d,%3d,%3d)" % (a, b, b - a + 1, c[0], c[1], c[2]))
