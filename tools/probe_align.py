#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""探针 v2：从截图里量「图标 tile 与文字块是否垂直居中对齐」，**自动分行**。

v1 的毛病：整张图当成一条带，键盘截图会算出无意义的 Δ=0（我曾拿它当证据，是错的）。
v2 先按「整行近乎同色」切出分隔行（卡片之间的空白、行之间的 1px 分隔线都满足），
得到的每条带就是一行/一张卡，再在带内比 tile 与文字的垂直中心。

用法：python tools/probe_align.py <图.png> [--tol 2] [--min-band 24]
"""
import sys

try:
    from PIL import Image
except ImportError:
    raise SystemExit("需要 Pillow：pip install pillow")


def content_bands(im, min_band=10, quiet=0.02):
    """按"墨水剖面"切行：以**该行自己的底色**为基准数非底色像素，有内容的行连成一条带。

    为什么不用"整行同色"判分隔线：设置页行之间的 1px 分隔线是抗锯齿的，实际占 2~3 行
    像素、颜色逐行渐变，永远不满足"同色" —— 上一版因此把整张卡并成一条大带。

    每行底色 = 该行出现次数最多的颜色。页面底色、卡片底色都能自适应，不需要事先知道配色。
    """
    px = im.load()
    w, h = im.width, im.height
    step = 2
    # ⚠ 只扫**卡片内部**的横带：卡片是内缩的，两侧的页面底色与该行的卡片底色不同，
    #    若横跨整幅，每一行都会被判成"有内容"，几行又并成一条大带（上一版就是这样）。
    xs = list(range(int(w * 0.12), int(w * 0.88), step))

    def close(a, b, tol=10):
        return all(abs(int(a[i]) - int(b[i])) <= tol for i in range(3))

    has_ink = []
    for y in range(h):
        row = [px[x, y] for x in xs]
        c0 = max(set(row), key=row.count)
        n = sum(0 if close(c, c0) else 1 for c in row)
        has_ink.append(n > max(2, int(len(xs) * quiet)))

    bands, start = [], None
    for y in range(h):
        if has_ink[y] and start is None:
            start = y
        elif not has_ink[y] and start is not None:
            if y - start >= min_band:
                bands.append((start, y))
            start = None
    if start is not None and h - start >= min_band:
        bands.append((start, h))
    return bands


def measure(im, y0, y1):
    """带内：tile = 左侧第一块连通的非背景像素；文字 = 其余。

    ⚠ 背景色取**带内中间那一行自己的底色**（= 卡片底），不能取页面底色 ——
      卡片是内缩的，用页面底色会把整张卡都当成"墨水"，tile/文字全都量不出来。
    """
    px = im.load()
    w = im.width
    xs = list(range(int(w * 0.10), int(w * 0.92), 2))
    mid = [px[x, (y0 + y1) // 2] for x in xs]
    bg = max(set(mid), key=mid.count)

    def is_bg(c, tol=12):
        return all(abs(int(c[i]) - int(bg[i])) <= tol for i in range(3))

    cols = []
    for x in xs:
        cols.append(sum(0 if is_bg(px[x, y]) else 1 for y in range(y0, y1)))
    th = max(2, (y1 - y0) // 6)
    tile_x0 = tile_x1 = None
    i = 0
    while i < len(xs):                            # 左侧第一段连续列 = tile
        if cols[i] >= th:
            tile_x0 = xs[i]
            while i < len(xs) and cols[i] >= 2:
                i += 1
            tile_x1 = xs[min(i, len(xs) - 1)]
            break
        i += 1

    def bbox(xa, xb):
        yy0 = yy1 = xx0 = xx1 = None
        for y in range(y0, y1):
            for x in range(xa, xb):
                if not is_bg(px[x, y]):
                    yy0 = y if yy0 is None else yy0
                    yy1 = y
                    xx0 = x if xx0 is None else min(xx0, x)
                    xx1 = x if xx1 is None else max(xx1, x)
        return None if yy0 is None else (xx0, yy0, xx1, yy1)

    if tile_x0 is None or tile_x1 is None:
        return None
    tile = bbox(tile_x0, tile_x1 + 1)
    text = bbox(tile_x1 + 4, int(w * 0.92))
    return tile, text


def main():
    a = sys.argv[1:]
    if not a:
        raise SystemExit(__doc__)
    path, tol, min_band = a[0], 2, 24
    i = 1
    while i < len(a):
        if a[i] == "--tol":
            tol = int(a[i + 1]); i += 2
        elif a[i] == "--min-band":
            min_band = int(a[i + 1]); i += 2
        else:
            i += 1

    im = Image.open(path).convert("RGB")
    print("探针 v3  %s  (%dx%d)  容差 %dpx" % (path, im.width, im.height, tol))
    bands = content_bands(im, min_band)
    print("自动分行：%d 条带" % len(bands))
    bad = 0
    for (y0, y1) in bands:
        got = measure(im, y0, y1)
        if not got or not got[0] or not got[1]:
            print("  y=%3d..%3d  跳过（找不到 tile 或文字）" % (y0, y1))
            continue
        t, x = got
        tc = (t[1] + t[3]) / 2.0
        xc = (x[1] + x[3]) / 2.0
        d = xc - tc
        ok = abs(d) <= tol
        if not ok:
            bad += 1
        print("  y=%3d..%3d  tile 中心 %6.1f (高%3d)  文字中心 %6.1f (高%3d)  Δ=%+5.1f  %s"
              % (y0, y1, tc, t[3] - t[1], xc, x[3] - x[1], d, "OK" if ok else "FAIL"))
    print("结论：%s" % ("PASS —— 每条带内 tile 与文字垂直居中一致" if bad == 0 else "FAIL —— %d 条带没对齐" % bad))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
