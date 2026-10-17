# -*- coding: utf-8 -*-
"""B1：经典主题作为与「明暗 / 色相」正交的第二维「界面风格」（HKeyboard-UI方案评估 §四）。

锚点全部先核实再写（上一版 4 处 MISS 的原因已记在提交里）：
  · HkbTokensBuild 结束于 primaryFg 之后
  · ThemeSegItems 是 static int（返回段数）
  · 主题 Tab 的绘制/命中分支以 `} else {`（关于页）结束
  · 落盘那行有完整原文，整行做锚点
用法：python tools/patch_theme_style.py [hkeyboard.cpp]
"""
import io, sys

p = sys.argv[1] if len(sys.argv) > 1 else "hkeyboard.cpp"
s = io.open(p, encoding="utf-8").read()
orig = s
rep = []


def rep1(name, old, new):
    global s
    if old in s:
        s = s.replace(old, new, 1); rep.append((name, 1))
    else:
        rep.append((name, 0))


# 1) 全局
rep1("global", "// Theme mode: 0 = follow system, 1 = force dark, 2 = force light",
     "// 界面风格（方案 §4.1 B1）：与明暗 / 色相正交的第二维。0=现代（默认）1=经典（中性灰，忽略色相，只做浅色）\n"
     "static int  g_themeStyle = 0;\n\n"
     "// Theme mode: 0 = follow system, 1 = force dark, 2 = force light")

# 2) 令牌覆盖（函数末尾、primaryFg 之后）
rep1("overlay",
     "    out->primaryFg       = dark ? BlendColor(out->primary, RGB(0, 0, 0), 0.70) : out->text;\n}",
     """    out->primaryFg       = dark ? BlendColor(out->primary, RGB(0, 0, 0), 0.70) : out->text;

    // 经典风格（方案 §4.2 的实机取色）：中性灰、忽略色相。深色下不套用 ——
    // #D6D6D6 描边在深色上不可用（§4.4.3），经典只做浅色。
    if (g_themeStyle == 1 && !dark) {
        out->pageBg             = RGB(243, 243, 243);
        out->cardBg             = RGB(255, 255, 255);
        out->btnRegularBg       = RGB(232, 232, 232);
        out->btnRegularBgHover  = RGB(224, 224, 224);
        out->btnRegularBgActive = RGB(216, 216, 216);
        out->btnCardBgHover     = RGB(247, 247, 247);
        out->btnPlainBgHover    = RGB(240, 240, 240);
        out->keyOutline         = RGB(214, 214, 214);
        out->keyOutlineHover    = RGB(190, 190, 190);
        out->text               = RGB(26, 26, 26);
        out->textMuted          = RGB(102, 102, 102);
        out->btnContent         = RGB(26, 26, 26);
        out->lineDivider        = RGB(230, 230, 230);
        out->metaDivider        = RGB(214, 214, 214);
    }
}""")

# 3) 键帽圆角 8 -> 4（经典）
rep1("radius", "DrawRoundRect(dc, k->x, k->y, k->w, k->h, bg, outline, 8);",
     "DrawRoundRect(dc, k->x, k->y, k->w, k->h, bg, outline, g_themeStyle == 1 ? 4 : 8);")

# 4) 命中码
rep1("hitcode", "#define S_HIT_OPACITY_DROP   96",
     "#define S_HIT_OPACITY_DROP   96\n#define S_HIT_STYLE          97   // 主题 Tab：界面风格（现代 / 经典）")

# 5) 分段项（按 ThemeSegItems 的真实签名 static int）
rep1("segitems", "static int ThemeSegItems(const wchar_t** out) {",
     """static int StyleSegItems(const wchar_t** out) {
    static const wchar_t* zh[2] = { L"\\u73B0\\u4EE3", L"\\u7ECF\\u5178" };   // 现代 / 经典
    static const wchar_t* en[2] = { L"Modern", L"Classic" };
    for (int i = 0; i < 2; i++) out[i] = g_lang ? en[i] : zh[i];
    return 2;
}
static int ThemeSegItems(const wchar_t** out) {""")

# 6) 行数 3 -> 4
rep1("count", "if (tab == 1) return 3;", "if (tab == 1) return 4;")

# 7) 绘制：主题分支末尾（`} else {` 之前）
rep1("draw", "    } else if (g_sTab == 1) {",
     "    } else if (g_sTab == 1) {")
m_old = s
i = s.find("    } else if (g_sTab == 1) {")
if i >= 0:
    j = s.find("\n    } else {", i)
    if j >= 0:
        block = ("""
        // 界面风格（B1）：现代 / 经典
        r = SettingsRowRect(m, 3);
        { const wchar_t* st[2]; int stn = StyleSegItems(st);
          RECT stR = RowSegRect(m, r, st, stn);
          DrawSettingRowContent(dc, m, r, HKICON_CONTRAST, NULL,
                                T(L"界面风格", L"Interface Style"),
                                T(L"经典：中性灰 + 4px 圆角（忽略色相）", L"Classic: neutral grey, 4px corners (hue ignored)"),
                                g_sHov == S_HIT_STYLE, stR.left);
          DrawSegmented(dc, m, stR, st, stn, g_themeStyle); }
""")
        s = s[:j] + block + s[j:]
        rep.append(("draw_insert", 1))
    else:
        rep.append(("draw_insert", 0))
else:
    rep.append(("draw_insert", 0))

# 8) 命中：命中分支末尾（第二处 `} else {`）
i = s.find("    } else if (g_sTab == 1) {", i + 1 if i >= 0 else 0)
if i >= 0:
    j = s.find("\n    } else {", i)
    if j >= 0:
        block = ("""
        r = SettingsRowRect(m, 3);
        { const wchar_t* st[2]; int stn = StyleSegItems(st);
          RECT sr = RowSegRect(m, r, st, stn);
          if (x >= sr.left && x < sr.right && y >= sr.top && y < sr.bottom) return S_HIT_STYLE; }
""")
        s = s[:j] + block + s[j:]
        rep.append(("hit_insert", 1))
    else:
        rep.append(("hit_insert", 0))
else:
    rep.append(("hit_insert", 0))

# 9) 点击
rep1("click", "    if (hit == S_HIT_CLOSE) {",
     """    if (hit == S_HIT_STYLE) {
        SettingsMetrics sm = GetSettingsMetrics(hWnd);
        RECT r3 = SettingsRowRect(sm, 3);
        const wchar_t* st[2]; int stn = StyleSegItems(st);
        RECT sr = RowSegRect(sm, r3, st, stn);
        int idx = SegmentedHitIndex(sm, sr, st, stn, x);
        if (idx >= 0 && idx != g_themeStyle) {
            g_themeStyle = idx;
            IniSetInt(L"Theme", L"Style", g_themeStyle);
            RefreshThemeAndRepaint(hWnd);   // 主键盘 / 设置页 / 关闭提示窗一起刷
        }
        return;
    }
    if (hit == S_HIT_CLOSE) {""")

# 10) 装载（整行做锚点，避免上次那种注释叠在一起）
rep1("load", '    g_dualSymbols = (IniGetInt(L"General", L"DualSymbols", 1) != 0);',
     '    g_dualSymbols = (IniGetInt(L"General", L"DualSymbols", 1) != 0);\n'
     '    g_themeStyle = IniGetInt(L"Theme", L"Style", 0);   // 0=现代 1=经典（B1）')

io.open(p, "w", encoding="utf-8", newline="").write(s)
for n, c in rep:
    print("  %-14s %s" % (n, "OK" if c == 1 else "MISS"))
print("changed" if s != orig else "NOCHANGE")
