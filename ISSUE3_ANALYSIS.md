# issue #3 排查档案

> 这份文档记录**已排除的假设**和**关键证据**，避免后人重走弯路。
> 每条结论都有实测或代码依据，不是推测。

## ✅ 最终结论（2026-10-04 已修复并实机验证）

**根因：微软拼音（Win10）在「鼠标左键按下期间」拒绝处理注入的键盘事件。**

`OnLDown` 是在 `WM_LBUTTONDOWN` 的处理过程中调用的，此刻鼠标左键必然处于
按下状态 —— 于是紧接着注入的按键不被 IME 组字，直接落成英文字母。
`-sendtest` / `-envtest` 之所以一直"通过"，是因为它们用 **`SendInput`
注入的鼠标**点击，而**注入的鼠标按下不会被系统记为"按下"**，
`GetAsyncKeyState(VK_LBUTTON)` 仍是 0。

### 证据（diag.txt，三类场景对比注入那一刻的系统状态）

| 场景 | tag | LBTN | 其余状态 |
|---|---|---|---|
| 用户**真实**鼠标点击按键 | `[before]/[after]` | **1** | fg=Notepad、focus/caret/hkl 全同 |
| **注入**鼠标点击（envtest 阶段2） | `[before]/[after]` | **0** | 同上 |
| 定时器注入（envtest 阶段1） | `[env-bef]/[env-aft]` | **0** | 同上 |

**唯一差异就是 LBTN。** 最有说服力的一点：同样是 `[before]/[after]` 两组，
一组 LBTN=1、一组 LBTN=0。

### 修复
`DoKeyAction` 开头统一调用 `WaitForLeftButtonUp()`：若左键按下就**泵消息
等它抬起**（上限 250ms）。空格同样受益（它负责把候选框里的中文上屏）。

三个易踩的点：
1. **必须 `PeekMessage` 泵消息，不能死 `Sleep`** —— `OnLDown` 在
   `WM_LBUTTONDOWN` 处理中，`WM_LBUTTONUP` 还得靠消息循环派发，
   死等必然一路耗到超时。
2. **防重入**（`g_inWaitLButton`）—— 泵消息会派发 `WM_LBUTTONUP` → `OnLUp`。
3. **`WM_QUIT` 要投回去** —— `PeekMessage` 取走它而 `DispatchMessage`
   不处理，不补这一下程序再也收不到退出信号。

长按连发用 `g_inRepeat` 跳过等待（连发时用户一直按着鼠标，等只会每次
耗满超时；且连发是重复同一字符，不走组字）。

### 这条规则解释全部现象
| 现象 | 解释 |
|---|---|
| 手动点击必失败 | 注入时鼠标必然按下 |
| `-sendtest` / `-envtest` 必成功 | 无真实鼠标按下 |
| 物理键盘不受影响 | 不涉及注入 |
| 点标题栏无影响 | 不触发按键注入 |
| Win11 / Win7 / 第三方输入法正常 | 对鼠标状态不敏感 |

### 诊断工具保留在 `#ifdef HK_DIAG` 下
`-sendtest` / `-envtest` / `-noscapture` / `-norepaint` / `-diag`
（含 `RunSendTest`、`EnvTestInject`、`DiagSnap` 等）全部保留在源码里，
但**正式版一律不定义 `HK_DIAG`，这些代码不参与编译**。
需要排查时编译命令加 `/DHK_DIAG`。

---

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

**待用户实机验证**（2026-10-03 23:08 那版）。

### ⚠ 补 SCANCODES 后暴露的旧错误（左/右 Shift）

用户实测：补 SCANCODES 后，**Win10 记事本里按 Shift 切不了中英文**。

**这不是 SCANCODES 引入的新 bug，而是它暴露了原先被掩盖的错误：**

```
MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC)  ->  0x2A（**左** Shift）
补上 SCANCODES 后，系统按扫描码反查 VK：0x2A -> VK_LSHIFT
而微软拼音的切换键默认绑定在**右 Shift** ⇒ 发出去的是左 Shift，IME 不认
```

不设 SCANCODES 时，系统只认 `wVk = VK_SHIFT`（通用 Shift，左右皆可触发），
**把左/右这个错误掩盖了**。补上扫描码后原错显形。

同文件 `ToggleImeLang` 一直用 `VK_RSHIFT` / 0x36（正确的），
两者本来就不一致，只是没人同时读过这两处。

已修：`SendKey` 第 1/4 段 + `SendKeyGap` 两处，Shift 一律用
`VK_RSHIFT` + **硬编码 `wScan = 0x36`**，且不带 `KEYEVENTF_EXTENDEDKEY`
（右 Shift 不是扩展键）。按下与抬起必须成对。

顺带修掉：`SendKeyGap` 有一处**连 `dwFlags` 都没设**（默认 0），
是全局唯一一处漏设标志位的地方。

### `KEYEVENTF_SCANCODE` 的准确语义（Microsoft Learn）

> **KEYEVENTF_SCANCODE (0x0008)**: If specified, **wScan identifies the key
> and wVk is ignored.**

⇒ 同时设 `wVk`/`wScan` 是**有定义的**（`wVk` 被忽略），不是未定义行为。

同页另一句，点明了**该用扫描码的真正理由**：

> The virtual key value of a key can change depending on the current keyboard
> layout or what other keys were pressed, **but the scan code will always be
> the same.**

⇒ 理由是「**VK 随布局变，扫描码恒定**」。

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

8. **当一个"修复"暴露出新症状，先怀疑它暴露了旧问题，别急着回退** ——
   补 SCANCODES 后 Shift 切换失效，看起来像新引入的回归，实际是原先
   `wVk = VK_SHIFT`（通用 Shift，左右皆可触发）把左/右这个错误掩盖了。
   回退只会让旧错误继续隐身。

9. **相邻代码的"不一致"是最值钱的线索** —— 同一功能的另一处实现
   （`ToggleImeLang` vs `SendKey`）用了不同的 VK。通读单个函数永远看不出来，
   只有横向对比才暴露。修 bug 时主动找"同一功能的另一处实现"做对照。

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
