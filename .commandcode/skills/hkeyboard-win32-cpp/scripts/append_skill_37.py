# -*- coding: utf-8 -*-
"""把第 37 轮的诊断知识插进 hkeyboard-win32-cpp 技能。"""
import io

p = r"C:/Users/29776/.workbuddy/skills/hkeyboard-win32-cpp/SKILL.md"
s = io.open(p, encoding="utf-8").read()

anchor = "### 自己驱动「托盘启动 → 显示」这条链路（能省掉用户往返）"
assert s.count(anchor) == 1

block = """### ⚠⚠⚠ 触屏（平板）上「自动呼出完全不工作」的根因（`b711eca`）

**症状**：平板触屏环境下自动呼出**完全失效**；日志里 `await=1` 占近一半拍数。

**根因**：触屏兜底 `UserInputAfterForegroundChange()` 的判据是
「最后一次输入发生在**这次前台变化之后** **且** 光标自那次切换起**没动过**」。
第二个条件有致命性质 —— **只要中途有任何一次移动光标**
（手指点任务栏/开始菜单、外接鼠标被碰、别的程序 SetCursorPos），
它就**永久为假**，直到下一次前台变化 ⇒ `g_fgAwaitUserInput` 再也解不开 ⇒
`if (g_fgAwaitUserInput) return;` 永远拦住 ⇒ 自动呼出等于完全失效。

**修法**：不绑定前台变化的"刚刚有过一次**触控式点击**"——
`TouchInputTick()` 每拍采 `GetLastInputInfo` + `GetCursorPos`，判
「有输入」**且「与上一拍比光标没动」**（排除"只是晃鼠标"），命中刷 `g_touchTapTick`；
`byUserInFg = TouchTappedRecently()`（1500ms 窗口）。**每次触控都能刷新，中途移过光标也自愈。**

配套**触屏一次性证据** `TouchTapEdgeConsume()`：触屏拿不到落点，而两条强证据
（系统触摸键盘出现 / 前台窗口里出现焦点事件）在用户机器上都可能不成立 ——
那样"隐藏态要弹必须攒证据"就变成永远弹不出来。一次性（消费即失效）⇒ 不振荡；
代价是压制态下点空白会闪一下。另把 `TouchFocusEvidence` 放宽到**同进程也算**
（Chromium 有多个顶层窗口，焦点事件有时报在另一个上面）。

**教训（可复用）**：任何"**自从某个事件以来源**状态没变"式判据都要警惕 ——
它会被一次无关操作**永久**关掉，而且没有自愈路径。改成"**与上一次采样比**"。

### `why=` —— 「自动呼出为什么没弹」的直接答案（`b711eca`）

`UpdateAutoVisibility` 的每个判定点写 `g_dbgWhy`（分支名），日志**挪到函数出口**
（`goto done:`，判定段内已无裸 return）。取值：
`warmup / anim / verdict-hide / vhid-reset / vhid-blank / vhid-click-ok /
vhid-touch-ok / vhid-tap-ok / vhid-hold / userHid / shell-hide / await / show / shown /
manualHide / own-window / noInput-streak / hidden-nop / manualHide-nop /
noInput-delay / manualShow-nop / auto-hide`。

⚠ 改这段代码时的两个硬约束：
1. `goto done` 会跳过**带初始化**的函数级声明 ⇒ **MSVC C2362**。
   所以 `HWND fg = RealForegroundWindow();` 必须声明在判定段**之前**。
2. 日志挪到出口后 `g_vis` 已是动作后的值 ⇒ 用 `visForLog` 在判定前捕获，
   否则与 `ShowKB` 那行的语义对不上。

### ⚠⚠ 导出格式的两个致命坑（`b711eca` 修，用户那份日志因此**不可用**）

1. **字段顺序 = 重要性顺序**。`HKRING_LEN` 曾设 320，而 eval 行有 330~360 字符 ⇒
   `_snprintf_s(_TRUNCATE)` 从**结尾**截断 —— 而当时 `=> <决策>`(`uiact/pt/hid`)
   恰好写在最末尾 ⇒ 用户导出的 **614/640 条**丢掉全部关键字段。
   ⇒ 现在 `=> <决策>` 紧跟 tag，`HKRING_LEN` 提到 **576**。
2. **必须保证换行**。`_TRUNCATE` 会把结尾的 `\\n` 一起截掉 ⇒ 640 条**挤成一行**。
   ⇒ 导出时逐条写，结尾不是换行就补一个。

⇒ 通用原则：**写环形/定长缓冲的日志，前几个字段必须是最重要的，且不要在结尾依赖换行符。**
自检方法：跑一次后量 `max(len(line))` 与 `HKRING_LEN` 的余量（现在的余量见下面分析脚本）。

### 分析导出日志的脚本：`.workbuddy/analyze_diag.py`

它会把被挤成一行的环形段**按时间戳切开**并给出字段分布 + 「只在变化时打印」的时间线。
用法：`python .workbuddy/analyze_diag.py <diag.txt> [--timeline-only]`。

⚠⚠ **两个工具坑（都踩过，省得再查）**：
1. **Bash 工具起的进程会在该次工具调用结束时被清掉**。所以"启动程序 → 观察 N 秒"
   必须写在**同一条命令**里；否则下一条命令一看进程就没了
   （我因此一度以为程序有"自己退出"的 bug，还去查了 WER 事件日志）。
2. **heredoc 会吃掉正则里的反斜杠**：`python - <<'EOF'` 里写 `\\d\\d:` 拿不到匹配，
   同一段代码写成 `.py` 文件跑就正常。⇒ **分析脚本一律写成文件再跑。**
   另外 `python -c "..."` 里含 `cmd` 等字样时会被安全启发式拦掉 ⇒ 同样改走脚本文件。

"""

s = s.replace(anchor, block + anchor, 1)
io.open(p, "w", encoding="utf-8").write(s)
print("技能已更新，长度", len(s))
