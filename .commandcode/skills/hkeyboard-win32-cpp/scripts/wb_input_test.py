# wb_input_test.py
# 只针对 WorkBuddy 自己的聊天输入框做三段对照，配合 HKeyboard -afdiag：
#   1) 点进输入框 → HKeyboard 该弹出
#   2) 点同窗口空白 → 该收起（且不振荡）
#   3) 再点回输入框 → 该弹出
# 每段同时打印 Python 侧的 UIA 真值（焦点元素 ControlType），便于和 C++ 日志对照。
import time

import uiautomation as auto

INPUT_PT = (1522, 1299)      # WorkBuddy 聊天输入框中心（最大化时）
BLANK_PT = (2400, 1299)      # 同窗口、输入框右外侧空白


def ctl(x=None, y=None):
    try:
        c = auto.ControlFromPoint(x, y) if (x is not None) else auto.GetFocusedControl()
        return "%s/%s" % (c.ControlTypeName, (c.ClassName or "")[:16]) if c else "<null>"
    except Exception as e:
        return "ERR"


def stage(tag, pt, seconds):
    print("--- %s：点(%d,%d) ---" % (tag, pt[0], pt[1]), flush=True)
    print("    落点元素 =", ctl(*pt), flush=True)
    auto.Click(*pt)
    for i in range(2):
        time.sleep(seconds / 2.0)
        print("    +%.1fs 焦点 = %s" % ((i + 1) * seconds / 2.0, ctl()), flush=True)


def main():
    print("初始落点元素 =", ctl(*INPUT_PT), flush=True)
    print("初始焦点   =", ctl(), flush=True)
    print()
    stage("① 点输入框", INPUT_PT, 4)
    print()
    stage("② 点空白", BLANK_PT, 6)
    print()
    stage("③ 再点输入框", INPUT_PT, 4)
    print("\n完成")


if __name__ == "__main__":
    main()
