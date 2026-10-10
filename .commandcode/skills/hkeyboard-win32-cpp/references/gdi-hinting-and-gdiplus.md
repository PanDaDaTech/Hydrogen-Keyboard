# GDI 网格拟合（hinting）与 GDI+ 换渲染器

**当用户说「字体还是怪怪的」「该粗的地方不粗，细的地方反而有点粗」「文字发虚」
「字重不统一」「标题立不起来」「字号调了没用」时读这份。**

这是 `references/font-single-weight.md` 的**续篇**。那份解决了「假粗体」，
这份解决「去粗体之后，为什么层级还是塌的」。

---

## 一、一句话根因

**GDI 的 TrueType 光栅器会把笔画 snap 到整数像素**（grid fitting / hinting）。
结果：**字号连续变化，笔画粗细却不连续**。

实测「十」字的竖画宽度（alpha 加权，亚像素精度，本机 175% DPI）：

| 字号 DIP | em_px | GDI 竖宽 | **GDI 竖宽/em** | GDI+ 竖宽 | **GDI+ 竖宽/em** |
|---:|---:|---:|---:|---:|---:|
| 10.5 | 25 | 2.76 | 0.1106 | 2.40 | 0.0960 |
| 12 | 28 | 3.29 | **0.1175** | 2.62 | 0.0936 |
| 13 | 30 | 3.48 | 0.1161 | 2.84 | 0.0946 |
| 14 | 33 | 3.00 | **0.0909** ↓骤降 22% | 2.84 | 0.0860 |
| 16 | 37 | 4.10 | 0.1108 ↑回弹 | 3.36 | 0.0909 |
| 18 | 42 | 4.00 | 0.0952 | 3.88 | 0.0924 |
| 20 | 47 | 4.76 | 0.1014 | 4.42 | 0.0940 |

- **GDI 列是锯齿状的**：12 比 13 粗、14 又比 13 细 22%。
- **GDI+ 列波动只有 ±5%，单调平滑。**

### 两条推论，正好对应那句抱怨

**「该粗的地方不粗」**：字号 10.5→20 增长 **90%**，GDI 的竖宽只增长 **72%**（2.76→4.76），
GDI+ 增长 **84%**（2.40→4.42）。大字号被"压缩"了，所以标题不够重。

**「细的地方反而粗」**：12pt 的 3.29px 只比 13pt 的 3.48px 细 5.5%，
而字号差 8% —— **小字号的相对笔画被撑大**。

---

## 二、先用探针拿数据，别靠眼睛

```bash
PY="C:/Users/29776/.workbuddy/binaries/python/envs/default/Scripts/python.exe"
"$PY" scripts/stroke_probe.py "E:/Project/Git/Hydrogen-Keyboard/winres/MiSans-Medium.ttf"
```

输出 GDI 与 GDI+ 各字号的竖宽/em 对照表，以及线性度、层级方向两项判定。
**任何「字体看起来不对」的争论，先跑这个脚本。**

### 探针的三个关键实现细节（自己写也照这个来）

1. **量笔画要用 alpha 加权，不能用阈值计数。**
   `sum(pixel)/255` 才能得到 2.76 这样的亚像素真值。
   用 `>128` 计数只能得到整数 3，**GDI+ 的抗锯齿笔画会被系统性低估**，
   从而得出「GDI+ 更细」的错误结论。
2. **`PatBlt` 的 ROP 别写反**：`BLACKNESS = 0x00000042`，`WHITENESS = 0x00FF0062`。
   写反的典型症状是「整个 DIB 判成全白 / 墨迹 100%」。
3. **每次只加载一个字体、独立进程跑**。
   `RemoveFontResourceExW` 之后 GDI 仍会缓存字体映射，
   同进程连续测多个同族字体（`MiSans-Regular`/`Medium`/`Bold` 族名都是 `MiSans`）
   **会串味**，测出来的 Regular 可能比 Medium 还粗。

---

## 三、从字体侧关掉 hinting：全线无效，别再试

已实测四组变体（用 fontTools 改表后重新测量）：

| 变体 | `gasp` 值 | 结果 |
|---|---|---|
| 原样 | `{8:15, 16:15, 65535:15}` | 基准 |
| 仅灰度 AA | `{65535:2}` | **与原样完全一致** |
| 无 AA 无 gridfit | `{65535:0}` | 笔画变细（其实是变成无 AA 的 DRAFT 效果），**不是我们要的** |
| 删除 `gasp` 表 | — | **与原样完全一致** |
| 删除 `prep` | `{8:15,…}` | **与原样完全一致** |

**结论**：GDI 对没有 TrueType 指令的字体执行的是**自己的 auto-hint**，
只受 `CreateFontW` 的 `lfQuality` 控制，**不受 `gasp` 影响**。
（本项目内嵌字体缺 `fpgm` / `cvt ` 表，属"无指令"字体，全部走 auto-hint。）

⇒ **想在字体侧解决，是死路。必须换渲染器。**

---

## 四、方案：GDI+ `TextRenderingHintAntiAlias`

项目已 `#include <gdiplus.h>` + `#pragma comment(lib,"gdiplus.lib")`，
`InitGdiPlus()` 也已全局调用 —— **换渲染器不需要加任何依赖**。

### 4.1 内嵌字体要单独注册给 GDI+

**GDI+ 看不见 `AddFontMemResourceEx` 注册的字体。** 必须用私有字体集合：

```cpp
static Gdiplus::PrivateFontCollection* g_gdipFonts = NULL;

// LoadEmbeddedFonts() 成功分支里追加：
if (!g_gdipFonts) {
    g_gdipFonts = new Gdiplus::PrivateFontCollection();
    g_gdipFonts->AddMemoryFont(data, (INT)sz);   // data 即 LockResource 得到的指针
}
```

> `AddMemoryFont` 要求 GDI+ 已 `GdiplusStartup`，所以 `InitGdiPlus()` 必须早于 `LoadEmbeddedFonts()`。
>
> **踩过的坑**：GDI+ 的 flat API `GdipPrivateAddFontFile(coll, path)` /
> `GdipPrivateAddMemoryFont(coll, ptr, len)` 的**第一个参数是 `FontCollection`，不是 `Graphics`**。
> 传错会得到 `GdipCreateFontFamilyFromName` 返回 **14 (FontFamilyNotFound)**。
> 正确姿势：`GdipNewPrivateFontCollection(&coll)` → `GdipPrivateAddFontFile(coll, path)`
> → `GdipCreateFontFamilyFromName(name, coll, &fam)`（**collection 要一路传下去**）。

### 4.2 绘制函数

`Gdiplus::Font` 用 `UnitPixel` 时的 em size，与 GDI `CreateFontW` 的
**负 `lfHeight`（em 高）同义**，所以可以直接从 `HFONT` 换算，替换成本极低：

```cpp
static float FontEmPx(HFONT f) {
    LOGFONTW lf = {};
    if (!f || !GetObjectW(f, sizeof(lf), &lf)) return 0.0f;
    return (float)(lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight);
}

static BOOL DrawTextGp(HDC dc, int x, int y, int w, int h, const wchar_t* s,
                       float emPx, DWORD color, BOOL center, BOOL wrap) {
    if (!s || !s[0] || w <= 0 || h <= 0 || emPx <= 0.0f) return FALSE;
    if (!g_gdipFonts) return FALSE;

    Gdiplus::Graphics g(dc);
    // 高 DPI 用无 gridfit 的 AntiAlias（笔画线性）；低 DPI 小字号太糊，退回带 gridfit 的
    g.SetTextRenderingHint(GetSystemDpiScale() >= 1.5
        ? Gdiplus::TextRenderingHintAntiAlias
        : Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::FontFamily family(g_fontReady ? L"MiSans" : L"Microsoft YaHei", g_gdipFonts);
    if (!family.IsAvailable()) return FALSE;   // FontFamily 才有 IsAvailable()
    Gdiplus::Font font(&family, emPx, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    if (font.GetLastStatus() != Gdiplus::Ok) return FALSE;   // Font 只能查 GetLastStatus

    Gdiplus::StringFormat fmt;
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip);
    if (!wrap)
        fmt.SetFormatFlags((Gdiplus::StringFormatFlags)(fmt.GetFormatFlags()
                                                       | Gdiplus::StringFormatFlagsNoWrap));
    fmt.SetAlignment(center ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color),
                                                  GetGValue(color), GetBValue(color)));
    Gdiplus::RectF rc((Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)w, (Gdiplus::REAL)h);
    g.DrawString(s, -1, &font, rc, &fmt, &brush);
    return TRUE;     // Graphics 是栈对象，析构时自动 Flush —— 不要漏
}
```

然后 `DrawTextL` / `DrawTextC` / `DrawKeyDual` 改成：

```cpp
    if (DrawTextGp(dc, x, y, w, h, s, FontEmPx(f), c, FALSE, wrap)) return;
    /* 回退：GDI 直绘（GDI+ 未就绪 / 字体不可用） */
```

**保留回退分支很重要**——精简 PE 环境上 `AddMemoryFont` 可能失败，
有回退就只是观感退化，不会白屏。

### 4.3 同时删掉 Mica 时代的蒙版合成

`DrawAlphaText()`（把白字画到 32bpp DIB → 取蓝通道当 alpha → 8bit 手工混合回目标）
是**半透明材质时代**的产物。材质去掉后它只剩副作用：

1. 每段文字创建/销毁一次 DIB + DC
2. RGB 压成单通道 alpha，8bit 二次量化 → 边缘多一层灰
3. **使 ClearType 不可用**（子像素信息被当灰度）

⇒ 整条删除，连带 `g_alphaPaintActive` / `g_alphaPaintBits` / `g_alphaPaintRowPixels` /
`g_alphaPaintRect` 四个变量。
**但离屏 DIB 本身（`BeginWindowPaintSurface`）要保留** —— 那是双缓冲，是另一回事。

### 4.4 ⚠ 量宽函数必须一起换成 GDI+，否则整字被静默丢弃

这是换渲染器时**最容易漏、现象最直观**的一步。

`MeasureTextW()` 原来走 GDI：

```cpp
SelectObject(dc, f);
GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);   // 汉字 26px/字
```

而文字已经交给 `DrawTextGp` 画了。**GDI+ 的布局宽度比 GDI 的 advance 之和大 10~18%**：

| 字符串 | 字号 | GDI 量 | GDI+ `MeasureString` | 比值 | **容器至少要有** |
|---|---|---:|---:|---:|---:|
| 「常规」 | 11pt @175% | 52px | **62.2px** | 1.196 | **64px** |
| 「设置」 | 12pt @175% | 56px | 67.0px | 1.196 | **68px** |
| 「退出程序」 | 11pt @175% | 104px | 115.8px | 1.113 | 117px |
| 「隐藏到托盘」 | 11pt @175% | 130px | 142.5px | 1.096 | 144px |
| `General` | 11pt @175% | 95px | 106.5px | 1.121 | 108px |
| 「开」 | 11pt @175% | 26px | 35.4px | 1.363 | 37px |

「容器至少要有」= `ceil(GDI+量) + 1` —— `DrawString` 要**整数**宽度，且实测 +1 才稳。
（Tab 原来分到 59px < 64px，所以「常规」被画成「常」。）

> **别查错方向**：这不是字体的毛病。`hmtx` 里汉字 advance 是标准 **1000/1000 = 1.0 em**
> （632 个汉字全是一千），所以 GDI 量出的 26px/字是**对的**。
> 多出来的那部分是 GDI+ 自己的保守余量。

**而 `DrawString` 在 `NoWrap` 下就按这个余量判断「放不放得下」，放不下直接丢弃末尾字符**
—— 不是裁切，是**布局阶段就扔掉**，`NoClip` 管不到这件事。于是出一类很隐蔽的事故：

| 调用点 | 量出的矩形 | GDI+ 真需求 | 现象 |
|---|---:|---:|---|
| Tab（`MeasureTabWidth`） | 52 + 4dip = **59px** | 61.4px | 四个 Tab 各只剩第一个字：**常 布 主 关** |
| 标题栏「设置」pill（`DrawHeaderPill`） | 56 + 4 = **60px** | 67.0px | 只剩「⚙ 设」 |
| 键面「图标+文字」（`DrawKeyLabel`） | `tw + 4` | — | 长标签丢尾字 |

**修法：给 `MeasureTextW` 加一条 GDI+ 路径并让它优先。**

```cpp
static int MeasureTextGp(HDC dc, const wchar_t* s, HFONT f) {
    if (!s || !s[0] || !f || !g_gdipFonts) return 0;
    float emPx = FontEmPx(f);
    if (emPx <= 0.0f) return 0;

    Gdiplus::Graphics g(dc);
    Gdiplus::FontFamily family(g_fontReady ? L"MiSans" : L"Microsoft YaHei", g_gdipFonts);
    if (!family.IsAvailable()) return 0;
    Gdiplus::Font font(&family, emPx, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    if (font.GetLastStatus() != Gdiplus::Ok) return 0;

    // flag 必须与 DrawTextGp 的单行分支一致，否则「量的」和「画的」判定条件不同
    Gdiplus::StringFormat fmt;
    fmt.SetFormatFlags((Gdiplus::StringFormatFlags)
                       (Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip));

    Gdiplus::RectF box(0.0f, 0.0f, 4096.0f, emPx * 4.0f), out;
    if (g.MeasureString(s, -1, &font, box, &fmt, &out) != Gdiplus::Ok) return 0;

    int w = (int)out.Width;      // 向上取整：整数宽度才够 DrawString 判定「放得下」
    if ((float)w < out.Width) w++;
    return w > 0 ? w + 1 : 0;    // 再留 1px 呼吸空间
}

static int MeasureTextW(HDC dc, const wchar_t* s, HFONT f) {
    if (!s || !s[0] || !f) return 0;
    int gp = MeasureTextGp(dc, s, f);
    if (gp > 0) return gp;
    /* 回退：启动早期 g_gdipFonts 还没建。此时绘制也走 DrawTextL/C 的 GDI 分支，
       两边依然同源，不会产生"量窄画宽"的错配。 */
    HFONT old = (HFONT)SelectObject(dc, f);
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
    SelectObject(dc, old);
    return sz.cx;
}
```

**`ceil` + 1 不是保险起见，是必须的**：`MeasureString` 返回 61.4，实测矩形给 61px
仍然丢字、给 62px 才完整 —— `DrawString` 要的是**整数**宽度。

**改之前先把影响面算一遍**（这是个会让容器整体变宽的改动）：

| 调用点 | 容器从哪来 | 修复后是否会溢出 |
|---|---|---|
| `MeasureTabWidth` | 自算（`量宽 + 4dip`） | 不会。4 个 Tab 共 427px ≪ 卡片 1120px |
| `SegmentedWidth` / `DrawSegmented` | 自算（`量宽 + 36dip`） | 不会。36dip 余量本来就大 |
| `DrawHeaderPill` | **固定** `hm.wMenu=68dip` / `wNum=84dip` | 不会但变紧：「设置」内容 96→107px（容器 119）；「小键盘」124→141px（容器 147） |
| `DrawKeyLabel` | 键宽 `k->w`（固定） | 不会。判据失败就往回退成纯图标，是安全方向 |
| `FitKeyFont` | `maxW`（键内可用宽） | 不会。量宽变大只会更容易降档，同样是安全方向 |

> **只有 `DrawHeaderPill` 这类「容器宽度独立写死」的地方有溢出风险**，
> 因为它的容器不随内容走。改动后如果哪个 pill 的文字贴边了，就是这里要加宽。

**一条通用纪律**：**凡是「量宽 → 当绘制矩形宽度」的调用点，量宽函数就必须与绘制函数同源。**
换渲染器时要成对替换，不能只换画的那个。

### 4.5 自查方法（不用起程序）

复刻渲染最直接：**同一个词，给矩形填不同宽度，数墨迹被分成几段**。

```bash
PY="C:/Users/29776/.workbuddy/binaries/python/envs/default/Scripts/python.exe"
"$PY" scripts/measure_parity.py "E:/Project/Git/Hydrogen-Keyboard/winres/MiSans-Medium.ttf" "常规:11" "设置:12"
```

脚本给出每个词的 GDI 量宽、GDI+ 量宽、两者的比值，以及**容器至少要给多少才不会丢字**。

**这个脚本在开发过程中踩了四个坑，全是 ctypes 层面的，记录在此免得重犯：**

1. **GDI+ flat API 的 argtypes 必须逐参数写全。**
   `GdipMeasureString` 末尾还有一个 `linesFilled`，共 **9 个**参数。少写一个 ctypes
   **不会报错**，原生函数会去读栈上的垃圾指针 →
   **静默 Access Violation：进程直接死，没有任何 traceback、没有任何输出，
   bash 里表现为 `EXIT=127` 且 stdout 为空**。
   调试这类"无声死亡"的正确姿势：**把 stdout/stderr 重定向到文件再看**，
   或者先在模块级插 `print(..., flush=True)` 二分定位。
2. **量 DPI 不能用 `CreateCompatibleDC`** —— 内存 DC 的 DPI 固定 96，
   在 175% 机器上会算出 `scale=1.0`，em 和所有宽度偏小 43%。必须用**屏幕 DC**。
3. **进程必须先声明 DPI 感知**，否则系统做 DPI 虚拟化，屏幕 DC 也只返回 96。
   HKeyboard 的 manifest 是 DPI 感知的，Python 默认不是 —— 要先
   `shcore.SetProcessDpiAwareness(2)`（失败则退回 `user32.SetProcessDPIAware()`）。
4. **别用红色描边框辅助观察** —— 红框亮度低于墨迹阈值，会被当成文字，
   量出来永远是"填满矩形"。另外**汉字之间的空隙只有 4~5px**，
   合并相邻墨迹列的阈值取 4 就会把两个字并成一段。

> 截图比对时还有一个坐标细节：**QQ 截图的四周有 24px 边框偏移**，
> 程序坐标 = 截图坐标 − 24。（本轮就是靠这个偏移量逐像素对上，才确认复刻是准确的。）
> 本机用 `scripts/hk_probe.ps1` 截的图**无边框**（窗口 rect == client rect），偏移传 0。

### 4.6 ⚠ 量宽拆成两个函数：容纳宽度 vs 布局宽度（换渲染器之后必然要拆）

§4.4 把量宽改成 GDI+ 之后会出现**第二个坑**：`MeasureString` 的返回值比文字实际占宽大
（两侧各约 0.2em），它只回答了「绘制矩形至少要多宽」，**没有回答「文字真占多宽」**。
而标题栏 pill、键面「图标 + 文字」都是拿这个返回值去**居中**的：

| | 「设置」@ g_f12（em 28px） |
|---|---:|
| GDI+ `MeasureString`（容纳宽度） | 68px |
| GDI `GetTextExtentPoint32W`（advance，真占宽） | 56px |
| 差 | 12px（两侧各 6px） |

那 6px 被当成"文字宽度"参与居中，于是 **图标被挤到贴左、图标与文字之间凭空多出近一倍空隙**
（标题栏 pill 实机实测 18px，设计值 10.5px），整组还偏左（左右留白 6 / 12 不对称）。

⇒ **拆成两个语义不同的函数，调用点各取所需**：

```cpp
// 布局宽度：GDI advance。排版 / 居中 / 图标与文字的间距都用它。
static int MeasureTextAdvW(HDC dc, const wchar_t* s, HFONT f) {
    if (!s || !s[0] || !f) return 0;
    HFONT old = (HFONT)SelectObject(dc, f);
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);   // ← 它就是 hmtx 的推进宽，是对的
    SelectObject(dc, old);
    return sz.cx;
}

// 容纳宽度：绘制矩形的宽必须 ≥ 它（DrawString 在 NoWrap 下按这个宽度判定，放不下就整字丢弃）。
static int MeasureTextW(HDC dc, const wchar_t* s, HFONT f) {
    if (!s || !s[0] || !f) return 0;
    int gp = MeasureTextGp(dc, s, f);       // 首选：与绘制同源
    if (gp > 0) return gp;
    return MeasureTextAdvW(dc, s, f);       // 回退：启动早期 g_gdipFonts 未就绪
}
```

> ⚠ **`MeasureTextAdvW` 必须定义在 `MeasureTextW` 之前**（或加前置声明），
> 否则触发 §一 的 `check_fwd.py` —— 这是本仓库最高频的 CI 失败类型。

调用点的写法（图标 + 文字并排时的**通用模板**）：

```cpp
int tw  = MeasureTextW(dc, label, f);       // 容纳宽度 → 绘制矩形宽度用它
int adv = MeasureTextAdvW(dc, label, f);    // 布局宽度 → 整组居中用它
if (adv <= 0) adv = tw;
int cx = x + (w - (iconSz + gap + adv)) / 2;                    // 整组按真占宽居中
DrawHkIcon(dc, cx, ..., iconSz, icon, fg, fg);
// DrawTextC 是把墨迹居中在矩形里的，所以矩形要再左移「(矩形宽 − 文字宽)/2」，
// 墨迹才会正好落在间距之后（否则整段文字右移半个余量）。
DrawTextC(dc, cx + iconSz + gap + adv / 2 - (tw + 4) / 2, y, tw + 4, h, label, f, fg);
```

注意「放得下判定」仍然要用 `tw`（容纳宽度），**这条不能改成 adv**：
`DrawString` 是按 `tw` 判定的，改小了就又回去丢尾字。

### 4.7 ⚠ GDI+ 垂直居中偏上的补偿（「图标 / 文字 / 按钮对不齐」的总根因）

**这是「图标文本对不齐」里最难想到的一层：文字和图标根本不是按同一套基准居中的。**

`SetLineAlignment(StringAlignmentCenter)` 居中的是**行盒**（`hhea` 的 `asc + desc`），
不是墨迹。MiSans-Medium 的度量（upem = 1000）：

| 表 | 值 |
|---|---|
| `hhea` | `asc = 1044` / `desc = -282` / `gap = 0` → **行盒 1.326em** |
| `OS/2` | `typoAsc = 890` / `typoDesc = -110` / `typoGap = 326`；`winAsc = 1044` / `winDesc = 282` |
| 其它 | `capHeight = 748`、`xHeight = 545`；汉字 advance **全部 632 个都是 1000/1000 = 1.0em** |

由 `asc/(asc+desc) = 1044/1326 = 0.7873` 推出：**基线固定在「矩形中心 + 0.2936em」**。
而汉字与无降部的拉丁字母并不占满下降部，于是墨迹中心总比矩形中心**偏高**。
图标（`DrawHkIcon` 的路径上下对称）却是按**自身包围盒**居中的 —— 两边基准不同，并排必然错开。

实测墨迹中心相对矩形中心的偏移（em 归一化，多字号取中位数）：

| 字形类别 | 例 | 偏移 |
|---|---|---|
| 汉字 | 设置 / 自动呼出 / 小键盘 | **−0.089 ~ −0.107 em** |
| 大写 / 数字 / 无降部拉丁 | A / 1 / Tab / Del | **−0.071 ~ −0.080 em** |
| 带降部 | Caps / Backspace / Settings / Layout | **+0.018 ~ +0.027 em** |

⇒ 两类各取一个折中值，在 `DrawTextGp` 里**一处**修掉（调用点不用各自打补丁）：

```cpp
// 拉丁小写里带下降部的字符。这类串的墨迹本来就低，垂直补偿要另算。
static BOOL TextHasDescender(const wchar_t* s) {
    if (!s) return FALSE;
    for (const wchar_t* p = s; *p; ++p) {
        wchar_t c = *p;
        if (c == L'g' || c == L'j' || c == L'p' || c == L'q' || c == L'y') return TRUE;
    }
    return FALSE;
}

// 单行垂直居中时，把文字矩形下移这个量，墨迹中心才落到矩形中心上。
static int TextInkShiftPx(float emPx, const wchar_t* s) {
    if (emPx <= 0.0f) return 0;
    float v = emPx * (TextHasDescender(s) ? -0.025f : 0.085f);
    return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}
```

```cpp
    Gdiplus::RectF rc((Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)w, (Gdiplus::REAL)h);
    if (!wrap) rc.Y += (Gdiplus::REAL)TextInkShiftPx(emPx, s);   // ← 只补偿单行
    g.DrawString(s, -1, &font, rc, &fmt, &brush);
```

> ⚠ **`GenericTypographic` flags 救不了这件事**（实测量宽、位置都不变），
> 必须自己做这个补偿。别在这上面浪费一轮。

**光有补偿还不够 —— 图标那一侧的"块"本身也要摆对。** 「图标 + 两行文字」的行里：

```cpp
// 文字块必须与 tile「中心对中心」。tile 30 DIP、文字块 42 DIP，共用同一个 top 是不行的：
// 那样 tile 中心会比文字块中心高 6 DIP。
// 块内两行盒的中心分别落在 块顶+10dpi（标题盒 20）与 块顶+26dpi+8dpi（描述盒 16），
// 于是块中心 = 块顶 + 22dpi；让块中心对齐 tile 中心，反解出块顶。
int descBoxH = descWrap ? (int)(32 * m.dpi) : (int)(16 * m.dpi);
int textMid  = ((int)(20 * m.dpi) / 2 + (int)(26 * m.dpi) + descBoxH / 2) / 2;
int tyText   = ty + m.tileSize / 2 - textMid;
```

因为 `DrawTextGp` 已经把「行盒中心 vs 墨迹中心」补偿掉了，**这里直接按盒中心算即可，
不要再叠一次偏移**（叠了就是错两倍）。

**实机闭环数据**（`settings_row_align.py`，7 行平均）：

| | tile 中心 − 文字块墨迹中心 |
|---|---:|
| 改前 | **−5.67 DIP**（−9.93 px） |
| 改后 | **+0.04 DIP**（+0.07 px） |

标题栏 pill 同一轮：图标↔文字间距 **18px → 10px**（设计值 6 DIP = 10.5px），
图标与文字中心差 **2.0px → 0.25px**。

**影响面（必须说清）**：`DrawTextGp` 是所有居中文字的必经路径，补偿会同时作用于
键面、Tab、按钮、弹窗等**全部单行居中文本**。它们原本偏的方向一致（都偏高），
属于一并收紧，不会出现「有的移有的不移」。多行（`wrap`）不补偿。


---

## 五、配套：字号阶梯（`18 / 12.5 / 11 / 10 / 9`）

换渲染器只解决"粗细是否线性"。**层级还得靠字号拉开**，两条纪律：

1. **控件标签必须从描述里独立出来。** 常见错误是一个 `g_sfBase` 同时承担
   Tab / 描述 / 开关「开·关」/ 分段 / 按钮 / 下拉 六七种角色 ——
   结果是**控件和灰色说明文字一样大**，整页层级是平的。
2. **相邻档至少差 1.5pt**（单字重下没有字重可用，只能靠字号+颜色）。

| 角色 | 一轮前 | 二轮 | **三轮（当前 `a5f6f51`）** |
|---|---:|---:|---:|
| 页面大标题 | 20 | 22 | **18** |
| 行主文本 | 13（错用 `g_sfHead`） | 15 | **12.5** |
| 控件标签（Tab/开·关/分段/按钮/下拉） | 10.5 | 12 | **11** |
| 行描述 | 10.5 | 11 | **10** |
| 元信息 / Copyright | 10 | 10 | **9** |

**三轮为什么整体下移一档**：二轮那套是为了"单字重下硬拉层级"设计的，相邻档刻意拉开
（22→15 差 7），层级是立住了，但**整体基准被抬高**。实机反馈"字太大了"，
量化后是行主文本比 Win11 设置页大 **43%**、页面标题大 **47%**。

两个叠加因素：
1. 阶梯本身跨度大；
2. **窗口只有 700 DIP 宽**（系统设置窗口约 1024），同样 14px 的字在窄窗口里
   视觉占比大 45% —— **不能照抄系统设置的绝对字号，也不能按 1024 宽窗口取值。**

所以形状不变、整体下移一档。**容器尺寸（行高 / rowPadY / tileSize）一个都没动** ——
字号变小后在框内重新居中即可，不会裁字。

⚠ **改阶梯时一定要 grep 每个 `g_sfXxx` 的调用点**，包括 `MeasureTabWidth` /
`SegmentedWidth` 这类**测宽函数**（漏改会导致控件宽度算错）。
`g_sfRow` 那次就是只改了 `InitFixedFonts`、没改调用点，白白空转一轮。

---

## 六、验收

- [ ] `grep -n "g_sfHead"` 零命中（语义错位的字号应被 `g_sfRow` / `g_sfCtrl` 取代）
- [ ] **每个 `g_sfXxx` 都有绘制点引用**（用 `grep -c` 数一下，防止再出现 `g_sfRow` 那种死对象）
- [ ] 笔画单调：`stroke_probe.py` 的 GDI+ 列波动 < ±8%，且 20pt 明显粗于 11pt
- [ ] 眯眼看设置页，能一眼分出「行标题 / 控件标签 / 描述」三层
- [ ] `y g q` 下降部不被裁（`NoClip` 已规避，但要实机确认）
- [ ] 「开/关」与分段文字仍居中；长文本不再溢出容器
- [ ] **所有「量宽 → 当绘制宽度」的调用点都不丢字**（见 §4.4）：
      Tab 显示「常规 / 布局 / 主题 / 关于」、标题栏显示「⚙ 设置」而不是「⚙ 设」
- [ ] 100% DPI 下 11pt 不发虚（已由 `AntiAliasGridFit` 兜底）
- [ ] 5 档 DPI × 浅/深色各看一遍

---

## 七、配套产物

| 文件 | 说明 |
|---|---|
| `HKeyboard-字体渲染重写方案.md` | 完整方案（含全部实测数据表与逐行改动清单） |
| `hk_字体渲染-现状vs方案.png` | 设置页局部仿真，1:1 物理像素，上下对照 |
| `hk_字号阶梯-笔画对比.png` | 8 个字号 × 3 种渲染路径，「国」字放大 3× |
| `scripts/stroke_probe.py` | GDI vs GDI+ 笔画宽度探针（**通用**，任何 Windows 字体都能测） |
| `scripts/measure_parity.py` | GDI / GDI+ 量宽对比 + 「最小能完整放下的矩形宽度」（查 §4.4 那类丢字） |
| `HKeyboard-字号收窄方案.md` | 三轮阶梯下移方案（含 1:1 像素对比） |
| `hk_Tab丢字-根因与修复.png` | Tab 丢字三行对照：用户截图 / 修复前复刻 / 修复后 |

---

## 八、行内元素的垂直基准：一律用「行的头部」，不要用整行

**设置页最高频的一类错位。** 2026-10-01 前后撞了三次（图标 vs 文字块 / 控件 vs 图标 /
折行行间距），根子都是「同一行的元素各算各的垂直中心」。

### 8.1 定义

```
行的头部 headH = min(整行高, rowPadY*2 + 40 DIP)     // 正常行 = 12+40+12 = 64 DIP
```

**头部固定 64 DIP。行因为描述折行变高（80 DIP）或展开（色相行 210 DIP）时，头部不变。**
图标 tile、文字块、开关、分段控件、外链按钮 —— 全都只在这 64 DIP 里居中。

```cpp
static int SettingsRowHeadH(const SettingsMetrics& m, const RECT& row) {
    int headH = m.rowPadY * 2 + (int)(40 * m.dpi);
    int rowH = row.bottom - row.top;
    return headH > rowH ? rowH : headH;
}
```

### 8.2 三处实际踩过的坑

| 写法 | 后果 | 实机实测（175% DPI） |
|---|---|---|
| 控件用 `row.top + (rowH - ctrlH)/2`（整行居中） | 折行行的开关比 tile 低 `(80−64)/2 = 8 DIP` | 「开」621.0 vs tile 606.5 |
| 文字块与 tile 共用同一个 `top` | tile 中心比文字块中心高 6 DIP | 上一轮已修 |
| 折行描述用「盒顶 26 + 盒高 32」 | 单行也被居中到 `块顶+42dpi`，标题↔描述间距大 8 DIP | 54.5px vs 42.5px |

**`SettingsComboYTop`（`row.top + rowPadY`）与「头部居中」是等价的** ——
头部高 = `rowPadY*2 + comboH`，居中后偏移正好是 `rowPadY`。所以两条规则可以合并成一个，
不必再维护两套。（色相行当年就是靠这个绕开了坑，但没抽象成公共规则，于是折行行又踩了一遍。）

### 8.3 折行描述的画法：盒顶上移、盒高不变

描述盒要为两行预留 32 DIP，但**单行时不能被居中到盒中间**，否则比不折行的行低 8 DIP：

```cpp
// 折行行：盒顶从 26 上移到 18 DIP、盒高仍 32 DIP
//   → 单行墨迹中心 = 块顶 + 34dpi（与不折行的行完全同高）
//   → 真折两行时第二行自然往下走（GDI+ 侧开了 NoClip，不会被裁）
DrawTextL(dc, tx, tyText + (int)(18 * m.dpi), tw, (int)(32 * m.dpi), desc, g_sfBase, C_DIM, TRUE);
// 不折行行：盒顶 26、盒高 16
DrawTextL(dc, tx, tyText + (int)(26 * m.dpi), tw, (int)(16 * m.dpi), desc, g_sfBase, C_DIM);
```

配套的 `textMid`（文字块中心相对块顶的偏移）**恒为 22 DIP，与折行与否无关**：
`(标题盒 20 的一半 10) + 描述盒顶 26 + 描述盒 16 的一半 8` 取平均 → `(10+34)/2 = 22`。
块中心本来就对齐 tile，偏的只是**块内**标题与描述的间距。

另外 `DrawTextGp` 的墨迹补偿要**无条件**做——折行文本的「行盒中心 vs 墨迹中心」偏移
是同一个量，只补单行会让折行描述比别人高 2px。

### 8.4 怎么量：逐行比「标题墨迹中心」，别逐像素 diff

```python
dark = (abs(a[:,:,0]-35)<25) & (abs(a[:,:,1]-32)<25) & (abs(a[:,:,2]-42)<25)
dark[:, :170] = False; dark[:, 660:] = False   # 避开 tile 与右侧控件文字
# 按行聚类 → 每行一个中心值；修前/修后逐行比，Δ = 0 就是没动
```

**diff 图会被 hover 位置、分段选中态、抗锯齿差异淹没。** 本轮第一次 diff 就被残留 hover
骗过，误以为布局页有 28 万差异像素（其实只是鼠标停的行不同）。截图前务必清 hover
（见 `hk_probe.ps1` 的 `ClearHover`）。

### 8.5 控件宽度：分段控件用 advance，不是容纳宽度

`SegmentedWidth` 每段若用 `MeasureTextW`（= `MeasureString` 容纳宽度，两侧各含约 0.2em
安全边距），每段会凭空宽出约 10px。6 项的透明度控件因此宽出 145px，把文本列挤到 150px，
`"主界面透明度"`（需要 174px）被 GDI+ 在 NoWrap 下裁成 `"主界面透"`。

```cpp
static int SegmentedItemW(HDC dc, const wchar_t* item, double dpi) {
    int adv = MeasureTextAdvW(dc, item, g_sfCtrl);   // 布局宽度
    if (adv <= 0) adv = MeasureTextW(dc, item, g_sfCtrl);
    return adv + (int)(28 * dpi);                    // 每侧 14 DIP 内边距
}
```

段宽恒大于容纳宽度（28 DIP > 0.4em ≈ 10px），所以段内文字不会被裁。绘制与命中三处共用。

**再加一道兜底**（`DrawSettingRowContent`）：文本列真的画不下标题时，允许标题一直画到
控件左缘（只吃掉 12 DIP 行内间距，不压住控件本身）—— 宁可贴在一起，也不要半截标题。

### 8.6 「间距」类问题就两个模式

用户说「优化下间距」时，先往这两条上套 —— 到目前为止每次都命中其中之一。

**模式一：按「可能性」预留空间，实际没用上 → 下方留一大片空白。**

布局页「小键盘按钮」行的描述，中文单行就放得下（墨迹止于 x=659，文本列到 x≈940），
但代码按「**可能**折两行」预留了 80 DIP 行高（内容只占 52）——
描述底到行底 **29.1 DIP**，而同一页其他行都是 13 DIP。并排一看就是"这行下面空了一大块"。

判据：`MeasureTextAdvW(描述) > 文本列可用宽` 才留两行。修后 64 DIP，描述底到行底 13.1 DIP。

关键约束：
- **文案必须唯一一处定义**（`SettingsRowDescText(tab,index)`）—— 行高测量它、绘制用它，
  两边各抄一遍必然漂移；
- 判断要放在 `SettingsRowHeight` 里（命中测试与窗口高度都经它），保证同一时刻成立；
- 字体未建时（极早的尺寸询问）返回「两行」，宁可多留也不挤字；
- **行高变了，描述盒要跟着换**：单行 26/16、两行 18/32。只改行高不换盒，
  描述会在 32 高的盒里居中到偏低 8 DIP。

**模式二：分界线贴着文字，两侧留白悬殊。**

主题色相展开行的分隔线画在 `r.top + 56dpi`，而描述盒底在 52 —— 只隔 4 DIP，
实机上看着像给描述加了条下划线；它到色板顶却隔 20 DIP。
**分界线要落在两段正中间**：移到 64（两侧各 12 DIP），展开区元素一个没动。

### 8.7 验收

- [ ] 折行行（布局页「小键盘按钮」）：`「开」中心 == tile 中心 == 文字块中心`（±1px）
- [ ] 同一页所有行的「描述墨迹中心 − 标题墨迹中心」一致（折行行与非折行行同值）
- [ ] 每行标题完整（`MeasureTextAdvW(title) <= tw`，或肉眼确认无半截字）
- [ ] 逐行标题墨迹中心与改动前比对，只有预期的行有 Δ
- [ ] **折行行在中文单行时的行高 == 非折行行**（`scripts/settings_row_align.py` +
      看窗口总高变化；本次是 743 → 715 px）
- [ ] 展开行（主题色相）：分隔线到上下的距离相等（±1 DIP）

⚠ 比对用 `scripts/tab_regression.py`（逐行标题墨迹中心），**不要用逐像素 diff** ——
后者有约 0.3% 噪声，且「内容完全不同的两个 Tab 差异量几乎相同」就是噪声的指纹。
