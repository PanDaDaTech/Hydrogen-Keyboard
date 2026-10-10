# 设置页 UI 规格 + 自绘图标

两个主题，都来自实测源码（不是凭印象）：

- **UI 规格**取自 Rust 制作工具 `PanDaPE-Maker` 的
  `crates/panda-app/src/{style.rs,widgets.rs,pages.rs}`（HKeyboard 要对齐的目标）；
- **图标几何**取自同仓库的 `crates/panda-app/src/icons.rs`。

---

## 一、制作工具的 UI 规格（DIP）

改 HKeyboard 界面时按这张表查，不要重新读 Rust 源码。

### 布局骨架

| 元素 | 规格 |
|---|---|
| 页面标题 | 26px `t90` |
| 页面头图标 | tile 38×38，圆角 10，底 `btn_regular_bg`，图标 20px 色 `btn_content` |
| tab strip | 高 42，字 14，间隙 **28**，strip 左右 inset 4，选中项下 **3px** 主色横线，strip 下缘 1px `line_divider` |
| tab 宽 | **文字宽 + 4**（不是固定格子） |
| 卡片 | 圆角 **16**，padding 上下 16 / 左右 20，底 `card_bg`，**无边框无阴影** |
| 行垂直 padding | 12（**最后一行 2**） |
| 行分隔 | 1px `line_divider`，**不画在最后一行下** |
| 设置行 | `[tile 30] +18+ [标签] +…+ [控件]` |
| 图标 tile | 30×30，圆角 10，底 `btn_regular_bg`，图标 **17px** 色 `btn_content` |
| 列表行 tile | 40×40，图标 20px |
| 行标题 | 14px `t90` |
| 行描述 / hint | 12px `t50` |
| meta 行 | 12px `t50`，分隔符是 `\u00a0· `（NBSP + 中点 + 空格） |

### 控件

| 控件 | 规格 |
|---|---|
| 开关 | 46×26，轨圆角 13，钮 20，内 padding 3；开=`primary`，关=`btn_regular_bg_active`，钮深色主题 `#DEDEE6` |
| 下拉 / 输入框 | 高 **40**，圆角 **12**，左右 padding 14，描边 **1.5px**；静默 主色18%，悬停 主色35%，聚焦 主色实色 + 底改 `card` |
| 分段控件 | 轨：底 `btn_regular_bg` 圆角 10 padding 3；段：圆角 8，padding 上下8/左右18，字14；选中段 底`card` 字`title_active` |
| 按钮 | 主：40 高圆角 12 padding 左右20 字14；行内：32 高圆角 8 padding 左右14 字13 |
| 圆形图标按钮 | 34×34，圆角 17，底 `btn_regular_bg`，图标 17px 色 `btn_content` |
| 色板方块 | 26×26，圆角 8，间隙 8；选中 = 主色环 + 2px `card` 间隔环（嵌套 padding，不是 border） |
| pill | padding 上下5/左右12，字 12，圆角 999 |
| chip | padding 上下4/左右10，字 12，圆角 8 |
| 进度条 | 高 12，圆角 4 |

字号层级只有五档：**26 / 18 / 14 / 13 / 12**。

### 按钮家族（`style.rs` 的 `Kind`）

- `Primary` —— 主色底 + `on_primary` 字，**一屏只有一个**；悬停/按下是把主色往黑混 7%/15%
- `Regular` —— `btn_regular_bg` + `btn_content` 字（所有次级控件）
- `Plain` —— 无底，悬停 `btn_plain_bg_hover` + 主色字（窗口按钮、行内链接）
- `Danger` / `DangerSoft` —— 红系，HKeyboard 用不上

> **`Kind::Plain` 的悬停色是 `btn_plain_bg_hover`，但制作工具自己发现它在白卡片上几乎看不见**
> （`oklch(0.95 0.025 hue)` vs 白卡只有 0.05 的明度差），窗口按钮最终改用了
> `btn_regular_bg_hover`（0.90 明度那档）。**HKeyboard 的行悬停也应照此办理。**

---

## 二、设置页的两种语法（改的时候别混）

| | 制作工具（目标） | HKeyboard 改前（现状） |
|---|---|---|
| 行的组织 | **一 tab 一张卡**，行间 1px 分隔线 | 每行一张独立卡，行间 6px 空隙 |
| 阅读结果 | 整个 tab = 一个面板 | 每行 = 一颗独立按钮 |
| 图标 | 30×30 **有底**圆角方块 | 24px 裸图标 |
| 卡片圆角 | 16 | 8 + 1px 描边 |
| tab | 文字宽 + 28 间隙 + 3px 下划线 | 固定 80 宽 + 8 间隙 |

**窄窗口必须偏离制作工具的一点**：制作工具的控件**左起**
（`[tile][label 110][control]`，右侧留白），因为它的内容区约 900 DIP。
HKeyboard 设置窗 700×575、margin 30 → **内容区只有 640**。
照搬左起的话，一个 46px 的开关后面会留 418px 空白。
⇒ **保留「控件右对齐」**，只采纳 tile + 定宽标签 + 分组卡片语法。

---

## 三、自绘图标（取代图标字体）

### 为什么换

HKeyboard 的图标来自图标字体（`g_sfIcon` 9 个 + `g_fKeyIcon` 1 个），
Win10 以下靠内嵌 `winres/segmdl2.ttf` —— **只有 6,620 字节，是个子集字体**。

⇒ **想加一个新图标，必须重跑字体子集化工具再重编资源。**
自绘后加图标 = 多一段顶点数据。

### 几何来源与转换

用 `scripts/svg_to_gdi.py` 从 `icons.rs` 直接生成，**不要为键盘另画一套**：

```bash
PY="C:/Users/29776/.workbuddy/binaries/python/versions/3.13.12/python.exe"
"$PY" "C:/Users/29776/.workbuddy/skills/hkeyboard-win32-cpp/scripts/svg_to_gdi.py" \
      --outdir <目录> --html
# → hk_icons_generated.h（顶点数组）+ hk_icons_check.html（并排对照，Edge 截图验收）
```

脚本做的事：解析 `icons.rs` 的 SVG body（`<path> <circle> <ellipse> <rect>` +
`<g transform>` 的 translate/scale/rotate），把全部命令归一化成
**MOVE / LINE / CUBIC / CLOSE**（`H/V/S/Q/T` 转直线或三次贝塞尔，
椭圆弧 `A` 按 W3C 附录 F.6 转贝塞尔）。

**已实测**：55 个图标、870 个节点全部转换成功，并排渲染逐对一致
（含齿轮、双色熊猫、调色板等复杂图形）。

### 数据格式

```cpp
enum HkIconOp   { HKIC_END, HKIC_BEGIN, HKIC_MOVE, HKIC_LINE, HKIC_CUBIC, HKIC_CLOSE };
enum HkIconPaint{ HKIC_STROKE, HKIC_FILL };
struct HkIconNode { unsigned char op; float a,b,c,d,e,f; };
struct HkIconDef  { const HkIconNode* nodes; float grid; };
// BEGIN : a=笔画类型 b=描边宽度 c=颜色槽(0=FG 1=HOLE)
// MOVE/LINE : a,b = 终点 ； CUBIC : a,b=c1  c,d=c2  e,f=终点
```

- **`BEGIN` 的必要性**：一个图标里描边与填充混用（`Disc` = 描边圆 + 填充圆心；
  `Panda` = 填充脸 + 挖空的眼睛）。每个 `BEGIN` 换一条 `GraphicsPath` 和一种画笔。
- **颜色槽**：`{FG}` / `{HOLE}` 两色，熊猫靠它做出"挖空露出主色底"的效果
  （制作工具里 `ink = on_primary`，`hole = primary`）。
- **grid**：所有图标画在 **24 网格**上（两个窗口按钮 Minimize/Close 是 **12**）。
  运行时 `k = size / grid`。

### 绘制要点（两个坑）

1. **描边宽度必须补偿**。几何在 24 网格上，`stroke-width` 是网格单位。
   缩放矩阵会把它一起缩掉：16px 图标（k=0.667）配 1.9 的线只得到 1.27px，比制作工具细一截。
   正确做法是**在缩放后的坐标系里用 `k * strokeW`**。
2. **线帽线接必须显式设圆头**：`SetLineCap(LineCapRound, LineCapRound, DashCapRound)`
   + `SetLineJoin(LineJoinRound)` —— 对应 SVG 的 `stroke-linecap/linejoin="round"`。

### 需要新画几个

从 `icons.rs` 的 55 个里**现成可用**：`Contrast`（主题模式）、`Palette`（主题色相）、
`Panel`（透明度 / 键盘布局）、`Globe`（界面语言 / 项目地址）、`Info`（关于页头）、
`Github`、`Close`、`External`、`ChevronDown`。

**需新画 4 个**：键盘（键盘布局）、光标/输入框（自动呼出）、⇧（Shift 符号）、
三横线（键盘 Menu 键）。放在 `icons.rs` 同一张表里（同 24 网格、同 1.9 描边、
同 `{FG}` 占位符），重跑脚本即可。

### 取消图标字体要删的东西

| 位置 | 内容 |
|---|---|
| `hkeyboard.rc` | `IDR_FONT_MDL2` 那行 |
| `resource.h` | `#define IDR_FONT_MDL2 103` |
| `winres/` | `segmdl2.ttf` |
| `hkeyboard.cpp` | `g_fontRegMdl2` + `AddFontMemResourceEx` 注册分支 |
| `hkeyboard.cpp` | `MakeIconFont()` 整个函数 |
| `hkeyboard.cpp` | `g_sfIcon`、`g_fKeyIcon` 及其 `DeleteObject` |
| `hkeyboard.cpp` | `DrawSettingsIcon()` 的 `glyphs[9]` 码位表 |
| `hkeyboard.cpp` | Menu 键的 `L"\xE700"` 分支 |

**⛔ `DrawAlphaText` 不能删** —— 它是**所有文字**的绘制路径（键帽文字、设置页文字、
描述文字），图标字体只是它的一个调用者。

---

## 四、按键显示形态：文字 / 图标 / 图标+文字

键面标签三态，`int g_keyIconStyle`（`[Keyboard] KeyIconStyle`，默认 0）：
`0=文字`（= 现状，升级不改外观）／`1=图标`／`2=图标+文字`。

### 哪些键能图标化（硬规则）

| 可图标化 | Menu / Shift / Caps / Tab / Enter / 退格 / 收起 / 方向键 ×4 / 标题栏「设置」 |
|---|---|
| **永远文字** | 双符号键（`1`+`!`）、字母、`F1~F12`、小键盘数字、`Esc` `Ctrl` `Alt` `Fn` `PrtSc` `ScrLk` `Pause` `Ins` `Home` `PgUp` `End` `PgDn` `Num` |
| 本来就是图标 | Win 键（矢量四格）、最小化、关闭 |
| 已是通用符号 | `123`、`Num` |

理由：**认不出来的图标比文字更糟**；双符号键的两个字符本身就是信息。
⚠️ `icons.rs` 里的 `Home` 是"房子=主页"，**与键盘 Home 键语义冲突**，不能混用。

### 几何与回退

| 模式 | 画法 |
|---|---|
| 图标 | 图标 **20** DIP 居中，不画文字 |
| 图标+文字 | `[icon 18] + 6 + [文字]` 整组居中；图标与文字同色 |

```
need = iconSize + gap + MeasureText(text)
need > keyW - 16 → 降级纯图标        // 例：Enter 需 18+6+40=64 > 50
keyW < 40        → 直接纯图标        // 小键盘窄键
```

键面 46×66（`g_keyHeight=46`、`g_keyW≈66`），可用宽 50。
**图标颜色 = 该键当时的 `textC`**，普通/修饰/按下/强调四态自动跟随，无需第二套配色。

### 设置项放哪

**布局 tab 第 1 行**（紧挨「键盘布局」），分段控件三选一。

- 为什么不放**常规 tab**：内容区 435 DIP、卡片内可用约 403；常规 tab 在「自动呼出」
  开着时已 7 行 × 54 = 378，**第 8 行溢出**。
- 为什么不放**主题 tab**：主题色相是可展开行（`SettingsHighlightRect` 向下 210px），
  必须留在最后一行，插前面要改 4 处硬编码 `row 2`。
- 控件：轨道 `C_REGULAR` 圆角 10 padding 3 高 40；段圆角 8 padding 8/18 字 14
  （段宽 = 文字宽 + 36，总宽 ≈ 240）。
  **选中段底 `C_KEY` + 字 `C_BTN_CONTENT`** —— 不用主色字，主色在白卡上只有 2.66:1。
  不想引入新控件就退化为 `DrawCombo()`，取值不变。
- 行号平移（**两处都要改**）：`S_HIT_FNWEB 1→2`、`S_HIT_NPBTN 2→3`、`S_HIT_NPTAB 3→4`。

### 标题栏

`设置` 按钮宽 48 → 纯图标 **40** / 图标+文字 **68**。**`DrawHeader()` 与
`HitHeader()` 各抄了一份几何，必须同时改**，否则画与热区错位。
`123` / 最小化 / 关闭三态不变。

### 三个坑

1. **`KbFrameSig` 必须加 `keyIconStyle` 字段** —— 不加则主键盘帧缓存命中，
   切了形态**纹丝不动**（看着像功能没生效）。凡影响主键盘外观的新状态都要加进去。
2. `SettingsComboListY()` 的 `minY = tabsY + tabH + 4` 里 `tabH` 要用新值 **42**（旧 33）。
3. 改设置后要 `InvalidateRect(g_hWnd, ...)` 让主键盘重绘（三窗口联动）。

### v4 新增的 7 个键面图标

`ArrowDown` / `ArrowLeft` / `ArrowRight`（**`ArrowUp` 的旋转副本**，
`<g transform="rotate(180|90|-90 12 12)">`，逐像素同源）、
`Backspace`（⌫）、`Tab`（⇥）、`CapsLock`（**方框 A**，不用 ⇪ —— Caps 与 Shift
相邻，20px 下那根横线区分不出来）、`Enter`（↵）。

退格必须换掉：现在 `KeyText()` 里 `0x08` 返回 `L"\x2190"`，与方向键左箭头同字符，
开图标模式会出现两个一模一样的左箭头。

补完后 **66 个图标 / 1078 个节点**。

