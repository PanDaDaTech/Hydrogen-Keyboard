# -*- coding: utf-8 -*-
# 实验：验证哪个 API 能读到 NumLock 的「锁定态」
# ⚠ 会切换 NumLock 两次（切完再切回，结束后状态与开始时相同）
import ctypes, time
from ctypes import wintypes
user32 = ctypes.WinDLL('user32', use_last_error=True)
ULONG_PTR = ctypes.c_ulonglong

class MOUSEINPUT(ctypes.Structure):
    _fields_=[("dx",wintypes.LONG),("dy",wintypes.LONG),("mouseData",wintypes.DWORD),
              ("dwFlags",wintypes.DWORD),("time",wintypes.DWORD),("dwExtraInfo",ULONG_PTR)]
class KEYBDINPUT(ctypes.Structure):
    _fields_=[("wVk",wintypes.WORD),("wScan",wintypes.WORD),("dwFlags",wintypes.DWORD),
              ("time",wintypes.DWORD),("dwExtraInfo",ULONG_PTR)]
class HARDWAREINPUT(ctypes.Structure):
    _fields_=[("uMsg",wintypes.DWORD),("wParamL",wintypes.WORD),("wParamH",wintypes.WORD)]
class _U(ctypes.Union):
    _fields_=[("mi",MOUSEINPUT),("ki",KEYBDINPUT),("hi",HARDWAREINPUT)]
class INPUT(ctypes.Structure):
    _fields_=[("type",wintypes.DWORD),("u",_U)]

print("sizeof(INPUT) =", ctypes.sizeof(INPUT), "(x64 应为 40)")
VK_NUMLOCK=0x90
def toggle():
    a=(INPUT*2)()
    for i,f in enumerate((0x0001, 0x0001|0x0002)):
        a[i].type=1; a[i].u.ki.wVk=VK_NUMLOCK; a[i].u.ki.wScan=0
        a[i].u.ki.dwFlags=f; a[i].u.ki.time=0; a[i].u.ki.dwExtraInfo=0
    return user32.SendInput(2, a, ctypes.sizeof(INPUT))

def snap(tag):
    ks  = user32.GetKeyState(VK_NUMLOCK) & 0xFFFF
    ak  = user32.GetAsyncKeyState(VK_NUMLOCK) & 0xFFFF
    ak2 = user32.GetAsyncKeyState(VK_NUMLOCK) & 0xFFFF   # 连读第二次
    print("%-22s GetKeyState=0x%04X(bit0=%d)  GetAsync=0x%04X(bit0=%d)  再读GetAsync=0x%04X(bit0=%d)"
          % (tag, ks, ks&1, ak, ak&1, ak2, ak2&1))

snap("初始")
try:
    for k in (1,2):
        n = toggle()
        time.sleep(0.25)
        snap("第%d次切换后 (SendInput=%d)" % (k, n))
finally:
    print("\n(已切回原状态)")
