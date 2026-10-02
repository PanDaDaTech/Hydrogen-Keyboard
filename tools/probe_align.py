#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""探针：从**截图**里量「图标 tile 与文字块到底有没有对齐」。

之前几轮我一直在用肉眼估截图，估偏几个像素就把结论带歪（这是实打实犯过的错）。
这个脚本只认像素：找出每个行/卡片里的「图标 tile」（一块彩色圆角方块）与
「文字墨迹」，比较两者的**垂直中心**，差超过阈值就 FAIL。

用法：
    python tools/probe_align.py <截图.png> [--row y0 y1] [--tol 2]
        <截图.png>   要分析的图（PNG/JPG）
        --row y0 y1  只看这一条水平带（某一行/某张卡的纵向范围），可重复
        --tol N      允许的中心偏差（像素，默认 2）

输出：每一带里 tile 的 bbox/中心、文字墨迹的 bbox/中心、两者的 Δ，以及 PASS/FAIL。
"""
import sys

try:
    from PIL import Image
except ImportError:
    raise SystemExit("需要 Pillow：pip install pillow")


def find_tile_and_text(im, y0, y1, x0=0, x1=None):
    """在 y0..y1 这条带里找 tile（横向最靠左的成片彩色块）与文字墨迹。"""
    px = im.load()
    x1 = im.width if x1 is None else x1
    bg = px[x0 + 2, (y0 + y1) // 2]          # 带内的背景色（卡片底）

    def is_bg(c, tol=10):
        return all(abs(int(c[i]) - int(bg[i])) <= tol for i in range(3))

    # tile：从左边找第一段连续的非背景列，宽度 > 8px 视为 tile
    tile_x0 = tile_x1 = None
    col_run = 0
    for x in range(x0, x1):
        n = sum(0 if is_bg(px[x, y]) else 1 for y in range(y0, y1))
        if n >= (y1 - y0) // 5:
            col_run += 1
            if tile_x0 is None and col_run >= 3:
                tile_x0 = x - 2
        else:
            if tile_x0 is not None and tile_x1 is None and col_run == 0:
                tile_x1 = x
                break
            col_run = 0
    if tile_x0 is None:
        return None
    if tile_x1 is None:
        tile_x1 = x1

    def ink_bbox(xA, xB):
        yy0, yy1, xx0, xx1 = None, None, None, None
        for y in range(y0, y1):
            for x in range(xA, xB):
                if not is_bg(px[x, y]):
                    yy0 = y if yy0 is None else yy0
                    yy1 = y
                    xx0 = x if xx0 is None else min(xx0, x)
                    xx1 = x if xx1 is None else max(xx1, x)
        return None if yy0 is None else (xx0, yy0, xx1, yy1)

    tile = ink_bbox(tile_x0, tile_x1)
    text = ink_bbox(tile_x1 + 4, x1)
    return tile, text


def main():
    args = sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)
    path = args[0]
    tol = 2
    rows = []
    i = 1
    while i < len(args):
        if args[i] == "--tol":
            tol = int(args[i + 1]); i += 2
        elif args[i] == "--row":
            rows.append((int(args[i + 1]), int(args[i + 2]))); i += 3
        else:
            i += 1

    im = Image.open(path).convert("RGB")
    if not rows:
        rows = [(0, im.height)]
    print("截图探针 %s  (%dx%d)  容差 %dpx" % (path, im.width, im.height, tol))
    fail = False
    for (y0, y1) in rows:
        got = find_tile_and_text(im, y0, y1)
        print("-" * 62)
        print("  纵向带 y=%d..%d" % (y0, y1))
        if not got or not got[0] or not got[1]:
            print("  找不到 tile 或文字（换一段带，或图里没有这行）")
            continue
        t, x = got
        tc = (t[1] + t[3]) / 2.0
        xc = (x[1] + x[3]) / 2.0
        d = xc - tc
        print("  tile  bbox x=%d..%d y=%d..%d   中心y=%.1f  高=%d" % (t[0], t[2], t[1], t[3], tc, t[3] - t[1]))
        print("  文字  bbox x=%d..%d y=%d..%d   中心y=%.1f  高=%d" % (x[0], x[2], x[1], x[3], xc, x[3] - x[1]))
        print("  垂直中心差 Δ = %+.1f px  %s" % (d, "PASS" if abs(d) <= tol else "FAIL"))
        if abs(d) > tol:
            fail = True
    print("-" * 62)
    print("结论：%s" % ("FAIL —— 有行没对齐" if fail else "PASS —— 各带内 tile 与文字垂直居中一致"))
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
