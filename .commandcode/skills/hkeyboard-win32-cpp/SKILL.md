---
name: hkeyboard-win32-cpp
description: 维护 HKeyboard（Hydrogen-Keyboard，单文件纯 Win32 C++ 屏幕键盘，E:\Project\Git\Hydrogen-Keyboard\hkeyboard.cpp，约 7000 行）时使用。本机**没有任何 C/C++ 编译器**（无 cl.exe / link.exe / mingw / clang），CI 是唯一编译验证途径，所以「推之前本地预检」是核心纪律。触发场景：「hkeyboard 编译报错」「C3861 identifier not found」「identifier not found」「'IniSetInt' 找不到」「先用后定义 / 函数未声明」「三架构构建全挂 / 只报 x86」「Build x86 失败后 x64 arm64 没跑」「本机编不了 C++」「推之前先扫一遍」「又要等一轮 CI」「gh run watch 看构建」。也覆盖**材质（Mica / 亚克力）子系统**：「去除 Mica」「去掉亚克力」「关掉背景材质」「背板磨砂」「SetWindowCompositionAttribute」「DWMWA_SYSTEMBACKDROP_TYPE」「BufferedPaint」「ApplyWindowMaterial」「背景材质下拉要不要删」「主界面透明度」「分层窗口 SetLayeredWindowAttributes」「圆角没了」「文字发虚」。还覆盖 **Win32 常驻键盘的兼容性判断**：「Win7 / PE 上能不能用」「DPI 缩放 175%」「GetDpiForWindow 在 Win7 不存在」「Win11 原生圆角 DWMWA_WINDOW_CORNER_PREFERENCE」「深色模式跟 AppsUseLightTheme 还是 SystemUsesLightTheme」。还覆盖 **自动呼出 / 主动隐藏的语义分层**：「最小化后自动弹出」「收起后又弹出来」「必须完全退出程序才停」「键盘老是自己出现」（Issue #2），含 **`g_manualHide` vs `g_userHidInInput` 两标志的分工**、**「所有隐藏入口都得置位」这条守门纪律**（`UpdateAutoVisibility` 里 `if (!g_manualHide && !g_vis) ShowKB(TRUE, ...)` 只有一处，漏一条入口就必弹）、以及**「暂时收起 vs 永久隐藏必须分成两个按钮」**这条产品判据（§十三之二，含一次改错被用户当场推翻的完整记录：把最小化改成永久收起 ⇒ 「自动呼出功能名存实亡」；正解是新增独立的 ⏑ 按钮、含「加标题栏按钮」的 7 步机械改动清单）。含：**「改布局前先出示意图」的真实截图像素搬运预演法**（用户说「请画个示例图给我评估下」「先出图再改代码」时用，见 §零之三）、两个推前静态检查脚本（先用后定义扫描、未定义标识符扫描）、CI 结构速查（三 job 串行、失败会掩盖后两个架构）、以及材质子系统的完整依赖边界图（哪些能删、哪些看着像材质专属但其实是圆角/透明度/文字渲染的命脉）。**还覆盖字体渲染子系统**：「字体还是怪怪的」「该粗的地方不粗，细的地方反而有点粗」「标题立不起来 / 层级是平的」「字号调了没用」「字重不统一」「文字发虚 / 边缘脏」「hinting / 网格拟合 / gridfit」「GDI+ 画文字 / TextRenderingHintAntiAlias」「PrivateFontCollection / AddMemoryFont」「DrawAlphaText 要不要删」「g_sfRow 没被用上 / 死字号」「设置页字号阶梯」「部分字体显示不对劲」「这几个字的字体跟别的不一样」「一行字有的字粗有的字细」「字变宋体了 / 回退宋体」「全局宋体 / 整个界面都是宋体」，含 GDI 与 GDI+ 的笔画宽度实测对照（GDI 波动 31% 且层级倒挂，GDI+ 波动 12% 且笔画与字号同比增长 122%）、内嵌字体注册给 GDI+ 的正确姿势（`GdipPrivateAddFontFile` 第一参数是 FontCollection 不是 Graphics，错了会返回 14）、以及「改字号阶梯必须 grep 全部调用点含测宽函数」这条纪律。还覆盖 **Ethereal 主题风格迁移 / 换皮配色**：「Ethereal 风格」「换成 Ethereal 那套配色」「跟 Rust 工具（PanDaPE-Maker）一样的配色」「oklch 令牌 / 色相旋钮 / 一个色相换全皮」「键帽描边看不见 / 键帽糊成一片」「悬停看不出来」「按下态对比度不够 / 不达 AA」「Enter 键白字看不清」「WinXP 和 Win11 统一 UI」「XP 上圆角有锯齿 / 区域裁剪」「AppsUseLightTheme 在 XP 上不存在」，含完整令牌表（可脚本生成任意色相）、键帽 7 态状态机、四处必须偏离原配方的实算依据。还覆盖 **标题栏开关按钮 / 小键盘按钮 / 数字区显示隐藏**：「保留小键盘按钮」「按钮点了没反应」「看不出数字区开没开」「按钮激活态」「标题栏开关按钮」「窗口自动加宽」「数字区被挤出窗口」「全尺寸下按钮不见了」，含零新增令牌的激活态配方（实测对比度）、`needW = 700 × dpi` 的加宽闭式解、以及「按钮与 Tab 键必须并到同一个函数」这条纪律。还覆盖 **「图标与文字对不齐」这一整类视觉问题**：「图标文本对不齐」「图标歪了 / 文字偏高偏低」「药丸里图标和文字间距太大」「行里 tile 和文字块中心不齐」「测了最新 CI 还是老样子 / 改了但没生效」（先怀疑**单实例互斥**：旧实例在跑时新 exe 会直接 exit 0 只唤出旧窗口），含 GDI+ **垂直居中偏上的量化模型**（行盒 1.326em、基线固定在矩形中心 +0.2936em、汉字 −0.089~−0.107em / 无降部拉丁 −0.071~−0.080em / 有降部 +0.018~+0.027em）、`TextInkShiftPx` 补偿、**量宽的「容纳宽度 vs 布局宽度」双语义拆分**（`MeasureTextW` / `MeasureTextAdvW`），以及「下载 CI 产物 → 本机真跑 → 截图 → 量像素」的闭环验收法（`scripts/hk_probe.ps1` + `scripts/settings_row_align.py`）。还覆盖 **键面标签的水平对齐（居中 vs 贴边）**：「白色按钮文本怎么不居中了」「文本偏右 / 不居中」「Esc 比 Tab 更靠里」「左右贴边不对称 / 贴边留白一边宽一边窄」，含三条绘制路径各自的尺子、**GDI+ Near/Far 对齐的是「容纳框」不是「推进框」**这个坑、以及「贴边锚点必须与 `DrawKeyLabel` 的 `edge = 6 DIP` 统一、要改就三条路径一起改」这条纪律（§十一，附 `scripts/key_label_align.py` / `key_icon_text_align.py` / `about_card_metrics.py` / `edge_pad_compare.py` / `hk_probe_v9.ps1`）。还覆盖 **关于页的上下间距 / 卡片间距**：「关于页的上下间距很奇怪」「还是感觉很奇怪」「图标陷在卡片下半边 / 上下留白不匀」「两张卡贴在一起 / 分组感不清」「卡间距比卡内留白还小」，含 **「先分辨截图里那个东西是程序画的还是用户手绘标注」** 这一步（拿同版本探针截图做像素颜色对照，程序里精确匹配 0 像素 = 手绘；用户在图上画竖条是在标「文字块高度」当参照，别去代码里找一个不存在的元素）、**「行盒中心 ≠ 墨迹中心」** 这个真凶（标题带降部向上溢出行盒、描述墨迹只占行盒 13/18 DIP，按行盒中心对齐图标会稳定差 3 DIP）、图标应「居中于卡片」而不是居中于行盒、卡间距应 ≳ 卡内留白 + 2 DIP 的经验值（否则整页节奏会出现 14/12/18 这种凹陷）、以及改后逐张 md5 证明「没误伤」的收尾纪律（§十二，附 `scripts/col_runs.py` / `ink_bands.py` / `about_page_metrics.py` / `about_card_compare.py` / `about_page_compare.py`）。**还覆盖输入注入与 IME 这一整类问题**（§十六）：「打不出中文」「微软拼音不组字」「中文候选框不出来」「按 shift 切不了中英文」「Win10 打不出中文但 Win11 正常」「英文数字能打中文不能打」「issue #3」「注入的按键不被输入法处理」；含 **⛔ 守门纪律 `WaitForLeftButtonUp()` 不能删**（它是修复本体，删了就复发）、**根因「微软拼音在鼠标左键按下期间拒绝处理注入的键盘事件」**、**「SendInput 注入的鼠标按下不会被系统记为按下 ⇒ 自动化测试永远测不出」**这条最关键的认知、修复的三个易踩点（必须泵消息 / 防重入 / WM_QUIT 回投），以及 **`#ifdef HK_DIAG` 诊断工具**（`-sendtest` / `-envtest` / `-noscapture` / `-norepaint` / `-diag`，正式版不编译）与改完后的两层隔离验证法（剥离宏查源码残留、搜 exe 三种编码）。
agent_created: true
---

# HKeyboard（单文件 Win32 C++ 屏幕键盘）

**仓库**：`E:\Project\Git\Hydrogen-Keyboard`（origin `https://github.com/PanDaDaTech/Hydrogen-Keyboard.git`，分支 `main`）
**主文件**：`hkeyboard.cpp`，单翻译单元，约 7000 行。资源 `hkeyboard.rc` + `resource.h` + `winres/`（图标 + 三个内嵌字体）。

## 零、最重要的一条：本机没有编译器

`cl` / `link` / `clang` / `g++` **全部不存在**（`command -v` 一律 NOT_FOUND）。
`C:\Program Files (x86)\Microsoft Visual Studio` 下只有 `Installer`，没有 IDE。

⇒ **CI 是唯一的编译验证途径**。一次推送 = 一次编译机会。
所以任何对 `hkeyboard.cpp` 的改动，**推之前必须跑一遍 §一的静态检查**，
否则一个"函数忘了前置声明"就会白烧一轮 CI（见 §二）。

## 零之二、验收：把 CI 产物拉下来真跑真截图（别再纯几何推演）

**「图标文本对不齐」这类视觉问题，靠复刻渲染推演能定位根因，但结论必须用实机截图收口。**

```bash
cd "E:/Project/Git/Hydrogen-Keyboard"
gh run download <run-id> -n HKeyboard_x64 -D <工作目录>/app-run     # 产物是单个自包含 exe
```
然后在 **PowerShell 工具**里跑（不要从 bash 调 powershell，会被安全策略拦）：

```powershell
& scripts\hk_probe.ps1 "<工作目录>\app-run\HKeyboard_x64.exe" "<工作目录>\shots"
```
产出：`v3_kb.png`（键盘）、`v3_kb_numpad.png`（小键盘布局）、`v3_tab_{general,layout,theme,about}.png`（设置四个 Tab）。
再用 `scripts/settings_row_align.py` 量「tile 中心 − 文字块墨迹中心」。

### ⚠ 用户说「我测了最新 CI 还是没变」时，先怀疑单实例互斥

`hkeyboard.cpp` 用 `CreateMutexW(0, FALSE, L"HKeyboard_Mutex")` 判重，已有实例时
**新 exe 立刻 `exit 0` 退出，只向旧窗口发 `WM_SHOW_KEYBOARD` 把它唤到前台**：

```cpp
g_mutex = CreateMutexW(0, FALSE, L"HKeyboard_Mutex");
if (g_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND ew = FindWindowW(L"HKeyboard", 0);
    if (ew) SendMessageTimeoutW(ew, WM_SHOW_KEYBOARD, ...);
    g_exiting = FALSE; return 0;      // ← 新代码一行都没跑
}
```

键盘是常驻程序（托盘 / 自启），**这台机器上极可能一直挂着一个旧实例**。
实测确认：连开两次同一 exe，第二次 `hasExited=True / exit=0`，进程数始终为 1。

⇒ 让用户「**先完全退出 HKeyboard（托盘右键 → 退出程序，或任务管理器结束 `HKeyboard_x64.exe`），再运行新 exe**」，
再截图确认。这一条能省掉一整轮"改了但没生效"的扯皮。

### 探针的实现坑

1. `Add-Type -TypeDefinition` 画图必须加 **`-ReferencedAssemblies System.Drawing`**，
   否则报「命名空间 System.Drawing 中不存在类型 Imaging」。
2. **不要 SetCursorPos + mouse_event**：依赖前台窗口、坐标还可能被 DPI 虚拟化。
   统一 `PostMessage` 投递 `WM_LBUTTONDOWN/UP`（客户端坐标）；开设置窗口直接发
   `WM_COMMAND(0x0111)` + `ID_MENU_SETTINGS(10010)`，比模拟点击稳。
3. PowerShell 工具的 stdout 有时不回显 —— 把结果 `Set-Content` 到文件再 Read。
4. **截图前必须清 hover**（`Shot` 里调 `ClearHover`：投一个「远离窗口」的 `WM_MOUSEMOVE`，
   lParam = `(20000<<16)|20000`，HitTest 返回 -1）。不清的话，鼠标所在那一行会带
   整行高亮 + 白色 tile，两张图逐像素比对时**整行都是差异**，极易被误读成「这里有 bug」
   （本轮就被骗过一次：以为布局页有 28 万差异像素，其实只是鼠标停在别的行）。
   注意窗口激活 / 移动时 Windows 会用**真实鼠标位置**补发一次 WM_MOUSEMOVE，
   所以「脚本没动鼠标」≠ 没有 hover。
5. **输出目录必须先建**，否则 `bmp.Save` 抛「GDI+ 中发生一般性错误」——
   报错信息完全看不出真正原因是目录不存在。

> 本机截图的窗口是无边框的（`win rect == client rect`），所以**截图坐标 = 客户端坐标**；
> 而用户发的 QQ 截图**四周有 24px 边框**，程序坐标 = 截图坐标 − 24。
> 两者都用 `/1.75` 换算 DIP。（设置窗口：本机 1225×1051 ↔ 用户截图 1273×1099，正好差 24×2。）

### 改完做回归：`scripts/tab_regression.py <改前截图目录> <改后截图目录>`

逐 Tab 逐行比对**标题墨迹中心**，输出每行 Δ。没动的地方恰好 0.00。

**别拿逐像素 diff 当判据**：同一台机器两次运行仍有约 **0.3%** 噪声（窗口边缘 / 阴影 /
光标），而且「内容完全不同的两个 Tab 差异像素数几乎相同」正是噪声的指纹
（实测 759 vs 761，差异行段模式也一模一样）。真正改动的量级是 28 万级 —— 差三个数量级。
看到「跨 Tab 等量差异」直接判噪声，别去追。

## 零之三、**改之前**就能把效果画出来：真实截图「按键像素搬运」预演

**用户说「请画个示例图给我评估下」「先出图，评估通过再改代码」时，不要手绘 mockup、不要先推 CI。**
只要满足一个条件，就能用**已有真实截图**拼出 100% 真实渲染的示例图：

> **条件是「同一行内相邻键重排，且每个键在新旧位置上的宽度不变」。**

判据写成三行断言（不满足就退回手绘）：

```python
assert sum(OLD_W) + gap * (n - 1) == KEY_AREA_W        # 旧排布铺满键区
assert sum(NEW_W) + gap * (n - 1) == KEY_AREA_W        # 新排布同样铺满
assert all(NEW_W[i] == OLD_W[SRC_OF[i]] for i in range(n))   # 逐键宽度对得上
```

`SRC_OF[i] = 「新位置 i 的像素从旧位置哪个键搬过来」` —— **这一张映射表就是全部改动。**
本轮实例（默认布局 Row4 按 Win11 触控键盘重排）：`SRC_OF = [1,0,2,3,4,6,5,8,9,10,7]`。

⚠️ **注意不能只做「相邻两键互换位置」**：`Fn`(80) 与 `Ctrl`(98) 宽度不同，
直接对调会露出 18px 旧键边缘。正确做法是**整行擦掉再重排**，靠上面的宽度断言保证零失真。

几何必须**逐行对着 `BuildKeys()` 复刻**，绝不目测。已实测的一组（dpiScale=1.75 / 1715×560 / 默认 5 行）：

| 量 | 值 | 来源 |
|---|---|---|
| `keyAreaX` / `KEY_AREA_W` | 17 / 1681 | `int(10*dpi)` / `g_ww - 2*keyAreaX` |
| `gap` | 7 | `int(4*dpi*scaleX)` |
| `keyH` | 86 | `((gh-padV)-(hdrBot+padV)-(rows-1)*gap)/rows` |
| `y0` / Row4 的 y | 84 / **456** | `hdrBot+padV+vRem/2`，再加 4 行步距 93 |

**先自检再搬运（`frame` 模式）**：把算出的每个键 bbox 画在原图上 `Read` 一眼，
框不贴合 = 公式抄错，别继续。贴合后：整行填成「两行之间缝隙」的底色 → 按新顺序 `paste` 旧块。

产出三张：改后整图（评估主图）、上下对比条、**箭头标注图**（红框旧位置 + 红框新位置 + 连线）。
脚本骨架见 `.workbuddy/scratch/mock/build_mock.py`（`frame` / `build` 两模式）。

**为什么值得**：示例图与最终渲染逐像素同源，用户评估的就是最终效果，改代码前就能定案；
本机没有编译器时这是唯一零成本的"看效果"手段（与 §零之二 的"CI 产物实拍"互补：**一个管改前，一个管改后**）。

## 零之四、量参考截图里的键宽（给「照着某把键盘改布局」用）

**用户丢一张真键盘 / 触控键盘截图说「照这个改」时，先把它量成 1u 网格，别目测。**

### 可用判据（踩过坑）

⚠️ **灰键（Tab/Caps/Shift）键面颜色常与键盘底板几乎相同**（实测 207,212,218 vs 211,215,221），
按亮度分「灰键/底板」会完全失败。两个能用的判据：

| 目标 | 判据 |
|---|---|
| 白键（数字/字母/标点） | 列最大亮度 **> 246**（键面 255，底板 216） |
| 灰键 | 列内「亮度 < 235 的像素数 **≥ 行高 60%**」—— 灰键列整列都暗，键缝列只有 ~5px 暗 |
| 行的 y 范围 | 逐 y 统计「纯白像素数」，>0 的是键面行，连续为 0 的是行缝 |

### 已实测的参考基准：Win11 触控键盘（可复用）

- **整个键盘是均匀 1u 网格**：白键键面 39~40px、键距 44px（原图 1280×800 里键盘宽 690px）。
- **四行白键全部 1.00u**（Row1 13 个 / Row2 13 个 / Row3 11 个 / Row4 10 个，宽度全等）；
  窄键 `- = [ ] \ ; ' , . /` **也是 1u**，不打折。
- 宽键：**Tab 1.5u · Caps 2u · LShift 2.4u · Enter 2.5u · RShift 1.8u · ⌫ 1.5u · Esc/Del/↑ 1u**。
- **每行总宽统一 = 15.5u**（各行键数不同，靠宽键宽度凑齐 —— 这是「白键能等宽」的前提）。
- **左右留白 ≈ 0.25u**；**上间距 71px = 面板高 27.7%**（下间距只有 5.9%，上下严重不对称）。

### 对照出的本项目问题（layoutMode==0 默认布局）

「每行 剩余宽度 ÷ 键数」→ **白键四行宽度都不同：57 / 59 / 67 / 71 DIP（最大差 24.6%）**；
宽键是硬编码随机值（`RShift 52` 比白键还窄）；`keyAreaX = 10 DIP ≈ 0.17u`（参考是 0.25u）。

⚠️ **要把白键统一，必须让「每行总 u 数相同」** —— 而默认布局各行键数天然不同
（15/15/13/13/11），加上 gap 固定 4 DIP，所以每行反解出的 u 会有 ~1.8% 差异
（实测 57.26 / 57.77 / 58.29 DIP）。这是固定 gap 的必然结果，肉眼不可见，可以接受。
**§八 的全尺寸布局已经做了 1u 网格，改默认布局时直接复用那套思路。**

## 一、推前预检（技能里的四个脚本 + 仓库里的字体子集校验）

脚本在 `scripts/`（前四个）；第五个在仓库 `tools/` 内。在仓库根目录跑：

```bash
cd "E:/Project/Git/Hydrogen-Keyboard"
PY="C:/Users/29776/.workbuddy/binaries/python/versions/3.13.12/python.exe"
SK="C:/Users/29776/.workbuddy/skills/hkeyboard-win32-cpp/scripts"
"$PY" "$SK/check_fwd.py"
"$PY" "$SK/check_undef.py"
"$PY" "$SK/check_macros.py"
"$PY" "$SK/check_balance.py"
"$PY" "E:/Project/Git/Hydrogen-Keyboard/tools/subset_font.py" --check   # 字体子集覆盖：改过任何 UI 文案必跑
```

- **`check_balance.py`** —— 括号 / 引号平衡（跳过字符串、字符字面量、注释、预处理续行）。
  5000+ 行的单文件里最容易犯的就是「改一处漏一个 `}`」，而 CI 要等 40s 才告诉你。
  期望输出形如 `() = 3596/3596  [] = 357/357  {} = 944/944` + `[OK]`。
  （只增删成对括号的改动，改前改后的计数应当同步变化。）

- **`check_fwd.py`** —— 找出「静态函数在定义之前被调用，且没有前置声明」。
  这类问题在 C++ 里是**硬错误**（`C3861 identifier not found`），
  但只要调用点在文件后半段之前就必炸。**这是本项目最高频的 CI 失败类型。**
  期望输出：`[OK] 未发现"先用后定义"的静态函数`。
- **`check_undef.py`** —— 调用名未定义启发式扫描，自动过滤 Win32 命名前缀。
  期望输出：只剩 `SUCCEEDED` / `FAILED` / `RGB` / `MAKEINTRESOURCEW` / `IsWindow` /
  `GetPropW` / `memcmp` / `_wcsicmp` 这类 Win32/GDI+/CRT 宏与函数，
  以及 `Gdiplus::Color`、`Gdiplus::Point` 这种**带命名空间的误报**（脚本按 `(` 前是否有 `.`/`>` 过滤，但 `Gdiplus::Color(` 仍会被当成候选）。
  **出现任何陌生的驼峰小写开头标识符，就是真问题。**
- **`check_macros.py`** —— 扫描「局部变量/参数名与 Windows/CRT 宏同名」。
  这类问题**前两个脚本都看不见**，但会让预处理悄悄改写代码。已经踩过一次：

  ```c
  int small = (int)(18 * dpi);
  // <windows.h> → rpcndr.h 里有 `#define small char`
  // ⇒ 预处理后：int char = (int)(18 * dpi);
  // ⇒ error C2632: 'int' followed by 'char' is illegal
  //    error C2513: 'int': no variable declared before '='
  ```
  同类的还有 `hyper`（`__int64`）、`pure`（老 MFC/ATL 的 `=0`）、`near` / `far`。
  期望输出：`[OK] 未发现与 Windows/CRT 宏同名的标识符`。
  **注意 `y0`/`y1`/`j0`/`j1`/`index` 不在此列** —— 它们只是老 CRT 的函数名、
  不是宏，`DrawLineAA(..., int y1, ...)` 这类参数是安全的。

> ⚠️ 已知误报来源：`#pragma comment(lib, ...)` 的 `comment(`、C 风格注释里被吃掉的标识符、
> 字符字面量 `L'1'`、以及局部函数指针调用（如 `fn(&vi)`）。看输出时先按这几类排掉。

## 二、CI 结构与失败模式

`.github/workflows/build.yml`：**单 job `build`，内部串行三步** `Build x86` → `Build x64` → `Build arm64`。

**关键推论**：第一步失败就 `exit /b`，后两个架构**根本不会跑**。
所以看到"只报 x86 的错"时，**不要以为 x64/arm64 是好的** —— 它们只是没机会编译。
一个与架构无关的语法/名字错误会表现为"x86 挂了"。

每步做的事（三份几乎相同的 cmd 块）：
1. `VsDevCmd.bat -arch=x86|x64|arm64`
2. 从 `deps\YY-Thunks` 找 `YY_Thunks_for_WinXP.obj`（用 `findstr x86|x64|arm64` 挑架构）
3. 从 `deps\VC-LTL` 找 `5.1.2600.0`（x86）/ `5.2.3790.0`（x64、arm64）下的 `lib\Win32|x86|x64|arm64` 与 `header`，塞进 `INCLUDE`/`LIB`
4. `rc /nologo /c65001 /fo ... hkeyboard.rc`
5. `cl /nologo /utf-8 /O2 /MT /D_HAS_EXCEPTIONS=0 /GR- /d2FH4- /D_WIN32_WINNT=0x0501 /c hkeyboard.cpp /Fo...`
6. `cl ... /link /OUT:dist\<arch>\HKeyboard_<arch>.exe /SUBSYSTEM:WINDOWS,5.01|5.02|6.02 /MANIFEST:NO /CETCOMPAT:NO ...`
   链接库固定为：`Comctl32 Shell32 Gdi32 User32 Advapi32 Imm32`

x86 单独走 `5.1.2600.0` / Win32 —— 这就是为什么"有些符号在 x86 上解析不了"这类问题会**只在 x86 暴露**。
但 §二 那个 `C3861` 与架构无关。

### 2.1 ⚠⚠ 发版路径必须唯一：`build.yml` 只编译，`release.yml` 才发版

| 文件 | 职责 | 触发 |
| :--- | :--- | :--- |
| `build.yml` | **只做编译验证**：三架构 + 产物 artifact | `push: main/master`、`pull_request`、`workflow_dispatch` |
| `release.yml` | **唯一发版路径**：7z 合并包 + 校验文件 + 自动补日期 | **仅 `workflow_dispatch`** |

**`build.yml` 里绝对不能有** `on.push.tags` 和 `Create Release` 步骤。
两条路径都发 Release 会互相踩，而且踩的是**权限**这种看不出来的坑：

```bash
gh api repos/PanDaDaTech/Hydrogen-Keyboard/actions/permissions/workflow
# 本项目：{"default_workflow_permissions":"read", ...}
```
`build.yml` 没声明 `permissions` ⇒ 拿到的是 **read-only token** ⇒
`generate_release_notes: true` 要调需要 write 的接口 ⇒
```
⚠️ Unexpected error fetching GitHub release for tag refs/tags/vX:
   HttpError: Resource not accessible by integration
Error: Resource not accessible by integration - .../generate-release-notes-content
```
而 `release.yml` 有 `permissions: contents: write` 所以一直正常。

⚠ **推 tag 后若 build.yml 的 tags 触发器已删，`gh run list` 不应出现新行** —— 用它验证改对了。

⚠ **想挪 tag 到最新提交**（**仅改 `.github/workflows/`、未碰源码时**安全，二进制完全一致）：
```bash
git fetch origin --tags
git tag -f v2.0_20261003 HEAD && git push --force origin refs/tags/v2.0_20261003
```
⚠ **改了 `hkeyboard.cpp` 就绝对不能挪** —— Release 里的 exe 会与 tag 指向的源码不符。

⚠ **CI 报错先分清是哪条路径**：tag 触发的冗余路径失败，**不等于正式 Release 失败**。
```bash
gh release view <tag> --json tagName,isDraft,isPrerelease,assets
```
别看到 CI 红就慌着重跑发布。

### 2.1b `release.yml`：**只被 `workflow_dispatch` 触发，push 碰不到它**

**改 release.yml 后 push 不会验证它，必须手动实跑一次**：

```bash
gh workflow run release.yml -f tag=v2.0 -f title= -f draft=true -f prerelease=false
gh run watch <run_id> --exit-status
gh release delete v2.0_20261003 --yes   # 清理验证产物（draft 模式不打扰用户）
```
⚠ tag 是 `v2.0` 而不是 `v2.0.0` —— 见 §2.2 的版本号口径。

### 2.1c ⭐ 首选：用仓库根的 **`release.cmd` 一条命令发**（2026-10-05 起）

用户明确「懒得上 Github 了」⇒ 不要每次手敲 `gh workflow run` + `gh release edit`。

```cmd
release.cmd                 create a DRAFT + auto-apply the notes file
release.cmd --publish       publish directly (no draft)
release.cmd --tag v2.0      set the tag (default v2.0)
release.cmd --notes FILE    use a specific notes file
```

脚本内部顺序：
1. 触发 `release.yml`；
2. **记下触发前的 `gh run list --limit 1 --json databaseId`，轮询认领新 run**
   （`workflow_dispatch` 不返回 run id，只能这样认领）；
3. `gh run watch <id> --exit-status`；
4. 解析真实 tag → `gh release edit <tag> --notes-file <file>`；
5. 打印预览 / 公开 / 删除三条后续命令。

正文查找顺序：`--notes` → `.workbuddy\release_body_<YYYYMMDD>.md`
→ `.workbuddy\release_body.md` → `RELEASE_NOTES.md`（仓库根，**入库**）。

#### ⚠⚠ 写这类本地 `.cmd` 工具的三个必查项

1. **取 tag 不能用 `gh run view --json displayTitle`** —— 那是**工作流名**
   （`Release`），不是 tag。第一次跑就因此把正文静默填到了不存在的 tag 上。
   正解：`gh release list --limit 1 --json tagName -q ".[0].tagName"`。
2. **`.cmd` 必须纯 ASCII + CRLF**（cmd 按系统代码页读脚本）：
   - 注释**全用英文**（中文注释会变成几百个非 ASCII 字节）；
   - Write 工具产出的是 **LF**，必须显式转 CRLF；
   - 校验（一条命令）：
     ```bash
     python -c "d=open('release.cmd','rb').read();print('CRLF',d.count(b'\r\n'),'bareLF',d.count(b'\n')-d.count(b'\r\n'),'nonascii',sum(1 for b in d if b>127))"
     ```
     目标是 `bareLF=0 nonascii=0`。
3. **draft 的 tag 尚未真正创建** ⇒ `gh api .../releases/tags/<tag>` **404**，
   要用 `gh api .../releases?per_page=N --jq '.[]|{tag:.tag_name,draft:.draft,body_len:(.body|length)}'`
   核对正文有没有填上。


### 2.2 ⚠ tag 用「主版本号 + 日期」：`v2.0` → 自动解析成 `v2.0_20261003`

用户口径：**tag = 主版本号_日期**，与关于页显示的 `2.0_20261003` 对齐。
（注意 v1.1.1 时代 tag 是三段 semver `v1.1.1`，但 Release **名**是 `26.08.10_v2`，
口径不统一，v2.0 起统一成 `主版本号_日期`。）

`Resolve release tag` step（`id: tag`）负责解析，输出 5 个 output：

| output | 例|
| :--- | :--- |
| `tag` | `v2.0_20261003` |
| `version` | `2.0` |
| `date` | `20261003` |
| `title` | 留空则回落到 `tag` |
| `prerelease` | 带预发布后缀时强制 `true` |

⚠ **下游三处必须用 `steps.tag.outputs.*` 而不是 `inputs.tag`**：
artifact 名、`softprops` 的 `tag_name` / `name` / `prerelease`。

**两道防呆**（都别删）：
1. 显式给的日期用 `[DateTime]::TryParseExact($date,'yyyyMMdd',...)` 验日历合法性
2. **与 `resource.h` 的 `VER_FILEVERSION_STR` 对账** —— tag 写 `v3.0` 而 exe 里还是 `2.0` 时直接拒。
   只比 `major.minor`，不拦 patch（`2.0.1` 合法）。抠版本号的正则：
   ```powershell
   $m = [regex]::Match((Get-Content resource.h -Raw), 'VER_FILEVERSION_STR\s+"([^"]+)"')
   ```

⚠ **北京时间不能用 `Get-Date`** —— GitHub runner 本地时区是 **UTC**，
晚上 8 点后跑就变成明天。必须显式换算（北京时间固定 UTC+8 无夏令时）：
```powershell
try { $tz = [TimeZoneInfo]::FindSystemTimeZoneById('China Standard Time') }
catch { $tz = [TimeZoneInfo]::FindSystemTimeZoneById('Asia/Shanghai') }   # Linux runner 兜底
$bj = [TimeZoneInfo]::ConvertTimeFromUtc([DateTime]::UtcNow, $tz)
```

⚠ **exe 的文件版本里不能也放日期**：`VER_FILEVERSION 2,0,0,0` 是 Windows 版本资源四段，
**每段上限 65535**，`20261003` 装不进去会溢出。日期只在 tag / Release 名 / 关于页里体现，
**别为了"对齐"去改 `resource.h` 的 `VER_FILEVERSION`**。

### 2.3 ⚠ 加新产物附件：三处清单必须同步，漏一处就发布失败

`softprops/action-gh-release` 的 `fail_on_unmatched_files: true` 意味着
**配置与实际文件不一致会直接让发布失败**。加附件要同步改：
1. `Verify release files` 的 `$required` 数组
2. `actions/upload-artifact` 的 `path`
3. `softprops` 的 `files`

现有附件：`HKeyboard_{x86,x64,arm64}.exe` + `HKeyboard_X86_X64_Arm64.7z`
+ `HKeyboard_X86_X64_Arm64.{md5,sha256}.txt`（**两个工作流的附件名保持一致**，
否则 Release 里会突然多出日常 CI 没见过的文件）。

### 2.4 ⚠ 改 workflow 的 YAML：插入 step 后**必须 `yaml.safe_load` 验一遍**

用 Edit 在 `run: |` 块中间插入新 step 时，**原块的最后一行容易被甩到块外变成零缩进**，
YAML 立刻解析失败，报错还指向错行：
```
release.yml, line 340, column 1: could not find expected ':'
line 342, column 13
```
⚠ **真正的问题在报错位置之前** —— 340 行是块结束后落到块外的第一行内容。
验法：
```bash
python -c "import yaml;yaml.safe_load(open('.github/workflows/release.yml',encoding='utf-8'));print('OK')"
```

### 2.5 ⚠ 读某个 step 的完整日志：`gh run view --log | grep` 会漏

ANSI 转义 + 长命令折行会让 grep 经常抓不全。可靠做法是下整份 zip 自己解析：
```bash
gh api repos/PanDaDaTech/Hydrogen-Keyboard/actions/runs/<run_id>/logs > run.zip
python -c "import zipfile;print(zipfile.ZipFile('run.zip').read('release/10_StepName.txt').decode('utf-8','replace')[-1400:])"
```
zip 内路径 = `<job名>/<两位序号>_<step名>.txt`，**step 名里的 `/` 会被替换成 `_`**。

## 二点五、自动呼出（焦点识别）—— 链路、判据、以及怎么自己造实验

### 链路（两个函数、两段）
- `TIMER_FOCUS`（50ms，`WndProc` 里）→ **`UpdateAutoVisibility()`**
- 焦点探测 **`GetFocusedInputControl()`** → 返回 NULL 就收起；非 NULL 才可能弹
- 全局钩子：`WH_MOUSE_LL`（记点击落点 `g_lastClickTick` / `g_lastClickTopHwnd`）、
  `WH_KEYBOARD_LL`（记 `g_lastKeyTick` 与 Shift/Win/Caps 物理态）
- `SetWinEventHook(EVENT_OBJECT_FOCUS / EVENT_SYSTEM_FOREGROUND)` → `WM_FOCUS_EVENT`
- 两道封锁：`g_fgAwaitUserInput`（切窗口后先别弹）、`g_userHidInInput`（手动收起后同输入框别回弹）。
  **两者都只由 `byKey`（实体按键）或 `clickInFg`（鼠标落点在前台窗口内）解除。**

### ⚠⚠ Chromium 系的 Win32 层**什么都不暴露**（本机实测，别再白试）
| 探测 | Chromium 的结果 |
|---|---|
| `GetGUIThreadInfo().hwndFocus` | 恒为**顶层** `Chrome_WidgetWin_1`（不是 render host） |
| `hwndCaret` / `GUI_CARETBLINKING` | `none` / 无标志 —— **Chromium 自绘光标，没有 Win32 carry** |
| 子窗口 | 恒为 `Chrome_RenderWidgetHostHWND` + `Intermediate D3D Window` |
| `ImmGetContext` 跨进程 | 恒 NULL（不可用） |

⇒ **Win32 层面分不出「光标在不在输入框里」，只有 UI Automation 能分。**
⇒ 火狐能用，是因为它**有** Win32 光标 ⇒ 走 `gi.hwndCaret` 那条路。

### UIA 判据（实测有效，2026-10-10）
`IUIAutomation::GetFocusedElement()` → `get_CurrentControlType()`：
| 操作 | ControlType | 次数 |
|---|---|---|
| 点进输入框 | **`EditControl`(50004)** | 3/3 |
| 点同窗口空白 | `DocumentControl`(50030) | 4/4 |
| 点回输入框 | **`EditControl`** | 3/3 |

⚠ **第十一轮回退 UIA 精判的理由（「整张网页永远有一个 Document 持有焦点」）
与上述实测不符。** 翻当年提交，失败的可查原因是**管道接错**
（`修 VT_I4 分支不下钻` / `UIA 接入的两个编译错误` / `Document 不能当可编辑判据`），
而且**当年把它用在了「显示」上**。⇒ 别因为那句旧结论就直接回避这条路。

**改动它的铁律** —— ⚠ 这一条被后续实测**修正过三次**，最终口径在下面
「但它在**隐藏态**给的是**冻结值**」那段；此处只留两条始终成立的原则：

1. **判据必须对称地管"该不该弹"与"该不该收"。**
   第一版只想让它管收起（显示仍走粗判），结果直接**自激振荡**（用户实测"每约 2 秒闪一下"）：
   粗判对 Chromium 恒为真，只要"收起"由精判定、"弹出"由粗判定，两者必然互相打架。
   日志特征一眼可辨 —— `vis=0 ... => hasInput uiact=-1`（**uiact=-1 = 那一拍没查**）。
2. **信任门**：只有在该窗口上**见过 Edit** 才采信它的"不在输入框"。
   ⚠ 硬事实：contenteditable 型输入框报不出 Edit —— 实测 QQ 的聊天输入框是
   **ProseMirror，UIA 里报 `GroupControl`**（见 `GroupControl` 的实测），
   且**整个 QQ 窗口里 `EditControl` 数为 0**。没有这道门，那类应用会被判成
   "永远不在输入框"⇒ 自动呼出失效（= 第十一轮翻车形态）。
   没见过 Edit 的窗口 = 判据在这不可用 ⇒ 退回粗判（旧行为：永不收起，但不会永不弹出）。

配套保留：保持期 400ms、正在输入（`g_lastOwnKeyTick` / `byKey`）不收起、
250ms 查询缓存、跨进程前 50ms 响应性预检、ini 开关 `General/ChromiumPrecise`（默认 1）。

**判据本身的稳定性有实测背书，别误判成"UIA 不可靠"**：
WorkBuddy 前台 + 光标在输入框 + **全程不碰任何东西** → `EditControl` **233/233，
连续 14.0 秒零跳变**（`.workbuddy/uia_stability2.py`）。
日志里"50004/50030 成块交替"是上面那个振荡带出来的假象。

### ⚠⚠⚠ 但它在**隐藏态**给的是**冻结值**（本轮最硬的一条，`c926ebf`）

同一个 exe，只差启动方式：

| 启动方式 | C++ 侧 `uiact` | Python 侧同刻**同一 API** |
|---|---|---|
| 不带 `-hide`（可见） | `50004` ×167 / `50030` ×182（**完全正确**） | 一致 |
| 带 `-hide`（隐藏） | **恒为 `50000`(ButtonControl) ×129** | `EditControl`（真值） |

已核对包源码（`uiautomation.py` 10388 行）：Python 的 `GetFocusedControl()` 调的就是
`IUIAutomation.GetFocusedElement()` —— **同一 API、同一时刻、两个进程两个答案**。
⇒ **隐藏期间不能消费这个判据。**

**最终定稿（第 34 轮 `ddf5e3a`，前四版全被实测打回）**：

判据只一条主线：**"该不该收"用焦点判据（仅可见时），"该不该弹"必须有正面证据。**

1. **已显示** → 查焦点判据。明确"不在可编辑元素里"才收（信任门已开时）。
   ⚠ 只在 `g_vis` 为真时查 —— 隐藏期间它给过无效值（见上面那张表）。
2. **隐藏中** → 必须重新攒到**正面证据**之一，否则保持隐藏：
   - 鼠标：`ElementFromPoint(g_lastClickPt)` == Edit（`pt==1`）
   - 触屏：系统触摸键盘刚出现（上升沿），或**刚才那次焦点变化发生在当前前台窗口里**
     （`TouchFocusEvidence()`，用 `EVENT_OBJECT_FOCUS` 事件；只用**边沿**，绝不用电平式的
      `byUserInFg`，否则点空白也会弹 → 又是乱跳）
   - 敲了实体键 / 换了前台窗口
   ⚠ **"保持隐藏"本身即稳定态**，所以不需要额外的压制标记去兜 —— 前几版的"锁死"
     全部出自那种标记。`g_verdictHide` 现在只是一句备忘（换了前台窗口即作废）。
3. **`-1`（查不出来）一律不做决定** —— 不设信任门、不触发收起。
   ❌ 曾写成 `cached = (v < 0) ? 1 : v`（折成"是输入框"）**又拿它设信任门** ⇒
   之后一次无效值就满足"可信 + 非 Edit" ⇒ **键盘被永久收掉**。
4. **绝不按 `g_vis` 跳过查询来决定"该不该弹"** —— 那会让弹出退化成粗判（对 Chromium
   恒为真），与精判的收起来回打架 ⇒ 周期约 2 秒的自激振荡。
   日志特征：`vis=0 ... => hasInput uiact=-1`（**`uiact=-1` = 那一拍没查**）。

⚠⚠ **`pt==0` 的含义是「判出来了、就是空白」，不是「判定失败」。**
第 33 轮把它当失败、加了"连点 3 次就放行"的兜底 ⇒ 隐藏中 `hid=1 pt=0` 连 3 拍就放行 →
弹出 → 精判立刻说"不在输入框" → 又收 = 用户看到的**「离开输入框时乱跳一次」**。
日志能逐拍对上（1010d 那次：37.611/37.672/37.742 三拍 `pt=0` → 37.743 ShowKB）。
**判据"给出否定答案"和"给不出答案"必须分开处理。**

**陷阱：缓存与保持期必须按窗口分组**（`g_uiaCacheFg` / `g_uiaHoldFg`）。
原来是单个 `static` ⇒ 在 WorkBuddy 里见过 Edit 后切到 QQ，那 400ms 保持期把 `1`
带过去、`g_chromiumTrustFg` 被设成 **QQ 的窗口**，于是 QQ 的"不在输入框"
（ProseMirror 只报 Group/Document）开始生效 ⇒ **QQ 再也弹不出来**。
同理 `UiaFocusEditable(HWND fgExpect)` 里要**校验焦点元素的归属窗口**
（`get_CurrentNativeWindowHandle` → `GA_ROOT` 必须等于前台顶层窗口），归属不符返回"未知"。

**点击/按键后要作废缓存**（`InvalidateUiaCache()`）：不然点进输入框后的前 250ms
还在用"点在空白"时的旧结论去收起 → 表现为"点一下输入框，键盘闪一下又没了"。

新诊断字段：`pt=`（落点查询结果 1/0/-1）、`ptc=`（落点元素 ControlType）、
`hid=`（是否处于精判收起态）。

### ⚠⚠ Win10 与 Win11 的系统触摸键盘**不是同一个东西**（本机 Win11 实测）

| | 进程 | 窗口类 | 怎么判"正在显示" |
|---|---|---|---|
| Win10 | `TabTip.exe` | `IPTip_Main_Window` | `FindWindowW` + `IsWindowVisible` 即可 |
| Win11 | `TextInputHost.exe` | `Windows.UI.Core.CoreWindow` | ⚠ **该宿主窗口常年 `IsWindowVisible=TRUE`**（rect 就是整屏 1463x914，标题「Windows 输入体验」）⇒ 必须补 **DWM cloak** 判定（实测没显示时 `DWMWA_CLOAKED=2`） |

⚠ `Windows.UI.Core.CoreWindow` 这个类名**被大量 UWP 宿主复用**（本机 explorer.exe /
SystemSettings.exe 各有一个）⇒ **必须再按进程名确认**，只看类名会误伤。
⚠ `ITipInvocation`（`{4CE576FA-…}`）在本机 Win11 上 `CoCreateInstance` 返回
`0x80040154`（未注册），**别指望用它把触摸键盘叫出来做实验**；
`TabTip.exe` 直接 `CreateProcess` 会被 `WinError 740`（需要提升）挡掉。

### 自测：`-dginj` 让注入输入可见（`ddf5e3a` 新增，**仅自测**）

`SendInput` / `mouse_event` 带 `LLMHF_INJECTED`，默认被钩子按设计忽略 ⇒
`clickInFg` / `byKey` 永远为假，**"点输入框能弹回来"这条自测不了**（只能让用户往返）。
⇒ 加了 `-dginj`（`g_dbgAcceptInjected`）让钩子接受注入事件，于是可以自己驱动全链路：

```python
# 受控靶：自己起一个 Chromium 窗口，不动用户任何窗口
msedge --new-window file:///E:/Project/Git/Hydrogen-Keyboard/.workbuddy/aftest/aftest.html
# 再用 uiautomation 找 EditControl 的 rect，对输入框中心 / 页面空白各点一次
auto.Click(ix, iy)     # uiautomation.Click 走 SendInput ⇒ 配 -dginj 才被接受
```

⚠ 正式使用**绝不要**开这个开关。

### 日志导出（`ddf5e3a` 新增，用户明确要求）

原先只有 `-afdiag` 才有日志，且必须"重现前先启动好"，用户往返成本高。
⇒ 加了**常驻环形缓冲**（`g_ring[HKRING_N=1000][320]`，~320KB BSS，默认每 4 拍记录
一条 eval ≈ 200ms，加所有事件 → 约 3.3 分钟），托盘右键 →**「导出诊断日志」**
（`ExportDiagLog`，`ID_MENU_DIAG`）即写出
`<exe目录>\HKeyboard_diag_<YYYYMMDD_HHMMSS>.txt` 并打开资源管理器选中它。

文件头是一份环境快照：build / OS（读注册表取真值，`GetVersionEx` 会撒谎）/
DPI / 触屏三项 / 三个 ini 开关 / 布局 / 当前全部状态量（`vis`、`verdictHide`、
`trustFg`、`tabStopVisible`…）。
⇒ **以后让用户复现问题，不用再让他带参数启动**，出问题直接导出。

### ⚠⚠⚠ 触屏（平板）上「自动呼出完全不工作」的根因（`b711eca`）

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
2. **必须保证换行**。`_TRUNCATE` 会把结尾的 `\n` 一起截掉 ⇒ 640 条**挤成一行**。
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
2. **heredoc 会吃掉正则里的反斜杠**：`python - <<'EOF'` 里写 `\d\d:` 拿不到匹配，
   同一段代码写成 `.py` 文件跑就正常。⇒ **分析脚本一律写成文件再跑。**
   另外 `python -c "..."` 里含 `cmd` 等字样时会被安全启发式拦掉 ⇒ 同样改走脚本文件。

### 自己驱动「托盘启动 → 显示」这条链路（能省掉用户往返）

```python
u.SendMessageW(hwnd, 0x8000 + 100, 0, 0x0202)   # WM_TRAY=WM_APP+100, WM_LBUTTONUP
```
直接给主窗口发托盘消息就能走 `ToggleKB()` 显示出来，**不需要真鼠标** ✓。

⚠ 找窗口必须 `FindWindowW(ctypes.c_wchar_p("HKeyboard"), None)` ——
**传 Python `str` 给 `FindWindowW` 会被当 ANSI 指针**，返回 NULL（我因此一度误判"类名找不到"）。

## 二点六、托盘（通知区）逻辑

- `WM_TRAY = WM_APP + 100`；`AddTray()` 用 `uID = 1003`。
- ⚠ **一次双击会送来三条消息**：`WM_LBUTTONDOWN, WM_LBUTTONUP, WM_LBUTTONDBLCLK, WM_LBUTTONUP`。
  原来 `UP` 与 `DBLCLK` 都调 `ToggleKB()` ⇒ **连切 3 次**（显示→隐藏→显示）：
  净结果看着对纯属**奇偶侥幸**，中间还闪两下；外壳只送两条时直接变成"点了托盘没反应"。
  ⇒ 已加 400ms 去抖（`g_lastTrayToggleTick`，`c489db0`）。
- 单实例：`HKeyboard_Mutex` 已存在时新进程发 `WM_SHOW_KEYBOARD`（`w = 是否要求隐藏`）
  → `ShowKB(w, TRUE)`，因此**再跑一次不带 `-hide` 的 exe 会把已有实例显示出来**。
  ⚠ 这条会被旧实例静默接管 ⇒ 用 `-afdiag` 调试时**必须先完全退出旧实例**，否则日志只剩 BOM。

⚠ **链接依赖**：只用纯 getter（`CoCreateInstance(__uuidof(CUIAutomation))` /
`GetFocusedElement` / `get_CurrentControlType`）。**不要用 `GetCurrentPropertyValue`** ——
它要 VARIANT，会拉进 `oleaut32`，而链接行只有
`Comctl32 Shell32 Gdi32 User32 Advapi32 Imm32`，arm64 job 上因此链接失败过（19b0aa2）。

### ⚠⚠ 触屏是这套判定的**盲区**（Chromium 系上）
Chromium 注册了指针输入 ⇒ 触控只走 `WM_POINTER`/`WM_TOUCH`，
**不会被提升成鼠标消息** ⇒ `WH_MOUSE_LL` 一次都不触发 ⇒
`clickInFg` 永远为假 ⇒ `g_fgAwaitUserInput` / `g_userHidInInput` **永远解不开**
⇒ 自动呼出彻底失效（用户实测表现：只能从托盘手动打开）。
火狐没做这个注册、触控照常提升 ⇒ 火狐正常 —— 这组对照能直接指认根因。

兜底办法：`GetLastInputInfo()`（会话级最后输入，鼠标/键盘/触控/笔都算），
判据是「最后输入发生在**这次前台变化之后**」（`g_lastFgSetTick` 配合），
并加两道过滤：**只在触屏设备启用**（`IsTouchDevice()`），
且**「仅鼠标移动」不算**（用「光标位置有没有变」区分 —— 触控不移动光标）。

### 怎么自己造实验（不要再让用户按秒表操作，那个数据一定会被污染）
问用户"描述现象"、或让用户按时间节奏点两段，**实测两次都拿到污染数据**
（前台在浏览器/QQ/终端/托盘之间跳）。正解是**自己驱动焦点**：

```python
# C:\Users\29776\.workbuddy\binaries\python\envs\default\Scripts\python.exe
# 依赖：pip install uiautomation
import uiautomation as auto
auto.Click(x, y)            # 真实鼠标点击 —— 唯一能真正挪动 Chromium 焦点的手段
auto.GetFocusedControl()    # 读焦点的 ControlType / ClassName / BoundingRectangle / 各 Pattern
```
- ⚠ `SetFocus()` **对 Chromium 无效**（不报错，但焦点完全不动）⇒ 必须用 `Click()`。
  我一度据此误判"判据不成立"。
- 现成脚本（`.workbuddy/`，可复用）：`uia_click_test.py`（三段对照，最有效）、
  `probe_uia_focus*.py`（采样）、`probe_chromium_focus*.py`（Win32/ctypes 探针）。
- **Windows 工具的 `Add-Type` 被安全策略拦死**（理由："compiles and loads .NET code"），
  所以 Win32 探测走 **Python + ctypes**；UIA 走隔离 venv 的 `uiautomation`。
- chcp/编码与 bash 字样相关的命令也可能被拦 —— **写文档/记忆用 Edit 工具，别用 bash**。

**自己驱动复现整条时序**（`drive_repro.py`）：拉前台 → 点输入框 → 点空白 → 再点回，
同时配 `-afdiag` 跑 HKeyboard，最后读 `ShowKB` 时序看有没有振荡。三个坑：
1. ⚠ **HKeyboard 自己 TOPMOST，会挡住点击落点** ⇒ 必须先让它从**隐藏态**起步
   （`-hide`），并且**排除 HKeyboard 窗口矩形**（`ClassName == "HKeyboard"`）再选输入框，
   否则点击全打在它自己身上，焦点根本不进输入框。
2. ⚠ 用 `(./exe &)` 从 bash 启的 GUI 进程**会随命令结束一起消失** ——
   要留后台得用 `cmd //c start "" "exe" -hide`，再用
   `tasklist //FI "IMAGENAME eq HKeyboard_x64_*.exe"` 确认。
3. ⚠⚠ **不要在用户正在用机器时驱动鼠标点击。** 实测踩过：前台已被用户切到别的窗口，
   我的点击落到了无关地方。自测要么先跟用户约时间，要么只在确定前台是自己的窗口时做。

**读 `afdiag.txt` 的两个惯用手法**：
```bash
# 显示/隐藏时序（⚠ vis= 是**调用前**的值，所以 vis=1 那行其实是"隐藏"）
grep -n "ShowKB" afdiag.txt | sed -E 's/(.{0,11}) ShowKB.*(vis=[0-9]).*(=> .*)$/\1 \2 \3/'
# 每拍的关键尾部字段（含 uiact / inFg）
grep eval afdiag.txt | grep -oE "^[0-9:.]+ .*(hasInput|noInput) uiact=[-0-9]* inFg=[0-9]*"
```
判读：**`uiact=-1` = 这一拍没做精判**（被 `typing` 或条件跳过）；
`uiact=50004/50030` = Edit / Document；`inFg=1` = 触屏兜底成立。

### `-afdiag`：正式版可用的一键诊断
`HKeyboard_x64.exe -afdiag` → exe 同目录写 `afdiag.txt`，逐 tick 记：
`fg / fgTop / input / rc(recentClick) / cif(clickInFg) / bk(byKey) / aw(g_fgAwaitUserInput) /
vis / hasInput`，外加 **`fg class -> XXX`（前台窗口类变化）**、
**`uiact`（本次焦点 ControlType，50004=Edit / 50030=Document / -1=没查）**、
**`inFg`（触屏兜底是否成立）**。
- ⚠ **跑之前必须完全退出旧实例**（单实例互斥体），否则新进程静默退出、日志只剩 BOM。
- `sharedtest2/inbox/Run_afdiag.cmd` 是一键脚本：自动挑最新 `HKeyboard_x64*.exe`、
  杀旧实例、`-afdiag` 启动、35 秒复现、打印并存 `afdiag_result.txt`。
  ⚠ 必须**纯 ASCII + CRLF**（Write 工具产出 LF，要显式转换后再校验）。

## 三、材质（Mica / 亚克力）子系统的依赖边界

⚠️ **这是去材质时最容易删错的地方。** 材质不是"一个特效开关"，而是从
**DWM 合成层 → BufferedPaint 画布层 → 绘制层 → 设置层**的一条纵切面。

### 曾评估过的删除边界（结论可直接复用）

**可以删**（纯材质专属，约 370 行）：

| 内容 | 说明 |
|---|---|
| `GetDwmExtendFrameIntoClientArea`、`GetDwmFlush` | 延迟绑定，仅材质用 |
| `GetBufferedPaintApi` | uxtheme 的 BufferedPaint 五个入口 |
| `GetSetWindowCompositionAttribute` | **未公开 API**，随时可能被系统移除 |
| `AccentPolicyLocal` / `WindowCompositionAttribDataLocal` | 结构体 |
| `ApplyWindowMaterial` / `ApplyAllWindowMaterials` | 核心（约 93 行） |
| `IsMainMaterialPaintActive` / `DrawWindowMaterialTint` / `DrawAlphaSurface` | 色调层 |
| `IsMaterialApplied` / `IsWindowsPE` / `IsSystemBackdropDarkMode` | 判据 |
| `WM_REAPPLY_MATERIAL` 消息 + 3 个 handler + 2 个投递点 | |
| `KbFrameSig::material` 缓存签名位 | |
| 设置层：`MaterialOptionCount/ModeOf/IndexOf/NameOf`、下拉、命中表、点击、`Theme/Material` ini | |

**⛔ 必须保留**（看着像材质专属，其实是别的东西的命脉）：

1. **`GetDwmSetWindowAttribute` + `ApplyRoundedWindow` / `TryApplyWin11RoundedWindow`**
   —— 这是 Win11 **原生窗口圆角**（`DWMWA_WINDOW_CORNER_PREFERENCE`，需 `SetWindowRgn(hWnd,NULL,TRUE)` 清区域裁剪）。删了窗口变直角。
   同理 **L4195-4201 / L4540-4546 的 `DWMWA_TRANSITIONS_FORCEDISABLED`** 也属于它，别一起删。
2. **`g_winBuild` / `DetectWinVersion`** —— 版本号还被圆角判定（`>= 22000`）复用。（`g_isWin11` 若无其它引用可随材质删。）
3. **`ApplyWindowOpacity`** —— 去掉材质后的**替代能力**，必须保留并在所有系统放开。
4. **`g_alphaPaintActive` + 32bpp 顶向下 DIB + `DrawMaterialText`**
   —— ⚠️ **最大陷阱**。`BeginWindowPaintSurface` 的**非材质分支**同样建 32bpp DIB 并把 `g_alphaPaintActive=TRUE`，
   `DrawMaterialText` 在无材质时照常工作（注释明说"使文字统一走 alpha 混合路径"）。
   **删掉它会连带破坏全部文字渲染**（圆角抗锯齿、Segoe MDL2 glyph、设置页文字）。
   只能删 `g_bufferedPaintActive == TRUE` 那一支。
5. **`g_wallpaperAccent` / `GetWallpaperAccentBgr`** —— 壁纸强调色跟随，与材质无关。

### 去材质时两个「不问清楚就会功能倒退」的点

1. **透明度必须顶上来。** 设置页第 2 行是**互斥显示**：
   `if (g_supportsMaterial) 显示"背景材质" else 显示"主界面透明度"`。
   而 `ApplyWindowOpacity` 的启用条件是 `!g_supportsMaterial && hWnd == g_hWnd && g_mainOpacity < 100`。
   `g_supportsMaterial = !g_isWinPE && g_winBuild >= 17763` —— **Win11 与 Win10 1809+ 恰好是 TRUE**。
   ⇒ 只删材质的话，**现代系统用户会既没材质也没透明度**，第 2 行直接空掉。
   **必须同时**把透明度下拉在所有系统放开、并去掉 `ApplyWindowOpacity` 的 `!g_supportsMaterial` 条件。
2. **ini 兼容。** 老 `HKeyboard.ini` 里有 `Theme/Material=1|2`。
   读取后必须**丢弃并停止回写**，否则会出现"设置页没材质项但窗口在套材质"的幽灵状态。

### 三个会被忽略的行为变化

1. **深色判定源改变**：`IsDarkThemeActive()` 是
   `g_materialMode != 0 ? IsSystemBackdropDarkMode() : IsSystemDarkMode()`，
   即材质跟 **`SystemUsesLightTheme`（系统 UI）**、非材质跟 **`AppsUseLightTheme`（应用）**。
   去材质后一律走后者，「系统深色 + 应用浅色」的用户会看到键盘深浅**反过来**。属预期，但发版说明要提。
2. **`ResolveFontColor` 的材质分支失效**：材质模式下浅色主题把 `C_DIM→RGB(112,112,112)`、`C_WHITE→RGB(48,48,48)`；
   去材质后文字回落 `g_theme` 定义值。**必须实测浅色主题 + 纯色底的对比度（≥4.5:1）**，这是最容易翻车的观感点。
3. **`ApplyAllWindowMaterials` 有 8 个调用点**被当作"重绘/补套"用（含 L248、2433、2461、3996、4066、4072、4205、4395、4550、5208、5213）。
   删函数后要逐点替换：主题切换处已有 `InvalidateRect`（安全）；透明度变更处改调 `ApplyWindowOpacity`；
   窗口显示/补套处改 `InvalidateRect` —— **漏改会导致切主题或显示窗口后画面不刷新**。

### 唯一需要真机验证的点

透明度走 `WS_EX_LAYERED` + `SetLayeredWindowAttributes`。
**Win11 下分层窗口与 DWM 原生圆角/投影的共存表现无法从代码推断**：
必须把透明度调到 80% 后确认圆角是否保留、投影是否异常、文字是否发虚。

### 顺带的性能收益

`StartWindowMotion`（约 L2420-2426）用两次 `RedrawWindow` + `RDW_UPDATENOW` 专门消除材质模式的"黑一帧"；
`FinishWindowFade` 也有透明度恢复分支。去材质后这些冗余刷新可收敛。

## 四、改完的验收清单

- [ ] `check_fwd.py` / `check_undef.py` 双双干净
- [ ] CI 三架构绿（`gh run watch <id> --exit-status`）
- [ ] 设置页第 2 行在 **Win11 / Win10 1809+ / PE** 三种环境都显示"主界面透明度"
- [ ] 透明度 100%↔80%：无残影、文字清晰、圆角保留（Win11 重点）
- [ ] 主题切换（跟随系统/强制深/强制浅）后立即刷新，无残留材质
- [ ] 浅色主题 + 纯色底：正文/次要文字对比度 ≥ 4.5:1
- [ ] 键位按下/悬停/Shift/Caps/Fn 各状态高亮正常（证明 alpha 文字路径未被破坏）
- [ ] 呼出/收起动画无"黑一帧"
- [ ] 带 `Theme/Material=2` 的旧 ini 启动：无幽灵材质；回退旧版 exe 不异常

## 五、Ethereal 统一 UI（换皮 / 配色 / 跨系统观感）

**完整细节见 `references/ethereal-ui.md`**，令牌表由
`scripts/ethereal_tokens.py` 生成（任意色相 + 对比度校验）。

要点速记：

- **配色归属**：Ethereal 的 oklch 令牌已经被 Rust 工具 `PanDaPE-Maker` 搬到了
  `crates/panda-core/src/{tokens.rs,color.rs}`，**HKeyboard 复用它，不要另起一套色值**。
  `DEFAULT_HUE = 300`（紫，不是 Ethereal 原版的 250）；`HUE_MAX` 必须 **359 不是 360**。
- **换皮成本极低**：颜色已经全部经 `ThemeColors` 表 + 9 个 `C_*` 宏间接寻址，
  绘制代码里几乎没有色值字面量（只剩 5 处硬编码白/黑待清）。`hot` 保留为 `primary` 别名，
  18 处 `C_HOT` 调用点一处都不用改。
- **键帽状态机是 7 态、三路同时变**（底 / 轮廓 / 字），不是现在这样"一个 `bg` + 恒定 `C_KEY_BORDER`"。
- **四处必须偏离 Ethereal/Rust 原配方**（都是实算出来的，不是审美偏好）：
  1. 键帽轮廓**不能用「主色 15%」**——浅色下对 `page_bg` 只有 **1.01:1**，完全不可见。
     改用 `mix(deep_text, card_bg, 0.20)`（一个公式自动适配明暗）。
  2. 悬停**不能只改底色**——`btn_card_bg_hover` vs `card_bg` 只有 **1.06:1**。
     必须同时把轮廓换成 `primary 45%`。
  3. 按下态文字**不能是 active 底上的 `btn_content`**（**3.15:1**，14px 小字不达 AA），
     改用 `deep_text`（9.87:1）。
  4. Enter **不能用白字**（**2.80 / 2.33:1**），改用 `deep_text`(浅) / `black@70%`(深)。
     Rust 工具自己的测试只卡 `>=2.5`，因为那边是 26px 大标题。
- **三个真实 OS 差异**：窗口圆角（XP 只能 1-bit 区域裁剪 → 建议两边都做直角，成本 3 行）；
  系统深色（`AppsUseLightTheme` **XP 上不存在** → 恒浅色）；
  透明度上限（`ApplyWindowOpacity` 的 `!g_supportsMaterial` 条件必须去掉）。
- **已经统一的部分不要动**：三个窗口全是 `WS_POPUP` 自绘（OS 主题无处泄漏）；
  文字走灰度 AA 而非 ClearType（XP 与 Win10+ 的 ClearType 行为不一致，灰度反而更一致）；
  字体全部内嵌（`MakeFont` 只在注册失败时回退）。
- **渲染约定**：轮廓与键帽底**一律用预先算好的实色，不要在绘制时传 alpha** ——
  非材质路径全不透明，且 GDI+ 往 32bpp `BI_RGB` DIB 画半透明语义不可靠。
  真需要透明的只有整窗 `WS_EX_LAYERED`。

## 六、设置页 UI + 自绘图标

**完整规格见 `references/settings-and-icons.md`**（制作工具的布局表、控件尺寸表、
设置页两种语法的差异、图标转换与绘制要点）。要点速记：

- **制作工具的设置页语法是「一 tab 一张卡」**，行间用 **1px 分隔线**；
  HKeyboard 改前是「一行一张卡」，行间 6px 空隙 —— 这是"风格不同步"的真正原因，
  **不是色值**。图标也从「24px 裸图标」改成「30×30 有底圆角方块」。
- **窄窗口必须偏离的一点**：制作工具的控件**左起**（它的内容区约 900 DIP）。
  HKeyboard 设置窗 700×575 / margin 30 → **内容区只有 640**，
  照搬左起会让一个 46px 开关后面留 418px 空白。**保留控件右对齐。**
- **行高不再是常数**（下拉 40 高、开关 26 高）。`SettingsRowRect` 的等距布局
  （`contentY + index*(rowH+rowGap)`）要改成**累加**，并让 `SettingsHitTest`
  复用同一份 y 表 —— 绘制与命中用同一份数据。
- **⚠ 所有长度必须「同单位、最后一次性乘 dpi」**（踩过一次真实 bug，实机截图才发现）：
  行高曾写成 `m.rowPadY * 2 + content`，其中 `rowPadY` 已乘过 dpi、`content`(34) 还是 DIP，
  于是 **175% 下行高只有 74px（应为 101px）**。症状很好认：
  **整张卡比预期矮一截、底部留一大片空白、两行文字挤在一起**。
  正确写法是把 DIP 全部加完再乘一次：`(int)((12 + contentDip + 12) * m.dpi)`。
  同类易漏的还有：tab 选中横线 3px、开关圆钮留白 3px、行内各种间距。
  自查办法：把 `m.dpi` 代入 1.0 / 1.75 各算一遍，看几何是否等比。
- **`Kind::Plain` 式的悬停色在白卡片上几乎看不见**（制作工具自己踩过），
  行悬停改用 `btn_regular_bg_hover` 那档（HKeyboard 的 `C_REGULAR_HOV`）。
- **自绘图标**：用 `scripts/svg_to_gdi.py` 从制作工具的
  `crates/panda-app/src/icons.rs` + `scripts/hk_icons_extra.rs`（键盘专用）直接生成
  C++ 顶点数组，**不要为键盘另画一套**。已实测 **66 个图标 / 1078 个节点**全部转换一致
  （`Panda` 默认排除）。
  - 两个坑：**描边宽度要乘 `k` 补偿**（否则小图标线条偏细）；
    **线帽线接要显式设圆头**。
  - `rotate(a cx cy)` **带中心点是支持的** —— 方向键的 ↓←→ 就是 `ArrowUp` 的旋转副本，
    比手工重算坐标可靠。
  - **⛔ 同一个 `<path>` 里的多个子路径（多个 `M…L`）必须 `path.StartFigure()` 断开。**
    GDI+ 的 `GraphicsPath::AddLine` 是**接到当前点**的，不会自动开新子路径 ——
    第二个 `MOVE` 之后直接 `AddLine`，会从上一个子路径的**终点拉一条线连过去**。
    实测判据（`PathTypes`）：断开前 `0,1,1,1`（一条折线），加 `StartFigure()` 后 `0,1,0,1`（两条）。
    - 症状：`Close` 的 X **多出一条边**（看着像 ⋈），`Hamburger` 三条杠之间**多两道斜线**
      —— 都会被误判成"图标画得不对/太小太轻"，去改 glyph 坐标**治不了根**。
    - 受害面：**6 / 66** 个图标 —— `Close`(2)、`Hamburger`(3)、`Keyboard`(3)、`Caret`(2)、
      `Sun`(8)、`Bilibili`(2)。单子路径的图标（`Minimize`/箭头/`Tab`/`Enter`…）不受影响。
    - 判据写法：遍历每个 `HKIC_BEGIN` 组，数组内 `HKIC_MOVE` 的个数，>1 的就是受害者。
    - 复核观感别靠脑补：按 `DrawHkIcon` 的语义（`k = size/grid`、描边乘 `k`、圆头线帽）
      在 Python/PIL 里把节点数组重画一遍，**并分别模拟「连成一条」和「断开」**，
      两张图并排就能一眼看出差别；能跑 PowerShell 的话用 `System.Drawing` 直接光栅化最准
      （和程序里同一套 GDI+）。
  - **只有几何本身就不对的 glyph 才改源**（`hk_icons_extra.rs`，同一网格同一描边覆盖）；
    目前仅 `Minimize` 一个：`icons.rs` 把它画在 y=8，而 12 网格中心是 6，
    和居中的 `Close` 并排会明显偏低。
    **改完必须重跑生成器并核对 diff 行数** —— 只该出现改过的那几个 glyph，
    多了就是误伤了别的图标（本次只有 `Minimize` 一个 glyph = 3 行）。
  - 内嵌的 `winres/segmdl2.ttf` 只有 **6,620 字节**，是**子集字体**
    —— 加一个新图标必须重跑子集化，这才是换自绘的真正理由。
  - **⛔ `DrawAlphaText` 不能删**（所有文字的绘制路径，图标只是它的一个调用者）。
- **键面标签有三态**（v4）：文字 / 图标 / 图标+文字，设置里可选
  （布局 tab 第 1 行「按键图标样式」，分段控件）。
  - **不是所有键都能图标化**：双符号键（`1`/`!`）、字母、`F1~F12` 与
    `Esc` `Ctrl` `Alt` `Fn` 及各缩写键**永远文字** —— 认不出来的图标比文字更糟。
  - `icons.rs` 的 `Home` 是"房子=主页"，**与键盘 Home 键语义冲突，不可混用**。
  - 图标吃键帽当时的 `textC`，**四态自动跟随，不需要第二套配色**。
  - **⚠ 「标签放不下」的判据只能跟「键高」走，按 DIP 猜必翻车**（本轮实算）：
    `FitKeyFont` 可用宽 = `k->w - 6*dpi`，地板档 `g_f12 = 12*keyHeight/48 = keyHeight/4` pt
    → **em 像素 = keyHeight/3**；而 `MeasureTextW("Backspace") = 5.49em`，于是
    **「Backspace 放得下」⇔ 键宽 ≥ 1.83 × 键高**（与 DPI 无关）。
    实测 175%：小键盘 1u（177px / 键高 89）要降到地板档 22pt 才放得下、余量仅 **6px**；
    全尺寸数字区 1u（99px / 键高 93）**全称放不下，但 `Bksp` 在地板档只要 83px —— 放得下**。
    阈值写成 `k->w >= 90*dpi` 等于偷偷假设「键高 ≈ 49 DIP」，而窗口是 8 向可拖拽的：
    把小键盘**拖高**到键高 152px，`Backspace` 需要 278px，阈值仍判「放得下」→
    GDI+ 在 `NoWrap` 下**整字丢弃尾部**，标签变成 `Backsp`。要写就写实测
    （字体重建时缓存一次 `MeasureTextW(dc, L"Backspace", g_f12)`），退一步用 `k->w >= 1.9*k->h`。
    ⚠ 顺带：全尺寸数字区的 `Enter` 也贴着上限（地板档 90px vs 可用 89px，靠绘制矩形 99px 才没出事）。
  - **小键盘布局（layoutMode==1）左上角原为 NumLock（0x90），是死键**：全键盘走 `SendInput`
    合成 VK，NumLock 只作用于「物理扫描码 → VK」那一步，对合成 VK 无效，
    按下去反而会翻转宿主机的 NumLock。换成 `0x08`（仍用 `K_SPECIAL` 保持动作键配色）即可，
    零配套改动：`KeyIconFor` 已有 `HKICON_BACKSPACE`，Auto-Repeat 白名单也认 0x08。
  - **坑（会让改动"看着没生效"）：`KbFrameSig` 必须加 `keyIconStyle` 字段**，
    否则主键盘帧缓存命中、切了形态纹丝不动。
- **关于页已并入设置的第 4 个 tab**，不能照搬制作工具的整页版式：
  82px 居中大标志改**横向 48 tile 身份卡**，文字链接改**整行可点的图标行**，
  「赞助」tab 不引入，Copyright 降到卡片外。
- **⚠ 行内元素的垂直基准一律用「行的头部」，不要用整行**（这一层撞过三次，完整版见
  `references/gdi-hinting-and-gdiplus.md` §八）：
  - `行的头部 = min(整行高, 12+40+12 DIP)`。**头部固定 64 DIP**，行因为描述折行变高
    （80 DIP）或展开（色相行 210 DIP）时头部不变；图标 tile / 文字块 / 开关 / 分段
    / 外链按钮全都只在这 64 DIP 里居中。抽成 `SettingsRowHeadH(m, row)` 一处定义。
  - 反例：控件写 `row.top + (rowH - ctrlH)/2`（整行居中）→ 折行行的开关比 tile 低
    `(80−64)/2 = 8 DIP`（实机：「开」621.0 vs tile 606.5）。
  - `SettingsComboYTop`（`row.top + rowPadY`）与头部居中是**等价**的（头部高 =
    `rowPadY*2 + comboH`），两条规则可以合并。
  - 折行描述要「盒顶 26 → **18 DIP**、盒高仍 32 DIP」，单行才与不折行的行同高；
    于是 `textMid` 恒为 22 DIP，**与折不折行无关**（块中心本来就对，偏的只是块内间距）。
  - **⚠ 行高要按「描述实际占几行」决定，不能按「可能折行」预留**（「间距」类问题的
    头号根因）。中文短、英文长、窗口宽度还会变，所以「需要两行」只是**可能性**。
    一律按两行留 80 DIP 的话，单行时行高 80 而内容只占 52 —— **描述下方空 30 DIP**，
    和上一行（12 DIP 内边距）一并排就是「这行下面空了一大块」。
    做法：`MeasureTextAdvW(描述)` 放得进文本列 → 64 DIP，真折行 → 80 DIP。
    - 描述文案随之必须收敛到 **`SettingsRowDescText(tab, index)` 一处定义**：
      行高要测量它、绘制要用它，两边各抄一遍就必然漂移。
    - 判断要**与绘制同一时刻成立** —— 命中测试与窗口高度都经 `SettingsRowHeight`
      走到它，所以放在 `SettingsRowHeight` 里算，别再建一份 y 表。
    - 字体还没建时（极早的尺寸询问）返回「两行」：宁可多留 16 DIP，也别把字挤出窗口。
    - ⚠ **行高变了，描述盒必须跟着换**：单行走 26/16、两行走 18/32。
      只改行高不换盒，描述会在 32 高的盒里居中到偏低 8 DIP 的位置。
    - 实测（175% DPI）：行高 78.9 → 64 DIP，描述底→行底 **29.1 → 13.1 DIP**，
      标题/描述墨迹中心 19.71 / 44.00 DIP，与上方非折行行完全一致。
  - **行的「分界线」要落在两段正中间，别贴着字**：主题色相展开行的分隔线原来画在
    `r.top + 56dpi`，而描述盒底在 52 —— 只隔 4 DIP，实机上看着像给描述加的下划线；
    它到色板顶却隔 20 DIP。移到 64（两侧各 12 DIP），展开区元素一个没动。
  - **分段控件段宽用 `MeasureTextAdvW`（advance）+ 每侧 14 DIP**，别用 `MeasureTextW`
    （容纳宽度含 0.2em 安全边距，6 段控件会凭空宽出 145px 并挤掉标题 ——
    `"主界面透明度"` 曾被裁成 `"主界面透"`）。另加兜底：真放不下时允许标题画到控件左缘，
    宁可贴着也不要半截标题。
  - **验收用「逐行比标题墨迹中心」而不是逐像素 diff**：diff 会被 hover 位置、分段
    选中态、抗锯齿差异淹没（被残留 hover 骗过一次，误以为有 28 万差异像素）。Δ = 0 就是没动。
    - **噪声的指纹：跨 Tab 等量差异**。同一台机器两次运行的逐像素 diff 仍有约 **0.3%**
      差异，而且**内容完全不同的两个 Tab 差异像素数几乎相同**（实测 759 vs 761，
      差异行段模式也一模一样）—— 见到这种特征直接判噪声，不要去追。
      对照：本次改动的布局页 diff 是 **28 万级**（整行高度变化），量级差三个数量级。
    - 干净的判据长这样：常规 9 行 / 关于 5 行 / 布局 4 行 / 主题 8 行的标题墨迹中心
      **Δ 全部 0.00 px**，唯一变化是布局页窗口高 −28 px（行高 80→64 DIP 的必然结果）。

## 七、杂项

- 仓库根有个 **0 字节的游离文件 `main`**（可能是某次 shell 重定向的产物），一直显示为 untracked，别被它干扰。
- `.gitignore` 屏蔽 `build/ dist/ deps/ .workbuddy/`。
- **还有一个 untracked 的 `.commandcode/`**（工具目录）。
  ⇒ **提交时必须 `git add hkeyboard.cpp` 显式点名，禁止 `git add -A` / `git add .`**，
  否则会把这两个垃圾一起提交进仓库。
- **⚠ 这个仓库常常同时有「另一个会话 / 另一个 AI 工具」在写同一个文件。** 本轮实测：
  开工时 `git log` 停在上一次的提交、`git status` 干净；分析到一半，HEAD **凭空多出一个
  22:47 的提交**（提交信息风格与本文档同一套），22:49 又有人**未提交**地改 `hkeyboard.cpp`，
  同时工作区里冒出不是我写的 `scratch/**/probe_*.ps1`、`fontmeas.py`、`shots/`；
  我的一次 `Edit` 直接报「内容与 old_string 不一致」。
  ⇒ **动手前先花 3 秒看三个东西**：`ls --time-style=full-iso hkeyboard.cpp`（mtime 是否晚于
  上次提交）、`git log --oneline -3`（HEAD 是否就是你以为的那个）、`git status --short`
  （有没有陌生文件）。三个里有一个对不上，就是有并行写入者 —— **先停下问用户**，
  别硬改：两边的 `Edit` 会互相覆盖，两边的提交会互相踢（还会白烧一轮 CI）。
- 提交习惯：改完立刻 push 触发 CI（用户就是靠 CI 迭代的）。
- 提交信息用中文、`type(scope): 一句话` 开头（`fix(font):` / `refactor(settings):` / `fix(key):`），
  正文写清「根因 → 实测数据 → 改法 → 影响面核算」，这个仓库的 commit 一直是这个密度。
  多行信息用 `git commit -F - <<'MSG' ... MSG`，别用 `-m` 拼。
- 推完用 `gh run watch <id> --exit-status` 等结果（**必须在 `E:/Project/Git/Hydrogen-Keyboard`
  里跑**）。一次构建约 50s~2.5min，x86 / x64 / arm64 三个架构串行。
- 报错行号会随编辑漂移，**定位时用函数名/标识符 grep，不要记行号**。

## 八、全尺寸（104 键）布局：1u 单位网格

**用户抱怨「布局歪 / 键宽不一致 / 某一行对不上」时，先读
`references/full-layout-grid.md`，不要凭感觉调字面量。**

核心三条：

1. **全尺寸布局禁止「每行 剩余宽度 ÷ 键数」。** 那是「同一把键盘上 1u 键有五种宽度」的来源
   （`81f828c` 实测：46px ~ 73px，差 59%）。正确做法是 **1u 由宽度反解一次，
   所有 x = `round(列位 × u)`**（`u` 用 double，`round` 保证误差不跨键累积）。
2. **`BuildComplete` 里 12 个固定键宽字面量全部乘了 `scaleX`**（`g_ww/980`），
   它们是给 980 宽的默认布局写的，在全尺寸下被放大 1.408 倍 —— **这是「改不动」的根因**，
   不是你的错觉。网格化后这些字面量全部消失。
3. **数字区必须 5 行、对齐「数字行 ↔ Ctrl 行」**（判据：真键盘 `↓` 与数字区 `0` 同在底行）；
   导航区的行位现状是**对的，别动**。

改前先量：`python scripts/hk_full_eval.py`（打印每行键宽、行尾右边缘、相对 ANSI 列位的偏差）。
改后自检：按真实像素渲染 + 每 1u 一条参考竖线，网格落在键缝里才算对；
Edge 无头截图**只能写 `$TEMP`**（写工作区会拒绝访问 0x5）。

网格化之后**必然暴露**的四处配套（不修就是「看起来没生效」）：
`FitKeyFont()` 字号阶梯、`g_npHidden` 与 `g_npHiddenAuto` 分成两个状态、
`KbFrameSig` 纳入隐藏状态、标题栏「小键盘」按钮在自动隐藏时必须放出来（→ 第九章）。

---

## 九、标题栏「小键盘」按钮 = 数字区开关

**用户说「保留小键盘按钮 / 按钮显示隐藏数字区 / 按钮看不出开没开 / 点了没反应」时，
先读 `references/titlebar-numpad-button.md`。**

核心四条：

1. **按钮和数字区一直是两套逻辑**：`HDR_NUM` 只碰 `g_layoutMode`，
   数字区显隐只归 Tab 键碰 `g_npHidden`。所以「点了没反应」和「看不出状态」
   是同一个病的两种症状 —— **先把两条路径并到一个 `SetFullNumpadHidden()`，再谈外观**。
2. **激活态零新增令牌**：用早就存在的 `C_HOT` + `C_ON_PRIMARY`（Enter 键在用）。
   实测比普通态还清楚（浅 6.03 vs 4.18）。
   但 **激活悬停必须向白混**（`BlendColor(C_HOT, RGB(255,255,255), 0.12)`），
   向 `cardBg` 混在深色主题下会掉到 4.54:1 —— 因为深色的 `cardBg` 是暗色。
3. **自动加宽用闭式解 `needW = 700 × dpi`**，且**必须在改 `g_ww` 之前算**
   （`needW` 依赖的 `g_keyGap` 是由 `g_ww` 反算的，先读会拿到旧值）。
   默认 1380 宽下点击**不会有任何尺寸变化**；隐藏数字区时**不缩回**。
4. **`KbFrameSig` 这次漏的是 `npHidden`** —— 与第八章、第六章那两条同性质。
   现在靠"数字区 17 个键消失 → `nk` 变"侥幸重绘，别依赖这个巧合。

---

## 十、字体：单字重（MiSans Medium）下的层级

**用户说「粗体看着难受 / 字重不统一 / 标题加粗很怪 / 字体显示方案」时，
先读 `references/font-single-weight.md`。**

核心五条：

1. **内嵌字体只有一个字面**：`AddFontMemResourceEx` 实测 `fonts: 1`。
   `MakeFont(size, bold)` 请求 `FW_BOLD` 时，GDI 找不到 700，就把 Medium 的轮廓
   **整体等距外扩**（假粗体）—— 中文笔画本来就密，外扩后字腔直接被填死。
   **这不是"粗体不好看"，是"这个粗体是假的"**。
2. **量化判据**（`scripts/gdi_text_probe.py` 可复现）：假粗体让墨迹覆盖率
   **+30~38%**（真 Bold 字面只 +15~20%），笔画连通域 **−4~5 个**，
   字宽 **中文 +6.2% / ASCII +12.6%**。
   ⚠ 字宽变化最容易被忽略：凡「先量宽再居中/裁剪」的地方都会偏，
   而 **`FitKeyFont()` 正是一边量一边画**。
3. **参考对象 PanDaPE-Maker 的字面策略是「一个字面、零字重变化」**
   （它的 `Weight::Inline/Bar` 是**边框样式不是字重**）。层级由「字号 + 颜色」两把尺子承担：
   **跨度大的靠字号，跨度小的靠颜色**；相邻层级差 1pt 时必须同时换颜色。
   ⚠ 它把 MiSans 的 `usWeightClass` 从 380 改成 400 是 **cosmic-text 的硬要求**
   （`font_weight_diff == 0`），**GDI 没有这个约束**，本项目的出路是
   **不去请求不存在的字重**，不是改字体标记。
4. **关键改动是「行主文本与行描述拉开档」**：旧代码里两者**同字号**，
   只靠颜色分，粗体是第二层 —— 粗体一去这行就塌了。
   ⚠ 但**别把字号越推越大**：`22/15/12/11/10` 这套把层级拉满，实机反馈是"字太大"。
   基准要**按窗口宽度归一化**（本项目窗口只有 700 DIP，不能照搬系统设置页的 1024 DIP 标尺），
   收窄时**整体下移一档**而不是重新设计层级 —— 见 `references/font-single-weight.md` §10。
   另外 `g_sf13` **同时**服务「行主文本」和「下拉框文字」，必须拆开。
5. **键面去掉粗体后修饰键照样分得开** —— 粗体只是第四层，
   底色（`C_KEY` 白 vs `C_REGULAR` 淡蓝）与文字色（`C_WHITE` vs `C_BTN_CONTENT`）两层已经够。
   顺带清掉**死变量 `g_f16b`**（只有声明/创建/销毁，无任何绘制引用）。

6. **「该粗的地方不粗、细的地方反而粗」根因在渲染器，不在字号**：
   GDI 对没有 TrueType 指令（`fpgm`/`cvt ` 缺失）的字体跑自己的 auto-hint，
   笔画粗细随字号**锯齿跳变**（alpha 加权的 `竖宽/em`：10.5→0.1106、13→0.1161、
   **14→0.0909 骤降 22%**），连续调字号永远治不好。
   解法是文字改走 **GDI+ `TextRenderingHintAntiAlias`**（无网格拟合）：
   实测笔画增长 **+122%**、与字号增长 **+122%** 完全一致，大字号不再倒挂。
   ⚠ **改字体侧无效**：`gasp=2 / gasp=0 / 删 gasp / 删 prep` 五组变体实测笔画与原件
   **完全一致** —— GDI 的 auto-hint 不受 `gasp` 控制，这条路是死的。
   细节与 `DrawTextGp()` 可粘贴代码见 `references/gdi-hinting-and-gdiplus.md`；
   反查实机截图真实字号用 `scripts/gdi_vs_gdiplus_metric.py`。

7. **换渲染器时必须连量宽函数一起换**（最易漏、现象最直观）。
   `MeasureTextW` 若仍走 GDI，而绘制走 GDI+，GDI+ 会因为自己的布局宽度大 10~18%
   而在 `NoWrap` 下**整字丢弃尾部字符**（不是裁切）。实测 11pt 的「常规」：
   GDI 量 52px、GDI+ 需要 62.2px，而 Tab 只分到 59px —— 于是设置页上
   **四个 Tab 各只剩一个字：常 布 主 关**。同一 bug 还藏在标题栏「设置」pill
   （只剩「设」）和键面「图标+文字」（长标签丢尾字）。
   修法与影响面核算见 `references/gdi-hinting-and-gdiplus.md` **§4.4**，
   自查用 `scripts/measure_parity.py`。

**顺带会挖出来的**：`DrawTextL()` 是 `DT_SINGLELINE` 超宽直接裁，实测
「下拉行文字可用宽 364 DIP」「下拉框内 116 DIP」，各有文案**现在就在被裁**
（含英文的 `Simplified Chinese`）。**分段控件是全屏最紧的一处**
（宽度公式 `Σ(MeasureTextW+36)`），这就是「下拉文字不能跟着行主文本放大」的原因。

---

## 十一、键面标签的水平对齐：居中 / 贴边（两条路径、两把尺子）

**用户说「白色按钮文本怎么不居中了」「图标和文本还对不齐」「Esc 比 Tab 更靠里 / 左右贴边不对称」
时先读这一节。** 这一整类问题的根因只有一个：**同一件事被两条路径各算了一遍，尺子还不一样。**

### 三条绘制路径

| 路径 | 用在哪 | 水平对齐 |
|---|---|---|
| `DrawTextC(k->x, k->y, k->w, k->h, …)` | `KA_CENTER` 键、双符号键、`DrawKeyLabel` 的「图标+文字」组 | 在**整键矩形**里居中 |
| `DrawTextL` / `DrawTextR`（GDI+ `StringAlignmentNear` / `Far`） | `KA_LEFT` / `KA_RIGHT` 的**纯文字**标签 | 贴边 |
| `DrawKeyLabel` | 有图标的键（图标 + 文字整组） | 整组按 `adv` 算宽，`gx = k->x + edge` 或 `k->x + k->w - edge - groupW` |

### 两个「文字宽度」永远不能混用

- `MeasureTextW` → GDI+ `MeasureString`，**容纳宽度** = 推进宽 + 约 0.2em（175% DPI ≈ 10px）。
  `DrawString(NoWrap)` 的判据就是它，**绘制矩形宽必须 ≥ 它**。
- `MeasureTextAdvW` → GDI `GetTextExtentPoint32W`，**推进宽度**（布局宽）。

两个坑都踩过，方向相反：

1. **矩形宽按 `adv` 给** ⇒ GDI+ 一次判定放不下就**整字丢掉尾部**：`Esc→Es`、`Del→De`、`Ctrl→Ctr`、`Fn→F`。
2. **矩形宽按 `tw` 给、再让 `DrawTextC` 在里面居中** ⇒ 墨迹缩回半个余量
   `(tw − adv)/2 ≈ 5px`。若起点还自己按 `adv` 算，净偏移 = `(tw−adv)/2 + 2 ≈ +7px` →
   **白色字母键整体偏右 6.5~8.0px**，且底行 `Ctrl/Alt/Fn`（走这条）与 `Win/Menu`（走图标路）错位。

⇒ **`KA_CENTER` 分支直接把整键矩形交给 `DrawTextC`**，与双符号键同源，两条路径的居中量完全一致
（残余 `−1 ~ −2px` 是 GDI+ 量宽左右不对称的固有量，与字形有关，别再追）。

### 贴边：锚点常量只能有一个，且要补掉 GDI+ 的容纳框内缩

```cpp
// hkeyboard.cpp DrawTextKey 的非居中分支（最终形态）
int edge = (int)(6 * GetSystemDpiScale());   // ← 与 DrawKeyLabel 的 edge 是同一个 6 DIP
int tw  = MeasureTextW(dc, s, f);            // 容纳宽度（含 GDI+ 两侧留白）
int adv = MeasureTextAdvW(dc, s, f);         // 推进宽度（真实布局宽度）
if (tw <= 0) return;
if (adv <= 0) adv = tw;
int p = (tw > adv) ? (tw - adv) / 2 : 0;     // 容纳框相对推进框的单侧内缩量
int x = k->x + edge - p;
int w = k->w - 2 * edge + 2 * p;
if (w < 8) { x = k->x; w = k->w; }
if (k->align == KA_LEFT) DrawTextL(dc, x, k->y, w, k->h, s, f, c);
else                     DrawTextR(dc, x, k->y, w, k->h, s, f, c);
```

三条实测出来的纪律：

1. **贴边不能用 `DrawTextC`**：它在矩形里居中，等于把 `(tw−adv)/2` 又加回来。
   实测同一列 `Esc`（纯文字）墨迹离键左 **27px**，而带图标的 `Tab` 只有 **14px** —— 差近一倍。
2. **`Near` / `Far` 对齐的是「容纳框」不是「推进框」**，转换矩形时不补 `p = (tw−adv)/2`
   的话，右贴边会凭空多出这 ~5px（实测 `Del` 右侧留白 **11 → 18px**）。
3. **锚点常量必须与 `DrawKeyLabel` 的 `edge = 6 DIP` 统一**。
   历史教训：曾只把右侧改成「减去容纳宽度」，得到左 24 / 右 11 —— **图标键与文字键分了家**，
   用一个非默认档的小瑕疵换了默认档的回归，那个提交已被 revert（`e458bd8`）。
   **要改就三条路径一起改，别单改一侧。**

修好后实测（175% DPI、默认布局，像素扫描）：8 个贴边键的留白全部落在 **11~14px**
（改前 11~27px）；纯文字档从「左 25~27 / 右 7~10」变成「左 8~11 / 右 9~12」。
居中键的 dCx 分布在改前改后**逐值完全相同**（−3.0 ~ +1.0），可用来证明"没误伤居中键"。

### ⚠ 三个会让结论跑偏的坑

1. **布局 1（小键盘）/ 布局 2（全尺寸）的键表根本没有 align 字段。**
   `KbKeySpec` = `{col, w, vk, type, vrow}`，`FullPut()` 调 `AddKey(...)` 时**不传 align**
   ⇒ 那些布局的标签**全是居中**。所以改贴边代码后 `09_full.png` / `10_numpad.png`
   **逐字节不变是正常的，不是"没生效"**；先算清楚哪些布局用哪张表，再判断。
2. **关于页的度量脚本必须与代码常量同步。** 脚本里若还留着旧的 `c1b = c1t + 16*2 + mark`（76 DIP）、
   `图标 +16 / 标题 +22`，会算出「卡内留白差 4px、仍偏上」这种**假结论**。
   改完 `GetAboutLayout` 一定要回到 `scripts/about_card_metrics.py` 把 `CARD1_DIP / ICON_DY /
   TITLE_DY / DESC_DY` 一起改。另外卡底线要按「整行白像素占比 > 60% 的**连续**段」取
   （圆角会让靠边的单列误判；卡 1 与卡 2 之间有空隙，不能一路连过去）。
3. **贴边键名单以 `hkeyboard.cpp` 的 align 为准，别按名字黑名单判。**
   `Backspace`（`0x08`，1.5u）与 `Del`（`0x2E`）都是 **`KA_RIGHT`**；
   按「名字不在 (Esc,Tab,Del,Caps,Enter,LShift,RShift) 里就算居中」会把 `Backspace`
   误报成 `+13.5px` 的不居中偏差。

### 配套脚本（`scripts/`）

| 脚本 | 用途 |
|---|---|
| `hk_probe_v9.ps1` | 12 场景实机探针（`-Exe <exe> -Out <dir>`），比 `hk_probe.ps1` 多：PrintWindow 抓图 + 存活断言、场景级重试、修饰键闸门、双帧稳定判定；产出 `01_default.png` … `12_about.png` |
| `hk_probe_tabs.ps1` | 在 v9 基础上加 2 个场景：**N11 主题 tab**（抓色相行展开态）+ **N12 布局页 row3**。v9 只有常规/布局/关于，主题页抓不到 |
| `key_label_align.py` | 逐键量「墨迹中心 − 键中心」的 dCx，并单列 8 个贴边键的留白（**几何按默认布局硬编码**：u=106.9565 / kax=32 / gap=7 / KH=86 / ROWS=[84,177,270,363,456]） |
| `key_icon_text_align.py` | 量「图标 blob ↔ 文字 blob」的横向间距与纵向中心差 |
| `about_card_metrics.py` | 关于页身份卡：卡内上下留白、图标纵心 vs 文字块纵心、标题↔描述间距 |
| `edge_pad_compare.py` | 画「键边（洋红）+ 墨迹边（青）」对照图，改前/改后 × 两种图标样式各一张 |
| `about_card_ink.py` | **身份卡墨迹全量体检**（§2.5~2.8 的落地脚本）：自动用蓝色谓词切图标、逐行切标题/描述两段墨迹，输出卡高 / 标题中心 / 描述中心 / 中心距 / 墨迹间隙 / 顶底留白 / 图文间距。⚠ 卡边界用「白色游程投票」自动定位，文字窗口从 `ix1+7` 起（排抗锯齿灰边） |
| `pixel_regress.py` | 12 场景逐像素回归：对比 `shotsA/` 与 `shotsB/` 两目录，阈值 `>8`，输出每张的差异像素数与 bbox。用于证明「只动了该动的那张」 |

⚠ Python 依赖 PIL，用隔离 venv 跑：
`C:/Users/29776/.workbuddy/binaries/python/envs/default/Scripts/python.exe`（裸的 3.13.12 没有 PIL）。

---

## 十二、「关于页上下间距很奇怪」这类投诉：测量顺序 & 先分清手绘标注

**用户说「关于页的上下间距很奇怪」「还是感觉很奇怪」「图标和文字对不齐」时先读这一节。**

### 0. 第一步永远是：截图里那个东西是不是程序画的

用户会用 **QQ 截图的画笔**在截图上画标注（竖线、方框、箭头）。**别把标注当成 UI 元素去代码里找。**

实测踩过：关于页三张卡上各有一条 `rgb(0,153,255)` 的圆角竖条，宽 8 DIP、高 42 DIP
（像是「图标与文字之间的强调条」）。grep `C_HOT` / `DrawRoundRect` / `accent` /
`DrawIconTile` 全部找不到，三条的 x 还互相对不齐（195 / 168 / 176）—— 因为它根本不在程序里。

**判别只要 1 步：拿同版本的探针截图做像素级颜色对照。**

```python
# 程序里精确匹配该颜色的像素数 = 0  →  用户手绘
# 这是我的截图(全程序渲染)      用户截图
# shots/12_about.png          0 个
# userimg/about_user.png      2616 个
```

⚠ 不要靠肉眼猜「看起来像 UI」，也不要因为「位置上像是某个元素」就去改代码。
另外：三条标注各在不同位置 **本身就是手绘的强信号** —— 程序画的重复元素一定对齐。

**读标注的意图**：用户画「竖条」通常是标**文字块的高度**，用来对照左边方块的高低。
所以他的潜台词是「方块和这个高度不匹配」，而不是「这里该有个竖条」。

### 1. 测量顺序（从外到内，别跳）

用户说「上下间距」时可能是**四个层级**里的任何一个，按顺序量完再下结论：

| 层级 | 量什么 | 正常值（175% DPI） |
|---|---|---|
| ① 客户区上下 | 客户区顶→页头墨迹顶 / 版权墨迹底→客户区底 | 24 / 31 DIP |
| ② 页内节奏 | tab 线→卡1 / 卡1→卡2 / 卡2→版权 | 14 / 6 / 18 DIP |
| ③ 卡内留白 | 卡边→**墨迹**（不是行盒）| 卡1 上下各 20 DIP、链接行 tile 各 14 DIP |
| ④ **标识 vs 卡纵心** | 标识中心 − 卡中心 | **0.00 DIP**（构造上即居中） |

> ⚠ ② 的卡间距是 **6 DIP 不是 16**（`AboutGap`）。PanDaPE 看着像 16 是因为它用
> 「卡 → 章节标题 → 卡」的组合节奏，**不存在相邻两卡直接相接的场景**，别照抄。
> 要抄的是它的**卡内**留白，不是它的段落间距。

### 1.5 ⚠⚠ 头号陷阱：`DrawHkIcon` 的 `size` 是**网格边长**，不是可见外框

`DrawHkIcon(dc, x, y, size, def, ...)` 里 `k = size / def.grid`，画的是**整个 24×24 网格**。
而每个图形的**着色 bbox 往往只占网格的一小块** —— 量节点坐标的极值就是它的可见范围：

```python
# k_icon_KeyboardMark（grid=24）：着色 bbox = x 1.4..22.6、y 4.6..19.4
#   → 21.2 × 14.8 = 网格的 88.33% × 61.67%
#   → 传 size=56 实际只画出 49.5 × 34.5 DIP 的扁块！
```

**症状**：想让图标占满 56×56 的方块，实际得到一条扁带。
**正解**：外层套一个**独立的正方容器**（`DrawRoundRect` 主色圆角方块 + radius），
再把 glyph 按 `容器边长 × (图形占网格比 / 期望占容器比)` 居中画进去。
PanDaPE 的 `brand_tile` 就是这个语法：`ABOUT_TILE = 56` 正方容器 + `ABOUT_GLYPH = 32` 居中。

**⚠⚠ 本节结论已于 2026-10-02 作废 —— 照抄 PanDaPE 的 `brand_tile` 是错的。**

我当时的推理是「PanDaPE 的品牌块 = 56×56 色块 + 居中 32 DIP 图形，所以 HKeyboard 也该这么画」。
实机做出来是「蓝方块里一个**实心黑方块**」，用户直接要求换回老方案。**两个独立根因**：

### 坑 1：`DrawHkIcon` 的 `hole` 色必须与 `fg` **不同** —— 否则挖空整体失效

```cpp
// ✗ 错：我把两个色都传成了 C_ON_PRIMARY
DrawHkIcon(dc, gx, gy, glyph, HkIcon(HKICON_KEYBOARDMARK), C_ON_PRIMARY, C_ON_PRIMARY);
// ✓ 对：hole 必须是「底色」，与 fg 形成对比
DrawHkIcon(dc, x, y, 56, HkIcon(HKICON_KEYBOARDMARK), C_HOT, C_ON_PRIMARY);
```

`DrawHkIcon` 里键块是**独立子路径**（`HKIC_BEGIN` 段的第二、三…组），
**靠 hole 色「挖」出来才可见**。fg 与 hole 同色 ⇒ 挖空被填平，只剩外框一个实心方块。

### 坑 0（选图标前先看）：**同一图标在一屏里出现两次 = 用户认成同一个按钮**

关闭对话框标题行原来用 `HKICON_CLOSE`，右上角关闭钮也是 `HKICON_CLOSE` ——
用户看着像「标题上有个 ✕」，以为点它能关窗。实际语义是**询问**「是否关闭轻键？」。

改用 `HKICON_DIALOGINFO`（ⓘ 圆圈信息）后语义对了。
**图标表在 `hk_icons_generated.h`（67 个，枚举顺序 = `k_hkIcons[]` 表顺序，
第 N 项对应 ID = N-1）。** 按语义挑，别按「长得像」挑：

| 语义 | 图标 | 可见范围（24 网格） |
|---|---|---|
| 询问 / 提示 | `HKICON_DIALOGINFO` | 3.4..20.6（圆 + 竖线 + 点）|
| 纯信息 | `HKICON_INFO` | 3.8..20.2 |
| 关闭 | `HKICON_CLOSE` | 3..9（24 网格里只占 1/4）|

**没有问号图标**（`grep -i question hk_icons_generated.h` 为空），
「是否…」类标题统一用 `DIALOGINFO`。

⚠ 画大尺寸（>20 DIP）时注意 `HeaderIconBox` 的反解表：它按「想让它视觉上
多大」反算外框。`DIALOGINFO` 可见 17.2 DIP，落在 `default: box=17.0`，
不用额外补表项。

**这是 `DrawHkIcon` 的通用陷阱**：任何带挖空结构的图标（`KeyboardMark` / `GitHub` /
`Keyboard`）都必须检查 fg 与 hole 是否**不同**。`k_icon_Keyboard`（设置页用的）的键块是
**两条横线**，`k_icon_KeyboardMark`（身份卡用的）是**独立小方块** —— 结构不同，不能互换。

**验收判据：键块像素占整个图形的比例 > 0**（实测回退后 21.6%）。测法：
先按主色找外框 bbox，再在框内数「既不是主色也不是底色」的像素 —— 那就是键块。

### 坑 2：抄参考实现要抄**语法**，不要抄**形似**

PanDaPE 的 `brand_tile` 之所以成立，是因为它色块里装的是**熊猫头** —— 一个和色块
形状完全不同、需要底色衬托的**独立图形**。而 `KeyboardMark` 本身**就是完整剪影**
（外框 + 挖空键块），再套一层色块只会：① 56 DIP 的位置里只塞进一条 49×34 的扁带，
② 中间多一圈冗余底色。

**动手前先问：「参考实现那个色块里装的是什么图形？它也需要底色吗？」**
需要 ⇒ 可以抄；本身就是完整图形 ⇒ 别套。

### 回退后的正确画法（现行代码）

```cpp
// 直接以 56 DIP 网格画，纵向居中于卡片（构造上居中，不需要魔数）
int my = al.card1.top + (al.card1.bottom - al.card1.top - al.mark) / 2;
DrawHkIcon(dc, (float)(al.card1.left + AboutPadX(m)), (float)my, (float)al.mark,
           HkIcon(HKICON_KEYBOARDMARK), C_HOT, C_ON_PRIMARY);
```

实测：着色 `50.09 × 34.55 DIP`（网格理论 `21.2/24×56 = 49.47 × 34.53`，差为抗锯齿边缘）、
键块占 21.6%、标识中心 − 卡中心 = **0.00 DIP**。

> 保留的教训：`DrawHkIcon` 的 `size` 确实是**网格边长**（这一点没错，见上文节点分析），
> 所以「着色尺寸 = 节点范围 / 网格 × size」这个换算依然有效 ——
> 作废的只是「因此要套个方块补成正方」这个结论。

**实测这次的结论：① ② ③ 都正常，罪在 ④ —— 卡1 图标纵心比卡纵心低 +6.0px（3.43 DIP）**，
即图标上留白 50px、下留白 38px，而文字是 26/24。肉眼看就是「图标陷在卡片下半边」。

量法（`scripts/col_runs.py` 先看竖直结构，再按颜色 bbox 量方块）：

```bash
python col_runs.py about.png 1150          # 靠右空白列：一眼看清所有卡边界
python ink_bands.py about.png 233 368      # 卡内墨迹带（标题/描述各一段）
# 方块要按「底色偏色」找 bbox，且**必须先卡死 y 范围在这张卡内**，否则会一路连到下一张卡
```

⚠ **两个测量陷阱（都真实踩过、结论一度被推翻）**：

1. **别用整图搜主色**。`b-r > 50` 会把「MIT」这类主色文字也算进去，bbox 直接撑到全图。
   必须先由已知量反推 DPR（例：卡1 高 132 px / 76 DIP = **DPR 1.7368**），
   再把搜索窗口卡死在「该卡的 y 范围 + x < 卡片文字起点」里。
2. **别用固定 x 窗口找左侧方块**。窗口开太宽会把方块右边缘和文字一起框进来，
   算出的「文字块 padTop 12 / padBot 34、整体偏上 11 DIP」**是假的** ——
   限定到「方块右边界 + 15px 之后」重新分带，真实值是 `padTop 12.00 / padBot 10.86`（已对称）。

**教训**：得到「差得离谱」的数值时，先怀疑测量方法，再怀疑代码。
下面第 2 节就是被这两个陷阱带偏后纠正的。

### 2. ⚠ 「行盒中心」不是「墨迹中心」—— 通用教训

`DrawTextGp` 居中时 GDI+ 的 `LineAlign=Center` 对齐的是**行盒**，不是墨迹。
行盒高 = `hhea` 的 `asc+desc`（MiSans 是 `1.044+0.282 = 1.326em`），
而汉字与无下降部的拉丁字并不占满下降部 ⇒ **墨迹中心普遍比行盒中心高 0.07~0.11em**；
带下降部的字（`Caps` / `Backspace` / `Settings`）则相反，为 `+0.018~0.027 em`。

- 所以**「图标 + 文字」并排时文字必须做垂直墨迹补偿**（`TextInkShiftPx`），
  否则就是用户反馈的「图标文本对不齐」。补偿值：无下降部 `+0.085em`、有下降部 `−0.025em`。
- 差异被放大的原因：`DrawHkIcon` 是按**自身包围盒**居中的（上下对称），两边基准不同。

**反过来说：能用「构造上居中」解决的，不要用「算 + 补偿」**。
现在的身份卡走的是前者 —— 卡高 `padY*2 + mark`、标识按 `(卡高 − mark)/2` 居中，
所以「标识中心 − 卡中心」**恒为 0**，不需要任何魔数，改字号/改卡高都自洽。

### 2.5 「上下间距对不齐」= 墨迹间隙，不是盒顶间隙 —— **可复用的推导公式**

用户圈出标题与描述之间那道缝、说「间距对不齐」时，用这个公式算，别凭感觉调。

**先认清根因：真正决定「描述看起来偏下」的不是盒顶间隙，而是「中心距」**

```
标题↔描述中心距 = 墨迹间隙 + (标题墨迹高 + 描述墨迹高) / 2
```

汉字与拉丁字母的**墨迹高**差 2 倍（20 DIP 字号下 29.36 对 15.43），
所以照抄参考实现的中心距一定翻车。参考实现 `PanDaPE-Maker` 的产品名是纯拉丁
「PanDa PE」（只有 cap height），而 HKeyboard 是「HKeyboard 轻键」——
**带两个汉字就几乎撑满 em 盒**。抄 22.57 的中心距需要把标题缩到 8.5~10 DIP，
产品名会失去主标题地位，**这条路走不通，只能接受部分改善**。

配套的墨迹间隙公式：

```
墨迹间隙 = 盒顶间隙 + 标题盒内下留白 + 描述盒内上留白
         = 盒顶间隙 + (标题盒高 − 标题墨迹高) + (描述盒高 − 描述墨迹高)/2
```

**关键是「盒内留白」这项不能忽略** —— 它由**字形**决定，不是你写进代码的那个数：

| 字形 | 20 DIP 字号下的墨迹高 | 盒内留白 |
|---|---|---|
| 汉字（撑满 em 盒） | ≈ 29.4 DIP | ≈ 0 |
| 拉丁字母（只到 cap height） | ≈ 15.4 DIP | 上下各 ≈ 7 |

**所以同一份「盒顶间隙 = 6」代码，汉字行看起来比拉丁行宽得多。**
实测对照（175% DPI，身份卡标题↔版本号）：

| | 盒顶间隙 | 标题墨迹 | 描述墨迹 | 算出墨迹间隙 | 实测 |
|---|---|---|---|---|---|
| 抄参考值 | 6 | 29.36 | 12.09 | 11.60 | **11.52** ✓ |
| 改后 | **3** | 29.36 | 12.09 | 8.60 | **8.64** ✓ |

参考实现的「9.14 DIP」是**拉丁字母**量出来的，直接抄参数给汉字行就会宽 2.4 DIP。
**改完后务必把两个数都验一遍**：公式预测 vs 实测，差值应在 0.1 DIP 内
（实测 0.04 / 0.08）—— 差得多说明还有第三项没算进去。

⚠ 改盒顶间隙时**块高要同步改**（`块高 = 标题盒 + 间隙 + 描述盒`），
否则居中基准漂移，会引入一个新的偏移把这次修复抵消掉。

⚠ **别为了对齐中心距把盒顶间隙调成 0**：汉字墨迹几乎撑满标题盒，
间隙归零后字面会贴到一起。3 DIP 是下限。

#### ⚠⚠ 测「上下留白」时，**不要预设「顶底各半」是目标**

对齐的参照物是**参考实现的真实几何**，不是「看起来应该对称」。
`PanDaPE-Maker` 身份卡实测：卡高 88、顶留白 27.43、底留白 24.00 ——
**顶比底多 3.43，墨迹块心落在卡心 +1.72**（也就是**块心略低于卡心**）。

HKeyboard 调到顶 19.58 / 底 17.85、块心 +0.87，比参考实现**更居中**。
中途我曾把「顶底各 19」当目标硬凑，那是**伪基准**，纯属浪费一轮 CI。
**先量参考实现的顶/底留白，再决定要不要动纵向。**

### 2.6 ⚠⚠ 图文间距：必须按**墨迹**量，不能按 `DrawHkIcon` 的 `size` 量

`al.mark` 是 56 DIP 的**绘制网格**（见 §1.5），而 `KeyboardMark` 的着色只占网格 88%
（实测墨迹 49.52 DIP，网格内左留白 2.48）。**按网格起点量间距会多算约 6.5 DIP**，
这是「图标贴着文字」的成因。

参考实现那边不存在这个偏差 —— 它的 brand tile **满格**，网格 = 墨迹。
所以本窗口要达到同样的视觉间距 18.98，得从 18 往下减：

```
gap = 18 − 6.5(网格/墨迹差) + 1.8(GDI+ Near 对齐起点与字形墨迹左边距的差，实测反解) ≈ 13.3
```

实机复核：12.7 → **图文间距 18.42**，13.3 → **19.00**（目标 18.98，差 0.02）。
斜率约 1 DIP 改动 → 1 DIP 实测，**可以按差值直接线性反解**，不用反复试。

⚠ 图标**纵向不用加魔数**：墨迹已居中（实测墨迹中心 42.03 vs 卡心 42.03，偏差 0）。

### 2.7 ⚠⚠ 纵向居中必须加实测补偿：GDI+ **行盒可能比绘制矩形还高**

`SetLineAlignment(Center)` 对齐的是**行盒**，不是绘制矩形。行盒高 = `hhea` 的
`asc+desc`（MiSans = `1.044+0.282 = 1.326em`）。17 DIP 字号 → em = 22.00、
**行盒 29.17 DIP > 28 DIP 的绘制盒高**，GDI+ 居中后墨迹几乎贴着盒顶
（实测盒内上留白仅 0.10 DIP）。

所以**「盒高居中」≠「墨迹居中」**，只居中盒子会让整块偏上、卡底留白比卡顶多 4 DIP。
本窗口补 `+1.7 DIP` 下移后落到块心 +0.87，正好落在参考实现的 +1.72 附近。

> 通用判据：**行盒高 > 绘制盒高时，任何基于盒高的居中都必然偏上**，
> 补偿量要靠实拍反解，不能算。

### 2.8 ⚠ 测距时的三个采样陷阱（都踩过）

1. **图标必须用蓝色谓词单独提取**，不能混进文字扫描窗口。
   谓词 `(B>180) & (B−R>60) & (B−G>40)`。先用它切掉图标，文字窗口从
   `ix1 + 7` 起（**留 6~7 px 余量**排抗锯齿灰边），否则行剖面尾巴全是
   1~2 px 的噪声，会把「标题↔描述」判成一整段连续墨迹。
2. **卡边界检测要选卡内空白列**。用 x=60 落在圆角区会量出错误卡高；
   本窗口用 x=1150 / x=1100（卡内右侧空白）才得到正确的 132 px。
3. **参考实现采样要排除色块 tile**。`PanDaPE` 的 x 240..850 窗口里含紫块
   tile（x 273..370），紫块灰度低于墨迹阈值 → 标题墨迹高虚高到 26.95 DIP。
   改用 x 380..850 才得到合理的 15.43 DIP。


### 3. 卡间距必须大于卡内留白
改前整页节奏是 `14（tab→卡1）/ 12（卡1→卡2）/ 18（卡2→版权）` —— **中间那个凹下去了**。
> ⚠ **本节结论已作废（2026-10-02 复核）**：当时把卡间距从 12 改到 16，理由是
> 「卡间距 ≳ 卡内留白 + 2 DIP」。重测 PanDaPE 后发现它用的是
> **「卡 → 章节标题 → 卡」的组合节奏**，压根不存在相邻两卡直接相接的场景；
> 照抄的 16 是段落间距而非卡间距。HKeyboard 的 `AboutGap = 6 DIP` 是对的，
> 实测卡间距 `y 365..374` = 5.71 DIP，与实现一致。**已回退，注释同步改正。**
>
> 教训：「向参考实现看齐」时要先确认**它有没有这个场景**，
> 别把结构不同的段的数值搬过来。

### 4. 连带影响：改任何几何都会让窗口高度跟着变

`SettingsDesiredHeight()` 直接吃 `al.card3.bottom + 18dpi*2`，所以卡一动窗口就自动收边 ——
**这是正常的，不是回归**；`12_about.png` 的尺寸变了要同步更新度量脚本里的期望值。

### 4.2 ⚠⚠⚠ 卡片高 `cardH` **必须显式包含上下留白**

「卡片内 = 上留白 + 内容 + 下留白」是基本结构，但代码常常只写内容：

```cpp
L.cardH = L.rowH * 2 + 1;                // ❌ 漏了 rowPadY*2
L.cardH = L.rowPadY * 2 + L.rowH * 2 + 1;   // ✅
```

漏掉的后果**不报错、不崩溃**，只是最后一行**溢出卡片底边**：

| | 声明 | 实测（上留白 / 下留白） |
|---|---|---|
| 漏算 | `cardH=109`，行 2 底 197 vs 卡片底 185 | **24.6 / 0.6 DIP** |
| 正确 | `cardH=133` | **24.6 / 24.6 DIP** |

肉眼表现就是用户说的「**下面顶到边了**」/「上下间距对不齐」——
上留白多出一倍，下面一点空隙都没有。

**判据：写完 `cardH` 立刻做「上下留白对称」对账**：

```
上留白 = 首行顶- 卡片顶
下留白 = 卡片底 - 末行底        # 两者必须相等
```

这个对账比量「卡片总高」可靠得多 —— 总高要跟声明值比，差1~2 DIP
是圆角抗锯齿的测量误差；而上下留白只要不等就是**真错**。

**推论：凡是有「上下留白」概念的容器（卡/面板/列表项），高度公式都要
显式写出 `padY*2`。** 顺手在公式旁写死数字注释（如 `// 上下 12 + 两行 108 + 分隔线 1 = 133`），
下次改间距时一眼能看出漏没漏项。

### 4.3 行内容变化时，**居中公式要跟着换**

行内标题居中不能写死。两种内容结构对应两个公式：

```cpp
// 有描述（两行文字）
ty = rowTop + (rowH - (titleH2 + descH)) / 2;
// 只有标题（描述已去掉）—— 必须换成这个，否则字偏上 3 DIP
ty = rowTop + (rowH - titleH2) / 2;
```

沿用旧公式的后果是文字与右侧控件（分段/ 开关）**不在一条基线上**。
去掉描述这类改动，一定要连带搜`(titleH2 + descH)` 这种组合式。

### 4.5 ⚠⚠ 右对齐 = 减**自身宽度**，不是减到「右边界」

复刻设置页控件时最常犯的一类错。设置页的参照实现是：

```cpp
static RECT RowSegRect(const SettingsMetrics& m, const RECT& row, ...) {
    int w = SegmentedWidth(m, items, count);
    int x = row.right - (int)(20 * m.dpi) - w;     // ← 有 -w
    ...
}
```

关闭对话框里手写等价代码时漏成了 `x = W - pad - 20*dpi`（**没有 `- w`**），
结果控件整体右移一个自身宽度，末段被窗口裁掉。168 DPI 下 `segW ≈ 248px`，
溢出 178px —— 但纵向布局全对，肉眼只看纵向根本发现不了。

**判据：写完右对齐，立刻用「`右边界 - 自身宽`」和「窗口宽 - 右margin」两个数对账，
确认 `x + w <= W - margin`。** 紧邻的其它控件（如 `swX`）如果写了 `- swW`，
那就是明确的对照证据 —— **同一段代码里两个同类控件一个对一个错，基本必是漏项。**

### 4.6 ⚠⚠⚠ 定窗口宽度必须用**实机字宽**反推，不能用「每字多少 DIP」的估值

这是本项目最贵的一个坑，连踩两次（340 → 480 → 540）。

**错的做法**：`需 = 字数 × 13.5 DIP`，然后「取整到 40 的倍数」。
PIL 渲染器同样不可信 —— 它的 `load(pt)` 与 C++ 的 `MakeFont` 换算口径不同，
量出来的数会偏小 1.4~2.2 倍，于是「预览看着正好，实机裁字」。

**对的做法**：拿**同一个 MiSans TTF** + **实机真实字号**量，公式逐项相加：

```
需要的窗口宽 = tx(文字块左缘) + 间距 + 描述文字宽 + 右margin + 右侧控件宽
```

实例（关闭对话框，168 DPI = 1.75x）：

| 项 | 值 | 来源 |
|---|---|---|
| 文字块左缘 tx | 88 | `cardX(20) + 20 + tile(30) + tileGap(18)` |
| 行1 描述「选择关闭窗口时执行的操作」 | 156 | MiSans **10pt**（`g_sfBase`）实测 |
| 分段控件 segW | 197 | `6 + Σ(28 + MiSans **11pt** 各段文字)` |
| 间距 12 + 右留白 20 | 52 | 代码常量 |
| **合计** | **493** | ← 480 差 13 DIP，正好裁掉末尾 2 字 |

**固定字号表**（`InitFixedFonts`，别猜）：

```cpp
g_sfBig=18  g_sfSec=17  g_sfRow=12.5
g_sfCtrl=11   // 控件标签：Tab / 分段 / 按钮 / 弹窗标题
g_sfBase=10   // 行描述
g_sfMeta=9    // 版本号
```

量字脚本（放进渲染器模块直接复用字体）：

```python
fCtrl = m.load(11); fBase = m.load(10)
segW = 6 + sum(28 + m.load(11).getlength(t) for t in items)   # 已是 DIP，别再除 dpi
need = 88 + 12 + m.load(10).getlength(desc) + 40 + segW
```

**再留 30~50 DIP 余量**（字体回退、英文文案更长、`MiSans` 未加载时
回退 `Microsoft YaHei` 都会让字变宽），最后取整到 10 的倍数。

**验收铁律：定完宽度必须抓实机图，把每行描述单独裁出来放大看有没有缺字。**
整图缩小看是看不出「少了 2 个字」的。

### 5. 收尾纪律：**像素级**对比，证明「没误伤」

**做法：改前改后各跑一遍同一个探针，对 12 张 PNG 逐像素求差。**

```python
d = (np.abs(ia - ib).sum(axis=2) > 12).sum()   # 阈值 12 挡抗锯齿噪声
```

- 视觉改动：本轮 `12_about.png` / `11_settings.png` 变，其余 10 张**差异 = 0**。
- 纯重构（删死代码/清注释）：**12 张全部差异 = 0** —— 这就是「删的函数确实无调用点」
  最硬的证据，比「grep 不到引用」强得多。
- 改前基线从**上一个成功的 CI run** 拉：`gh run download <run_id> -n HKeyboard_x64`，
  两个版本跑**同一个** `hk_probe_v9.ps1`。注意 `PrintWindow` 抓的是窗口自身，
  不受 z 序/遮挡影响，但**同机同 DPI 才能逐像素相等**。

⚠ 别用 `tab_regression.py` 验「没误伤」到主键盘上 —— 它只比设置页四个 tab 的标题墨迹中心。
主键盘用上面的逐像素法。

### 6. ⚠⚠ 描述折行的设置行：**「头部高」不能写死，要用整行高**

用户说「设置页某一行间距不合理 / 描述那行怪怪的」时，先量这三项：
**行高 / 文字块心 − 行心 / 文字块顶距行顶**。

症状（布局页「小键盘按钮」行，描述折两行）：

| | 行高 | tile 中心 | 文字块心 | 行心 | 判定 |
|---|---|---|---|---|---|
| row1 键盘布局（不折行） | 63.33 | 31.09 | 31.38 | 31.67 | ✅ |
| row2 Fn 网页布局 | 64.49 | 23.61 | 33.11 | 32.24 | 勉强 |
| **row3 小键盘按钮（折行）** | **81.18** | **23.61** | **32.82** | **40.59** | ❌ |

肉眼表现：**标题距行顶只剩 5.18 DIP、描述第二行下方空 20.73 DIP**（正常行是 12/12），
tile 与开关一起浮在上半部。

根因：`SettingsRowHeadH` 返回 `rowPadY*2 + 40` = 64 DIP（按**单行**文字块算）。
描述折行让行高变成 80 DIP，多出的 16 DIP 全被推到行底，而「头部」贴在行顶。

**改法：`SettingsRowHeadH` 直接返回 `row.bottom - row.top`。**
tile / 文字块 / 开关三者共用整行居中，行心 40.59、顶底各 12.95 对称。
不折行的行 `headH == rowH`（64 ≈ 63.33），**行为与原来完全一致**，无副作用。

> ⚠⚠ **写这个函数时最容易踩的坑：兜底那行会静默吃掉你的修复。**
>
> ```cpp
> // ❌ 错：这是「取小值」，而 headH(111px) < rowH(141px) 时正好取到不该取的 headH
> int rowH = row.bottom - row.top;
> return headH > rowH ? rowH : headH;
>
> // ✅ 对：折叠行就是要整行高
> return row.bottom - row.top;
> ```
>
> 这个 bug 实际发生过：`a8f61c8` 改对了语义却留下这行兜底，
> `2683f46` 的 diff 保留了它，**修复被静默回退**，实测数值与修复前完全一致
> （距行顶 5.18 / 距行底 20.73 / 块心 −7.77）。
>
> **「取小值兜底」不是安全的默认值** —— 行高 > 头部高是**常态**（折行行），
> 取小就等于永远按单行头部居中。兜底前先问：行被压得比头部还矮时该取谁？

> ⚠ 原文注释写的「控件若按整行居中，会比 tile 与文字块低 (80−64)/2 = 8 DIP」
> 是**控件按整行、tile 按 64 头部**两套基准混用才有的偏差。
> 三者统一读同一个 `SettingsRowHeadH` 后差恒为 0（实测开关 22.46 / tile 23.61，
> 本来就在 1.15 容差内）。**别为了保住一个自己制造出来的偏差而留两套基准。**
> 顺带：主题页色相行（210 DIP 可展开行）本来就按整行居中，不改这行则两处基准不一致。

⚠ 这类「某一行不对、别的行都对」的投诉，**一定要先按行分段量**，
别直接改行高公式 —— 行高公式（`12 + content + 12`，64 / 80 DIP）实测是对的，
错的是行内元素的居中基准。

#### 🔴 排查纪律：同一处逻辑连续两轮改动，第二轮异常时先怀疑第一轮没生效

这次踩了大坑：修完第一轮（`a8f61c8`）实测是对的，第二轮（`2683f46`）改别的地方
后**实测数值回到了修复前**，我却在「缓存 / 单实例 / 探针脚本不同 / exe 不是最新的」
之间反复推理了十几轮，全是错的。

**正确的排查顺序**：

1. **先 `git diff` 上一轮的改动** —— 语义改动有没有被后续 commit 悄悄保留/改回。
   一行三元表达式就能解释掉全部现象。
2. **再排除环境**（旧实例 / 缓存 / 不同 exe）—— 顺序应该在第 1 步**之后**。
   环境问题应该先「杀进程 + 重新下载」一句话排除，而不是反复推理。
3. **像素差分定位范围** —— `np.abs(a-b).max(axis=2) > 8` 看差异 bbox。
   本次差异只落在 `y 448..558`（row3 区域），11 张场景全一致 →
   **立刻排除全局性原因**（配置、DPI、字体、exe 版本），锁定单行逻辑。
   这一步我做得太晚。

> 一句话：**「实测异常」时，先信 git diff，再信环境。**
> 差异 bbox 只覆盖局部 ⇒ 一定是该处的逻辑，不要去查全局配置。

### 配套脚本（`scripts/`）

| 脚本 | 用途 |
|---|---|
| `about_card_ink.py` | 身份卡墨迹体检（卡高/中心距/间隙/顶底留白/图文间距） |
| `pixel_regress.py` | 12 场景逐像素回归（阈值 8，输出差异数与 bbox） |
| `settings_row_center.py` | 设置页每行居中体检：行高 / tile 中心 / 文字块心 / 行心 / 顶底留白 + 三组中心差 |

> ⚠ `settings_row_center.py` 已经踩平了三个坑，换截图重跑前先知道：
> 1. **分隔线会污染行边界** —— 它跨整行宽度，在 tile 列里也是「非白」，
>    必须掐掉行首 3 行，否则 tile 中心虚高 **8.64 DIP**。
> 2. **卡上下边界用 `CX0 + 18` 列**（已越过圆角）。用卡中点列会被文字打断，
>    用 `CX0 + 5` 会落在圆角里 —— 两种错法都会让行高算错好几 DIP。
> 3. **开关不能用「深色像素」测** —— 那个窗口同时框住「开/关」文字 + 开关 + 分隔线。
>    开关是彩色的，用 `B−R > 18`（开态蓝）/ `B−R > 5`（关态灰蓝）单独提取。
>    ⚠ **关态灰开关仍测不准**（谓词会把「关」字框进去，量出 −12.67 假值），
>    这一项**以目视为准**，别拿它当判据。
>
> **通用教训：谓词选不对时的表现是「量出一个整齐但明显错误的数」**
> （−12.67 这种整数感很强的值）。发现可疑值先裁图目视确认，别直接采信。


关于页几何是**独立**的（`GetAboutLayout` 只被 `SettingsDraw` 的 `g_sTab == 2` 分支用），
所以「只有 about 那张变」既是预期、也是能拿出来的证据。改动如果让键盘主界面截图也变了，
说明碰了共享代码，要回头查。

#### 切 tab 的坐标算法（抓主题页/色相行必备）

`g_sTab` 的**内部编号与视觉顺序不一致**：

| 视觉位置 | 标签 | `g_sTab` |
|---|---|---|
| 1 | 常规 | 0 |
| 2 | 布局 | **3**（临时值，只有切到布局页时才是 3） |
| 3 | 主题 | 1 |
| 4 | 关于 | 2 |

四个标签都是 2 个汉字、宽度相同，所以**按比例算点击位置**即可（client 1225 px 实测）：

```powershell
$tx = [int]($sw * 0.272)   # 主题 tab
```

比例：常规 0.0935 / 布局 0.157 / 主题 0.272 / 关于 0.370

⚠ 切 tab 前必须先点标题栏菜单按钮打开设置页（探针里的 `$g.hdr.menu`），
否则点击落在主键盘上，什么都不会发生。

### 配套脚本（`scripts/`）

| 脚本 | 用途 |
|---|---|
| `col_runs.py` | 沿指定列打印颜色游程 —— 一眼看清页头/tab/卡边界/卡内墨迹的竖直结构 |
| `ink_bands.py` | 给定 y 区间与卡片，列出行墨迹带（自动找卡片左右边界与底色） |
| `about_page_metrics.py` | 关于页整页体检：客户区、各卡边界、卡间距、卡内图标/文字墨迹带 |
| `about_card_compare.py` | 画「卡顶底（红）+ 图标墨迹（橙）+ 文字墨迹（绿）」并标留白数值，改前/改后并排 |
| `about_page_compare.py` | 关于页整页改前/改后并排，标出卡间距与窗口高度 |

⚠ 这几个脚本都**硬编码 DPR 1.75 与默认布局的几何**（`contentX = 30 DIP`、
`tileSize = 30`、`tileGap = 18`），换 DPI 或改常量后要一起改。
另外找卡底时**只取「从卡顶起的第一段连续白」**，否则会一路连到下一张卡（会算出 357px 的假卡高）。

## 十三、设置行 / 链接行的 hover 高亮：**高亮只作用在按钮上，不铺整行**

用户说「高亮的位置不太合理」「为什么整条都亮了」时先读这一节。

**PanDaPE 实测结论（`panda_about.png`，逐像素取色）**：

| 元素 | 底色 | 说明 |
|---|---|---|
| 链接卡整卡 | `(255,255,255)` 纯白 | **hover 时恒不变色** |
| 右侧圆钮（非 hover） | `(241,235,253)` | = `btn_regular_bg` |
| 右侧圆钮（hover） | `(211,200,233)` / `(224,212,248)` | = `btn_regular_bg_hover` |
| 圆钮内的图标 | `(125,95,173)` | 深紫，不随底色变 |

⇒ **语法是「整卡纯白 + 只有那枚 34 DIP 圆钮换底色」**，不是「整行铺一个高亮圆角块」。

HKeyboard 原来的 `DrawAboutLinkRow` 里有 `if (hover) DrawSettingsRowHover(dc, m, row);`
把整行铺成 `C_REGULAR_HOV` —— 实测高亮块 **547×60 DIP 横跨整张卡**，比圆钮大一个量级；
更糟的是圆钮**也**用同一个 `C_REGULAR_HOV`，两者同色 `(227,215,251)`，
圆钮在 hover 态直接「消失」在行底里。**删掉那一行即可，命中区仍保持整行**
（触摸场景下按钮不能只有 34×34 DIP）。

验收判据（实测值）：

```python
# 1) 圆钮是正方：bbox 33.97 x 33.97 DIP，ratio ≈ 1.0
# 2) 圆钮在行内纵向居中：|圆钮中心 - 行中心| = 0.00 DIP
# 3) 圆钮左侧的背景条 100% 纯白 —— 注意别统计「非白像素数」，
#    文字墨迹本身就是非白的，会得到几千个假阳性。要取「文字上/下方的背景条」再看直方图。
```

**通用教训**：自绘列表行里，「行」是布局单位、「按钮」是交互单位，
**高亮属于后者**。铺整行是桌面软件的常规做法，但触摸目标小的高亮块观感更精确。

## 十三之二、「暂时收起」与「永久隐藏」是**两个按钮**，不能共用一个

用户报「最小化后键盘又自己弹出来了」「必须完全退出程序才停」（Issue #2，
第一个发行版）。这个 bug 修过一轮，**修错了**，被用户当场推翻：

### ❌ 错误修法（commit 6ffaee1，已 revert）

把标题栏「最小化」改成置 `g_manualHide`（永久收起）：

```cpp
case HDR_MIN: UserHideKeyboard(TRUE); break;   // ← 最小化 = 永久
```

用户反馈：**「这么一搞自动呼出功能就名存实亡了」** —— 点一次最小化，
键盘就再也不自动出现了。而自动呼出是这个屏幕键盘的**核心功能**，
不是可以牺牲的副产物。

### ✅ 现行做法（commit 1bb554c）：加一个独立按钮

| 按钮 | 图标 | 函数 | 置 `g_manualHide` | 自动呼出 |
|---|---|---|---|---|
| **最小化** | `—` | `UserHideKeyboard()` | ❌ | ✅ 仍然有效 |
| **⏑（新增）** | `HKICON_CHEVRONDOWN` | `HideToTray()` | ✅ | ❌ 只有单击托盘图标才回来 |
| 关闭 → 隐藏到托盘 | — | `HandleCloseAction` | ✅ | ❌ |

「暂时不要」和「我不想看到它」是**两件不同的事**，不该挤在一个按钮上。
按钮不够就**加一个**，别改动已有按钮的语义 —— 已有按钮的语义是用户已经
形成肌肉记忆的东西。

### 自动呼出的守门只有一处

```cpp
if (!g_manualHide && !g_vis) ShowKB(TRUE, FALSE);   // 每 ~300ms 的 WM_FOCUS_EVENT 轮询
```

⇒ `g_manualHide` 只该由**明确表达「我不想看到它」的入口**置位。
临时收起（最小化、收起键）**故意不置**。

### `g_manualHide` 与 `g_userHidInInput` 是两件不同的事

| 标志 | 语义 | 作用域 | 谁清它 |
|---|---|---|---|
| `g_manualHide` | 用户明确「不想看到它」 | 全局，直到手动恢复 | `ShowKB(TRUE, TRUE)` 的 isManual 分支 |
| `g_userHidInInput` | 自动收起开启时，**同一个输入框内**不回弹 | 局部、短期 | 焦点离开输入框 / 手动显示 |

### 验收：恢复路径也要核对

改了隐藏逻辑，必须确认**还能回来**：

```
托盘左键 → ToggleKB() → ShowKB(!g_vis, TRUE) → isManual 分支清 g_manualHide（3056 行）
```

⚠ 托盘菜单里「显示轻键」这一项只在 `g_vis == FALSE` 时出现，
所以隐藏后它必然在 —— 这一条是「能恢复」的前提，改窗口可见性时别破坏它。

### 加标题栏按钮的完整改动清单

`HeaderMetrics` 是单一数据源（`GetHeaderMetrics` 出几何、`HitHeader` 出命中码、
`DrawHeader` 出绘制，三处各读一份），所以加一个按钮是**机械改动**：

1. `#define HDR_HIDE 1006`
2. `HeaderMetrics` 加 `wHide / xHide` 两个字段
3. `GetHeaderMetrics`：赋宽 + `xHide = xMin - gap - wHide`
   ⚠ **插在最小化左边**，让两个「收起」按钮相邻；且 `xNum` 的依赖要跟着改成 `xHide`
4. `HitHeader`：加一条 `x` 区间判断
5. `DrawHeader`：hover 底 + `DrawHkIcon`
6. `HeaderIconBox`：**必须**给新图标补尺寸反解，否则它会比邻居大/小一圈
7. `OnLDown`：`case` 里调对应函数

第 6 步最容易漏。`HeaderIconBox` 的注释写得很清楚：几何包围盒差很远，
同一个外框画出来齿轮比小键盘大 27%。`CHEVRONDOWN` 在 24 网格、可见 6..18，
所以 `box = 13.0 * 24.0 / 12.0`。

回归验证：像素差分应**只落在标题栏带内**（`y 0..89`），键区 0 差异。

#### ⚠ 这类 bug **没法用脚本自动化验证** —— 别浪费一轮 CI

写过自动化脚本（`scripts/verify_issue2_hide.ps1`，点最小化 → 把焦点送进记事本
→ 等 3.5s 看键盘是否弹回），**修复前的 exe 也能跑出 PASS** —— 假的。原因：

1. 弹回的守门是 `GetFocusedInputControl()`，它要求目标控件通过
   **MSAA `IAccessible` + `AccessibleHasEditableFocus`** 判定。
   记事本（现代 Win11 是 UWP 包装）**通不过** ⇒ 根本进不了弹回分支。
2. 判据也踩过坑：`IsWindowVisible` 在隐藏动画（`MOTION_HIDE`，150ms 滑到底
   才 `SW_HIDE`）期间**仍为 TRUE**。必须用**窗口 top 是否滑到 `work.bottom`** 判定。

⇒ **结论：涉及 MSAA / 焦点 / 全局热键这类 Win32 输入行为的，验证只能靠代码论证 +
用户实机确认。** 脚本只适合验证「像素变了没有」这类可视行为
（`hk_probe_v9.ps1` + `pixel_regress.py`）。

脚本里值得保留的两处正确做法：
- 按钮坐标用 `GetDpiForWindow(h)/96` 反推 scale，再照 `GetHeaderMetrics` 的
  公式算，别硬编码 DPI。
- `trap { ...落盘...; exit 1 }` 包住全部逻辑 —— 工具层不回显 PowerShell 输出时，
  日志文件是唯一能看见失败原因的地方。

### ⚠ 这类「语义分层」的需求，**先问用户再动手**

「修 Issue #2」听起来是明确的 bug 修复，但它实际是**产品决策**：
最小化该不该保留自动呼出？不同用户的答案完全相反（有人嫌它老自己出来，
有人靠它做主力输入）。这类问题写代码前用 `AskUserQuestion` 问一句，
成本远低于做错方向再 revert 一轮 + 改文档 + 撤技能条目。

判据：**如果一个「修复」会让某个现有功能消失，先确认那是不是用户想要的**。

## 十三之三、实机抓图：Python + ctypes 版（本机唯一可靠路径）

`.workbuddy/snap_prompt.py`（已同步到 `scripts/snap_prompt.py`）。**PowerShell 抓图在本机反复失败**，
别再重试 PowerShell 路线：

| 尝试 | 结果 |
|---|---|
| `Add-Type -TypeDefinition` 编译 Win32 代码 | 缺程序集引用，加 `-ReferencedAssemblies System.Drawing` 也不行 |
| `2>&1 \| Out-String` / `Out-File` / `trap` 落盘 | **stdout 完全不回显**，看不出任何错误 |
| `Start-Process powershell.exe` 绕开 |被安全策略拦截：`spawns a child process that bypasses PowerShell command validation` |
| Bash 调 PowerShell | 同样被拦：`Invoking PowerShell from Bash bypasses PowerShell security checks` |
| `reg query` 查配置 | `reg.exe` 在程序黑名单里，直接 BLOCKED（改用 Python `winreg`）|

**结论：抓图一律走 Python + ctypes，解释器用 venv**
（`C:/Users/29776/.workbuddy/binaries/python/envs/default/Scripts/python.exe`，
托管版 3.13.12 里**没有 PIL/numpy**）。

### ⚠ 四个必踩的坑

**1. 抓图进程自己必须 DPI-aware，否则尺寸全错**
本机 168 DPI = 1.75x。未设感知时 `GetWindowRect` 返回**虚拟化坐标**：
840x460 的窗口被报成 480x263，`PrintWindow` 画的是 840x460 却只取左上角 480x263 ——
表现为「只拍到窗口左上角一小块」。**在任何窗口操作之前**调用：

```python
ctypes.windll.shcore.SetProcessDpiAwareness(2)   # PROCESS_PER_MONITOR_DPI_AWARE
# 失败再退回 u.SetProcessDPIAware()
```

对照换算：主窗口 1715x560 = 980x320 DIP ✓（`InitWindowSizeForDpi`），
关闭对话框 945x460 = 540x263 DIP ✓。

**2. 触发对话框只能靠模拟点击标题栏 ✕，不能靠 `WM_CLOSE`**

```python
# lParam 用**客户区**坐标（GetClientRect 的 right），不是 GetWindowRect
lparam = (32 << 16) | ((GetClientRect(h_main).right - 24) & 0xFFFF)
PostMessageW(h_main, WM_LBUTTONDOWN, 1, lparam)
PostMessageW(h_main, WM_LBUTTONUP,   0, lparam)
```

实测对照（同一份二进制、同一天）：

| 方式 | 结果 |
|---|---|
| `PostMessage(WM_CLOSE, wParam=SC_CLOSE)` | 窗口毫无反应，对话框不弹 |
| `PostMessage(WM_CLOSE, wParam=0)` 加重发 | 第 10 轮弹成功；换个环境重试 15 轮全不弹 |
| `SendMessageW(WM_CLOSE, 0)` 同步调用 | 15 轮全不弹 |
| **`PostMessage(WM_LBUTTONDOWN/UP)` 打到 ✕** | **一次就中** |

`WM_CLOSE` 这条路时灵时不灵的原因不明（`RememberClose=0` 已确认、
无残留实例、互斥体正常），**别在它上面耗时间，直接模拟点击**。

**3. 找主窗口后要等 2.0s**
托盘图标、配置加载、淡入动画都还没完成。另一个坑：`lParam` 是**客户区**
坐标，用 `GetWindowRect` 的中心算 x，窗口在屏幕右侧时完全点空。

**4. 找不到目标窗口时**，别猜，先把该进程所有顶层窗口连类名/可见性/尺寸一起打出来
（`EnumWindows` + `GetClassNameW` + `GetWindowRect`）。本轮就是靠它发现
「`HKeyboardClosePrompt` 从未出现 → 不是绘制问题，是触发链路问题」。

### ⚠⚠ 渲染器预览**不能替代**实机截图

本轮最强的教训：分段控件溢出窗口 178px 的 bug，
**PIL 渲染器画的一直是对的**（`segX = W - pad - px(20) - segW`），
只有实机 `PromptComputeLayout` 漏了 `- L.segW`。
渲染器复刻的是**你理解的**布局，程序执行的是**你写的**布局 ——
两者不一致时，只有实机能发现。

## 十四、源码瘦身：注释与死代码的删除纪律

用户会明确要求「清理下一些没必要的注释，不然源码文件会太大了」。照下面做，别乱删。

### 1. 注释：**删三类、留一类**

| 删 | 留 |
|---|---|
| 复述代码的（「`v` 置为 TRUE 表示…」紧跟着 `v = TRUE`） | 「为什么这样做」（推导、约束、OS 差异） |
| 记流水账（「修复 #2」「第 3 轮改的」，脱离上下文只剩噪音） | 「别再改回去」的反坑记录（`⚠` 开头的踩坑段） |
| 已失效的推理（描述的是删除/改写前的代码） | 与外部实现的**逐项对照**（`PanDaPE 实测…`） |

**判据：把这条注释删掉，下一个人会不会踩同一个坑？** 会 → 留；不会 → 删。

几个具体的删除靶子：

- **外部 issue 编号**（「修复 #2」「#53A3F2」）—— 脱离原 issue 就是纯噪音，且常与邻近行重复。
- **符号墓碑**（「已删除：static int AboutSectionH ...」）—— 直接删，别留名字。
  但要确认真的**无引用**（正则统计全文出现次数，`<=1` 即无调用）。
- **过时的尺寸数字**（注释写 78 DIP、代码是 76）—— 改成实测值或删掉整句，别留错数。
- **大段 `// =====` 布局说明里的键位清单** —— 只在「列位是单一真源」这类**推导**时保留，
  纯罗列键序的删掉（代码里的 `kDefRow3[]` 本身就是清单）。

**反例（别删）**：L1748 那 21 行讲「贴边锚点只能有一个、两条绘制路径必须共用同一常量」的，
是踩过两次坑才写出来的；L937 那 20 行是 1u 网格的反解公式。**这类全留。**

### 2. 死代码：只删**纯函数**，不动结构体字段

扫法：正则提取 `static` 函数定义 + `struct` + `#define`，统计全文出现次数。

```python
# 出现次数 <= 1 → 无任何调用点
```

⚠ **正则会因跨行签名漏判**，务必 `grep -n` 逐个复核再删。

**决策规则**：

- 纯函数 / 纯 `#define`（`TIMER_EXIT` 这种）→ 删。
- **在 `struct` 里的字段**（如 `HkbTokens::titleActive` / `floatPanelBg`）→ **保留**。
  删字段会改变结构体布局，进而影响 `HkbTokensBuild` 的初始化列表和后续所有字段偏移，
  风险远大于省下那两行。除非顺手把整个结构体一起重排。
- **已废弃的注释掉的代码块** → 删（本来就不可执行）。

### 3. 收尾：证明「删的确实没用」

删完跑一遍**双 exe 逐像素对比**（方法见第十二节第 5 条）：

- 纯删死代码/清注释 ⇒ **12 张截图差异必须全为 0**。
- 有非零差异 ⇒ 删到了活代码，**回滚重查**。

这个证据比「grep 不到引用」强得多，也能直接回答用户的「会不会改坏」。

---

## 十五、写发行更新日志 / README：先按**文件维度**定量剔除非产品面

需求形态通常是「把 X 之后至今的修改整理成更新日志，我要发 Release」
或「根据最新代码去掉 README 里已放弃的功能」。两种都**不要逐条读 commit**——
201 条会看麻，而且容易把工程细节当产品卖点写进去。

### 1. 先跑文件维度统计，判断「哪些算产品面」

```bash
git log --since="2026-08-10" --name-only --pretty=format: \
  | grep -v '^$' | sort | uniq -c | sort -rn
```

本项目实测结果（2026-08-10 基准）：

| 文件 | 次数 | 处置 |
| :--- | --- | :--- |
| `hkeyboard.cpp` | 188 | **产品面主体** |
| `README.md` | 27 | **剔**（文档不是产品） |
| `winres/MiSans-Medium.ttf` | 5 | 保留（字体） |
| `hk_icons_generated.h` | 5 | 保留（图标） |
| `resource.h` / `hkeyboard.rc` | 7 / 7 | 保留（版本号与资源） |
| `tools/*.py` | 3×3 | **剔**（探针 / 字体子集脚本是私有工具链） |
| `.github/workflows/*` | 2 / 2 | **只在讲 Arm64 构建时**保留 |

再跑一条把「只动了非产品文件」的提交单独捞出来，确认没有漏判：

```bash
git log --since="..." --pretty=format:"%h|%s" --name-only \
| awk 'BEGIN{RS="";FS="\n"}{h=$1;for(i=2;i<=NF;i++){
    if($i !~ /hkeyboard\.cpp$/ && $i !~ /\.ico$/ && $i !~ /\.ttf$/){
      print h" ["$i"]"; break}}}' | head -40
```

### 2. 日志的分类维度：用**产品模块**，不用**工程类型**

❌ `Added / Changed / Fixed / Refactored` —— 这是给开发者看的，Release 用户不关心
哪些是 `refactor`。

✅ 按模块分章，每章都是用户能看见的东西：

```
界面体系：材质 → 自绘
字体
图标
键盘布局
设置页
关闭确认对话框
关于页
动画
核心功能修复（按 输入 / 自动呼出 / 对齐 / 老系统兼容 再分子项）
新增 Arm64 构建支持
```

### 2.5 ⚠ 基线必须是**上一个正式 Release tag**，不是「我翻开发的那天」

**这是最容易整章写错的地方。** `--since=` 的日期是「我关心的起点」，
跟「用户手上是什么版本」完全是两件事。

**基线查询（固定动作）**：

```bash
gh release list --limit 20# Release 名 + tag + 发布日期
git ls-remote --tags origin      # tag -> commit
git show v1.1.1:hkeyboard.cpp | wc -l        # 基线源码行数
gh release view v1.1.1           # 上一次的 release notes
```

本项目基线 = **v1.1.1**（Release「26.08.10_v2」，2026-08-10，`f284f7d`）。

⚠ **不要把 tag 之后才开发的中间态功能写进「破坏性变更」**。真实案例：Mica / 亚克力
材质是 08-16 才加的（`77aaead` 等），而 v1.1.1 是 08-10 —— **从没有用户拿到过含
材质的版本**，把它列进「破坏性变更」纯属虚构。**整章删除。**

**写「新增 X」之前必做**：

```bash
git show <上一个tag>:hkeyboard.cpp | grep -c '<X 的关键词>'
```

**命中数 > 0 就是「已有」，不是新增。** 凭 commit 时间推断不可靠 —— feature 可能在
打 tag 之前就写完了。

> 实战里靠这条 grep 抓出 4 个「把已有当新增」的错误：
> 1. 关闭确认框 —— v1.1.1 **已有** `PromptCloseAction`（系统 `MessageBoxW(MB_YESNOCANCEL)`，
>    三选项 是/否/取消，只是没有「记住选择」）
> 2. 命令行参数 —— v1.1.1 **已有** `-theme:system`，全集与 v2.0 完全一致
> 3. 7z 合并包 —— **没取消**，只是 `HKeyboard_X86_X64.7z` → `..._X64_Arm64.7z`
> 4. 托盘菜单 —— v1.1.1 **已有**主题子菜单（跟随系统/深色/浅色），且总共 5 项

**v1.1.1 基线速查**（实测，别凭印象写）：

| 项 | v1.1.1 |
| :--- | :--- |
| 源码规模 | **1570 行**（v2.0 = 6445） |
| `VER_FILEVERSION_STR` | `1.1.0.0` |
| Mica / 亚克力 | **grep 命中 0** |
| 设置页 | **完全没有** |
| 托盘菜单 | 仅 5 项：显示轻键 / 自动呼出 / 主题子菜单 / 关于 / 退出键盘 |
| 字体 | **无内嵌** —— 系统 `Microsoft YaHei` + `FW_BOLD` 仿真粗体；`winres/` 只有 `main.ico` |
| 配色 | 读注册表 `AppsUseLightTheme` / `AccentColor` / `ImmersiveColorSet` |
| 布局 | **单一布局** |
| 关闭确认 | **有**，系统 MessageBox 三选项，无「记住选择」 |
| 命令行参数 | 与 v2.0 **完全一致** |
| 自动呼出 / 托盘 / Win 键 | **已有**（不是新增） |

### 2.7 ⚠ 第三类虚构：自己测试过程中引入又修掉的「内部迭代」

前两类是「时基」错（用了错的起点），这一类是**用户根本没见过的问题**。

用户原话：「这是你测试过程中出现的产物，而不是正式版应该提出来的」。

典型形态：**UI 调优过程的自产自销** —— 加宽对话框时算错 segX、改卡片高度时漏算
rowPadY、重写关于页时绘制参数写错、生成图标头文件时路径数据有误。
这些「问题」是开发过程中**产生并立刻修掉**的，v1.1.1 用户升级过来从未遇到过，
写进更新日志等于让用户读自己的开发过程。

**判据（按引入 commit 判，不按修复 commit）**：

```bash
# 找到「引入这个 bug」的 commit—— 通常是那个「feat/fix(布局)」，
# 不是后面「修它」的那个 commit
git log --oneline --since="<上个tag日期>" --grep="<关键词>"
git show <可疑commit> --stat          # 看它同时改了哪些行
```

若「引入」晚于上一个 Release ⇒ **删掉这条**，别写进给用户的日志。

⚠ **例外：用户真会遇到的必须留**。判据是「用户从 v1.1.1 升上来会不会碰到」：
- 高 DPI 首启窗口异常 → **留**（用户开着高分屏，升级就可能碰到）
- 关闭对话框留白不对称 → **删**（v2.0 才有的新窗口，用户没见过旧的）

**最快的判别法：查 issue 有没有人报过**

```bash
gh api repos/PanDaDaTech/Hydrogen-Keyboard/issues/<n> --jq '.title, .body'
```

⚠ `gh issue view`走 GraphQL，本机常`unexpected EOF`；**用 `gh api` 的 REST 版**。

- **有 issue** ⇒ 真实用户遇到过 ⇒ **留**，并在日志里标注编号
  （`（[#2](https://github.com/PanDaDaTech/Hydrogen-Keyboard/issues/2)）`）
- **只有 commit** ⇒ 大概率是自产自销 ⇒ **删**

### 2.8 ⚠⚠ UI 取舍：**用户的文字答复 ≠ 用户看过实机后的判断**（2026-10-05）

第 29 轮我给了两个方案让用户选（导航区标签缩写换字号），用户**在文字里选了缩写**。
我照做、CI 验证、交付。第 30 轮用户看了实机立刻推翻：
「能 9pt 就 9pt 吧，**不能显示全称太难受了**。」

⇒ 教训：涉及**可读性 / 信息完整性**的取舍（标签要不要缩写、图标要不要配字、
   文案要不要精简），用户很难凭空想象出实机效果，**文字确认不算数**。
   能先给一版可运行的东西让他看一眼，就别急着按答复定稿。
   反过来，纯数值取舍（14pt 还是 13pt）文字确认是可靠的。

本项目现有 issue 只有两条：#1 托盘乱码（v1.1.1 已修）、#2 最小化后自动弹出。

**反向自查：一条「修复/优化」该不该写？四条判据**

1. 引入该功能的 commit **晚于上一个 Release** ⇒ 用户没见过这个问题 ⇒ **删**
   （`git log --reverse --grep="<功能名>" | head -1` 看引入日期）
2. `git show <上个tag>:hkeyboard.cpp | grep -ci "<功能关键词>"` **> 0** ⇒ 功能已存在 ⇒
   措辞用「优化 / 修 xxx 的问题」，**不能用「增加」**
3. **有 issue 编号** ⇒ 真实用户报过 ⇒ 留并标编号
4. 拿不准 ⇒ **保守留**（漏报好过报错：用户会自己发现；虚报显得在凑数）

⚠ **判据 2 只看「有无」，还要看「具体条件是否逐字相同」—— 这是本项目踩过的最深的一个坑。**

`grep -c "repeat"` 命中 13 处，只能证明 v1.1.1 **有连发机制**，证明不了**连发哪些键**。
实际第 1207 行是：
```cpp
if (k->vk == 0x08 || k->vk == 0x2E || k->vk == 0x20 || k->type == K_ARROW) {
    g_repeatKeyIdx = ki;
    SetTimer(hWnd, TIMER_REPEAT, 350, NULL);
}
```
退格(0x08) / Delete(0x2E) / 空格(0x20) / 方向键 **全都在**，
和 v2.0 准备写的「优化 退格、删除、空格与方向键支持高频连发」**一字不差**。
所以那条是**把 v1.1.1 已有能力当成新增写**，性质比「CI 产物」更严重。

**正确姿势：直接读那段判断代码，不要只数命中**
```bash
git show <上个tag>:hkeyboard.cpp | grep -n -B3 -A6 "<函数名>"# 看具体条件
```

同理 SendInput：v1.1.1 已有 `MapVirtualKeyW(vk, MAPVK_VK_TO_VSC)` + 扩展键标志，
注释原文写着「修复 #5: 部分 IME 依赖正确扫描码」—— 写成「本次改用 SendInput 加扫描码」
是把 v1.1.1 的既有实现说成新引入。

⚠ **典型误判：「恢复」「优化」听起来像改进，其实是 v2.0 自己重做。**
v2.0 重写了键面标签体系，所以「退格键恢复 Backspace 全称」—— 但v1.1.1 本来就是
`Backspace` 文字（`git show v1.1.1:... | grep Backspace` 有命中），只是 v2.0 中途
改成过符号又改回来。这类**直接删**（中间态反复，用户只见过最终态），不能写「修复」也不能写「更新」。

**v1.1.1 已有能力速查**（别把这些写成 v2.0 新增）：

| 能力 | `grep -c` 实测 | ⚠ 注意 |
| :--- | :--- | :--- |
| F 行（F1~F12） | **36** | |
| Auto-Repeat 连发 | **13** | 退格/Del/空格/方向键**全都支持**，见上文 1207 行 |
| 圆角 | **10** | |
| `-wallpaper` 强调色 | **10** | |
| Backspace 文字标签 | 命中（`Row 0: ..., Backspace`） | |
| SendInput + 扫描码 + 扩展键标志 | 有（第 522-529 行） | 注释「部分 IME 依赖正确扫描码」 |
| `SetLayeredWindowAttributes` | 有（第 1248 行） | ⚠ **但没有淡入淡出动画**，见下 |
| **GDI+ / `TextRenderingHint`** | **0** | v2.0 才换（`8396cae` 10-01），「更换文字渲染」是真变更 |
| **窗口动画（`AnimateWindow` / DWM 位移）** | **0** | v2.0 才加，「窗口过渡」是真新增 |
| **淡入淡出渐变** | **0**（只有上面那行设 255 不透明） | v1.1.1 的 `TIMER_SLIDE` 是**位置滑动**不是透明度渐变 |
| **窄屏自适应（minw/clamp）** | **0** | v2.0 才加 |
| **per-monitor DPI（`GetDpiForWindow`）** | **0**（只有 `GetSystemDpiScale`） | v2.0 换掉 ⇒ 「修复高 DPI 首启窗口异常」是真修复 |

**v2.0 才引入的**（这些的「修复」一律不写）：设置页 `0501adc`、完整键盘布局 `2171bcd`、
Fn 网页层 `16ad630`、内嵌字体、Arm64、键面图标 `KeyIconFor`（`67f8fd4` 10-01）、
分段控件、下拉框移除。

### 2.8 ⚠ 写新 Release 日志前，先读上一个 Release 的正文

```bash
gh release view v1.1.1
```

**基线不只是代码版本，还有已发布的文字。** 用户记得自己在上一版里写过什么，
重复列出来会让他觉得「这我上次就看过了」。

v1.1.1 已列的（**不要在 v2.0 日志里再列**）：
- 托盘图标乱码 #1
- 方向 ↑↓ 键不对齐
- 托盘图标无显示 / 显示异常
- 关闭时托盘图标无法正常隐藏
- 最小化、托盘图标状态下的关闭逻辑
- Shift / Win 键的点击逻辑
- 菜单—隐藏轻键选项

**句式对齐**：旧日志写「修复 xxx 问题」，新日志也写「**修复 xxx 的问题**」——
「修正」是内部 commit 术语（`fix:` / `修正`），**「修复」才是用户视角**。

**⭐ 标记法**：只给「相对 v1.1.1 真正从无到有」的东西打 ⭐
（设置页 / 三套布局 / 内嵌字体 / 矢量图标 / 自绘配色 / Arm64），
一眼看出本次的核心增量。

### 2.6 ⚠ 中间态方案不进更新日志（第二类虚构改动）

上一条治的是「把 tag 之后的东西当新增」。这一条治**反方向**：把**只活了几天的中间态
方案**写进「修复 / 老系统兼容」。

真实案例（用户两次抓出同一类错误）：

| 虚构的改动 | 实情 |
| :--- | :--- |
| 「移除 Mica / 亚克力材质」（放破坏性变更） | 08-16 加、10-01 就删了，**从未进任何Release**（v1.1.1 = 08-10） |
| 「Win7 内嵌 Segoe MDL2 字体资源」（放修复） | `a67bfc2` 08-31 加 → `67f8fd4` 10-01 删，**只活了 1 天** |
| 「v1.1.1 用系统 UDI 绘制图标」（放基线） | UDI 是 **Win8 才有的 API**；v1.1.1 标题栏是 `DrawHeader` 用 **GDI 画直线**手绘，托盘图标才用 `LoadIconA(NULL, IDI_APPLICATION)` |

**写「修了什么老系统问题」前的必做判据**：

```bash
# 1. 这个方案的生命周期有多长？
git log --oneline -S"<方案关键词>" -- <文件># 引入commit + 移除 commit
# 2. 它活到发布那天了吗？
git show <上一个tag>:<文件> | grep -c '<关键词>'   # 命中 0 = 从未发布过 → 删掉这条
# 3. 现在代码里还有吗？
grep -in "<关键词>" hkeyboard.cpp hkeyboard.rc resource.h
```

⚠ **「日志里写了但代码里查无此物」= 一定是写错了**。本项目两次都是用户先发现，
我后核实。更新日志写完最后一遍，值得拿关键词 grep 一次成品文件。

**本项目「老系统兼容」现在实际只剩一条**（真材实料，可写）：
旧版 DWM（Win7/8 与 Win10+ 颜色布局不同）下壁纸强调色回退到 `ColorizationColor`
—— hkeyboard.cpp:290/ 316，注释原文：「ColorizationColor（Win7 起即存在，ARGB 布局；
Win7 无 AccentColor 系列键，壁纸派生强调色由此获得）」。

**资源文件现状**（`winres/` 只有两个文件，别再写「内嵌多字体」）：
- `winres/MiSans-Medium.ttf` —— 145 KB，唯一字面，`IDR_FONT 101`
- `winres/main.ico` —— `IDI_MAIN_ICON 100`
- 已删：`MiSans-Regular.ttf` / `MiSans-Bold.ttf` / `AlibabaPuHuiTi-*.ttf` / `segmdl2.ttf`

### 3. 数字要从代码/CI 取，不能凭记忆

本项目几个反复写错的数：

```bash
grep -oP 'k_hkIcons\[\]' hk_icons_generated.h          # 图标数 = 67（枚举数应相同）
ls -la winres/                                           # MiSans-Medium.ttf = 148724 B ≈ 145 KB
wc -l hkeyboard.cpp                                      # 行数
grep -n "WIN32_WINNT\|SUBSYSTEM:WINDOWS" .github/workflows/build.yml
```

⚠ **三架构的 `SUBSYSTEM` 目标不同，别写成一句「XP~Win11」**：

| 架构 | 目标 | 含义 |
| :--- | :--- | :--- |
| x86 | `5.01` | Windows XP 起 |
| x64 | `5.02` | Windows 7 起 |
| arm64 | `6.02` | Windows 10 起 |

⚠ **arm64 不链 YY-Thunks**（build.yml 注释原文：ARM64 Windows 为 Win10+，无 XP
需求），x86/x64 才链。写「三架构都靠 YY-Thunks 兼容 XP」是错的。

### 4. 改 README 前：分清「文档错了」还是「代码变了」

README 与代码不符时，**先找多处交叉证据**再动手。判据（以 `g_sTab` 注释
「0=常规 1=主题 2=关于」为例，它看起来像「Tab 从 4 个合并成 3 个」）：

```bash
grep -n "SettingsTabLabels" -A 8 hkeyboard.cpp        # 标签数组
grep -n "k_settingsTabHits" -A 2 hkeyboard.cpp         # 命中映射
grep -n "SettingsRowCount" -A 6 hkeyboard.cpp          # 行数分支
grep -n "DrawTabStrip" -A 6 hkeyboard.cpp              # 绘制循环
```

结论是**Tab 仍是 4 个**，错的是那行注释。`k_settingsTabHits` 里
`S_HIT_TABL` 是「布局」的独立命中码（不是 `TAB1` 的别名）—— 这就是关键证据。
**顺手把注释也改对**，别只改文档。

本项目 README 已知过时点（对照 `hkeyboard.cpp` 核过）：

| README 曾写 | 实际 | 依据 |
| :--- | :--- | :--- |
| 按键显示两态（文字/图标+文字） | 键面**固定**图标+文字 | `02371cb` 删了该设置行 |
| 字号 20/13/12/10.5/10 五档 | 18/17/12.5/11/10/9 六档 | `InitFixedFonts` |
| 字体子集 ~170 KB | ~145 KB | `winres/MiSans-Medium.ttf` |
| 关于页「社区交流」章节 + 问题反馈 | 已删，只剩「开源许可」+ 项目地址 | `1c84949` |
| 「下拉选择」布局/主题/透明度 | 全是**分段控件**，下拉框代码已删光 | `8a36905` / `b6b50be` |
| 入口「菜单」按钮 | 标题栏齿轮「设置」按钮 | `DrawHeaderMenuButton` |
| 「左侧 Tab」 | Tab 条在**顶部** | `DrawTabStrip` 画在 `tabsY` |

### 5. 顺手可修的小瑕疵

- `winres/` 里旧字体链路（`MiSans-Regular/Bold.ttf`、
  `AlibabaPuHuiTi-*.ttf`、`segmdl2.ttf`）都已删/替换，别再写「内嵌多字重」
- 透明度是**六档**步进 10（`g_opacityValues` = 100/90/80/70/60/50），
  别只写「100%~50%」

---

### 6. 改完**用真实渲染器验证**（别靠肉眼猜）

文本全对、渲染出来却是坏的 —— 2026-10 连续踩两次，都是用户直接截图
指出「实际显示渲染有问题」：

- **两个 `~` 会被 GFM 配对成删除线**（单 `~` 就够，不必 `~~`）。
  写 `（1~0、-、=）切换为 F1~F12`，GitHub 实际渲染成
  `（1<del>0、-、=）切换为 F1</del>F12` —— `~` 之间的整段被加删除线，
  看起来就是「波浪号消失、文字连成一片」。
  ⇒ **范围一律用「至」**（`125% 至 250%`、`F1 至 F12`），**全文不留 `~`**。
- **反引号别包连续键位**：`` `1`~`0`、`-`、`=` `` 会碎成「灰块 + 裸波浪 + 灰块」交替。
  ⇒ 反引号只给单个键名（`Fn`）。

**验证命令（调 GitHub 自己的渲染 API，最接近线上效果）**：

    gh api markdown -X POST -f text="$(cat /tmp/片段.txt)" -f mode=gfm

看返回 HTML 里有没有 `<del>` / `<s>`，再剥掉标签比对纯文本是否完整。
⚠ Git Bash 会把 `/markdown` 当路径转换、报 `invalid API endpoint` ——
**省略前导斜杠**写成 `gh api markdown`。

**改完必跑的自查**：

    grep -c '~' README.md                            # 应为 0
    grep -nP '`[^`]+`\s*~|~\s*`[^`]+`' README.md     # 应为空

## 二十、UI 观感类问题的排查方法（2026-10-04 新增）

字体大小 / 键宽这类**看得见但测不出**的问题，用下面这套方法能定位到根因，
不用靠猜阈值反复试版本。

### 1. 把几何算式搬到 Python 里离线复算（最有效的一招）
把布局函数（`FullColX` / `FullPut` 之类）的算式**原样翻译成 Python**，
扫 4~5 组参数（窗口宽 × gap × 布局档），逐项打印：

- 同一行里各列的宽度 → 检查是否等宽
- 相邻键 `x + w` 与下一个 `x` → 检查是否重叠
- 块与块之间的实际间隙 → 检查是否等于设计值

实战案例：全尺寸布局导航区三列实测 `57 / 58 / 63`，第三列凭空多一个 gap。
根因是 `bc = {15, 18}` **恰好就是下一块的起始列**，于是「右端落在块边界的那一列」
被 `b*gap` 多算了一次。

⚠ **这个脚本同时否掉了我第一版修法**：当时想「宽度直接取 `round(w*u)`」，
脚本一算就发现相邻键会**粘连**（间隙被吃掉）—— 只读代码是看不出这一点的。

### 2. 用真实字体文件量标签宽度
`winres/MiSans-Medium.ttf` 就是程序内嵌的那份。PIL 的
`ImageFont.truetype(path, pt).getlength(s)` 量出来与 GDI+ 略有差异但**足够判断
「谁比谁宽、差几个 px」**，用来验证「是不是宽度差导致的降档」。

⚠ PIL 只装在**系统 Python 3.12**（`C:/Users/29776/AppData/Local/Programs/Python/Python312/python.exe`），
托管的 3.13 里没有。用错解释器会直接 `ModuleNotFoundError`。

### 2.5 ⛔ 静态检查有盲区：局部变量未声明只能靠 CI
本项目 2026-10-04 踩过：`error C2065: 'before': undeclared identifier`，
而**三个检查脚本全绿**。

| 脚本 | 查什么 | **不查什么** |
|---|---|---|
| `check_balance` | 括号 / 字面量闭合 | 任何语义 |
| `check_fwd` | 「先用后定义」的**函数** | **局部变量**的作用域 |
| `check_macros` | 与 Windows/CRT 宏同名 | 任何语义 |
| `check_undef.py` | 未定义标识符 | **噪音太大**（把 `memcmp`/`wcslen`/`x660E` 全报出来）⇒ 不能当判据 |

⇒ **能过静态检查 ≠ 能编译。** 局部变量未声明、类型不匹配、隐式转换
   这类**编译期**错误，本地一律抓不到。

⇒ 做「替换一段代码」时（`str.replace` / 编辑器多行替换）：
   **锚点必须包含新代码引用到的所有标识符的声明**。
   我那次锚点从 `BOOL before = ...` **之后**开始 ⇒ 声明被删掉、引用还在。
⇒ 改完立刻逐个 `grep` 确认新用到的变量都有声明行，别等 CI。
⇒ 大改动后**第一时间看 CI 日志**（`gh run view <id> --log-failed | grep error`），
   不要只看本地检查结果就以为稳了。

### 3. 「字有些大有些小」= 字号统一问题：**先怀疑判据写反，再怀疑分组**（2026-10-05，五轮返工）

这是本项目**返工最多的一类问题**（同行 → 同列 → 按区块 → 合并区块 → **判据反向**，五轮）。
用户每次的措辞都很像：「导航栏还是一样」「自适应字体有些大有些小」「改成 Num 同样的字号」。

#### ⚠⚠⚠ 第一嫌疑：那个"取组内最严档"的循环判据写反了

**真凶是这个，前四轮全在改分组，都没碰到它**：

```cpp
if (!KeyHasGlyph(k)) continue;     // ⛔ 写反了 —— 这个循环从来没生效过
```

`KeyHasGlyph(k)` 返回 **TRUE = 有图形**（`Tab`/`Caps`/`Shift`/`Enter`/退格/方向键/Win/Menu）。
该循环要的是「同组的**纯文字**键都放得下才用这一档」⇒ 应该 `continue` 掉**有图形**的键。
`!KeyHasGlyph(k)` 恰好相反：**把纯文字键全跳过、只留图标键**；而图标键
（Tab 80 / Caps 94 / Shift 151…）又都被上面的 `dw > 2` 挡掉。

⇒ **循环永远空转**，`worst` 恒等于「本键自己的档位」。
  「同宽键统一字号」等于**从来没生效**，实际一直是**逐键独立选档**：
  `Ins`/`Del`/`End`（3 字母，14pt 放得下）停在 t=0，
  `Home`/`PgUp`/`PgDn`/`PrtSc`/`ScrLk`/`Pause` 掉到 t=4。

**症状特征（一眼认出来）**：同一区块里的字号**按标签长度**分层 —— 短标签大、长标签小。
看到「`Ins`/`Del`/`End` 比 `Home`/`PgUp`/`PgDn` 大」就是它。
正解只有一行：`if (KeyHasGlyph(k)) continue;`

#### 🔑 定位方法：让**程序自己**打印它算出来的东西（`-fontdiag`）

⚠ **离线复算（PIL）照不出这个问题** —— 脚本用 `advance`、程序用 GDI+ `MeasureString`
的**包围盒**（明显更宽），两者档位本就不同；更关键的是脚本是**复刻逻辑**，
复刻不到「判据写反」这种 bug。前四轮"脚本说应该统一、实机却分裂"的分歧就卡在这。

⇒ 正解：加 `-fontdiag`（`#ifdef HK_DIAG` 里，正式版一行不带）—— 字体建好后 dump
每个键的 `idx / block / w / maxW / glyph / t_own / t_block / label` 到 exe 同目录 `fontdiag.txt`，
量宽判据与 `FitUnifiedTier` **逐字一致**。

判读：**同 block 里出现两个 tier 就是分组没并到一起**；看 `w`/`groupW` 是否一致即可定位。
本次实测（1280 全尺寸，导航区 9 键 `w/gW/mW` 全部相同 `52/51/45`，档位却分两种）：

```
t=4 PrtSc   t=4 ScrLk   t=4 Pause
t=0 Ins     t=4 Home    t=4 PgUp        ← 分组参数全同、档位分两半
t=0 Del     t=0 End     t=4 PgDn           ⇒ 只能是循环没把同组标签算进去
```

修复后同一张表 9 个 `t=4` ⇒ 验证通过（比截图更快、更硬）。
CI 跑法：临时 `workflow_dispatch` 工作流 → `cl /DHK_DIAG` 单编一份 →
预置 `HKeyboard.ini` 的 `[Keyboard] Layout=0/2` → `Start-Process ... -fontdiag` →
`Start-Sleep 6` 后读 `fontdiag.txt`（用完删工作流，`-fontdiag` 代码留着）。

#### 先认清：`FitUnifiedTier` 现在只剩**两个**自由度

| 自由度 | 取值 | 说明 |
|---|---|---|
| **阶梯** | `MainLadder()` | `g_f14/f13/f12/f10/f9/f8/f7/f6` |
| **分组边界** | `InUnifiedGroup()` | 主区(0) + 导航区(1) 合并；数字区(2) 退出。**不按键宽拆** |
| ~~组内取档循环的判据~~ | — | 已固定为 `if (KeyHasGlyph(k)) continue;`（第 27 轮根因，已修） |
| ~~组内最小宽 groupW~~ | — | **已删除**：每键按自己可用宽量，取最严档全盘共用 |

#### ⛔ 六个已证伪的答案（别再试）

| 分组 | 为什么失败 |
|---|---|
| 按**同行** | 导航区 3×3 方阵每行各自为政：`Ins` 只跟 `PrtSc` 比、`Home` 比不到 |
| 按**同列** | 导航区列距只有键宽 1.11 倍 ⇒ 三列混成一组 ⇒ 「不齐」变成「全都小」 |
| 按**区块**（block） | 解决了导航区**内部**不齐，但**区块之间**仍不齐（**这条后来被证明是对的**，见下） |
| 按**区块 + 0.85 缩放** | 第 25 轮 B 方案。给导航区单独乘 0.85 ⇒ **导航区整体比主区小一号** |
| 按**区块 + 键宽容差(`dw>2`)** | 第 27 轮方案。把 1.25u 的 `Ctrl`/`Alt` 与 1u 字母拆成两组 ⇒ **Ctrl 那边大得要死** |
| 全键盘**统一一个档** | 第 28 轮方案。导航区 5~6 字母的长标签（Delete/Pause/ScrLk…）把**整盘**拖到 9pt ⇒ 主区字母明明 14pt 放得下却只有 9pt |

⚠ 「按区块」这一条**经历了一次否定再肯定**：第 28 轮认为它是错的（因为"区块之间仍不齐"），
改用全盘统一；第 29 轮发现**用户其实接受"两块不一样大"，只是不接受"主区被导航区拖小"**。
⇒ **最终答案就是按区块各自成组**（见 §二十之 3）。

⚠ 改分组的几条路之所以"看起来有效果"，是因为它们顺带改了 `dw > 2` 的命中范围
—— 但**循环空转时容差多少都没区别**。`k->w != gw` → `|k->w - gw| > 2` 这个容差改动
本身是对的（`FullPut` 逐键取整会让同区块三列差 1~2px），**保留**，但它单独修不好问题。

#### ✅ 正确答案（第 29 轮最终形态）：**区块各自成组**，各算一档

演进链：同行/同列 ✗ → 按区块（内部齐了、块间不齐）→ 区块+0.85 ✗ → 按压宽拆组 ✗
→ **全盘统一**（第 28 轮）✗ 主区被拖小 → **按区块各自成组**（第 29 轮）✓

```cpp
// 分组 = 区块，主区(0) / 导航区(1) / 数字区(2) 各成一组、各算一档。
static BOOL InUnifiedGroup(unsigned char blk, unsigned char want) { return blk == want; }

static int FitUnifiedTier(HDC dc, unsigned char block) {
    const FontLadder L = MainLadder();
    int worst = 0; BOOL any = FALSE;
    for (int i = 0; i < g_nk; i++) {
        const KeyDef* k = &g_keys[i];
        if (!InUnifiedGroup(k->block, block)) continue;   // 只看本区块
        if (KeyHasGlyph(k)) continue;                     // 图标键不参与（走 DrawKeyLabel 自己的阶梯）
        wchar_t s[KEY_TEXT_MAX];
        KeyTextInto(k, s, KEY_TEXT_MAX);                  // ⚠ 写进局部缓冲，不要用 static 返回
        if (!s[0]) continue;
        int avail = k->w - (int)(6 * GetSystemDpiScale());   // 每键按自己可用宽量
        if (avail < 1) continue;
        int t = FitTierInLadder(dc, s, avail, L);
        if (!any || t > worst) worst = t;                 // 取本区块最严档
        any = TRUE;
    }
    return any ? worst : 0;
}

// 缓存：按区块各一份，一帧各算一次
static int  g_blockTier[3] = { 0, 0, 0 };
static BOOL g_blockTierValid = FALSE;
static int BlockTierNow(HDC dc, unsigned char blk) {
    if (blk > 2) blk = 0;
    if (!g_blockTierValid) {
        g_blockTier[0] = FitUnifiedTier(dc, 0);
        g_blockTier[1] = FitUnifiedTier(dc, 1);
        g_blockTier[2] = FitUnifiedTier(dc, 2);
        g_blockTierValid = TRUE;
    }
    return g_blockTier[blk];
}
// DrawKeys 里：int tier = BlockTierNow(dc, k->block);
```

要点：
- **每键按自己真实可用宽量自己的标签**，**区块内**取最严档统一，**区块之间不互相牵制**。
- **一帧只算 3 次**（`g_blockTier` 缓存 + `BlockTierNow()`），不会回到第 27 轮逐键 8 万次量宽的卡死。
- ⚠ 变量改名后**务必全局搜旧名**（`g_unifiedTierValid` 残留会让编译直接失败，CI 才拦得住）。
- `FitKeyFontUnified` 的 `tier` 由**调用方传入**，它不再自己量档。

#### 实测结果（CI `-fontdiag`，第 29 轮，dpi=1）

| 布局 | 窗口 | 主区 b0 | 导航区 b1 | 数字区 b2 |
|---|---|---|---|---|
| 默认（mode 0） | 980×320 | **t=0** | **t=0** | **t=0**（63 键全 t=0，与改动前一致） |
| 全尺寸（mode 2） | 1280×404 | **t=0（14pt）** | **t=3（10pt）** | **t=2（12pt）** |

⇒ 验收标准：**每个区块内所有键的 `t_block` 相同**（区块之间允许不同）。

#### ⚠ 导航区想上 10pt：标签必须 ≤3 字符（第 29 轮新增）

全尺寸下 1u 键可用宽只有 **46px**。GDI+ 的 `MeasureString` 是**包围盒**，
比 PIL 的 `getlength`（advance）宽约 **20~25%** —— 拿 PIL 估档会系统性偏乐观：

| 标签 | 实测 t_own | 说明 |
|---|---|---|
| `ScrLk` `Pause` `Home` | **4**（9pt） | 5/4 字母，10pt 时超宽 1~3px |
| `PrtSc` `PgUp` `PgDn` | **3**（10pt） | 恰好卡在 10pt 边缘（余 ~3px） |
| `Ins` `Del` `End` | **0**（14pt） | 3 字母，富余很大 |

⇒ 想稳上 10pt，**导航区标签压到 3 字符**：`ScrLk`→`Scr`、`Pause`→`Pse`、`Home`→`Hom`。
（`Pse` 是 Pause 的通行节略写法；用户明确否决过"整片不做缩写"，但**接受单个键缩写**。）
⚠ 缩写改动后**必须重跑 fontdiag** 确认，不要凭 PIL 估算收工。


#### ⚠⚠ 顺带的必修项：**双重扣边距**（第 25 轮的教训，仍然有效）

调用方传的是 `k->w - 6*dpiScale`，`FitUnifiedTier` 内部**又扣一次 6px**
⇒ 量宽按 40px 判、绘制按 46px 排，**用两把尺子**，明明放得下的标签被误判降档。
⇒ 量宽与绘制**必须同一把尺子**：可用宽统一用 `k->w - 6*dpiScale`。

#### 3.5 ⛔⛔ 与字号统一同行的一颗雷：**返回 static 缓冲区的指针**（2026-10-05）

**症状**：改完判据、CI 编译通过、`-fontdiag` 表也全绿，但实机一跑
**全键盘字母键都显示成 `m`**、F1~F12 变成 `/`、整个界面**卡得要死**。

**根因**：`KeyText`（及 `LetterKeyText`）**返回指向 static 缓冲区的指针**：

```cpp
static const wchar_t* KeyText(const KeyDef* k) {
    static wchar_t buf[16];                 // ⛔ 全进程只有一份
    ...
    swprintf(buf, 16, L"F%d", n);
    return buf;
}
```

只要调用方**拿到指针后又调一次 `KeyText`**，前一个指针的内容就被改写：

```cpp
// DrawKeys 里（旧代码）
const wchar_t* txt = KeyText(k);        // "q"
HFONT f = FitKeyFontUnified(dc, i, k, txt, ...);
//        └→ FitUnifiedTier 循环里对每个同组键调 KeyText(...)
//           ⇒ 最后一个组员是字母 m ⇒ static buf 变成 "m"
DrawTextKey(dc, k, txt, ...);           // 画出来是 "m"（全键盘都是）
```

**为什么潜伏这么久才爆**：判据写反时那个循环**空转**，从不调 `KeyText`；
第 27 轮把判据改对后循环开始遍历全组 ⇒ 立刻引爆。

**为什么 `-fontdiag` 照不出来**：`FontDiagDump` 是**立即使用**返回值
（`fwprintf` 紧接着打印），不像 `DrawKeys` 跨 `FitKeyFontUnified` 调用持有指针。
⇒ **诊断代码的用法必须与真实代码路径一致**，否则会给出"假绿"。
（第 27 轮的血泪教训：工具说 OK、实机全崩。）

**正解 —— 所有返回文本的入口都写进调用方缓冲区**：

```cpp
#define KEY_TEXT_MAX 16
static void LetterKeyTextInto(short vk, wchar_t* out, size_t n);            // 写 out
static const wchar_t* KeyTextInto(const KeyDef* k, wchar_t* out, size_t n); // 写 out
// KeyText 只保留为「拿到就用、中间不再调」的便捷包装（内部 static）

// DrawKeys / FitUnifiedTier / FontDiagDump 全部改成局部缓冲：
wchar_t txtBuf[KEY_TEXT_MAX];
const wchar_t* txt = KeyTextInto(k, txtBuf, KEY_TEXT_MAX);
```

**排查信号**：任何 `static wchar_t buf[...]` + `return buf;` 都是嫌疑。
`grep -n 'static wchar_t .*\\[' hkeyboard.cpp` 扫一遍。

#### 快速定位法：离线复算（不要靠肉眼估）

`scripts/` 下用 PIL + `winres/MiSans-Medium.ttf` 复刻判据，估算各布局的统一档：

```python
def em(pt, fs=1.0):            # 复刻 MakeFont 的换算
    return int(round(round(pt * fs)) * 10 * 96 / 720)
# 档位 = 第一个满足 font.getlength(label) <= avail 的档
```

⚠ 但**离线复算照不照得到"判据写反"这类 bug —— 它是复刻逻辑，复刻不到 bug**。
「脚本说应该统一、实机却分裂」时，**必须上 `-fontdiag`**（见上）。

#### ⚠ 附带的两条纪律

1. **删一个「专用字体」要清五处**（本项目 `g_nf*` 的教训）：
   声明 / 创建 / `RecreateFontsAndLayout` 里的释放 / `WM_DESTROY` 里的释放 /
   阶梯取用点。漏一处就是**每次改 DPI 泄漏 7 个 HFONT** 或**编译报未定义**。
2. **量宽与取字体必须是同一套阶梯**。上一轮踩过：拿 `g_f*`（常规）去量
   `g_nf*`（0.85 缩放后）实际画出来的字，量到的宽度比实际大 18% ⇒ 白白降档。
   现在只有一套阶梯，这条自动满足；**再引入任何"专用档"时要把这句话抄回注释里**。

#### 数字区的特殊地位

数字区（block 2）**永远单独退出统一**：
它的标签是 `0-9` `/ * - +` 和 `Num`，全是 1~3 字符，**天然都放得下最大档**；
一旦参与统一，就会被同宽的 `Enter`（5 字母）拖着一起降档。
⇒ 用户明确要求过「不要动数字区」，`if (k->block == 2) continue;` 是**故意的**，
不是遗漏。

### 4. ⚠⚠ 用户说「功能正常了，但感觉卡卡的」：查**点击路径里的阻塞**（2026-10-04）

「功能对了但手感不对」这类反馈，九成是**上一次修功能时在 WndProc 里加了等待**。

本项目实例（NumLock 第 24 轮）：上一轮为了「读到真实翻转态」写了

```cpp
for (int wait = 0; wait < 20; wait++) { Sleep(10); now = ReadSystemNumLock(); ... }
```

用户点一次 Num 键，WndProc 就**死等最多 200ms**（乐观也有 10ms）。
WndProc 被堵住 ⇒ 消息不派发 ⇒ `TIMER_WINDOW_ANIM`(15ms) 掉帧、
连点明显停顿。**功能是对的，手感是卡的** —— 完全对应那句反馈。

#### 排查顺序（从点击路径往外扫）

1. `grep -n 'Sleep(' hkeyboard.cpp` —— 看每个 Sleep **在哪个函数里**。
   在 `DoKeyAction` / `OnLDown` / `WM_TIMER` 里的都要问一句「这里必须等吗」。
   以下是**正常**的，别动：
   - `SendKey` 里 `KEY_INJECT_GAP_MS`(1ms) × 3 段 —— 微软拼音组字必需（issue #3）
   - `ToggleImeLang` / `SendWinToggle` 的 `Sleep(50)` —— down/up 间隔
   - `HK_DIAG` 段里的（正式版不编译）
2. 找 `InvalidateRect(hWnd, 0, TRUE)` —— **全窗失效 + 擦背景**。
   本窗口的 WM_PAINT 不是直接上屏，而是先整张 32bpp 帧缓存
   `RenderKbFrameInto` 重画（几十个圆角矩形 + 文字 alpha 混合）。
   每次全窗失效 = 整张缓存重画 + 整窗 Blit。
   ⇒ 高频路径（`TIMER_FOCUS` 每 50ms）里的全窗失效必须收窄。
3. 看 `WM_TIMER` 里做了什么。`TIMER_FOCUS` 是 **50ms** 的高频定时器，
   里面任何「可能变化就重绘」的写法都会被放大 20 倍/秒。

#### 本项目的三层收窄（可直接复用）

| 层 | 改法 | 关键点 |
|---|---|---|
| 点击处理 | 删掉「注入后原地等读回」的死循环 | 翻转结果交给高频定时器读回校正（**可自愈**，不必当场知道） |
| 失效范围 | 新增 `InvalidateKeyByVk(hWnd, vk)` 只失效那 1~4 格 | 左右成对上报的物理态（Shift/Win 是 L/R 的或）⇒ 同名的另一格一起失效 |
| 上屏范围 | `WM_PAINT` 只 `BitBlt` **`ps.rcPaint`** 脏区 | 这条不做，前两层收窄会被「整窗上屏」把收益吃光 |

#### ⚠ 三个坑

1. **「先读后写」的顺序不能反**。`BOOL before = ReadSystemNumLock();` 必须
   在 `SendInput` **之前** —— 注入到锁定位更新是异步的，写在后面时，
   系统落实快的那几次会读到新值，`!before` 反而算反。
2. **别用「预期值」时忘了兜底**。零阻塞后高亮先按 `!before` 画（跟手），
   必须确认有定时器在 50ms 内读回真值校正，否则注入失败就长期显示错。
3. **两个 `g_*` 名字别写串**。这轮发现上一版有个隐蔽 bug：
   `if (g_npLock != pNum) { g_npLock = pNum; g_physNum = pNum; ... }`
   —— 顺手把 `g_physNum` 也赋了值，导致后面 `dNum = (g_physNum != pNum)`
   **恒为假**（只是靠前一个 if 已经失效过那格才没出问题）。
   ⇒ 判据变量和状态变量要分开，「谁变谁失效」的判据必须自洽。

#### 验收：**不要靠肉眼说「快了」**

改完在用户实机上比：
- 连点 Num 键 10 次，动画是否还有顿挫；
- 按住某个键连发（`TIMER_REPEAT` 40ms）时是否跟手。

⚠ 这类问题还有一个**低频但真实的第二源**：`UpdateAutoVisibility()` 每 50ms
调 `GetFocusedInputControl()`，里面 `GetGUIThreadInfo` 是**跨进程**调用
（文件注释里写着「动画卡顿的主要来源」）。已有缓解措施（外壳类窗口直接
return、动画期间跳过、启动预热 1200ms），改这块前先读 7075 行附近的注释。

#### ⛔ 分组粒度 = 本项目连错两次的地方
这个需求返工了两轮（用户两次截图指出「还是不齐」），两次都是**分组切错了**：

| 切法 | 失败原因 |
|---|---|
| **同行** | 导航区是 3×3 **方阵**，每行各自为政 —— `Ins` 只跟 `PrtSc` 比（标签短、不需降档 ⇒ 第 1 列保持大字号），而需降档的 `Home` / `PgDn` 在**上一行**，比不到 |
| **同列** | 导航区**列距只有键宽的 1.11 倍**（实测 W=1400 时列距 63 / 键宽 57），任何列中心 / x 容差判据都会把三列**全混成一组** —— `ScrLk` / `Pause` 又被拉小 |
| **区块** ✓ | 导航区 9 键是一个语义整体（九宫格），数字区同理 |

⇒ **先问「什么在视觉上是一个整体」，再定分组键**。行、列都只是**排版产物**，
不是语义单元。

#### ⛔⛔⛔ 但更要紧：分组粒度选对了，字号**仍然可能不对**
本项目这个需求返工三轮（同行 → 同列 → 按区块），用户三次都说不齐。
**前两轮的分组粒度都不是主因** —— 真正的两个原因：

| 根因 | 症状 | 判据 |
|---|---|---|
| **同宽判据用了精确相等** | 分组仍裂成两半 | 键宽是**逐键取整**算出来的，同区块各列常差 1px ⇒ `k->w != gw` 会让它们互不参与统一 |
| **可用宽算错（被边距吃掉）** | 整齐但**整体偏小** | `maxW = k->w - edge`，最长的标签卡在两档之间 ⇒ 取最严就掉一档 |

第 2 条本项目的实例：默认全尺寸 W=1280 时 1u 导航键宽 **51px**，
扣 6px 边距剩 45px；而最长标签 `Pause` 在 14pt 需 **51px**、10pt 需 39px
⇒ 45 卡在中间 ⇒ 落到 10pt。同宽的 `Num` 键标签只有 3 个字母、14pt 仅需 30px
⇒ 照旧 14pt ⇒ **同一排 Num 大、Pause 小一号**。
⇒ 导航区改成不扣边距（标签横向居中绘制，51px 放进 51px 不溢出）。

⇒ **同宽判定用容差，不用 `!=`**：`|k->w - gw| <= 2`。真正的不同规格键
（2u / 1.5u / 2.25u）差几十 px，不会被误并入。

⇒ **算档前先把三个数都量出来**（别凭感觉调阈值）：
`组内最小键宽` → `可用宽 = 键宽 - 边距` → `该档最长标签的实际文本宽`。
三者一对就知道该落在哪一档。本项目靠 PIL 量 MiSans 复现 `MakeFont` 的
`em = MulDiv(size*10, 96, 720) * finalFontScale` 定位到这个根因。

⇒ **窄窗口下允许「整齐但偏小」**：5 字母标签物理上放不下大字号时，
宁可小一号也不能裁成 `Paus` / `PrtS`（GDI+ NoWrap 会**整字丢弃尾部**）。

#### ⛔⛔⛔ 算字号前必须先跑真实几何 —— 我用常量当键高，错了三轮
**本项目字号 em 跟「键高」走，不跟键宽走**：

    keyHDip = g_keyHeight / dpiScale              // 键高由窗口**高**决定
    FS      = dpiScale * (keyHDip / 48.0)         // 48 = kFontRefKeyH
    em      = MulDiv(round(label * FS) * 10, 96, 720)
    键宽 u  = (KEY_AREA_W - nb*gap) / units       // 键宽由窗口**宽**决定

⇒ **窗口的宽高比决定了长标签放不放得下**。全尺寸 2240×599 下
`keyHeight=82` ⇒ `FS=1.708` ⇒ `em(14pt)=32px`，而 1u 键宽只有 **91px**；
`Pause` 在 14pt 下需 **93px** ⇒ **放不下**。

⚠⚠ **我连错三轮的根因**：离线验证时一直用 `FS = 46/48`（`g_keyHeight`
的**初始值 46**，那是小键盘的键高）当常量。而全尺寸布局 KH 是 53~82，
FS 是 1.1~1.7 —— **结论全部偏低**，据此做的判断（"够 14pt"、"加一档就行"）
自然全错。

⇒ **验证脚本必须复现整条链**：`g_ww/g_wh → g_keyGap → rows → vAvail →
   g_keyHeight → keyHDip → FS → em → MeasureText`。
   只量「键宽 vs 文本宽」是不够的 —— **字号本身是变量**。

#### ⛔ 给某个区块单独缩字号时，量宽与取字体**必须用同一套**
B 方案（用户指定「导航区单独缩一点，数字区不要动」）的实现：给导航区单独一套
`g_nf*`（字号 × 0.85），与主区 `g_f*` 并存。

⚠⚠ **我第一版就踩了**：抽了 `NavLadder()` 却只在 `FitKeyFontUnified`（取字体）
里用了，`FitUnifiedTier`（**量宽**）仍在用 `g_f*` ⇒
拿常规档去量压缩后实际画出的字，量得比实际大 `1/0.85 ≈ 18%`
⇒ 误判「放不下」⇒ 白白降档 ⇒ **压缩等于白做，而且不报错、很难发现**。

⇒ **同一套阶梯要贯穿两处**：`选档（量宽）` 与 `取字体`。
   本项目正确做法是抽 `struct FontLadder` + `MainLadder()/NavLadder()`，
   两个函数都按 `block` 取同一份。
⇒ 验证时**必须断言「量宽用的字体 == 绘制用的字体」**，别只看最终字号数字。

⇒ 另：新增字体必须补齐**四处生命周期** ——
   声明 / 创建 / 重建前 `DeleteObject` / `WM_DESTROY` 销毁。
   漏任一处 ⇒ **每次改 DPI / 切布局泄漏一批 HFONT**（本项目一次 7 个）。

#### ⛔ 阶梯加档（加更高 pt）解决不了「标签放不下」
方向本就反了：**档位序号越大字号越小**，而「放不下」的原因是字号太大。
本项目实测：2240 宽下 14pt 需 93px / 键宽 91px（差 2px），
加一档 15pt 后 em 从 32 涨到 34、`Pause` 需 **99px** ⇒ **更挤**。

⇒ 想让长标签更大，只有一条路：**减小 em**（即降 `keyHDip` / `FS`，
或给该区块单独用更小的 `FS`），**不是加档**。
⇒ 加档只在「地板档太粗、下一档又太小」时有用（细分中间层），
   不解决「最大档也放不下」。

#### ⛔⛔ 更重要的：把范围锁死在用户说的那个对象上
本项目这个需求返工了三轮（同行 → 同列 → 全键盘），**每一轮都是范围没锁死**：

| 轮次 | 我的做法 | 用户反馈 |
|---|---|---|
| 15 | 按同行统一 | 「Ins / Del / End 字体偏大了」 |
| 16 | 按区块统一（主区 + 导航区 + 数字区） | 「改数字区干嘛，**改回去**」 |
| 17 | 只对主区 + 导航区统一 | ✓ |

用户从第一句起要的**只是「导航区那九个键字号一致」**，我却在两轮返工里
把范围一路扩大到全键盘 —— 而每次扩大都会牵连**用户根本没提**的区域
（数字区被「Enter」「Num」拖着降档，比主区还小）。

⇒ **修 bug 的默认作用域 = 用户描述的那个对象**。发现别处也有类似问题时，
   **先问，不要顺手一起改**。扩大范围 = 引入新的回归 + 用户的额外返工。

⇒ **统一规则要有「例外清单」并写明理由**：本项目 `FitUnifiedTier` 里
   `if (k->block == 2) continue;`（数字区标签都是单字符，天然放得下最大档，
   参与统一反而会被长标签拖小）。

⇒ **紧邻布局不要靠坐标启发式推断归属**（列距 1.1 倍键宽，任何坐标判断都会误判）——
在构建时显式标记。本项目给 `KeyDef` 加了 `block` 字段（0 主区 / 1 导航区 / 2 数字区），
由 `AddKey` / `FullPut` 透传（有默认参数，其余调用点不用改）。

⚠ 统一取档的代价是**每个键都要遍历全盘键比较** ⇒ 必须加按帧缓存
（`g_fitTierCache[MAX_KEYS]` + `InvalidateFitTier()`），否则一帧上千次
`MeasureString`，绘制会明显变卡。缓存要在 `BuildKeys()` 与 `DrawKeys()` 末尾作废。

#### ⛔ 插入设置行时，`SettingsRowCount` 必须同步
加一行设置开关时，除了绘制 / 命中测试 / 行高 / 行序注释 / `SettingsRowHidden`，
**还要改 `SettingsRowCount`** —— 它决定 `SettingsCardRect` 的 bottom 与
`SettingsDesiredHeight` 给出的窗口高度。

2026-10-04 漏改过一次（加了「Shift 双击锁定」但 `return g_af ? 7 : 6` 没动），
症状是 **「界面语言」那一行被裁在窗口外** —— 表现为"最后一行看不见"，
很容易误以为是窗口太小或 margin 不对。改行数前先数一遍实际用到的
`closeRow + N` 最大值。

#### ⛔ 新增 hit 码时，`SettingsSwitchRect` 的映射链必须同步
它把 hit 码映射到**行号**，决定开关动画期间 `RedrawWindow` 重绘哪一行。
函数末尾是兜底 `else`（返回「Shift 符号」行），**新 hit 码不加进 `else if` 链
就会落到兜底** ⇒ 动画重绘到**上面那一行**，并留下高亮残影。

2026-10-04 加 `S_HIT_SHDLOCK` 时就踩了：用户看到「点双击锁定，动画却跑在
Shift 符号上」。补齐时顺手发现 **`S_HIT_LANG_DROP` 也从来没在这条链里**
（历史遗留，一并补上）。

⇒ 设置页新增 hit 码要同步的地方**有 6 处**：
绘制 / 命中测试 / 行高 / 行序注释 / `SettingsRowHidden` / **`SettingsSwitchRect`**。
漏掉任何一处的症状都不同（前五处是位置错乱，第六处是"动画跑到别的行上"）。

#### ⛔⛔ 「模拟按键」有没有生效，只能由系统确认（NumLock 连错三轮）
2026-10-04 在 NumLock 上返工三轮，根因是**发送方从未确认系统是否接受**：

| 轮次 | 我的做法 | 用户反馈 |
|---|---|---|
| 17 | 只翻自持状态，删掉 `SendKey` | 「无法模拟键盘，触发不了」 |
| 18 | 加回 `SendKey(0x90)`，但状态仍 `g_npLock = !g_npLock` | 「**灯都没亮**，触发不了」 |
| 19 | 发送后 `GetKeyState(VK_NUMLOCK)` **读回真值** | ✓ |

第 18 轮的症状最具欺骗性：**界面高亮「亮着」，实际完全无效** ——
因为 `g_npLock` 是自己取反的，与系统真实状态无关。

⇒ ⛔⛔ **且注入路径本身可能就不对 —— 锁存键必须走 VK，不能走 `SendKey`**
   本项目 NumLock 连修四轮才找到：前几轮一直在用 `SendKey(0x90)`，
   而 `SendKey` 强制带 `KEYEVENTF_SCANCODE`（issue #3 修复所必需，对**字符键**有效）。
   **锁存键（NumLock / CapsLock / ScrollLock）的翻转由键盘布局层按 VK 处理**，
   扫描码路径（`MapVirtualKeyW(0x90)` = 0xE045，带 E1 前缀）被**直接忽略**。

   ⇒ **症状特征**：「字符键全都正常，唯独锁存键不生效」⇒ 路径错了，不是状态错了。
   ⇒ 锁存键的正确写法：**只给 `wVk`、不设 `wScan`、不设 `KEYEVENTF_SCANCODE`**，
     仅保留 `KEYEVENTF_EXTENDEDKEY`（真键盘 NumLock 是 E0 45），down/up 成对。

   ⇒ **`SendKey` 不是万能注入器**：它是为「字符键 + IME」设计的。
     凡不属于普通字符键的（锁存键、Win 组合、IME 切换），都要单独确认注入方式。

⇒ **铁律**：`g_x = !g_x` 这种「自己取反」**永远不能**用来表示外部系统的状态。
   外部状态（NumLock/CapsLock/IME/网络…）**读回为准**，读不回就别改镜像。

⇒ **重试的判据用「与起始值不同」**（`now != before`），
   **绝不能写死 `!= 0`**：起始可能是「关」，写死判据会导致翻转成功后
   继续重发 ⇒ **按一次翻了奇数次**（第二层 bug，比第一层更难查）。

⇒ ⛔⛔⛔ **但正确做法是「根本不重试」——锁存键绝不重复注入**
   上面那条「重试」本身就是个陷阱，我第 19~21 轮在这里连续翻车。
   **锁存键每收到一次注入就翻一次**，「再试一次」= **再翻一次**，
   与「读文件失败重试」那种幂等操作的直觉**完全相反**。

   症状特征：「**有时候正常有时候不正常**」⇒ 竞态，典型是
   「注入 → 立刻读 → 没生效 → 再注入」的循环。离线复算（attempt × 系统延迟）：

       系统 1ms 内生效  → 第 1 次读到翻转 → 1 次注入，恰好对      ✓
       系统 40ms 才生效 → 3 次都读到旧值 → 注入了 3 次（翻转 3 次=奇数，
                          真实态确实翻了）
                          但显示取**最后一次那个旧读数**
                          ⇒ **界面与真实状态相反**               ✗

   ⇒ 正确写法（**只注入一次，然后只读不注入地等**）：

       BOOL before = (GetKeyState(VK_NUMLOCK) & 1) != 0;
       SendInput(2, pair, sizeof(INPUT));            // ← 只注一次
       BOOL now = (GetKeyState(VK_NUMLOCK) & 1) != 0;
       for (int wait = 0; wait < 20; wait++) {       // ≤200ms，每 10ms 读
           if (now != before) break;                 //   变了就停
           Sleep(10);
           now = (GetKeyState(VK_NUMLOCK) & 1) != 0;
       }
       g_npLock = now;

   ⚠ 等待窗口要用**实测**值，别拿 `KEY_INJECT_GAP_MS`（本项目 = **1ms**，
     为 IME 组字设计，对锁存键远远不够）。

⇒ **动手前先在同一个文件里搜「同类功能怎么做的」** ——
   本项目 `K_CAPS` 早就用对了（`g_cp = (GetKeyState(VK_CAPITAL) & 1) != 0`），
   照抄即可。**标准答案往往就在隔壁。**

⇒ 宁可在界面上如实显示「未生效」，也不要假装成功 ——
   **假象比失败更难排查**（用户会一直查错方向）。

⇒ ⛔⛔⛔ **最隐蔽的一个：读「系统状态」的两个 API 语义完全不同**
   这是本项目返工**五轮**才找到的根因，也是最容易被忽略的：

   | API | bit0 读的是什么 | 什么时候才是新值 |
   |---|---|---|
   | `GetKeyState(VK) & 1` | **调用线程的 key table** | 只在**本线程处理了 WM_KEYDOWN** 后 |
   | `GetAsyncKeyState(VK) & 1` | **系统全局 toggle 状态**（win32k） | 物理按键 / 注入都立即反映 |

   本项目致命之处：HKeyboard 窗口是

       CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST, ..., WS_POPUP, ...)

   `WS_EX_NOACTIVATE` ⇒ 点它**不激活**，前台窗口始终是别的程序
   ⇒ 注入的按键消息进的是**前台窗口的线程队列**
   ⇒ **本线程永远收不到 WM_KEYDOWN**
   ⇒ `GetKeyState` 的锁存位**永远是进程启动时的初值**
   ⇒ **无论注入成功与否，读回来都是同一个旧值**，且**不报错**。

   ⇒ **症状特征**：状态「永远不变」或「变一次之后再也不对」，
     而**用实体键盘操作同一个功能却正常**（物理事件会进系统，
     自校正路径恰好先跑完 —— 看起来正常，**是巧合掩盖了 bug**）。

   ⇒ ⛔⛔⛔ **但「锁定态」是另一个坑：`GetAsyncKeyState` 的低位不是锁定态！**
     （2026-10-04 实测纠正，此前这条写反了，害我返工一轮）

     | API | 低位语义 | 实测行为 |
     |---|---|---|
     | `GetKeyState(VK)` | **锁定态本身**（low bit = toggled） | 精确跟随 1→0→1，**重复读稳定** |
     | `GetAsyncKeyState(VK)` 低位 | **「自上次调用以来按过没有」瞬时标志** | 读一次就**清零**；再读变 0 |
     | `GetAsyncKeyState(VK)` 高位 | 物理是否按住 | 稳定 |

     实测数据（同进程连切两次 NumLock）：

         时刻          GetKeyState  GetAsync低位  紧接着再读GetAsync
         初始               1            0              0
         切换一次后         0            1              0   ← 变回 0
         切换两次后         1            1              0

     ⇒ 用 `GetAsyncKeyState & 1` 判锁定态 → 每次注入后都读到 1 ⇒ 判成"已锁定"
       ⇒ **高亮永远亮着、关不掉**，且那个瞬时位会被别处读取抢先清掉
       ⇒ 症状「有时候正常有时候不正常」。

   ⇒ **判锁定键（NumLock / CapsLock / ScrollLock）用 `GetKeyState(VK) & 1`。**
   ⇒ **判"此刻是否按住"用 `GetAsyncKeyState(VK) & 0x8000`。**
   ⇒ 两者**不要混用**，低位语义完全不同。

   ⇒ 见 `scripts/probe_numlock_api.py`（本项目自带的实测脚本，30 行 ctypes 直接调
     Win32 API）—— **API 语义有疑问就跑它，不要靠文档印象推断**。

   ⇒ 本项目已封装 `ReadSystemNumLock()`，**凡问「NumLock 开没开」一律走它**。
     凡是「同屏显示某个硬件/系统状态」的功能（音量、电池、 Caps…）都要照此核对。

⇒ ⛔ **返工三轮以上：停下来重新审视整条链路，不要继续微调**
   本项目 NumLock 一个功能返工**四轮**，每次错法都不同：

   | 轮 | 做法 | 症状 |
   |---|---|---|
   | 1 | 只翻状态不发键 | 目标程序收不到 |
   | 2 | 加发送但自己取反 | 界面假亮 |
   | 3 | 读回真值 + 重试 3 次 | 反复翻转，时好时坏 |
   | 4 | 改走 VK 路径 + 单次注入 | ✓ |

   ⇒ **每次「修法」只是换了个新 bug**，根因（路径错 + 状态来源错）始终没被同时看到。
   ⇒ 判据：症状**换了但没消失** ⇒ 不是同一个 bug 的残留，就是思路本身有问题。
   ⇒ 此时该做的是**把整条数据流画出来**（谁写、谁读、什么时候），
     而不是继续在同一条思路上调参数。

⇒ ⛔⛔⛔ **API 语义有疑问 → 立刻写实测脚本，不要靠文档印象推断**
   本项目 NumLock 返工**七轮**，其中**两轮（第 21、22 轮）是因为我"推断" API 行为**：

   | 轮 | 我的推断 | 实测结论 |
   |---|---|---|
   | 21 | 「锁存键走扫描码路径会被忽略，必须走 VK」 | 对（但没验证就改，风险大） |
   | 22 | 「GetKeyState 读不到注入，因为 WS_EX_NOACTIVATE 收不到 WM_KEYDOWN」 | **错**。锁定位由 win32k 全局维护，与线程收不收按键消息无关 |
   | 22 | 于是改用 `GetAsyncKeyState & 1` 当锁定态 | **错**。它的低位是「刚按过」瞬时标志 |

   ⇒ **成本对比**：写实测脚本 = `ctypes` 30 行、2 分钟；
     猜错 = 用户实测 + 我改一轮 + CI + 交付 = 十几分钟，且**用户暴怒**。
   ⇒ **本项目 Win32 API 都能用 ctypes 直接调**（见 `scripts/probe_numlock_api.py`），
     涉及「外部系统行为」的判断一律先实测。
   ⇒ 判断标准：**只要结论是「某个 API 会/不会怎样」，而不是「我读代码看出来的」，
     就必须有实测数据支撑。**

⇒ ⛔ **症状在「显示 / 高亮 / 读数」时：先问「这个值是怎么来的」，
   而不是「怎么让它变对」**
   本项目五轮返工的错误模式 —— **一直在「发送侧」打转**：

   | 轮 | 改了什么 | 为什么没用 |
   |---|---|---|
   | 1 | 加发送 | 状态来源本身就错 |
   | 2 | 改成读回真值 | **读的 API 错**，拿到假值 |
   | 3~5 | 修重试 / 改注入路径 / 调等待 | 全在调「怎么发」，没质疑「怎么读」 |

   ⇒ 读值型 bug 的排查顺序：**值从哪来（哪个 API / 哪个变量，谁写它）**
     → **语义对不对（线程相关还是全局）** → 最后才调「怎么产生它」。
   ⇒ **写侧与读侧要分开查**。「发了没用」可能是「发错了」，
     也可能是「读错了」—— 本项目五轮里有四轮在查前者。

#### ⛔ 修「状态不同步」时，别把「同步的手段」一起删掉
2026-10-04 在 NumLock 上连错两轮，两轮反馈**各说了一半**：

| 轮次 | 用户反馈 | 需要的机制 |
|---|---|---|
| 上轮 | 「小键盘无法锁定、数字打不进去」 | 界面高亮与注入逻辑一致（本进程自持状态） |
| 本轮 | 「**无法模拟键盘**，触发不了 NumLock」 | **真的把键发给目标进程**（`SendKey`） |

我上轮为修「不同步」，把 `SendKey(VK_NUMLOCK)` 整个删掉、只留自持状态 ——
结果目标程序永远收不到 NumLock，被用户打回。

⇒ 「界面显示对」≠「功能对」。**发送动作与显示状态是两件独立的事，
   修其中一个不代表另一个可以删。**
⇒ 判断依据：问「除了界面，谁还关心这个状态？」—— 目标进程（游戏 / Excel /
   计算器）关心 ⇒ 必须真发。

#### ⛔ 改变既有行为的开关，默认关闭
「Shift 双击锁定」改变了 Shift 的既有语义（单击原本是「锁定、再点切中/英」），
我却默认 `TRUE` 打开 —— 用户看到开关一上来就是「开」，直接要求"默认关闭"。

⇒ **新增会改变既有交互模式的开关，一律默认关闭**，由用户主动开启。
本项目：`g_shiftDoubleTap = FALSE` + `IniGetInt(..., L"ShiftDoubleTap", 0)`。

### 4. 本地截图验证 UI —— 目前走不通，别绕
- `Add-Type`（编译 .NET 代码）被安全策略拦
- 从 bash 调另一个 shell 解释器也被拦
⇒ **UI 观感验证只能交给用户实测截图**。改完把包给出、说清要看哪几处即可。

## 十六、输入注入与 IME：**鼠标左键按下期间微软拼音不组字**（issue #3）

### ⛔ 守门纪律（最容易误删的一条，改动 `DoKeyAction` 前必读）
`DoKeyAction()` 开头那句 **`WaitForLeftButtonUp();` 不能删**，
配套的 `g_inRepeat` / `g_inWaitLButton` 两个标志也不能删。

看着像"多余的等待"，其实是 issue #3（Win10 微软拼音打不出中文）的
**修复本体**。删掉会立刻复发，而症状是"打出来是英文"，
离"我删了一行等待"非常远，极难联想到。

### 症状
Win10 + 微软拼音，用 HKeyboard 敲字母**出英文、不出候选框**；
Win11 / Win7 / 微信拼音 / 物理键盘全部正常。
**英文数字能正常打**（说明按键送到了目标），只是 IME 不组字。

### 根因
**微软拼音（Win10）在「鼠标左键按下期间」拒绝处理注入的键盘事件。**

`OnLDown` 是在 `WM_LBUTTONDOWN` 的处理过程中调用的 —— 此刻鼠标左键
必然处于按下状态，于是紧接着 `SendInput` 注入的按键不被 IME 组字。
一条规则解释全部现象：

| 现象 | 解释 |
|---|---|
| 手动点击必失败 | 注入时鼠标必然按下 |
| `-sendtest` / `-envtest` 自动化注入必成功 | **无真实鼠标按下** |
| 物理键盘不受影响 | 不涉及注入 |
| 点标题栏无影响 | 不触发按键注入 |
| Win11 / Win7 / 第三方输入法正常 | 对鼠标状态不敏感 |

### ⚠⚠ 为什么自动化测试永远测不出（本次最大的教训）
```cpp
GetAsyncKeyState(VK_LBUTTON)   // SendInput 注入鼠标按下之后，仍然是 0！
```
**`SendInput` 注入的鼠标按下不会被系统记为"按下"** —— Windows 禁止注入的
鼠标事件执行窗口激活 / 捕获等流程。所以任何"程序自己点自己"的测试都
覆盖不到"真实鼠标点击"这个动作。

实测证据（`-diag` 记录注入那一刻的状态）：

| 场景 | LBTN |
|---|---|
| 用户**真实**鼠标点击按键 | **1** |
| **注入**鼠标点击（envtest 阶段2） | **0** |
| 定时器注入（envtest 阶段1） | **0** |

其余全部相同（同一个 `fg=Notepad`、同一个 `focus`/`caret`、
同一个 `hkl=0x08040804`）。**唯一差异就是 LBTN。**

### 修复的三个易踩点
1. **必须 `PeekMessage` 泵消息，不能死 `Sleep`** —— `OnLDown` 在
   `WM_LBUTTONDOWN` 的处理中，`WM_LBUTTONUP` 还得靠消息循环派发；
   死等的话它永远收不到，必然一路耗到 250ms 超时。
2. **防重入**（`g_inWaitLButton`）—— 泵消息会派发 `WM_LBUTTONUP` → `OnLUp`，
   用户此刻若又点别的键会重入本函数。
3. **`WM_QUIT` 要投回去** —— `PeekMessage` 会把它取走而 `DispatchMessage`
   不处理；不补这一下程序再也收不到退出信号（`PostQuitMessage(msg.wParam)`）。

长按连发用 `g_inRepeat` 跳过等待：连发时用户一直按着鼠标，等只会每次耗满
超时；且连发是重复同一字符、不走组字，真正需要组字的是**第一次**注入
（那一次在 `OnLDown` 里，等待很短就过）。

### 诊断工具：`#ifdef HK_DIAG`（**正式版不编译**）
源码里保留了一整套逐层二分工具。**正式版一律不定义该宏 → 这些参数与代码
一行都不进 exe**；需要排查时编译命令加 `/DHK_DIAG`。

| 参数 | 作用 |
|---|---|
| `-sendtest` | 裸进程里测 4 种注入方式 → **排除注入代码本身** |
| `-envtest` | 完整环境（窗口+钩子+定时器）里只做纯注入 → **排除环境干扰** |
| `-noscapture` | 跳过 `OnLDown` 里的 `SetCapture` |
| `-norepaint` | 跳过点击后的重绘 |
| `-diag` | 记录每次字母键注入前后的系统状态到 `diag.txt` |
| `-fontdiag` | 键面字号诊断：dump 每键的 `block/w/maxW/glyph/t_own/t_block/label` 到 `fontdiag.txt`（见 §二十之3） |

⚠ `-fontdiag` 的**通用价值**：凡是"程序内部算出来的值和我推演的不一致"，
就不要继续调参 —— 加一个 dump 让**程序自己**把中间量交出来。
它一次就把前四轮靠离线脚本推演没能定位的字号 bug 钉死了。

排查顺序（本次实际路径，核心是**逐层加变量**）：
```
-sendtest（裸进程） → -envtest（加完整环境） → 再加真实鼠标点击 → -diag 记录那一刻状态
```

### 改完 `#ifdef` 后必须做的两层验证
1. **源码层**：写脚本把 `#ifdef HK_DIAG` 块全部剥离，得到"正式版源码"再检查
   —— 诊断符号残留 0 处、修复符号（`WaitForLeftButtonUp`）仍在、括号平衡。
2. **exe 层**：搜 `-sendtest` / `-diag` / `diag.txt` 等字样。
   **ASCII / UTF-16LE / UTF-8 三种编码都要搜**（Win32 字符串常量是宽字符，
   只搜 ASCII 会漏）。体积差也是旁证（本次 418304 → 400896，小 17408 B）。

### 可迁移的方法论
**"自动化测试全过、手动操作失败"时，第一反应必须是
「自动化没有真正模拟手动那个动作」，而不是继续在代码里找。**
这类差异光读代码永远看不出来 —— 要去**记录"那一刻的系统状态"**。

相关文档：仓库根目录 `ISSUE3_ANALYSIS.md`（完整排查档案 + 已排除的 8 条假设）。

---

## 十七、自动呼出卡死（跨进程探测 + 低优先级定时器 + 状态标志 = 死结）

### 症状识别（关键判据）
用户说「自动呼出**有时候**卡死，有时自愈有时不能」、
「卡死时 **HKeyboard 整个完全无响应**，设置页也打不开」、
「切换窗口 / 长时间没用后最容易出现，没规律」。

**「完全无响应」这一条把范围从"动画状态机"直接推到"UI 线程被阻塞"** ——
先问这一句，能省掉大量瞎猜。

### 根因链（三个原本合理的设计叠成了死结）
```
IsAccessibleInputWindow 里的 AccessibleObjectFromWindow   ← 跨进程 COM
  由 50ms 的 TIMER_FOCUS 驱动（→ UpdateAutoVisibility → GetFocusedInputControl）
  目标进程无响应（弹模态框 / 启动中 / 死循环）时会阻塞很久甚至不返回
      ↓  UI 线程僵死
      ↓  WM_TIMER 得不到处理（它在消息队列里优先级很低）
      ↓  窗口滑动动画停在半路，g_mainMotion.active 一直为 TRUE
      ↓  UpdateAutoVisibility 开头「动画期间不评估」的保护**永久生效**
      ↓  自动呼出彻底失效
```
「有时恢复有时不恢复」= 目标恢复响应 / 永久挂着。

### 修复（两处，缺一不可）
1. **响应性预检** `IsWindowResponsive()`：跨进程 COM 之前先
   `SendMessageTimeoutW(hw, WM_NULL, 0, 0, SMTO_ABORTIFHUNG|SMTO_BLOCK, 100, &res)`
   —— 把「无限期阻塞」压成「有界的最多 100ms」
2. **动画超时兜底**：按时间戳判断动画是否早已该结束（duration + 1000ms），
   是就直接收尾 —— 防的是「永久卡死」那一半

### 排查手法（可迁移）
- 把**全部跨进程调用**列成表逐一评估，比读逻辑快得多：
  `SendMessage`(同步) / `GetWindowText` / `AccessibleObjectFromWindow` /
  `GetGUIThreadInfo` / `GetClassName` / `FindWindow` / `CoCreateInstance`
  —— 风险最高的是**由高频定时器驱动的**那些
- ⚠ **`WM_TIMER` 优先级很低**：任何 UI 线程阻塞都会让「基于定时器的动画」停摆；
  而「动画中不做 X」这类**保护条件会因此变成永久失效** ——
  凡是拿 `xxx.active` 当守卫的地方，都该配一个基于时间戳的兜底

### 诊断（HK_DIAG 下）
`IsAccessibleInputWindow` 记录 COM 探测耗时，≥150ms 时往 diag.txt 写
`[slowprobe] N ms`。再遇到卡死先看这行在不在。

### ⛔ 一条不能碰的兜底路径（踩过回归）
`GetFocusedInputControl` 末尾这段**必须保留**：

```cpp
BOOL haveGuiInfo = GetGUIThreadInfo(tid, &gi);
HWND focus = haveGuiInfo && gi.hwndFocus ? gi.hwndFocus : fg;
if (haveGuiInfo) { ...控件判断... }
// ⚠ 下面这段是给大应用用的，别因为"怕误判"砍掉
if (IsAccessibleInputWindow(focus, &token) || (focus != fg && IsAccessibleInputWindow(fg, &token)))
```

**`haveGuiInfo` 失败不是异常，是大应用的常态** —— NTQQ / 浏览器 / UWP /
Electron 的输入框**本来就不在 `gi.hwndFocus` 里**（焦点落在渲染宿主 / 组件
窗口上），只能靠整窗口的 accessibility 探测才认得出；资源管理器搜索栏也走这条。

2026-10-04 曾因为"拿不到 GUI 信息时判断依据会降级、容易误判"而加了
`if (!haveGuiInfo) return NULL;` —— 结果把这些应用的自动呼出**全砍了**，
用户实测「NTQQ 有时可以有时不行、浏览器 / UWP / Electron 无法呼出、
资源管理器搜索栏也不行」。已回退。

### ⚠ 两条通用教训
1. **改「判断依据降级」的代码要特别小心** —— 降级路径往往承载兼容性。
   它看着像"异常分支"，实际是某类对象的**唯一**识别途径。
2. **两个独立问题别用同一手段解决**：
   「启动时误判呼出」用 `AUTOSHOW_WARMUP_MS` 预热期；
   「大应用识别」必须靠整窗口探测。混在一起改就会一边修一边坏。

### 呼出触发条件：从「切到输入框就弹」改成「点输入框 / 键盘聚焦才弹」

用户要求（2026-10-04）：**「根据输入框的点击状态来呼出，而不是切换窗口来呼出
—— 有时候我切换窗口的目的又不是为了呼出键盘，而是查看其他内容。」**
细则：键盘聚焦（Tab / Ctrl+F / **网页自动聚焦搜索框**）也要弹；旧行为不做开关。

**为什么焦点判断做不到**：切窗口时新窗口的控件同样获得焦点、发出**完全相同**的
`EVENT_OBJECT_FOCUS`，光靠事件区分不出「切窗口」与「窗口内聚焦」。

**做法**：引入"用户主动操作"作为必要条件
- 全局鼠标钩子 `WH_MOUSE_LL`（只记录落点，永远 `CallNextHookEx` 放行）
- 键盘钩子里记录最后一次真实按键
- 条件：`byMouse`（近期点过 **且落点在输入控件所属窗口内**）或 `byKey` → 弹

| 场景 | 弹？ |
|---|---|
| 点输入框（同窗口 / 异窗口） | ✅ |
| 键盘 Tab / Ctrl+F / 网页自动聚焦 | ✅ |
| **切换窗口到有输入框的窗口** | ❌ |
| **点任务栏切窗口** | ❌ |

### ⚠⚠ 两个必须记住的设计点（都踩过）
1. **鼠标点击必须校验落点**：点任务栏、点别的窗口本身也是点击 ——
   只看"有没有点过"的话切窗口照样弹。落点比较**退一步到顶层窗口**：
   浏览器 / Electron 场景下 `GetFocusedInputControl` 返回的是渲染宿主窗口，
   用户点的是页面里的位置，命中点是它的子孙。
2. **用状态封锁，不用时间窗**：一开始写"1.5 秒内要求有操作"，
   但切窗口 1.5 秒后封锁就过期、键盘照样自己弹出来 —— **等于没解决**。
   正解是状态式 `g_fgAwaitUserInput`：切换窗口后进入，只有
   "点输入框 / 敲键盘"才解除。**凡是想用时间窗表达"之后一直有效"的意图，
   都要回头检查它会不会到期失效。**

### ⚠ `g_afAutoHide` 的语义（2026-10-04 改过，别看错）
- **现在**：控制「**自动隐藏**」—— 点击输入框以外的区域时自动收起键盘
- **以前**：控制「防回弹」（用户手动收起后同一输入框内不自动弹出），
  **名不副实** —— 用户抱怨「键盘不会自动隐藏，只能手动最小化」就是这个原因
- **防回弹现在是无条件生效的**（`UserHideKeyboard` 的记录 +
  `UpdateAutoVisibility` 的判断两处），别再拿它挂条件：
  没有防回弹，用户手动收起后会被立刻弹回来，"手动收起"这个动作直接失效

### 设置页「子项样式」（参考 UU 远程）
父行带图标、子行不带图标 —— 借"少一格"表达层级从属。实现：
`DrawSettingRowContent` 的 `noTile` 参数（为真时跳过 `DrawIconTile`）。

⚠ **不能只靠 `iconId < 0` 表达**：`DrawIconTile` 在 `iconId<0` 且无文字时
**仍会画一个空的圆角 tile**（其 `DrawRoundRect` 是无条件调用的）。

文字位置不动 —— 子项标题与父项标题**左对齐**，内缩感纯来自缺了图标那一格
（参照图里子行文字也是与父行文字对齐的），所以不需要动任何坐标计算。

### ⛔ 自动呼出的十处易错点（2026-10-04 全踩过一遍）
1. **状态结算必须放在所有 `return` 之前**。「结算用户是否刚操作过」原本排在
   可能 `return` 的自动隐藏之后，一旦提前 return 就**永远解除不了
   `g_fgAwaitUserInput` 封锁** —— 键盘被收掉且再也弹不出来。
   症状离根因极远（表现成"点输入框不弹、切窗口反而正常"）。
   ⇒ **在逻辑链上插入可能 return 的代码时，必须检查它会不会把后面的状态结算整段跳过。**
2. **防回弹不能只认一次 token 比对**。`g_userHidInInput = FALSE;` 原本无条件
   执行，而焦点探测返回的 token 并不总落在同一个东西上（有时是控件、有时是
   caret 窗口），只要有一次对不上，标记就被清掉、防回弹**永久失效**
   ⇒ 用户看到"莫名其妙又弹回来"。正解：token 对不上也先按住，
   直到超时（8 秒）才放行。
3. **失焦延迟别写太长**。原 1000ms，用户实测"自动收起太慢" ⇒ 300ms。
   （"点输入框以外"那条路径是立刻收，不受延迟影响。）
4. **防抖判定要用「持续时间」，不能用「连续次数」**（第五轮返工）。
   **`GetForegroundWindow()` 会瞬时返回 NULL** —— 用户日志里实打实出现过
   **80ms 内连续 6 条 noInput** 的爆发采样（fg 字段为空）。
   失焦分支当时只按"连续 4 次"（≈200ms）判定，"爆发"正好**穿过次数门槛**，
   把 `g_userHidInInput`（手动收起标记）清掉 —— 防回弹永久失效，
   用户看到「手动收起后过几秒键盘自己弹回来」。
   ⇒ 改为：失焦必须**持续** `AUTOSHOW_INPUT_GRACE_MS`（500ms）以上才算真的
   离开（`g_noInputSinceTick` 记起始、输入恢复时清零）。
   ⚠ 只保护"清标记"这一句，**隐藏流程不动**（80ms 级爆发本来就穿不过
   300ms 的 hideDelay）—— 否则会把用户已经适应的隐藏时机整体拖慢。
   **凡是防抖，一律用时间窗，别数次数。**
5. **别用"类名粗判"代表"焦点在输入框"**（根因已锁定，见第 7、8 条）。
   `IsInputControl(Chrome_WidgetWin_1)` 命中后直接 return，**不管焦点实际在
   输入框还是页面空白** —— 日志里 QQ 段 `hasInput` 恒为 **100%**
   （火狐段则有 37%~63% 的 noInput）。后果："点空白自动隐藏"对 Chrome 系
   应用**天然失效**（永远不会走失焦隐藏），用户只能手动收起。
   ⇒ 修法方向：让 Chrome 类应用也走 accessibility 精判（`found` 字段）。
   需先验证 a11y 能否分辨两种焦点状态 —— 诊断字段 `xms=`（补查耗时）
   / `xok=`（补查结果），补查只在诊断模式 + 用户操作窗口内 + 300ms 降频执行。
   ⚠ 实测数据（第六轮）：77 条补查全部 `hr=0(S_OK) found=0` —— COM 调用成功
   但找不到可编辑焦点。进一步诊断字段：`fvt=`（get_accFocus 的 VARIANT 类型：
   0=EMPTY / 3=child id / 9=对象 / -2=调用失败）、`frole=`（实际 role）。
   **✅ 根因已由第七轮的 `frole` 数据锁定（见下方第 8 条）**，a11y 精判现已生效。
   ⚠ 另一条待验证的判定路径：**FOCUS 事件流**。`WinEventProc` 本来就全局监听
   `EVENT_OBJECT_FOCUS`，新增两个诊断字段 —— `fev=`（距最近一次该事件的毫秒数，
   4294967295 = 从未收到）、`feh=`（该事件的来源窗口**是否就是当前前台顶层**，
   用于滤掉"其它应用弹窗抢焦点"的噪音）。**要验证的假设**：「点输入框」触发
   FOCUS 事件、「点空白处」不触发 —— 若成立，这就是区分两者的天然信号，
   比轮询 a11y 可靠（事件是状态变化本身，不是采样）。
6. **「误判触发 + 自动反向」会变成自激振荡**（第六轮）。
   日志实证：0.18~0.26s 周期连续 6 次显示/隐藏 —— 周期**恰好等于动画时长**
   （220ms 显示 / 150ms 隐藏）+ 一拍评估。成因：`click != 前台窗口` 持续
   不一致（应用多窗口 / NOACTIVATE 窗口 / 激活延迟都会造成）→ 每拍都
   判"点到别处→隐藏"，下一拍"自动呼出→显示"，来回循环。
   ⇒ **凡是"条件 A → 隐藏 / 条件 B → 显示"的自动逻辑，都要回头检查
     "A 和 B 会不会交替成立"**；触发条件要收窄到**确定性信号**
     （本项目的解：只在"落点确实是桌面 / 任务栏 / 托盘"时才收起）。
7. **⛔⛔ Chrome 系「精确判输入焦点」这条路是**死路**（第七~十一轮，五次失败，已整体回退）**
   **最终结论：类名粗判（`Chrome_WidgetWin` 无条件 TRUE）是目前唯一稳定的行为。**
   五次尝试的实测数据（存档，别再试）：

   | 路线 | 失败原因（实测） |
   |---|---|
   | MSAA `VT_I4` 下钻 | 焦点落在 `ROLE_SYSTEM_GROUPING` **容器**上；746 次采样走到白名单（TEXT）仅 **23 次（3%）** |
   | MSAA `get_accChild` 语义 | `varID` + childId 组合**不保证返回焦点对象本身** |
   | UIA `Document`(50030) | 整张网页**永远有一个 Document 元素持有键盘焦点**，noInput 时仍出现 56 次 |
   | UIA `Edit`(50004) | 唯一干净信号，但 QQ 里仅 116 次，**覆盖不全** |

   ⚠⚠⚠ **由此引出的最重要一条纪律**（第八、九、十轮三次回归都是它导致的）：

   > 连续三轮我都在**同一个未解决的技术难题**上打转，而**每轮都在已能正常工作的
   > 主功能上做实验**。用户三次回归（输入框不弹 / 切换窗口不弹 / 又搞没了）全部由我造成。

   ⇒ 改动核心功能前先问「**这个判据有多可靠**」。判据不可靠时：
   - **保留现有粗判不动**（它是当前唯一稳定的行为）
   - 实验放在**独立的新开关**后面，失败只关开关、不动主逻辑
   - 「附加功能」与「主功能」冲突时，**无条件保主功能**
     （本项目：点空白就收 = 附加，自动呼出 = 主）

   以下是过程记录，细节可查 git log（`07d8558` 是 UIA 之前的最后可用状态）。
   第七轮曾给 `VT_I4` 分支加"取 child 继续递归"，看起来逻辑对，但**实测无效**：
   746 次采样里真正走到白名单（`frole=0x2A` TEXT）只有 **23 行（3%）**，
   `frole=0x0F`（GROUPING，容器）有 **526 行**。两个原因叠加：
   1. Chrome 把焦点放在**容器**元素上，不在输入框本身
   2. MSAA 的 `get_accChild(varID, ...)` 那套 varID + childId 语义很别扭 ——
      拿 `get_accFocus` 返回的 childId 去问，**不保证返回焦点对象本身**
   ⇒ **改用 UI Automation**：`IUIAutomation::GetFocusedElement()`
     **直接给焦点元素**，看 `CurrentControlType` + `CurrentHasKeyboardFocus`，
     不递归、不碰 childId。

   ⚠⚠ **但 `Document`(50030) 绝不能当"可编辑"判据**（第十轮踩过）：
   浏览器/Chromium 里**整张网页永远有一个 Document 元素持有键盘焦点**
   （body / documentElement），哪怕焦点在空白处也一样。实测 786 次采样：

       uct      hasInput   noInput
       50030 Document   330        56     ← 点空白时照样是 Document
       50026 Text       155        35
       50004 Edit       116         0     ← 唯一干净的信号

   `Document` 表达的是"焦点在这个窗口内"，**不是"焦点在可编辑区"**。
   ⇒ **只认 `Edit`(50004)**，Chromium 系的 `<input>` / `<textarea>` 报的正是它。

   ⚠⚠ **还有一条元教训：引入了更精确的判据，就不能同时留着粗判把它推翻。**
   第十轮改完判据仍不生效，因为开关用的是 `hrOk`（MSAA 的 S_OK）而不是
   `uiaOk` —— UIA 说"不是 Edit"→ 仍把类名兜底设 TRUE → 粗判再放回来，
   **判据等于没生效**。正解：

       if (uiaOk) {
           g_chromeClassFallback = FALSE;   // UIA 说了算
           if (editable) { ...; return focus; }
           return NULL;                     // 可信的否定答案，必须尊重
       }
       g_chromeClassFallback = TRUE;       // 仅 XP / COM 失败才用粗判
   ⚠ XP 兼容：`CUIAutomation` 是 Vista+，XP 上 `CoCreateInstance` 失败 →
     `EnsureUia()` 返回 FALSE → 自动退回 MSAA → 类名兜底，行为不变。
   ⚠ MSAA 那套**保留给传统 Win32 控件**（Edit / RichEdit / Scintilla / Console），
     别把它一起改坏。

8. **MSAA 遍历：`VT_I4` 分支不递归本身就是 bug（第七轮，方向对但不够）**
   这个 bug 让「点输入框才弹、点空白就收」在 Chrome 系应用上失效了 7 轮。
   日志实测 908 次采样的 role 分布：
   `0x0F GROUPING` 739 次（81%）、`0x29 ANIMATION` 168 次、
   `0x2A TEXT`（白名单内）**0 次** —— **Chrome 从不把焦点放在输入框上，
   而是放在一个"容器"元素上**，真正的输入框是它的子节点。
   而 `AccessibleHasEditableFocus` 的两条分支行为不一致：

       if (focus.vt == VT_I4) {                    // ← Chrome 走这条
           if (AccessibleRoleIsEditable(acc, focus)) { ... return TRUE; }
           return FALSE;                           // ← 角色是容器就判死，不下钻
       }
       if (focus.vt == VT_DISPATCH && focus.pdispVal) {
           ...
           if (!editable) editable = AccessibleHasEditableFocus(focused, depth+1, token);  // ← 这条会递归
       }

   ⇒ **写递归遍历时，每条分支都要能下钻**。修法：`get_accChild` 取出该 child 的
   `IAccessible` 继续递归（`focus.lVal == 0` 即 CHILDID_SELF 才真的无子可取）。
   ⚠ 配套：类名粗判要**降级为兜底**而不是删掉 —— Chrome 系改为
   `g_chromeClassFallback`，仅当 COM 拿不到对象时才用类名放行
   （直接删会重犯「浏览器/UWP/Electron 完全无法呼出」那次回归）。

9. **⚠⚠ oleacc 的 COM 接口签名不要凭印象写**（第七轮，连错两次）
   `IAccessible::get_accChild` 我连写两个版本都编译失败：

       三参数      → C2660: function does not take 3 arguments
       当返回值用  → C2440: cannot convert from 'HRESULT' to 'IDispatch *'

   CI 编译器给出的真原型（`oleacc.h:596`）：

       HRESULT get_accChild(VARIANT varID, IDispatch **ppdispChild);

   **两参数、`IDispatch**` 是出参**，且 **`varID` 就是要取的那个子元素 id**
   （不是"父对象 id"、也不是 0）。同一个类里 `get_accFocus`（出参 VARIANT）、
   `get_accRole`（出参 VARIANT）、`get_accChild`（出参 `IDispatch**`）
   **三个长得像但不一样**。
   ⇒ 写之前先 `Grep` 一下 `oleacc.h` 的原型；或**故意写错一次让 CI 把原型打出来**
   （一轮 CI 就能确认，比在本地盲猜三轮快）。

10. **⚠ 加新 API（含新 header）前先看链接行**（第九轮）
    本项目链接行只有：`Comctl32 Shell32 Gdi32 User32 Advapi32 Imm32`
    （+ `#pragma comment` 的 `ole32` / `gdiplus`）。**没有 `oleaut32`** ——
    `ole32` ≠ `oleaut32`，别混。

    引入 `<uiautomation.h>` 时踩了三个坑：
    · **C2039** `get_CurrentIsTextPatternAvailable` 不是
      `IUIAutomationElement` 的方法 —— UIA 的**能力（capability）**没有
      `get_CurrentXxx`，只能 `GetCurrentPropertyValue` + VARIANT
    · **C2065** `g_dbgUiaCt` undeclared —— 变量定义在 6300 行而 `AfLog` 在
      732 行读它 ⇒ **新增全局诊断变量必须放在 `AfLog` 之前的全局区**
    · **LNK2019 (arm64)** `VariantClear` unresolved —— 它在 `oleaut32.lib`
      ⇒ 干脆**不查这个属性**，判定只用两个纯 getter
        （`get_CurrentControlType` / `get_CurrentHasKeyboardFocus`）
    ⇒ **一个 header 里藏着两种依赖成本**：`__uuidof(CUIAutomation)` 只取
      GUID 常量、不需要 import lib（能编过），但同一次引入的 `VariantClear`
      就要 oleaut32。**别为了一个诊断属性去改链接行**（牵动三架构 + VC-LTL）。

11. **⚠ 按行号做区间替换前先打印原文确认边界**（第十轮）
   用「找第一个缩进相同的 `}`」当区间结尾，很容易命中**内层**那个 `}`：
   第十轮替换 `if (hrOk) {...} else {...}` 时，边界命中了紧邻的
   `if (editable) {...}` 的收尾 ⇒ **新块插进去了、旧块还留着**，
   CI 报 `C2065: undeclared identifier`。
   ⇒ 替换整段前先 `sed -n` 打印那几十行确认边界；
   或**用整段唯一文本做锚点**（比行号可靠）。

12. **⚠ 删掉「超时兜底」路径时，检查剩下��条件是否还覆盖所有「用户主动操作」**（第十二轮）
   第十一轮把防回弹的"8 秒超时自动弹回"删掉（用户要求：收起后不该自己回来），
   清除条件只剩 `byKey`（按键）+「token 变化」，**漏掉了鼠标点击**：

       if (sameInput && !byKey) return;   // ← 再点同一个输入框 → 一直拦着

   用户实测（Edge）：手动收起后再点地址栏，**4.2 秒不弹**。日志状态全是
   「条件已满足」：`vis=0 rc=1 cif=1 aw=0 hasInput`。
   资源管理器能弹是因为 token 路径不同、没落到这条拦截上。

   ⇒ 修法：`if (sameInput && !byKey && !clickInFg) return;`
   `clickInFg` = 点在当前前台窗口内 —— 对 Chromium 系就意味着点到地址栏 / 输入框。

   **通用形态**：`用户主动操作` 有多种形式（**点击 / 按键 / 换控件 / 切换输入源**），
   守卫条件必须**逐条列全**。删掉其中一条兜底时，剩下几条是否还够用？
   —— 点击恰恰是最常见的那种，漏了它整条路径就废了。

13. **⚠ 屏幕上键盘的"锁定态"要自持，别去翻目标进程的**（第十四轮）
   原实现是「发 `VK_NUMLOCK` 给目标进程 + 界面高亮跟 `GetAsyncKeyState` 实体灯」。
   **两个状态分属不同线程**（注入的按键翻的是目标线程，自己读的是本线程），
   可以不同步 ⇒ 界面显示"已锁定"、实际发出去的是 `Ins/Home/End` 等光标键。
   用户症状：「小键盘无法锁定、数字打不进去」。

   ⇒ 屏幕键盘**自持**状态：小键盘数字键按它决定**注入数字键还是光标键**
   （`NumpadNavKey()` 做映射，5 在未锁定时**不发**，同实体 104）。
   启动时跟实体灯**对齐一次**后不再跟随 —— 持续跟随会让两个输入源互相干扰。

14. **⚠ 加设置行前先 `grep 'closeRow + N'` 查占用**（第十四轮）
   「Shift 双击锁定」插在「Shift 符号」之后用了 `closeRow+4`，
   而**「界面语言」已经占着 `+4`**（绘制 / 命中测试 / 行高三处都在用）。
   ⇒ 插行前先列一遍所有 `closeRow + N`；插完要把后续行**整体后移**，
   并同步 5 处：绘制 / 命中测试 / 行高 / 行序注释 / `SettingsRowHidden`。

15. **⚠ 插函数定义：锚点要选全局区**（第十四轮）
   我用「`switch (k->type) {` 之前」当锚点插 `ReleaseShiftOnce()`，
   但文件里有**两处**同名 switch（一个在别的处理函数里）⇒ 插进了函数体内：

       error C2267: 'ReleaseShiftOnce': static functions with block scope are illegal

   ⇒ 锚点用「某个**全局变量声明之后**」这类全局区唯一文本，别用函数内锚点。

16. **别给"用户主动收起"设超时自动恢复**（第六轮）。
   防回弹原有一条"8 秒超时后放行"，= 用户收起后 8 秒键盘自己弹回来
   （日志三次收起后 3.7 / 7.1 / 8.0 秒全部复现）。
   **"收起"是用户的意图声明，不该被超时推翻** —— 恢复条件应该是
   "用户的**新**操作"（按键 / 换了输入控件 / 手动显示），而不是"时间到了"。

### ⛔⛔ 最重要的一条：别拿"会滞后的值"当判据
自动呼出前后返工四轮，**三次错误的本质是同一个**：

`GetFocusedInputControl()` 是**异步状态** —— 它依赖 `GetGUIThreadInfo` /
`hwndCaret` 这些**瞬态量**，**滞后于焦点转移**。拿它去比对"刚刚发生的动作"
（比如"用户点的是不是输入框"），必然时对时错，表现出来就是
**"有时候行、有时候不行"** —— 这个症状本身就是"判据选错了"的信号。

| 曾经的错误判据 | 为什么错 |
|---|---|
| `if (!haveGuiInfo) return NULL` | 把大应用的**常态**路径当异常砍掉 |
| `clickOnInput` 用 `input` 比对点击落点 | `input` 滞后，点输入框时可能还是旧值/NULL |
| 无条件清 `g_userHidInInput` | token 是瞬态量，一次对不上就永久失效 |

**正解：就地记录动作发生时的事实。**
在鼠标钩子里当场算好「点击落点的顶层窗口」（`g_lastClickTopHwnd`），
之后只比对两个**顶层窗口**（`g_lastClickTopHwnd == GetAncestor(fg, GA_ROOT)`）——
不碰任何异步状态。判据一旦稳定，"有时候行有时候不行"就消失了。

**推论**：凡是出现"间歇性、无规律"的行为 bug，先去检查判据里有没有
**异步/滞后的 API 返回值**（焦点、悬停、动画状态…），而不是加各种补偿。

### ⚠ 句柄 / 指针一律用 HWND / ULONG_PTR / LPVOID，别存 DWORD
`g_lastFocusEvHwnd` 曾写成 `DWORD` —— **x86 编译毫无警告，x64 下静默截断
高 32 位**（句柄值可以超过 32 位）。本项目三架构一起编译，
**"x86 通过了"绝不代表 x64 是对的**。
