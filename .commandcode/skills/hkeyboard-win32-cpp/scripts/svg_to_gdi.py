#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
把 PanDaPE-Maker 的图标几何（crates/panda-app/src/icons.rs 里的 inline SVG）
编译成 HKeyboard 可以直接喂给 GDI+ GraphicsPath 的顶点数组。

为什么需要它
------------
HKeyboard 目前的图标来自图标字体：Segoe MDL2 Assets / Segoe Fluent Icons 的
私有区码位（\\xE7C9 之类），Win10 以下靠内嵌的 6.6KB 子集字体 segmdl2.ttf 兜底。
两个问题：

  1. 内嵌字体是**子集**，只有当前用到的 9 个图标。想加一个新图标就得重新跑
     子集化工具、重新嵌资源 —— 而键盘上想加一个图标是很常见的需求。
  2. 图标来自系统字体，XP / PE / 精简系统的渲染结果不完全一致；字体缺失时
     会退化成方框。

改成矢量自绘后：图标与 Rust 制作工具逐字节同源（同一个 24 单位网格、同一套
几何），不再依赖任何字体，任意色相着色，任意尺寸，XP 与 Win11 一致。

输出
----
  <out>.h     顶点数组 + 图标索引表
  <out>.html  原始 SVG 与转换后路径的并排对照页，用于肉眼验收

用法
----
  python svg_to_gdi.py                       # 默认输出到当前目录
  python svg_to_gdi.py --icons <icons.rs> --outdir <dir> --html
"""

import argparse
import math
import os
import re
import sys

# --------------------------------------------------------------------------
# 1. 从 icons.rs 提取每个 glyph 的 SVG body
# --------------------------------------------------------------------------

def read_icons_rs(path):
    """返回 {GlyphName: svg_body}，body 里保留 {FG} / {HOLE} 颜色槽。

    两种输入都吃：
      * icons.rs       —— 有 `fn art(self) -> Art` 标记，只取该函数体内的 Glyph 分支；
      * 补充图标文件   —— 没有标记，就整文件扫 `Glyph::Name => "..."`。
    后者用于放键盘专用、制作工具里没有的图标。
    """
    src = open(path, encoding='utf-8').read()
    i = src.find('fn art(self) -> Art')
    if i < 0:
        block = src                      # 补充图标文件：整文件解析
    else:
        # 边界要落在「函数体里那个构造返回值的 Art {」上：
        # 函数签名本身就以 "Art {" 结尾，直接 index('Art {', i) 会得到一个空块。
        m = re.search(r'\n\s*Art \{\s*\n\s*grid:', src[i:])
        if not m:
            raise SystemExit('icons.rs 里找不到构造返回值的 Art { grid: ... }，文件结构变了？')
        block = src[i:i + m.start()]

    out = {}
    for m in re.finditer(r'Glyph::(\w+)\s*=>\s*"', block):
        name = m.group(1)
        k = m.end()
        buf = []
        while k < len(block):
            c = block[k]
            if c == '\\':                      # Rust 转义
                nxt = block[k + 1]
                buf.append({'n': '\n', 't': '\t', '"': '"', '\\': '\\'}.get(nxt, nxt))
                k += 2
                continue
            if c == '"':
                break
            buf.append(c)
            k += 1
        out[name] = ''.join(buf)
    return out


# --------------------------------------------------------------------------
# 2. 迷你 SVG 解析：只覆盖 icons.rs 实际用到的那部分语法
# --------------------------------------------------------------------------

NUM = re.compile(r'[-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?')

# 仿射矩阵 (a,b,c,d,e,f)：x' = a*x + c*y + e ; y' = b*x + d*y + f
IDENT = (1.0, 0.0, 0.0, 1.0, 0.0, 0.0)


def mat_mul(m, n):
    a1, b1, c1, d1, e1, f1 = m
    a2, b2, c2, d2, e2, f2 = n
    return (a1 * a2 + c1 * b2, b1 * a2 + d1 * b2,
            a1 * c2 + c1 * d2, b1 * c2 + d1 * d2,
            a1 * e2 + c1 * f2 + e1, b1 * e2 + d1 * f2 + f1)


def mat_apply(m, x, y):
    a, b, c, d, e, f = m
    return (a * x + c * y + e, b * x + d * y + f)


def parse_transform(s):
    m = IDENT
    for name, args in re.findall(r'(\w+)\s*\(([^)]*)\)', s or ''):
        v = [float(x) for x in NUM.findall(args)]
        if not v:
            continue
        if name == 'translate':
            t = (1, 0, 0, 1, v[0], v[1] if len(v) > 1 else 0.0)
        elif name == 'scale':
            sx = v[0]
            sy = v[1] if len(v) > 1 else v[0]
            t = (sx, 0, 0, sy, 0, 0)
        elif name == 'rotate':
            a = math.radians(v[0])
            cos, sin = math.cos(a), math.sin(a)
            t = (cos, sin, -sin, cos, 0, 0)
            if len(v) >= 3:                    # rotate(a cx cy)
                t = mat_mul(mat_mul((1, 0, 0, 1, v[1], v[2]), t),
                            (1, 0, 0, 1, -v[1], -v[2]))
        elif name == 'matrix':
            t = tuple(v[:6])
        else:
            continue
        m = mat_mul(m, t)
    return m


def parse_attrs(tag):
    return {k: v for k, v in re.findall(r'([\w:-]+)\s*=\s*"([^"]*)"', tag)}


# --- 椭圆弧 -> 三次贝塞尔（W3C SVG 附录 F.6） ------------------------------

def arc_to_cubics(x1, y1, rx, ry, phi_deg, large_arc, sweep, x2, y2):
    if x1 == x2 and y1 == y2:
        return []
    rx, ry = abs(rx), abs(ry)
    if rx < 1e-12 or ry < 1e-12:
        return [('C', x1, y1, x2, y2, x2, y2)]

    phi = math.radians(phi_deg)
    cos_p, sin_p = math.cos(phi), math.sin(phi)

    dx2, dy2 = (x1 - x2) / 2.0, (y1 - y2) / 2.0
    x1p = cos_p * dx2 + sin_p * dy2
    y1p = -sin_p * dx2 + cos_p * dy2

    lam = (x1p * x1p) / (rx * rx) + (y1p * y1p) / (ry * ry)
    if lam > 1.0:
        s = math.sqrt(lam)
        rx *= s
        ry *= s

    num = rx * rx * ry * ry - rx * rx * y1p * y1p - ry * ry * x1p * x1p
    den = rx * rx * y1p * y1p + ry * ry * x1p * x1p
    if den < 1e-20:
        return [('C', x1, y1, x2, y2, x2, y2)]
    coef = math.sqrt(max(0.0, num / den))
    if large_arc == sweep:
        coef = -coef

    cxp = coef * rx * y1p / ry
    cyp = -coef * ry * x1p / rx

    cx = cos_p * cxp - sin_p * cyp + (x1 + x2) / 2.0
    cy = sin_p * cxp + cos_p * cyp + (y1 + y2) / 2.0

    def angle(ux, uy, vx, vy):
        dot = ux * vx + uy * vy
        n = math.hypot(ux, uy) * math.hypot(vx, vy)
        if n < 1e-20:
            return 0.0
        a = math.acos(max(-1.0, min(1.0, dot / n)))
        return -a if (ux * vy - uy * vx) < 0 else a

    theta1 = angle(1, 0, (x1p - cxp) / rx, (y1p - cyp) / ry)
    dtheta = angle((x1p - cxp) / rx, (y1p - cyp) / ry,
                   (-x1p - cxp) / rx, (-y1p - cyp) / ry)
    if not sweep and dtheta > 0:
        dtheta -= 2 * math.pi
    elif sweep and dtheta < 0:
        dtheta += 2 * math.pi

    segs = max(1, int(math.ceil(abs(dtheta) / (math.pi / 2.0))))
    delta = dtheta / segs
    t = (4.0 / 3.0) * math.tan(delta / 4.0)

    out = []
    th = theta1
    px = x1
    py = y1
    for _ in range(segs):
        th2 = th + delta
        cos1, sin1 = math.cos(th), math.sin(th)
        cos2, sin2 = math.cos(th2), math.sin(th2)

        e1x = cos_p * rx * cos1 - sin_p * ry * sin1 + cx
        e1y = sin_p * rx * cos1 + cos_p * ry * sin1 + cy
        e2x = cos_p * rx * cos2 - sin_p * ry * sin2 + cx
        e2y = sin_p * rx * cos2 + cos_p * ry * sin2 + cy

        d1x = -cos_p * rx * sin1 - sin_p * ry * cos1
        d1y = -sin_p * rx * sin1 + cos_p * ry * cos1
        d2x = -cos_p * rx * sin2 - sin_p * ry * cos2
        d2y = -sin_p * rx * sin2 + cos_p * ry * cos2

        c1x, c1y = e1x + t * d1x, e1y + t * d1y
        c2x, c2y = e2x - t * d2x, e2y - t * d2y

        out.append(('C', c1x, c1y, c2x, c2y, e2x, e2y))
        th = th2
        px, py = e2x, e2y

    return out


# --- d 属性 -> 绝对 M / L / C / Z 序列 ------------------------------------

ARITY = {'M': 2, 'L': 2, 'H': 1, 'V': 1, 'C': 6, 'S': 4, 'Q': 4, 'T': 2, 'A': 7, 'Z': 0}


def tokenize_path(d):
    items = []
    for cmd, num in re.findall(r'([MmLlHhVvCcSsQqTtAaZz])|([-+]?(?:\d*\.\d+|\d+\.?)(?:[eE][-+]?\d+)?)', d):
        items.append(cmd if cmd else float(num))
    return items


def parse_path(d):
    """返回 [('M'|'L'|'C'|'Z', pts...), ...]，全部是绝对坐标。"""
    items = tokenize_path(d)
    out = []
    i = 0
    cmd = None
    cur = (0.0, 0.0)
    sub = (0.0, 0.0)
    prev_c2 = None
    prev_q = None

    while i < len(items):
        if isinstance(items[i], str):
            cmd = items[i]
            i += 1
            if cmd in 'Zz':
                out.append(('Z',))
                cur = sub
                prev_c2 = prev_q = None
                continue

        if cmd is None:
            i += 1
            continue

        rel = cmd.islower()
        up = cmd.upper()
        need = ARITY[up]

        if need and i + need > len(items):
            break
        if need and any(isinstance(x, str) for x in items[i:i + need]):
            # 下一个命令提前出现（参数不足），交给外层处理
            continue

        args = items[i:i + need] if need else []
        i += need

        if up == 'M':
            x, y = args
            if rel:
                x, y = cur[0] + x, cur[1] + y
            out.append(('M', x, y))
            cur = sub = (x, y)
            cmd = 'l' if rel else 'L'
            prev_c2 = prev_q = None

        elif up == 'L':
            x, y = args
            if rel:
                x, y = cur[0] + x, cur[1] + y
            out.append(('L', x, y))
            cur = (x, y)
            prev_c2 = prev_q = None

        elif up == 'H':
            x = args[0] + (cur[0] if rel else 0.0)
            out.append(('L', x, cur[1]))
            cur = (x, cur[1])
            prev_c2 = prev_q = None

        elif up == 'V':
            y = args[0] + (cur[1] if rel else 0.0)
            out.append(('L', cur[0], y))
            cur = (cur[0], y)
            prev_c2 = prev_q = None

        elif up == 'C':
            c1x, c1y, c2x, c2y, x, y = args
            if rel:
                c1x, c1y = cur[0] + c1x, cur[1] + c1y
                c2x, c2y = cur[0] + c2x, cur[1] + c2y
                x, y = cur[0] + x, cur[1] + y
            out.append(('C', c1x, c1y, c2x, c2y, x, y))
            prev_c2 = (c2x, c2y)
            prev_q = None
            cur = (x, y)

        elif up == 'S':
            c2x, c2y, x, y = args
            if rel:
                c2x, c2y = cur[0] + c2x, cur[1] + c2y
                x, y = cur[0] + x, cur[1] + y
            c1x, c1y = (2 * cur[0] - prev_c2[0], 2 * cur[1] - prev_c2[1]) if prev_c2 else cur
            out.append(('C', c1x, c1y, c2x, c2y, x, y))
            prev_c2 = (c2x, c2y)
            prev_q = None
            cur = (x, y)

        elif up == 'Q':
            qx, qy, x, y = args
            if rel:
                qx, qy = cur[0] + qx, cur[1] + qy
                x, y = cur[0] + x, cur[1] + y
            c1x, c1y = cur[0] + 2 / 3.0 * (qx - cur[0]), cur[1] + 2 / 3.0 * (qy - cur[1])
            c2x, c2y = x + 2 / 3.0 * (qx - x), y + 2 / 3.0 * (qy - y)
            out.append(('C', c1x, c1y, c2x, c2y, x, y))
            prev_q = (qx, qy)
            prev_c2 = None
            cur = (x, y)

        elif up == 'T':
            x, y = args
            if rel:
                x, y = cur[0] + x, cur[1] + y
            qx, qy = (2 * cur[0] - prev_q[0], 2 * cur[1] - prev_q[1]) if prev_q else cur
            c1x, c1y = cur[0] + 2 / 3.0 * (qx - cur[0]), cur[1] + 2 / 3.0 * (qy - cur[1])
            c2x, c2y = x + 2 / 3.0 * (qx - x), y + 2 / 3.0 * (qy - y)
            out.append(('C', c1x, c1y, c2x, c2y, x, y))
            prev_q = (qx, qy)
            prev_c2 = None
            cur = (x, y)

        elif up == 'A':
            rx, ry, rot, laf, sf, x, y = args
            if rel:
                x, y = cur[0] + x, cur[1] + y
            out.extend(arc_to_cubics(cur[0], cur[1], rx, ry, rot,
                                     int(laf) != 0, int(sf) != 0, x, y))
            prev_c2 = prev_q = None
            cur = (x, y)

    return out


def circle_path(cx, cy, rx, ry):
    """圆/椭圆 -> 4 段三次贝塞尔。"""
    k = 0.5522847498307936
    ox, oy = rx * k, ry * k
    return [
        ('M', cx + rx, cy),
        ('C', cx + rx, cy + oy, cx + ox, cy + ry, cx, cy + ry),
        ('C', cx - ox, cy + ry, cx - rx, cy + oy, cx - rx, cy),
        ('C', cx - rx, cy - oy, cx - ox, cy - ry, cx, cy - ry),
        ('C', cx + ox, cy - ry, cx + rx, cy - oy, cx + rx, cy),
        ('Z',),
    ]


def rect_path(x, y, w, h, rx, ry):
    if rx <= 0 and ry <= 0:
        return [('M', x, y), ('L', x + w, y), ('L', x + w, y + h),
                ('L', x, y + h), ('Z',)]
    rx = min(rx, w / 2.0)
    ry = min(ry, h / 2.0)
    return [
        ('M', x + rx, y),
        ('L', x + w - rx, y),
        ('C', x + w - rx + rx * 0.5523, y, x + w, y + ry - ry * 0.5523, x + w, y + ry),
        ('L', x + w, y + h - ry),
        ('C', x + w, y + h - ry + ry * 0.5523, x + w - rx + rx * 0.5523, y + h, x + w - rx, y + h),
        ('L', x + rx, y + h),
        ('C', x + rx - rx * 0.5523, y + h, x, y + h - ry + ry * 0.5523, x, y + h - ry),
        ('L', x, y + ry),
        ('C', x, y + ry - ry * 0.5523, x + rx - rx * 0.5523, y, x + rx, y),
        ('Z',),
    ]


COLOR_SLOT = {'{FG}': 0, '{HOLE}': 1}


def parse_svg(body):
    """
    把一段 SVG body 拆成绘制批次：
      [ (paint, stroke_width, color_slot, [ (op, pts), ... ]), ... ]
    paint: 0 = 描边, 1 = 填充
    """
    batches = []
    stack = [IDENT]

    # 把 <g ...> 与 </g> 和自闭合元素一起按顺序扫出来
    for m in re.finditer(r'<g\b([^>]*)>|</g>|<(path|circle|ellipse|rect)\b([^>]*)/?>', body):
        if m.group(0).startswith('</g'):
            if len(stack) > 1:
                stack.pop()
            continue

        if m.group(0).startswith('<g'):
            stack.append(mat_mul(stack[-1], parse_transform(parse_attrs(m.group(0)).get('transform', ''))))
            continue

        tag = m.group(2)
        attr = parse_attrs(m.group(0))
        mat = mat_mul(stack[-1], parse_transform(attr.get('transform', '')))

        fill = (attr.get('fill') or '').strip()
        stroke = (attr.get('stroke') or '').strip()
        width = float(attr.get('stroke-width') or 1.9)

        if tag == 'path':
            segs = parse_path(attr.get('d', ''))
        elif tag == 'circle':
            cx, cy = float(attr.get('cx', 0)), float(attr.get('cy', 0))
            r = float(attr.get('r', 0))
            segs = circle_path(cx, cy, r, r)
        elif tag == 'ellipse':
            cx, cy = float(attr.get('cx', 0)), float(attr.get('cy', 0))
            rx, ry = float(attr.get('rx', 0)), float(attr.get('ry', 0))
            segs = circle_path(cx, cy, rx, ry)
        elif tag == 'rect':
            x, y = float(attr.get('x', 0)), float(attr.get('y', 0))
            w, h = float(attr.get('width', 0)), float(attr.get('height', 0))
            # SVG 规则：只写 rx 时 ry 跟随 rx（反之亦然）。icons.rs 里的 <rect> 大多
            # 只写 rx，早期版本把 ry 当 0 处理，圆角实际被算成了直角。
            a_rx, a_ry = attr.get('rx'), attr.get('ry')
            if a_rx is None and a_ry is None:
                rx = ry = 0.0
            elif a_ry is None:
                rx = ry = float(a_rx)
            elif a_rx is None:
                rx = ry = float(a_ry)
            else:
                rx, ry = float(a_rx), float(a_ry)
            segs = rect_path(x, y, w, h, rx, ry)
        else:
            continue

        # 一个元素可能同时有 fill 和 stroke（icons.rs 里没有，但留个口子）
        if fill and fill != 'none':
            batches.append(apply_mat(mat, segs, 1, 0.0, COLOR_SLOT.get(fill, 0)))
        if stroke and stroke != 'none':
            batches.append(apply_mat(mat, segs, 0, width, COLOR_SLOT.get(stroke, 0)))

    return batches


def apply_mat(m, segs, paint, width, slot):
    out = []
    for seg in segs:
        if seg[0] == 'Z':
            out.append(('Z',))
        elif seg[0] == 'M':
            out.append(('M',) + mat_apply(m, seg[1], seg[2]))
        elif seg[0] == 'L':
            out.append(('L',) + mat_apply(m, seg[1], seg[2]))
        elif seg[0] == 'C':
            x1, y1 = mat_apply(m, seg[1], seg[2])
            x2, y2 = mat_apply(m, seg[3], seg[4])
            x3, y3 = mat_apply(m, seg[5], seg[6])
            out.append(('C', x1, y1, x2, y2, x3, y3))
    return (paint, width, slot, out)


# --------------------------------------------------------------------------
# 3. 输出 C++ 与对照 HTML
# --------------------------------------------------------------------------

OP_END, OP_BEGIN, OP_MOVE, OP_LINE, OP_CUBIC, OP_CLOSE = range(6)
OPNAME = {OP_END: 'HKIC_END', OP_BEGIN: 'HKIC_BEGIN', OP_MOVE: 'HKIC_MOVE',
          OP_LINE: 'HKIC_LINE', OP_CUBIC: 'HKIC_CUBIC', OP_CLOSE: 'HKIC_CLOSE'}


def emit_nodes(batches):
    """一个图标的全部批次压成一条节点流。"""
    nodes = []
    for paint, width, slot, segs in batches:
        nodes.append((OP_BEGIN, paint, width, slot, 0.0, 0.0, 0.0))
        for seg in segs:
            if seg[0] == 'Z':
                nodes.append((OP_CLOSE, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0))
            elif seg[0] == 'M':
                nodes.append((OP_MOVE, seg[1], seg[2], 0.0, 0.0, 0.0, 0.0))
            elif seg[0] == 'L':
                nodes.append((OP_LINE, seg[1], seg[2], 0.0, 0.0, 0.0, 0.0))
            elif seg[0] == 'C':
                nodes.append((OP_CUBIC, seg[1], seg[2], seg[3], seg[4], seg[5], seg[6]))
    nodes.append((OP_END, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0))
    return nodes


def fnum(v):
    # ⚠ C++ 浮点字面量必须带小数点或指数：把 "0.0"/"12.0" 压成 "0"/"12" 再拼 'f'
    #   会得到 0f / 12f —— 非法字面量（MSVC: invalid literal suffix 'f'）。
    s = ('%.3f' % v).rstrip('0').rstrip('.')
    if s in ('', '-0', '-'):
        s = '0'
    if '.' not in s:
        s += '.0'          # 始终保证是合法浮点字面量
    return s


def write_header(path, glyphs, order, grid_of, generated_by):
    lines = []
    lines.append('// 由 %s 自动生成，请勿手工编辑。' % generated_by)
    lines.append('//')
    lines.append('// 图标几何与 PanDaPE-Maker 的 crates/panda-app/src/icons.rs 同源，')
    lines.append('// 变换后喂给 GDI+ GraphicsPath 绘制。单位是 24×24 网格（两个窗口按钮是')
    lines.append('// 12×12），运行时按 size/grid 缩放。')
    lines.append('')
    lines.append('#pragma once')
    lines.append('#include <windows.h>')
    lines.append('')
    lines.append('enum HkIconOp {')
    for op in range(6):
        lines.append('    %s = %d,' % (OPNAME[op], op))
    lines.append('};')
    lines.append('')
    lines.append('enum HkIconPaint {')
    lines.append('    HKIC_STROKE = 0,   // 用 Pen 描边（圆头圆角，对应 SVG 的 round/round）')
    lines.append('    HKIC_FILL   = 1,   // 用 Brush 填充')
    lines.append('};')
    lines.append('')
    lines.append('struct HkIconNode {')
    lines.append('    unsigned char op;')
    lines.append('    float a, b, c, d, e, f;')
    lines.append('};')
    lines.append('// BEGIN : a=笔画类型(HkIconPaint) b=描边宽度 c=颜色槽(0=FG 1=HOLE)')
    lines.append('// MOVE  : a,b = 终点')
    lines.append('// LINE  : a,b = 终点')
    lines.append('// CUBIC : a,b = 控制点1  c,d = 控制点2  e,f = 终点')
    lines.append('// CLOSE : 闭合当前子路径')
    lines.append('')
    lines.append('struct HkIconDef {')
    lines.append('    const HkIconNode* nodes;')
    lines.append('    float grid;   // 几何所在的网格边长，运行时缩放系数 = size / grid')
    lines.append('};')
    lines.append('')

    anon = '_anon_%d' % abs(hash(path)) if False else None
    for name in order:
        nodes = glyphs[name]
        lines.append('static const HkIconNode k_icon_%s[] = {' % name)
        for (op, a, b, c, d, e, f) in nodes:
            lines.append('    { %s, %sf, %sf, %sf, %sf, %sf, %sf },'
                         % (OPNAME[op], fnum(a), fnum(b), fnum(c),
                            fnum(d), fnum(e), fnum(f)))
        lines.append('};')
        lines.append('')

    lines.append('// 图标索引：地址由调用方按名字取，这里只提供一张可遍历的表。')
    lines.append('static const HkIconDef k_hkIcons[] = {')
    for name in order:
        lines.append('    { k_icon_%s, %sf },' % (name, fnum(grid_of[name])))
    lines.append('};')
    lines.append('static const int k_hkIconCount = %d;' % len(order))
    lines.append('')
    lines.append('// 名字 -> 下标，避免调用方依赖数组顺序。')
    lines.append('enum HkIconId {')
    for i, name in enumerate(order):
        lines.append('    HKICON_%-16s = %d,' % (name.upper(), i))
    lines.append('};')
    lines.append('')
    # 守卫：C++ 浮点字面量必须带小数点或指数。
    # "0f" / "12f" 是非法的（MSVC: C3688 invalid literal suffix 'f'），
    # 这条检查就是为了把这类问题挡在推 CI 之前。
    for ln in lines:
        if re.search(r'(?<![\w.])\d+f\b', ln):
            raise SystemExit('生成结果含非法浮点字面量（缺少小数点）：%s' % ln)
    return '\n'.join(lines)


def render_svg_replay(batches, grid):
    """把转换后的几何再写成一段 SVG，用来和原图并排比对。"""
    out = ['<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %g %g">' % (grid, grid)]
    for paint, width, slot, segs in batches:
        col = '#111' if slot == 0 else '#e0e0e0'
        d = []
        for seg in segs:
            if seg[0] == 'Z':
                d.append('Z')
            elif seg[0] == 'M':
                d.append('M%s %s' % (fnum(seg[1]), fnum(seg[2])))
            elif seg[0] == 'L':
                d.append('L%s %s' % (fnum(seg[1]), fnum(seg[2])))
            elif seg[0] == 'C':
                d.append('C%s %s %s %s %s %s' % tuple(fnum(v) for v in seg[1:]))
        attr = ('fill="%s"' % col) if paint == 1 else \
               ('fill="none" stroke="%s" stroke-width="%s"' % (col, fnum(width)))
        out.append('<path %s d="%s" stroke-linecap="round" stroke-linejoin="round"/>' % (attr, ' '.join(d)))
    out.append('</svg>')
    return ''.join(out)


def write_html(path, originals, replay, order, grid_of):
    rows = []
    for name in order:
        g = grid_of[name]
        rows.append(
            '<figure><figcaption>%s</figcaption>'
            '<div class="pair">'
            '<div class="cell"><span class="tag">原始 SVG</span>'
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 %g %g" width="96" height="96">%s</svg></div>'
            '<div class="cell"><span class="tag">转换后路径</span>%s</div>'
            '</div></figure>' % (name, g, g, originals[name].replace('{FG}', '#111').replace('{HOLE}', '#e0e0e0'),
                                 replay[name].replace('viewBox="0 0 %g %g"' % (g, g),
                                                       'viewBox="0 0 %g %g" width="96" height="96"' % (g, g)))
        )
    html = """<!DOCTYPE html><html lang="zh"><head><meta charset="utf-8"><title>图标转换对照</title>
<style>
 body{background:#fff;color:#111;font:13px/1.5 system-ui,sans-serif;padding:24px;margin:0}
 h1{font-size:18px;margin:0 0 4px} p.sub{color:#666;margin:0 0 20px}
 .grid{display:flex;flex-wrap:wrap;gap:16px}
 figure{margin:0;border:1px solid #e3e3e3;border-radius:12px;padding:12px;width:236px}
 figcaption{font-weight:600;margin-bottom:8px}
 .pair{display:flex;gap:8px}
 .cell{flex:1;display:flex;flex-direction:column;align-items:center;background:#fafafa;border-radius:8px;padding:8px 4px}
 .tag{font-size:11px;color:#888;margin-bottom:6px}
</style></head><body>
<h1>icons.rs → GDI+ 路径 转换对照</h1>
<p class="sub">左为 Rust 制作工具里的原始 SVG，右为解析后的几何重新写出。两侧一致即转换保真。</p>
<div class="grid">%s</div>
</body></html>""" % ''.join(rows)
    open(path, 'w', encoding='utf-8').write(html)


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    default_icons = ('C:/Users/29776/WorkBuddy/2026-09-12-16-13-00/_lc/crates/panda-app/src/icons.rs'
                     ',' + os.path.join(here, 'hk_icons_extra.rs'))
    ap = argparse.ArgumentParser()
    ap.add_argument('--icons', default=default_icons,
                    help='icons.rs 路径，可逗号分隔多个；后面的文件覆盖同名 glyph')
    ap.add_argument('--outdir', default=here)
    ap.add_argument('--only', default='', help='只处理这些 glyph（逗号分隔），默认全部')
    ap.add_argument('--exclude', default='Panda',
                    help='排除的 glyph（逗号分隔）。默认排除 Panda —— 那是 PanDaPE-Maker 的'
                         '品牌标记，HKeyboard 用 winres/main.ico，不要任何熊猫图形。')
    ap.add_argument('--html', action='store_true', help='同时输出对照页')
    args = ap.parse_args()

    drop = {s.strip() for s in args.exclude.split(',') if s.strip()}

    bodies = {}
    for p in args.icons.split(','):
        p = p.strip()
        if not p:
            continue
        if not os.path.exists(p):
            print('  [warn] 找不到 %s，跳过' % p, file=sys.stderr)
            continue
        got = read_icons_rs(p)
        print('  [read] %s -> %d 个 glyph' % (os.path.basename(p), len(got)))
        bodies.update(got)

    # 两个窗口按钮画在 12 网格上
    small = {'Minimize', 'Close'}

    order = [n for n in bodies
             if n not in drop and (not args.only or n in args.only.split(','))]
    order.sort()
    if drop:
        print('  [drop] 排除：%s' % ', '.join(sorted(drop & set(bodies))))

    compiled, replay, orig, grids = {}, {}, {}, {}
    for name in order:
        grid = 12.0 if name in small else 24.0
        batches = parse_svg(bodies[name])
        if not batches:
            print('  [skip] %s 没有可解析的几何' % name, file=sys.stderr)
            continue
        compiled[name] = emit_nodes(batches)
        replay[name] = render_svg_replay(batches, grid)
        orig[name] = bodies[name]
        grids[name] = grid

    order = [n for n in order if n in compiled]
    hdr = os.path.join(args.outdir, 'hk_icons_generated.h')
    open(hdr, 'w', encoding='utf-8').write(
        write_header(hdr, compiled, order, grids, 'svg_to_gdi.py'))
    print('写出 %s（%d 个图标，%d 个节点）' % (hdr, len(order),
          sum(len(compiled[n]) for n in order)))

    for name in order:
        n = len(compiled[name])
        b = sum(1 for x in compiled[name] if x[0] == OP_BEGIN)
        print('  %-14s grid=%-4g 节点=%-4d 批次=%d' % (name, grids[name], n, b))

    if args.html:
        p = os.path.join(args.outdir, 'hk_icons_check.html')
        write_html(p, orig, replay, order, grids)
        print('写出对照页 %s' % p)


if __name__ == '__main__':
    main()
