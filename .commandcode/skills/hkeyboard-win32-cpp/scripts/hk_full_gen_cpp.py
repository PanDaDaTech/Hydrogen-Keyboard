# -*- coding: utf-8 -*-
"""把已验证的 1u 几何直接生成 C++ 表驱动代码（与对照页同源，保证一致）。"""
import io, sys
sys.path.insert(0, ".")
import hk_full_eval as M

# 每行的 vk（与 hkeyboard.cpp 现有 BuildComplete/BuildNumpad 完全一致）
VK_MAIN = [
    [0x1B] + [0x70 + i for i in range(12)] + [0x2E],
    [0xC0, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x30, 0xBD, 0xBB, 0x08],
    [0x09, 0x51, 0x57, 0x45, 0x52, 0x54, 0x59, 0x55, 0x49, 0x4F, 0x50, 0xDB, 0xDD, 0xDC],
    [0x14, 0x41, 0x53, 0x44, 0x46, 0x47, 0x48, 0x4A, 0x4B, 0x4C, 0xBA, 0xDE, 0x0D],
    [0xA0, 0x5A, 0x58, 0x43, 0x56, 0x42, 0x4E, 0x4D, 0xBC, 0xBE, 0xBF, 0xA1],
    [0x00, 0x11, 0x5B, 0x12, 0x20, 0x12, 0x5D, 0x11],
]
VK_NAV = [[0x2C, 0x46, 0x13], [0x2D, 0x24, 0x21], [0x2E, 0x23, 0x22], [], [0x26], [0x25, 0x28, 0x27]]
VK_NUM = [[0x90, 0x6F, 0x6A, 0x6D], [0x67, 0x68, 0x69, 0x6B],
          [0x64, 0x65, 0x66], [0x61, 0x62, 0x63, 0x0D], [0x60, 0x6E]]
# Fn 网页层：Shift 行字母键换成网址后缀键（照抄现状的 vW/tW）
VK_WEBROW = [0x200, 0x201, 0x202, 0x203, 0x204, 0x205, 0xBF, 0xBC, 0xBE, 0x2F]
TYPE_WEBROW = ["K_SPECIAL"] * 6 + ["K_NORMAL"] * 4

KIND2TYPE = {"n": "K_NORMAL", "l": "K_LETTER", "mod": "K_SPECIAL",
             "arr": "K_ARROW", "sp": "K_SPACE"}

def cfile(name):
    kn = KIND2TYPE[name]
    if name == "mod" and kn == "K_SPECIAL":
        return "K_SPECIAL"
    return kn

def emit(name, rows, vks, kindfix=None):
    # 逐行长度对齐检查
    out = []
    for r, (row, vk) in enumerate(zip(rows, vks)):
        assert len(row) == len(vk), (name, r, len(row), len(vk))
        items = []
        for d, v in zip(row, vk):
            lab, col, span, kind = d[0], d[1], d[2], d[3]
            vr = d[4] if len(d) > 4 else 1
            ty = cfile(kind)
            if kindfix: ty = kindfix(lab, kind, ty)
            items.append("{ %-6s, %-5s, %-6s, %-10s, %d }" %
                         ("%.2ff" % col, "%.2ff" % span, "0x%02X" % v, ty, vr))
        out.append("    { %s }," % (",\n      ".join(items)))
    return out

COLS = lambda row: ", ".join("%.2f" % d[1] for d in row)

def main(out_path):
    L = []
    A = L.append
    A("// ============================================================================")
    A("// HKeyboard · 全尺寸（ANSI 104）布局 —— 1u 单位网格版")
    A("// 由「hk_全尺寸布局-单位网格对照.html」同源的几何模型直接生成。")
    A("// 替换 hkeyboard.cpp 里的 BuildComplete()（约 130 行逐行除法 → 表驱动）。")
    A("//")
    A("// 列位是单一真源：主区 15u / 导航区 3u / 数字区 4u，块间 1 个 gap。")
    A("// 1u 由窗口宽度反解；所有 x 一律 round(全局列位 * u)，残差被四舍五入摊掉，")
    A("// 所以「列永远对齐、行尾永远齐平」，也不再需要逐行的 rem。")
    A("// ============================================================================")
    A("")
    A("// 竖向跨行数：1 = 单行；2 = 跨两行（数字区 + 与回车）")
    A("struct KbKeySpec { float col; float w; short vk; KeyType type; char vrow; };")
    A("struct KbRowSpec { const KbKeySpec* keys; int n; };")
    A("")
    A("#define FULL_U_MAIN  15.0f")
    A("#define FULL_U_NAV    3.0f")
    A("#define FULL_U_NUM    4.0f")
    A("")
    A("// ---- 全局列位（单位 u）→ 像素 x ----------------------------------------")
    A("// 跨块时补一个 gap；bc/nb 是「导航区 / 数字区」的起始列。")
    A("static int FullColX(float col, double u, int gap, const float* bc, int nb) {")
    A("    int b = 0;")
    A("    for (int i = 0; i < nb; i++) if (bc[i] <= col + 0.001f) b++;")
    A("    return KEY_AREA_X + (int)lround(col * u) + b * gap;")
    A("}")
    A("")
    A("static void FullPut(const KbKeySpec& s, int yy, int kh, double u, int gap,")
    A("                    const float* bc, int nb) {")
    A("    int xa = FullColX(s.col, u, gap, bc, nb);")
    A("    int xb = FullColX(s.col + s.w, u, gap, bc, nb);")
    A("    AddKey(xa, yy, xb - xa - gap, kh, s.vk, s.type);")
    A("}")
    A("")

    # ---- 主区 6 行 ----
    A("// ---- 主区 6 行（列位严格按 ANSI 104）---------------------------------")
    names = ["F 行（Esc + F1~F12 + Del）", "数字行", "Tab 行（qwerty）",
             "Caps 行（asdf）", "Shift 行（zxcv）", "Ctrl 行（含 6.25u 空格）"]
    for r in range(6):
        A("// %s" % names[r])
        A("static const KbKeySpec kMain%d[] = {" % r)
        for line in emit("main%d" % r, [M.ROW_MAIN[r]], [VK_MAIN[r]]):
            A(line)
        A("};")
    A("")
    A("// Fn 网页层：Shift 行字母键换网址后缀键（与现状一致）")
    A("static const KbKeySpec kMainZWeb[] = {")
    row = M.ROW_MAIN[4]
    items = []
    for i, d in enumerate(row):
        lab, col, span, kind = d[0], d[1], d[2], d[3]
        if i == 0:
            items.append("{ 0.00f, 2.25f, 0xA0, K_SPECIAL, 1 }")     # 左 Shift
        elif i == len(row) - 1:
            items.append("{ 12.25f, 2.75f, 0xA1, K_SPECIAL, 1 }")    # 右 Shift
        else:
            j = i - 1
            items.append("{ %.2ff, 1.00f, 0x%03X, %s, 1 }" % (col, VK_WEBROW[j], TYPE_WEBROW[j]))
    A("    " + ",\n    ".join(items) + ",")
    A("};")
    A("")
    A("static const KbRowSpec kFullRows[6] = {")
    A("    { kMain0, (int)(sizeof(kMain0)/sizeof(KbKeySpec)) },")
    A("    { kMain1, (int)(sizeof(kMain1)/sizeof(KbKeySpec)) },")
    A("    { kMain2, (int)(sizeof(kMain2)/sizeof(KbKeySpec)) },")
    A("    { kMain3, (int)(sizeof(kMain3)/sizeof(KbKeySpec)) },")
    A("    { kMain4, (int)(sizeof(kMain4)/sizeof(KbKeySpec)) },   // webFn 时换 kMainZWeb")
    A("    { kMain5, (int)(sizeof(kMain5)/sizeof(KbKeySpec)) },")
    A("};")
    A("")
    def sp(one):
        return "{ %-6s, %-5s, %-6s, %-10s, %d }" % (
            "%.2ff" % one[0], "%.2ff" % one[1], "0x%02X" % one[2], one[3], one[4])

    def tbl(name, items, per=3, comment=None):
        A("static const KbKeySpec %s[] = {" % name)
        sss = [sp(i) for i in items]
        for i in range(0, len(sss), per):
            tail = "," if i + per < len(sss) or True else ""
            A("    " + ", ".join(sss[i:i + per]) + "," + (("   // " + comment) if comment and i == 0 else ""))
        A("};")

    A("// ---- 导航区 3u（PrtSc 行 → ←↓→ 行，↑ 独占第 5 行）-------------------")
    A("//      行位上与主区完全对齐：PrtSc 在 F 行、Ins 在数字行、Del 在 Tab 行、")
    A("//      ↑ 在 Shift 行、←↓→ 在 Ctrl 行 —— 这一点现状已经是对的，保持不动。")
    tbl("kNav0", [(0, 1, 0x2C, "K_SPECIAL", 1), (1, 1, 0x46, "K_SPECIAL", 1), (2, 1, 0x13, "K_SPECIAL", 1)])
    tbl("kNav1", [(0, 1, 0x2D, "K_SPECIAL", 1), (1, 1, 0x24, "K_SPECIAL", 1), (2, 1, 0x21, "K_SPECIAL", 1)])
    tbl("kNav2", [(0, 1, 0x2E, "K_SPECIAL", 1), (1, 1, 0x23, "K_SPECIAL", 1), (2, 1, 0x22, "K_SPECIAL", 1)])
    tbl("kNav4", [(1, 1, 0x26, "K_ARROW", 1)])
    tbl("kNav5", [(0, 1, 0x25, "K_ARROW", 1), (1, 1, 0x28, "K_ARROW", 1), (2, 1, 0x27, "K_ARROW", 1)])
    A("")
    A("// ---- 数字区 4u：5 行，对齐主区的「数字行 ↔ Ctrl 行」（= 真实 104）----")
    A("//      现状把 NumLock 行放在 F 行，整块被拉成 6 行：回车底边停在「0 行之上")
    A("//      一整行」，0 与回车底不齐。这里改为 5 行，F 行位置的 4 列留空。")
    tbl("kNum0", [(0, 1, 0x90, "K_SPECIAL", 1), (1, 1, 0x6F, "K_NORMAL", 1),
                  (2, 1, 0x6A, "K_NORMAL", 1), (3, 1, 0x6D, "K_NORMAL", 1)])
    tbl("kNum1", [(0, 1, 0x67, "K_NORMAL", 1), (1, 1, 0x68, "K_NORMAL", 1),
                  (2, 1, 0x69, "K_NORMAL", 1), (3, 1, 0x6B, "K_NORMAL", 2)], comment="+ 跨两行")
    tbl("kNum2", [(0, 1, 0x64, "K_NORMAL", 1), (1, 1, 0x65, "K_NORMAL", 1), (2, 1, 0x66, "K_NORMAL", 1)])
    tbl("kNum3", [(0, 1, 0x61, "K_NORMAL", 1), (1, 1, 0x62, "K_NORMAL", 1),
                  (2, 1, 0x63, "K_NORMAL", 1), (3, 1, 0x0D, "K_SPECIAL", 2)], comment="回车跨两行")
    tbl("kNum4", [(0, 2, 0x60, "K_NORMAL", 1), (2, 1, 0x6E, "K_NORMAL", 1)], comment="0 占两列")
    A("")
    A("// ============================================================================")
    A("static void BuildComplete(int y, double dpiScale, double scaleX, BOOL webFn) {")
    A("    (void)dpiScale; (void)scaleX;            // 网格化后不再需要这两个缩放因子")
    A("    const int gap = g_keyGap;")
    A("    const int KH  = g_keyHeight;")
    A("    const int h2  = KH * 2 + gap;            // 跨两行的键高")
    A("")
    A("    // ---- 1u 反解：宽度 = 22u + 2*gap（隐藏数字区时 18u + gap）--------")
    A("    const int   nb   = g_npHidden ? 1 : 2;")
    A("    const float units = FULL_U_MAIN + FULL_U_NAV + (g_npHidden ? 0.f : FULL_U_NUM);")
    A("    const double u   = (double)(KEY_AREA_W - nb * gap) / (double)units;")
    A("    const float bc[2] = { FULL_U_MAIN, FULL_U_MAIN + FULL_U_NAV };")
    A("")
    A("    // ---- 主区 6 行 ---------------------------------------------------")
    A("    for (int r = 0; r < 6; r++) {")
    A("        int yy = y + r * (KH + gap);")
    A("        if (r == 4 && webFn) {                // Fn 网页层换 Shift 行")
    A("            for (int i = 0; i < (int)(sizeof(kMainZWeb)/sizeof(KbKeySpec)); i++)")
    A("                FullPut(kMainZWeb[i], yy, KH, u, gap, bc, nb);")
    A("            continue;")
    A("        }")
    A("        const KbRowSpec& row = kFullRows[r];")
    A("        for (int i = 0; i < row.n; i++) {")
    A("            const KbKeySpec& s = row.keys[i];")
    A("            FullPut(s, yy, s.vrow == 2 ? h2 : KH, u, gap, bc, nb);")
    A("        }")
    A("    }")
    A("")
    A("    // ---- 导航区（6 行）-----------------------------------------------")
    A("    {")
    A("        struct { const KbKeySpec* k; int n; int r; } nav[] = {")
    A("            { kNav0, 3, 0 }, { kNav1, 3, 1 }, { kNav2, 3, 2 },")
    A("            { NULL,  0, 3 }, { kNav4, 1, 4 }, { kNav5, 3, 5 },")
    A("        };")
    A("        for (int i = 0; i < 6; i++) {")
    A("            if (!nav[i].k) continue;")
    A("            int yy = y + nav[i].r * (KH + gap);")
    A("            for (int j = 0; j < nav[i].n; j++)")
    A("                FullPut(nav[i].k[j], yy, KH, u, gap, bc, nb);")
    A("        }")
    A("    }")
    A("")
    A("    // ---- 数字区（5 行：数字行 ↔ Ctrl 行）------------------------------")
    A("    if (!g_npHidden) {")
    A("        struct { const KbKeySpec* k; int n; } num[] = {")
    A("            { kNum0, 4 }, { kNum1, 4 }, { kNum2, 3 }, { kNum3, 4 }, { kNum4, 2 },")
    A("        };")
    A("        for (int i = 0; i < 5; i++) {")
    A("            int yy = y + (i + 1) * (KH + gap);      // ← 从「数字行」起，不是 F 行")
    A("            for (int j = 0; j < num[i].n; j++) {")
    A("                const KbKeySpec& s = num[i].k[j];")
    A("                FullPut(s, yy, s.vrow == 2 ? h2 : KH, u, gap, bc, nb);")
    A("            }")
    A("        }")
    A("    }")
    A("}")
    A("")
    A("// ============================================================================")
    A("// 配套 1：键面标签自适应字号（网格化后导航区键变窄，PrtSc/ScrLk/Pause 会撑破）")
    A("//   现有字体里挑最小可用档，不新建 GDI 对象。")
    A("static HFONT FitKeyFont(HDC dc, const wchar_t* s, int maxW, BOOL bold) {")
    A("    if (!s || !s[0]) return bold ? g_f14b : g_f14;")
    A("    HFONT try3[3] = { bold ? g_f14b : g_f14, bold ? g_f13 : g_f13, g_f12 };")
    A("    for (int i = 0; i < 3; i++)")
    A("        if (MeasureTextW(dc, s, try3[i]) <= maxW) return try3[i];")
    A("    return try3[2];          // 仍放不下就交给 DrawTextC 的省略逻辑")
    A("}")
    A("// 调用点（DrawKeys 里）：")
    A("//   HFONT f = isMod ? g_f14b : g_f14;")
    A("//   HFONT f = FitKeyFont(dc, txt, k->w - (int)(14 * GetSystemDpiScale()), isMod);")
    A("")
    A("// ============================================================================")
    A("// 配套 2：窄屏自适应（复用已有的 g_npHidden 机制，但两者不能互相覆盖）")
    A("//   g_npHidden     = 用户按 Tab 手动隐藏（持久化）")
    A("//   g_npHiddenAuto = 由窗口宽度推导（不持久化）")
    A("//   实际生效：numpadHidden = g_npHidden || g_npHiddenAuto")
    A("")
    A("// 在 BuildKeys() 里、算 rows 之前插入：")
    A("static void UpdateNumpadAuto(int ww, double dpiScale) {")
    A("    if (g_layoutMode != 2) { g_npHiddenAuto = FALSE; return; }")
    A("    // 键帽 >= 44px（触摸安全）反解：22*(44+gap)+2*gap+2*areaX ≈ 1090 DIP")
    A("    g_npHiddenAuto = (ww < (int)(1090 * dpiScale));")
    A("}")
    A("")
    A("// 在 WM_GETMINMAXINFO 里按布局给最小尺寸（现状写死 300×150，全尺寸会被裁掉半张键盘）")
    A("//   layout 2：min.x = 870*dpi（隐藏数字区后的下限），min.y = 300*dpi")
    A("//   layout 1：min.x = 300*dpi")
    A("")
    A("// ============================================================================")
    A("// 配套 3：主键盘帧缓存签名必须加字段，否则切「自动隐藏数字区」时主键盘纹丝不动")
    A("//   struct KbFrameSig { ... bool npHiddenAuto; ... };  // + 参与 memcmp")
    A("")
    A("// 配套 4：标题栏按钮——现状 numBtnVisible = g_showNumBtn && g_layoutMode != 2；")
    A("//   一旦引入自动隐藏，被隐藏时必须把按钮放出来，否则用户没法把数字区叫回来：")
    A("//   hm.numBtnVisible = g_showNumBtn && (g_layoutMode != 2 || numpadHidden);")
    A("")
    A("// 配套 5：默认窗口尺寸。现状 1380×372 → 键帽 52×52 正方应改为 1280×404")
    A("//   layout 2: g_ww = 1280*dpi, g_wh = 404*dpi   （1u = 56.8px，键帽 52px）")
    A("")

    with io.open(out_path, "w", encoding="utf-8") as f:
        f.write("\n".join(L))
    print("已写出 %s（%d 行）" % (out_path, len(L)))

if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "hk_full_layout_unit.cpp")
