# tray_mode_test.py
# 验证用户实际使用的路径：**托盘启动（-hide）→ 显示 → 点输入框 → 点空白**。
# 关键点：显示不需要真鼠标 —— 直接给主窗口发托盘消息（WM_TRAY + WM_LBUTTONUP）。
import ctypes
import ctypes.wintypes as wt
import time

import uiautomation as auto

u = ctypes.windll.user32
u.FindWindowW.restype = wt.HWND
u.FindWindowW.argtypes = [ctypes.c_wchar_p, ctypes.c_wchar_p]
u.SendMessageW.argtypes = [wt.HWND, ctypes.c_uint, ctypes.c_size_t, ctypes.c_ssize_t]

WM_TRAY = 0x8000 + 100        # WM_APP + 100
WM_LBUTTONUP = 0x0202

INPUT_PT = (1522, 1299)
BLANK_PT = (2400, 1299)


def hk_hwnd():
    return u.FindWindowW("HKeyboard", None)


def ctl():
    try:
        c = auto.GetFocusedControl()
        return c.ControlTypeName if c else "<null>"
    except Exception:
        return "ERR"


def main():
    hwnd = hk_hwnd()
    print("HKeyboard 主窗口 =", hex(hwnd) if hwnd else None, flush=True)
    if not hwnd:
        return

    print("发托盘消息（= 单击托盘图标）→ 应显示", flush=True)
    u.SendMessageW(hwnd, WM_TRAY, 0, WM_LBUTTONUP)
    time.sleep(2.0)

    print("点输入框 (%d,%d)" % INPUT_PT, flush=True)
    auto.Click(*INPUT_PT)
    time.sleep(1.5)
    print("  焦点 =", ctl(), flush=True)
    time.sleep(1.5)

    print("点空白 (%d,%d)" % BLANK_PT, flush=True)
    auto.Click(*BLANK_PT)
    for i in range(3):
        time.sleep(1.5)
        print("  +%.1fs 焦点 = %s" % ((i + 1) * 1.5, ctl()), flush=True)

    print("\n完成")


if __name__ == "__main__":
    main()
