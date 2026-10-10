#!/usr/bin/env python3
"""推前预检 3/3：Windows/CRT 宏名冲突扫描。

背景（真实 CI 失败）：
    hkeyboard.cpp(1494): error C2632: 'int' followed by 'char' is illegal
    hkeyboard.cpp(1494): error C2513: 'int': no variable declared before '='
根因：局部变量写成 `int small = ...`，而 <windows.h> 的 rpcndr.h 里有
      `#define small char` —— 预处理后变成 `int char = ...`。
      `pure`（老 MFC/ATL 的 `#define pure =0`）、`near`/`far`/`hyper` 同理。

用法（在仓库根目录跑，脚本硬编码读 hkeyboard.cpp）：
    python check_macros.py

输出：命中的 file:line 与宏名。期望「[OK] 未发现与 Windows/CRT 宏同名的标识符」。
只扫描**声明位置**（变量/参数），避免把注释、字符串、结构体成员引用误报。
"""
import os
import re
import sys

SRC = "hkeyboard.cpp"

# <windows.h> / rpcndr.h / windef.h / winnt.h / 老 MFC-ATL 里会静默改写
# 标识符的宏。命名局部变量或参数时一律避开。
# 注意只收「宏」：y0/y1/j0/j1/index/major/minor 只是老 CRT 的函数名、不是宏，
# HKeyboard 里本来就有 `int y1` / `int index` 这类参数，命名成那样是安全的。
KNOWN_MACROS = {
    # rpcndr.h（← 本次 CI 失败就在这里：`int small` 变成 `int char`）
    "small", "hyper", "byte", "boolean",
    # windef.h / winnt.h
    "min", "max", "near", "far", "near16", "far16", "IN", "OUT", "OPTIONAL",
    "CONST", "VOID", "CHAR", "SHORT", "LONG", "UCHAR", "WCHAR", "TCHAR",
    "TRUE", "FALSE", "TEXT", "DELETE", "ERROR", "Yield", "interface",
    "pascal", "cdecl", "stdcall", "THIS", "APIENTRY", "CALLBACK", "WINAPI",
    # 老 MFC / ATL
    "pure", "DEBUG", "RELEASE",
}

DECL = re.compile(
    # 声明起点：行首（含缩进）或紧跟 ; { } ( ,
    r"(?:^|[;{}(,])\s*"
    r"(?:const\s+|static\s+|unsigned\s+|signed\s+)*"
    r"(?:char|short|int|long|float|double|bool|BOOL|BYTE|WORD|DWORD|QWORD|"
    r"UINT|INT|UCHAR|WCHAR|TCHAR|HFONT|HDC|HWND|RECT|SIZE|POINT|LONG|HRESULT|"
    r"size_t|wchar_t|Gdiplus::\w+|[A-Z]\w*)\s*"
    r"(?:const\s+)?[\*&]*\s*"
    # 注意：不把 ')' 作为合法的后随字符 —— 否则 `(LONGLONG)` 这种强转会
    # 被当成「类型 LONG + 变量名 LONG」而误报。
    r"([A-Za-z_]\w*)\s*(?:=|;|\[|,)"
)


def strip_literals_and_comments(src):
    out, i, n = [], 0, len(src)
    while i < n:
        c = src[i]
        if c in "\"'":
            q, i = c, i + 1
            while i < n:
                if src[i] == "\\":
                    i += 2
                    continue
                if src[i] == q:
                    break
                i += 1
            i += 1
            out.append(" ")
        elif src.startswith("//", i):
            while i < n and src[i] != "\n":
                i += 1
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            i = n if j < 0 else j + 2
        else:
            out.append(c)
            i += 1
    return "".join(out)


def main():
    if not os.path.exists(SRC):
        print("[ERR] 找不到 %s（请在仓库根目录运行）" % SRC)
        return 2
    src = strip_literals_and_comments(open(SRC, encoding="utf-8").read())
    hits = []
    for ln, line in enumerate(src.split("\n"), 1):
        for m in DECL.finditer(line):
            name = m.group(1)
            if name in KNOWN_MACROS:
                hits.append((ln, name, line.strip()[:90]))
    if hits:
        print("[FAIL] 以下标识符与 Windows/CRT 宏同名，会被预处理改写：")
        for ln, name, text in hits:
            print("   %s(%d): '%s'  <- %s" % (SRC, ln, name, text))
        return 1
    print("[OK] 未发现与 Windows/CRT 宏同名的标识符")
    return 0


if __name__ == "__main__":
    sys.exit(main())
