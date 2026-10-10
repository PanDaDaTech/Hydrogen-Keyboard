# -*- coding: utf-8 -*-
"""生成「小键盘按钮」对照页：真实令牌 + 真实几何 + hk_icons_generated.h 的真实图标顶点。"""
import io, re, math, sys

REPO = r"E:/Project/Git/Hydrogen-Keyboard"
HDR  = REPO + "/hk_icons_generated.h"
OUT  = r"C:/Users/29776/WorkBuddy/2026-09-25-19-32-21/hk_小键盘按钮-数字区开关对照.html"

# ---------------- 色彩：照抄 HkbTokensBuild ----------------
def oklch(L, C, h):
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
def blend(A, B, t): return tuple(int(round(A[i] + (B[i]-A[i])*t)) for i in range(3))
def overlay(base, white, alpha): return blend(base, (255,255,255) if white else (0,0,0), alpha)
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
def tokens(dark):
    T = {}
    T['primary']   = oklch(0.75,0.140,H) if dark else oklch(0.70,0.140,H)
    T['pageBg']    = oklch(0.16,0.014,H) if dark else oklch(0.95,0.010,H)
    T['cardBg']    = oklch(0.23,0.015,H) if dark else (255,255,255)
    T['btnReg']    = oklch(0.33,0.035,H) if dark else oklch(0.95,0.025,H)
    T['btnRegHov'] = oklch(0.38,0.040,H) if dark else oklch(0.90,0.050,H)
    T['btnContent']= oklch(0.75,0.100,H) if dark else oklch(0.55,0.120,H)
    T['textMuted'] = overlay(T['pageBg'], dark, 0.60)
    T['primaryFg'] = blend(T['primary'], (0,0,0), 0.70) if dark else oklch(0.25,0.020,H)
    T['activeHov'] = blend(T['primary'], (255,255,255), 0.12)
    # 反面教材：向 cardBg 混
    T['activeHovBad'] = blend(T['primary'], T['cardBg'], 0.12)
    return {k: (hx(v) if isinstance(v, tuple) else v) for k, v in T.items()}
TL, TD = tokens(False), tokens(True)

# ---------------- 图标：解析 hk_icons_generated.h ----------------
def load_icons(path):
    txt = io.open(path, encoding="utf-8").read()
    icons = {}
    for m in re.finditer(r'static const HkIconNode (k_icon_\w+)\[\] = \{(.*?)\n\};', txt, re.S):
        name, body = m.group(1), m.group(2)
        nodes = []
        for n in re.finditer(r'\{\s*(HKIC_\w+),([^}]*)\}', body):
            op = n.group(1)
            vals = []
            for x in n.group(2).split(','):
                x = x.strip()
                if not x: continue
                vals.append(float(x.rstrip('f')))
            nodes.append((op, vals))
        icons[name] = nodes
    grids = {k: float(v) for k, v in re.findall(r'\{\s*(k_icon_\w+),\s*([\d.]+)f\s*\}', txt)}
    return icons, grids

ICONS, GRIDS = load_icons(HDR)

def icon_svg(name, color, size):
    nodes = ICONS.get(name)
    if not nodes: return "<!-- 缺图标 %s -->" % name
    grid = GRIDS.get(name, 24.0)
    subs, cur = [], None
    for op, v in nodes:
        if op == 'HKIC_BEGIN':
            cur = dict(paint=int(v[0]), w=v[1], d=[])
            subs.append(cur)
        elif cur is None: continue
        elif op == 'HKIC_MOVE':  cur['d'].append("M %.3f %.3f" % (v[0], v[1]))
        elif op == 'HKIC_LINE':  cur['d'].append("L %.3f %.3f" % (v[0], v[1]))
        elif op == 'HKIC_CUBIC': cur['d'].append("C %.3f %.3f %.3f %.3f %.3f %.3f" % tuple(v[:6]))
        elif op == 'HKIC_CLOSE': cur['d'].append("Z")
    out = []
    for p in subs:
        d = " ".join(p['d'])
        if not d: continue
        if p['paint'] == 0:
            out.append('<path d="%s" fill="none" stroke="%s" stroke-width="%.2f" '
                       'stroke-linecap="round" stroke-linejoin="round"/>' % (d, color, p['w']))
        else:
            out.append('<path d="%s" fill="%s"/>' % (d, color))
    return ('<svg viewBox="0 0 %.4g %.4g" width="%.4g" height="%.4g" '
            'style="flex:0 0 auto">%s</svg>' % (grid, grid, size, size, "".join(out)))

# ---------------- 标题栏几何：照抄 GetHeaderMetrics ----------------
def header(ww, active, dark, hov=None, scale=1.0, show_num=True, label_cn=True):
    T = TD if dark else TL
    dpi = 1.0
    headerH = 44
    btnH, btnY = 28, 10
    gap, rMargin = 6, 6
    wMenu, wClose, wMin, wNum = 68, 28, 28, 84
    xClose = ww - rMargin - wClose
    xMin   = xClose - gap - wMin
    xNum   = (xMin - gap - wNum) if show_num else xMin
    xMenu  = 6

    def pill(x, w, act, h, icon, label):
        if act:
            bg = T['activeHov'] if h else T['primary']
            fg = T['primaryFg']
        else:
            bg = T['btnRegHov'] if h else T['btnReg']
            fg = T['btnContent']
        return ('<div style="position:absolute;left:%.1fpx;top:%.1fpx;width:%.1fpx;height:%.1fpx;'
                'background:%s;border-radius:%.1fpx;display:flex;align-items:center;'
                'justify-content:center;gap:6px;font:500 12px/1 \'MiSans\',\'Microsoft YaHei UI\','
                '\'Microsoft YaHei\',sans-serif;color:%s">%s<span>%s</span></div>'
                % (x, btnY, w, btnH, bg, btnH/2, fg, icon_svg(icon, fg, 17), label))

    parts = [pill(xMenu, wMenu, False, hov == 'menu', 'k_icon_Gear',
                  '设置' if label_cn else 'Settings')]
    if show_num:
        parts.append(pill(xNum, wNum, active, hov == 'num', 'k_icon_Numpad',
                          '小键盘' if label_cn else 'Numpad'))
    parts.append('<div style="position:absolute;left:%.1fpx;top:%.1fpx;width:%.1fpx;height:%.1fpx;'
                 'border-radius:6px;display:flex;align-items:center;justify-content:center;'
                 'background:%s">%s</div>'
                 % (xMin, btnY, wMin, btnH, T['btnRegHov'] if hov == 'min' else 'transparent',
                    icon_svg('k_icon_Minimize', T['textMuted'], 18)))
    parts.append('<div style="position:absolute;left:%.1fpx;top:%.1fpx;width:%.1fpx;height:%.1fpx;'
                 'border-radius:6px;display:flex;align-items:center;justify-content:center;'
                 'background:%s">%s</div>'
                 % (xClose, btnY, wClose, btnH, T['btnRegHov'] if hov == 'close' else 'transparent',
                    icon_svg('k_icon_Close', T['textMuted'], 18)))
    return ('<div class="hdbar" style="width:%.1fpx;height:%.1fpx;background:%s;position:relative;'
            'border-radius:8px;transform:scale(%.4f);transform-origin:0 0">%s</div>'
            % (ww, headerH, T['pageBg'], scale, "".join(parts)))

def hd_block(ww, active, dark, hov=None, scale=1.0, show_num=True, note=""):
    inner = header(ww, active, dark, hov, scale, show_num)
    return ('<div class="hdwrap" style="width:%.1fpx;height:%.1fpx">%s</div>%s'
            % (ww*scale, 44*scale, inner,
               ('<p class="cap" style="margin-top:8px">%s</p>' % note) if note else ""))

# ---------------- 按钮四态矩阵 ----------------
def state_chip(dark, state):
    T = TD if dark else TL
    if state == 'norm':  bg, fg = T['btnReg'], T['btnContent']
    elif state == 'hov': bg, fg = T['btnRegHov'], T['btnContent']
    elif state == 'act': bg, fg = T['primary'], T['primaryFg']
    else:                bg, fg = T['activeHov'], T['primaryFg']
    return ('<div class="chip"><div class="pill" style="background:%s;color:%s">%s<span>小键盘</span></div>'
            '<em>%s<br><code>%s</code></em></div>'
            % (bg, fg, icon_svg('k_icon_Numpad', fg, 17),
               {'norm':'普通','hov':'普通 · 悬停','act':'激活','acthov':'激活 · 悬停'}[state],
               bg))

def cr_row(name, bg, fg, dark):
    r = cr(tuple(int(fg[i:i+2],16) for i in (1,3,5)), tuple(int(bg[i:i+2],16) for i in (1,3,5)))
    cls = "ok" if r >= 4.5 else "bad"
    return ('<tr><td>%s</td><td><code>%s</code></td><td><code>%s</code></td>'
            '<td class="%s">%.2f:1</td></tr>' % (name, bg, fg, cls, r))

Tcr = ""
for tag, T in (("浅色", TL), ("深色", TD)):
    Tcr += '<tr class="sep"><td colspan="4"><b>%s（hue 250）</b></td></tr>' % tag
    Tcr += cr_row("普通", T['btnReg'], T['btnContent'], tag)
    Tcr += cr_row("普通 · 悬停", T['btnRegHov'], T['btnContent'], tag)
    Tcr += cr_row("激活", T['primary'], T['primaryFg'], tag)
    Tcr += cr_row("激活 · 悬停（向白混 .12）", T['activeHov'], T['primaryFg'], tag)
    Tcr += cr_row("激活 · 悬停（<b>向 cardBg 混</b>，反面）", T['activeHovBad'], T['primaryFg'], tag)

# ---------------- 加宽：几何 ----------------
def geom(ww, np_hidden, dpi=1.0):
    sx = ww / (980.0*dpi)
    gap = max(int(4.0*dpi*sx), 2)
    keyAreaX = max(int(10*dpi), 6)
    keyAreaW = ww - 2*keyAreaX
    navW = int(52*dpi*sx)
    navBlockW = navW*3 + 2*gap
    numBlockW = navW*4 + 3*gap
    rest = 2*gap + navBlockW + (0 if np_hidden else numBlockW)
    raw = keyAreaW - rest
    MAIN_W = raw if raw >= 400 else 400
    right = keyAreaX + MAIN_W + rest + keyAreaX
    return dict(sx=sx, gap=gap, keyAreaX=keyAreaX, keyAreaW=keyAreaW, navW=navW,
                navBlockW=navBlockW, numBlockW=numBlockW, rest=rest,
                raw=raw, MAIN_W=MAIN_W, right=right)

def wbar(ww, label, T, S=0.55):
    """一条窗口：左边距 | 主区 | gap | 导航 | gap | 数字区 | 右边距。
       所有 px 直接乘 S（不用 CSS transform，免得把 10.5px 的字缩成 6px 看不清）。"""
    g = geom(ww, False)
    ok = g['right'] <= ww
    W = max(g['right'], ww) * S
    def seg(x, w, color, txt):
        return ('<div class="seg" style="left:%.1fpx;width:%.1fpx;background:%s">%s</div>'
                % (x*S, w*S, color, txt))
    parts = []
    parts.append(seg(0, g['keyAreaX'], "rgba(255,255,255,.05)", ""))
    parts.append(seg(g['keyAreaX'], g['MAIN_W'], T['cardBg'], "主区 15u"))
    nx = g['keyAreaX'] + g['MAIN_W'] + g['gap']
    parts.append(seg(nx, g['navBlockW'], T['btnReg'], "导航 3u"))
    numx = nx + g['navBlockW'] + g['gap']
    parts.append(seg(numx, g['numBlockW'], T['primary'], "<b>数字区 4u</b>"))
    live = ('<div class="liverect" style="width:%.1fpx"></div>' % (ww*S))
    over = ('<div class="overrect" style="left:%.1fpx;width:%.1fpx"></div>'
            % (ww*S, (g['right']-ww)*S))
    return ('<div class="wrow"><span class="wlab">%s</span>'
            '<div class="wcol"><div class="wbar" style="width:%.1fpx;height:34px">%s%s%s</div>'
            '<div class="wtag %s">%s</div></div></div>'
            % (label, W, "".join(parts), live, over,
               "ok" if ok else "bad",
               ("数字区完整可见（右边缘 %d px）" % g['right']) if ok
               else ("<b>被裁掉 %d px</b>" % (g['right']-ww))))

WT = ""
for ww in [600, 700, 900, 1380]:
    WT += wbar(ww, "窗口 %d 宽" % ww, TL)

# ---------------- 组装 ----------------
html = """<!DOCTYPE html><html lang="zh-CN"><head><meta charset="utf-8">
<title>HKeyboard 小键盘按钮 · 数字区开关</title><style>
:root{color-scheme:dark}
body{margin:0;background:#0d0d10;color:#d7d3e0;font:14px/1.65 "MiSans","Microsoft YaHei UI","Segoe UI",system-ui,sans-serif}
.wrap{max-width:1000px;margin:0 auto;padding:34px 26px 80px}
h1{font-size:22px;margin:0 0 6px;color:#fff;font-weight:600}
h2{font-size:16px;margin:40px 0 12px;color:#c9b8ff;font-weight:600;border-left:3px solid #7c5cff;padding-left:10px}
p{color:#a9a4b8;margin:8px 0}
.panel{background:#141419;border:1px solid #262632;border-radius:14px;padding:16px;margin:14px 0}
.panel.dark{background:#0b0b0e;border-color:#1e1e28}
.cap{font-size:12.5px;color:#8d88a0;margin:0 0 10px}
.cap b{color:#e2ddf0}
.hdwrap{overflow:hidden;border-radius:8px;margin:6px 0 4px}
.hdbar{transform-origin:0 0}
.chipbg{display:flex;gap:24px;flex-wrap:wrap;align-items:flex-start;padding:20px 16px;border-radius:10px}
.chipbg.light{background:#EAEFF5}
.chipbg.dark{background:#090E13}
.chip{display:flex;flex-direction:column;gap:8px;align-items:center}
.pill{height:28px;border-radius:14px;display:inline-flex;align-items:center;
      justify-content:center;gap:6px;font:500 12px/1 "MiSans","Microsoft YaHei UI",sans-serif;padding:0 15px}
.chip em{font-style:normal;font-size:11.5px;color:#8d88a0;text-align:center;line-height:1.5}
.chipbg.light em{color:#5A6675}
.chipbg.light code{background:rgba(0,0,0,.06);border-color:rgba(0,0,0,.12);color:#3275B4}
table{width:100%;border-collapse:collapse;font-size:12.5px;margin-top:6px}
th,td{text-align:left;padding:7px 9px;border-bottom:1px solid #242430}
th{color:#8d88a0;font-weight:500}
tr.sep td{background:#191922;color:#c9b8ff}
td.ok{color:#6fdc9e;font-weight:600}td.bad{color:#ff8f8f;font-weight:600}
code{background:#1c1c24;border:1px solid #2a2a36;border-radius:5px;padding:1px 5px;
     font-family:ui-monospace,Consolas,monospace;font-size:12px;color:#c9b8ff}
.wrow{display:flex;align-items:flex-start;gap:12px;margin:16px 0 4px}
.wlab{width:96px;flex:0 0 auto;font-size:12.5px;color:#8d88a0;text-align:right;padding-top:9px}
.wcol{display:flex;flex-direction:column;gap:6px}
.wbar{position:relative;flex:0 0 auto;border-radius:6px;overflow:visible;border:1px dashed #3a3a48}
.seg{position:absolute;top:0;bottom:0;display:flex;align-items:center;justify-content:center;
     font-size:10.5px;color:#6e6a80;overflow:hidden;white-space:nowrap}
.seg:first-child{border-radius:6px 0 0 6px}
.liverect{position:absolute;top:-3px;bottom:-3px;left:0;border:2px solid #6fdc9e;border-radius:8px;
          pointer-events:none}
.overrect{position:absolute;top:-3px;bottom:-3px;border:2px dashed #ff8f8f;border-radius:8px;
          background:repeating-linear-gradient(45deg,rgba(255,110,110,.18) 0 5px,transparent 5px 10px);
          pointer-events:none}
.wtag{font-size:12px;color:#8d88a0;flex:0 0 auto}
.wtag.ok{color:#6fdc9e}.wtag.bad{color:#ff8f8f}
.two{display:flex;gap:16px;flex-wrap:wrap}
.two>div{flex:1 1 380px}
.legend{display:inline-flex;align-items:center;gap:6px;font-size:12px;color:#8d88a0;margin-right:18px}
.sw{width:14px;height:10px;border-radius:3px;display:inline-block}
</style></head><body><div class="wrap">

<h1>标题栏「小键盘」按钮 → 数字区开关</h1>
<p>页面里所有颜色都是 <code>HkbTokensBuild()</code> 在 <b>hue 250</b> 下的实算值，
几何是 <code>GetHeaderMetrics()</code> 的逐项照抄，<b>图标是 <code>hk_icons_generated.h</code>
里的真实顶点</b>转出来的 —— 不是示意图。</p>

<h2>一、四种局面下的实际样子</h2>
<p>按钮的激活语义统一为「<b>小键盘当前是开着的</b>」：独立小键盘布局算开，全尺寸下数字区可见算开。</p>

<div class="panel">
  <p class="cap"><b>默认布局</b> · 980 宽 · 按钮 = 切到独立小键盘 &nbsp;→&nbsp; 非激活</p>
  __HD1__
</div>

<div class="panel">
  <p class="cap"><b>全尺寸 · 数字区隐藏</b> · 1380 宽 · 按钮 = 显示数字区 &nbsp;→&nbsp; 非激活</p>
  __HD2__
</div>

<div class="panel">
  <p class="cap"><b>全尺寸 · 数字区显示</b> · 1380 宽 · 按钮 = 隐藏数字区 &nbsp;→&nbsp; <b>激活（主色实底）</b></p>
  __HD3__
</div>

<div class="panel">
  <p class="cap"><b>独立小键盘布局</b> · 430 宽 · 按钮 = 切回上一个布局 &nbsp;→&nbsp; <b>激活</b></p>
  __HD4__
</div>

<h2>二、按钮四态（浅色 / 深色）</h2>
<div class="panel"><p class="cap"><b>浅色</b> · 摆在真实页头底 <code>#EAEFF5</code> 上</p>
  <div class="chipbg light">__CHIPS_L__</div></div>
<div class="panel"><p class="cap"><b>深色</b> · 摆在真实页头底 <code>#090E13</code> 上</p>
  <div class="chipbg dark">__CHIPS_D__</div></div>
<p>激活态用的是 <b>早就存在的</b> <code>C_HOT</code> 与 <code>C_ON_PRIMARY</code>
（Enter 键在用），<b>一个新令牌都不用加</b>。</p>

<h2>三、对比度实测</h2>
<div class="panel">
<table><tr><th>状态</th><th>底</th><th>前景</th><th>对比度</th></tr>__CR__</table>
<p style="margin-top:12px">两个要点：</p>
<p>① <b>激活态比普通态还清楚</b>（浅 6.03 vs 4.18，深 5.58 vs 5.50）。
原因是 <code>primaryFg</code> 是纯文字色，而 <code>btnContent</code> 是带色相的蓝。</p>
<p>② <b>激活悬停必须向白混</b>。看表里最后一行：向 <code>cardBg</code> 混的话，深色主题下
对比掉到 4.54:1 —— 那是 <code>cardBg #1E1B23</code> 是暗色、把主色压暗了的缘故。
写死 <code>BlendColor(C_HOT, RGB(255,255,255), 0.12)</code> 就没这个问题。</p>
</div>

<h2>四、窗口加宽：只在真会被挤出去时才动尺寸</h2>
<p>判据是 <code>BuildComplete()</code> 里那句 <code>if (MAIN_W &lt; 400) MAIN_W = 400;</code>
—— <b>这句一触发，数字区就已经跑到窗口外面了</b>。闭式解给出临界宽度
<code>needW = 700 × dpi</code>。</p>
<div class="panel">
  <p class="cap">
    <span class="legend"><i class="sw" style="background:#6fdc9e"></i>绿框 = 窗口实际范围</span>
    <span class="legend"><i class="sw" style="background:#ff8f8f"></i>红斜纹 = 被裁掉的部分</span>
  </p>
  __WBAR__
  <p style="margin-top:14px">1380 是默认宽度，<b>点按钮不会有任何尺寸变化</b>；
  只有用户先把窗口拖窄，加宽才会发生。隐藏数字区时<b>不缩回</b> —— 自动缩回是"手比你快"的冒犯。</p>
</div>

<h2>五、状态机</h2>
<div class="panel">
<table>
<tr><th>当前布局</th><th>点击行为</th><th>激活态</th></tr>
<tr><td>默认 <code>mode 0</code></td><td>记住 <code>g_prevLayout</code>，切到独立小键盘 <code>mode 1</code></td><td>否</td></tr>
<tr><td>独立小键盘 <code>mode 1</code></td><td>切回 <code>g_prevLayout</code>（<b>唯一出口，不能删</b>）</td><td><b>是</b></td></tr>
<tr><td>全尺寸 <code>mode 2</code></td><td><code>g_npHidden = !g_npHidden</code>（必要时先加宽窗口）</td><td><b>数字区可见时 = 是</b></td></tr>
</table>
<p style="margin-top:12px">按钮与 Tab 键<b>必须走同一个函数</b>
（<code>SetFullNumpadHidden()</code>）—— 现在这两条路径是各改各的 <code>g_npHidden</code>，迟早分叉。</p>
</div>

</div></body></html>"""

CHIPS_L = "".join(state_chip(False, s) for s in ('norm','hov','act','acthov'))
CHIPS_D = "".join(state_chip(True,  s) for s in ('norm','hov','act','acthov'))

html = (html.replace("__HD1__", hd_block(980,  False, False, scale=0.92,
                       note="非激活：中性浅底 <code>#E2F0FF</code> + 蓝色字 <code>#3275B4</code>"))
            .replace("__HD2__", hd_block(1380, False, False, scale=0.66,
                       note="数字区关着 → 按钮也是中性浅底"))
            .replace("__HD3__", hd_block(1380, True, False, scale=0.66,
                       note="数字区开着 → 按钮换成主色实底 <code>#53A3F2</code> + 深字 <code>#1A222B</code>"))
            .replace("__HD4__", hd_block(430,  True, False, scale=0.92,
                       note="独立小键盘布局整块就是小键盘 → 同样算「开着」"))
            .replace("__CHIPS_L__", CHIPS_L)
            .replace("__CHIPS_D__", CHIPS_D)
            .replace("__CR__", Tcr)
            .replace("__WBAR__", WT))

io.open(OUT, "w", encoding="utf-8").write(html)
print("已写出 %s  (%d 字节)" % (OUT, len(html.encode("utf-8"))))
print("解析到图标：%d 个" % len(ICONS))
