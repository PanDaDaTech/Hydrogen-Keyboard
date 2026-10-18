# probe_chromium_focus2.py
# 两段对照：光标「在输入框里」vs「不在输入框里」时，前台 Chromium 窗口暴露出哪些可区分的信号。
#
# 采集项：
#   1. 前台窗口类 / GI 焦点控件类 / Win32 光标 / GI flags
#   2. 子窗口结构（找渲染宿主 Chrome_RenderWidgetHostHWND）
#   3. IME 上下文（imm32）：Chromium 只在可编辑元素获得焦点时才需要 IME，
#      所以 HIMC 是否存在、composition window 是否被设置，可能是一条干净判据
#
# 用法：python probe_chromium_focus2.py [秒数] [“阶段标记”]
import ctypes
import ctypes.wintypes as wt
import sys
import time

user32 = ctypes.windll.user32
imm32 = ctypes.windll.imm32

user32.GetForegroundWindow.restype = wt.HWND
user32.GetWindowThreadProcessId.restype = wt.DWORD
user32.GetWindowThreadProcessId.argtypes = [wt.HWND, ctypes.POINTER(wt.DWORD)]
user32.GetClassNameW.argtypes = [wt.HWND, ctypes.c_wchar_p, ctypes.c_int]
user32.EnumChildWindows.restype = wt.BOOL
user32.EnumChildWindows.argtypes = [wt.HWND, ctypes.c_void_p, wt.LPARAM]

imm32.ImmGetContext.restype = wt.HANDLE
imm32.ImmGetContext.argtypes = [wt.HWND]
imm32.ImmReleaseContext.argtypes = [wt.HWND, wt.HANDLE]


class GUITHREADINFO(ctypes.Structure):
    _fields_ = [
        ("cbSize", wt.DWORD), ("flags", wt.DWORD),
        ("hwndActive", wt.HWND), ("hwndFocus", wt.HWND), ("hwndCapture", wt.HWND),
        ("hwndMenuOwner", wt.HWND), ("hwndMoveSize", wt.HWND), ("hwndCaret", wt.HWND),
        ("rcCaret", wt.RECT),
    ]


class COMPOSITIONFORM(ctypes.Structure):
    _fields_ = [("dwStyle", wt.DWORD), ("ptCurrentPos", wt.POINT), ("rcArea", wt.RECT)]


user32.GetGUIThreadInfo.restype = wt.BOOL
user32.GetGUIThreadInfo.argtypes = [wt.DWORD, ctypes.POINTER(GUITHREADINFO)]
imm32.ImmGetCompositionWindow.restype = wt.BOOL
imm32.ImmGetCompositionWindow.argtypes = [wt.HANDLE, ctypes.POINTER(COMPOSITIONFORM)]

CFS_DEFAULT = 0x0000
CFS_POINT = 0x0002


def cls_name(h):
    if not h:
        return "(null)"
    b = ctypes.create_unicode_buffer(256)
    user32.GetClassNameW(h, b, 256)
    return b.value


def children(hwnd, max_n=6):
    out = []
    CB = ctypes.WINFUNCTYPE(wt.BOOL, wt.HWND, wt.LPARAM)

    def cb(h, l):
        if len(out) < max_n:
            out.append(cls_name(h))
        return True

    user32.EnumChildWindows(hwnd, CB(cb), 0)
    return out


def sample():
    fg = user32.GetForegroundWindow()
    pid = wt.DWORD(0)
    tid = user32.GetWindowThreadProcessId(fg, ctypes.byref(pid))
    gti = GUITHREADINFO()
    gti.cbSize = ctypes.sizeof(GUITHREADINFO)
    ok = user32.GetGUIThreadInfo(tid, ctypes.byref(gti))

    focus_cls = cls_name(gti.hwndFocus) if ok else "?"
    caret = ("0x%X" % gti.hwndCaret) if (ok and gti.hwndCaret) else "none"
    flags = gti.flags if ok else -1

    kids = children(fg)

    # ---- IME ----
    himc = imm32.ImmGetContext(fg)
    ime = "none"
    if himc:
        cf = COMPOSITIONFORM()
        got = imm32.ImmGetCompositionWindow(himc, ctypes.byref(cf))
        if got:
            style = ("POINT" if cf.dwStyle == CFS_POINT else
                     ("DEFAULT" if cf.dwStyle == CFS_DEFAULT else "0x%X" % cf.dwStyle))
            ime = "HIMC/%s pt=(%d,%d)" % (style, cf.ptCurrentPos.x, cf.ptCurrentPos.y)
        else:
            ime = "HIMC/nocomp"
        imm32.ImmReleaseContext(fg, himc)

    return ("%s | t=%s | fg=%s | focus=%s caret=%s flags=0x%X | kids=%s | ime=%s" % (
        time.strftime("%H:%M:%S"), time.strftime("%S"), cls_name(fg),
        focus_cls, caret, flags, ",".join(kids) if kids else "-", ime))


def main():
    secs = int(sys.argv[1]) if len(sys.argv) > 1 else 24
    ms = int(sys.argv[2]) if len(sys.argv) > 2 else 500
    print("=== 采样 %d 秒（每 %d ms）%s ===" % (secs, ms, time.strftime("%H:%M:%S")))
    print()
    end = time.time() + secs
    while time.time() < end:
        print(sample(), flush=True)
        time.sleep(ms / 1000.0)
    print()
    print("=== 结束 %s ===" % time.strftime("%H:%M:%S"))


if __name__ == "__main__":
    main()
