# -*- coding: utf-8 -*-
"""按 HKeyboard 源码几何，把真实截图里的 Row4 按键像素块原样重排，生成示例图。
所有几何公式与 hkeyboard.cpp BuildKeys() 逐行对应（dpiScale=1.75, 1715x560）。"""
import sys
from PIL import Image, ImageDraw, ImageFont

SRC = r'../v8/final/01_default.png'
OUT_CLEAN = 'mock_layout0_after.png'
OUT_CMP = 'mock_row4_compare.png'

img = Image.open(SRC).convert('RGB')
W, H = img.size
dpi = 1.75
gw, gh = W, H

keyAreaX = int(10 * dpi)
KAW = gw - keyAreaX * 2
gap = int(4 * dpi)
padV = int(10 * dpi)
hdrBot = int((10 + 28) * dpi)
rows = 5
vAvail = (gh - padV) - (hdrBot + padV)
vAvail -= (rows - 1) * gap
keyH = vAvail // rows
vRem = vAvail - keyH * rows
y0 = hdrBot + padV + vRem // 2
row4y = y0 + 4 * (keyH + gap)

wFn = int(46 * dpi); wCtl = int(56 * dpi); wWin = int(46 * dpi)
wAlt = int(58 * dpi); wMenu = int(56 * dpi); wArw = int(52 * dpi)

print(f'geometry: keyAreaX={keyAreaX} KAW={KAW} gap={gap} keyH={keyH} y0={y0} row4y={row4y}')

# ---- 旧顺序（hkeyboard.cpp L1189-1197）----
old_left = wFn + wCtl + wWin + wAlt + wAlt + wMenu + wCtl
spaceW = KAW - old_left - wArw * 3 - 10 * gap
print(f'spaceW={spaceW}')
OLD_NAMES = ['Fn', 'Ctrl', 'Win', 'Alt', 'Space', 'Alt', 'Menu', 'Ctrl', 'L', 'D', 'R']
OLD_W = [wFn, wCtl, wWin, wAlt, spaceW, wAlt, wMenu, wCtl, wArw, wArw, wArw]

# ---- 新顺序（图1 + 用户箭头：Menu 移到空格右侧、右Alt 前；右Ctrl 移到最右）----
NEW_NAMES = ['Ctrl', 'Fn', 'Win', 'Alt', 'Space', 'Menu', 'Alt', 'L', 'D', 'R', 'Ctrl']
NEW_W = [wCtl, wFn, wWin, wAlt, spaceW, wMenu, wAlt, wArw, wArw, wArw, wCtl]
# 新位置 i 的像素来自旧位置 SRC_OF[i]
SRC_OF = [1, 0, 2, 3, 4, 6, 5, 8, 9, 10, 7]

assert sum(OLD_W) + 10 * gap == KAW, (sum(OLD_W) + 10 * gap, KAW)
assert sum(NEW_W) + 10 * gap == KAW, (sum(NEW_W) + 10 * gap, KAW)
for i, s in enumerate(SRC_OF):
    assert NEW_W[i] == OLD_W[s], (i, s, NEW_W[i], OLD_W[s])

def xs(widths):
    out, x = [], keyAreaX
    for w in widths:
        out.append(x); x += w + gap
    return out

old_x = xs(OLD_W)
new_x = xs(NEW_W)

mode = sys.argv[1] if len(sys.argv) > 1 else 'frame'

if mode == 'frame':
    # 自检：在原图上把算出来的 Row4 键框画出来
    chk = img.copy()
    d = ImageDraw.Draw(chk)
    for i, x in enumerate(old_x):
        d.rectangle([x, row4y, x + OLD_W[i] - 1, row4y + keyH - 1], outline=(255, 0, 0), width=2)
        d.text((x + 4, row4y + 4), OLD_NAMES[i], fill=(200, 0, 0))
    chk.save('geom_check.png')
    print('saved geom_check.png')
    sys.exit(0)

# ---- 搬运 ----
bg = img.getpixel((keyAreaX + 4, row4y - 3))   # Row3/Row4 之间的纯背景色
print('bg sample:', bg)

blocks = []
for i, x in enumerate(old_x):
    blocks.append(img.crop((x, row4y, x + OLD_W[i], row4y + keyH)))

canvas = img.copy()
d = ImageDraw.Draw(canvas)
d.rectangle([keyAreaX, row4y - 2, keyAreaX + KAW - 1, row4y + keyH + 1], fill=bg)
for i, s in enumerate(SRC_OF):
    canvas.paste(blocks[s], (new_x[i], row4y))
canvas.save(OUT_CLEAN)
print('saved', OUT_CLEAN)

# ---- 对比图：上=当前，下=目标 ----
PAD = 26
LBL = 46
band_h = keyH
out_w = gw
out_h = LBL + band_h + PAD + LBL + band_h + PAD
cmp_img = Image.new('RGB', (out_w, out_h), (255, 255, 255))
try:
    font = ImageFont.truetype('C:/Windows/Fonts/msyhbd.ttc', 24)
    fontS = ImageFont.truetype('C:/Windows/Fonts/msyh.ttc', 18)
except Exception:
    font = ImageFont.load_default(); fontS = font
dc = ImageDraw.Draw(cmp_img)
dc.text((12, 10), '当前默认布局 Row4', font=font, fill=(30, 30, 30))
cmp_img.paste(img.crop((0, row4y, gw, row4y + band_h)), (0, LBL))
y2 = LBL + band_h + PAD
dc.rectangle([0, y2 - PAD, out_w, y2 - PAD + 0], fill=(255, 255, 255))
dc.text((12, y2 + 10), '改后 Row4（按你的箭头：Menu 移到空格右侧 / 右 Alt 前）', font=font, fill=(200, 30, 30))
cmp_img.paste(canvas.crop((0, row4y, gw, row4y + band_h)), (0, y2 + LBL))
cmp_img.save(OUT_CMP)
print('saved', OUT_CMP)
