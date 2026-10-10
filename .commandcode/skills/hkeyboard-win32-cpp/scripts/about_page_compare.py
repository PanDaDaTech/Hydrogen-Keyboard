# -*- coding: utf-8 -*-
# 关于页整页「改前 / 改后」对照：标出 tab底线 / 卡1 / 卡间距 / 卡2 / 版权 的间距节奏。
from PIL import Image, ImageDraw, ImageFont

V9 = r'C:\Users\29776\WorkBuddy\2026-09-25-19-32-21\.workbuddy\scratch\v9'
PAIRS = [
    ('BEFORE (f8c6881)', V9 + r'\shots_about2_before\12_about.png'),
    ('AFTER (690931e)', V9 + r'\shots\12_about.png'),
]
DPI = 1.75


def white_runs(px, W, H, xp):
    runs, st = [], None
    for y in range(H):
        w = all(v > 250 for v in px[xp, y])
        if w and st is None:
            st = y
        elif not w and st is not None:
            if y - st > 8:
                runs.append((st, y - 1))
            st = None
    if st is not None and H - st > 8:
        runs.append((st, H - 1))
    return runs


try:
    F = ImageFont.truetype(r'C:\Windows\Fonts\arial.ttf', 14)
    FB = ImageFont.truetype(r'C:\Windows\Fonts\arial.ttf', 16)
except Exception:
    F = FB = ImageFont.load_default()

panels = []
for (tag, path) in PAIRS:
    im = Image.open(path).convert('RGB'); px = im.load()
    runs = white_runs(px, im.width, im.height, 1150)
    card1, card2 = runs[0], runs[1] if len(runs) > 1 else runs[0]
    d = ImageDraw.Draw(im)

    def hl(y, col, lab):
        d.line([(0, y), (im.width - 1, y)], fill=col, width=1)
        d.text((6, y + 2), lab, fill=col, font=F)

    hl(card1[0], (220, 40, 40), 'card1 top')
    hl(card1[1], (220, 40, 40), 'card1 bottom')
    hl(card2[0], (0, 90, 220), 'card2 top')
    hl(card2[1], (0, 90, 220), 'card2 bottom')

    gap = card2[0] - card1[1] - 1
    d.rectangle([im.width - 300, 4, im.width - 6, 74], fill=(255, 255, 255), outline=(170, 170, 180))
    d.text((im.width - 294, 8), tag, fill=(20, 20, 20), font=FB)
    d.text((im.width - 294, 32), 'card1->card2 gap : %d px (%.0f DIP)' % (gap, gap / DPI),
           fill=(200, 110, 0), font=F)
    d.text((im.width - 294, 52), 'window height    : %d px' % im.height, fill=(20, 20, 20), font=F)
    panels.append(im)

W = sum(p.width for p in panels) + 14
H = max(p.height for p in panels) + 34
out = Image.new('RGB', (W, H), (246, 246, 248))
x = 0
for p in panels:
    out.paste(p, (x, 34)); x += p.width + 14
d = ImageDraw.Draw(out)
d.text((8, 8), 'About page - card gap 12 -> 16 DIP  (orange = card1 bottom -> card2 top)', fill=(20, 20, 20), font=FB)
out.save(V9 + r'\cmp_about_page.png')
print('saved cmp_about_page.png', out.size)
