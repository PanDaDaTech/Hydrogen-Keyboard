# -*- coding: utf-8 -*-
"""把第 37 轮的长期结论追加到项目 MEMORY.md。"""
import io

p = r"C:/Users/29776/WorkBuddy/Worktrees/Hydrogen-Keyboard/main-4cee7061/.workbuddy/memory/MEMORY.md"
s = io.open(p, encoding="utf-8").read().rstrip() + """

## ⚠⚠⚠ 触屏上「自动呼出完全不工作」的根因（2026-10-10 第 37 轮）

**症状**：平板触屏环境下自动呼出完全失效（用户报 Issue #4）。

**根因**：`UserInputAfterForegroundChange()` 判的是「输入在**这次前台变化之后**
且 光标自那次切换起**没动过**」。第二个条件会让它被一次无关的光标移动
（点任务栏 / 外接鼠标被碰 / 别的程序 SetCursorPos）**永久**关掉，
直到下一次前台变化 ⇒ `g_fgAwaitUserInput` 再也解不开 ⇒
`if (g_fgAwaitUserInput) return;` 永远拦住。

**正解**：改判「**刚刚**有过一次触控式点击」——每拍采 `GetLastInputInfo` + `GetCursorPos`，
「有输入」且「**与上一拍比**光标没动」⇒ 刷 `g_touchTapTick`；1500ms 窗口。
另加一次性证据 `TouchTapEdgeConsume()`（每次触控只放行一拍，不振荡），
`TouchFocusEvidence` 放宽到**同进程**也算。

⇒ **通用教训**：任何「**自从某个事件以来源**状态没变」式判据都会被一次无关操作
永久关掉且无自愈路径。一律改成「**与上一次采样比**」。

## 诊断日志：`why=` 与两个格式铁律（2026-10-10 第 37 轮）

- `eval` 行现在以 `=> why=... uiact=... pt=... ptc=... inFg=... tip=... foc=...
  mh=... uhi=... ns=... hid=...` **开头** —— `why` 就是"自动呼出为什么没弹"的答案
  （分支名，见代码 `g_dbgWhy`）。日志打在函数出口（`goto done`），
  所以 `vis=` 用 `visForLog` 在判定前捕获。
- ⚠⚠ **字段顺序 = 重要性顺序**，且**不要在结尾依赖换行符**：
  `_TRUNCATE` 从结尾截断。曾经 `HKRING_LEN=320` 而 eval 行 330~360 字符 ⇒
  用户导出的 614/640 条**全丢关键字段**、640 条**挤成一行**。
  现在 `HKRING_LEN=576`，导出时逐条写并补换行。
- 分析脚本：`.workbuddy/analyze_diag.py <diag.txt>`（按时间戳切开、字段分布、变化时间线）。
- 自测开关 `-dginj`（让钩子接受注入事件）+ 受控靶
  `.workbuddy/drive_aftest.py`（自己起 Edge 打开 `.workbuddy/aftest/aftest.html`，不动用户窗口）。

### ⚠ 两个工具坑（都会伪装成"程序有 bug"）
1. **Bash 工具起的进程在该次工具调用结束时被清掉** ⇒ "启动 → 观察"必须写进**同一条命令**。
2. **heredoc / `python -c` 会被吃掉正则里的反斜杠**，且含 `cmd` 等字样会被安全启发式拦掉
   ⇒ **脚本一律写成文件再跑**。
"""
io.open(p, "w", encoding="utf-8").write(s)
print("MEMORY.md 已更新，长度", len(s))
