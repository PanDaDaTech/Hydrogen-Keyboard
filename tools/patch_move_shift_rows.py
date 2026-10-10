# -*- coding: utf-8 -*-
"""把「Shift 符号」+「Shift 双击锁定」两行从常规 Tab 搬到布局 Tab，并让它们
在小键盘布局下隐藏（2026-10-11 用户要求）。

行号是索引驱动的，所以 8 处映射必须一次改完，否则必然「画一行、点一行」。
按技能里的纪律：这类大批量改动写成脚本执行（并行发多个 Edit 会互相覆盖），
脚本自己打印每处替换数，改完再扫重复与残留。

用法：python tools/patch_move_shift_rows.py [hkeyboard.cpp]
"""
import re, io, sys

p = sys.argv[1] if len(sys.argv) > 1 else "hkeyboard.cpp"
s = io.open(p, encoding="utf-8").read()
orig = s
report = []


def sub1(name, pat, repl, flags=0):
    global s
    new, n = re.subn(pat, repl, s, count=1, flags=flags)
    report.append((name, n))
    s = new


# ---- 1) SettingsRowHidden ----
sub1("hidden", r"static BOOL SettingsRowHidden\(int tab, int index\) \{.*?\n\}\n",
     """static BOOL SettingsRowHidden(int tab, int index) {
    BOOL onlyDefault = (g_layoutMode != 0);
    if (tab == 0) {
        int closeRow = g_af ? 2 : 1;
        // 功能键行：全尺寸也藏（全尺寸常驻 F 行，开关没有意义）
        if (index == closeRow + 2) return onlyDefault;
        return FALSE;
    }
    // 布局 Tab：0=键盘布局 1=Fn 网页布局 2=小键盘按钮 3=Shift 符号 4=Shift 双击锁定
    if (tab == 3) {
        // Fn 网页布局：非默认布局没有 Fn 层；开了功能键行时 Fn 键被移除
        if (index == 1) return (onlyDefault || g_showFKeys);
        // Shift 两行（2026-10-11 从常规 Tab 搬来）：小键盘布局没有 Shift 键与双符号键 ⇒ 藏
        if (g_layoutMode == 1) return (index == 3 || index == 4);
        return FALSE;
    }
    return FALSE;
}
""", flags=re.S)

# ---- 2) 行数 ----
sub1("count0", r"if \(tab == 0\) return g_af \? 8 : 7;", "if (tab == 0) return g_af ? 6 : 5;")
sub1("count3", r"if \(tab == 3\) return 3;", "if (tab == 3) return 5;")

# ---- 3) 控件高（常规「界面语言」行号前移） ----
sub1("ctrl0", r"if \(index == closeRow \+ 5\) return 40;(\s*// 界面语言下拉)",
     r"if (index == closeRow + 3) return 40;\1")

# ---- 4) 绘制：剪出两块，插到布局 Tab 的「小键盘按钮」之后 ----
m = re.search(
    r"        r = SettingsRowRect\(m, closeRow \+ 3\);\n"
    r"        DrawSettingRowContent\(dc, m, r, HKICON_SHIFT, NULL,.*?"
    r"DrawSettingSwitch\(dc, m, r, g_shiftDoubleTap, S_HIT_SHDLOCK\);\n", s, re.S)
if not m:
    report.append(("draw_cut", 0))
else:
    block = m.group(0)
    s = s[:m.start()] + s[m.end():]
    report.append(("draw_cut", 1))
    sub1("draw_lang", r"r = SettingsRowRect\(m, closeRow \+ 5\);\n(\s*)const wchar_t\* gseg\[2\];",
         r"r = SettingsRowRect(m, closeRow + 3);\n\1const wchar_t* gseg[2];")
    ins = "        DrawSettingSwitch(dc, m, r, g_showNumBtn, S_HIT_NPBTN);\n"
    if ins not in s:
        report.append(("draw_ins_anchor", 0))
    else:
        moved = block.replace("closeRow + 3", "3").replace("closeRow + 4", "4")
        s = s.replace(ins, ins + "\n" + moved, 1)
        report.append(("draw_ins", 1))

# ---- 5) 命中：剪掉常规里那两条判断 ----
m = re.search(
    r"        if \(!SettingsRowHidden\(0, closeRow \+ 2\)\) \{.*?S_HIT_SHIFTSYM;\n        \}\n", s, re.S)
if m:
    keep = m.group(0)
    only_fkeys = re.search(
        r"        if \(!SettingsRowHidden\(0, closeRow \+ 2\)\) \{.*?S_HIT_FKEYS;\n        \}\n", keep, re.S)
    if only_fkeys:
        s = s[:m.start()] + only_fkeys.group(0) + s[m.end():]
        report.append(("hit_cut", 1))
    else:
        report.append(("hit_cut", 0))
else:
    report.append(("hit_cut", 0))

# ---- 6) 命中：布局 Tab 里加上（紧跟小键盘按钮） ----
ins = ("        r = SettingsRowRect(m, 2);\n"
       "        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_NPBTN;\n")
if ins in s:
    add = ("\n        if (!SettingsRowHidden(3, 3)) {\n"
           "            r = SettingsRowRect(m, 3);\n"
           "            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_SHIFTSYM;\n"
           "            r = SettingsRowRect(m, 4);\n"
           "            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_SHDLOCK;\n"
           "        }\n")
    s = s.replace(ins, ins + add, 1)
    report.append(("hit_ins", 1))
else:
    report.append(("hit_ins", 0))

# ---- 7) SettingsSwitchRect：两个命中码改指布局 Tab 的行号 ----
sub1("switchrect_fk", r"else if \(hit == S_HIT_FKEYS\) rowIndex = g_af \? 4 : 3;", "")
sub1("switchrect_fk2", r"else if \(hit == S_HIT_FKEYS\) rowIndex = g_af \? 4 : 3;\n", "")
sub1("switchrect", r"else if \(hit == S_HIT_SHIFTSYM\) rowIndex = g_af \? 5 : 4;\n(\s*)else if \(hit == S_HIT_SHDLOCK\)  rowIndex = g_af \? 6 : 5;",
     r"else if (hit == S_HIT_SHIFTSYM) rowIndex = 3;\n\1else if (hit == S_HIT_SHDLOCK)  rowIndex = 4;")

io.open(p, "w", encoding="utf-8", newline="").write(s)
for n, c in report:
    print("  %-18s %s" % (n, "OK" if c == 1 else ("MISS" if c == 0 else c)))
print("changed" if s != orig else "NOCHANGE")
