"""离线复算 HKeyboard 的键面统一字号档位（不需要编译器 / 不用跑程序）。

用途：用户报「字有些大有些小」时，**先算出各布局下的统一档位**，
再决定改分组边界还是改窗口尺寸 —— 别靠肉眼估、别靠试。

原理：复刻 `FitUnifiedTier` 的判据
    · 分组 = {block 0 或 1} ∩ {|k->w - self->w| <= 2}
    · groupMaxW = 组内最小键宽 + delta,  delta = selfMaxW - self->w = -6*dpi
    · 档位 = 组内所有纯文字键各自「第一个量得下的档」的最严值
    · 档位表 PTS 与源码 MainLadder() 一一对应

关键：**统一档 = 组内最严者的档位，然后全组都用这一档**。
不要按「每个键各自的档位」判「是否同档」（那样永远判成否）。

用法：
    python unified_font_tier.py                # 默认扫一组窗口宽度
    python unified_font_tier.py 1280 1600      # 指定窗口宽度（DIP）
"""
import math
import os
import sys

from PIL import ImageFont

FONT = r"E:/Project/Git/Hydrogen-Keyboard/winres/MiSans-Medium.ttf"
PTS = [14, 13, 12, 10, 9, 8, 7, 6]          # == MainLadder() 的 8 档

# 源码 FullPut 的几何：KEY_AREA_X=12 DIP，22 个 1u 列，跨块补偿 2 个 gap，gap=6 DIP
KEY_AREA_X = 12.0
TOTAL_U = 22.0
BLOCK_GAPS = 2
GAP = 6.0
EDGE = 6.0                                   # DrawKeys 传 maxW 时扣的边距（DIP）

# 主区 + 导航区的 1u 纯文字标签（这是**合并后**的分组；Esc/Tab/Shift 等带图标的键不参与）
GROUP_1U = (
    list("qwertyuiop") + list("asdfghjkl") + list("zxcvbnm") +
    list("1234567890") +
    ["PrtSc", "ScrLk", "Pause", "Ins", "Home", "PgUp", "Del", "End", "PgDn"]
)

_cache = {}


def em(pt, fs=1.0):
    """复刻源码 MakeFont 的 em 换算。"""
    return int(round(round(pt * fs)) * 10 * 96 / 720)


def _font(pt, fs=1.0):
    key = (pt, round(fs, 4))
    if key not in _cache:
        _cache[key] = ImageFont.truetype(FONT, em(pt, fs))
    return _cache[key]


def text_w(label, pt, fs=1.0):
    return _font(pt, fs).getlength(label)


def tier_for(label, avail, fs=1.0):
    """该标签所需的最小档位序号（越小字越大），全放不下则地板档。"""
    for idx, pt in enumerate(PTS):
        if text_w(label, pt, fs) <= avail:
            return idx
    return len(PTS) - 1


def u_width(win_w, dpi=1.0):
    """1u 键宽（像素）。复刻 FullPut: floor(c1*u+0.5)-floor(c0*u+0.5)-gap。"""
    u = (win_w - 2 * KEY_AREA_X - BLOCK_GAPS * GAP) / TOTAL_U
    return math.floor(1 * u + 0.5) - math.floor(0 * u + 0.5) - GAP, u


def unified(win_w, dpi=1.0, fs=1.0):
    kw, u = u_width(win_w, dpi)
    group_w = kw                                # 组内最小键宽（1u 组里没有更窄的）
    group_max_w = group_w - EDGE * dpi          # delta = -EDGE*dpi
    worst, detail = 0, {}
    for s in GROUP_1U:
        t = tier_for(s, group_max_w, fs)
        detail[s] = t
        worst = max(worst, t)
    return kw, u, group_max_w, worst, PTS[worst], detail


def main(argv):
    wins = [int(a) for a in argv[1:]] or [870, 1280, 1600, 1920, 2240]
    print(f"字体: {FONT}")
    print(f"档位表: {PTS}  (== MainLadder)")
    print("=" * 78)
    for w in wins:
        kw, u, avail, t, pt, detail = unified(w)
        strict = max(detail.items(), key=lambda kv: kv[1])[0]
        # 该档下是否全部放得下（自检：worst 档应当让所有标签都放得下）
        over = [s for s in GROUP_1U if text_w(s, pt) > avail]
        print(f"\n窗口宽 {w:>5} DIP:  1u键宽={kw}px (u={u:.2f})  可用宽={avail:.0f}px")
        print(f"  ⇒ 统一档 = {t} → {pt}pt   （最严标签: {strict}）")
        print(f"     该档下超宽标签: {over if over else '无（全部放得下）'}")


if __name__ == "__main__":
    if not os.path.exists(FONT):
        sys.exit(f"找不到字体: {FONT}")
    main(sys.argv)
