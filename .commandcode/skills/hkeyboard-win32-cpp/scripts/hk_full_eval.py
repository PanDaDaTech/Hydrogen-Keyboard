# -*- coding: utf-8 -*-
"""
复现 Hydrogen-Keyboard 全尺寸布局（g_layoutMode==2）的真实几何，
并与「单位网格（1u）」提案逐键对比，输出：
  1) 终端数据表（每行键宽 px / u、行右边缘、列对齐偏差）
  2) hk_全尺寸布局-网格化对照.html （两版按真实 px 渲染 + 1u 参考线）
所有算式严格照抄 hkeyboard.cpp 的 BuildKeys / BuildComplete（含 C 的整数截断）。
"""
import math, os, sys, io

# ---------------- C++ 语义工具 ----------------
def ci(x):                      # C 的 (int) 截断
    return int(x)
def cdiv(a, b):                 # C 的整数除法（全正数 = 向下取整）
    return a // b

# ---------------- oklch → sRGB（与 HkbTokensBuild 一致） ----------------
def _enc(x):
    return 12.92 * x if x <= 0.0031308 else 1.055 * x ** (1 / 2.4) - 0.055
def oklch(L, C, h):
    rad = math.radians(h); a = C * math.cos(rad); b = C * math.sin(rad)
    lp = L + 0.3963377774 * a + 0.2158037573 * b
    mp = L - 0.1055613458 * a - 0.0638541728 * b
    sp = L - 0.0894841775 * a - 1.2914855480 * b
    l, m, s = lp ** 3, mp ** 3, sp ** 3
    r = 4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s
    g = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s
    bl = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s
    f = lambda x: max(0, min(255, int(round(max(0.0, min(1.0, _enc(max(0.0, min(1.0, x))))) * 255))))
    return "#%02X%02X%02X" % (f(r), f(g), f(bl))
def blend(bg, fg, t):           # BlendColor(cardBg, text, 0.20)
    p = lambda c: (int(c[1:3], 16), int(c[3:5], 16), int(c[5:7], 16))
    a, b = p(bg), p(fg)
    return "#%02X%02X%02X" % tuple(int(round(a[i] * (1 - t) + b[i] * t)) for i in range(3))

HUE = 250
TOK = {
    "light": dict(
        page=oklch(0.95, 0.010, HUE), card="#FFFFFF",
        reg=oklch(0.95, 0.025, HUE), content=oklch(0.55, 0.120, HUE),
        text=oklch(0.25, 0.020, HUE), primary=oklch(0.70, 0.140, HUE)),
    "dark": dict(
        page=oklch(0.16, 0.014, HUE), card=oklch(0.23, 0.015, HUE),
        reg=oklch(0.33, 0.035, HUE), content=oklch(0.75, 0.100, HUE),
        text=oklch(0.85, 0.020, HUE), primary=oklch(0.75, 0.140, HUE)),
}
for k in TOK:
    TOK[k]["outline"] = blend(TOK[k]["card"], TOK[k]["text"], 0.20)

# ---------------- 公共：页头 / 行高 / 间距（BuildKeys） ----------------
def frame(ww, wh, dpi=1.0, rows=6):
    scaleX = ww / (980.0 * dpi)
    headerH = max(ci(44.0 * dpi), 34)
    gap = max(ci(4.0 * dpi * scaleX), 2)
    areaX = max(ci(10 * dpi), 6)
    botPad = max(ci(16 * dpi), 8)
    keyH = max(cdiv(wh - headerH - botPad - (rows - 1) * gap, rows), 20)
    return dict(scaleX=scaleX, headerH=headerH, gap=gap, areaX=areaX,
                keyH=keyH, areaW=ww - areaX * 2, rows=rows,
                y0=headerH + gap + 2)

# ================= 现状：逐行独立除法 =================
def cur_layout(ww=1380, wh=372, dpi=1.0, np_hidden=False):
    F = frame(ww, wh, dpi)
    gap, areaX, areaW, keyH, y = F["gap"], F["areaX"], F["areaW"], F["keyH"], F["y0"]
    sX = F["scaleX"]
    navW = ci(52 * dpi * sX)
    navBlockW = navW * 3 + 2 * gap
    numBlockW = navW * 4 + 3 * gap
    rest = 2 * gap + navBlockW + (0 if np_hidden else numBlockW)
    MAIN_W = areaW - rest
    if MAIN_W < 400: MAIN_W = 400
    h2 = keyH * 2 + gap
    mx, nx = areaX, areaX + MAIN_W + gap
    px = nx + navBlockW + gap
    K = []
    def add(x, yy, w, h, label, kind, vk):
        K.append(dict(x=x, y=yy, w=w, h=h, t=label, kind=kind, vk=vk))

    # Row0: Esc F1..F12 Del | PrtSc ScrLk Pause | Num / * -
    wEsc, wDel = ci(56 * dpi * sX), ci(56 * dpi * sX)
    aw = cdiv(MAIN_W - wEsc - wDel - 13 * gap, 12)
    rem = MAIN_W - wEsc - wDel - 13 * gap - aw * 12
    add(mx, y, wEsc, keyH, "Esc", "mod", 0x1B); mx += wEsc + gap
    for i in range(12):
        w = aw + (1 if i < rem else 0)
        add(mx, y, w, keyH, "F%d" % (i + 1), "n", 0x70 + i); mx += w + gap
    add(mx, y, wDel, keyH, "Del", "mod", 0x2E)
    nav_cur = [(0x2C, "PrtSc"), (0x46, "ScrLk"), (0x13, "Pause")]
    cx = nx
    for v, t in nav_cur: add(cx, y, navW, keyH, t, "mod", v); cx += navW + gap
    row0_num = None
    if not np_hidden:
        row0_num = []
        cx = px
        for v, t in [(0x90, "Num"), (0x6F, "/"), (0x6A, "*"), (0x6D, "-")]:
            row0_num.append((cx, y, navW, keyH, t, v)); add(cx, y, navW, keyH, t, "n" if v not in (0x90,) else "mod", v)
            cx += navW + gap

    y += keyH + gap
    # Row1: ` 1..0 - = Backspace | Ins Home PgUp | 7 8 9 +(2)
    wBk = ci(68 * dpi * sX)
    aw = cdiv(MAIN_W - wBk - 13 * gap, 13)
    rem = MAIN_W - wBk - 13 * gap - aw * 13
    mx = areaX
    digs = [("`", 0xC0)] + [(str(i), 0x30 + (i % 10)) for i in range(1, 11)] + [("-", 0xBD), ("=", 0xBB)]
    for i, (t, v) in enumerate(digs):
        w = aw + (1 if i <= rem else 0)          # ← 现状的 i<=rem
        add(mx, y, w, keyH, t, "n", v); mx += w + gap
    add(mx, y, wBk, keyH, "BkSp", "mod", 0x08)
    cx = nx
    for v, t in [(0x2D, "Ins"), (0x24, "Home"), (0x21, "PgUp")]:
        add(cx, y, navW, keyH, t, "mod", v); cx += navW + gap
    if not np_hidden:
        cx = px
        for i in range(3): add(cx, y, navW, keyH, str(7 + i), "n", 0x67 + i); cx += navW + gap
        add(cx, y, navW, h2, "+", "n", 0x6B)
    y += keyH + gap
    # Row2: Tab q..p [ ] \ | Del End PgDn | 4 5 6
    wTab = ci(68 * dpi * sX)
    aw = cdiv(MAIN_W - wTab - 13 * gap, 13)
    rem = MAIN_W - wTab - 13 * gap - aw * 13
    mx = areaX
    add(mx, y, wTab, keyH, "Tab", "mod", 0x09); mx += wTab + gap
    qs = list("qwertyuiop") + ["[", "]", "\\"]
    for i, t in enumerate(qs, start=1):
        v = [0x51,0x57,0x45,0x52,0x54,0x59,0x55,0x49,0x4F,0x50,0xDB,0xDD,0xDC][i-1]
        w = aw + (1 if i <= rem else 0)
        add(mx, y, w, keyH, t, "l" if i <= 10 else "n", v); mx += w + gap
    cx = nx
    for v, t in [(0x2E, "Del"), (0x23, "End"), (0x22, "PgDn")]:
        add(cx, y, navW, keyH, t, "mod", v); cx += navW + gap
    if not np_hidden:
        cx = px
        for i in range(3): add(cx, y, navW, keyH, str(4 + i), "n", 0x64 + i); cx += navW + gap
    y += keyH + gap
    # Row3: Caps a..;' Enter | 1 2 3 Enter(2)
    wCaps, wEnt = ci(80 * dpi * sX), ci(90 * dpi * sX)
    aw = cdiv(MAIN_W - wCaps - wEnt - 12 * gap, 11)
    rem = MAIN_W - wCaps - wEnt - 12 * gap - aw * 11
    mx = areaX
    add(mx, y, wCaps, keyH, "Caps", "mod", 0x14); mx += wCaps + gap
    home = list("asdfghjkl") + [";", "'"]
    hv = [0x41,0x53,0x44,0x46,0x47,0x48,0x4A,0x4B,0x4C,0xBA,0xDE]
    for i, t in enumerate(home, start=1):
        w = aw + (1 if i <= rem else 0)
        add(mx, y, w, keyH, t, "l" if i <= 9 else "n", hv[i-1]); mx += w + gap
    add(mx, y, wEnt, keyH, "Enter", "mod", 0x0D)
    if not np_hidden:
        cx = px
        for i in range(3): add(cx, y, navW, keyH, str(1 + i), "n", 0x61 + i); cx += navW + gap
        add(cx, y, navW, h2, "Enter", "mod", 0x0D)
    y += keyH + gap
    # Row4: Shift z../ Shift | ↑
    wLSh, wRSh = ci(95 * dpi * sX), ci(52 * dpi * sX)
    aw = cdiv(MAIN_W - wLSh - wRSh - 11 * gap, 10)
    rem = MAIN_W - wLSh - wRSh - 11 * gap - aw * 10
    mx = areaX
    add(mx, y, wLSh, keyH, "Shift", "mod", 0xA0); mx += wLSh + gap
    zv = [0x5A,0x58,0x43,0x56,0x42,0x4E,0x4D,0xBC,0xBE,0xBF]
    zt = list("zxcvbnm") + [",", ".", "/"]
    for i, t in enumerate(zt):
        w = aw + (1 if i < rem else 0)
        add(mx, y, w, keyH, t, "l" if i <= 6 else "n", zv[i]); mx += w + gap
    add(mx, y, wRSh, keyH, "Shift", "mod", 0xA1)
    add(nx + navW + gap, y, navW, keyH, "\u2191", "arr", 0x26)
    y += keyH + gap
    # Row5: Fn Ctrl Win Alt Space Alt Menu Ctrl | ← ↓ → | 0(2) .
    wFn, wCtl = ci(46 * dpi * sX), ci(56 * dpi * sX)
    wWin, wAlt, wMenu = ci(46 * dpi * sX), ci(58 * dpi * sX), ci(56 * dpi * sX)
    left = wFn + wCtl + wWin + wAlt + wAlt + wMenu + wCtl
    spaceW = MAIN_W - left - 7 * gap
    if spaceW < 60: spaceW = 60
    ws = [wFn, wCtl, wWin, wAlt, spaceW, wAlt, wMenu, wCtl]
    vs = [(0, "Fn", "mod"), (0x11, "Ctrl", "mod"), (0x5B, "\u229E", "mod"), (0x12, "Alt", "mod"),
          (0x20, "", "sp"), (0x12, "Alt", "mod"), (0x5D, "\u2630", "mod"), (0x11, "Ctrl", "mod")]
    x = areaX
    for w, (v, t, kd) in zip(ws, vs): add(x, y, w, keyH, t, kd, v); x += w + gap
    cx = nx
    for v, t in [(0x25, "\u2190"), (0x28, "\u2193"), (0x27, "\u2192")]:
        add(cx, y, navW, keyH, t, "arr", v); cx += navW + gap
    if not np_hidden:
        add(px, y, navW * 2 + gap, keyH, "0", "n", 0x60)
        add(px + navW * 2 + 2 * gap, y, navW, keyH, ".", "n", 0x6E)
    return dict(keys=K, F=F, MAIN_W=MAIN_W, navW=navW, areaX=areaX, areaW=areaW,
                gap=gap, keyH=keyH, ww=ww, wh=wh)

# ================= 提案：1u 单位网格 =================
# ANSI 104 的列位（单位：u）。每块「U 个单位、k 个键」的物理宽度 = U*u - gap。
ROW_MAIN = [
    [("Esc", 0, 1, "mod"), ("F1", 2, 1, "n"), ("F2", 3, 1, "n"), ("F3", 4, 1, "n"),
     ("F4", 5, 1, "n"), ("F5", 6, 1, "n"), ("F6", 7, 1, "n"), ("F7", 8, 1, "n"),
     ("F8", 9, 1, "n"), ("F9", 10, 1, "n"), ("F10", 11, 1, "n"), ("F11", 12, 1, "n"),
     ("F12", 13, 1, "n"), ("Del", 14, 1, "mod")],
    [("`", 0, 1, "n"), ("1", 1, 1, "n"), ("2", 2, 1, "n"), ("3", 3, 1, "n"), ("4", 4, 1, "n"),
     ("5", 5, 1, "n"), ("6", 6, 1, "n"), ("7", 7, 1, "n"), ("8", 8, 1, "n"), ("9", 9, 1, "n"),
     ("0", 10, 1, "n"), ("-", 11, 1, "n"), ("=", 12, 1, "n"), ("BkSp", 13, 2, "mod")],
    [("Tab", 0, 1.5, "mod"), ("q", 1.5, 1, "l"), ("w", 2.5, 1, "l"), ("e", 3.5, 1, "l"),
     ("r", 4.5, 1, "l"), ("t", 5.5, 1, "l"), ("y", 6.5, 1, "l"), ("u", 7.5, 1, "l"),
     ("i", 8.5, 1, "l"), ("o", 9.5, 1, "l"), ("p", 10.5, 1, "l"), ("[", 11.5, 1, "n"),
     ("]", 12.5, 1, "n"), ("\\", 13.5, 1.5, "n")],
    [("Caps", 0, 1.75, "mod"), ("a", 1.75, 1, "l"), ("s", 2.75, 1, "l"), ("d", 3.75, 1, "l"),
     ("f", 4.75, 1, "l"), ("g", 5.75, 1, "l"), ("h", 6.75, 1, "l"), ("j", 7.75, 1, "l"),
     ("k", 8.75, 1, "l"), ("l", 9.75, 1, "l"), (";", 10.75, 1, "n"), ("'", 11.75, 1, "n"),
     ("Enter", 12.75, 2.25, "mod")],
    [("Shift", 0, 2.25, "mod"), ("z", 2.25, 1, "l"), ("x", 3.25, 1, "l"), ("c", 4.25, 1, "l"),
     ("v", 5.25, 1, "l"), ("b", 6.25, 1, "l"), ("n", 7.25, 1, "l"), ("m", 8.25, 1, "l"),
     (",", 9.25, 1, "n"), (".", 10.25, 1, "n"), ("/", 11.25, 1, "n"), ("Shift", 12.25, 2.75, "mod")],
    [("Fn", 0, 1.25, "mod"), ("Ctrl", 1.25, 1.25, "mod"), ("\u229E", 2.5, 1.25, "mod"),
     ("Alt", 3.75, 1.25, "mod"), ("", 5, 6.25, "sp"), ("Alt", 11.25, 1.25, "mod"),
     ("\u2630", 12.5, 1.25, "mod"), ("Ctrl", 13.75, 1.25, "mod")],
]
ROW_NAV = [
    [("PrtSc", 0, 1, "mod"), ("ScrLk", 1, 1, "mod"), ("Pause", 2, 1, "mod")],
    [("Ins", 0, 1, "mod"), ("Home", 1, 1, "mod"), ("PgUp", 2, 1, "mod")],
    [("Del", 0, 1, "mod"), ("End", 1, 1, "mod"), ("PgDn", 2, 1, "mod")],
    [],
    [("\u2191", 1, 1, "arr")],
    [("\u2190", 0, 1, "arr"), ("\u2193", 1, 1, "arr"), ("\u2192", 2, 1, "arr")],
]
# 数字区：5 行，从「数字行」起 —— 与真实 104 一致
ROW_NUM = [
    [("Num", 0, 1, "mod"), ("/", 1, 1, "n"), ("*", 2, 1, "n"), ("-", 3, 1, "n")],
    [("7", 0, 1, "n"), ("8", 1, 1, "n"), ("9", 2, 1, "n"), ("+", 3, 1, "n", 2)],
    [("4", 0, 1, "n"), ("5", 1, 1, "n"), ("6", 2, 1, "n")],
    [("1", 0, 1, "n"), ("2", 1, 1, "n"), ("3", 2, 1, "n"), ("Enter", 3, 1, "mod", 2)],
    [("0", 0, 2, "n"), (".", 2, 1, "n")],
]

def unit_layout(ww=1280, wh=None, dpi=1.0, np_hidden=False, keyH=None):
    """u 由宽度反解（浮点，不截断）；所有列坐标统一 round(c*u) → 零累积误差。
       keyH 默认 = u（正方键）。wh=None → 由 keyH 反算窗口高。"""
    F = frame(ww, wh if wh else 1, dpi)
    gap, areaX, areaW = F["gap"], F["areaX"], F["areaW"]
    units = [15, 3] + ([] if np_hidden else [4])
    starts = [0]                          # 各块的起始列
    for bu in units[:-1]: starts.append(starts[-1] + bu)
    total_u = sum(units)
    u = (areaW - (len(units) - 1) * gap) / float(total_u)      # 22u + 2*gap = areaW
    keyH = int(round(keyH if keyH else u - gap))               # 默认键帽 = 正方形（pitch - gap）

    bounds = starts[1:]                   # 这些列之前各插一个块间 gap
    def px(col):
        extra = sum(1 for b in bounds if b <= col) * gap
        return areaX + int(round(col * u)) + extra

    def put(yy, cs, sp, hh, label, kind, bstart):
        xa, xb = px(bstart + cs), px(bstart + cs + sp)
        K.append(dict(x=xa, y=yy, w=xb - xa - gap, h=hh, t=label, kind=kind))

    K = []
    def row(yy, bidx, defs):
        for d in defs:
            hh = keyH * d[4] + gap * (d[4] - 1) if len(d) > 4 else keyH
            put(yy, d[1], d[2], hh, d[0], d[3], starts[bidx])

    y0 = F["headerH"] + gap + 2
    for r, defs in enumerate(ROW_MAIN):
        row(y0 + r * (keyH + gap), 0, defs)
    for r, defs in enumerate(ROW_NAV):            # 导航区占满 6 行
        row(y0 + r * (keyH + gap), 1, defs)
    if not np_hidden:                             # 数字区 5 行：数字行 → Ctrl 行
        for r, defs in enumerate(ROW_NUM):
            row(y0 + (r + 1) * (keyH + gap), 2, defs)
    if wh is None:
        wh = F["headerH"] + gap + 2 + 6 * keyH + 5 * gap + max(ci(16 * dpi), 8)
    return dict(keys=K, u=u, keyH=keyH, gap=gap, areaX=areaX, areaW=areaW,
                xs=[px(s) for s in starts], ww=ww, wh=wh, y0=y0, units=units)

# ---------------- 度量 ----------------
def row_stats(L, areaX, areaW, u, gap, keyH, y0, nrow=6):
    out = []
    for r in range(nrow):
        y = y0 + r * (keyH + gap)
        ks = [k for k in L["keys"] if k["y"] == y and k["x"] < areaX + areaW * 0.75]
        if not ks: out.append(None); continue
        ws = [k["w"] for k in ks]
        out.append(dict(n=len(ks), wmin=min(ws), wmax=max(ws),
                        right=max(k["x"] + k["w"] for k in ks)))
    return out

def col_of(L, label):
    """取最左侧的同名键（避开数字区里的重名键）。"""
    hit = [k for k in L["keys"] if k["t"] == label]
    if not hit: return None, None
    k = min(hit, key=lambda z: z["x"])
    return k["x"], k["w"]

# 关键键位的理想列位（单位 u，ANSI 104）
IDEAL = {"1": 1.0, "2": 2.0, "3": 3.0, "p": 10.5, "q": 1.5, "a": 1.75,
         "z": 2.25, "m": 8.25, "/": 11.25, "\\": 13.5, "Tab": 0.0,
         "Caps": 0.0, "Esc": 0.0}
def drift(L, u, areaX):
    out = []
    for lab in ["Esc", "1", "2", "3", "q", "p", "a", "z", "m", "/", "\\"]:
        x, w = col_of(L, lab)
        if x is None: continue
        ideal = areaX + IDEAL[lab] * u
        out.append(dict(k=lab, x=x, w=w, ideal=ideal, d=x - ideal, du=(x - ideal) / u))
    return out


# 双符号键（与 GetSymForKey 同表）：主区普通键显示两行
SHIFT_SYM = {"`": "~", "1": "!", "2": "@", "3": "#", "4": "$", "5": "%", "6": "^",
             "7": "&", "8": "*", "9": "(", "0": ")", "-": "_", "=": "+", "[": "{",
             "]": "}", "\\": "|", ";": ":", "'": "\"", ",": "<", ".": ">", "/": "?"}
def apply_dual(L, main_right):
    for k in L["keys"]:
        if k["x"] < main_right and k["kind"] in ("n", "l") and k["t"] in SHIFT_SYM:
            k["t"] = SHIFT_SYM[k["t"]] + "\n" + k["t"]
    return L

# ================= 渲染：HTML 对照页 =================
def esc(s):
    return (s.replace("&", "&amp;").replace("<", "&lt;").replace(">", "&gt;"))

KIND_STYLE = {
    "n":   ("card", "text"),
    "l":   ("card", "text"),
    "sp":  ("card", "text"),
    "mod": ("reg", "content"),
    "arr": ("reg", "content"),
}
def render(L, theme, s, label_size, show_grid=False, grid_cols=None, grid_u=None,
           grid_color=None, crop=None, extra=""):
    t = TOK[theme]
    x0 = L["areaX"]; areaW = L["areaW"]
    cw, ch, ox, oy = (L["ww"], L["wh"], 0.0, 0.0)
    if crop:
        cx, cy, w, h = crop
        cw, ch, ox, oy = w, h, cx, cy
    out = ['<div class="kbcrop" style="width:%.0fpx;height:%.0fpx">' % (cw * s, ch * s)]
    out.append('<div class="kb %s" style="width:%.0fpx;height:%.0fpx;margin-left:%.1fpx;margin-top:%.1fpx">'
               % (theme, L["ww"] * s, L["wh"] * s, -ox * s, -oy * s))
    hh = L["F"]["headerH"] if "F" in L else 44
    out.append('<div class="hd" style="height:%.0fpx;background:%s"></div>' % (hh * s, t["page"]))
    for k in L["keys"]:
        bgk, fgk = KIND_STYLE.get(k["kind"], ("card", "text"))
        lab = k["t"]
        extra_css = ""
        if "\n" in lab:
            a, b = lab.split("\n", 1)
            inner = '<span class="du">%s</span><span>%s</span>' % (esc(a), esc(b))
            extra_css = ";flex-direction:column;line-height:1.02"
        else:
            inner = esc(lab) if lab else "&nbsp;"
        out.append('<div class="k" style="left:%.1fpx;top:%.1fpx;width:%.1fpx;height:%.1fpx;'
                   'background:%s;color:%s;border-color:%s;border-radius:%.1fpx;font-size:%.1fpx%s">%s</div>'
                   % (k["x"] * s, k["y"] * s, k["w"] * s, k["h"] * s,
                      t[bgk], t[fgk], t["outline"], 8 * s, label_size, extra_css, inner))
    if show_grid and grid_u:
        col = grid_color or "rgba(255,110,110,.55)"
        for i in range(grid_cols + 1):
            gx = x0 + i * grid_u
            if gx > x0 + areaW + 1: break
            heavy = " glM" if i in (0, 15, 18, 22) else ""
            out.append('<div class="gl%s" style="left:%.1fpx;background:%s"></div>' % (heavy, gx * s, col))
    out.append(extra)
    out.append('</div></div>')
    return "\n".join(out)

def report(name, L, keyH, y0, gap, areaW, areaX, u=None):
    print("\n=== %s （窗口 %d×%d，键高 %d，gap %d）===" % (name, L["ww"], L["wh"], keyH, gap))
    if u: print("  1u（列距）= %.2f px" % u)
    for r in range(6):
        y = y0 + r * (keyH + gap)
        ks = [k for k in L["keys"] if k["y"] == y]
        if not ks: continue
        ws = [k["w"] for k in ks]
        right = max(k["x"] + k["w"] for k in ks)
        uu = "" if not u else "  键帽 %.2fu~%.2fu" % (min(ws) / u, max(ws) / u)
        print("  行%d  n=%2d  键宽 %3d~%-3d  右边缘 %4d%s" % (r, len(ks), min(ws), max(ws), right, uu))
    if u:
        print("  --- 关键键位偏离 ANSI 列位 ---")
        for d in drift(L, u, areaX):
            print("    %-4s x=%4d w=%3d  理想 %6.1f  偏差 %+6.1f px (%+.2fu)"
                  % (d["k"], d["x"], d["w"], d["ideal"], d["d"], d["du"]))

if __name__ == "__main__":
    cur = cur_layout(1380, 372)
    u_main = (cur["MAIN_W"] + cur["gap"]) / 15.0
    print("现状默认窗口 1380×372")
    report("现状", dict(cur, ww=1380, wh=372), cur["keyH"], cur["F"]["y0"], cur["gap"],
           cur["areaW"], cur["areaX"], u=u_main)
    print("  → 主区列距 1u = %.2f px；导航/数字区键帽 %d px = %.2fu（应 1.00u）"
          % (u_main, cur["navW"], cur["navW"] / u_main))

    unit = unit_layout(ww=1280)
    print("\n提案默认窗口 %d×%d" % (unit["ww"], unit["wh"]))
    report("提案", unit, unit["keyH"], unit["y0"], unit["gap"], unit["areaW"],
           unit["areaX"], u=unit["u"])
    print("  块起点 x = %s" % [round(v, 1) for v in unit["xs"]])

    unit2 = unit_layout(ww=1000, np_hidden=True)
    print("\n窄屏自适应（<1090 DIP 自动隐藏数字区）：%d×%d  u=%.1f" % (unit2["ww"], unit2["wh"], unit2["u"]))
    print("  块起点 x = %s" % [round(v, 1) for v in unit2["xs"]])

    # ---- 自适应阈值 ----
    print("\n--- 最小宽度（键帽≥44px 触摸安全）---")
    for w in range(760, 1400, 10):
        F = frame(w, 1)
        u = (F["areaW"] - 2 * F["gap"]) / 22.0
        if u - F["gap"] >= 44:
            print("  全尺寸（含数字区）最小窗口宽 ≈ %d DIP（键帽 %.0f px）" % (w, u - F["gap"])); break
    for w in range(600, 1400, 10):
        F = frame(w, 1)
        u = (F["areaW"] - F["gap"]) / 18.0
        if u - F["gap"] >= 44:
            print("  隐藏数字区后最小窗口宽 ≈ %d DIP（键帽 %.0f px）" % (w, u - F["gap"])); break



# ================= 生成对照页 =================
def build_html(out_path):
    cur = cur_layout(1380, 372)
    u_main = (cur["MAIN_W"] + cur["gap"]) / 15.0
    prop = unit_layout(ww=1280)
    apply_dual(cur, cur["areaX"] + cur["MAIN_W"])
    apply_dual(prop, prop["areaX"] + prop["areaW"])
    yc, yp = cur["F"]["y0"], prop["y0"]
    kc, kp, gp = cur["keyH"], prop["keyH"], cur["gap"]

    S = 0.66
    LS = 9.5
    # ---- A. 整键盘 ----
    a_cur = render(cur, "light", S, LS, True, 16, u_main)
    a_pro = render(prop, "light", S, LS, True, 22, prop["u"], "rgba(70,190,130,.55)")
    a_pro_dark = render(prop, "dark", S, LS, True, 22, prop["u"], "rgba(70,200,140,.45)")

    # ---- B. 主区左半 局部（放大 1.55） ----
    Z = 1.55
    bc = (6, yc - 4, 410, 5 * (kc + gp) + kc + 4)
    bp = (6, yp - 4, 410, 5 * (kp + gp) + kp + 4)
    b_cur = render(cur, "light", Z, 15, True, 8, u_main, "rgba(230,80,80,.55)", crop=bc)
    b_pro = render(prop, "light", Z, 15, True, 8, prop["u"], "rgba(40,170,110,.60)", crop=bp)

    # ---- C. 数字区 局部（放大 1.55） ----
    cc = (cur["ww"] - 320, yc - 4, 314, cur["wh"] - yc + 4)
    cp = (prop["ww"] - 276, yp - 4, 270, prop["wh"] - yp + 4)
    c_cur = render(cur, "light", Z, 15, crop=cc)
    c_pro = render(prop, "light", Z, 15, crop=cp)

    # ---- D. 数据表 ----
    rows = []
    for r, (y1, y2) in enumerate(zip([yc + i * (kc + gp) for i in range(6)],
                                     [yp + i * (kp + gp) for i in range(6)])):
        n1 = [k for k in cur["keys"] if k["y"] == y1]
        n2 = [k for k in prop["keys"] if k["y"] == y2]
        if not n1 or not n2: continue
        w1 = sorted(set(k["w"] for k in n1)); w2 = sorted(set(k["w"] for k in n2))
        name = ["F 行", "数字行", "Tab 行", "Caps 行", "Shift 行", "Ctrl 行"][r]
        rows.append((name,
                     "%d~%d (%.2f~%.2fu)" % (min(w1), max(w1), min(w1) / u_main, max(w1) / u_main),
                     "%d~%d" % (min(w2), max(w2)),
                     max(k["x"] + k["w"] for k in n1), max(k["x"] + k["w"] for k in n2)))
    dt = "\n".join(
        "<tr><td>%s</td><td class=bad>%s</td><td class=ok>%s</td><td>%d</td><td>%d</td></tr>" % r
        for r in rows)

    dr = "\n".join(
        "<tr><td>%s</td><td>%d</td><td>%.1f</td><td class=%s>%+.1f px (%+.2fu)</td></tr>"
        % (d["k"], d["x"], d["ideal"], "bad" if abs(d["d"]) > 2 else "ok", d["d"], d["du"])
        for d in drift(cur, u_main, cur["areaX"]))

    html = """<!DOCTYPE html><html lang="zh-CN"><head><meta charset="utf-8">
<title>HKeyboard 全尺寸布局 · 单位网格化对照</title>
<style>
:root{color-scheme:dark}
body{margin:0;background:#0d0d10;color:#d7d3e0;font:14px/1.65 "MiSans","Segoe UI",system-ui,sans-serif}
.wrap{max-width:1080px;margin:0 auto;padding:34px 26px 80px}
h1{font-size:22px;margin:0 0 6px;color:#fff;font-weight:600}
h2{font-size:16px;margin:38px 0 12px;color:#c9b8ff;font-weight:600;
   border-left:3px solid #7c5cff;padding-left:10px}
p{color:#a9a4b8;margin:8px 0}
.panel{background:#141419;border:1px solid #262632;border-radius:14px;padding:16px;margin:14px 0}
.cap{font-size:12.5px;color:#8d88a0;margin:0 0 10px}
.cap b{color:#e2ddf0}
.kbcrop{position:relative;overflow:hidden;border:1px solid #2b2b38;border-radius:10px;background:#101015}
.kb{position:absolute;left:0;top:0;overflow:hidden;border-radius:10px}
.k{position:absolute;box-sizing:border-box;border:1px solid;display:flex;
   align-items:center;justify-content:center;font-weight:500;white-space:nowrap;overflow:hidden}
.hd{width:100%%}
.k .du{font-size:.78em;opacity:.68}
.gl{position:absolute;top:0;bottom:0;width:1px;opacity:.85}
.glM{width:2px;opacity:1}
.two{display:flex;gap:16px;flex-wrap:wrap}
.two>div{flex:1 1 420px}
table{width:100%%;border-collapse:collapse;font-size:12.5px;margin-top:6px}
th,td{text-align:left;padding:7px 9px;border-bottom:1px solid #242430}
th{color:#8d88a0;font-weight:500}
td.bad{color:#ff8f8f}td.ok{color:#6fdc9e}
code{background:#1c1c24;border:1px solid #2a2a36;border-radius:5px;padding:1px 5px;
     font-family:ui-monospace,Consolas,monospace;font-size:12px;color:#c9b8ff}
pre{background:#101015;border:1px solid #262632;border-radius:10px;padding:14px;
    overflow:auto;font:12px/1.6 ui-monospace,Consolas,monospace;color:#bdb7cc}
.legend{display:inline-flex;align-items:center;gap:6px;font-size:12px;color:#8d88a0;margin-right:16px}
.sw{width:14px;height:3px;display:inline-block}
</style></head><body><div class="wrap">
<h1>HKeyboard 全尺寸布局 · 从「逐行除法」到「1u 单位网格」</h1>
<p>两版都按 <code>hkeyboard.cpp</code> 的<span style="color:#e2ddf0">真实算式</span>逐键复算后渲染，不是示意图。
令牌取 hue=250（浅色 <code>#EAEFF5 / #E2F0FF / #3275B4</code>，与你线上配置一致）。</p>

<h2>一、整体对照</h2>
<div class="panel">
  <p class="cap"><b>现状</b> · 1380×372 · 列距 1u=%.1fpx <span class="legend"><i class="sw" style="background:rgba(230,80,80,.7)"></i>参考网格（每 1u 一条）</span></p>
  %s
</div>
<div class="panel">
  <p class="cap"><b>提案</b> · 1280×404 · 列距 1u=%.1fpx · 键帽 52×52 正方 <span class="legend"><i class="sw" style="background:rgba(60,190,130,.7)"></i>1u 网格</span></p>
  %s
</div>
<div class="panel" style="background:#0f0f14">
  <p class="cap"><b>提案（深色主题）</b></p>
  %s
</div>

<h2>二、列对齐（主区左半，放大 1.55×）</h2>
<div class="panel"><p class="cap"><b>现状</b>：红线 = ANSI 的每 1u 列位。四行首键各走各的，越往右偏得越多 —— p 偏 +0.47u、m 偏 +0.93u（整整一个键）</p>%s</div>
<div class="panel"><p class="cap"><b>提案</b>：q/a/z/m 全部落在红线（ANSI 列位）上，全程偏差 ≤0.5px</p>%s</div>

<h2>三、数字区（放大 1.55×）</h2>
<div class="panel"><p class="cap"><b>现状</b>：数字区整体高一行。回车只跨「1 2 3 行 + 空行」，它的下边缘在 0 行<b>之上</b>一整行 → 0 与回车底不齐</p>%s</div>
<div class="panel"><p class="cap"><b>提案</b>：数字区 5 行，对齐主区的「数字行 ↔ Ctrl 行」；+ 跨 789/456，回车跨 123/0. 且底边与 0 齐平</p>%s</div>

<h2>四、数据</h2>
<div class="panel">
<table><tr><th>行</th><th>现状 键帽宽 px（换算 u）</th><th>提案 键帽宽 px</th><th>现状 行右边缘</th><th>提案 行右边缘</th></tr>
%s</table>
</div>
<div class="panel">
<table><tr><th>键</th><th>实际 x</th><th>ANSI 理想 x</th><th>偏差</th></tr>
%s</table>
</div>

<h2>五、算法</h2>
<pre>// 1u 由宽度反解；所有列坐标统一 round(col * u)，余数自动摊掉 → 零累积漂移
double u = (KEY_AREA_W - (nblocks - 1) * gap) / 22.0;      // 22 = 15+3+4 单位
int X(double col) {                                          // col 为全局列位（单位 u）
    int b = 0;
    for (int i = 0; i &lt; nblocks - 1; i++) if (blockCol[i] &lt;= col) b++;   // 跨块补一个块间 gap
    return KEY_AREA_X + (int)lround(col * u) + b * gap;
}
// 一个 sp 单位的键：x = X(col)  w = X(col+sp) - X(col) - gap
// 行内不再出现除法，残差也不再需要 rem —— 现状唯一的算错（i &lt;= rem）随之消失</pre>
<p style="font-size:12.5px;color:#8d88a0">自适应阈值：键帽 ≥44px（触摸安全）时，全尺寸最小窗口宽 ≈ <b style="color:#e2ddf0">1090</b> DIP；
隐藏数字区后 ≈ <b style="color:#e2ddf0">870</b> DIP。建议 <code>&lt;1090</code> 自动隐藏数字区、<code>&lt;870</code> 拒绝继续缩小。</p>
</div></body></html>""" % (u_main, a_cur, prop["u"], a_pro, a_pro_dark,
                          b_cur, b_pro, c_cur, c_pro, dt, dr)

    with io.open(out_path, "w", encoding="utf-8") as f:
        f.write(html)
    print("已写出 %s" % out_path)
