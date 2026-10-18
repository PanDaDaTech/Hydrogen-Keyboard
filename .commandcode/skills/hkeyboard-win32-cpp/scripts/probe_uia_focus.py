# probe_uia_focus.py
# 两段对照：光标「在输入框里」vs「不在输入框里」时，UIA 焦点元素怎么变。
# 目的：给自动隐藏找一条只在 Chromium 上生效的判据，且**不能反过来破坏自动呼出**。
#
# 用法：python probe_uia_focus.py [秒数] [间隔毫秒]
import sys
import time

import uiautomation as auto

P = auto.PropertyId


def gv(ctrl, pid):
    try:
        v = ctrl.GetPropertyValue(pid)
        return v
    except Exception:
        return None


def parents(ctrl, n=4):
    out = []
    try:
        p = ctrl.GetParentControl()
        while p and len(out) < n:
            out.append(p.ControlTypeName)
            p = p.GetParentControl()
    except Exception:
        pass
    return "<".join(out) if out else "-"


def sample():
    t0 = time.time()
    total = getattr(sample, "t0", None)
    if total is None:
        sample.t0 = t0
        total = t0
    el = t0 - total

    try:
        c = auto.GetFocusedControl()
    except Exception as e:
        return "t=%5.1f | <GetFocusedControl 失败: %s>" % (el, e)

    if not c:
        return "t=%5.1f | <null>" % el

    ctl = c.ControlTypeName
    cls = c.ClassName or ""
    aid = c.AutomationId or ""
    nm = (c.Name or "")
    if len(nm) > 14:
        nm = nm[:14]
    try:
        r = c.BoundingRectangle
        rect = "%d,%d %dx%d" % (r.left, r.top, r.right - r.left, r.bottom - r.top)
    except Exception:
        rect = "-"
    fk = gv(c, 30009)      # IsKeyboardFocusable
    hasf = gv(c, 30008)    # HasKeyboardFocus
    txtp = gv(c, 30040)    # IsTextPatternAvailable
    txt2 = gv(c, 30134)    # IsTextPattern2Available
    valp = gv(c, 30043)    # IsValuePatternAvailable

    sel_n = sel_len = "-"
    if txtp:
        try:
            tp = c.GetTextPattern()
            if tp:
                sel = tp.GetSelection()
                sel_n = len(sel)
                if sel:
                    sel_len = len(sel[0].GetText(-1) or "")
        except Exception as e:
            sel_n = "err"

    return ("t=%5.1f | ctl=%-16s cls=%-26s fk=%s hf=%s T=%s T2=%s V=%s sel=%s/%s | name=[%s] rect=%s | up=%s"
            % (el, ctl, cls, fk, hasf, txtp, txt2, valp, sel_n, sel_len, nm, rect, parents(c)))


def main():
    secs = int(sys.argv[1]) if len(sys.argv) > 1 else 30
    ms = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    print("=== UIA 焦点采样 %d 秒（每 %d ms）%s ===" % (secs, ms, time.strftime("%H:%M:%S")))
    print("字段：ctl=控制类型 fk=可键盘聚焦 hf=当前有键盘焦点 T=有TextPattern T2=有TextPattern2")
    print("      V=有ValuePattern sel=选区数/选区文本长 up=父链控制类型")
    print()
    end = time.time() + secs
    while time.time() < end:
        print(sample(), flush=True)
        time.sleep(ms / 1000.0)
    print()
    print("=== 结束 %s ===" % time.strftime("%H:%M:%S"))


if __name__ == "__main__":
    main()
