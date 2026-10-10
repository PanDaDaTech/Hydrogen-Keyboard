"""关闭对话框重设计 —— 高保真预览渲染器。

不是示意稿：字体用项目自带的 MiSans-Medium.ttf，颜色用 HkbTokensBuild 的
oklch 配方（hue 可调），字号/几何用 C++ 里 PromptDraw 的真实数值。
所以渲染出来的图可以直接和实机截图对比。

用法：
    python render_prompt.py <hue> <out.png> [variant] [theme]
    hue     0..359，默认 300
    variant "now" | "A" | "B"，默认 now
    theme   "light" | "dark"，默认 light
"""
import math
import sys
from PIL import Image, ImageDraw, ImageFont

FONT = r"E:/Project/Git/Hydrogen-Keyboard/winres/MiSans-Medium.ttf"

# ---------- oklch 令牌（与 hkeyboard.cpp HkbTokensBuild 逐行对应） ----------
def _enc(x):
    return 12.92 * x if x <= 0.0031308 else 1.055 * (x ** (1 / 2.4)) - 0.055

def _ch(x):
    x = max(0.0, min(1.0, x))
    return max(0, min(255, int(_enc(x) * 255 + 0.5)))

def oklch(L, C, h_deg):
    h = math.radians(h_deg)
    a, b = C * math.cos(h), C * math.sin(h)
    lp = L + 0.3963377774 * a + 0.2158037573 * b
    mp = L - 0.1055613458 * a - 0.0638541728 * b
    sp = L - 0.0894841775 * a - 1.2914855480 * b
    l, m, s = lp ** 3, mp ** 3, sp ** 3
    r  =  4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s
    g  = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s
    bl = -0.0041960863 * l - 0.7034186147 * m + 1.7016147010 * s
    return (_ch(r), _ch(g), _ch(bl))

def blend(c1, c2, t):
    return tuple(int(c1[i] + (c2[i] - c1[i]) * t + 0.5) for i in range(3))

def tokens(hue, dark):
    h = float(hue)
    t = {}
    t["primary"]  = oklch(0.75, 0.140, h) if dark else oklch(0.70, 0.140, h)
    t["pageBg"]   = oklch(0.16, 0.014, h) if dark else oklch(0.95, 0.010, h)
    t["cardBg"]   = oklch(0.23, 0.015, h) if dark else (255, 255, 255)
    t["btnRegularBg"]      = oklch(0.33, 0.035, h) if dark else oklch(0.95, 0.025, h)
    t["btnRegularBgHover"] = oklch(0.38, 0.040, h) if dark else oklch(0.90, 0.050, h)
    t["btnContent"]  = oklch(0.75, 0.100, h) if dark else oklch(0.55, 0.120, h)
    t["text"]    = oklch(0.85, 0.020, h) if dark else oklch(0.25, 0.020, h)
    t["panel"]   = oklch(0.17, 0.012, h) if dark else (255, 255, 255)
    t["controlBg"] = blend(t["cardBg"], t["text"], 0.18)
    t["keyOutline"] = blend(t["cardBg"], t["text"], 0.20)
    # OverlayBgr(base, white, alpha)：第二参是「叠白」标志
    t["lineDivider"] = blend(t["pageBg"], (255, 255, 255) if not dark else (0, 0, 0), 0.08)
    # ⚠ OverlayBgr(base, white, alpha) 的第二参是「叠白」标志，不是「是否深色」——
    #   C++ 里 OverlayBgr(pageBg, dark, 0.60)，浅色下 dark=FALSE ⇒ 向**黑**混 60% = 深灰。
    #   这里曾经写成 `if not dark`，把浅色的 textMuted 算成近白 (#F9F8FB)。
    t["textMuted"]   = blend(t["pageBg"], (0, 0, 0) if not dark else (255, 255, 255), 0.60)
    t["primaryFg"]   = blend(t["primary"], (0, 0, 0), 0.70) if dark else t["text"]
    t["btnCardHover"] = oklch(0.30, 0.030, h) if dark else oklch(0.98, 0.005, h)
    return t

# ---------- 字体（pt -> px 用 96dpi，换算同 MakeFont） ----------
def load(pt):
    # GDI 的 pt 在 96dpi 下：px = pt * 96 / 72
    return ImageFont.truetype(FONT, int(round(pt * 96 / 72)))

def rr(d, box, radius, fill=None, outline=None, width=1):
    d.rounded_rectangle(box, radius=radius, fill=fill, outline=outline, width=width)

def center_text(d, cx, cy, txt, font, fill):
    l, t, r, b = d.textbbox((0, 0), txt, font=font)
    d.text((cx - (r - l) / 2 - l, cy - (b - t) / 2 - t), txt, font=font, fill=fill)

def render(variant, hue=300, dark=False, S=2.0):
    """S = 缩放倍数（DIP -> px 的模拟 DPR）。返回 PIL Image。"""
    T = tokens(hue, dark)
    fCtrl = load(11)     # 控件 / 标题
    fBase = load(10)     # 描述

    def px(dip):
        return int(round(dip * S))

    if variant == "now":
        W, H = px(300), px(190)     # 现状：内容 182 + 8 下留白
    elif variant == "A":
        W, H = px(340), px(258)
    else:
        W, H = px(460), px(287)     # 卡片版：20 + 247 内容 + 20 = 287（与 C++ 一致）
        # 460 是去掉描述后的口径：瓶颈成分段控件 segW=197 DIP，
        # 88(tx) + 12(间距) + 197 + 40(右留白) = 337 起步，留 33 余量。

    img = Image.new("RGB", (W, H), T["pageBg"])

    if variant == "now":
        # 现状：背景 = C_BG = pageBg（PromptDraw 不铺底，靠 ClearWindowBackBuffer）
        # 窗口圆角：ApplyRoundedWindow(hWnd, 14)
        _base(img, px, T["pageBg"])
        d = ImageDraw.Draw(img)
        _draw_now(d, T, px, fCtrl, fBase, W, H)
    elif variant == "A":
        # 改版：窗口底 = floatPanelBg（纯白），与 pageBg 拉开层级
        _base(img, px, T["panel"])
        d = ImageDraw.Draw(img)
        _draw_a(d, T, px, fCtrl, fBase, W, H)
    else:
        # 卡片版：窗口底 = pageBg 淡紫，白卡片浮在上面（与设置页同一层级语法）
        _base(img, px, T["pageBg"])
        d = ImageDraw.Draw(img)
        _draw_b(d, T, px, fCtrl, fBase, W, H)
    return img

def _base(img, px, fill):
    """铺窗口底 + 14 DIP 圆角（对应 ApplyRoundedWindow(hWnd, 14)）。"""
    W, H = img.size
    d = ImageDraw.Draw(img)
    d.rounded_rectangle([0, 0, W - 1, H - 1], radius=px(14), fill=fill)

# ---------------- 现状（逐行照抄 PromptDraw） ----------------
def _draw_now(d, T, px, fCtrl, fBase, W, H):
    hdr = px(36)
    d.text((px(14), px(0)), "关闭轻键", font=fCtrl, fill=T["text"])
    bw, bh = px(26), hdr - px(12)
    bx, by = W - bw - px(8), (hdr - bh) // 2
    _x_icon(d, bx + (bw - px(18)) // 2, by + (bh - px(18)) // 2, px(18), T["textMuted"])

    x0, cw = px(20), W - px(40)
    y = hdr + px(12)
    d.text((x0, y), "请选择关闭方式：", font=fBase, fill=T["btnContent"])
    y += px(22)
    rowH = px(24)
    _radio(d, x0 + px(8), y + rowH // 2, px(7), True, T)
    d.text((x0 + px(26), y), "直接退出程序", font=fCtrl, fill=T["text"])
    y += rowH
    _radio(d, x0 + px(8), y + rowH // 2, px(7), False, T)
    d.text((x0 + px(26), y), "隐藏到系统托盘", font=fCtrl, fill=T["text"])
    y += rowH + px(4)
    swW, swH = px(40), px(20)
    swX = x0 + cw - swW
    _switch(d, swX, y + (rowH - swH) // 2, swW, swH, False, T)
    d.text((x0, y), "记住我的选择", font=fCtrl, fill=T["text"])
    y += rowH + px(8)
    bw2, bh2 = px(84), px(28)
    bxCancel = W - px(20) - bw2
    bxOk = bxCancel - px(12) - bw2
    rr(d, [bxOk, y, bxOk + bw2, y + bh2], px(6), fill=T["primary"], outline=T["keyOutline"])
    center_text(d, bxOk + bw2 // 2, y + bh2 // 2, "确定", fCtrl, T["primaryFg"])
    rr(d, [bxCancel, y, bxCancel + bw2, y + bh2], px(6), fill=T["cardBg"], outline=T["keyOutline"])
    center_text(d, bxCancel + bw2 // 2, y + bh2 // 2, "取消", fCtrl, T["text"])

# ---------------- 方案 A：分组卡片 ----------------
def _draw_a(d, T, px, fCtrl, fBase, W, H):
    pad = px(20)
    # 标题行（带关闭钮，与现状/设置页一致）
    hdr = px(36)
    d.text((pad, px(8)), "关闭轻键", font=fCtrl, fill=T["text"])
    bw, bh = px(26), px(24)
    bx, by = W - pad - bw, px(6)
    _x_icon(d, bx + (bw - px(14)) // 2, by + (bh - px(14)) // 2, px(14), T["textMuted"])

    y = hdr + px(2)
    d.text((pad, y), "选择关闭方式", font=fBase, fill=T["btnContent"])
    y += px(24)

    # 选项组：浅底容器 + 两条可选卡片
    cardH = px(38)
    grpH = cardH * 2 + px(6)
    rr(d, [pad, y, W - pad, y + grpH], px(8), fill=T["btnCardHover"])
    inner = px(8)
    gx0, gx1 = pad + inner, W - pad - inner
    cy = y + inner
    for sel, label in ((True, "直接退出程序"), (False, "隐藏到系统托盘")):
        box = [gx0, cy, gx1, cy + cardH]
        rr(d, box, px(7),
           fill=T["panel"],
           outline=T["primary"] if sel else T["keyOutline"],
           width=max(1, px(0.5)))
        _radio(d, gx0 + px(18), cy + cardH // 2, px(6), sel, T)
        d.text((gx0 + px(32), cy + (cardH - px(16)) // 2 - px(1)),
               label, font=fCtrl, fill=T["text"])
        cy += cardH + px(6)
    y += grpH + px(14)

    # 记住我的选择
    swW, swH = px(38), px(20)
    swX = W - pad - swW
    rowH = px(24)
    d.text((pad, y), "记住我的选择", font=fCtrl, fill=T["text"])
    _switch(d, swX, y + (rowH - swH) // 2, swW, swH, True, T)
    y += rowH + px(12)

    # 按钮区分隔线
    d.line([(pad, y), (W - pad, y)], fill=T["lineDivider"], width=max(1, px(0.5)))
    y += px(12)

    bw2, bh2 = px(84), px(30)
    bxCancel = W - pad - bw2
    bxOk = bxCancel - px(10) - bw2
    rr(d, [bxOk, y, bxOk + bw2, y + bh2], px(6), fill=T["primary"])
    center_text(d, bxOk + bw2 // 2, y + bh2 // 2, "确定", fCtrl, T["primaryFg"])
    rr(d, [bxCancel, y, bxCancel + bw2, y + bh2], px(6), fill=T["btnRegularBg"])
    center_text(d, bxCancel + bw2 // 2, y + bh2 // 2, "取消", fCtrl, T["btnContent"])

# ---------------- 卡片版：照搬设置页「标题 + 卡片 + 行」 ----------------
def _draw_b(d, T, px, fCtrl, fBase, W, H):
    """逐行照抄 PromptComputeLayout 的推进链（单位 DIP）：
         20 标题行(38) → 58 +18 → 76 卡片顶
         卡片内 12+54(行)=66 每行，两行 +1 分隔线 = 109
         → 76+109=185 卡片底 → +16 → 201 分隔线 → +12 → 213 按钮(30)
         → 243 +20 底留白 = 263
    所有度量都取自设置页 SettingsMetrics：
    headIcon 38 / tile 30 / tileGap 18 / rowPadY 12 / comboH 40 / switch 46×26。
    """
    pad, headIcon = px(20), px(38)
    rowPadY, tile, tileGap = px(12), px(30), px(18)
    rowH = rowPadY * 2 + tile
    cardR = px(16)

    # 标题行（落在 pageBg 上，不进卡片）
    rr(d, [pad, pad, pad + headIcon, pad + headIcon], px(9), fill=T["btnRegularBg"])
    _info_icon(d, pad + (headIcon - px(17)) // 2, pad + (headIcon - px(17)) // 2,
               px(17), T["btnContent"])
    d.text((pad + headIcon + px(12), pad + (headIcon - px(18)) // 2 - px(1)),
           "是否关闭轻键", font=fCtrl, fill=T["text"])
    bw, bh = px(28), px(28)
    bx, by = W - pad - bw, px(5)
    _x_icon(d, bx + (bw - px(14)) // 2, by + (bh - px(14)) // 2, px(14), T["textMuted"])

    # 卡片：cardBg 白底 + 16 圆角（与 DrawSettingsCard 同一配方）
    cardX, cardY = pad, pad + headIcon + px(18)
    # ⚠ 含上下 rowPadY —— 与 C++ 的 L.cardH = rowPadY*2 + rowH*2 + 1 一致
    cardW, cardH = W - pad * 2, rowPadY * 2 + rowH * 2 + 1
    rr(d, [cardX, cardY, cardX + cardW, cardY + cardH], cardR, fill=T["cardBg"])
    # 行间分隔线（左右各缩进 20 —— 与设置页一致）
    d.line([(cardX + px(20), cardY + rowPadY + rowH),
            (cardX + cardW - px(20), cardY + rowPadY + rowH)],
           fill=T["lineDivider"], width=max(1, px(0.5)))

    # 行 1：选择关闭方式（无描述，标题按整个行盒居中）
    segH = px(40)
    segY = cardY + rowPadY + (rowH - segH) // 2
    items = ["退出程序", "隐藏到托盘"]
    segW = 2 * px(3) + sum(int(fCtrl.getlength(s)) + px(28) for s in items)
    segX = W - pad - px(20) - segW
    tx = cardX + px(20) + tile + tileGap
    ty = cardY + rowPadY + (rowH - px(18)) // 2
    rr(d, [cardX + px(20), cardY + rowPadY + (rowH - tile) // 2,
           cardX + px(20) + tile, cardY + rowPadY + (rowH + tile) // 2],
       px(8), fill=T["btnRegularBg"])
    _x_icon(d, cardX + px(20) + (tile - px(15)) // 2,
            cardY + rowPadY + (rowH - tile) // 2 + (tile - px(15)) // 2,
            px(15), T["btnContent"])
    d.text((tx, ty), "选择关闭方式", font=fCtrl, fill=T["text"])
    pad3 = px(3)
    rr(d, [segX, segY, segX + segW, segY + segH], px(10), fill=T["btnRegularBg"])
    cx = segX + pad3
    for i, s in enumerate(items):
        w = int(fCtrl.getlength(s)) + px(28)
        if i == 0:
            rr(d, [cx, segY + pad3, cx + w, segY + segH - pad3], px(8), fill=T["cardBg"])
        center_text(d, cx + w // 2, segY + segH // 2, s, fCtrl,
                    T["btnContent"] if i == 0 else T["textMuted"])
        cx += w

    # 行 2：记住我的选择（✓ tile + 标题，无描述 + 开关）
    remY = cardY + rowPadY + rowH + 1
    swW, swH = px(46), px(26)
    swX = W - pad - px(20) - swW
    ty2 = remY + (rowH - px(18)) // 2
    rr(d, [cardX + px(20), remY + (rowH - tile) // 2,
           cardX + px(20) + tile, remY + (rowH + tile) // 2],
       px(8), fill=T["btnRegularBg"])
    _check(d, cardX + px(20) + tile // 2, remY + rowH // 2, px(6), T, light=True)
    d.text((tx, ty2), "记住我的选择", font=fCtrl, fill=T["text"])
    _switch(d, swX, remY + (rowH - swH) // 2, swW, swH, True, T)

    # 卡片与按钮区之间的分隔线 + 按钮
    y = cardY + cardH + px(16)
    d.line([(pad, y), (W - pad, y)], fill=T["lineDivider"], width=max(1, px(0.5)))
    y += px(12)
    bw2, bh2 = px(84), px(30)
    bxCancel = W - pad - bw2
    bxOk = bxCancel - px(10) - bw2
    rr(d, [bxOk, y, bxOk + bw2, y + bh2], px(6), fill=T["primary"])
    center_text(d, bxOk + bw2 // 2, y + bh2 // 2, "确定", fCtrl, T["primaryFg"])
    rr(d, [bxCancel, y, bxCancel + bw2, y + bh2], px(6), fill=T["btnRegularBg"])
    center_text(d, bxCancel + bw2 // 2, y + bh2 // 2, "取消", fCtrl, T["btnContent"])

# ---------------- 控件 ----------------
def _radio(d, cx, cy, r, sel, T):
    rr(d, [cx - r, cy - r, cx + r, cy + r], r,
       fill=T["primary"] if sel else T["panel"],
       outline=T["primary"] if sel else T["textMuted"], width=max(1, r // 4))

def _check(d, cx, cy, s, T, light=False):
    """light=True 时**不画方块**，只画对勾 —— 用于「行图标 tile 里放对勾」
    （设置页的 ✓ tile：C_REGULAR 底 + btn_content 色对勾）。"""
    if not light:
        rr(d, [cx - s, cy - s, cx + s, cy + s], max(2, s // 3), fill=T["primary"])
    d.line([(cx - s * 0.45, cy), (cx - s * 0.1, cy + s * 0.4), (cx + s * 0.5, cy - s * 0.45)],
           fill=T["btnContent"] if light else T["primaryFg"],
           width=max(2, int(s * 0.28)), joint="curve")

def _switch(d, x, y, w, h, on, T):
    rr(d, [x, y, x + w, y + h], h // 2, fill=T["primary"] if on else T["controlBg"])
    kr = h // 2 - max(1, h // 16)
    kx = x + w - kr - max(1, h // 16) if on else x + kr + max(1, h // 16)
    d.ellipse([kx - kr, y + h // 2 - kr, kx + kr, y + h // 2 + kr], fill=T["panel"])

def _x_icon(d, x, y, s, col):
    # 两条对角线，端点内缩 s*0.18 才像「✕」而不是「⊠」
    w = max(1, int(s * 0.10))
    m = s * 0.18
    d.line([(x + m, y + m), (x + s - m, y + s - m)], fill=col, width=w)
    d.line([(x + s - m, y + m), (x + m, y + s - m)], fill=col, width=w)

def _info_icon(d, x, y, s, col):
    """HKICON_DIALOGINFO：圆圈 + 竖线 + 点（24 网格，可见 3.4..20.6）。"""
    w = max(1, int(s * 0.083))          # 原描边 1.9 / 24
    pad = s * 3.4 / 24.0                # 圆的外缘内缩
    r = (s - 2 * pad) / 2.0
    cx, cy = x + s / 2.0, y + s / 2.0
    d.ellipse([cx - r, cy - r, cx + r, cy + r], outline=col, width=w)
    # 竖线：y 11.2..16.6 -> 相对 24 网格
    d.line([(cx, y + s * 11.2 / 24.0), (cx, y + s * 16.6 / 24.0)], fill=col, width=w)
    # 点：圆心在 (12, 7.9)，半径 ≈ 1.05（HOLE 槽挖空后剩下的环）
    dr = s * 1.05 / 24.0
    d.ellipse([cx - dr, y + s * 7.9 / 24.0 - dr, cx + dr, y + s * 7.9 / 24.0 + dr],
              fill=col)

# ---------------- 拼版 ----------------
def board(hue=300, dark=False, S=2.0):
    """三列并排。每列按**自身高度**垂直居中，并在下方标注 DIP 尺寸。"""
    labels = [("now", "现状"), ("A", "A · 分组卡片"), ("B", "B · 图标前置")]
    imgs = [(render(v, hue, dark, S), lab) for v, lab in labels]
    T = tokens(hue, dark)
    pad = int(26 * S)
    lab_h = int(20 * S)
    cap_h = int(18 * S)
    colw = max(i.width for i, _ in imgs)
    rowh = max(i.height for i, _ in imgs)
    W = int(len(imgs) * colw + (len(imgs) + 1) * pad)
    H = int(rowh + pad * 2 + lab_h + cap_h)
    out = Image.new("RGB", (W, H), T["pageBg"])
    d = ImageDraw.Draw(out)
    try:
        f = ImageFont.truetype(FONT, int(11.5 * S))
    except Exception:
        f = ImageFont.load_default()
    for k, (im, lab) in enumerate(imgs):
        x = int(pad + k * (colw + pad))
        ytop = int(pad + lab_h + (rowh - im.height) // 2)
        d.text((x, int(pad - 4 * S)), lab, font=f, fill=T["text"])
        # 窗口投影（页面底比窗口底深一档，模拟真实浮层层级）
        rr(d, [x - 1, ytop - 1, x + im.width, ytop + im.height], px_r(S, 12),
           outline=T["lineDivider"], width=max(1, int(S * 0.5)))
        out.paste(im, (x, ytop))
        dim = "%d x %d DIP" % (im.width // S, im.height // S)
        d.text((x, ytop + im.height + int(4 * S)), dim, font=f, fill=T["textMuted"])
    return out

def px_r(S, v):
    return int(round(v * S))

if __name__ == "__main__":
    hue = int(sys.argv[1]) if len(sys.argv) > 1 else 300
    outp = sys.argv[2] if len(sys.argv) > 2 else "prompt.png"
    dark = "dark" in sys.argv[3:]
    b = board(hue, dark)
    b.save(outp)
    print("saved", outp, b.size, "hue", hue, "dark" if dark else "light")
