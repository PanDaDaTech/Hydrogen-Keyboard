# uia_stability2.py
# 把 WorkBuddy 拉到前台 → 点进输入框 → **静置 15 秒不碰任何东西**，
# 每 60ms 采一次焦点元素 ControlType，看它是否稳定。
#
# 要回答的问题：用户日志里 50004/50030 成块交替（~0.5s 一块）是
#   (a) Chromium 固有（UIA 焦点上报不稳）→ 判据不可用
#   (b) 我自己那个「隐藏时跳过精判」的循环把它带出来的 → 去掉门就能好
import time

import uiautomation as auto


def walk(c, d=0, out=None, cap=900):
    if out is None:
        out = []
    if d > 18 or len(out) > cap:
        return out
    try:
        kids = c.GetChildren()
    except Exception:
        return out
    for k in kids:
        if len(out) > cap:
            break
        out.append((k, d))
        walk(k, d + 1, out, cap)
    return out


def find_win(name):
    for w in auto.GetRootControl().GetChildren():
        if (w.Name or "") == name:
            return w
    return None


def main():
    win = find_win("WorkBuddy")
    if not win:
        print("没找到 WorkBuddy")
        return

    # 1) 先把 WorkBuddy 拉到前台（点它的标题区，避开 QQ 的矩形）
    auto.Click(1200, 60)
    time.sleep(0.8)
    fg = auto.GetForegroundControl()
    print("前台 =", (fg.Name or "")[:24])

    # 2) 找输入框并点进去
    nodes = walk(win)
    edits = [k for k, _ in nodes if k.ControlTypeName == "EditControl"]
    print("EditControl %d 个" % len(edits))
    if not edits:
        return
    edits.sort(key=lambda e: (-(e.BoundingRectangle.right - e.BoundingRectangle.left),
                              -e.BoundingRectangle.top))
    r = edits[0].BoundingRectangle
    cx, cy = (r.left + r.right) // 2, (r.top + r.bottom) // 2
    print("点输入框中心 (%d,%d)" % (cx, cy))
    auto.Click(cx, cy)
    time.sleep(1.0)
    print("点后焦点 =", auto.GetFocusedControl().ControlTypeName)
    print()

    # 3) 静置采样
    print("=== 静置 15 秒（每 60ms），全程不动 ===")
    seq = []
    t0 = time.time()
    while time.time() - t0 < 15:
        try:
            c = auto.GetFocusedControl()
            seq.append(c.ControlTypeName if c else "<null>")
        except Exception:
            seq.append("ERR")
        time.sleep(0.06)

    runs = []
    for s in seq:
        if runs and runs[-1][0] == s:
            runs[-1][1] += 1
        else:
            runs.append([s, 1])
    print("采样 %d 次 → 连续段 %d 个：" % (len(seq), len(runs)))
    for s, n in runs:
        print("   %-18s x%-3d (%.2fs)" % (s, n, n * 0.06))


if __name__ == "__main__":
    main()
