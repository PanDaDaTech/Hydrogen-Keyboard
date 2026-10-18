# uia_qq_probe.py
# 在 QQ（Chromium，contenteditable 型输入框）上测：
#   「光标在聊天输入框里」vs「不在」时，UIA 焦点元素与 TextPattern 分别是什么。
# 目的：判断 ControlType==Edit 这条判据在 contenteditable 输入框上是否还成立。
import time

import uiautomation as auto


def describe(tag):
    c = auto.GetFocusedControl()
    if not c:
        print("%-16s <null>" % tag)
        return
    r = c.BoundingRectangle
    tp = "-"
    sel_n = sel_len = "-"
    try:
        if c.GetPropertyValue(30040):        # IsTextPatternAvailable
            t = c.GetTextPattern()
            if t:
                tp = "Y"
                s = t.GetSelection()
                sel_n = len(s)
                if s:
                    sel_len = len(s[0].GetText(-1) or "")
    except Exception:
        tp = "err"
    print("%-16s ctl=%-15s cls=%-14s rect=(%d,%d %dx%d) T=%s sel=%s/%s name=[%s]" % (
        tag, c.ControlTypeName, (c.ClassName or "")[:14],
        r.left, r.top, r.right - r.left, r.bottom - r.top,
        tp, sel_n, sel_len, (c.Name or "")[:14]))


def main():
    win = auto.GetForegroundControl()
    r = win.BoundingRectangle
    print("前台窗口 =", (win.Name or "")[:24], r)
    if r.right - r.left < 100:
        print("窗口大小异常，退出")
        return

    # QQ 聊天输入框大致在窗口底部中间；会话列表在左上
    input_x = r.left + (r.right - r.left) // 2
    input_y = r.bottom - 90
    list_x = r.left + 80
    list_y = r.top + 200

    print("输入框落点 = (%d,%d)   会话列表落点 = (%d,%d)" % (input_x, input_y, list_x, list_y))
    print()
    describe("初始")

    print()
    print("=== 点聊天输入框 ===")
    auto.Click(input_x, input_y)
    for i in range(3):
        time.sleep(0.5)
        describe("  采%d" % (i + 1))

    print()
    print("=== 点会话列表 ===")
    auto.Click(list_x, list_y)
    for i in range(3):
        time.sleep(0.5)
        describe("  采%d" % (i + 1))

    print()
    print("=== 再点回聊天输入框 ===")
    auto.Click(input_x, input_y)
    for i in range(3):
        time.sleep(0.5)
        describe("  采%d" % (i + 1))


if __name__ == "__main__":
    main()
