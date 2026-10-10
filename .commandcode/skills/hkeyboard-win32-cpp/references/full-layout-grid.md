# 全尺寸（104 键）布局：1u 单位网格

适用：改 `g_layoutMode == 2` 的 `BuildComplete()`，或做任何「列对齐 / 键宽不一致 / 某一行看着歪」的活。

---

## 0. 一句话

**全尺寸布局不许用「每行 剩余宽度 ÷ 键数」。** 那会得到「同一把键盘上 1u 键有五种宽度」。
唯一正确做法：**1u 由宽度反解一次，所有 x = `round(列位 × u)`。**

---

## 1. 先量现状（改之前一定先做这步）

把 `BuildKeys()` + `BuildComplete()` 的算式（含 C 的整数截断）照抄到 Python 复算，别靠肉眼估。
脚本：`scripts/hk_full_eval.py`（`python hk_full_eval.py` 直接打印偏差表）。

`81f828c` 那次量出来的结论（默认 1380×372、dpiScale=1）：

| 事实 | 数字 |
|---|---|
| 主区列距 | 1u = 54.6px（= `(MAIN_W + gap) / 15`） |
| 各种「1u 键」实际宽度 | F 49 / 数字 50 / Tab 50 / **Caps 46** / **Shift 55** / **导航与数字区 73** |
| 最大差 | 46 → 73 = **59%** |
| 列漂移 | q +18px、a +21px、z +15px、p +26px、**m +51px(+0.93u)**、`/` +67px |
| 行尾 | 只有数字行 +1px（`i <= rem` 的 off-by-one） |
| 导航/数字区键帽 | 1.34u（应 1.00u） |

三个根因，改任何一条都要知道：

1. **每行自成封闭算式**（`aw = (MAIN_W - 固定键 - n*gap)/n`），行与行没有任何共享约束。
2. **行内 x 是累加出来的**（`mx += w + gap`），不是从列位算的 → 固定键一变宽，后面整排平移。
3. **`scaleX` 让 DIP 字面量语义失真**：`int wEsc = (int)(56 * dpiScale * scaleX)`，
   `scaleX = g_ww/980`。这些字面量是**为 980 宽的默认布局写的**，放到 1380 宽的全尺寸布局
   被放大 1.408 倍 —— 改「56」得到 79px。**这是「调不动」的根因**，网格化后这些字面量全部消失。

---

## 2. 单位账（ANSI 104，列位是唯一真源）

**主区每一行都必须是 15u：**

| 行 | 构成 |
|---|---|
| 数字行 | 13×1u + Backspace **2u** |
| Tab 行 | Tab **1.5u** + 10×1 + `[` `]` 2u + `\` **1.5u** |
| Caps 行 | Caps **1.75u** + 11×1 + Enter **2.25u** |
| Shift 行 | LShift **2.25u** + 10×1 + RShift **2.75u** |
| Ctrl 行 | 1.25×4 + 空格 **6.25u** + 1.25×4 |
| F 行 | Esc 1u + 空档 1u + F1~F12 12u + Del 1u |

导航区 **3u**、数字区 **4u**。全宽 = `22u + 2·gap`（隐藏数字区 = `18u + gap`）。

> 现状最容易做错的两个宽度：**右 Shift 应是 2.75u**（现状 1.35u）、
> **空格应是 6.25u**（现状 4.67u）、**`\` 应是 1.5u**（现状 0.92u）。

---

## 3. 算法（零累积漂移）

```cpp
double u = (double)(KEY_AREA_W - nblock * gap) / 22.0;   // 用 double，不截断

// 全局列位（单位 u）→ 像素 x；跨块补一个 gap
static int FullColX(float col, double u, int gap, const float* bc, int nb) {
    int b = 0;
    for (int i = 0; i < nb; i++) if (bc[i] <= col + 0.001f) b++;
    return KEY_AREA_X + (int)lround(col * u) + b * gap;
}
// 跨 sp 单位的键： x = FullColX(col)；w = FullColX(col+sp) - x - gap
```

**为什么一定对齐**：所有键边缘都取 `round(整数列 × u)`，任意两条边缘只差整数倍 u ± 1px，
误差不跨键累积。因此 **不需要 `rem`、不需要累加、不需要按行调参**。

⚠️ **`u` 千万别写成 `(KEY_AREA_W + gap) / 22` 再取整**：截断会把 11px 的余数整块丢掉，
最右一列直接短 11px。要么用 double，要么把块起点也纳入同一个 round 体系。

生成好的表 + 函数：本技能没有内置成品，按上面规则重写；也可以照
`hk_full_layout_unit.cpp`（在工作区里，由几何模型生成）抄 9 张表。

---

## 4. 行位（这块现状是对的，别乱动）

**导航区**（与 104 一致，`81f828c` 已正确）：
PrtSc/ScrLk/Pause 在 **F 行**、Ins/Home/PgUp 在 **数字行**、Del/End/PgDn 在 **Tab 行**、
↑ 在 **Shift 行**、←↓→ 在 **Ctrl 行**（Caps 行留空）。

**数字区必须 5 行，对齐主区的「数字行 ↔ Ctrl 行」**（现状错，占了 6 行）：
Num / * - 在数字行 → 789+ 在 Tab 行（+ 跨 Tab/Caps）→ 456 在 Caps 行 →
123Enter 在 Shift 行（回车跨 Shift/Ctrl）→ 0(2u) . 在 Ctrl 行。

**判据**：真键盘上 `↓` 与数字区 `0` 同在底行 → 数字区必然是「数字行 ↔ Ctrl 行」这 5 行；
NumLock 行因此对齐数字行（不是 F 行）。
现状把 NumLock 放在 F 行 → 数字区被拉成 6 行 → **回车底边停在「0 行之上整整一行」，0 与回车底不齐**。

腾空的 `4u × 1 行`（F 行右侧）**不是缺陷**：真键盘那里就是空位（指示灯 / 品牌 Logo）。
建议放品牌条（`main.ico` 的冷灰键盘标识），比留白更像刻意设计。

---

## 5. 网格化之后才会暴露的四处（配套必改）

| # | 问题 | 改法 |
|---|---|---|
| 1 | 导航键从 73px 收到 52px 后，`PrtSc`/`ScrLk`/`Pause` 14pt 下约 44px **撑破键帽**（可用仅 36px） | 加 `FitKeyFont()`：用现有 `g_f14b → g_f13 → g_f12` 阶梯，`MeasureTextW` 挑最小可用档，不新建 GDI 对象 |
| 2 | 窄屏自动隐藏数字区会**覆盖**用户按 Tab 的手动隐藏 | `g_npHidden`（手动、持久化）与 `g_npHiddenAuto`（宽度推导、不持久化）**分成两个状态**，生效值 = 或 |
| 3 | 改了隐藏状态主键盘**纹丝不动** | `KbFrameSig` 必须把生效的 `numpadHidden` 纳入 `memcmp` —— 否则帧缓存命中，看起来像「功能没生效」 |
| 4 | 数字区被自动隐藏后**没有入口叫回来** | `hm.numBtnVisible = g_showNumBtn && (g_layoutMode != 2 \|\| numpadHidden);`（现状是 `g_layoutMode != 2`，全尺寸下永久隐藏） |

---

## 6. 尺寸与自适应阈值

| 项 | 值 |
|---|---|
| 默认窗口（layout 2） | **1280×404**（1u ≈ 56.8px，键帽 52×52 正方） |
| 键高 | = `round(u) - gap`（正方形键帽），**不再由窗口高度反推** |
| 键帽 ≥44px（触摸安全线） | 全尺寸最小窗口宽 ≈ **1090 DIP** |
| 同上、隐藏数字区 | 最小 ≈ **870 DIP** |
| 宽度 < 1090 | 自动隐藏数字区 |
| 宽度 < 870 | `WM_GETMINMAXINFO` 拒绝继续缩小 |

现状 `WM_GETMINMAXINFO` 写死 `300×150` 且**不区分布局模式**，
全尺寸下缩到 300 宽时 `MAIN_W` 取 400 保底 → 整块溢出窗口、右侧被裁掉、无任何回退。

---

## 7. 验证配方（改完必做）

1. **复算**：`python scripts/hk_full_eval.py` → 每行键宽应完全相等、行尾右边缘应完全相等、
   q/a/z/m 偏差 ≤1px。
2. **渲染自检**：把两版几何按真实像素画进 HTML（键位用绝对定位 div），
   每 1u 拉一条竖线当参考网格 —— 网格落在键缝里 = 对了；穿在键身上 = 没对。
   用 Edge 无头截图确认：
   ```bash
   "/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" \
     --headless=new --disable-gpu --hide-scrollbars \
     --virtual-time-budget=6000 --window-size=1160,2400 \
     --screenshot="$TEMP/shot.png" "file:///<绝对路径>.html"
   ```
   ⚠️ **本机 Edge 无头截图不能写进工作区**（拒绝访问 0x5），必须写 `$TEMP`。
3. 真机（唯一能验的渠道仍是 CI 出包）：把窗口拖到 1090 与 800 两档，
   看数字区是否自动隐藏、标题栏按钮是否回来、键位有没有溢出裁切。
