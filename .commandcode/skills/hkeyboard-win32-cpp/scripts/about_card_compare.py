# -*- coding: utf-8 -*-
# 关于页身份卡「改前 / 改后」对照：把卡顶底、图标墨迹、文字墨迹的上下边界画出来，
# 并在右侧标出留白数值，直接看「上下留白是否相等」。
from PIL import Image, ImageDraw, ImageFont

V9 = r'C:\Users\29776\WorkBuddy\2026-09-25-19-32-21\.workbuddy\scratch\v9'
PAIRS = [
    ('BEFORE', V9 + r'\shots_about2_before\12_about.png'),
    ('AFTER', V9 + r'\shots\12_about.png'),
]
DPI = 1.75
CROP = (52, 198, 664, 368)


def diff(a, b):
    return abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2])


def segs(px, ct, cb, x0, x1, bg=(255, 255, 255)):
    out, cur = [], None
    for y in range(ct + 3, cb - 2):
        n = sum(1 for x in range(x0, x1) if diff(px[x, y], bg) > 24)
        if n:
            if cur is None:
                cur = [y, y]
            cur[1] = y
        else:
            if cur:
                out.append(cur); cur = None
    if cur:
        out.append(cur)
    return out


def measure(path):
    im = Image.open(path).convert('RGB'); px = im.load()
    xp = 1150
    ys = [y for y in range(im.height) if all(v > 250 for v in px[xp, y])]
    ct = ys[0]
    cb = ct
    for y in ys[1:]:                      # 只取「从卡顶起的第一段连续白」，避免跨到下一张卡
        if y == cb + 1:
            cb = y
        else:
            break
    yq = ct + 6
    xs = [x for x in range(im.width) if all(v > 250 for v in px[x, yq])]
    cl, cr = xs[0], xs[-1]
    ic = (cl + int(20 * DPI), cl + int(20 * DPI) + int(44 * DPI))
    tx = (ic[1] + int(18 * DPI), cr - int(20 * DPI))
    return im, ct, cb, segs(px, ct, cb, *ic), segs(px, ct, cb, *tx)


try:
    F = ImageFont.truetype(r'C:\Windows\Fonts\arial.ttf', 15)
    FB = ImageFont.truetype(r'C:\Windows\Fonts\arial.ttf', 17)
except Exception:
    F = FB = ImageFont.load_default()

panels = []
for (tag, path) in PAIRS:
    im, ct, cb, ib, tb = measure(path)
    crop = im.crop(CROP)
    d = ImageDraw.Draw(crop)
    ox, oy = CROP[0], CROP[1]

    def line(y, col, lab):
        yy = y - oy
        d.line([(0, yy), (crop.width - 1, yy)], fill=col, width=1)
        d.text((3, yy - 15 if yy > 16 else yy + 2), lab, fill=col, font=F)

    line(ct, (220, 40, 40), 'card top')
    line(cb, (220, 40, 40), 'card bottom')
    for s in ib:
        line(s[0], (240, 140, 0), 'icon ink top')
        line(s[1], (240, 140, 0), 'icon ink bottom')
    for s in tb:
        line(s[0], (0, 150, 0), 'text ink top')
        line(s[1], (0, 150, 0), 'text ink bottom')

    it, ibt = ib[0][0], ib[-1][1]
    tt, tbt = tb[0][0], tb[-1][1]
    d.rectangle([crop.width - 250, 4, crop.width - 4, 96], fill=(255, 255, 255),
                outline=(180, 180, 190))
    d.text((crop.width - 244, 8), '%s   card h=%dpx' % (tag, cb - ct + 1), fill=(20, 20, 20), font=FB)
    d.text((crop.width - 244, 30), 'icon pad  top %d / bottom %d' % (it - ct, cb - ibt), fill=(200, 110, 0), font=F)
    d.text((crop.width - 244, 50), 'text pad  top %d / bottom %d' % (tt - ct, cb - tbt), fill=(0, 130, 0), font=F)
    d.text((crop.width - 244, 70), 'icon cy vs card cy : %+.1f px' % ((it + ibt) / 2 - (ct + cb) / 2), fill=(20, 20, 20), font=F)
    panels.append(crop)

W = sum(p.width for p in panels) + 12 * (len(panels) - 1)
H = max(p.height for p in panels) + 34
out = Image.new('RGB', (W, H), (246, 246, 248))
x = 0
for p in panels:
    out.paste(p, (x, 34)); x += p.width + 12
d = ImageDraw.Draw(out)
d.text((6, 8), 'About identity card - vertical padding (cyan=icon ink, green=text ink, red=card edges)', fill=(20, 20, 20), font=FB)
out.save(V9 + r'\cmp_about_card.png')
print('saved cmp_about_card.png', out.size)
