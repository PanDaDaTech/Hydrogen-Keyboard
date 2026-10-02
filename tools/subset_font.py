# -*- coding: utf-8 -*-
"""
subset_font.py -- 重建 winres/MiSans-Medium.ttf 子集，并校验源码 UI 字符全覆盖。

背景（为什么这个脚本必须存在）：
  程序内嵌的是 MiSans Medium 的**子集**（全量 8 MB -> 子集约 170 KB，见 hkeyboard.rc 的
  IDR_FONT RCDATA）。子集只包含生成时字符表里的字 —— 以后**新增任何 UI 文案**（T(L"...")、
  L"..." 字符串），只要出现字符表之外的字，那一行字就会部分回退到系统宋体：
  同一行里 MiSans 与宋体混排，笔画一粗一细，肉眼即「部分字体显示不对劲」。
  实例：v2.0 关于页新增「社区交流 / 开源许可」，「社」「流」「协」不在子集里，
  章节标题一行两种字体（2026-10 实机截图确认）。

用法：
  python tools/subset_font.py            # 重建 winres/MiSans-Medium.ttf
  python tools/subset_font.py --check    # 只校验：源码字符是否全部落在现有 ttf 的 cmap 内

依赖：pip install fonttools（本机用 WorkBuddy 的 PIL venv，已带）。

字符收集规则（宁可多收，不漏）：
  1. hkeyboard.cpp 中所有 C 字符串字面量（含 T(a, b) 两个参数）里的字符；
  2. ASCII 可打印全集 0x20-0x7E（版本号 / 路径 / 键名运行时拼接的兜底）；
  3. 旧字符表 build/_ui_chars.txt 全量保留（历史字符不清出，防止偶发遗漏）;
  4. build/_pua.txt 保留。
  生成字体族名不变（MiSans），GDI 与 GDI+ 两条路径都按族名取字，无需改代码。
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "hkeyboard.cpp"
TTF_OUT = ROOT / "winres" / "MiSans-Medium.ttf"
TTF_FULL = ROOT / "build" / "font-backup" / "MiSans-Medium.ttf"
CHARS_TXT = ROOT / "build" / "_ui_chars.txt"
PUA_TXT = ROOT / "build" / "_pua.txt"

ESCAPES = {"n": "\n", "t": "\t", "r": "\r", "\\": "\\", '"': '"', "'": "'",
           "0": "\0", "a": "\a", "b": "\b", "f": "\f", "v": "\v"}

def _escaped(text, i, out):
    """处理 text[i] == '\\' 的转义序列，把结果字符加入 out，返回新的 i。"""
    n = len(text)
    if i + 1 >= n:
        return i + 1
    c = text[i + 1]
    if c == "u" and i + 6 <= n:                       # \uXXXX
        try:
            out.add(chr(int(text[i + 2:i + 6], 16))); return i + 6
        except ValueError:
            pass
    if c == "x":                                      # \xNN...
        j = i + 2
        while j < n and text[j] in "0123456789abcdefABCDEF" and j - i < 5:
            j += 1
        try:
            out.add(chr(int(text[i + 2:j], 16))); return j
        except ValueError:
            pass
    out.add(ESCAPES.get(c, c))
    return i + 2

def source_chars(text):
    """提取 C 源码里所有字符串 / 字符字面量的字符。

    ⚠ 必须用状态机扫描，不能用正则 —— 两个真实的坑（都踩过）：
      1. 注释里有孤立双引号：`// ... `{ } | : "`（副符号格）...`（L396），
         正则把之后 518 行吞成一个假字符串；
      2. 字符字面量包着双引号：键位表里的 `L'"'`（L2109 附近），
         正则把它当字符串起点，从这里起连锁错位 —— 「遇到 bug...」的「遇」
         就是这么从字符集里丢掉的（U+9047 不进子集 → 实机单个字回退宋体）。
    状态机处理 // 行注释、块注释、"字符串"、'字符字面量'（内容也收：键面符号）。
    """
    out = set()
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":      # 行注释
            while i < n and text[i] != "\n":
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":    # 块注释
            i += 2
            while i + 1 < n and not (text[i] == "*" and text[i + 1] == "/"):
                i += 1
            i += 2
        elif c == '"':                                          # 字符串
            i += 1
            while i < n and text[i] != '"':
                if text[i] == "\\":
                    i = _escaped(text, i, out)
                else:
                    out.add(text[i]); i += 1
            i += 1
        elif c == "'":                                          # 字符字面量（含 L'"' 这类）
            i += 1
            while i < n and text[i] != "'":
                if text[i] == "\\":
                    i = _escaped(text, i, out)
                else:
                    out.add(text[i]); i += 1
            i += 1
        else:
            i += 1
    return out

def collect():
    chars = {chr(c) for c in range(0x20, 0x7F)}
    chars |= source_chars(SRC.read_text(encoding="utf-8", errors="replace"))
    for p in (CHARS_TXT, PUA_TXT):
        if p.exists():
            chars |= set(p.read_text(encoding="utf-8", errors="replace"))
    chars -= set("\r\n\t\0")
    return chars

def _family_name(font):
    for r in font["name"].names:
        if r.nameID == 1 and r.platformID == 3:
            return r.toUnicode()
    return None

def allowed_missing(full_cmap):
    """两类字符允许不在子集里（全量字体也没有，历来靠系统回退）：
    1. PUA 私有区 U+E000-U+F8FF（_pua.txt 里的 Segoe 图标码位）；
    2. 全量 MiSans cmap 里就不存在的码点（如 ⚠ ↔ ⇔）。
    """
    def ok(c):
        cp = ord(c)
        return 0xE000 <= cp <= 0xF8FF or cp not in full_cmap
    return ok

def main():
    check_only = "--check" in sys.argv
    from fontTools.ttLib import TTFont

    chars = collect()
    print("源码 UI 字符数（含 ASCII）: %d" % len(chars))

    full_cmap = TTFont(str(TTF_FULL)).getBestCmap()
    is_ok = allowed_missing(full_cmap)

    font = TTFont(str(TTF_OUT if check_only else TTF_FULL))
    cmap = font.getBestCmap()
    missing = sorted(c for c in chars if ord(c) not in cmap and ord(c) >= 0x80 and not is_ok(c))
    if check_only:
        if missing:
            print("[ERR] 以下 %d 个 UI 字符不在 winres/MiSans-Medium.ttf 子集内，"
                  "界面会回退宋体（重跑 tools/subset_font.py 重建）:" % len(missing))
            for c in missing:
                print("   U+%04X %s" % (ord(c), c))
            return 1
        print("[OK] 子集覆盖源码全部 UI 字符（cmap %d 条）" % len(cmap))
        os2 = font["OS/2"]
        if os2.usWeightClass != 500 or not (os2.fsSelection & 0x40):
            print("[ERR] OS/2 元数据异常：usWeightClass=%d fsSelection=0x%04X"
                  "（应为 500 / 含 0x40 REGULAR，否则 GDI 回退宋体，重跑重建）"
                  % (os2.usWeightClass, os2.fsSelection))
            return 1
        fam = _family_name(font)
        if fam != "MiSans":
            print("[ERR] name ID1(family) = %r（应为 'MiSans'，否则 GDI+ FontFamilyNotFound，"
                  "重跑重建）" % fam)
            return 1
        print("[OK] OS/2 元数据正常（usWeightClass=500, fsSelection=0x%04X）；family=%r"
              % (os2.fsSelection, fam))
        return 0

    # 重建：只保留收集到的字符（含缺的），从全量字体子集化
    from fontTools import subset
    options = subset.Options()
    options.name_IDs = ["*"]          # 保留全部 name 记录：GDI 按族名 MiSans 找字体
    options.notdef_outline = True     # .notdef 保留轮廓，缺字时有形可画
    options.recalc_bounds = True
    options.drop_tables += ["FFTM"]   # 无用表
    ss = subset.Subsetter(options)
    ss.populate(unicodes=[ord(c) for c in chars])
    ss.subset(font)

    # —— 关键修正：小米原版 MiSans 的 OS/2 元数据是「非标准」的，必须重写，否则全局宋体 ——
    #   原版 usWeightClass=380（标准应是 100~900 整百）、fsSelection=0x0100（无 REGULAR 位，
    #   也无 BOLD/ITALIC 位）。GDI 的 CreateFontW(FW_NORMAL=400, "MiSans") 与 GDI+ 的
    #   FontStyleRegular 都按字重/style 匹配，匹配不到「380 / 既非 Regular 也非 Bold」的
    #   字体会静默回退系统默认字体（宋体）→ 整个界面全局宋体。
    #   能正常显示的那版旧子集正是把这两项修成 500 / 0x0140（见 git 历史 173608 B 那版）。
    os2 = font["OS/2"]
    os2.usWeightClass = 500                                       # Medium（原版错标成 380）
    os2.fsSelection = (os2.fsSelection & ~0x21) | 0x40            # 清 BOLD(0x20)+ITALIC(0x01)，置 REGULAR(0x40)

    # —— 关键修正 2：name 表 —— 程序按族名「MiSans」找字体（CreateFontW L"MiSans" /
    #    GDI+ FontFamily(L"MiSans", collection)），而原版 family name(ID1) 是「MiSans Medium」、
    #    subfamily(ID2) 是「Regular」。GDI+ 严格按 ID1 匹配 → FontFamilyNotFound(14)，
    #    DrawTextGp 失败回退 GDI/系统字体 → 全局宋体（这是比 OS/2 更致命的一处）。
    #    旧子集把 ID1/ID2 改成「MiSans」/「Medium」，并只保留 Windows(3) 平台记录。
    nametbl = font["name"]
    nametbl.names = [r for r in nametbl.names if r.platformID == 3]
    for r in nametbl.names:
        if r.nameID == 1:
            r.string = "MiSans"
        elif r.nameID == 2:
            r.string = "Medium"

    font.save(str(TTF_OUT))
    print("[OK] 已重建 %s: %d 字符, %d 字节" % (TTF_OUT.relative_to(ROOT),
                                               len(chars), TTF_OUT.stat().st_size))
    # 重建后立即自检一遍
    cmap2 = TTFont(str(TTF_OUT)).getBestCmap()
    miss2 = [c for c in chars if ord(c) not in cmap2 and ord(c) >= 0x80 and not is_ok(c)]
    if miss2:
        print("[ERR] 重建后仍有缺失: %s" % " ".join(miss2)); return 1
    f3 = TTFont(str(TTF_OUT))
    os2b = f3["OS/2"]
    if os2b.usWeightClass != 500 or not (os2b.fsSelection & 0x40):
        print("[ERR] 重建后 OS/2 元数据异常: usWeightClass=%d fsSelection=0x%04X"
              % (os2b.usWeightClass, os2b.fsSelection)); return 1
    fam = _family_name(f3)
    if fam != "MiSans":
        print("[ERR] 重建后 name ID1(family) = %r（应为 MiSans）" % fam); return 1
    print("[OK] 重建后自检通过（cmap %d 条 / usWeightClass=%d / fsSelection=0x%04X / family=%r）"
          % (len(cmap2), os2b.usWeightClass, os2b.fsSelection, fam))
    return 0

if __name__ == "__main__":
    sys.exit(main())
