# -*- coding: utf-8 -*-
"""括号/引号平衡检查（跳过字符串、字符字面量、注释、预处理续行）。

本机没有 C++ 编译器，改动后先用它兜住「漏一个 } 或 )」这类低级错误 ——
CI 是唯一真编，但等 CI 反馈太慢，本地先筛一遍。
用法：在仓库根目录运行 `python check_balance.py`。
"""
import sys

src = open('hkeyboard.cpp', encoding='utf-8').read()
n = len(src)

i = 0
line = 1
stack = []          # (char, line)
PAIR = {')': '(', ']': '[', '}': '{'}
counts = {'(': 0, ')': 0, '[': 0, ']': 0, '{': 0, '}': 0}
errors = []

while i < n:
    c = src[i]

    if c == '\n':
        line += 1
        i += 1
        continue

    # 行注释
    if c == '/' and i + 1 < n and src[i + 1] == '/':
        while i < n and src[i] != '\n':
            i += 1
        continue

    # 块注释
    if c == '/' and i + 1 < n and src[i + 1] == '*':
        i += 2
        while i + 1 < n and not (src[i] == '*' and src[i + 1] == '/'):
            if src[i] == '\n':
                line += 1
            i += 1
        i += 2
        continue

    # 字符串 / 字符字面量（含转义）
    if c == '"' or c == "'":
        q = c
        i += 1
        while i < n:
            if src[i] == '\\':
                i += 2
                continue
            if src[i] == q:
                i += 1
                break
            if src[i] == '\n':
                errors.append("行 %d: 字面量未闭合" % line)
                break
            i += 1
        continue

    # 预处理续行
    if c == '\\' and i + 1 < n and src[i + 1] == '\n':
        line += 1
        i += 2
        continue

    if c in counts:
        counts[c] += 1
        if c in '([{':
            stack.append((c, line))
        else:
            if not stack:
                errors.append("行 %d: 多余的 '%s'" % (line, c))
            else:
                o, ol = stack.pop()
                if o != PAIR[c]:
                    errors.append("行 %d: '%s' 与行 %d 的 '%s' 不匹配" % (line, c, ol, o))
    i += 1

for o, ol in stack:
    errors.append("行 %d: '%s' 未闭合" % (ol, o))

print("符号计数  () = %d/%d   [] = %d/%d   {} = %d/%d"
      % (counts['('], counts[')'], counts['['], counts[']'], counts['{'], counts['}']))
print("总行数    %d" % src.count('\n'))

if errors:
    print("\n[FAIL] 发现 %d 处问题：" % len(errors))
    for e in errors[:40]:
        print("  " + e)
    sys.exit(1)

print("\n[OK] 括号与字面量全部闭合")
