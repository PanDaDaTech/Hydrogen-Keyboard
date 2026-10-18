# -*- coding: utf-8 -*-
"""自动呼出回归自测：用**受控的 Chromium 窗口**（Edge 打开一个自带 input 的本地页）
驱动「点输入框 / 点空白 / 再点回输入框」，同时监控 HKeyboard 的可见性。

⚠ 需要 HKeyboard 带 `-dginj` 启动（注入的点击才会被它的鼠标钩子接受）。
用法：python drive_aftest.py [settle_seconds]
"""
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys
import threading
import time

import uiautomation as auto

u = ctypes.windll.user32
PAGE = r"file:///E:/Project/Git/Hydrogen-Keyboard/.workbuddy/aftest/aftest.html"
TITLE = "HKeyboardAFTest"

stop = False
last_state = None
events = []


def hk_wnd():
    return u.FindWindowW("HKeyboard", None)


def hk_visible():
    h = hk_wnd()
    return bool(h and u.IsWindowVisible(h))


def hk_rect():
    h = hk_wnd()
    if not h:
        return None
    r = wt.RECT()
    u.GetWindowRect(h, ctypes.byref(r))
    return (r.left, r.top, r.right, r.bottom)


def monitor():
    global last_state
    while not stop:
        v = hk_visible()
        if v != last_state:
            print("  [%s.%03d] HKeyboard visible=%s rect=%s" %
                  (time.strftime("%H:%M:%S"), int(time.time() * 1000) % 1000,
                   v, hk_rect()), flush=True)
            events.append((time.strftime("%H:%M:%S"), v))
            last_state = v
        time.sleep(0.03)


def ct_of(el):
    try:
        return el.ControlTypeName
    except Exception:
        return None


def walk(el, d=0, out=None, cap=500):
    if out is None:
        out = []
    if d > 12 or len(out) > cap:
        return out
    try:
        kids = el.GetChildren()
    except Exception:
        return out
    for k in kids:
        if len(out) > cap:
            break
        out.append(k)
        walk(k, d + 1, out, cap)
    return out


def find_page_window():
    try:
        for w in auto.GetRootControl().GetChildren():
            try:
                if TITLE not in (w.Name or ""):
                    continue
                if w.ClassName != "Chrome_WidgetWin_1":
                    continue
                # ⚠ 必须排除 0×0 / 不可见的：Edge 关掉窗口后进程还在，
                #   会留下标题相同但矩形为 0 的隐藏窗口，匹配到它就没树可走。
                r = w.BoundingRectangle
                if r.right - r.left > 200 and r.bottom - r.top > 200:
                    return w
            except Exception:
                pass
    except Exception:
        pass
    return None


def click(x, y):
    auto.Click(x, y)


def main():
    settle = float(sys.argv[1]) if len(sys.argv) > 1 else 2.5

    print("启动受控 Chromium 窗口 ...", flush=True)
    subprocess.Popen(["cmd", "/c", "start", "", "msedge", "--new-window", PAGE])
    win = None
    for _ in range(60):
        time.sleep(0.5)
        win = find_page_window()
        if win:
            break
    if not win:
        print("没等到测试页窗口")
        sys.exit(1)

    hwnd = win.NativeWindowHandle
    # 这是**我自己起的**窗口：摆到固定位置，方便算点位（不动用户任何窗口）
    u.SetWindowPos(hwnd, -1, 120, 60, 1000, 740, 0x0040)   # HWND_TOPMOST, SWP_SHOWWINDOW
    time.sleep(0.6)
    u.SetWindowPos(hwnd, -2, 120, 60, 1000, 740, 0x0040)   # HWND_NOTOPMOST
    u.SetForegroundWindow(hwnd)
    time.sleep(1.2)
    print("窗口 rect =", win.BoundingRectangle, flush=True)

    nodes = walk(win)
    boxes = []
    for n in nodes:
        if ct_of(n) == "EditControl":
            try:
                b = n.BoundingRectangle
                # ⚠ 必须过滤 0×0 的离屏/隐藏元素：Chromium 会暴露若干个零尺寸
                #   EditControl，按宽度排序时它们排在最前，会把点位算成 (0,0)。
                if b.right - b.left > 40 and b.bottom - b.top > 8:
                    boxes.append(b)
            except Exception:
                pass
    print("页面里的 EditControl 数 =", len(boxes), flush=True)
    for b in boxes:
        print("   rect=(%d,%d %dx%d)" % (b.left, b.top, b.right - b.left, b.bottom - b.top),
              flush=True)
    if not boxes:
        print("没找到输入框")
        sys.exit(1)

    # 宽的是 Edge 地址栏（omnibox），窄的是本页那个 <input>
    boxes.sort(key=lambda b: b.right - b.left)
    page_input = boxes[0]
    omnibox = boxes[-1]

    def center(b):
        return ((b.left + b.right) // 2, (b.top + b.bottom) // 2)

    ix, iy = center(page_input)
    bx, by = ix, page_input.bottom + 220          # 页面空白区（在 #blank 里）
    ox, oy = center(omnibox)
    print("页面输入框中心=(%d,%d)  地址栏中心=(%d,%d)  空白点=(%d,%d)" %
          (ix, iy, ox, oy, bx, by), flush=True)

    th = threading.Thread(target=monitor, daemon=True)
    th.start()

    def step(name, x, y, wait):
        print("\n>>> %s  click(%d,%d)" % (name, x, y), flush=True)
        click(x, y)
        time.sleep(wait)
        print("    after %.1fs -> visible=%s rect=%s" %
              (wait, hk_visible(), hk_rect()), flush=True)

    step("① 点页面输入框", ix, iy, settle)
    step("② 点页面空白", bx, by, settle + 1.5)
    step("③ 点地址栏（用户报的场景）", ox, oy, settle)
    step("④ 点页面空白", bx, by, settle + 1.5)
    step("⑤ 再点回页面输入框", ix, iy, settle)

    print("\n>>> 静置 7 秒观察是否闪烁 ...", flush=True)
    time.sleep(7)
    global stop
    stop = True
    time.sleep(0.2)

    print("\n=== 可见性变化序列 ===")
    for t, v in events:
        print("   %s visible=%s" % (t, v))
    print("总变化次数 =", len(events))

    print("\n关闭测试窗口 ...", flush=True)
    u.PostMessageW(hwnd, 0x0010, 0, 0)   # WM_CLOSE


main()
