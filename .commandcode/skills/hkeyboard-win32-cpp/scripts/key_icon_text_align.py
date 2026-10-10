# -*- coding: utf-8 -*-
"""量化校验：图标 + 文字 的组合键里，图标与文字是否横向分组正确、纵向同高。"""
import sys, collections
from PIL import Image

IMG = sys.argv[1] if len(sys.argv) > 1 else r"C:\Users\29776\WorkBuddy\2026-09-25-19-32-21\.workbuddy\scratch\v9\shots\01_default.png"
im = Image.open(IMG).convert("RGB"); px = im.load()

u, kax, gap, KH = 106.9565217391304, 32, 7, 86
ROWS = [84, 177, 270, 363, 456]
def rect(col, wu, row):
    xa = kax + round(col * u); xb = kax + round((col + wu) * u)
    return xa, ROWS[row], xb - xa - gap, KH

CASES = [
    ("Row0  Bksp",  rect(14.00, 1.50, 0), "backspace+text"),
    ("Row1  Tab",   rect( 0.00, 1.50, 1), "tab+text"),
    ("Row2  Caps",  rect( 0.00, 2.00, 2), "caps+text"),
    ("Row2  Enter", rect(13.00, 2.50, 2), "enter+text"),
    ("Row3  LShift",rect( 0.00, 2.50, 3), "shift+text"),
    ("Row3  RShift",rect(13.50, 2.00, 3), "shift+text"),
    ("Row4  Menu",  rect( 9.50, 1.00, 4), "hamburger+text"),
    ("Row4  Win",   rect( 2.00, 1.00, 4), "winmark+text"),
]
DIP = 1.75
def modal(x0,y0,w,h):
    c=collections.Counter()
    for yy in range(y0+3,y0+h-3):
        for xx in range(x0+3,x0+w-3): c[px[xx,yy]]+=1
    return c.most_common(1)[0][0]
def isink(x,y,bg,t=70):
    r,g,b=px[x,y]; return abs(r-bg[0])+abs(g-bg[1])+abs(b-bg[2])>t

print("图标尺寸基准：iconInline = 15 DIP = %.1f px ; gap = 4 DIP = %.1f px ; edge/inset 见代码\n" % (15*DIP, 4*DIP))
for name,(x0,y0,w,h),kind in CASES:
    bg = modal(x0,y0,w,h)
    # 列向墨迹计数 -> 分段（图标 blob / 文字 blob）
    colcnt=[sum(1 for yy in range(y0+5,y0+h-5) if isink(xx,yy,bg)) for xx in range(x0+4,x0+w-4)]
    base = x0+4
    segs=[]; s=None
    for i,c in enumerate(colcnt):
        if c>0 and s is None: s=i
        if c==0 and s is not None:
            if i-s>=2: segs.append((s+base,i-1+base))
            s=None
    if s is not None: segs.append((s+base,len(colcnt)-1+base))
    # 合并间隔 < 3px 的段（字符内部空隙）
    merged=[]
    for a,b in segs:
        if merged and a-merged[-1][1] <= 3: merged[-1]=(merged[-1][0],b)
        else: merged.append((a,b))
    def vrange(a,b):
        ys=[yy for yy in range(y0+4,y0+h-4) for xx in range(a,b+1) if isink(xx,yy,bg)]
        return (min(ys),max(ys)) if ys else None
    print("── %s  key=[%d..%d] w=%d 分隔出 %d 段" % (name,x0,x0+w,w,len(merged)))
    for (a,b) in merged:
        vr=vrange(a,b)
        print("     段 x=[%4d,%4d] 宽=%3d  纵向[%4d,%4d] 高=%3d  纵心y=%6.1f  相对键顶=%5.1f px (%5.1f DIP)"
              % (a,b,b-a+1,vr[0],vr[1],vr[1]-vr[0]+1,(vr[0]+vr[1])/2.0,(vr[0]+vr[1])/2.0-y0,((vr[0]+vr[1])/2.0-y0)/DIP))
    if len(merged)>=2:
        a0,b0=merged[0]; a1,b1=merged[1]
        v0=vrange(a0,b0); v1=vrange(a1,b1)
        cy0=(v0[0]+v0[1])/2.0; cy1=(v1[0]+v1[1])/2.0
        print("     → 图标底 right=%d, 文字 left=%d, 两者间距 = %d px (%.1f DIP)【期望 %d px】"
              % (b0,a1,a1-b0-1,(a1-b0-1)/DIP,round(4*DIP)))
        print("     → 纵心差 = %+.1f px  %s" % (cy0-cy1, "OK 同高" if abs(cy0-cy1)<=2 else "!! 高低不齐"))
    print()
