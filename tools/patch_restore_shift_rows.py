# -*- coding: utf-8 -*-
"""回退 2026-10-11 那次「Shift 两行搬到布局 Tab」，并新增「双符号」行到布局 Tab。

用户最终要求（附截图）：
  · 「Shift 符号」+「Shift 双击锁定」**留在常规 Tab**，描述回原样
    （「按下 Shift 后数字键仅显示特殊符号」/「连按两下 Shift 锁定，再点解除」）
  · 「双符号」是**另一件事**，单独一行放在**布局 Tab**
  · 语义分工：g_shiftSymbols = 按 Shift 只显示特殊符号（原义）；
              g_dualSymbols  = 键面是否上下并列显示两个符号（新，默认开）

行号是索引驱动的，8 处映射一次改完，脚本打印每处替换数。
用法：python tools/patch_restore_shift_rows.py [hkeyboard.cpp]
"""
import re, io, sys

p = sys.argv[1] if len(sys.argv) > 1 else "hkeyboard.cpp"
s = io.open(p, encoding="utf-8").read()
orig = s
rep = []


def sub1(name, pat, repl, flags=0):
    global s
    new, n = re.subn(pat, repl, s, count=1, flags=flags)
    rep.append((name, n))
    s = new


# ---- 1) 新增全局 g_dualSymbols ----
sub1("global", r"BOOL        g_shiftSymbols = TRUE;[^\n]*\n",
     "BOOL        g_shiftSymbols = TRUE;     // 按 Shift 时数字键只显示特殊符号（原义，常规 Tab）\n"
     "BOOL        g_dualSymbols = TRUE;      // 键面是否**上下并列**显示主/副两个符号（布局 Tab，默认开）\n")

# ---- 2) 渲染：Shift 优先，其次看双符号开关 ----
sub1("render", r"        if \(dual\) \{.*?\n        \} else if \(!DrawKeyLabel",
     """        if (dual) {
            // 三条路（用户 2026-10-11 定稿）：
            //   ① 按住 Shift 且「Shift 符号」开 → 只显示副符号（灰）—— 这是原来那条语义，保留；
            //   ② 否则「双符号」开 → 上下并列显示主字符 + 灰色副符号；
            //   ③ 都关 → 只显示主字符（黑）。
            // ⚠ 符号永远是灰色，别再改回「按 Shift 把符号提成大黑字」。
            if (shiftOn && g_shiftSymbols) {
                wchar_t single[2] = { shiftCh, 0 };
                DrawTextC(dc, k->x, k->y, k->w, k->h, single, f, C_DIM);
            } else if (g_dualSymbols) {
                DrawKeyDual(dc, k->x, k->y, k->w, k->h, baseCh, shiftCh, f, g_f12, textC, C_DIM);
            } else {
                wchar_t single[2] = { baseCh, 0 };
                DrawTextC(dc, k->x, k->y, k->w, k->h, single, f, textC);
            }
        } else if (!DrawKeyLabel""")

# ---- 3) SettingsRowHidden：常规恢复成只有功能键行；布局只藏「双符号」于小键盘 ----
sub1("hidden", r"static BOOL SettingsRowHidden\(int tab, int index\) \{.*?\n\}\n",
     """static BOOL SettingsRowHidden(int tab, int index) {
    BOOL onlyDefault = (g_layoutMode != 0);
    if (tab == 0) {
        int closeRow = g_af ? 2 : 1;
        // 功能键行：全尺寸也藏（全尺寸常驻 F 行，开关没有意义）
        if (index == closeRow + 2) return onlyDefault;
        // Shift 符号 / Shift 双击锁定：留在常规（2026-10-11 回退搬迁），全尺寸也显示
        return FALSE;
    }
    // 布局 Tab：0=键盘布局 1=Fn 网页布局 2=小键盘按钮 3=双符号
    if (tab == 3) {
        if (index == 1) return (onlyDefault || g_showFKeys);
        // 双符号：小键盘布局没有双符号键 ⇒ 藏
        if (index == 3) return (g_layoutMode == 1);
        return FALSE;
    }
    return FALSE;
}
""", flags=re.S)

# ---- 4) 行数：常规回 8/7；布局 5 -> 4 ----
sub1("count0", r"if \(tab == 0\) return g_af \? 6 : 5;", "if (tab == 0) return g_af ? 8 : 7;")
sub1("count3", r"if \(tab == 3\) return 5;", "if (tab == 3) return 4;")

# ---- 5) 控件高：语言行回 closeRow+5 ----
sub1("ctrl0", r"if \(index == closeRow \+ 3\) return 40;(\s*// 界面语言下拉)",
     r"if (index == closeRow + 5) return 40;\1")

# ---- 6) 绘制：从布局剪出 Shift 两块，插回常规（功能键行之后）；布局只留双符号 ----
m = re.search(r"        r = SettingsRowRect\(m, 3\);\n"
              r"        DrawSettingRowContent\(dc, m, r, HKICON_SHIFT, NULL,.*?"
              r"DrawSettingSwitch\(dc, m, r, g_shiftDoubleTap, S_HIT_SHDLOCK\);\n", s, re.S)
if m:
    block = m.group(0)
    s = s[:m.start()] + s[m.end():]
    rep.append(("draw_cut", 1))
    anchor = "        DrawSettingSwitch(dc, m, r, g_showFKeys, S_HIT_FKEYS);\n"
    if anchor in s:
        s = s.replace(anchor, anchor + "\n" + block.replace("m, 3", "m, closeRow + 3").replace("m, 4", "m, closeRow + 4"), 1)
        rep.append(("draw_back", 1))
    else:
        rep.append(("draw_back", 0))
    # 布局里新增「双符号」行
    ins = "        DrawSettingSwitch(dc, m, r, g_showNumBtn, S_HIT_NPBTN);\n"
    if ins in s:
        s = s.replace(ins, ins + """
        if (!SettingsRowHidden(3, 3)) {                  // 小键盘下不存在
        r = SettingsRowRect(m, 3);
        DrawSettingRowContent(dc, m, r, HKICON_SHIFT, NULL,
                              T(L"双符号", L"Both Symbols"),
                              T(L"键面上下并列显示主符号与副符号", L"Stack both symbols on the key face"),
                              g_sHov == S_HIT_DUAL, SettingsSwitchTextRight(m, r));
        DrawSettingSwitch(dc, m, r, g_dualSymbols, S_HIT_DUAL);
        }
""", 1)
        rep.append(("draw_dual", 1))
    else:
        rep.append(("draw_dual", 0))
else:
    rep.append(("draw_cut", 0))

# ---- 7) 命中：布局删 Shift 两条，常规加回；布局加双符号 ----
old = ("        if (!SettingsRowHidden(3, 3)) {\n"
       "            r = SettingsRowRect(m, 3);\n"
       "            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_SHIFTSYM;\n"
       "            r = SettingsRowRect(m, 4);\n"
       "            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_SHDLOCK;\n"
       "        }\n")
new = ("        if (!SettingsRowHidden(3, 3)) {\n"
       "            r = SettingsRowRect(m, 3);\n"
       "            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_DUAL;\n"
       "        }\n")
if old in s:
    s = s.replace(old, new, 1)
    rep.append(("hit_dual", 1))
else:
    rep.append(("hit_dual", 0))

anchor = ("        if (!SettingsRowHidden(0, closeRow + 2)) {       // 全尺寸 / 小键盘下这行不存在\n"
          "            r = SettingsRowRect(m, closeRow + 2);\n"
          "            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_FKEYS;\n"
          "        }\n")
add = anchor + ("""
        r = SettingsRowRect(m, closeRow + 3);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_SHIFTSYM;
        r = SettingsRowRect(m, closeRow + 4);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_SHDLOCK;
""")
if anchor in s:
    s = s.replace(anchor, add, 1)
    rep.append(("hit_back", 1))
else:
    rep.append(("hit_back", 0))

# ---- 8) SettingsSwitchRect：常规三条回原行号；新增双符号 -> 3 ----
sub1("switchrect", r"else if \(hit == S_HIT_SHIFTSYM\) rowIndex = 3;\n(\s*)else if \(hit == S_HIT_SHDLOCK\)  rowIndex = 4;",
     r"else if (hit == S_HIT_SHIFTSYM) rowIndex = g_af ? 5 : 4;\n\1else if (hit == S_HIT_SHDLOCK)  rowIndex = g_af ? 6 : 5;\n\1else if (hit == S_HIT_DUAL)     rowIndex = 3;")
sub1("switchrect_lang", r"else if \(hit == S_HIT_LANG_DROP\) rowIndex = g_af \? 5 : 4;",
     "else if (hit == S_HIT_LANG_DROP) rowIndex = g_af ? 7 : 6;")

# ---- 9) 命中码 + 点击处理 + INI ----
sub1("define", r"#define S_HIT_SHIFTSYM       19",
     "#define S_HIT_SHIFTSYM       19   // 常规 Tab：Shift 符号\n#define S_HIT_DUAL           27   // 布局 Tab：双符号（键面是否并列显示两个）")
sub1("ini_set", r"IniSetInt\(L\"General\", L\"ShiftSymbols\", 1\);",
     "IniSetInt(L\"General\", L\"ShiftSymbols\", 1);\n    IniSetInt(L\"General\", L\"DualSymbols\", 1);")
sub1("ini_get", r"g_shiftSymbols = \(IniGetInt\(L\"General\", L\"ShiftSymbols\", 1\) != 0\);",
     "g_shiftSymbols = (IniGetInt(L\"General\", L\"ShiftSymbols\", 1) != 0);\n"
     "    g_dualSymbols = (IniGetInt(L\"General\", L\"DualSymbols\", 1) != 0);")

io.open(p, "w", encoding="utf-8", newline="").write(s)
for n, c in rep:
    print("  %-16s %s" % (n, "OK" if c == 1 else "MISS"))
print("changed" if s != orig else "NOCHANGE")
