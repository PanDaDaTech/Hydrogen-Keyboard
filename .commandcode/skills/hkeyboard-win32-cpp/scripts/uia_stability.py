# uia_stability.py
# 测「焦点元素 ControlType」在**没有人操作**时稳不稳。
# 决定：Edit/Document 能不能当判据；若它自己就会跳，就必须先平滑再使用。
#
# 步骤：先把光标点进输入框，然后**全程不动**，每 60ms 采一次，看 ControlType 序列。
import time

import uiautomation as auto


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
    # 显式锁定 WorkBuddy 窗口（不看前台是谁 —— 前台可能被切走了）
    win = None
    try:
        for w in auto.GetRootControl().GetChildren():
            if (w.Name or "") == "WorkBuddy":
                win = w
                break
    except Exception as e:
        print("枚举顶层窗口失败:", e)
    if not win:
        print("没找到 WorkBuddy 窗口，退出")
        return
    print("WorkBuddy 窗口 =", win.ControlTypeName, win.BoundingRectangle)

    nodes = walk(win)
    edits = [n for n in nodes if n.ControlTypeName == "EditControl"]
    print("子树 %d 节点，EditControl %d 个" % (len(nodes), len(edits)))
    if not edits:
        print("没找到输入框，退出")
        return
    edits.sort(key=lambda e: (-(e.BoundingRectangle.right - e.BoundingRectangle.left),
                              -e.BoundingRectangle.top))
    box = edits[0]
    r = box.BoundingRectangle
    cx, cy = (r.left + r.right) // 2, (r.top + r.bottom) // 2
    print("输入框中心 = (%d,%d)，现在点进去，之后全程不动" % (cx, cy))
    auto.Click(cx, cy)
    time.sleep(1.0)

    print()
    print("=== 静置采样 12 秒（每 60ms），看 ControlType 是否稳定 ===")
    seq = []
    t0 = time.time()
    while time.time() - t0 < 12:
        try:
            c = auto.GetFocusedControl()
            name = c.ControlTypeName if c else "<null>"
        except Exception as e:
            name = "ERR"
        seq.append(name)
        time.sleep(0.06)

    # 统计连续段，把"跳变"显式打出来
    runs = []
    for s in seq:
        if runs and runs[-1][0] == s:
            runs[-1][1] += 1
        else:
            runs.append([s, 1])
    print("采样 %d 次，连续段 %d 个：" % (len(seq), len(runs)))
    for s, n in runs:
        print("   %-18s x%-3d  (%.2fs)" % (s, n, n * 0.06))


if __name__ == "__main__":
    main()
