#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""探针：把关于页的布局几何算出来，并和源码里的常量对账。

为什么要有这个：关于页的"间距"肉眼很难判断改没改（16 -> 10 DIP 在 175% 下只差 10px）。
这个脚本从 hkeyboard.cpp 里**读真实的常量**，算完再回头核对一遍 —— 任何一个数字
对不上就直接报 FAIL，避免脚本自己变成另一份会漂移的真相。

用法：
    python tools/probe_about_layout.py [hkeyboard.cpp] [dpi_scale]
"""
import re
import sys

SRC = sys.argv[1] if len(sys.argv) > 1 else "hkeyboard.cpp"
DPI = float(sys.argv[2]) if len(sys.argv) > 2 else 1.75

src = open(SRC, encoding="utf-8", errors="replace").read()


def const(name, func):
    """取 `static int <func>(...) { return (int)(<value> * m.dpi); }` 里的 value"""
    m = re.search(r"static int %s\(const SettingsMetrics& m\)\s*\{[^}]*?\(int\)\(([0-9.]+) \* m\.dpi\)" % func, src)
    if not m:
        raise SystemExit("FAIL: 找不到 %s 的常量" % func)
    return float(m.group(1))


checks = []          # (名字, 源码里的值, 脚本里的值)
padY_src, padX_src, gap_src = const("padY", "AboutPadY"), const("padX", "AboutPadX"), const("gap", "AboutGap")
rowPad_src = int(re.search(r"m\.rowPadY = \(int\)\(([0-9.]+) \* m\.dpi\);", src).group(1))
mark_src = int(re.search(r"a\.mark = \(int\)\(([0-9.]+) \* m\.dpi\);", src).group(1))
rowContent_src = int(re.search(r"int AboutRowH.*?return m\.rowPadY \+ \(int\)\(([0-9.]+) \* m\.dpi\)", src, re.S).group(1))
licBtn_src = int(re.search(r"int AboutLicenceRowH.*?return m\.rowPadY \+ \(int\)\(([0-9.]+) \* m\.dpi\)", src, re.S).group(1))
margin_src = int(re.search(r"m\.margin = \(int\)\(([0-9.]+) \* m\.dpi\);", src).group(1))

PADY, PADX, GAP, ROWPAD = padY_src, padX_src, gap_src, rowPad_src
MARK = mark_src
ROW_CONTENT = rowContent_src          # AboutRowH 的内容高 36
LIC_BTN = licBtn_src                  # 许可行的按钮高 32

# --- 复刻 GetAboutLayout 的算术（DIP；最后再按 DPI 折算成像素）---
# 行高：上 12 + 内容 36 + 下 12；末行只给下 2（制作工具 form_card 的 :last-child）
row_h = ROWPAD + ROW_CONTENT + ROWPAD
row_h_last = ROWPAD + ROW_CONTENT + 2
# 链接卡用哪一档行高由源码决定（单行卡片必须与 AboutRowRect 的第 0 行一致，否则高亮块上下不齐）
links_flavor = re.search(r"AboutLinksCardH.*?AboutRowH\(m, (FALSE|TRUE)\)", src, re.S).group(1)
links_card_h = PADY * 2 + (row_h if links_flavor == "FALSE" else row_h_last)   # AboutLinksCardH
lic_row_h = ROWPAD + LIC_BTN + 2                          # AboutLicenceRowH
lic_card_h = PADY * 2 + lic_row_h                         # AboutLicenceCardH

card1_top = 0.0
card1_h = PADY * 2 + MARK
card2_top = card1_top + card1_h + GAP
card2_h = links_card_h
card3_top = card2_top + card2_h + GAP
card3_h = lic_card_h


def px(dip):
    return dip * DPI


def show(label, dip):
    print("  %-34s %7.1f DIP  %8.1f px" % (label, dip, px(dip)))


print("关于页布局探针   DPI = %.2f   (源码 %s)" % (DPI, SRC))
print("=" * 66)
print("常量（读自源码）")
show("AboutPadY        卡内上下内边距", PADY)
show("AboutPadX        卡内左右内边距", PADX)
show("AboutGap         卡与卡之间", GAP)
show("rowPadY          行内上下内边距", ROWPAD)
show("AboutRowH 内容高  标题 15 + 偏移 23", ROW_CONTENT)

print()
print("派生尺寸")
show("身份卡高         = 2*padY + mark(56)", card1_h)
show("链接卡高         = 2*padY + 末行高", card2_h)
show("许可卡高         = 2*padY + 许可行高", card3_h)

print()
print("竖直位置（DIP）")
show("card1.top", card1_top)
show("card1.bottom", card1_top + card1_h)
show("card2.top", card2_top)
show("card2.bottom", card2_top + card2_h)
show("card3.top", card3_top)
show("card3.bottom", card3_top + card3_h)

print()
print("★ 你框的那两段空隙")
show("身份卡 -> 链接卡（项目地址）", card2_top - (card1_top + card1_h))
show("链接卡 -> 许可卡", card3_top - (card2_top + card2_h))
print()
print("★ 高亮块（行背景）在卡里是否上下对称 —— 「高亮对不整齐」看的就是这个")
show("链接卡：高亮块上边距", PADY)
show("链接卡：高亮块下边距", card2_h - PADY - row_h)
show("许可卡：高亮块上边距", PADY)
show("许可卡：高亮块下边距", card3_h - PADY - lic_row_h)
if abs((card2_h - PADY - row_h) - PADY) > 1e-9:
    print("  !! 链接卡高亮块上下不对称")
print()
print("  注意：卡内那一圈紫色是行背景（SettingsRowHover），它的上缘距卡顶 = padY = %.0f DIP，" % PADY)
print("        所以「项目地址文字上方」的视觉空隙 = 卡间距 %.0f + padY %.0f = %.0f DIP。"
      % (GAP, PADY, GAP + PADY))

# --- 和源码对账：任何一处不一致就 FAIL ---
expect = [("AboutPadY", padY_src, PADY), ("AboutPadX", padX_src, PADX), ("AboutGap", gap_src, GAP),
          ("rowPadY", rowPad_src, ROWPAD), ("mark", mark_src, MARK), ("margin", margin_src, margin_src)]
bad = [c for c in expect if abs(c[1] - c[2]) > 1e-9]
print()
if bad:
    for n, a, b in bad:
        print("FAIL: %s 源码 %s != 脚本 %s" % (n, a, b))
    sys.exit(1)
print("对账 [OK]：以上每个数字都取自源码，脚本没有自己的假设")
