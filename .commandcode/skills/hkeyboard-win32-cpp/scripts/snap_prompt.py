# -*- coding: utf-8 -*-
"""打开关闭对话框并用 PrintWindow 抓图（Python 版，绕开 PowerShell 的输出黑洞）。
用法：python snap_prompt.py <exe> <outdir>
原理：
  1. 启动 exe（-noauto 阻止自动呼出抢焦点）
  2. 按 进程ID + 窗口类名 'HKeyboard' 找主窗口
  3. PostMessage(WM_CLOSE) 触发 HandleCloseAction -> OpenClosePrompt
  4. 按类名 'HKeyboardClosePrompt' 找对话框
  5. PrintWindow(h, dc, PW_RENDERFULLCONTENT=2) 抓图
⚠ flag 必须带 2：自绘窗口不实现 WM_PRINTCLIENT，flag=0 只会画出任务栏。
"""
import ctypes
import os
import subprocess
import sys
import time

from ctypes import wintypes

u = ctypes.windll.user32
g = ctypes.windll.gdi32

# ⚠ 必须让本进程也 DPI-aware，否则 GetWindowRect 返回的是「虚拟化坐标」
#   （= 物理像素 / 缩放比）。本机 168 DPI = 1.75x，840x460 的窗口会被报成 480x263，
#   PrintWindow 就会把整幅画面裁成左上角一小块。必须在任何窗口操作之前调用。
try:
    ctypes.windll.shcore.SetProcessDpiAwareness(2)   # PROCESS_PER_MONITOR_DPI_AWARE
except Exception:
    u.SetProcessDPIAware()

EnumWindows = u.EnumWindows
EnumWindowsProc = ctypes.WINFUNCTYPE(ctypes.c_bool, wintypes.HWND, wintypes.LPARAM)
GetWindowThreadProcessId = u.GetWindowThreadProcessId
GetClassNameW = u.GetClassNameW
IsWindowVisible = u.IsWindowVisible
GetWindowRect = u.GetWindowRect
GetClientRect = u.GetClientRect
GetDpiForWindow = u.GetDpiForWindow
PostMessageW = u.PostMessageW
ShowWindow = u.ShowWindow
PrintWindow = u.PrintWindow

WM_CLOSE = 0x0110
SC_CLOSE = 0xF060
WM_LBUTTONDOWN = 0x0201
WM_LBUTTONUP = 0x0202
SW_SHOW = 5
PW_RENDERFULLCONTENT = 2


def find_window(pid, cls):
    found = []

    def cb(h, _):
        p = wintypes.DWORD()
        GetWindowThreadProcessId(h, ctypes.byref(p))
        if p.value == pid and IsWindowVisible(h):
            buf = ctypes.create_unicode_buffer(64)
            GetClassNameW(h, buf, 64)
            if buf.value == cls:
                found.append(h)
                return False
        return True

    EnumWindows(EnumWindowsProc(cb), 0)
    return found[0] if found else 0


def snap(h, path):
    r = wintypes.RECT()
    GetWindowRect(h, ctypes.byref(r))
    w, ht = r.right - r.left, r.bottom - r.top
    if w <= 0 or ht <= 0:
        return "BAD SIZE %dx%d" % (w, ht)
    hdc = u.GetWindowDC(h)
    mdc = g.CreateCompatibleDC(hdc)
    bmp = g.CreateCompatibleBitmap(hdc, w, ht)
    g.SelectObject(mdc, bmp)
    ok = PrintWindow(h, mdc, PW_RENDERFULLCONTENT)

    class BITMAPINFOHEADER(ctypes.Structure):
        _fields_ = [("biSize", wintypes.DWORD), ("biWidth", ctypes.c_long),
                    ("biHeight", ctypes.c_long), ("biPlanes", wintypes.WORD),
                    ("biBitCount", wintypes.WORD), ("biCompression", wintypes.DWORD),
                    ("biSizeImage", wintypes.DWORD),
                    ("biXPelsPerMeter", ctypes.c_long),
                    ("biYPelsPerMeter", ctypes.c_long),
                    ("biClrUsed", wintypes.DWORD), ("biClrImportant", wintypes.DWORD)]

    bi = BITMAPINFOHEADER()
    bi.biSize = ctypes.sizeof(BITMAPINFOHEADER)
    bi.biWidth, bi.biHeight = w, -ht     # 负值 = 自上而下
    bi.biPlanes, bi.biBitCount = 1, 32
    bi.biCompression = 0
    buf = ctypes.create_string_buffer(w * ht * 4)
    g.GetDIBits(mdc, bmp, 0, ht, buf, ctypes.byref(bi), 0)

    g.DeleteObject(bmp)
    g.DeleteDC(mdc)
    u.ReleaseDC(h, hdc)

    import numpy as np
    from PIL import Image
    a = np.frombuffer(buf, dtype=np.uint8).reshape(ht, w, 4)
    Image.fromarray(a[:, :, :3]).save(path)
    return "%dx%d dpi=%d ok=%s -> %s" % (w, ht, GetDpiForWindow(h), bool(ok), path)


def list_windows(pid):
    """诊断用：列出该进程所有顶层窗口（含不可见的）。"""
    rows = []

    def cb(h, _):
        q = wintypes.DWORD()
        GetWindowThreadProcessId(h, ctypes.byref(q))
        if q.value == pid:
            b = ctypes.create_unicode_buffer(64)
            GetClassNameW(h, b, 64)
            r = wintypes.RECT()
            GetWindowRect(h, ctypes.byref(r))
            rows.append((h, IsWindowVisible(h), b.value,
                         (r.right - r.left, r.bottom - r.top)))
        return True

    EnumWindows(EnumWindowsProc(cb), 0)
    return rows


def main():
    exe, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    subprocess.run(["taskkill", "/F", "/IM", "HKeyboard_x64.exe"],
                   capture_output=True)
    time.sleep(0.6)
    p = subprocess.Popen([exe, "-noauto"])
    try:
        h_main = 0
        for _ in range(30):
            time.sleep(0.4)
            h_main = find_window(p.pid, "HKeyboard")
            if h_main:
                break
        print("main hwnd =", h_main)
        if not h_main:
            return
        # ⚠ 必须等主窗口完全就绪（托盘图标、配置加载、淡入动画）再触发。
        time.sleep(2.0)
        #⚠ lParam 是**客户区**坐标，不是屏幕坐标 —— 之前用GetWindowRect
        #   的中心算 x，窗口在屏幕右侧时完全偏出按钮范围，点击全部落空。
        _rc = wintypes.RECT()
        GetClientRect(h_main, ctypes.byref(_rc))
        cx_btn = _rc.right          # 客户区宽度（WS_POPUP 下即窗口宽）
        h_dlg = 0
        for dx in (-24, -28, -30, -32, -36):
            # ⚠⚠ 唯一可靠的触发方式：**模拟点击标题栏的 ✕ 按钮**。
            #   WM_CLOSE / PostMessage / SendMessage 三种方式全部时灵时不灵
            #   （实测同一天里 art12 用 PostMessage 第 10 轮才弹、重试 15 轮又全不弹），
            #   而 PostMessage(WM_LBUTTONDOWN/UP) 打到 ✕ 上**一次就中**。
            #   坐标取「窗口宽 - 24, y=32」，✕ 是 28 DIP 见方、靠右。
            lparam = (32 << 16) | ((cx_btn + dx) & 0xFFFF)
            PostMessageW(h_main, WM_LBUTTONDOWN, 1, lparam)
            PostMessageW(h_main, WM_LBUTTONUP, 0, lparam)
            for _ in range(4):
                time.sleep(0.4)
                h_dlg = find_window(p.pid, "HKeyboardClosePrompt")
                if h_dlg:
                    break
            print("  try dx=%d: dlg=%s" % (dx, h_dlg))
            if h_dlg:
                break
        if not h_dlg:
            print("DIALOG NOT FOUND, windows =", list_windows(p.pid))
            return
        print("dialog hwnd =", h_dlg)
        # 抓图前再等 0.6s，让淡入动画（StartWindowFade）彻底结束，避免拍到半透明帧
        time.sleep(0.6)
        print(snap(h_dlg, os.path.join(out, "14_prompt.png")))
    finally:
        subprocess.run(["taskkill", "/F", "/IM", "HKeyboard_x64.exe"],
                       capture_output=True)


if __name__ == "__main__":
    main()
