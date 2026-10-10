"""设置页「折行行居中」体检。

用法：
    python settings_row_center.py <11_settings.png> [DPR]

打印每行的：行高 / tile 中心 / 文字块心 / 行心 / 顶底留白，
以及 tile↔文字块、开关↔tile 的中心差。折行行的正确形态是
「三者都等于行心、顶底留白各约 12 DIP」。

判据（实测，布局页）：
    row1 键盘布局    行高 63.33  tile 31.09  块心 31.38  行心 31.67   OK
    row3 小键盘按钮  行高 81.18  tile 23.61  块心 32.82  行心 40.59   BAD（块偏上 7.8）
"""
import sys
import numpy as np
from PIL import Image

path = sys.argv[1]
DPR = float(sys.argv[2]) if len(sys.argv) > 2 else 1.7368

a = np.asarray(Image.open(path).convert("RGB")).astype(int)
g = a.mean(axis=2)
H, W, _ = a.shape

# 卡区：取 y=400 那一行的白段作左右边界
white = np.nonzero(g[400] > 250)[0]
CX0, CX1 = int(white.min()), int(white.max())

# 卡的上下边界：x = CX0 + 18（卡内左侧空白，**已越过圆角**；用中点列会被文字打断）
col = g[:, CX0 + 18]
card_rows = np.nonzero(col > 250)[0]
CT, CB = int(card_rows.min()), int(card_rows.max())

# 分隔线：卡内中段的长横线。⚠ 必须限制在 [CT, CB] 内 ——
# 否则窗口外的页面背景也会被当成分隔线，行边界全被污染。
seg_ys = [y for y in range(CT + 4, CB - 3)
          if int((g[y, CX0 + 80:CX1 - 200] < 248).sum()) > (CX1 - 200 - CX0 - 80) * 0.8]
bounds = [CT] + seg_ys + [CB + 1]
rows = [(bounds[i], bounds[i + 1] - 1) for i in range(len(bounds) - 1)]
rows = [(t, b) for t, b in rows if b - t > int(20 * DPR)]

TILE_R = CX0 + int(20 * DPR) + int(46 * DPR)


def text_bands(t, b, thr=235, min_gap=3):
    """文字列墨迹带。丢掉紧贴行边的（分隔线 / 卡边残留）。"""
    ink = g < thr
    prof = ink[t:b + 1, TILE_R:CX1 - int(20 * DPR)].sum(axis=1)
    out, s = [], None
    for k, v in enumerate(prof):
        y = t + k
        if v > 0:
            if s is None:
                s = y
        elif s is not None and y - s > min_gap:
            out.append((s, y - 1))
            s = None
    if s is not None:
        out.append((s, t + len(prof) - 1))
    return [x for x in out if x[0] - t > 4 and b - x[1] > 4]


def tile_span(t, b):
    """tile 背景（非白）纵向范围，限定在 tile 列内。

    ⚠ 必须**掐掉行首几行**：分隔线就画在行边界上，跨过整行宽度，
    在 tile 列里同样是「非白」，会被框进 tile 导致中心虚高 8.64 DIP
    （分隔线 1~2 px + 阈值边缘）。行尾不掐 —— tile 是居中元素，尾行不会有分隔线。
    """
    lo, hi = CX0 + int(18 * DPR), CX0 + int(52 * DPR)
    nw = (g[t:b + 1, lo:hi] < 249).sum(axis=1)
    t += 3
    nw = nw[3:]
    nz = np.nonzero(nw > 5)[0]
    return (t + int(nz[0]), t + int(nz[-1])) if len(nz) else None


def switch_center(t, b):
    """开关本体中心。

    ⚠ **不能**用「右侧窗口内的深色像素」—— 那个窗口同时框住了「开/关」文字、
    开关和分隔线，三者混在一起算出的中心没有意义（实测 row2/row3 都从分隔线
    那一行起算，量出 22.46 这种假值）。
    开关是**彩色**的（开=蓝，关=灰蓝），用「蓝味」谓词单独提取：
    灰度开关的 B−R 约 8~14，蓝色开关约 40+，都比文字（<20 但低饱和）更蓝。
    这里用 B−R > 18 抓彩色部分，抓不到就退回深色（兼容未来换配色）。
    """
    lo, hi = CX1 - int(120 * DPR), CX1 - int(24 * DPR)
    R = a[t:b + 1, lo:hi, 0]
    B = a[t:b + 1, lo:hi, 2]
    colored = (B - R > 18) & (B > 120)
    if colored.sum() > 20:
        mask = colored
    else:
        # 「关」态是**低饱和**灰蓝（B−R 只有 8~14），上面的谓词抓不到。
        # 放宽到 B−R > 5 且不是纯灰（B−R > 5 已经排除了中性灰的文字）。
        weak = (B.astype(int) - R > 5) & (B > 100)
        mask = weak if weak.sum() > 20 else (g[t:b + 1, lo:hi] < 240)
    nz = np.nonzero(mask.sum(axis=1))[0]
    return (t + int(nz[0]) + t + int(nz[-1]) + 1) / 2 if len(nz) else None


print("card x %d..%d  y %d..%d  separators %s" % (CX0, CX1, CT, CB, seg_ys))
print("DPR %.4f\n" % DPR)

for i, (t, b) in enumerate(rows, 1):
    h = (b - t + 1) / DPR
    rowc = (b - t + 1) / 2 / DPR
    ts = tile_span(t, b)
    tb = text_bands(t, b)
    sw = switch_center(t, b)

    print("row%d  y %d..%d  h=%.2f DIP  行心 %.2f" % (i, t, b, h, rowc))
    tc = ((ts[0] + ts[1] + 1) / 2 - t) / DPR if ts else None
    if ts:
        print("   tile   中心 %6.2f  | 相对行心 %+6.2f" % (tc, tc - rowc))
    if tb:
        s, e = tb[0][0], tb[-1][1]
        bc = ((s + e + 1) / 2 - t) / DPR
        print("   文字块 中心 %6.2f  | 相对行心 %+6.2f   距行顶 %5.2f  距行底 %5.2f"
              % (bc, bc - rowc, (s - t) / DPR, (b - e) / DPR))
        for (s2, e2) in tb:
            print("        段 y %3d..%3d  h=%5.2f  段心 %6.2f"
                  % (s2, e2, (e2 - s2 + 1) / DPR, ((s2 + e2 + 1) / 2 - t) / DPR))
        if tc is not None:
            print("   => 文字块 − tile = %+.2f DIP（应 ≈ 0）" % (bc - tc))
    if sw is not None and ts:
        print("   => 开关 − tile   = %+.2f DIP（应 ≈ 0）" % ((sw - t) / DPR - tc))
    print()
