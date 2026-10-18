# uia_click_test.py
# 自驱实验（真实点击）：在同一个 Chromium 窗口里，
#   点输入框 → 采样；点窗口空白 → 采样；再点回输入框 → 采样。
# 验证 `GetFocusedElement().ControlType` 能否当「焦点在可编辑输入框里」的判据。
#
# ⚠ 会移动鼠标并产生真实点击（这就是它有效的原因 —— UIA 的 SetFocus 对 Chromium 无效）。
import time

import uiautomation as auto


def dump(tag):
    c = auto.GetFocusedControl()
    if not c:
        print("%-22s -> <null>" % tag)
        return
    r = c.BoundingRectangle
    print("%-22s -> ctl=%-16s cls=%-20s rect=(%d,%d %dx%d)" % (
        tag, c.ControlTypeName, (c.ClassName or "")[:20],
        r.left, r.top, r.right - r.left, r.bottom - r.top))


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
        out.append(ch)
        walk(ch, depth + 1, out, cap)
    return out


def main():
    fg = auto.GetForegroundControl()
    nodes = walk(fg)
    edits = [n for n in nodes if n.ControlTypeName == "EditControl"]
    if not edits:
        print("没找到 EditControl，退出")
        return
    # 最大且最靠下的那个 = 聊天输入框
    edits.sort(key=lambda e: (-(e.BoundingRectangle.right - e.BoundingRectangle.left),
                              -e.BoundingRectangle.top))
    box = edits[0]
    r = box.BoundingRectangle
    cx, cy = (r.left + r.right) // 2, (r.top + r.bottom) // 2
    print("输入框 rect=(%d,%d) %dx%d  中心=(%d,%d)" % (
        r.left, r.top, r.right - r.left, r.bottom - r.top, cx, cy))
    # 输入框右侧的空白处（同一窗口内，尽量避开按钮）
    bx = min(r.right + 180, fg.BoundingRectangle.right - 40)
    by = cy
    print("空白落点 = (%d,%d)" % (bx, by))
    print()

    print("=== 阶段1：点输入框 ===")
    auto.Click(cx, cy)
    for i in range(3):
        time.sleep(0.45)
        dump("  采%d" % (i + 1))

    print()
    print("=== 阶段2：点同窗口空白处 ===")
    auto.Click(bx, by)
    for i in range(4):
        time.sleep(0.45)
        dump("  采%d" % (i + 1))

    print()
    print("=== 阶段3：再点回输入框 ===")
    auto.Click(cx, cy)
    for i in range(3):
        time.sleep(0.45)
        dump("  采%d" % (i + 1))


if __name__ == "__main__":
    main()
