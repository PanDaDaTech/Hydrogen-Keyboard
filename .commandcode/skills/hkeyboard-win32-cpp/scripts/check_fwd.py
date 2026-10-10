# -*- coding: utf-8 -*-
import re, sys
src = open('hkeyboard.cpp', encoding='utf-8').read()
lines = src.splitlines()

# 1) collect static function definitions  (line index 0-based)
defre = re.compile(r'^\s*static\s+(?:[\w:<>,*&\s]+?)\b(\w+)\s*\([^;]*\)\s*(?:const\s*)?\{')
defs = {}   # name -> line no (1-based)
for i, l in enumerate(lines):
    m = defre.match(l)
    if m:
        nm = m.group(1)
        if nm not in ('if','for','while','switch','return'):
            defs.setdefault(nm, i+1)

# 2) collect forward declarations (lines ending with ');')
fwd = set()
fwdre = re.compile(r'^\s*static\s+(?:[\w:<>,*&\s]+?)\b(\w+)\s*\([^;{]*\)\s*;')
for l in lines:
    m = fwdre.match(l)
    if m: fwd.add(m.group(1))

# 3) find call sites before definition
problems = []
for nm, dline in defs.items():
    if nm in fwd:
        continue
    pat = re.compile(r'\b' + re.escape(nm) + r'\s*\(')
    for i, l in enumerate(lines[:dline-1]):
        stripped = l.strip()
        if stripped.startswith('//') or stripped.startswith('*'):
            continue
        # skip the declaration-ish lines of the definition itself
        if pat.search(l):
            problems.append((nm, dline, i+1, l.strip()[:100]))
            break

print('static function definitions:', len(defs))
print('forward declarations      :', len(fwd))
print()
if not problems:
    print('[OK] 未发现"先用后定义"的静态函数')
else:
    print('[WARN] 以下静态函数在定义之前被调用，且无前置声明：')
    for nm, d, u, txt in sorted(problems, key=lambda x: x[2]):
        print(f'  行{u:>5} 调用  {nm}()   (定义在 行{d})')
        print(f'          {txt}')
