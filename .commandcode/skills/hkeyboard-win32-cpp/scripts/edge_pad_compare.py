# -*- coding: utf-8 -*-
"""生成「贴边留白」对照图：左右两列 × 两种图标样式 × 改前/改后。

每个键内画三条竖线当尺子：
  洋红 = 键的左右边缘（键框）
  青   = 标签墨迹的实际起止（由像素扫描得出）
这样「墨迹离键边多远」一眼可见。
"""
import collections
import sys
from PIL import Image, ImageDraw, ImageFont

V9 = r"C:\Users\29776\WorkBuddy\2026-09-25-19-32-21\.workbuddy\scratch\v9"
DIRS = [("BEF", V9 + r"\shots_pad_before"), ("AFT", V9 + r"\shots")]

# 与 hkeyboard.cpp 探针日志同源：默认布局 1u 网格
u, kax, gap, KH = 106.9565217391304, 32, 7, 86
ROWS = [84, 177, 270, 363]

def key_rect(col, wu, row):
    xa = kax + round(col * u)
    xb = kax + round((col + wu) * u)
    return xa, ROWS[row], xb - xa - gap, KH

# 左列 / 右列各自要展示的键（同 4 行，便于竖着比对）
LEFT  = [(0.00, 1.00, "Esc", 0), (0.00, 1.50, "Tab", 1),
         (0.00, 2.00, "Caps", 2), (0.00, 2.50, "Shift", 3)]
RIGHT = [(14.00, 1.50, "Bksp", 0), (14.50, 1.00, "Del", 1),
         (13.00, 2.50, "Enter", 2), (13.50, 2.00, "Shift", 3)]

def ink_span(px, x0, y0, w, h):
    """键内墨迹的水平起止（相对键左边），用键内部众数色当底色。"""
    c = collections.Counter()
    for yy in range(y0 + 4, y0 + h - 4):
        for xx in range(x0 + 4, x0 + w - 4):
            c[px[xx, yy]] += 1
    bg = c.most_common(1)[0][0]
    lo = hi = None
    for yy in range(y0 + 5, y0 + h - 5):
        for xx in range(x0 + 5, x0 + w - 5):
            r, g, b = px[xx, yy]
            if abs(r - bg[0]) + abs(g - bg[1]) + abs(b - bg[2]) > 70:
                lo = xx if lo is None else min(lo, xx)
                hi = xx if hi is None else max(hi, xx)
    return (lo - x0, hi - x0) if lo is not None else (None, None)

try:
    F = ImageFont.truetype(r"C:\Windows\Fonts\arial.ttf", 16)
    FB = ImageFont.truetype(r"C:\Windows\Fonts\arial.ttf", 20)
except Exception:
    F = ImageFont.load_default()
    FB = F

def render(tag, path, cols, style):
    img = Image.open(path).convert("RGB"); px = img.load()
    bands = []
    for (col, wu, name, row) in cols:
        x0, y0, w, h = key_rect(col, wu, row)
        # 左右各多带 0.45u，看得见相邻键才好判断「贴边」
        ext = int(0.45 * u)
        cx0 = max(0, x0 - ext); cx1 = min(img.width, x0 + w + ext)
        crop = img.crop((cx0, y0 - 6, cx1, y0 + h + 6))
        sc = 1.55
        crop = crop.resize((int(crop.width * sc), int(crop.height * sc)), Image.LANCZOS)
        d = ImageDraw.Draw(crop)
        for gx, color in (((x0 - cx0) * sc, (255, 40, 120)),
                          ((x0 + w - cx0) * sc, (255, 40, 120))):
            d.line([(gx, 0), (gx, crop.height)], fill=color, width=2)
        lo, hi = ink_span(px, x0, y0, w, h)
        if lo is not None:
            for gx in ((x0 + lo - cx0) * sc, (x0 + hi - cx0) * sc):
                d.line([(gx, 0), (gx, crop.height)], fill=(0, 170, 220), width=2)
            pad = (lo if cols is LEFT else w - 1 - hi)
            txt = "%s %s pad=%d" % (tag, name, pad)
            tw = d.textlength(txt, font=F)
            d.rectangle([crop.width - tw - 14, 2, crop.width - 2, 26], fill=(255, 255, 255))
            d.text((crop.width - tw - 9, 4), txt, fill=(10, 10, 10), font=F)
        bands.append(crop)
    return bands

def sheet(rows, out):
    labw = 8
    W = max(b.width for b in rows) + labw * 2
    H = sum(b.height for b in rows) + 8 * (len(rows) - 1) + 60
    s = Image.new("RGB", (W, H), (250, 250, 252))
    d = ImageDraw.Draw(s)
    d.text((labw, 10), "label ink distance from key edge  (cyan = ink edges, magenta = key edges)",
           fill=(20, 20, 20), font=FB)
    d.text((labw, 38), "BEF = before fix, AFT = after fix", fill=(90, 90, 100), font=F)
    yy = 60
    for b in rows:
        s.paste(b, (labw, yy)); yy += b.height + 8
    s.save(out); print("saved", out, s.size)

for style, fname in (("icon+text", "01_default.png"), ("text-only", "02_text.png")):
    for side, cols in (("L", LEFT), ("R", RIGHT)):
        rows = []
        for tag, dirpath in DIRS:
            rows += render("%s(%s)" % (tag, style), dirpath + "\\" + fname, cols, style)
        sheet(rows, V9 + r"\cmp_pad_%s_%s.png"
              % ("left" if side == "L" else "right", "icon" if style == "icon+text" else "text"))
