import numpy as np
from PIL import Image
import os, glob

A = r"E:/Project/Git/Hydrogen-Keyboard/.workbuddy/shots4"
B = r"E:/Project/Git/Hydrogen-Keyboard/.workbuddy/shots5"

for pa in sorted(glob.glob(os.path.join(A, "*.png"))):
    name = os.path.basename(pa)
    pb = os.path.join(B, name)
    if not os.path.exists(pb):
        print("%-22s MISSING in shots5" % name)
        continue
    a = np.asarray(Image.open(pa).convert("RGB")).astype(int)
    b = np.asarray(Image.open(pb).convert("RGB")).astype(int)
    if a.shape != b.shape:
        print("%-22s SIZE DIFF %s vs %s" % (name, a.shape, b.shape))
        continue
    d = np.abs(a - b).max(axis=2)
    n = int((d > 8).sum())
    if n == 0:
        print("%-22s identical" % name)
    else:
        ys, xs = np.nonzero(d > 8)
        print("%-22s diff px=%6d  bbox y %d..%d  x %d..%d"
              % (name, n, ys.min(), ys.max(), xs.min(), xs.max()))
