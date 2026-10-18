# uia_setfocus_test.py
# 自驱实验：用 UIA 的 SetFocus 把焦点在「输入框」和「页面主体」之间来回切，
# 观察 GetFocusedElement 的 ControlType 是否跟着变。
#
# 要回答的问题：`ControlType == EditControl` 能不能当「焦点在可编辑输入框里」的判据？
#   （历史注释说"整张网页永远有一个 Document 持有焦点"，导致判据失效 —— 我这里验证它）
import sys
import time

import uiautomation as auto


def dump(tag, c):
    if not c:
        print("%-26s -> <null>" % tag)
        return
    print("%-26s -> ctl=%-16s cls=%-18s name=[%s]" % (
        tag, c.ControlTypeName, (c.ClassName or "")[:18], (c.Name or "")[:22]))


def walk(ctrl, depth=0, out=None, cap=600):
    if out is None:
        out = []
    if depth > 16 or len(out) > cap:
        return out
    try:
        kids = ctrl.GetChildren()
    except Exception:
        return out
    for ch in kids:
        if len(out) > cap:
            break
        try:
            out.append(ch)
        except Exception:
            continue
        walk(ch, depth + 1, out, cap)
    return out


def main():
    fg = auto.GetForegroundControl()
    if not fg:
        print("没有前台控件，退出")
        return
    print("前台窗口: %s [%s]  rect=%s" % (fg.Name, fg.ControlTypeName, fg.BoundingRectangle))
    print()

    # 先把焦点切到页面主体（当前状态）
    dump("初始焦点", auto.GetFocusedControl())

    nodes = walk(fg)
    edits = [n for n in nodes if n.ControlTypeName == "EditControl"]
    docs = [n for n in nodes if n.ControlTypeName == "DocumentControl"]
    print("子树节点数=%d  EditControl=%d  DocumentControl=%d" % (len(nodes), len(edits), len(docs)))
    print()
    for i, e in enumerate(edits[:12]):
        r = e.BoundingRectangle
        print("  edit[%d] rect=(%d,%d) %dx%d name=[%s]" % (
            i, r.left, r.top, r.right - r.left, r.bottom - r.top, (e.Name or "")[:20]))
    print()

    target = None
    if edits:
        # 选「最靠下、面积最大」的那个 —— 大概率是聊天输入框
        def key(e):
            r = e.BoundingRectangle
            return (r.top, -(r.right - r.left) * (r.bottom - r.top))
        target = max(edits, key=key)

    if target:
        print("--- 试验：把焦点切到输入框 ---")
        try:
            target.SetFocus()
        except Exception as ex:
            print("  SetFocus 失败:", ex)
        for i in range(3):
            time.sleep(0.5)
            dump("  切后第%d次采样" % (i + 1), auto.GetFocusedControl())
    else:
        print("!! 没找到 EditControl")

    if docs:
        print()
        print("--- 试验：把焦点切回页面主体 ---")
        try:
            docs[0].SetFocus()
        except Exception as ex:
            print("  SetFocus 失败:", ex)
        for i in range(3):
            time.sleep(0.5)
            dump("  切后第%d次采样" % (i + 1), auto.GetFocusedControl())

    if target:
        print()
        print("--- 再切回输入框（验证可重复）---")
        try:
            target.SetFocus()
        except Exception:
            pass
        time.sleep(0.6)
        dump("  采样", auto.GetFocusedControl())


if __name__ == "__main__":
    main()
