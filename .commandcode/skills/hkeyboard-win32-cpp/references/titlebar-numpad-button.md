# 标题栏「小键盘」按钮 = 数字区开关

适用：改 `GetHeaderMetrics()` / `DrawHeaderPill()` / `HDR_NUM` 点击，或任何
「**标题栏放一个能反映并切换某块界面状态的开关按钮**」的活。

---

## 0. 一句话

**按钮和它要控制的东西，在 HKeyboard 里一直是两套互不相干的逻辑。**
`HDR_NUM` 只碰 `g_layoutMode`，数字区显隐只归 Tab 键碰 `g_npHidden` ——
所以"按钮点了没反应"和"状态看不出来"是同一个病的两种症状。
解法是**先把两条路径并到一个函数**，再谈外观。

---

## 1. 现状的三条事实（`81f828c`）

| 事 | 位置 | 原值 |
|---|---|---|
| 按钮可见性 | `GetHeaderMetrics()` | `hm.numBtnVisible = (g_showNumBtn && g_layoutMode != 2)` —— **全尺寸下恒 FALSE** |
| 按钮点击 | `OnLDown()` 的 `case HDR_NUM` | `g_layoutMode != 1` → 存 `g_prevLayout` 后切到 1；否则切回。**只碰 `g_layoutMode`** |
| 数字区显隐 | `K_SPECIAL` 里 `k->vk == 0x09` | 仅当 `g_layoutMode == 2 && g_npTabToggle` 时 `g_npHidden = !g_npHidden` 并落盘。**只碰 `g_npHidden`** |

后果：全尺寸布局下**按钮不存在、数字区没有任何界面开关、看不出它是开是关**。

---

## 2. 目标状态机（三种布局，语义收敛到「小键盘开着吗」）

```cpp
hm.numBtnVisible = g_showNumBtn;      // 去掉 && g_layoutMode != 2
```

| 当前布局 | 点击行为 | 激活态 |
|---|---|---|
| 默认 `mode 0` | 记住 `g_prevLayout`，切到独立小键盘 `mode 1` | 否 |
| 独立小键盘 `mode 1` | 切回 `g_prevLayout` | **是** |
| 全尺寸 `mode 2` | `g_npHidden = !g_npHidden` | **数字区可见时 = 是** |

```cpp
static BOOL NumBtnActive() {
    if (g_layoutMode == 1) return TRUE;              // 独立小键盘布局
    if (g_layoutMode == 2) return !g_npHidden;       // 全尺寸：数字区可见
    return FALSE;
}
```

> ⚠️ `mode 1` 的"切回"路径**不能删** —— `g_prevLayout` 是 430 宽小键盘布局的**唯一出口**，
> 删掉用户就锁死在里面了。

---

## 3. 激活态：零新增令牌

HKeyboard 里 **`C_HOT`（= `primary`）和 `C_ON_PRIMARY`（= `primaryFg`）早就在用**（Enter 键），
开关按钮的激活态直接拿这两个，**不要给 `HkbTokens` 加字段**。

```cpp
DWORD bg, fg;
if (active) {
    bg = hov ? BlendColor(C_HOT, RGB(255, 255, 255), 0.12) : C_HOT;
    fg = C_ON_PRIMARY;
} else {
    bg = hov ? C_REGULAR_HOV : C_REGULAR;
    fg = C_BTN_CONTENT;
}
```

### 实测对比度（hue 250）

| 状态 | 底 / 前景 | 浅色 | 深色 |
|---|---|---|---|
| 普通 | `btnRegularBg` / `btnContent` | 4.18:1 | 5.50:1 |
| 普通 · 悬停 | `btnRegularBgHover` / `btnContent` | 3.60:1 | 4.53:1 |
| **激活** | `primary` / `primaryFg` | **6.03:1** | **5.58:1** |
| **激活 · 悬停** | 主色**向白**混 .12 / `primaryFg` | **6.84:1** | **6.17:1** |
| 激活 · 悬停（反例） | 主色**向 `cardBg`** 混 .12 / `primaryFg` | 6.04:1 | **4.54:1 ✗** |

两条容易想反的：

1. **激活态比普通态还清楚**（浅 6.03 vs 4.18）。因为 `primaryFg` 是纯文字色（`text`），
   而 `btnContent` 是带色相的蓝。别担心"实底会压掉可读性"。
2. **激活悬停必须向白混，绝不能向 `cardBg` 混。** 深色主题的 `cardBg #1E1B23` 是**暗色**，
   向它混会把主色压暗到 `#5AA1E5`，而 `primaryFg` 是深蓝 → 掉到 4.54:1（踩 AA 线）。

> **附带认知**：浅色下 `btnRegularBg #E2F0FF` 与 `pageBg #EAEFF5` 的亮度比是 **1.00:1**
> —— 普通态按钮在页头里本来就几乎隐形，只靠色相差区分。
> 所以激活态的 2.30:1 是要跟"1.00:1 的隐形底"比，不是跟一个可见的深底比，感知是够的。

---

## 4. 窗口加宽：闭式解 `700 × dpi`

判据是 `BuildComplete()` 里的保底：

```cpp
int MAIN_W = KEY_AREA_W - restBlocks;
if (MAIN_W < 400) MAIN_W = 400;      // ← 这句一触发，数字区就被推出窗口了
```

> **顺手修的 DIP 漏乘**：`400` 是**物理像素**，而 `restBlocks` / `KEY_AREA_W` 都按 DPI 缩放了。
> 200% DPI 下 400 像素只有 200 DIP。应改 `400 * dpiScale`。

推导（`keyAreaX = 10·dpi`、`navW = 52·ww/980`、`gap = 4·ww/980`）：

```
ww − 2·10·dpi − 7·navW − 7·gap ≥ 400·dpi
ww · (1 − 7·52/980 − 7·4/980)  ≥ 420·dpi
ww · 0.6                       ≥ 420·dpi      →   ww ≥ 700·dpi
```

```cpp
int needW = (int)(700.0 * GetSystemDpiScale()) + 8;   // +8 吃 int 截断
if (g_ww < needW) { g_ww = needW; SetWindowPos(...); }
```

实测（dpi=1）：**500 宽溢出 116px、600 宽溢出 51px、700 宽恰好**、1380（默认）本就够。
→ **默认尺寸下点按钮不会有任何尺寸变化**，加宽只在用户先把窗口拖窄后才发生。

两个陷阱：

1. **顺序**：`needW` 依赖 `g_keyGap` / `g_keyAreaX`，而这两个在 `BuildKeys()` 里由 `g_ww` 反算。
   **先算 `needW` → 再改 `g_ww` → 最后 `BuildKeys()`**。用闭式解就是绕开这个循环。
2. **隐藏时不要缩回窗口** —— 用户可能刚把窗口拉宽另有用途。只加宽，不缩窄。

---

## 5. 必须同改的清单

1. `GetHeaderMetrics()` —— 去掉 `!= 2`
   （`xNum`/`xTitle`/`wTitle` 会自动跟着走；**`HitHeader()` 不用动**，它读同一份结构）
2. `DrawHeaderPill()` 加 `BOOL active` 参数（「设置」恒传 `FALSE`）
3. **新增** `NumBtnActive()`
4. **新增** `SetFullNumpadHidden(HWND, BOOL)` —— 按钮与 **Tab 键都调它**
5. **`KbFrameSig` 加 `npHidden` 字段** ← 现在**没有**。目前靠"数字区 17 个键消失 → `nk` 变"
   侥幸触发重绘；显式加上才不依赖这个巧合
6. `BuildComplete()` 的 `400` → `400 * dpiScale`
7. 设置页两行文案（`S_HIT_NPBTN` / `S_HIT_NPTAB`）—— 语义变了，旧文案会误导

---

## 6. 验收

- [ ] 三种布局下按钮**都可见**；默认布局点它切小键盘、再点切回
- [ ] 全尺寸下点它显示/隐藏数字区，**与按 Tab 键逐像素一致**
- [ ] 数字区可见时按钮是**主色实底**，隐藏时回到中性浅底
- [ ] 独立小键盘布局下按钮**也是激活态**
- [ ] 默认 1380 宽下点按钮，**窗口尺寸一动不动**
- [ ] 窗口拖到 600 宽再点"显示数字区" → 窗口自动加宽到 ≥700，数字区完整
- [ ] 隐藏数字区后窗口**不自动缩窄**
- [ ] 切换后主键盘**立刻重绘**（不动 → `KbFrameSig` 漏了 `npHidden`）
- [ ] 关掉设置里「小键盘按钮」后按钮消失，**Tab 键仍能切数字区**

---

## 7. 可复用资产

| 文件 | 作用 |
|---|---|
| `scripts/npbtn_metrics.py` | 激活态对比度 + 加宽临界宽度，全部按真实公式复算 |
| `scripts/titlebar_numpad_page.py` | 对照页生成器。其中 **`load_icons()` / `icon_svg()` 是通用的 `hk_icons_generated.h` → SVG 预览器**，可单独拿去验证任意图标（改 `Gear/Numpad/Minimize/Close` 那几个名字即可） |

> `hk_icons_generated.h` 的解析规则：`HKIC_BEGIN` 开一条子路径
> （`a`=0 描边/1 填充、`b`=描边宽、`c`=颜色槽），`MOVE`/`LINE`/`CUBIC`/`CLOSE` 逐节点转 SVG。
> 描边一律 `stroke-linecap/linejoin="round"`（对应 GDI+ 的圆头圆角）。
> 网格边长在文件末尾的 `{ k_icon_Xxx, 24.0f }` 表里。
