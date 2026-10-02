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

def source_chars(text):
    """提取 C 源码里所有字符串字面量的字符（含 \\uXXXX / \\xNN 转义）。"""
    out = set()
    for m in re.finditer(r'(?:L)?"((?:[^"\\]|\\.)*)"', text):
        s = m.group(1)
        i = 0
        while i < len(s):
            c = s[i]
            if c == "\\" and i + 1 < len(s):
                n = s[i + 1]
                if n == "u" and i + 6 <= len(s):       # \uXXXX
                    try:
                        out.add(chr(int(s[i + 2:i + 6], 16))); i += 6; continue
                    except ValueError:
                        pass
                if n == "x":                            # \xNN...
                    j = i + 2
                    while j < len(s) and s[j] in "0123456789abcdefABCDEF" and j - i < 5:
                        j += 1
                    try:
                        out.add(chr(int(s[i + 2:j], 16))); i = j; continue
                    except ValueError:
                        pass
                out.add(ESCAPES.get(n, n)); i += 2; continue
            out.add(c); i += 1
    return out

def collect():
    chars = {chr(c) for c in range(0x20, 0x7F)}
    chars |= source_chars(SRC.read_text(encoding="utf-8", errors="replace"))
    for p in (CHARS_TXT, PUA_TXT):
        if p.exists():
            chars |= set(p.read_text(encoding="utf-8", errors="replace"))
    chars -= set("\r\n\t\0")
    return chars

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
    font.save(str(TTF_OUT))
    print("[OK] 已重建 %s: %d 字符, %d 字节" % (TTF_OUT.relative_to(ROOT),
                                               len(chars), TTF_OUT.stat().st_size))
    # 重建后立即自检一遍
    cmap2 = TTFont(str(TTF_OUT)).getBestCmap()
    miss2 = [c for c in chars if ord(c) not in cmap2 and ord(c) >= 0x80 and not is_ok(c)]
    if miss2:
        print("[ERR] 重建后仍有缺失: %s" % " ".join(miss2)); return 1
    print("[OK] 重建后自检通过（cmap %d 条）" % len(cmap2))
    return 0

if __name__ == "__main__":
    sys.exit(main())
