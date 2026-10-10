# 字体显示：单字重（MiSans Medium）下的层级

**用户说「粗体看着难受 / 字重不统一 / 标题加粗很怪 / 字体显示方案」时，先读这份。**

## 一、一句话根因

`winres/MiSans-Medium.ttf` 里**只有一个字面**（`fonts: 1`），而 `MakeFont(size, bold)`
在 `bold` 时请求 `FW_BOLD` —— GDI 找不到 700，就把 Medium 的轮廓**整体等距外扩**。
那不是设计师画的粗体，是「描边加肥」，中文笔画本来就密，外扩后字腔直接填死。

## 二、先用脚本拿事实，别猜

```bash
PY="C:/Users/29776/.workbuddy/binaries/python/versions/3.13.12/python.exe"
FONT="E:/Project/Git/Hydrogen-Keyboard/winres/MiSans-Medium.ttf"

"$PY" scripts/gdi_text_probe.py family  "$FONT"        # 族里有几个字面
"$PY" scripts/gdi_text_probe.py metrics "$FONT" "小键盘设置"   # 假粗体多出多少墨
"$PY" scripts/gdi_text_probe.py measure "$FONT" "设置" "10.5,12,13"
```

`family` 的判据：**族名恒同 + `lfWeight` 只是回显请求值 = 族里没有多个字面**。
（实测 400/500/600/700/800 全部返回 `MiSans`。）

## 三、量化判据（可直接复用）

| 指标 | 真 Bold 字面 | 本项目的假粗体 |
|---|---|---|
| 墨迹覆盖率增幅 | +15~20% | **+30~38%** |
| 笔画连通域 | 基本不变 | **−4~5 个**（笔画粘连） |
| 字宽变化 | ±0（设计好的 advance） | **中文 +6.2% / ASCII +12.6%** |

**字宽变化是最容易被忽略的危害**：凡是「先 `MeasureTextW` 量宽、再按宽度居中/裁剪」
的地方，量宽与绘制字重不一致就会偏。`FitKeyFont()` 正是一边量一边画 ——
假粗体让它把本可以用 14pt 的键误判为放不下、掉到 13pt。
**去掉假粗体后窄键的降档需求反而减少**（Medium 一律比假粗体窄 4~5px）。

## 四、参考对象 PanDaPE-Maker 的做法

`crates/panda-app/src/theme.rs`：界面**只有一个字面、零字重变化**。
检索 `Weight::` 只命中 `Weight::Inline` / `Weight::Bar` —— **那是边框样式，不是字重**。

它的字号阶梯（源码实读）：`26 / 24 / 22 / 20 / 17 / 16 / 15 / 14 / 13 / 12 / 11`，
关键对应：

| 字号 | 角色 |
|---|---|
| 20 | 卡片主标题 / 对话框标题 |
| 17 | 区块标题 |
| 14 | **行主文本**（deep_text） |
| 13 | **正文**（deep_text） |
| 12 | **次要说明**（muted） |

**三条可复用规律：**

1. **跨度大的层级靠字号，跨度小的靠颜色。** 20→13 不用颜色也分得开；14→13 必须靠颜色。
2. **相邻层级字号差 1pt，且同时换颜色** —— 两把尺子一起用才立得住。
3. 同一字号可复用在完全不同的位置（13pt 既是首页正文、也是设置行说明）。

### ⚠ 一条不能照抄的地方

工具那份 MiSans 的 `OS/2.usWeightClass` 被从 **380 改成 400** —— 那是
`cosmic-text` 的硬要求（`FontMatchKey::font_weight_diff == 0`，不匹配就直接跳过整个族，
所有汉字变成 `.notdef` 方块）。

**GDI 没有这个约束**：实测请求 400/500/700 全部命中同一字面。
所以本项目的出路不是「改字重标记」，而是**根本不去请求不存在的字重**。
（HKeyboard 内嵌的这份是上游原始值 `500`，不要为了让字重"生效"去改它。）

## 五、落地方案（全部 `FW_NORMAL`，永不出现 `FW_BOLD`）

### 5.1 `MakeFont()` 去掉 bold 参数

```cpp
// 界面只有一个字面（MiSans Medium），族里不存在 700，
// 请求 FW_BOLD 只会拿到 GDI 的仿真加粗（轮廓等距外扩）：
// 中文笔画粘连、字宽 +6~13%，而且量宽与绘制不一致会让布局偏移。
static HFONT MakeFont(double size) {
    int h = -MulDiv((int)(size * 10 + 0.5), 96, 720);
    const wchar_t* face = g_fontReady ? L"MiSans" : L"Microsoft YaHei";
    return CreateFontW(h, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, face);
}
```

### 5.2 设置页字号阶梯（窗口 700×575）

| 角色 | 旧 | 新 | 颜色 |
|---|---|---|---|
| 页面大标题 | 20 **bold** | 20 | C_TEXT |
| 面板标题 | 11.5 **bold** | **13** | C_TEXT |
| 行主文本 | 10.5 | **12** | C_TEXT |
| 行描述 | 10.5 | 10.5 | C_DIM |
| Tab / 按钮 | 10.5 **bold** | 10.5 | 选中 C_TEXT／未选 C_DIM |
| 下拉框文字与选项 | 10.5 | 10.5 | C_TEXT |
| 元信息 / Copyright | 10.5 | 10 | C_DIM |

**「行主文本」必须提到 12**：旧代码里行主文本和行描述**是同一个字号（10.5）**，
只靠颜色分，粗体是第二层。粗体一去，这一行就塌了。

**「Tab」不需要加字号**：Tab 只有 2 个汉字，选中态已有深／浅之分，
去掉粗体后颜色就是唯一且足够的信号（工具的左侧栏正是这样）。

> ⚠ 旧代码 `g_sf13` **同时**服务「行主文本」和「下拉框文字」两类角色 ——
> 必须拆开：行主文本要变大，**下拉文字不能变大**（见 5.4）。

> ### ⚠ 二轮修订（2026-10-01 实机反馈后）
>
> 上表按方案实施后（`39e5b14`），用户反馈**"字体显示还是怪怪的 —— 该粗的地方不粗，
> 细的地方反而还有点粗"**。二轮实测确认：**问题不在字号选得对不对，而在渲染器**。
>
> 1. 上表里「行主文本 12pt」**其实根本没生效**：`g_sfRow` 创建了但没有任何绘制点引用它，
>    行主文本实际用的是 `g_sfHead`(13pt)。**改字号方案时务必 grep 确认调用点，别只改 `InitFixedFonts`。**
> 2. 更根本的是 **GDI 的网格拟合**：`竖宽/em` 随字号锯齿跳变（13→14 骤降 22%），
>    单字重下光靠字号拉不开稳定层级。
> 3. 修正后的阶梯：**22 / 15 / 12 / 11 / 10**，且**控件标签必须从描述里拆出来独立成档**。
>
> **详见 `references/gdi-hinting-and-gdiplus.md`。**

### 5.3 键面

| 角色 | 旧 | 新 |
|---|---|---|
| 普通键 / 修饰键主档 | 14 **bold** | 14 Medium |
| 退格大箭头 | 18 **bold** | 18 Medium |
| FitKeyFont 阶梯 | `{14b, 13, 12}` | `{14, 13, 12}` |

**去掉粗体后修饰键还分得开吗 —— 分得开，因为粗体只是第四层：**

| | 底色 | 文字色 | 粗体 |
|---|---|---|---|
| 普通键 | `C_KEY` 白 / `#181E24` | `C_WHITE` | ✗ |
| 修饰键 | `C_REGULAR` 淡蓝 / `#283747` | `C_BTN_CONTENT` 蓝 | ✓（要删的） |

**顺带清掉死变量 `g_f16b`** —— 只有声明/创建/销毁三处，没有任何绘制代码引用。

## 六、顺手会挖出来的文案溢出

`DrawTextL()` 是 `DT_SINGLELINE`，超宽直接裁。实测容器可用宽：

| 容器 | 算式（窗口 700、margin 30、textX 98） | 可用 |
|---|---|---|
| 开关行文字 | `670 − 20 − 46 − 42 − 98` | **464** |
| 下拉行文字 | `(670 − 20 − 176) − 12 − 98` | **364** |
| 分段控件行 | `(670 − 20 − 210) − 12 − 98` | **330** |
| 下拉框内文字 | `176 − 14 − 46` | **116** |

已实测越线、**现在就处于被裁状态**的（字体方案的顺带修复项）：

| 位置 | 文案例 | 宽 | 可用 |
|---|---|---|---|
| 布局 · 小键盘按钮 | `在标题栏显示；默认布局切到小键盘，全尺寸显示或隐藏数字区` | 392 | 364 |
| 同上（英） | `Show it in the title bar; toggles the numpad layout / section` | 387 | 364 |
| 下拉框 · 英文语言名 | `Simplified Chinese` | 123 | **116** |

**分段控件是全局最紧的一处**：宽度公式是 `Σ(MeasureTextW(label) + 36)`，
所以「按键图标样式」+ 两个分段项在英文下已占 272 / 330。
这**正是下拉文字不能跟着行主文本放大的原因**。

## 七、不做什么

| 方案 | 为什么 |
|---|---|
| 再加一个 MiSans-SemiBold 字面 | 要重跑子集化 + 多一份体积 + 多一个字体句柄；工具用**一个字面**已经做完同样的事 |
| 改字体 `usWeightClass` | 那是 cosmic-text 的要求，GDI 没有 |
| `SetTextCharacterExtra` 加字距 | 实测按**像素/字符**生效（`extra=32` → 6 字加 160px），粒度过粗 |
| 改配色令牌 / 行高内边距 | 与字重问题无关；`title`/`desc` 的 24 DIP 行距在 12pt 下仍然够 |

**`ANTIALIASED_QUALITY(4)` 与 `CLEARTYPE_QUALITY(5)` 实测字宽完全相同**，
所以切换质量**不会引起布局变化**（不会把控件撑宽、不会让文字被裁）。

> ### ⚠ 但「质量不重要」是错的 —— 二轮实测推翻
>
> **字宽相同 ≠ 观感相同。** `ANTIALIASED` 是灰度 AA 且**仍然做网格拟合**，
> 笔画粗细会随字号锯齿跳变（见 `references/gdi-hinting-and-gdiplus.md`）。
> 真正的解法是**把文字渲染整体换成 GDI+ `TextRenderingHintAntiAlias`**。
>
> 一句话：**布局安全 ≠ 渲染正确**，这两件事要分开判断。

## 八、验收

- [ ] `grep -n "FW_BOLD"` 零命中；`MakeFont(.*, *1)` 零命中
- [ ] `g_f14b` / `g_f16b` / `g_f18b` 已删除
- [ ] 行主文本 12pt vs 行描述 10.5pt —— 同屏**一眼分出主次**
- [ ] 4 个 tab 逐个检查描述不被右边缘裁切；英文界面同上
- [ ] 下拉框内文字不被箭头压住
- [ ] `PrtSc` / `ScrLk` / `Pause` 在 1u 键内完整、降档后与同排观感一致
- [ ] 5 档 DPI（100/125/150/175/200）各看一遍
- [ ] 浅色 + 深色各看一遍

## 九、配套产物

| 文件 | 说明 |
|---|---|
| `HKeyboard-字体显示方案.md` | 完整方案（含全部实测数据表） |
| `HKeyboard-字号收窄方案.md` | 字号整体下移一档（18/12.5/11/10/9），含精确行号 |
| `hk_字体方案-单字重层级.html` | 真实 GDI 位图对照页 |
| `hk_字号-方案对比.png` | 截图 vs 复刻 vs 两档候选，1:1 物理像素 |
| `scripts/gdi_text_probe.py` | 字面枚举 + 覆盖率/连通域 + 字宽探针（**通用**，任何 Windows 字体都能测） |
| `scripts/gdi_vs_gdiplus_metric.py` | 同一文本分别用 GDI / GDI+ 渲染并量墨迹宽高（**通用**） |

---

## 十、字号阶梯的基准怎么定（2026-10 补）

**坑：单字重下字号是唯一的层级手段，极容易越调越大。**
`22 / 15 / 12 / 11 / 10` 这套把层级拉满了，相邻档确实分得开，但**整体基准被抬高**，
实机反馈就是"字太大"。收窄时**不要重新设计层级，整体下移一档即可**。

### 10.1 参照系不能照搬系统设置页

| | 窗口宽 | 列表标题 | 描述 |
|---|---|---|---|
| Win11 设置页 | ~1024 DIP | 14px（10.5pt） | 12px（9pt） |
| HKeyboard 设置页 | **700 DIP** | 12.5pt | 10pt |

窗口窄 32%，**同 pt 的字视觉占比大 45%**。所以不能照搬 10.5pt —— 窄窗口要用更小的绝对字号，
才能得到相近的视觉重量。

### 10.2 当前推荐阶梯（v7）

| 用途 | 变量 | **值** | em @175% |
|---|---|---|---|
| 页面大标题 | `g_sfBig` | **18** | 42 px |
| 行主文本 | `g_sfRow` | **12.5** | 29 px |
| 控件标签 | `g_sfCtrl` | **11** | 26 px |
| 行描述 | `g_sfBase` | **10** | 23 px |
| 元信息 | `g_sfMeta` | **9** | 21 px |

- 全部集中在 `InitFixedFonts()`（`hkeyboard.cpp` 第 1138~1145 行），改 5 行即可。
- 行高 / `rowPadY` / `tileSize` / 文字框高**都不用动**：那些是容器，字号变小后在框内重新居中，不会裁字。
- 想更紧凑是**另一件事**（`rowPadY` 12→10、`tileSize` 30→28、`contentDip` 下限 40→34、
  文字框 20/16→18/14，行高 64→54 DIP）。**先只改字号**，一次改两处出问题不好定位。

### 10.3 Tab 必须脱离 `g_sfBase`

`DrawTabStrip()`（第 2895 行）此前把 Tab 画在 `g_sfBase` 上，而注释却写着"控件标签"用 `g_sfCtrl`。
Tab 是**导航**，不该和"行描述"同一档。改用时**必须同步 `MeasureTabWidth()`（第 2868 行）**，
否则 tab 宽度、下划线位置、命中区会一起错位。

### 10.4 用不变量反查截图（很有用的一招）

拿到实机截图时，先量三个**几何不变量**印证 DPI，再看字号：

| 不变量 | DIP | @175% | 实测（2026-10 截图） |
|---|---|---|---|
| 行高 | 64 | 112 px | 112 ✔ |
| 卡片宽 | 640 | 1121 px | 1121 ✔ |
| 标题左边距（margin30+icon38+16） | 84 | 147 px | 148 ✔ |

三个都对得上 ⇒ 几何没问题，问题只在字号。若只有字号对不上，用
`scripts/gdi_vs_gdiplus_metric.py` 把同字号渲染出来量墨迹宽，就能反推截图的真实字号：

```
截图「设置」墨迹 89px  ÷  22pt 渲染的 99px  × 22  ≈  19.8pt
```

> ⚠ **本项目实测到过一处未解释的偏差**：某次截图里量出的字号比代码标注的小 10~15%
> （22pt 位置完全正确、宽度只有 89px vs 渲染的 97~99px；GDI 与 GDI+ 两条路径都排除）。
> 遇到这种情况**不要硬凑公式**——先确认跑的是不是最新编译产物，再把方案的两个数字按同比例折算。

## 十一、字体子集缺字：UI 回退宋体（「部分字体显示不对劲」的真凶之一）

**用户说「部分字体显示不对劲 / 这几个字的字体跟别的不一样 / 一行字里有的字粗有的字细」时，
先查这个，再查字号。**（2026-10-02 实锤案例：v2.0 关于页「社区交流 / 开源许可」）

### 11.1 症状与确诊

- 同一行文字里**部分字符**笔画异常细、带衬线感（宋体特征），同行的其余字符是正常 MiSans
  无衬线 —— 例如「**社**区交**流**」中「区」「交」正常、「社」「流」变宋体。
- 确诊三步：
  1. PIL 裁剪该行文字 4x 放大，肉眼比对字形（细笔画衬线 = 回退）；
  2. `fontTools` 读 `winres/MiSans-Medium.ttf` 的 cmap，查缺的字符：
     `python -c "from fontTools.ttLib import TTFont; cmap=TTFont('winres/MiSans-Medium.ttf').getBestCmap(); print(ord('社') in cmap)"`
  3. 对照 `build/_ui_chars.txt`（当初生成子集的字符表）—— 缺的字必然不在表里。

### 11.2 根因

`winres/MiSans-Medium.ttf` 是**子集字体**（全量 8 MB → 子集约 170~190 KB，rc 里整文件 RCDATA 内嵌），
子集只包含 `build/_ui_chars.txt` 字符表里的字。**新增 UI 文案（T(L"...") / L"..."）出现表外的字时，
GDI+ DrawString 对缺字形回退系统字体（宋体）**，与同行 MiSans 混排。GDI 路径同理。
多数字符缺失并不立即露馅 —— 只有文案真显示出来才看得见，所以特别阴。

### 11.3 修复与防复发（tools/subset_font.py，已进仓库 f9b08c5）

```
python tools/subset_font.py            # 从源码字符串全量重建 winres/MiSans-Medium.ttf
python tools/subset_font.py --check    # 只校验：源码字符 ⊆ 现有 cmap，exit 1 报缺字清单
```

- 字符收集：hkeyboard.cpp 全部字符串字面量（含 `\uXXXX` 转义、T() 两参数）∪ ASCII 全集
  ∪ 旧 `_ui_chars.txt` ∪ `_pua.txt`；从 `build/font-backup/MiSans-Medium.ttf`（全量 8 MB）子集化。
- name 表全保留（GDI 按族名 MiSans 取字，**代码零改动**）；PUA 私有区与全量字体本就
  没有的码点（⚠ ↔ ⇔ 等，历来系统回退）不计缺。
- **纪律：改任何 UI 文案后，推之前跑一次 `tools/subset_font.py --check`**（与 5 个静态检查并列）。
- 实测数据：缺字时 745 条 cmap → 重建后 962 条（972 个 UI 字符），173 KB → 193 KB；
  exe 424448 → 444928 B（差值 ≈ 字体增量，资源层验证法：pefile 提取 RCDATA id=101
  再读 cmap，可不用截图确认修复进入产物）。

### 11.4 全局宋体 = 元数据未修正（比「缺字回退」更致命，2026-10-02 实锤）

**「全局宋体」与「部分字符回退宋体」是两个不同的坑**，前者更严重（整个界面全废）：

| | 部分字符回退（§11.1） | 全局宋体（本节） |
|---|---|---|
| 症状 | 同行个别字变宋体 | **所有文字**都变宋体 |
| 根因 | cmap 缺字 | **name/OS/2 元数据没修正** |

原版 MiSans-Medium.ttf 的元数据是「非标准」的，fontTools subset 原样保留了三处会
破坏 GDI/GDI+ 匹配的字段，**任何一处不改都会全局宋体**：

1. **name ID1(family) = 「MiSans Medium」**（最致命）—— 程序 `CreateFontW(L"MiSans")`
   与 GDI+ `FontFamily(L"MiSans", collection)` 都按「MiSans」**精确**匹配；GDI+ 严格按
   ID1 匹配 → 返回 `FontFamilyNotFound(14)` → `DrawTextGp` 失败回退 GDI/系统字体。
   （GDI 的 CreateFontW 会看 typo family ID16='MiSans' 所以 GDI 路径反而能匹配 ——
   但主绘制走 GDI+，GDI+ 一挂就回退到宋体。）
2. name ID2(subfamily) = 「Regular」（应改为「Medium」）。
3. OS/2.usWeightClass = **380**（标准 100~900 整百，Medium 应 500）。
4. OS/2.fsSelection = 0x0100（缺 REGULAR 位 0x40）。

**旧 173KB 子集（git 里正常那版）正是把这四处修正了**：family=MiSans / subfamily=Medium /
usWeightClass=500 / fsSelection=0x0140。

**验证法（ctypes 直调 GDI+ flat API，复现 DrawTextGp）**：`GdipNewPrivateFontCollection`
→ `GdipPrivateAddMemoryFont` → `GdipCreateFontFamilyFromName("MiSans", coll)`（返回 0=Ok，
14=FontFamilyNotFound）→ `GdipCreateFont(fam, em, 0/*Regular*/, 2/*Pixel*/)`（0=Ok，2=InvalidParameter）。
坏字体 Family=14，修复后 Family=0/Font=0。脚本见 `scripts/gdiplus_match.py`。

**修复已固化进 tools/subset_font.py**（c5a9131）：子集化后强制改写 name ID1/ID2 + OS/2
两字段，并把 family / usWeightClass / fsSelection 三项断言写进 `--check` 与重建自检，
任何一项不对即报 ERR。**改文案 → 重跑重建 → 自检三项全绿才推。**

### 11.5 字符收集器必须是词法状态机（「遇」字残留的根因，2026-10-02）

**「自检全覆盖但实机仍有个别字回退」= 收集器本身有洞。** tools/subset_font.py 最初的
`source_chars` 用正则 `L?"..."` 提取字符串，被两个真实源码结构搞错位：

1. **注释里的孤立双引号**（L396 `// ... `{ } | : "`（副符号格）...`）—— 正则字符类
   `[^"\]` **能吃换行**，把 L396→L913 共 518 行吞成一个假字符串；
2. **字符字面量包着双引号**（键位表 `L'"'`）—— 正则把它当字符串起点，从此连锁错位，
   「遇到 bug...」整串被吞，「遇」(U+9047) 从未进字符表。

自检为什么没拦住：自检校验的是「收集的字符 ⊆ cmap」，**收集本身不完整时自检必然
通过**（garbage in, garbage out）。这也解释了为什么旧字符集里有 210 个「假字符」
（从注释与错位里收进来的），真实 UI 字符只有 828 个而不是 972。

修复（7d91565）：`source_chars` 重写为逐字符状态机（// 行注释 / 块注释 / "字符串" /
'字符字面量' 四态；字符串与字符字面量内容都收，转义处理抽 `_escaped` 共用）。
重建后 cmap 962→752 条、193KB→149KB，「遇」「·」等 7 个真缺字补齐。

**教训：凡「从源码提取 X 再校验 X ⊆ 产物」的工具，提取器必须经得起源码里的一切
语法噪声；自检只闭环「提取→产物」，闭环不了「提取↔真实」。**
