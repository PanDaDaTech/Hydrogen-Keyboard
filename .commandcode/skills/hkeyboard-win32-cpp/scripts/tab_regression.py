# -*- coding: utf-8 -*-
"""设置页回归对比：逐 Tab 逐行比对「标题墨迹中心」，输出每行的位移 Δ。

用法：
    python tab_regression.py <改前截图目录> <改后截图目录>

两个目录里应有 hk_probe.ps1 产出的 v3_tab_{general,layout,theme,about}.png。

**为什么不用逐像素 diff**：同一台机器两次运行的逐像素 diff 仍有约 0.3% 噪声
（窗口边缘 / 阴影 / 光标），而且内容完全不同的两个 Tab 差异像素数几乎相同
（实测 759 vs 761）—— 拿它当判据会误判成「改了别的东西」。
墨迹中心是亚像素稳定的：没动的地方 Δ 恰好 0.00。
"""
import sys
import os
import numpy as np
from PIL import Image

DPI = 1.75
TABS = [('general', '常规'), ('layout', '布局'), ('theme', '主题'), ('about', '关于')]


def tabs_center(path):
    """回传 [(墨迹中心 y, 墨迹高), ...]，只取文字列（跳过 tile 与右侧控件）。"""
    a = np.asarray(Image.open(path).convert('RGB')).astype(int)
    dark = (abs(a[:, :, 0] - 35) < 25) & (abs(a[:, :, 1] - 32) < 25) & (abs(a[:, :, 2] - 42) < 25)
    dark[:, :170] = False
    dark[:, 700:] = False
    rows = dark.sum(axis=1)
    out, st = [], None
    for y, v in enumerate(rows):
        if v > 3 and st is None:
            st = y
        elif v <= 3 and st is not None:
            if y - st > 8:
                out.append(((st + y - 1) / 2.0, y - st))
            st = None
    if st is not None:
        out.append(((st + len(rows) - 1) / 2.0, len(rows) - st))
    return out, a.shape


def main(before_dir, after_dir):
    worst = 0.0
    for name, cn in TABS:
        pb = os.path.join(before_dir, 'v3_tab_%s.png' % name)
        pa = os.path.join(after_dir, 'v3_tab_%s.png' % name)
        if not (os.path.exists(pb) and os.path.exists(pa)):
            print('== %s  缺图，跳过' % cn)
            continue
        (b, sb), (a, sa) = tabs_center(pb), tabs_center(pa)
        size = '' if sb == sa else '  窗口 %dx%d → %dx%d' % (sb[1], sb[0], sa[1], sa[0])
        print('== %s  行数 %d → %d%s' % (cn, len(b), len(a), size))
        for i in range(max(len(b), len(a))):
            if i < len(b) and i < len(a):
                dd = a[i][0] - b[i][0]
                if abs(dd) > 1.0:
                    worst = max(worst, abs(dd))
                flag = '   <<< 位移 %+.2f px = %+.2f DIP' % (dd, dd / DPI) if abs(dd) > 1.0 else ''
                print('   行%d  %7.1f → %7.1f   Δ %+6.2f%s' % (i, b[i][0], a[i][0], dd, flag))
            else:
                print('   行%d  %s → %s' % (i, b[i] if i < len(b) else '—', a[i] if i < len(a) else '—'))
    print()
    if worst == 0.0:
        print('[OK] 所有行的标题墨迹中心 Δ 全为 0.00 —— 没有任何非预期的几何位移')
    else:
        print('[注意] 最大位移 %.2f px（应与你这次改动的位置一一对应，多的就是误伤）' % worst)


if __name__ == '__main__':
    if len(sys.argv) < 3:
        print(__doc__)
        sys.exit(1)
    main(sys.argv[1], sys.argv[2])
