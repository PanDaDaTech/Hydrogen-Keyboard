# -*- coding: utf-8 -*-
"""Ethereal / PanDaPE-Maker 令牌表发生器。

与 `PanDaPE-Maker` 的 `crates/panda-core/src/color.rs` 用同一套 oklch->sRGB 矩阵，
所以输出的色值与 Rust 工具逐字节一致（可用 `Tokens::with_hue` 的测试互校：
hue=250 浅色 primary 必须是 #53A3F2、deep_text #1A222B；hue=300 浅色 primary 必须是 #AC89E8）。

用法：
    python ethereal_tokens.py            # 默认 hue=300（Rust 工具默认紫）
    python ethereal_tokens.py 250        # Ethereal 原版蓝
    python ethereal_tokens.py 300 --key  # 只输出键帽状态机需要的那几行

输出里的 GDI 列是 COLORREF（BGR，低位是红）——直接可以填进 `ThemeColors`。
"""
import math
import sys

# ---------------------------------------------------------------- oklch -> sRGB
def oklch_to_rgb(l, c, h):
    h = math.radians(h)
    a, b = c * math.cos(h), c * math.sin(h)
    l_ = l + 0.39633778 * a + 0.21580376 * b
    m_ = l - 0.105561346 * a - 0.06385417 * b
    s_ = l - 0.08948418 * a - 1.2914855 * b
    l3, m3, s3 = l_ ** 3, m_ ** 3, s_ ** 3
    r = 4.0767417 * l3 - 3.3077116 * m3 + 0.23096994 * s3
    g = -1.268438 * l3 + 2.6097574 * m3 - 0.3413194 * s3
    bb = -0.0041960863 * l3 - 0.7034186 * m3 + 1.7076147 * s3

    def enc(v):
        v = min(max(v, 0.0), 1.0)
        return round((12.92 * v if v <= 0.0031308 else 1.055 * v ** (1 / 2.4) - 0.055) * 255)

    return (enc(r), enc(g), enc(bb))


def hexs(t):
    return '#%02X%02X%02X' % t


def bgr(t):
    """GDI COLORREF：低位字节是红。"""
    return '0x%02X%02X%02X' % (t[2], t[1], t[0])


def lum(t):
    def f(v):
        v /= 255
        return v / 12.92 if v <= 0.03928 else ((v + 0.055) / 1.055) ** 2.4
    return 0.2126 * f(t[0]) + 0.7152 * f(t[1]) + 0.0722 * f(t[2])


def cr(a, b):
    x, y = lum(a), lum(b)
    hi, lo = max(x, y), min(x, y)
    return (hi + 0.05) / (lo + 0.05)


def mix(fg, bg, a):
    return tuple(round(fg[i] * a + bg[i] * (1 - a)) for i in range(3))


def over(ra, bg):
    return mix(ra[0], bg, ra[1])


# ---------------------------------------------------------------- 令牌定义
# (名称, 浅色 (L,C), 深色 (L,C))，None 表示用固定值另行处理
RAMPS = [
    ('primary',                (0.70, 0.14),  (0.75, 0.14)),
    ('page_bg',                (0.95, 0.01),  (0.16, 0.014)),
    ('btn_regular_bg',         (0.95, 0.025), (0.33, 0.035)),
    ('btn_regular_bg_hover',   (0.90, 0.05),  (0.38, 0.04)),
    ('btn_regular_bg_active',  (0.85, 0.08),  (0.43, 0.045)),
    ('btn_card_bg_hover',      (0.98, 0.005), (0.30, 0.03)),
    ('btn_card_bg_active',     (0.90, 0.03),  (0.35, 0.035)),
    ('btn_plain_bg_hover',     (0.95, 0.025), (0.30, 0.035)),
    ('btn_content',            (0.55, 0.12),  (0.75, 0.10)),
    ('title_active',           (0.60, 0.10),  (0.75, 0.10)),
    ('deep_text',              (0.25, 0.02),  (0.85, 0.02)),
    ('caution',                (0.60, 0.20),  (0.65, 0.20)),   # 注意 h=25 固定
    ('success',                (0.65, 0.15),  (0.72, 0.15)),   # 注意 h=150 固定
]


def build(hue):
    L, D = {}, {}
    for name, (ll, cc), (dl, dc) in RAMPS:
        if name == 'caution':
            L[name], D[name] = oklch_to_rgb(ll, cc, 25.0), oklch_to_rgb(dl, dc, 25.0)
        elif name == 'success':
            L[name], D[name] = oklch_to_rgb(ll, cc, 150.0), oklch_to_rgb(dl, dc, 150.0)
        else:
            L[name], D[name] = oklch_to_rgb(ll, cc, hue), oklch_to_rgb(dl, dc, hue)
    L['card_bg'] = (255, 255, 255)
    D['card_bg'] = oklch_to_rgb(0.23, 0.015, hue)
    # 文字色阶：black/white alpha
    for T, base in ((L, (0, 0, 0)), (D, (255, 255, 255))):
        T['t90'] = (base, 0.90); T['t75'] = (base, 0.75); T['t60'] = (base, 0.60)
        T['t50'] = (base, 0.50); T['t40'] = (base, 0.40)
        T['line_divider'] = (base, 0.08); T['line_color'] = (base, 0.10); T['meta_divider'] = (base, 0.20)
    L['primary_fg'] = ((255, 255, 255), 1.00)
    D['primary_fg'] = ((0, 0, 0), 0.70)
    # 键盘专用派生（Ethereal 与 Rust 工具里都没有）
    # on_primary 刻意偏离 Rust 的 primary_fg：Rust 浅色用 white，只有 2.80:1，
    # 那是给 26px 大标题用的；键盘 Enter 是 14px 小字，改用 deep_text（5.72:1）。
    # rust_on_primary 保留原始值，纯粹为了在输出里对照偏离幅度。
    for T in (L, D):
        T['key_outline'] = mix(T['deep_text'], T['card_bg'], 0.20)
        T['key_outline_hover'] = mix(T['primary'], T['card_bg'], 0.45)
        T['rust_on_primary'] = over(T['primary_fg'], T['primary'])
    L['on_primary'] = L['deep_text']
    D['on_primary'] = over(D['primary_fg'], D['primary'])
    return L, D


ORDER = ['primary', 'page_bg', 'card_bg', 'btn_regular_bg', 'btn_regular_bg_hover',
         'btn_regular_bg_active', 'btn_card_bg_hover', 'btn_card_bg_active',
         'btn_plain_bg_hover', 'btn_content', 'title_active', 'deep_text',
         'key_outline', 'key_outline_hover', 'on_primary', 'caution', 'success']


def main():
    hue = float(sys.argv[1]) if len(sys.argv) > 1 and not sys.argv[1].startswith('-') else 300.0
    key_only = '--key' in sys.argv
    L, D = build(hue)
    print('=' * 78)
    print('  Ethereal 令牌表   hue = %.0f   （Rust 工具默认 300，Ethereal 原版 250）' % hue)
    print('=' * 78)
    print('%-24s%-12s%-12s%-12s%-12s' % ('token', '浅色 HEX', '浅色 GDI', '深色 HEX', '深色 GDI'))
    for k in ORDER:
        print('%-24s%-12s%-12s%-12s%-12s' % (k, hexs(L[k]), bgr(L[k]), hexs(D[k]), bgr(D[k])))

    print('\n文字色阶（Rgba，绘制时按 alpha 混到键帽底上）')
    for k in ('t90', 't75', 't60', 't50', 't40', 'line_divider', 'line_color', 'meta_divider'):
        print('  %-14s 浅 %s alpha %.2f      深 %s alpha %.2f'
              % (k, hexs(L[k][0]), L[k][1], hexs(D[k][0]), D[k][1]))

    print('\n--- 对比度校验（键盘场景）---')
    tests = [
        ('普通键字 deep_text / card_bg', L['deep_text'], L['card_bg'], D['deep_text'], D['card_bg']),
        ('次要字 t60 / card_bg', over(L['t60'], L['card_bg']), L['card_bg'],
         over(D['t60'], D['card_bg']), D['card_bg']),
        ('键帽轮廓 key_outline / card_bg', L['key_outline'], L['card_bg'], D['key_outline'], D['card_bg']),
        ('悬停轮廓 key_outline_hover / card_bg', L['key_outline_hover'], L['card_bg'],
         D['key_outline_hover'], D['card_bg']),
        ('修饰键字 btn_content / btn_regular_bg', L['btn_content'], L['btn_regular_bg'],
         D['btn_content'], D['btn_regular_bg']),
        ('按下字 deep_text / btn_regular_bg_active', L['deep_text'], L['btn_regular_bg_active'],
         D['deep_text'], D['btn_regular_bg_active']),
        ('Enter 字 on_primary / primary  [键盘配方]', L['on_primary'], L['primary'], D['on_primary'], D['primary']),
        ('  对照 Rust primary_fg / primary', L['rust_on_primary'], L['primary'],
         D['rust_on_primary'], D['primary']),
        ('  反例 btn_content / btn_regular_bg_active', L['btn_content'], L['btn_regular_bg_active'],
         D['btn_content'], D['btn_regular_bg_active']),
        ('  反例 主色15%轮廓 / page_bg', mix(L['primary'], L['card_bg'], 0.15), L['page_bg'],
         mix(D['primary'], D['card_bg'], 0.15), D['page_bg']),
    ]
    for n, a, b, c, d in tests:
        print('  %-42s 浅 %5.2f:1   深 %5.2f:1' % (n, cr(a, b), cr(c, d)))

    if not key_only:
        print('\n--- 面板底 vs 键帽底（Ethereal 原值，刻意不压暗）---')
        print('  浅 %.2f:1   深 %.2f:1   -> 层次靠 key_outline 补，不靠底色差'
              % (cr(L['page_bg'], L['card_bg']), cr(D['page_bg'], D['card_bg'])))
    print()


if __name__ == '__main__':
    main()
