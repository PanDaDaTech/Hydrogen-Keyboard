# -*- coding: utf-8 -*-
"""探「可编辑输入框」在 UIA 里到底怎么表达。

目的：给 C++ 侧找一条**普适**的 editable 判据。
只读，不点击、不移动鼠标。
"""
import sys
import uiautomation as auto

P = auto.PropertyId if hasattr(auto, "PropertyId") else None

TEXT2 = 30148      # UIA_IsTextPattern2AvailablePropertyId
TEXT1 = 30032      # UIA_IsTextPatternAvailablePropertyId
VALUEP = 30043     # UIA_IsValuePatternAvailablePropertyId
KFOCUSABLE = 30009
KFOCUS = 30008
NATIVEHWND = 30020
PID = 30026


def pv(el, pid):
    try:
        return el.GetPropertyValue(pid)
    except Exception:
        return "ERR"


def describe(el, indent="  "):
    r = el.BoundingRectangle
    ct = el.ControlTypeName
    flags = []
    if pv(el, TEXT2) is True:
        flags.append("Text2")
    if pv(el, TEXT1) is True:
        flags.append("Text1")
    if pv(el, VALUEP) is True:
        flags.append("Value")
    if pv(el, KFOCUSABLE) is True:
        flags.append("KFocusable")
    if pv(el, KFOCUS) is True:
        flags.append("HASFOCUS")
    print("%s%-18s rect=(%4d,%4d %4dx%4d) name=[%s] cls=[%s] %s" % (
        indent, ct, r.left, r.top, r.right - r.left, r.bottom - r.top,
        (el.Name or "")[:18], (el.ClassName or "")[:22], " ".join(flags)))


def walk(el, depth=0, out=None, cap=400):
    if out is None:
        out = []
    if depth > 14 or len(out) > cap:
        return out
    try:
        kids = el.GetChildren()
    except Exception:
        return out
    for k in kids:
        if len(out) > cap:
            break
        out.append((k, depth))
        walk(k, depth + 1, out, cap)
    return out


def main():
    pat = sys.argv[1] if len(sys.argv) > 1 else ""
    root = auto.GetRootControl()
    targets = []
    for w in root.GetChildren():
        try:
            n = (w.Name or "")
            if pat.lower() in n.lower() or pat.lower() in (w.ClassName or "").lower():
                targets.append(w)
        except Exception:
            pass
    if not targets:
        print("没找到匹配 '%s' 的顶层窗口。当前顶层窗口：" % pat)
        for w in root.GetChildren():
            try:
                print("   [%s] %s | %s" % (w.ControlTypeName, (w.Name or "")[:28], (w.ClassName or "")[:26]))
            except Exception:
                pass
        return

    for w in targets[:3]:
        print("=" * 70)
        print("窗口: [%s] cls=%s" % ((w.Name or "")[:30], w.ClassName))
        nodes = walk(w)
        print("节点数 %d" % len(nodes))
        from collections import Counter
        print("ControlType 分布:", Counter(k.ControlTypeName for k, _ in nodes).most_common(12))
        print("- 带文本/值模式的元素：")
        hits = 0
        for k, d in nodes:
            f = pv(k, TEXT2) is True or pv(k, TEXT1) is True or pv(k, VALUEP) is True
            if f:
                describe(k, "  ")
                hits += 1
                if hits >= 18:
                    break
        if hits == 0:
            print("  （没有）")
        print("- 带键盘焦点的元素：")
        for k, d in nodes:
            if pv(k, KFOCUS) is True:
                describe(k, "  ")
        print("- 落点查询（对最可能的输入框区域）：")
        r = w.BoundingRectangle
        pts = [(r.left + (r.right - r.left) // 2, r.bottom - 90),
               (r.left + (r.right - r.left) // 4, r.bottom - 120)]
        for (x, y) in pts:
            try:
                el = auto.ControlFromPoint(x, y)
                print("  (%d,%d) ->" % (x, y))
                describe(el, "      ")
            except Exception as e:
                print("  (%d,%d) -> ERR %s" % (x, y, e))

    foc = auto.GetFocusedControl()
    if foc:
        print("=" * 70)
        print("当前全局焦点：")
        describe(foc, "  ")


main()
