import io, re, sys
from collections import Counter

path = sys.argv[1]
raw = io.open(path, encoding="utf-8", errors="replace").read()
print("=== 文件 ===", path.split("\\")[-1], len(raw), "chars")
# 头部（环境快照）与运行记录分界：找第一条时间戳行
m = re.search(r"^\d\d:\d\d:\d\d\.\d\d\d ", raw, re.M)
head = raw[:m.start()] if m else raw
ring = raw[m.start():] if m else ""

print("\n=== 头部要点 ===")
for line in head.splitlines():
    if re.search(r"monitor|dpi|scale|layout|touch|ini|precise|state|verdict|trust|tabStop", line, re.I):
        print("  ", line.strip()[:120])

print("\n=== 记录条数 ===", len(ring.splitlines()))

ev = [l for l in ring.splitlines() if " eval " in l]
show = [l for l in ring.splitlines() if " ShowKB" in l]
print("eval", len(ev), " ShowKB", len(show))

def f(l, k, pat=r"(-?[\w.]+)"):
    m = re.search(r"\b%s=%s" % (k, pat), l)
    return m.group(1) if m else "?"

print("\n=== why 分布 ===")
print(" ", dict(Counter(f(l, "why") for l in ev).most_common()))
print("\n=== ShowKB 时序 ===")
for l in show[:40]:
    print("  ", l[:11], "vis=%s" % f(l, "vis"), "manual=%s" % f(l, "manual"), (f(l, "=>"))[:20])

print("\n=== 状态变化点（why / vis / input 任一变化）===")
prev = None
n = 0
for l in ring.splitlines():
    if " eval " not in l:
        continue
    cur = (f(l, "why"), f(l, "vis"), f(l, "input") != "0000000000000000", f(l, "uiact"), f(l, "hid"))
    if cur != prev:
        print("  %s why=%-14s vis=%s in=%s uiact=%-6s hid=%s fg=%s"
              % (l[:11], cur[0], cur[1], "Y" if cur[2] else "N", cur[3], cur[4], f(l, "fg", r"(\S+)")))
        prev = cur
        n += 1
        if n > 60:
            print("  ...")
            break
