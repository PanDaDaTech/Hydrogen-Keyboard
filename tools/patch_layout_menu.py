# -*- coding: utf-8 -*-
"""A1：把布局切换收进标题栏按钮（窗口内弹层）。见 HKeyboard-UI方案评估 §三。

做法要点（避免留下不可达代码）：**不碰旧的 HDR_NUM 分支**，而是让 HitHeader 为这颗
胶囊返回新的 HDR_LAYOUT，OnLDown 里新 case 接管 —— 旧分支变成无人使用的死分支，
但仍可编译，回退时只需把 HitHeader 那一行改回去。

用法：python tools/patch_layout_menu.py [hkeyboard.cpp]
"""
import io, sys

p = sys.argv[1] if len(sys.argv) > 1 else "hkeyboard.cpp"
s = io.open(p, encoding="utf-8").read()
orig = s
rep = []


def rep1(name, old, new):
    global s
    if old and old in s:
        s = s.replace(old, new, 1)
        rep.append((name, 1))
    else:
        rep.append((name, 0))


MENU = r"""// ===== 布局菜单（A1：窗口内弹层，见 HKeyboard-UI方案评估 §三）=====
// 点标题栏按钮 → 按钮下方弹出：默认布局 / 小键盘 / 全尺寸（当前项带勾），
// 全尺寸布局时多一项「数字区」。不新建窗口、不抢前台。
// ⚠ 三件事必须一起做，少一件就是 bug：
//   ① 绘制排在按键之后 ② 状态进 KbFrameSig ③ UpdateAutoVisibility 里短路
static BOOL g_lmOpen = FALSE;
static int  g_lmHover = -1;

static void ApplyKeyboardLayout(BOOL resetSize);

static void SetLayoutMode(int mode) {
    if (mode < 0 || mode > 2 || mode == g_layoutMode) return;
    if (mode == 1) g_prevLayout = g_layoutMode;   // 记下「上次布局」，小键盘可回退
    g_layoutMode = mode;
    ApplyKeyboardLayout(TRUE);                    // 统一入口：落盘 + 按布局重置窗口尺寸
}

static int LayoutMenuItemCount() { return (g_layoutMode == 2) ? 4 : 3; }

static RECT LayoutMenuRect() {
    double dpi = GetSystemDpiScale();
    HeaderMetrics hm = GetHeaderMetrics();
    int n = LayoutMenuItemCount();
    int itemH = (int)(44 * dpi);
    int pad = (int)(6 * dpi);
    int w = (int)(168 * dpi);
    if (w < hm.wNum) w = hm.wNum;
    int hh = n * itemH + pad * 2;
    int right = hm.xNum + hm.wNum;
    int top = hm.btnY + hm.btnH + (int)(8 * dpi);
    if (top + hh > g_wh) top = hm.btnY - (int)(8 * dpi) - hh;   // 超出底边就上翻
    if (top < 0) top = 0;
    RECT r = { right - w, top, right, top + hh };
    return r;
}

static int LayoutMenuHitIndex(int x, int y) {
    RECT p = LayoutMenuRect();
    if (x < p.left || x >= p.right || y < p.top || y >= p.bottom) return -1;
    double dpi = GetSystemDpiScale();
    int pad = (int)(6 * dpi), itemH = (int)(44 * dpi);
    int idx = (y - (p.top + pad)) / itemH;
    if (idx < 0 || idx >= LayoutMenuItemCount()) return -1;
    return idx;
}

static void LayoutMenuApply(HWND hWnd, int idx) {
    g_lmOpen = FALSE;
    g_lmHover = -1;
    if (idx == 0)      SetLayoutMode(0);
    else if (idx == 1) SetLayoutMode(1);
    else if (idx == 2) SetLayoutMode(2);
    else if (idx == 3 && g_layoutMode == 2) SetFullNumpadHidden(hWnd, !NumpadHidden());
    if (hWnd && IsWindow(hWnd)) InvalidateRect(hWnd, NULL, TRUE);
}

// 勾用两条线画，不依赖字体里有没有 ✓ 这个字形
static void DrawMenuCheck(HDC dc, int cx, int cy, int size, DWORD color) {
    HPEN pen = CreatePen(PS_SOLID, (size >= 20 ? 2 : 1), color);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, cx - size / 4, cy, NULL);
    LineTo(dc, cx - size / 16, cy + size / 4);
    LineTo(dc, cx + size / 3, cy - size / 4);
    SelectObject(dc, old);
    DeleteObject(pen);
}

static void DrawLayoutMenu(HDC dc) {
    if (!g_lmOpen) return;
    double dpi = GetSystemDpiScale();
    RECT p = LayoutMenuRect();
    int pad = (int)(6 * dpi), itemH = (int)(44 * dpi);
    DrawRoundRect(dc, p.left, p.top, p.right - p.left, p.bottom - p.top,
                  C_KEY, C_KEY, (int)(12 * dpi));
    const wchar_t* labels[4] = { T(L"默认布局", L"Default"), T(L"小键盘", L"Numpad"),
                                 T(L"全尺寸", L"Full size"), T(L"数字区", L"Number pad") };
    int n = LayoutMenuItemCount();
    for (int i = 0; i < n; i++) {
        int top = p.top + pad + i * itemH;
        RECT ir = { p.left + pad, top, p.right - pad, top + itemH };
        BOOL cur = (i == 3) ? !NumpadHidden() : (i == g_layoutMode);
        if (i == g_lmHover)
            DrawRoundRect(dc, ir.left, ir.top, ir.right - ir.left, ir.bottom - ir.top,
                          C_REGULAR_HOV, C_REGULAR_HOV, (int)(8 * dpi));
        else if (cur)
            DrawRoundRect(dc, ir.left, ir.top, ir.right - ir.left, ir.bottom - ir.top,
                          BlendColor(C_KEY, C_HOT, 0.08), BlendColor(C_KEY, C_HOT, 0.08), (int)(8 * dpi));
        if (cur) DrawMenuCheck(dc, ir.left + (int)(16 * dpi), ir.top + itemH / 2, (int)(16 * dpi), C_HOT);
        DrawTextL(dc, ir.left + (int)(30 * dpi), ir.top, (ir.right - ir.left) - (int)(36 * dpi),
                  itemH, labels[i], g_sfCtrl, C_WHITE);
    }
}

"""
rep1("menu_code", "static void DrawHeader(HDC dc) {", MENU + "static void DrawHeader(HDC dc) {")

# 绘制顺序：按键之后（否则被键面盖住）
rep1("draw_call", "    DrawKeys(mem);\n", "    DrawKeys(mem);\n    DrawLayoutMenu(mem);\n")

# 帧签名：结构体 + 填充
rep1("sig_field", "    BOOL physLShift, physRShift;",
     "    BOOL physLShift, physRShift;\n"
     "    // 布局菜单：打开/悬停变化必须触发重绘（整帧缓存，漏了就「点了没反应」）\n"
     "    BOOL lmOpen;\n    int  lmHover;")
rep1("sig_fill", "    sig.keyIconStyle = g_keyIconStyle;",
     "    sig.lmOpen = g_lmOpen; sig.lmHover = g_lmHover;\n    sig.keyIconStyle = g_keyIconStyle;")

# 命中优先级：菜单 > 一切；点菜单外只收起、不传给底下的键
rep1("hit", "static void OnLDown(HWND hWnd, int x, int y) {\n    int hh = HitHeader(x, y);",
     "static void OnLDown(HWND hWnd, int x, int y) {\n"
     "    // 布局菜单优先级最高：点项=应用；点外面=只收起，这一下不传给底下的键\n"
     "    if (g_lmOpen) {\n"
     "        int idx = LayoutMenuHitIndex(x, y);\n"
     "        if (idx >= 0) { LayoutMenuApply(hWnd, idx); return; }\n"
     "        g_lmOpen = FALSE; g_lmHover = -1;\n"
     "        InvalidateRect(hWnd, NULL, TRUE);\n"
     "        return;\n"
     "    }\n"
     "    int hh = HitHeader(x, y);")

# 自动显隐：菜单开着时挂起
rep1("autovis", '        g_dbgWhy = "settings-open";\n        return;\n    }',
     '        g_dbgWhy = "settings-open";\n        return;\n    }\n'
     '    // 布局菜单开着时同样挂起：用户正在挑布局，不能让键盘自己滑走\n'
     '    if (g_lmOpen) { g_dbgWhy = "layout-menu"; return; }')

# 标题栏胶囊：返回新的 HDR_LAYOUT（旧 HDR_NUM 分支保留但不达）
rep1("hitcode", "if (hm.numBtnVisible && x >= hm.xNum && x < hm.xNum + hm.wNum) return HDR_NUM;",
     "if (hm.numBtnVisible && x >= hm.xNum && x < hm.xNum + hm.wNum) return HDR_LAYOUT;")
rep1("defcode", "#define HDR_NUM   1005   // 123 按钮：切换小键盘",
     "#define HDR_NUM   1005   // 旧语义（三态切换），已被 HDR_LAYOUT 接管，保留备查\n"
     "#define HDR_LAYOUT 1007  // 标题栏布局胶囊：打开/收起布局菜单（A1）")

# 点击：新 case 放在旧 case 之前
rep1("click", "        case HDR_NUM:   // 「小键盘」按钮：语义统一为「小键盘开着吗」",
     "        case HDR_LAYOUT:\n"
     "            // 打开/收起布局菜单（原来的三态语义已收进菜单，见 LayoutMenuApply）\n"
     "            g_lmOpen = !g_lmOpen;\n"
     "            g_lmHover = -1;\n"
     "            g_lastOwnKeyTick = GetTickCount();   // 算作「用户正在用键盘」\n"
     "            InvalidateRect(hWnd, NULL, TRUE);\n"
     "            break;\n"
     "        case HDR_NUM:   // 「小键盘」按钮：语义统一为「小键盘开着吗」")

# 按钮文案：小键盘 -> 布局
rep1("label", 'T(L"\\x5C0F\\x952E\\x76D8", L"Numpad")', 'T(L"\\x5E03\\x5C40", L"Layout")')

io.open(p, "w", encoding="utf-8", newline="").write(s)
for n, c in rep:
    print("  %-12s %s" % (n, "OK" if c == 1 else "MISS"))
print("changed" if s != orig else "NOCHANGE")
