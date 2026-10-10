# HKeyboard × Ethereal 统一 UI（令牌表、键帽状态机、跨系统差异）

配套脚本：`scripts/ethereal_tokens.py`（生成任意色相的完整令牌表 + 对比度校验）

```bash
PY="C:/Users/29776/.workbuddy/binaries/python/versions/3.13.12/python.exe"
"$PY" "C:/Users/29776/.workbuddy/skills/hkeyboard-win32-cpp/scripts/ethereal_tokens.py" 300 --key
```

## 零、这套令牌的出处

Rust 工具 `PanDaPE-Maker` 的 `crates/panda-core/src/tokens.rs` + `color.rs`。
它已经把 Ethereal 主题的 oklch 三元组搬成了运行时转换表，所以**HKeyboard 不要另起一套色值**，
直接复用同一组 `(L, C, hue)` 定义，两边产物就能逐字节一致。

```rust
pub const DEFAULT_HUE: f32 = 300.0;                        // 紫色，不是 Ethereal 原版的 250
pub const PRESET_HUES: [f32; 5] = [250.0, 210.0, 180.0, 300.0, 30.0];
pub const HUE_MIN: f32 = 0.0;
pub const HUE_MAX: f32 = 359.0;   // 必须 359：滑轨末端是闭区间，360 会被 rem_euclid 打回 0
pub const RADIUS_LARGE: f32 = 16.0;
```

互相校验用的参照值（两个 crate 的测试都钉了这些）：
`hue=250` 浅色 `primary=#53A3F2`、`page_bg=#EAEFF5`、`deep_text=#1A222B`；
`hue=300` 浅色 `primary=#AC89E8`、深色 `primary=#BC98F9`、深色 `page_bg=#0E0C12`。

## 一、hue=300 令牌表（COLORREF 直接填 ThemeColors）

| token | 浅色 GDI | 深色 GDI |
|---|---|---|
| `primary` | `0xE889AC` | `0xF998BC` |
| `page_bg` | `0xF4EDEF` | `0x120C0E` |
| `card_bg` | `0xFFFFFF` | `0x231B1E` |
| `btn_regular_bg` | `0xFDEBF1` | `0x453138` |
| `btn_regular_bg_hover` | `0xFBD7E3` | `0x553E45` |
| `btn_regular_bg_active` | `0xFBC2D5` | `0x654A53` |
| `btn_card_bg_hover` | `0xFBF8F9` | `0x3B2A30` |
| `btn_card_bg_active` | `0xF0DAE1` | `0x4A363D` |
| `btn_content` | `0xAD5F7D` | `0xE4A0B7` |
| `deep_text` | `0x2A2023` | `0xD9CBCF` |
| `key_outline`（键盘专用） | `0xD4D2D3` | `0x473E41` |
| `key_outline_hover`（键盘专用） | `0xF5CADA` | `0x835365` |
| `on_primary`（键盘配方） | `0x2A2023` | `0x4B2E38` |
| `caution` | `0x3D3BDE` | `0x4C4DF1` |
| `success` | `0x5BA83A` | `0x70BE53` |

## 二、键帽状态机（7 态）

| 状态 | 底 | 轮廓 | 字 | 对比度 浅/深 |
|---|---|---|---|---|
| 普通键 | `card_bg` | `key_outline` | `deep_text` | 16.01 / 10.68 |
| 悬停 | `btn_card_bg_hover` | `key_outline_hover` | `deep_text` | 15.9 / 10.4 |
| 修饰键（Esc/Tab/Shift/Ctrl/Alt/Win/Fn/方向） | `btn_regular_bg` | `key_outline` | `btn_content` | 4.39 / 5.41 |
| 修饰键悬停 | `btn_regular_bg_hover` | `key_outline_hover` | `deep_text` | 11.73 / 6.37 |
| 按下 / 激活（Caps on、Fn on） | `btn_regular_bg_active` | `key_outline_hover` | `deep_text` | 9.87 / 5.21 |
| 强调键（Enter） | `primary` | —（同色 2px） | `on_primary` | 5.72 / 5.42 |
| 收起 / 次要键 | `btn_plain_bg_hover` | `line_divider` | `t60` | 5.74 / 6.85 |

圆角：键帽 **8px**（Ethereal 的 `rounded-lg`，全主题出现 70 次的事实标准）、面板/卡片 **16px**。
键高建议 56 DIP、最小 44 DIP、键间距 8~12 DIP。

对应 `DrawKeys()` 的改动：现在是「一个 `bg` 变量 + 恒定 `C_KEY_BORDER`」，
要拆成**底色 / 轮廓 / 文字三路同时选**，约 20 行。

## 三、四处必须偏离原配方（全部有实算数据）

### 3.1 键帽轮廓不能用「主色 15%」—— 浅色下只有 1.01:1

Ethereal 的 `.btn-card-outline` = `color-mix(in oklab, var(--primary) 15%, transparent)`。

| 比例 | 浅色描边 | 对 `page_bg` | 深色描边 | 对 `page_bg` |
|---|---|---|---|---|
| **15%（原配方）** | `#F3EDFC` | **1.01:1** ← 不可见 | `#362E43` | 1.51:1 |
| 40% | `#DED0F6` | 1.25:1 | `#5D4D79` | 2.60:1 |
| 60% | `#CDB8F1` | 1.54:1 | `#7D66A3` | 3.98:1 |

**根因**：`.btn-card-outline` 是给「白卡片上的细分隔」用的——两端都是同一个白卡片，
它的职责是"提示边界"不是"勾出轮廓"。而键帽需要从面板里**浮起来**。
且浅色下 `primary 15% over white` ≈ `#F3EDFC`，与 `page_bg` `#EFEDF4` 几乎是同一个颜色。

**修正**：`key_outline = mix(deep_text, card_bg, 0.20)`
⇒ 浅 `#D3D2D4`（对键帽 1.51）、深 `#413E47`（1.62）。
用 `deep_text` 而不是 `primary` 的好处：**它一个公式自动产出"浅色压暗、深色提亮"**，
因为 `deep_text` 本身在两种模式里就是反的，不需要写模式分支。

### 3.2 悬停态不能只改底色 —— 只有 1.06:1

`btn_card_bg_hover` vs `card_bg`：浅 `#F9F8FB`/`#FFFFFF` = **1.06:1**，深 `#302A3B`/`#1E1B23` = **1.23:1**。
网页上够用（鼠标指针在），**触摸键盘上完全看不出**。

**修正**：悬停**同时**把轮廓换成 `key_outline_hover = mix(primary, card_bg, 0.45)`
⇒ 浅 `#DACAF5`（1.53）、深 `#655383`（2.52）。底色变化降级为辅助信号。

### 3.3 按下态文字不能是 active 底上的 `btn_content` —— 3.15:1

| `btn_content` 放在 | 浅 | 深 |
|---|---|---|
| `btn_regular_bg` | **4.39** ✅ | **5.41** ✅ |
| `btn_regular_bg_hover` | 3.74 | 4.53 |
| `btn_regular_bg_active` | **3.15** ❌ | **3.61** ❌ |

键面文字是 14px，按正文标准要 4.5:1。

**修正**：按下态文字换成 `deep_text` ⇒ `btn_regular_bg_active` 上 **9.87 / 5.21** ✅。
（"底加深 + 字压深 + 轮廓变主色"三重信号，对比度反而更好。）

### 3.4 Enter 键不要"实心主色 + 白字"—— 2.80 / 2.33

| 前景 on `primary` | 浅 | 深 |
|---|---|---|
| 白字 | **2.80** ❌ | **2.33** ❌ |
| `deep_text` | **5.72** ✅ | 1.47 ❌ |

Rust 工具的 `primary_fg` = `white@1.0`(浅) / `black@0.70`(深)，
它自己的测试只卡 `>= 2.5`（`tokens.rs::text_on_accent_stays_readable_in_both_modes`），
因为那边是 26px 大标题；键盘 Enter 是 14px 小字且最高频。

**修正**：`on_primary` = 浅色 `deep_text`（5.72）/ 深色 `black@70% over primary` = `#382E4B`（5.42）。
⇒ 这是**有意偏离 Rust 工具的一处**，理由只跟字号有关。若要严格一致，
退路是 Enter 也走 `btn_regular_bg_active` + `deep_text`（9.87 / 5.21），代价是强调力度弱一档。

## 四、三个真实的 OS 差异

### 4.1 窗口圆角（唯一无法完全消除的）

现状 `ApplyRoundedWindow()`：`g_winBuild >= 22000` 走 DWM 原生圆角，否则 `SetWindowRgn(NULL)` 强制直角。
⇒ 现状本身就是 **Win11 圆角 / XP、Win7、Win10 直角**，不统一。
（`(void)logicalRadius;` 这个被忽略的参数就是老 `CreateRoundRectRgn` 实现的残留。）

- **推荐**：两边都直角（Win11 也 `DWMWCP_DONOTROUND`），成本 3 行，差异归零。
  Ethereal 的圆角感来自**面板内的卡片和键帽**（16/8px），不是窗口外轮廓，损失很小。
  记得给窗口加 1px 描边（浅 `black@10%` / 深 `white@10%`），否则纯色面板在桌面上会糊边。
- **备选**：两边都 `CreateRoundRectRgn`。但 XP 侧是 **1-bit 区域裁剪、硬锯齿**，
  16px 半径下明显；半径压到 10px 才勉强可接受。仍是"看起来一样"而非"真的一样"。

### 4.2 系统深色判定

`IsSystemDarkMode()` 读 `HKCU\...\Themes\Personalize\AppsUseLightTheme`。
**XP 上没有这个键** ⇒ 打开失败 ⇒ `val` 保持初值 `1` ⇒ 恒返回浅色。
⇒ XP 上"跟随系统"= 浅色主题。合理降级，但设置页要标注，否则用户以为坏了。

另：去材质后 `IsDarkThemeActive()` 恒定走 `IsSystemDarkMode()`（不再是 `IsSystemBackdropDarkMode()`），
即从 **`SystemUsesLightTheme`（系统 UI）切到 `AppsUseLightTheme`（应用）**。
"系统深色 + 应用浅色"的用户会看到键盘深浅**反过来**——属预期，发版说明要提。

### 4.3 透明度上限

`ApplyWindowOpacity()` 的启用条件含 `!g_supportsMaterial`，而
`g_supportsMaterial = !g_isWinPE && g_winBuild >= 17763` ⇒ **Win11 与 Win10 1809+ 恒为 TRUE**。
去材质后必须去掉这一项，否则现代系统用户设置页第 2 行直接空白。

## 五、为什么"统一"这件事已经完成了一半（四条证据）

1. **三个窗口全是 `WS_POPUP` 自绘**（主窗口 / `HKeyboardSettings` / `HKeyboardClosePrompt`），
   没有标题栏、系统按钮、滚动条 ⇒ OS 主题与 uxtheme 无处泄漏。
2. **文字走灰度 AA 不是 ClearType**：`DrawMaterialText()` 把白字画到 32bpp DIB mask →
   按像素亮度当 alpha → 逐像素合成。XP 与 Win10+ 的 ClearType 引擎行为并不一致，
   灰度 AA 反而让两边字节级接近。**迁移时不要动它。**
3. **字体全部内嵌**（MiSans Regular/Bold + `segmdl2.ttf`），
   `MakeFont()` 只在 `AddFontMemResourceEx` 失败时才回退 `Microsoft YaHei`。
4. **颜色已经间接寻址**：`ThemeColors` 9 字段 + 9 个 `C_*` 宏，换皮 = 改一张表。

## 六、色相旋钮：骑在现成的选色器上

设置页**已经有**完整的高亮色选择器：`g_hlMode`（默认/跟随壁纸/自定义）、`g_hlColor`、
`BgrToHsv()`/`BgrFromHsv()`、5 色预设色板、hex 输入框、`GetWallpaperAccentBgr()`（含 XP 的
`ColorizationColor` 兜底）。把它的语义从「高亮色」升级成「主题色相」即可。

- 新增 `Hue`，范围 `0..359`（**不能是 360**，见 §零）。
- 预设色板改为 `PRESET_HUES`，用 `oklch(0.70 0.14 H)` 渲染，默认选中 300。
- 新增 `OklchToBgr(L, C, hue)`（约 25 行，只用 `cos/sin/pow`，无需 `_USE_MATH_DEFINES`）
  与 `HkbTokensBuild(hue, dark, out)`。
- `ThemeColors` 从 9 字段扩到约 17 字段；**保留 `hot` 作为 `primary` 的别名**，
  这样 18 处 `C_HOT` 调用点一处都不用改。
- ini 兼容：老 `HighlightColor` 存在而无 `Hue` → 用 `BgrToHsv()` 取 H 作初值
  （老默认 `#0078D4` 的 H≈206，正好落在预设 210 附近）。
- **`ResolveFontColor()` 整段可删**：它是材质模式的硬编码补丁
  （`C_DIM→RGB(112,112,112)`、`C_WHITE→RGB(48,48,48)`），
  新令牌表的 `text`/`textMuted` 已经是对比度达标的实色。

## 七、渲染约定（易踩的坑）

> **轮廓色和键帽底色一律用"预先算好的实色"，不要在绘制时传 alpha。**

- 去材质后非材质路径走 `DrawRoundRectAlpha(..., 255, 255)`，全不透明；
- GDI+ 往 32bpp `BI_RGB` DIB 里画半透明形状，alpha 通道语义不可靠
  （只有材质模式走 BufferedPaint 时才有明确语义）；
- 用实色 ⇒ XP 与 Win11 **字节级一致**。
- 真正需要透明的地方只有整窗透明度（`WS_EX_LAYERED`），那是 OS 做的。

## 八、顺手要清的 5 处硬编码色

| 位置 | 现值 | 应为 |
|---|---|---|
| 开关圆钮 `DrawRoundRect(..., 0xFFFFFF, ...)` | 硬编码白 | `card_bg` |
| 勾选对勾 `CreatePen(PS_SOLID, 2, 0xFFFFFF)` | 固定白 | `on_primary` |
| 滑块圆钮 `BlendColor(C_DIM, 0xFFFFFF, value)` | 固定白 | `BlendColor(C_DIM, card_bg, value)` |
| 色板 chip 对勾 `IsLightColor(color) ? 0x202020 : 0xFFFFFF` | 硬编码 | `on_primary` |
| 按钮文字 `IsLightColor(C_HOT) ? 0x1A1A1A : C_WHITE`（×2） | 硬编码 | 预计算的 `on_primary` |

`IsLightColor()` 用 `(r*299+g*587+b*114)/1000 >= 150`，是 `readable_foreground` 的粗代理，
可以留着，但两个候选色不能硬编码。

## 九、验收要点

- 色相滑轨拖到**最右端不弹回最左端**（359 而非 360）
- 5 个预设色板点击后 `Theme/Hue` 与芯片高亮一致
- 老 ini（`HighlightColor=#0078D4`）启动 → 色相落在 206 附近，预设 210 芯片呈选中
- 浅/深两模式下，普通/悬停/按下/修饰/Enter **五态同屏可区分**
- 键帽轮廓：肉眼能看出每个键是独立的一块，不是一片糊在一起的板
- 悬停有明确反馈（不是只有 1.06:1 的底色变化）
- 对比度抽检：普通 ≥10:1、修饰 ≥4.4:1、按下 ≥5:1、Enter ≥5.4:1
- XP/PE 上启动：无控件主题泄漏、内嵌字体生效、Menu 键汉堡图标能显示
- 白色系控件（开关钮、滑块钮、对勾）在新 `page_bg` 上不消失
