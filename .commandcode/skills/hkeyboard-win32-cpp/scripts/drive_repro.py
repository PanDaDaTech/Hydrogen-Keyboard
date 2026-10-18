# drive_repro.py
# 自驱复现「Chromium 系离开输入框」的完整时序，配 HKeyboard -afdiag 一起跑。
#   1) 把 WorkBuddy 拉到前台
#   2) 点进输入框 3 秒
#   3) 点同窗口空白 6 秒   ← 这里应该稳定收起，不该乱跳
#   4) 再点回输入框 3 秒   ← 应该重新弹出
# 每步同时打印 UIA 焦点元素的 ControlType，作为"真值"参照。
import sys
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


def ctl():
    try:
        c = auto.GetFocusedControl()
        return c.ControlTypeName if c else "<null>"
    except Exception:
        return "ERR"


def stage(tag, seconds):
    print("[%s] 焦点=%s" % (tag, ctl()), flush=True)
    time.sleep(seconds)
    print("[%s] +%.0fs 焦点=%s" % (tag, seconds, ctl()), flush=True)


def hk_rects():
    """HKeyboard 自己的窗口矩形（要排除 —— 它 TOPMOST，会挡住点击落点）"""
    out = []
    for w in auto.GetRootControl().GetChildren():
        try:
            if (w.ClassName or "") == "HKeyboard":
                out.append(w.BoundingRectangle)
        except Exception:
            pass
    return out


def covered(x, y, rects):
    for r in rects:
        if r.left <= x <= r.right and r.top <= y <= r.bottom:
            return True
    return False


def main():
    win = None
    for w in auto.GetRootControl().GetChildren():
        if (w.Name or "") == "WorkBuddy":
            win = w
            break
    if not win:
        print("没找到 WorkBuddy")
        sys.exit(1)

    auto.Click(1200, 60)          # 拉前台
    time.sleep(1.0)
    print("前台 =", (auto.GetForegroundControl().Name or "")[:20], flush=True)

    hks = hk_rects()
    print("HKeyboard 窗口矩形 =", [(r.left, r.top, r.right, r.bottom) for r in hks], flush=True)

    edits = [k for k, _ in walk(win) if k.ControlTypeName == "EditControl"]
    if not edits:
        print("没找到输入框")
        sys.exit(1)
    # 候选：按面积从大到小，且中心不被 HKeyboard 盖住
    cands = []
    for e in edits:
        r = e.BoundingRectangle
        w, h = r.right - r.left, r.bottom - r.top
        cx, cy = (r.left + r.right) // 2, (r.top + r.bottom) // 2
        if w < 200 or h < 40:
            continue
        if covered(cx, cy, hks):
            continue
        cands.append((w * h, cx, cy, w, h))
    cands.sort(reverse=True)
    print("可用输入框候选（面积降序，前 5）：", flush=True)
    for a, cx, cy, w, h in cands[:5]:
        print("   (%d,%d) %dx%d" % (cx, cy, w, h), flush=True)
    if not cands:
        print("没有不被 HKeyboard 遮住的输入框")
        sys.exit(1)
    _, cx, cy, _, _ = cands[0]
    bx, by = cx, cy - 260       # 同窗口内、输入框正上方 260px（避开 HKeyboard 的底部区域）
    print("输入框=(%d,%d)  空白=(%d,%d)" % (cx, cy, bx, by), flush=True)

    print()
    auto.Click(cx, cy)
    stage("点输入框", 3)

    print()
    auto.Click(bx, by)
    stage("点空白", 6)

    print()
    auto.Click(cx, cy)
    stage("再点输入框", 3)
    print("\n完成")


if __name__ == "__main__":
    main()
