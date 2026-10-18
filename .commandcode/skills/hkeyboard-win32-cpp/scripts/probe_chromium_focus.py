# probe_chromium_focus.py
# 用 ctypes 采样「前台窗口的焦点状态」，找一条能区分
# 「焦点在可编辑输入框里」/「焦点不在任何输入框」的信号。
#
# 重点看 getguithreadinfo 的 hwndCaret / flags：
#   Firefox 走的是 Win32 光标（GUI_CARETBLINKING），所以它天然分得清；
#   Chromium 自己画光标，需要实测它到底暴不暴露。
#
# 用法：python probe_chromium_focus.py [秒数] [间隔毫秒]
import ctypes
import ctypes.wintypes as wt
import sys
import time

user32 = ctypes.windll.user32


class GUITHREADINFO(ctypes.Structure):
    _fields_ = [
        ("cbSize", wt.DWORD),
        ("flags", wt.DWORD),
        ("hwndActive", wt.HWND),
        ("hwndFocus", wt.HWND),
        ("hwndCapture", wt.HWND),
        ("hwndMenuOwner", wt.HWND),
        ("hwndMoveSize", wt.HWND),
        ("hwndCaret", wt.HWND),
        ("rcCaret", wt.RECT),
    ]


user32.GetForegroundWindow.restype = wt.HWND
user32.GetWindowThreadProcessId.restype = wt.DWORD
user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
user32.GetGUIThreadInfo.restype = wt.BOOL
user32.GetGUIThreadInfo.argtypes = [wt.DWORD, ctypes.POINTER(GUITHREADINFO)]
user32.GetClassNameW.argtypes = [wt.HWND, ctypes.c_wchar_p, ctypes.c_int]

GUI_CARETBLINKING = 0x00000001
GUI_INMOVESIZE = 0x00000002
GUI_INMENUMODE = 0x00000004
GUI_SYSTEMMENUMODE = 0x00000008
GUI_POPUPMENUMODE = 0x00000010


def cls_name(hwnd):
    if not hwnd:
        return "(null)"
    buf = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(hwnd, buf, 256)
    return buf.value


def flag_names(f):
    out = []
    for bit, nm in ((GUI_CARETBLINKING, "CARETBLINKING"), (GUI_INMOVESIZE, "INMOVESIZE"),
                    (GUI_INMENUMODE, "INMENUMODE"), (GUI_SYSTEMMENUMODE, "SYSTEMMENUMODE"),
                    (GUI_POPUPMENUMODE, "POPUPMENUMODE")):
        if f & bit:
            out.append(nm)
    return "|".join(out) if out else "-"


def sample():
    fg = user32.GetForegroundWindow()
    pid = wt.DWORD(0)
    tid = user32.GetWindowThreadProcessId(fg, ctypes.byref(pid))
    gti = GUITHREADINFO()
    gti.cbSize = ctypes.sizeof(GUITHREADINFO)
    ok = user32.GetGUIThreadInfo(tid, ctypes.byref(gti))

    if not ok:
        return "%s | fg=%s | GI 调用失败" % (time.strftime("%H:%M:%S"), cls_name(fg))

    caret = "none"
    caret_rect = "-"
    if gti.hwndCaret:
        caret = "0x%X" % gti.hwndCaret
        r = gti.rcCaret
        caret_rect = "(%d,%d,%d,%d)" % (r.left, r.top, r.right, r.bottom)

    return ("%s | fg=%s | focusCls=%s | caret=%s %s | rect=%s" % (
        time.strftime("%H:%M:%S"), cls_name(fg), cls_name(gti.hwndFocus),
        caret, flag_names(gti.flags), caret_rect))


def main():
    secs = int(sys.argv[1]) if len(sys.argv) > 1 else 20
    ms = int(sys.argv[2]) if len(sys.argv) > 2 else 400
    print("=== 开始采样 %s（%d 秒，每 %d ms）===" % (time.strftime("%H:%M:%S"), secs, ms))
    print("看两件事：fg/focusCls 变不变、caret 有没有从 none 变成有值")
    print()
    end = time.time() + secs
    while time.time() < end:
        print(sample(), flush=True)
        time.sleep(ms / 1000.0)
    print()
    print("=== 结束 %s ===" % time.strftime("%H:%M:%S"))


if __name__ == "__main__":
    main()
