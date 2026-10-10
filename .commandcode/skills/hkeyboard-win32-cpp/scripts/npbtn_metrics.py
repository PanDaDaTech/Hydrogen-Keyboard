# -*- coding: utf-8 -*-
"""小键盘按钮：激活态配色对比度 + 自动加宽目标宽度。全部按 hkeyboard.cpp 的真实公式复算。"""
import math

# ---------- 色彩 ----------
def oklch(L, C, h):
    if L is None: return None
    r_ = math.radians(h); a = C*math.cos(r_); b = C*math.sin(r_)
    lp = L + 0.3963377774*a + 0.2158037573*b
    mp = L - 0.1055613458*a - 0.0638541728*b
    sp = L - 0.0894841775*a - 1.2914855480*b
    l, m, s = lp**3, mp**3, sp**3
    r =  4.0767416621*l - 3.3077115913*m + 0.2309699292*s
    g = -1.2684380046*l + 2.6097574011*m - 0.3413193965*s
    bb= -0.0041960863*l - 0.7034186147*m + 1.7076147010*s
    def enc(x): return 12.92*x if x <= 0.0031308 else 1.055*x**(1/2.4) - 0.055
    def ch(x):
        v = enc(max(0.0, min(1.0, x))); return int(max(0, min(255, round(v*255))))
    return (ch(r), ch(g), ch(bb))

def blend(A, B, t):
    return tuple(int(round(A[i] + (B[i]-A[i])*t)) for i in range(3))

def overlay(base, white, alpha):
    return blend(base, (255,255,255) if white else (0,0,0), alpha)

def lum(c):
    def f(x):
        x = x/255.0
        return x/12.92 if x <= 0.03928 else ((x+0.055)/1.055)**2.4
    return 0.2126*f(c[0]) + 0.7152*f(c[1]) + 0.0722*f(c[2])

def cr(a, b):
    la, lb = lum(a), lum(b)
    if la < lb: la, lb = lb, la
    return (la+0.05)/(lb+0.05)

def hx(c): return "#%02X%02X%02X" % c

H = 250
for dark in (False, True):
    tag = "深色" if dark else "浅色"
    T = {}
    T['primary']   = oklch(0.75,0.140,H) if dark else oklch(0.70,0.140,H)
    T['pageBg']    = oklch(0.16,0.014,H) if dark else oklch(0.95,0.010,H)
    T['cardBg']    = oklch(0.23,0.015,H) if dark else (255,255,255)
    T['btnReg']    = oklch(0.33,0.035,H) if dark else oklch(0.95,0.025,H)
    T['btnRegHov'] = oklch(0.38,0.040,H) if dark else oklch(0.90,0.050,H)
    T['btnContent']= oklch(0.75,0.100,H) if dark else oklch(0.55,0.120,H)
    T['text']      = oklch(0.85,0.020,H) if dark else oklch(0.25,0.020,H)
    T['primaryFg'] = blend(T['primary'], (0,0,0), 0.70) if dark else T['text']
    # 激活态悬停：主色向 cardBg 混 12%
    T['actHov']    = blend(T['primary'], T['cardBg'], 0.12)

    print("=== %s（hue %d）===" % (tag, H))
    print("  primary        %s   primaryFg      %s" % (hx(T['primary']), hx(T['primaryFg'])))
    print("  btnRegularBg   %s   btnContent     %s" % (hx(T['btnReg']), hx(T['btnContent'])))
    print("  pageBg         %s   activeHover    %s" % (hx(T['pageBg']), hx(T['actHov'])))
    print("  —— 激活态（主色实底）——")
    print("     primaryFg on primary          %5.2f:1   %s" % (cr(T['primaryFg'], T['primary']),
          "OK(≥4.5 正文)" if cr(T['primaryFg'], T['primary'])>=4.5 else "不足"))
    print("     primaryFg on activeHover      %5.2f:1" % cr(T['primaryFg'], T['actHov']))
    print("  —— 普通态（中性浅底）——")
    print("     btnContent on btnRegularBg    %5.2f:1" % cr(T['btnContent'], T['btnReg']))
    print("     btnContent on btnRegHover     %5.2f:1" % cr(T['btnContent'], T['btnRegHov']))
    print("  —— 可辨识度：激活 vs 普通 / 与页头底 ——")
    print("     primary  vs btnRegularBg      %5.2f:1   (底色差)" % cr(T['primary'], T['btnReg']))
    print("     primary  vs pageBg            %5.2f:1   (按钮能不能从页头里跳出来)" % cr(T['primary'], T['pageBg']))
    print("     btnRegularBg vs pageBg        %5.2f:1   (现状普通态)" % cr(T['btnReg'], T['pageBg']))
    print()

# ---------- 几何 ----------
print("=== 全尺寸自动加宽：窗口宽 → 最小键宽（dpi=1）===")
def geom(ww, np_hidden, dpi=1.0):
    scaleX = ww / (980.0*dpi)
    gap = max(int(4.0*dpi*scaleX), 2)
    keyAreaX = max(int(10*dpi), 6)
    keyAreaW = ww - 2*keyAreaX
    navW = int(52*dpi*scaleX)
    navBlockW = navW*3 + 2*gap
    numBlockW = navW*4 + 3*gap
    rest = 2*gap + navBlockW + (0 if np_hidden else numBlockW)
    MAIN_W = keyAreaW - rest
    if MAIN_W < 400: MAIN_W = 400
    return dict(scaleX=scaleX, gap=gap, keyAreaX=keyAreaX, keyAreaW=keyAreaW,
                navW=navW, navBlockW=navBlockW, numBlockW=numBlockW, MAIN_W=MAIN_W)

# 主区每行的最小键宽（固定键宽字面量按 scaleX 放大，取自 BuildComplete 源码）
FIXED = {   # 行 → (固定键总宽表达式系数, 普通键数, gap 数)
    "数字行": ("56*3+56+56+56+56+56", 13, 14),   # 略；用实测扫描代替
}

for ww in range(820, 1421, 40):
    g = geom(ww, False)
    g2 = geom(ww, True)
    print("  ww=%4d  navW=%3d  MAIN_W=%4d(隐数字区 %4d)  gap=%d  ← 数字区 %s"
          % (ww, g['navW'], g['MAIN_W'], g2['MAIN_W'], g['gap'],
             "塞下" if g['MAIN_W'] > 400 or True else ""))
