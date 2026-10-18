# -*- coding: utf-8 -*-
"""把第 37 轮记录追加到工作区日志。"""
import io
import os

base = r"C:/Users/29776/WorkBuddy/Worktrees/Hydrogen-Keyboard/main-4cee7061/.workbuddy/memory"
p = os.path.join(base, "2026-10-10.md")
s = io.open(p, encoding="utf-8").read()
s += """

---

## 第 37 轮（2026-10-10 深夜）：分析 Issue #4 用户（**平板触屏**）的导出日志

用户转来一份别的机器上的 `HKeyboard_diag_20261010_220004.txt`，并补充关键信息：
**那台机器是平板触屏环境**。本轮核心是「日志本身有缺陷 → 先修日志 + 补日志」。

### ⚠⚠ 我上一版导出的日志有**两个致命缺陷**（必须先修，否则拿到也没用）

1. **614/640 条记录被截断**。`HKRING_LEN=320`，而 eval 行有 330~360 字符 ⇒
   `_snprintf_s(_TRUNCATE)` 把**结尾**截掉 —— 而 `=> <决策>`（`uiact/pt/hid`）
   恰好写在最末尾 ⇒ **最关键的字段全丢**，那份日志对"为什么没弹"完全无用。
   ⇒ ① 关键字序重排：`=> <决策>` 紧跟 tag；早期 a11y 探索留下的
        `fvt/frole/fev/xms/xok` 挪到末尾（丢了不致命）；② `HKRING_LEN` 320 → 576。
2. **640 条挤成一行**。`_TRUNCATE` 把结尾的换行一起截掉 ⇒ 没有换行。
   ⇒ 导出时逐条写，结尾不是换行就补一个。

### 平板触屏「自动呼出完全不工作」的**根因**

`UserInputAfterForegroundChange()` 的判据是「最后一次输入发生在**这次前台变化之后**
**且** 光标自那次切换起**没动过**」。第二个条件有致命性质：
**只要中途有任何一次移动光标**（手指点任务栏/开始菜单、外接鼠标被碰、别的程序
SetCursorPos），它就**永久为假**，直到下一次前台变化 ⇒
`g_fgAwaitUserInput` 再也解不开 ⇒ `if (g_fgAwaitUserInput) return;` 永远拦住 ⇒
**自动呼出等于完全失效**。
对数：用户日志 616 拍里 `await=1` 占 **297 拍（48%）**；本机复现同样现象
（274/274 拍 `why=await`）。

**修法**：改成**不绑定前台变化**的"刚刚有过一次触控式点击"——
`TouchInputTick()` 每拍采 `GetLastInputInfo` + `GetCursorPos`，
判「有输入」**且「与上一拍比光标没动」**（排除"只是晃鼠标"），命中刷 `g_touchTapTick`；
`byUserInFg = TouchTappedRecently()`（1500ms）。**每次触控都能刷新，中途移过光标也能自愈。**

另加**触屏一次性证据** `TouchTapEdgeConsume()`（每次触控只放行一拍，消费即失效）：
触屏拿不到落点，而两条强证据（系统触摸键盘出现 / 前台窗口里出现焦点事件）都可能
不成立 —— 那样"隐藏态要弹必须攒证据"就变成永远弹不出来。一次性 ⇒ 不振荡；
代价是压制态下点空白闪一下（可接受）。同时 `TouchFocusEvidence` 放宽一档
（**同进程**也算 —— Chromium 有多个顶层窗口，焦点事件有时报在另一个上面）。

### 新诊断字段 `why=`（用户要求"补充自动呼出相关日志"）

旧日志只有输入量，要人自己反推是哪条 `return` 拦住的。现在 `UpdateAutoVisibility`
每个判定点写 `g_dbgWhy`，并把日志**挪到函数出口**（`goto done:`；判定段内已无裸 return，
⚠ 同时把 `HWND fg = ...` 移到判定段之前，否则 MSVC C2362「跳过初始化」）。
取值：warmup / anim / verdict-hide / vhid-reset / vhid-blank / vhid-click-ok /
vhid-touch-ok / vhid-tap-ok / vhid-hold / userHid / shell-hide / await / show / shown /
manualHide / own-window / noInput-streak / hidden-nop / manualHide-nop / noInput-delay /
manualShow-nop / auto-hide。
新增字段：`why / tip（系统触摸键盘刚出现）/ foc（前台窗口里有焦点事件）/
mh（manualHide）/ uhi（userHidInInput）/ ns（无输入焦点连续拍数）`；
导出头加 `touchTapAgo`。
⚠ `vis=` 固定成**判定时**的值（`visForLog`）—— 日志挪到出口后 `g_vis` 已是动作后的。

### ⚠ 两个"看起来像 bug 其实不是"的坑（省得下次再查）

1. **程序"自己退出"**：用 Bash 工具起的进程（`start` 方式）会在**该次工具调用结束时
   被清掉子进程**。所以"启动 → 观察"必须写在**同一条命令**里，否则下次命令一看就没了。
   （我因此一度以为程序有自退 bug，还去查了 WER。）
2. **heredoc 会吃掉正则里的反斜杠**：在 `python - <<EOF` 里写正则拿不到匹配，
   同一段代码写成 `.py` 文件跑就正常。⇒ **分析脚本一律写成文件再跑**
   （`.workbuddy/analyze_diag.py`）。

### 自测结果（受控 Chromium 靶 + 注入开关）

点页面输入框 → 键盘弹出（visible False→True，**无闪烁**），`uiact=50004`（精判正常），
`hid=0`（信任门逻辑未误触发），全程只有 2 次可见性变化。
⚠ 「点空白 → 收起」**这条自测不到**：键盘窗口（ini 里存的位置 y=147~735）正好盖住了
我选的空白点，点击落在键盘自己身上。那条由用户实测（他说 Edge/WorkBuddy/QQ 都通过）。

### 交付物
`sharedtest2/inbox/HKeyboard_{x64,x86,arm64}_1010h.exe`
（commit `2d460dd`，CI run `38061239274` 三架构全绿）—— **未发 Release**。
"""
io.open(p, "w", encoding="utf-8").write(s)
print("已追加第 37 轮，长度", len(s))
