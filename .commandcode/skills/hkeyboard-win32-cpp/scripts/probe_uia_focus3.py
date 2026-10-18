# probe_uia_focus3.py
# 干净版两段对照：全程只在同一个应用窗口内切换「光标在输入框里 / 不在输入框里」，
# 采集 UIA 焦点元素的可区分信号。
#
# 关键假设（待验证）：TextPattern 的选区是**退化区间（长度 0）** ⇒ 存在文本光标（caret）
#   ⇒ 焦点在可编辑输入框里；而「没有输入框」时选区要么不存在、要么是非退化的。
#   若成立，就得到一条与「整张网页永远持有 Document 焦点」无关的判据 ——
#   因为它问的不是"哪个元素有焦点"，而是"焦点这棵树里有没有活动的文本光标"。
#
# 用法：python probe_uia_focus3.py [秒数] [间隔毫秒]
import sys
import time

import uiautomation as auto

user32 = auto.windll.user32 if hasattr(auto, "windll") else None


def gv(ctrl, pid):
    try:
        return ctrl.GetPropertyValue(pid)
    except Exception:
        return None


def short(s, n=16):
    s = s or ""
    return s[:n]


def fg_info():
    """前台窗口类名 + 标题（用于确认采样确实落在同一个应用上）"""
    try:
        import ctypes
        import ctypes.wintypes as wt
        u = ctypes.windll.user32
        u.GetForegroundWindow.restype = wt.HWND
        u.GetClassNameW.argtypes = [wt.HWND, ctypes.c_wchar_p, ctypes.c_int]
        h = u.GetForegroundWindow()
        b = ctypes.create_unicode_buffer(256)
        u.GetClassNameW(h, b, 256)
        cls = b.value
        tbuf = ctypes.create_unicode_buffer(256)
        u.GetWindowTextW(h, tbuf, 256)
        return cls, tbuf.value
    except Exception as e:
        return "?", str(e)


def sample():
    if not hasattr(sample, "t0"):
        sample.t0 = time.time()
    el = time.time() - sample.t0

    fg_cls, fg_title = fg_info()
    try:
        c = auto.GetFocusedControl()
    except Exception as e:
        return "t=%5.1f | fg=%s[%s] | <失败 %s>" % (el, fg_cls, short(fg_title, 12), e)
    if not c:
        return "t=%5.1f | fg=%s[%s] | <null>" % (el, fg_cls, short(fg_title, 12))

    ctl = c.ControlTypeName
    cls = c.ClassName or ""
    txtp = gv(c, 30040)          # IsTextPatternAvailable
    valp = gv(c, 30043)          # IsValuePatternAvailable

    # ---- 核心信号：选区是否退化（长度 0 = 有光标）----
    sel_n = sel_len = "-"
    if txtp:
        try:
            tp = c.GetTextPattern()
            if tp:
                sel = tp.GetSelection()
                sel_n = len(sel)
                if sel:
                    sel_len = len(sel[0].GetText(-1) or "")
        except Exception:
            sel_n = "err"

    # ---- 光标处的元素（另一条候选判据）----
    pt_cls = pt_ctl = "-"
    try:
        import ctypes
        import ctypes.wintypes as wt
        u = ctypes.windll.user32
        class POINT(ctypes.Structure):
            _fields_ = [("x", wt.LONG), ("y", wt.LONG)]
        p = POINT()
        u.GetCursorPos(ctypes.byref(p))
        e2 = auto.ControlFromPoint(p.x, p.y)
        if e2:
            pt_ctl = e2.ControlTypeName
            pt_cls = e2.ClassName or ""
    except Exception:
        pt_ctl = "err"

    return ("t=%5.1f | fg=%-22s[%s] | ctl=%-15s cls=%-26s T=%s V=%s SEL=%s/%s | 光标处=%s/%s"
            % (el, fg_cls, short(fg_title, 14), ctl, short(cls, 26), txtp, valp,
               sel_n, sel_len, pt_ctl, short(pt_cls, 24)))


def main():
    secs = int(sys.argv[1]) if len(sys.argv) > 1 else 36
    ms = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    print("=== UIA 三段对照采样 %d 秒（每 %d ms）%s ===" % (secs, ms, time.strftime("%H:%M:%S")))
    print("SEL=选区数/选中文本长度  ← 长度 0 表示「这是一个文本光标」")
    print()
    end = time.time() + secs
    while time.time() < end:
        print(sample(), flush=True)
        time.sleep(ms / 1000.0)
    print()
    print("=== 结束 %s ===" % time.strftime("%H:%M:%S"))


if __name__ == "__main__":
    main()
