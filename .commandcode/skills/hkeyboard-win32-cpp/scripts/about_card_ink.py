import numpy as np
from PIL import Image
import sys

DPR = 1.7368
p = r"E:/Project/Git/Hydrogen-Keyboard/.workbuddy/shots5/12_about.png"
im = Image.open(p).convert("RGB")
a = np.asarray(im).astype(int)
H, W, _ = a.shape
print("size", W, H, "DPR", DPR)

g = a.mean(axis=2)

# ---- 1. 用多列投票找 card1 的白色卡区 ----
white = g > 250
votes = white.sum(axis=1)
cand = [y for y in range(120, min(H, 420)) if votes[y] > W * 0.30]
# 连续段
runs = []
s = None
prev = None
for y in cand:
    if s is None:
        s = y
    elif y - prev > 2:
        runs.append((s, prev))
        s = y
    prev = y
if s is not None:
    runs.append((s, prev))
print("white runs (y):", runs)
CT, CB = max(runs, key=lambda r: r[1] - r[0]) if runs else (215, 360)
CH = CB - CT + 1
ch = CH / DPR
c = ch / 2.0
print("card1 top=%d bot=%d  h=%.2f DIP  center=%.2f" % (CT, CB, ch, c))

# ---- 2. 图标：蓝色谓词 ----
R, G, B = a[:, :, 0], a[:, :, 1], a[:, :, 2]
blue = (B > 180) & (B - R > 60) & (B - G > 40)
rows_blue = blue[CT:CB + 1].sum(axis=1)
nz = np.nonzero(rows_blue)[0]
if len(nz):
    iy0, iy1 = CT + nz[0], CT + nz[-1]
    cols = blue[iy0:iy1 + 1].sum(axis=0)
    nzc = np.nonzero(cols)[0]
    ix0, ix1 = nzc[0], nzc[-1]
    iw = (ix1 - ix0 + 1) / DPR
    ih = (iy1 - iy0 + 1) / DPR
    icx = ((ix0 + ix1 + 1) / 2.0 - 0) / DPR
    icy = ((iy0 + iy1 + 1) / 2.0 - CT) / DPR
    print("icon ink: x %d..%d (%.2f DIP)  y %d..%d (%.2f DIP)  centerx=%.2f centery=%.2f"
          % (ix0, ix1, iw, iy0, iy1, ih, icx, icy))
else:
    print("icon blue not found")
    ix0 = ix1 = None

# ---- 3. 文字墨迹（排除图标右侧） ----
# 图标蓝色墨迹右缘之后留 6px 余量，避开抗锯齿灰边
X0 = (ix1 + 7) if ix1 is not None else 190
ink = g < 235
ink[:, :X0] = False
ink[:CT, :] = False
ink[CB + 1:, :] = False
rows = ink.sum(axis=1)
runs = []
s = None
prev = None
for i, v in enumerate(rows):
    y = i
    if v > 0:
        if s is None:
            s = y
        prev = y
    else:
        if s is not None and y - prev > 1:
            runs.append((s, prev))
            s = None
        elif s is None:
            pass
if s is not None:
    runs.append((s, prev))
runs = [r for r in runs if r[1] - r[0] >= 1]
print("ink row runs:", [(int(a1), int(b1), round((b1 - a1 + 1) / DPR, 2)) for a1, b1 in runs])

if len(runs) >= 2:
    t0, t1 = runs[0]
    d0, d1 = runs[-1]
    th = (t1 - t0 + 1) / DPR
    dh = (d1 - d0 + 1) / DPR
    tC = ((t0 + t1 + 1) / 2.0 - CT) / DPR
    dC = ((d0 + d1 + 1) / 2.0 - CT) / DPR
    gap = (d0 - t1 - 1) / DPR
    cd = dC - tC
    print("title  ink h=%.2f  center=%+.2f" % (th, tC))
    print("desc   ink h=%.2f  center=%+.2f" % (dh, dC))
    print("gap=%.2f   center-dist=%.2f" % (gap, cd))
    print("topPad=%.2f   botPad=%.2f" % (tC - th / 2, c - (dC + dh / 2)))

# ---- 4. 图文间距 ----
if ix1 is not None and runs:
    tx_cols = np.nonzero(ink[CT:CB + 1].sum(axis=0) > 0)[0]
    if len(tx_cols):
        tix0 = tx_cols[0]
        gappx = tix0 - ix1 - 1
        print("icon-to-text gap = %.2f DIP  (ink edge icon x=%d, text x=%d)"
              % (gappx / DPR, ix1, tix0))
