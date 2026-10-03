# 焦点探测 vs 注入时序 —— 对 issue #3 的归因

## 结论先说

你怀疑的 `check_is_input_focused`（本文件里对应 `GetFocusedInputControl`）
**不是 issue #3 的原因**，但你指出的「类名白名单不可靠」这个问题**确实存在**
—— 只是它的表现方向和你担心的相反。

---

## 一、焦点探测的影响面（已用代码核实）

`GetFocusedInputControl` 全文件只有 2 个调用点：

| 行号 | 位置 | 作用 |
|---|---|---|
| 3182 | `UserHideKeyboard()` | 只记一个 token，供「同一输入框内不回弹」用 |
| 5965 | `UpdateAutoVisibility()` | 只管键盘显隐（`ShowKB`） |

**按键发送路径（`DoKeyAction` → `SendKey`）里完全没有它。**

所以：显隐 ≠ 能不能打出中文。issue #3 的现象是「键盘已显示、
但打不出中文」，焦点探测不在这条链路上。

### 而且它的偏差方向是「该藏却没藏」，不是「该弹却不弹」

```cpp
// IsInputControl() 对 Chromium 无条件返回 TRUE
if (strstr(buf, "Chrome_RenderWidgetHostHWND") || strstr(buf, "Chrome_WidgetWin"))
    return TRUE;
```

焦点在Edge 的**标签栏、菜单、页面空白处**时也算「有输入焦点」⇒
键盘一直显示不收。这是**假阳性（过度触发）**，与「键盘弹不出来」相反。

确实存在的两个真缺陷：

1. `GetGUIThreadInfo` 失败时直接拿顶层窗口当焦点：
   ```cpp
   HWND focus = haveGuiInfo && gi.hwndFocus ? gi.hwndFocus : fg;
   ```
   顶层窗口通常匹配不上白名单 ⇒ 误判为「无输入焦点」⇒ 键盘不弹。

2. UWP（`Windows.UI.Core.CoreWindow`）无类名可匹配，只能靠
   `IsAccessibleInputWindow` 的跨进程 COM 调用兜底 —— 而这条路径
   被 `IsShellSurfaceClass` 里的 `XamlExplorerHost` 等条目部分挡住。

**这两条影响的是「键盘显不显示」。** 如果你在 Win10 上遇到的是
「键盘根本不弹」，那确实可能是这里的问题；但那是另一个症状。

---

## 二、真正的嫌疑：注入时序（`SendKey`）

issue #3 里最有价值的一条证据：

> 同一台 Win10、同一套微软拼音，**资源管理器搜索框能打中文，
> Edge 地址栏和记事本里不能**。而系统键盘在所有地方都正常。

这说明**注入通路是通的**（英文/数字全部正常），问题在**按键节奏**。

### 当前时序

```cpp
#define KEY_INJECT_GAP_MS 1     // 2277 行
```

`SendKey` 分4 段：修饰键 down → Sleep(1) → 主键 down → Sleep(1)
→ 主键 up → 修饰键 up。

**而 `DoKeyAction` 里字母之间完全没有延时**（2551 行起）。
所以实际是零间隔连发：

```
[n.down][1ms][n.up] → 立即 [i.down][1ms][i.up] → 立即 [h.down]...
```

### 为什么这会让微软拼音丢字

中文组字需要连续 5 个字母（n-i-h-a-o）累积成拼音串。微软拼音的
TSF 前端要在每次 `WM_KEYDOWN` 之后跑一轮自己的消息循环，才会更新
候选窗。真键盘是**物理**按下/抬起，天然有 50~100ms 间隔；我们注入时
字母间隔≈0，候选窗来不及更新，整串被丢掉 —— 不上屏、不出候选窗。

这与issue #3 的现象完全吻合：

| 目标 | 路径 | 对时序敏感度 |
|---|---|---|
| 资源管理器搜索框 | 标准 `Edit` + IMM32 | 宽容 ⇒ 能打 |
| Edge 地址栏 | Chromium 自绘 + TSF/UIA | 敏感 ⇒ 不能打 |
| Notepad4 | Scintilla 自绘 | 敏感 ⇒ 不能打 |
| 微信拼音 | 实现更宽容 | 正常 |
| Win11 / Win7 | TSF 更活跃 / 路径更传统 | 正常 |

原注释里那句「1ms 是足够让出消息循环与不拖慢连发的折中」
—— **这个折中算错了**：它只解决了「单键down/up 之间」的间隔，
没解决「逐个字母之间」的间隔。

---

## 三、不需要探针的验证方法

### 实验 A：手动验证「时序假设」（5 分钟，零改动）

在 Win10 虚机上：

1. 切到**微软拼音**中文态
2. 用系统自带输入法候选框（`Win + 空格` 切到微软拼音），
   在 Edge 地址栏**手动一个一个慢慢敲** n-i-h-a-o
   —— 中间停顿明显 ⇒ 应该能出候选窗 ⇒ 证实「时序敏感」
3. 再用**搜狗/微信拼音**同样手动敲
   —— 对比是否更宽容

若第2 步能出候选窗而HKeyboard 不能，时序假设成立。

### 实验 B：直接把gap 调大（一次改动，可回滚）

把 2277 行的

```cpp
#define KEY_INJECT_GAP_MS 1
```

改成

```cpp
#define KEY_INJECT_GAP_MS 12
```

并**额外**在 `DoKeyAction` 的 `K_LETTER` 分支里，
每次发送后加一个字母间隔（见下方建议实现）。

注意：`Sleep(1)` 在 Windows 上实际会挂起到 ~15ms 的时间片
（默认计时器分辨率 15.6ms），所以 1ms 已经≈15ms；
改成 12 会真的变成 ~15ms+ 的间隔，效果比想象中明显。

代价：长按连发会变慢（`TIMER_REPEAT` 是 350ms 一个 tick，
实际影响有限）。若嫌慢，可只给**字母**加间隔，
功能键/导航键保持 1ms。

---

## 四、建议的实现（若实验 B 证实假设）

```cpp
// 字母之间的组字间隔：微软拼音的 TSF 前端需要这个时间来更新候选窗。
// ⚠ 与 KEY_INJECT_GAP_MS 是两件事：
//    KEY_INJECT_GAP_MS = 单键内部 down↔up 的间隔
//    KEY_LETTER_GAP_MS = 相邻两个字母之间的间隔
//    后者才是「组字能否累积」的决定因素。
#define KEY_LETTER_GAP_MS 30
```

在 `DoKeyAction` 的 `K_LETTER` 分支，`SendKey` 之后加：

```cpp
Sleep(KEY_LETTER_GAP_MS);
```

真键盘典型按键间隔是 50~100ms，30ms 是偏保守的下限。

---

## 五、附带说明：本机环境有个坑

演练时发现：**本机 `%SystemRoot%\system32\notepad.exe` 已被替换成
Notepad4**（窗口类 `Notepad4`、焦点在 `Scintilla`）。

所以在这台机器上，「用系统记事本当对照组」是**不成立的** ——
它恰恰是故障环境本身。这一点在做任何对比实验时都要注意。
