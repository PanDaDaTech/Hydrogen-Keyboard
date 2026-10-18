# -*- coding: utf-8 -*-
"""解析 HKeyboard 导出的诊断日志：摘要 + 变化时间线。

用法：python analyze_diag.py <diag.txt> [--timeline-only]
"""
import io
import re
import sys
from collections import Counter

path = sys.argv[1]
only_tl = "--timeline-only" in sys.argv
s = io.open(path, encoding="utf-8", errors="replace").read()

m = re.search(r"\d\d:\d\d:\d\d\.\d\d\d ", s)
head = s[:m.start()]
ring = s[m.start():]

if not only_tl:
    print("=" * 78)
    print(head.rstrip())
    print("=" * 78)

entries = re.split(r"(?<=\d\d:\d\d:\d\d\.\d\d\d )(?=\d\d:\d\d:\d\d\.\d\d\d )", ring)
entries = [e for e in entries if e.strip()]
print("环形记录 %d 条；最长 %d 字符；被截断(>=318) %d 条"
      % (len(entries), max(len(e) for e in entries),
         sum(1 for e in entries if len(e) >= 318)))


def g(e, name, pat=r"([-\w]+)"):
    mm = re.search(r"\b" + name + "=" + pat, e)
    return mm.group(1) if mm else "?"


if not only_tl:
    print()
    print("--- 字段分布 ---")
    for f in ("rc", "cif", "key", "await", "vis", "mot", "uiact", "pt", "ptc", "inFg", "hid"):
        print("  %-6s %s" % (f, dict(Counter(g(e, f) for e in entries).most_common(8))))
    print()
    print("--- 前台窗口类分布 ---")
    for k, v in Counter(g(e, "fg", r"(\S+)") for e in entries).most_common(20):
        print("  %-28s %d" % (k, v))
    print()
    print("--- 事件行 / ShowKB ---")
    for e in entries:
        if " eval " not in e:
            print("  ", e[:190])

print()
print("--- 变化时间线（只在字段变化时打印） ---")
print("时间戳         fg                          rc cif key await inN vis   变化")
prev = None
for e in entries:
    ts = e[:12]
    fg = g(e, "fg", r"(\S+)")
    cur = (fg, g(e, "rc"), g(e, "cif"), g(e, "key"), g(e, "await"),
           "N" if re.search(r"input=0{16}", e) else "Y", g(e, "vis"))
    if cur != prev:
        note = ""
        if prev is not None:
            note = ",".join(n for n, a, b in zip(
                ("fg", "rc", "cif", "key", "await", "input", "vis"), prev, cur) if a != b)
        print("%s %-27s %s   %s   %s   %s     %s   %s   %s" %
              (ts, fg[:27], cur[1], cur[2], cur[3], cur[4], cur[5], cur[6], note))
        prev = cur
