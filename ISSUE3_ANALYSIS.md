# issue #3 排查档案

> 这份文档记录**已排除的假设**和**关键证据**，避免后人重走弯路。
> 每条结论都有实测或代码依据，不是推测。

## 现象（issue #3 原文）

- Win10（19045.7725）+ 微软拼音，**用 HKeyboard 打不出中文**
- 同机同输入法：**系统键盘正常**
- **微信拼音正常**、**Win11 正常**、**Win7 正常**
- 用 HKeyboard：**资源管理器搜索框能打中文**（关键对照，见下）

---

## 关键对照数据（决定性）

2026-10-03 用户实机确认：

| 目标 | 控件 / 路径 | 用 HKeyboard 打中文 |
|---|---|---|
| **原版 Win32 记事本** | 标准 `Edit`，IMM32 | ❌ **失败** |
| Edge 地址栏 | Chromium 自绘，TSF/UIA | ❌ 失败 |
| **资源管理器搜索框** | 标准 `Edit`，IMM32 | ✅ **成功** |

### 这组数据推翻了什么

- 推翻「控件类型决定」：原版记事本和资源管理器搜索框**都是标准
  `Edit`、都走 IMM32**，结果相反。
- 推翻「TSF vs IMM32」：失败的两个里既有 TSF（Edge）也有 IMM32（记事本）。
- 推翻「时序 / 节奏」：30ms 字母间隔实测**无效**（已 revert `ed13b09`）。
- 推翻「焦点探测」：`GetFocusedInputControl` 只有 2 个调用点，
  **都不在按键发送路径上**（`UserHideKeyboard` / `UpdateAutoVisibility`）。
  显隐 ≠ 能否打出中文。

**结论：问题出在更基础的地方 —— 注入的按键本身。**

---

## 已确认的根因：`wScan` 一直是死字段

### 机制

Win32 语义：**`INPUT_KEYBOARD` 的 `dwFlags` 不含 `KEYEVENTF_SCANCODE`
时，系统只认 `wVk`，`wScan` 被完全忽略。**

而本文件的 `SendKey` / `SendKeyGap` 里到处是：

```cpp
i.ki.wScan = (WORD)MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
i.ki.dwFlags = ext;              // ← 缺 KEYEVENTF_SCANCODE
```

于是这个 `wScan` **从来没有生效过**。文件顶部注释写的
「扫描码走 `MapVirtualKeyW(MAPVK_VK_TO_VSC)` —— 部分 IME 依赖正确扫描码」，
描述的事情**一件都没发生**。

### 为什么这能解释现象

TSF 前端从 `WM_KEYDOWN` 的 `lParam` 读扫描码（bit 16~23）与扩展键标志
（bit 24）判断按了哪个物理键。只给 `wVk` 时系统要自己反推扫描码，
在 IME 激活态 / 非美式布局下反推结果可能与真键盘不同
⇒ IME 认不出这串按键 ⇒ 不组字、不上屏。

### 最有力的佐证：同文件里能工作的路径都设了

```cpp
ToggleImeLang()   // 右 Shift 切中英 —— 工作正常
    in.ki.dwFlags = KEYEVENTF_SCANCODE;
SendWinToggle()   // Win 键开关开始菜单 —— 工作正常
    in.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
```

偏偏最常走的 `SendKey` 漏了。这与 issue 现象高度吻合：
**中英切换能用，字母组字不能用。**

资源管理器搜索框那个字段大概走的是 IMM32 兼容层（另一条路径），
所以不受影响 —— 这解释了它为什么是唯一能用的那个。

### 状态

已修（`SendKey` 8 处 + `SendKeyGap` 8 处），**待用户实机验证**。
故意不动 `SendWinToggle` / `ToggleImeLang`（已正确，且不涉 IME 组字）。

### 若仍失败的下一步

查 `MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)` 在**中文 IME 激活态**下返回的
扫描码是否需要先切换键盘布局再取 —— `VK_TO_VSC` 的结果与当前布局相关。

---

## 已排除的假设（附排除依据）

### 1. 焦点探测误判（用户最初怀疑的方向）
- `GetFocusedInputControl` 只有 2 个调用点：`UserHideKeyboard()`、
  `UpdateAutoVisibility()`。按键路径（`DoKeyAction → SendKey`）里没有它。
- 偏差方向是「该藏却没藏」（`Chrome_WidgetWin` **无条件**返回 TRUE，
  焦点在标签栏/菜单/空白页也算有输入焦点），**与「弹不出来」相反**。
- 确实存在的两个真缺陷（只影响显隐，不影响输入）：
  - `GetGUIThreadInfo` 失败时 `focus = fg`（顶层窗口），通常匹配不上白名单
  - UWP `Windows.UI.Core.CoreWindow` 无类名可匹配，只能靠 COM 兜底

### 2. 注入时序太紧（字母间隔）
- 假设：`DoKeyAction` 的 `K_LETTER` 字母之间零延时，候选窗来不及更新
- 实测：**加 30ms 无效**，已 revert
- 附带纠正一个认知：**`Sleep(1)` 并不是只睡 1ms**。默认计时器分辨率
  15.6ms，1ms 的 Sleep 会挂起到下一个时间片 ≈15ms。所以「1ms 折中」
  这个说法本身就是错的（它把「单键内 down↔up 间隔」和
  「字母间间隔」混成了一件事）。

### 3. 低层钩子误吞
- 显式跳过 `LLKHF_INJECTED` + 无条件 `CallNextHookEx`

### 4. 代码抢焦点
- 全文唯一的 `SetFocus` 在设置页色块编辑

### 5. STA COM 初始化
- 日志确认 `apartment AFTER pump=MAINSTA (3)`

### 6. `WS_EX_NOACTIVATE`
- v1.1.1 同样带此样式，而 v1.1.1 无此问题

### 7. 段内 down/up 一次性灌完
- 已由 `KEY_INJECT_GAP_MS` 分段解决；本轮修复的正是配套的 SCANCODES

### 8. 1ms 分段注入
- 历史上试过并回退；本轮证明真问题是 SCANCODES 而非间隔大小

---

## 排查这条 issue 沉淀的通用铁律

1. **用「当前列表里没有」推断「不存在」** —— 探针曾把「Edge 没运行」
   误报成「没装 Edge」（Win10 预装 Edge）。进程/窗口列表只能证明
   「没在跑」。

2. **「总数够」不等于「结构够」** —— 探针门槛写成 `nTgt < 2`，
   结果启动了一个浏览器就收工，没有对照组，跑完什么也没证明。

3. **判定不能信任自己的意图，要验证客观事实** —— 光看「我启动的是
   `notepad.exe`」就当对照组；实际那台机器的 `notepad.exe`
   **已被替换成 Notepad4**（窗口类 `Notepad4`、焦点 `Scintilla`），
   是故障环境本身。

4. **判定的根本前提是「读回内容包含我们打进去的东西」** —— 缺这个前提时，
   「有中文 ⇒ 中文上屏了」是循环论证。`Ctrl+A` 在自绘控件上会选中整页，
   于是读到页面上原有的中文。

5. **「读不到值」与「值就是空」是不同状态** —— 混淆会误杀空输入框
   （空字段上按 `Ctrl+C` 剪贴板本来就是空的）。

6. **先确认某段代码是否真在故障路径上，再讨论它有没有 bug** ——
   假设往往指向真实缺陷，但**归因**可能错位。

7. **同一个文件里"能工作的路径"是最快的线索来源** —— SCANCODES 这个根因
   就是靠对比 `ToggleImeLang`（正常）与 `SendKey`（异常）发现的。

---

## 备查：环境的一个坑

`%SystemRoot%\system32\notepad.exe` **已被替换成 Notepad4**
（窗口类 `Notepad4`、焦点在 `Scintilla`）。

做对比实验时**不能拿它当对照组** —— 它本身就是故障环境。

---

## 已撤回的诊断代码

2026-10-03 撤回全部 IME 探针（17 个提交、约 +1400 行）。
代码保留在分支 `backup-probe-work`。
`hkeyboard.cpp` 中 `imeprobe` / `ProbeFmt` / `RunImeProbe` / `ProbeLog` /
`WriteStartupTrace` 全部 0 残留，产物从 439296 B 降到约 400 KB。
