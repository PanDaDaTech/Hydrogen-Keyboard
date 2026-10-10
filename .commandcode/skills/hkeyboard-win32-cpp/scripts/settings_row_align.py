# -*- coding: utf-8 -*-
"""逐行测量设置页：tile 中心 vs 文字块墨迹中心（判定「图标文本对不齐」的唯一硬指标）。

用法：
    python settings_row_align.py <图> <x偏移> <y偏移> <标签>
用户截图带 24px 边框 → 偏移传 24；hk_probe.ps1 的本机截图无边框 → 传 0。

两个必须注意的取样细节：
  · tile 底色 (241,235,253) 与白卡 (255,255,255) 在 G 通道上只差 20，
    **容差给 22 会把整个白卡算成 tile**，于是扫出一整条竖段。容差最多给 12。
  · 页头那颗 38 DIP 的 tile 也在 x<200 的范围内，从 y=200 起扫只取行内 tile。
  · 文字墨迹与描述墨迹的颜色掩膜会互相污染（(32,32,40)±80 与 (96,95,98)±60 有交集），
    所以这里按**行段切分**而不是按颜色分开取，别再把两个 mask 叠加。
"""
import sys
import numpy as np
from PIL import Image

DPI = 1.75


def load(path, ox, oy):
    a = np.asarray(Image.open(path).convert('RGB')).astype(int)
    return a[oy:, ox:, :]


def near(a, c, tol):
    return ((abs(a[:, :, 0] - c[0]) < tol) &
            (abs(a[:, :, 1] - c[1]) < tol) &
            (abs(a[:, :, 2] - c[2]) < tol))


def runs(mask_cols, lo, hi, minlen=3):
    segs, st = [], None
    for i in range(lo, hi):
        if mask_cols[i] > 0 and st is None:
            st = i
        elif mask_cols[i] == 0 and st is not None:
            if i - st >= minlen:
                segs.append((st, i - 1))
            st = None
    if st is not None:
        segs.append((st, hi - 1))
    return segs


def analyze(path, ox, oy, tag, tile_col, ink_col, tile_tol=12, ink_tol=70,
            tile_x=(60, 190), text_x=(230, 900), y0=200, y1=1020):
    a = load(path, ox, oy)
    H, W = a.shape[:2]
    tm = near(a, tile_col, tile_tol)
    im = near(a, ink_col, ink_tol)

    # tile：只看左列区域
    tc = tm[:, tile_x[0]:tile_x[1]].sum(axis=1)
    tile_rows = [y for y in range(y0, min(y1, H)) if tc[y] > 20]
    tiles = []
    st = None
    for y in range(y0, min(y1, H)):
        on = tc[y] > 20
        if on and st is None:
            st = y
        elif not on and st is not None:
            if y - st >= 20:
                tiles.append((st, y - 1))
            st = None

    # 文字墨迹：文本列区域，按行段切
    ic = im[:, text_x[0]:text_x[1]].sum(axis=1)
    bands, st = [], None
    for y in range(y0, min(y1, H)):
        on = ic[y] > 0
        if on and st is None:
            st = y
        elif not on and st is not None:
            if y - st >= 4:
                bands.append((st, y - 1))
            st = None

    print("=== %s   (图像 %dx%d, 偏移 %d,%d)" % (tag, W, H, ox, oy))
    print("    tile 段: %s" % (tiles,))
    print("    文本墨迹行段: %s" % (bands,))
    # 用 tile 段匹配文本：每行 = 标题段 + 描述段
    res = []
    for i, t in enumerate(tiles):
        tcy = (t[0] + t[1]) / 2.0
        # 找落在 tile 垂直邻域内的墨迹段
        cand = [b for b in bands if b[0] >= t[0] - 45 and b[1] <= t[1] + 45]
        if len(cand) < 2:
            cand = [b for b in bands if t[0] - 30 <= (b[0] + b[1]) / 2 <= t[1] + 30]
        if len(cand) < 2:
            print("    tile#%d y=%d..%d 中心%.1f  -> 墨迹段不足: %s" % (i, t[0], t[1], tcy, cand))
            continue
        ti, di = cand[0], cand[1]
        tic = (ti[0] + ti[1]) / 2.0
        dic = (di[0] + di[1]) / 2.0
        blk = (tic + dic) / 2.0
        res.append(tcy - blk)
        print("    tile#%d y=%3d..%3d 中心 %6.1f | 标题 %3d..%3d 中心 %6.1f | 描述 %3d..%3d 中心 %6.1f"
              "  => 块中心 %6.1f  Δ(tile-块) = %+.2f px = %+.2f DIP"
              % (i, t[0], t[1], tcy, ti[0], ti[1], tic, di[0], di[1], dic, blk,
                 tcy - blk, (tcy - blk) / DPI))
    if res:
        print("    ---- 平均 Δ = %+.2f px = %+.2f DIP   (n=%d, 最大 |Δ| %.2f px)"
              % (sum(res) / len(res), sum(res) / len(res) / DPI, len(res),
                 max(abs(r) for r in res)))
    return res


USR = r'C:/Users/29776/AppData/Local/Temp/QQ_1790858151970.png'
NEW = r'settings_new.png'

analyze(USR, 24, 24, '用户截图（旧构建）', (241, 235, 253), (35, 32, 42))
print()
analyze(NEW, 0, 0, '本机新构建', (241, 235, 253), (35, 32, 42))
