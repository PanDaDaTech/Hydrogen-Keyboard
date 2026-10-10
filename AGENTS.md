# AGENTS.md

> 由 .workbuddy/MEMORY.md 合并而来（2026-10-11）。这是 Command Code 的**记忆文件**：每轮自动带进上下文，请随项目演进持续维护。
## 协作偏好

- **技能按需启用，不要每次都开**。用户会明确说「开启 XX 技能」时才加载；像 `de-ai-writing`（去 AI 味 / 提示词精简）这类**修饰型**技能，默认不加载，需要时用户会点名。判断依据：用户说「开启…技能」= 这次任务要用；没说就别自作主张激活。
- **文档类交付物默认用点式编号，不用表格**。参考 ITSK（`https://www.itsk.com/thread/435474`）排版：`### [日期] 版本号` 标题行 + 两位数编号加全角顿号（`01、`）+ 动作前缀（增加 / 更换 / 更新 / 优化 / 修正）+ 每条结尾 `；` + 末条「其他已更新但未列出的细节修正」。用户原话：「我要发布在 GitHub Release 上，不是发布到博客上」—— GitHub Release 正文渲染不出好表格。
- **写文件前先 Read**。用户经常手改我上一轮输出的草稿，直接 Write 会覆盖（曾报 `File has been modified since read`）。

## 个人项目环境

- **Halo 博客**：主题使用 **Ethereal**（仓库 `AloneNanNan/Halo-Theme-Ethereal`，Astro + Thymeleaf，需 `pnpm build:pkg` 打包）；站点 `pandadatech.cn`；友链数据用官方插件 **链接管理 plugin-links**（`PluginLinks`，应用市场 app-hfbQg）。
  - **本机完整工作树在 `C:\Users\29776\WorkBuddy\2026-09-11-23-59-44\theme-work`**（同级的 `halo-theme-ethereal` 目录只 checkout 了 `.git`，是空的，别去那儿找源码）。要抄主题样式就直接读 `theme-work/src/styles/*.css` 与 `src/components/**`。
  - 想复用 Ethereal 的视觉，**抄 `src/styles/variables.css` 的变量表而不是抄色值**：色彩空间是 **oklch**；`--primary: oklch(0.7 0.14 var(--hue))`、`--page-bg: oklch(0.95 0.01 var(--hue))`、`--card-bg: white`、`--btn-regular-bg: oklch(0.95 0.025 var(--hue))`、`--deep-text: oklch(0.25 0.02 var(--hue))`、`--radius-large: 1rem`，默认 `--hue: 250`。**注意几乎所有中性色都挂着 `var(--hue)`**，只有文字色用 `black/white` 的 alpha。
  - 卡片 `.card-base` = **圆角 1rem + bg(card-bg)，无边框无阴影**；层级感来自浅色调底 `--page-bg` 与纯白卡片的反差。列表项 `.btn-card-outline` 才有 `1px solid color-mix(in oklab, var(--primary) 15%, transparent)`。章节标题 = 18px bold + 左侧 4×16px 主色圆角条。
  - 主题支持「页脚 → 自定义链接 → 自定义HTML」注入，但该处是 `th:utext`，**不会二次解析 Thymeleaf**，页脚里想用插件数据只能用客户端接口。
  - plugin-links 有匿名只读接口 `/apis/api.link.halo.run/v1alpha1/links`，无需 Token。
  - **页脚布局关键**：所有自定义HTML项都被塞进同一个 flex 容器（`mt-1.5 flex flex-wrap items-center justify-center gap-x-2 gap-y-1`，外层 `th:remove="tag"`），相邻项之间只有 4px `gap-y-1`；想独占一行必须写 `flex: 0 0 100%`。页脚顺序：版权行 → 自定义链接项（按后台排列顺序）→ 备案 → Powered by。
  - **新版页脚有 classic / card 两种版式**（`footer.styleConfig.style`，缺省 classic；另有正交的 `divider.style` = 虚线/星芒）。classic 的自定义链接容器**居中**、card 的落在**左对齐**的左列里。所以**脚挂件的外层容器千万别写死 `justify-content: center`**——card 下「整行宽 + 内容居中」会把挂件推到列中央，相对左对齐的版权行右移 `(列宽 − 挂件宽)/2`。正确写法：`width: 100%; min-width: 100%; justify-content: inherit;`（一条 inherit 同时适配两版式）。已实测：只改对齐 → 偏移 100.1px→0；只改尺寸不动对齐 → 仍 100.1px，**根因是对齐而非 flex-basis 百分比**。
  - **挂件的「自适应」应分两层回答**：①**版式自适应** = `justify-content: inherit`（主力，不需识别版式）；②**容器自适应** = `@supports selector(:has(*))` 兜底块，用 `footer.card-base` 认版式（新版主题把 `card-base` 写在 `<footer>` 上），内容为 `align-self: stretch` + 给中间层也补 `width/min-width: 100%` + 父列 `align-items: flex-start`。**必须用 `align-self` 而不是 `margin-right: auto`**（后者会被父级 `align-items: center` 抵消），**必须是 `stretch` 而不是 `flex-start`**（真实结构有中间层，收窄的是中间那层）。另 `min-width: 100%` 自带"天然抗性"（会把中间层反过来撑满），所以兜底属锦上添花。
  - **验证挂件对齐要「从线上 dump 原样切出整个 `<footer>`」再替换 `<style>` 做 OLD/NEW 对照**（手搭近似结构会漏 `flex-1` 兄弟节点导致整行塌宽）。线上真实数据：column 390.8px / 徽章 286.8px 时 v2 `dx=52.0px`、v3 `dx=0.0px`；1512/1024/768/480 四视口 v3 恒 0.0px。判线上部署了哪版：在 dump 里 grep `@supports selector(:has(*))` / `justify-content: inherit` / `flex: 0 0 100%`。
  - **新版主题已原生渲染友链**（`FooterFriendLinks`：非 card 版式独立成块、card 版式以 `compact` 嵌在卡片顶行，门控 `footer.friendLinks.enable_friend_links == true`，含 `is_home_only`）。**后台「自定义链接」里若还留着自写的友链 HTML 项，页面会出现两份友链，要删掉那一项。**
  - 写页脚小挂件（徽章/徽标等）时：选择器必须加自己的前缀、不要用全局类名、不要 `!important`、两个并排块用 `inline-flex` 而不是 `inline-block`（后者分两行书写会在中间留约 4px 换行缝隙）。
  - **图标只能用内联 SVG**：Ethereal 不加载 FontAwesome，`<i class="fa fa-*">` 永远不显示；内联 SVG（`stroke="currentColor"` / `fill="currentColor"`）在该站实测正常。
  - **想让自定义组件与主题某处「同款风格」，去引用同一套 CSS 变量，别抄色值**：变量都在 `src/styles/variables.css` 的 `:root`，深色由 `:root.dark`（第 120 行）自动切换，所以引用变量的组件**不需要写任何深色覆盖**。常用对应关系：
    - 侧边栏社交按钮 / 各类按钮底色 → `--btn-regular-bg` + `--btn-content`（`.btn-regular` 就这两个变量）
    - 主色 / 正文 / 卡片 → `--primary` / `--deep-text` / `--card-bg`
  - 后台改「主题色」实际是改 `--hue`，所有引用变量的地方会一起变——这也是「引用而非抄色值」的价值所在。
  - **验证配置是否生效别靠猜**：站点前面有 GoEdge 的 JS 校验（curl 只会拿到 278 字节校验页），但 `msedge --headless=new --virtual-time-budget=12000 --dump-dom <url>` 能过校验并拿到 JS 执行后的真实 DOM；首页 HTML 有缓存可能仍是旧版，**用 `/links` 之类的子页做判断更准**。改完后台务必点设置页底部【保存】再 Ctrl+F5。
- **C++ 屏幕键盘 HKeyboard（Hydrogen-Keyboard）**：仓库 `E:\Project\Git\Hydrogen-Keyboard`（origin `PanDaDaTech/Hydrogen-Keyboard`，main 分支），**单文件 `hkeyboard.cpp` 约 5500 行**（纯 Win32 + GDI/GDI+，自绘 UI，无 MFC/Qt）。习惯改完立刻 push 触发 CI。
  - **本机没有任何 C/C++ 编译器**（`cl` / `link` / `clang` / `g++` 全 NOT_FOUND），**CI 是唯一编译验证途径**。所以推之前必须先跑静态预检 —— 已固化成技能 **`~/.workbuddy/skills/hkeyboard-win32-cpp/`**（含 `check_fwd.py` 先用后定义扫描、`check_undef.py` 未定义标识符扫描；在仓库根目录跑）。
  - **CI 结构是个陷阱**：`build.yml` 是**单 job 内串行三步** x86 → x64 → arm64，第一步失败就 `exit /b`，**后两个架构根本不会跑**。所以「只报 x86 的错」≠ x64/arm64 没问题，"x86 挂了"经常只是因为它排第一。
  - **最高频的 CI 失败类型**：在文件前半段调用了一个定义在文件后半段的 `static` 函数、又忘了加前置声明 → `C3861 identifier not found`。文件头有一块「C++ 函数前置声明」区，新加的跨区调用要往那里补声明。
  - **材质（Mica/亚克力）子系统的删除边界**已评估成文（工作区 `HKeyboard-去除Mica与亚克力-可行性评估.md`）。核心结论：可以删约 370 行，但 **`g_alphaPaintActive` + 32bpp DIB + `DrawMaterialText` 是材质/非材质两种模式共用的文字合成路径，绝不能删**；`GetDwmSetWindowAttribute` 也必须留（Win11 原生圆角靠它）；且去材质必须同时把「主界面透明度」放开到所有系统，否则 Win11/Win10 1809+ 用户既无材质也无透明度。详见技能 `hkeyboard-win32-cpp`。
  - **去材质之后要全面推 Ethereal 风格 + WinXP↔Win11 统一 UI**（默认配色取 PanDaPE-Maker 的 hue=300），方案已成文（工作区 `HKeyboard-Ethereal统一UI方案.md`）。要点：换皮 = 改一张 `ThemeColors` 表（颜色已全部经 `C_*` 宏间接寻址，绘制代码里几乎无色值字面量，只剩 5 处硬编码白/黑待清），**不需要为 XP 开任何渲染分支**。四处**必须偏离 Ethereal / Rust 原配方**（都是实算出来的，不是审美）：键帽轮廓不能用「主色 15%」（浅色下 1.01:1 完全不可见，改 `mix(deep_text,card_bg,0.20)`）、悬停不能只改底色（1.06:1）、按下态不能用 active 底上的 `btn_content`（3.15:1 不达 AA）、Enter 不能用白字（2.80:1）。三个真实 OS 差异：窗口圆角（XP 只能 1-bit 区域裁剪，建议两边都做直角）、系统深色（`AppsUseLightTheme` **XP 上不存在**→恒浅色）、透明度上限（`ApplyWindowOpacity` 的 `!g_supportsMaterial` 要去掉）。**已经统一的部分不要动**：三窗口全 `WS_POPUP` 自绘、文字走灰度 AA 而非 ClearType（XP 与 Win10+ 的 ClearType 行为不一致，灰度反而更一致）、字体全内嵌。详见技能 `hkeyboard-win32-cpp/references/ethereal-ui.md` + `scripts/ethereal_tokens.py`（任意色相生成令牌表 + 对比度校验）。
  - **设置页要与 Rust 制作工具 UI 同步 + 关于页并入的取舍 + 自绘图标取代图标字体**，规范已成文（工作区 `HKeyboard-UI规范-设置页与图标.md`）。要点：① **配色上一轮方案已落地**（`HkbTokens` / `OklchToBgr` / `HkbTokensBuild` 已在 `hkeyboard.cpp` 里，18 个 `C_*` 宏已全部指向 `g_theme->xxx`），所以本次只谈结构/布局/图标；② 与制作工具"风格不同步"的**真正原因不是色值，是行的组织方式**——制作工具是「一 tab 一张卡 + 行间 1px 分隔线」，HKeyboard 是「一行一张卡 + 6px 空隙」；③ 窄窗口必须偏离的一点：制作工具控件**左起**（内容区约 900 DIP），而 HKeyboard 设置窗 700×575/margin 30 ⇒ **内容区只有 640**，必须**保留控件右对齐**，只采纳 tile(30×30 有底圆角方块) + 定宽标签 + 分组卡片语法；④ **行高不再是常数**（下拉 40 vs 开关 26），`SettingsRowRect` 的等距布局要改**累加**并与 `SettingsHitTest` **复用同一份 y 表**；⑤ 行悬停别用 `btn_plain_bg_hover` 档（白卡上 0.05 明度差看不出），用 `C_REGULAR_HOV`；⑥ 内嵌 `winres/segmdl2.ttf` **只有 6,620 字节是子集字体**（加图标得重跑子集化）⇒ 改自绘，`scripts/svg_to_gdi.py` 已能把制作工具 `icons.rs` 的 SVG 编译成 GDI+ 顶点数组（55 图标实测逐对一致），绘制时**描边宽度要乘 k 补偿**、线帽线接要显式设圆头；**⛔ `DrawAlphaText` 不能删**（所有文字的路径）。详见技能 `hkeyboard-win32-cpp/references/settings-and-icons.md`（含制作工具完整 UI 规格表）。
  - **`DrawHkIcon(dc,x,y,size,def,fg,hole)` 的两个反直觉点**（2026-10-02 实测踩坑）：① `size` 是**网格边长**不是可见外框，着色尺寸 = 节点坐标极值 / grid × size（`KeyboardMark` 是 21.2×14.8 / 24 网格 = 88%×62%，传 56 只画 49.5×34.5 DIP）；② **`hole` 色必须与 `fg` 不同** —— 键块是独立子路径，靠 hole 色「挖」出来才可见，同色则挖空整体失效、只剩一个实心方块。验收判据：键块像素占图形比例 > 0。
  - **⚠ 照抄参考实现的教训（用户明确要求「换回老图标方案」）**：我因 PanDaPE 的 `brand_tile` 是「56×56 色块 + 居中 32 DIP 图形」就照搬，但**没问「它那个色块里装的是什么图形」** —— 人家装的是熊猫头（需要底色衬托的独立图形），而 `KeyboardMark` 本身是完整剪影，套色块后变成「蓝方块里一个实心黑方块」。**抄语法不要抄形似**；参考实现某结构成立往往有隐含前提。
- **Rust 桌面端 PanDaPE-Maker**：仓库在 `C:\Users\29776\WorkBuddy\2026-09-12-16-13-00\_lc`（= `PanDaDaTech/PanDaPE-Maker`，Rust + iced 0.13 的 WinPE U 盘制作器）。
  - **`panda-app` 本机编不了**（proc-macro 构建脚本要 `link.exe`），**唯一的编译器是 CI**；`panda-core` 能本地测。所以**能搬的纯函数一律搬进 `panda-core`**——放 app 里等于「每次推送只有一次机会」，几何/算法 bug 本地复现不了。`panda-core` 无第三方依赖，动效数学在 `crates/panda-core/src/motion.rs`。
  - app 侧只能靠三个脚本做静态检查（先 `cd` 到 `2026-09-12-16-13-00`）：`tools/syntax.py`、`tools/typecheck.py`、`tools/unresolved_names.py _lc`。
  - 看构建：`gh run list` 要在 `_lc` 里跑（上级目录不是 git 仓库）；出包后 `tools/install_artifact.py <dir>` 解到 `.workbuddy/scratch/app-run`。
  - 截图探针 `tools/navshot.ps1`（`-Logical` 用 1020×716 设计坐标；`-Targets` 是**同一次会话里连续点击**，所以能先点侧栏再点页内 Tab）、`tools/elevated.ps1`（exe 带 `requireAdministrator` 清单，必须提权）。本机 175% DPI。截图前把 `<exe>/data/config.json` 里的 `hue` 设成想看的主题色。
  - **它是 Ethereal 配色（oklch 令牌）的权威实现**：`crates/panda-core/src/tokens.rs` + `color.rs` 已把 Ethereal 主题的三元组搬成运行时转换表。**任何项目要复用 Ethereal 的视觉，直接读这两个文件，不要另起一套色值。** 关键常量：`DEFAULT_HUE = 300`（紫，非 Ethereal 原版 250）、`PRESET_HUES = [250,210,180,300,30]`、`HUE_MAX = 359`（**不能是 360**，滑轨末端闭区间会被 `rem_euclid` 弹回 0）、`RADIUS_LARGE = 16`。参照值：hue=250 浅色 `primary=#53A3F2`；hue=300 浅色 `primary=#AC89E8`、深色 `#BC98F9`。**注意它自己的 `primary_fg` 是 white（浅色下只有 2.80:1），只够 26px 大标题用**，小字号场景必须换前景色。

## 本机工具链事实

- **本机没有任何 C/C++ 编译器**：`cl` / `link` / `clang` / `g++` 全部 `command -v` 不到，
  `C:\Program Files (x86)\Microsoft Visual Studio` 下只有 `Installer`（没有 IDE）。
  ⇒ **任何 C/C++ 项目在本机都编不了**，验证只能靠 CI 或静态检查脚本。
  （Rust 是唯一例外，见下。）也没有 mingw 与 clang。
  在本机跑 Rust 项目的测试，用 Rust 自带的 `rust-lld` + GNU 自包含库即可，**不需要额外装工具链**：

  ```bash
  TC="C:/Users/29776/.workbuddy/binaries/rust/tc"
  CARGO_TARGET_X86_64_PC_WINDOWS_GNU_LINKER="$TC/lib/rustlib/x86_64-pc-windows-msvc/bin/rust-lld.exe" \
  RUSTFLAGS="-Clink-self-contained=yes" \
  "$TC/bin/cargo.exe" test --target x86_64-pc-windows-gnu
  ```

  先用 `ls "$TC/bin/cargo.exe"` 确认工具链还在（路径可能随 WorkBuddy 版本变化）。
  `--target x86_64-pc-windows-gnu` 让 `cfg(windows)` 代码正常参与编译与测试，
  比用宿主默认目标更贴近真实行为。

- **用 Edge 无头模式自检 HTML 效果图**（做网页/界面 mockup 时先自己截一张确认渲染，别靠猜）：
  `msedge` **不在 PATH**，完整路径 `C:/Program Files (x86)/Microsoft/Edge/Application/msedge.exe`。

  ```bash
  "/c/Program Files (x86)/Microsoft/Edge/Application/msedge.exe" \
    --headless --disable-gpu --hide-scrollbars \
    --virtual-time-budget=2500 --window-size=1300,900 \
    --screenshot=out.png "file:///abs/path/page.html#hash"
  ```

  ⚠️ **2026-09-13 起 Edge 升到 153，`--headless=new` 已失效**（进程 rc=0、不报错，但**静默不产出文件**）。
  用 `--headless`（新版即 new headless）即可。若截图突然全部失败，先怀疑这个。
  另：`--headless=new` 与 `--headless` 混用过一次后，可能连续几次调用都不出图——**每条命令只跑一张截图**最稳。

  ⚠️ **2026-09-13 二次踩坑：整批截图静默失败**——所有参数组合（`--headless` / `=new` / `=old`、有无
  `--virtual-time-budget`）都 rc=0、无 stderr、不生成文件。**两个真凶**：
  1. **`--user-data-dir` 必须每次唯一**——复用同一个目录时，第二次起进程会挂到已存在的实例上直接退出，
     什么都不干（且不报错）。用 `tempfile` 每次生成一个新目录。
  2. **截图文件是在进程退出之后才落盘的**——`subprocess.run` 返回时 `os.path.exists` 可能还是 False，
     **必须轮询等文件出现**（0.5s 一次，最多 ~25s）。
  修好后连拍 3 张全部成功（waited=2.0s）。
  **现成工具：`C:\Users\29776\WorkBuddy\<项目>\.workbuddy\tools\shot.py`**
  （用法 `python shot.py <out.png> <url> [w] [h] [budget]`，已内置唯一 udd + 轮询 + 自动清理）。
  另：**bash 里的 `ls`/`head`/`cat` 时好时坏**，判断文件是否生成请用 `python -c "import os; os.path.exists(...)"`，别用 shell 内建。
  生成后用 Read 直接看图。截完记得删临时 png。页面里若支持读 `location.hash` 切状态，
  就能一个循环批量截出「多个页面 / 深色模式」等多状态图。
  - 截图 URL 带上 `?v=2#hash` 之类查询串可绕开浏览器缓存；**一次循环别超过 6 张**，容易超时被杀。
  - **`--screenshot=` 必须给绝对路径**，相对路径会**静默失败不生成文件**（stderr 里才有
    `Failed to write file …: 系统找不到指定的路径。(0x3)`）；Windows 绝对路径用正斜杠写成
    `C:/Users/.../out.png` 即可正常工作。
  - 页面长于一屏时，先在窗口 CSS 里给固定高度，再用 Python 复制一份把高度改大生成临时副本单独拍长页。
  - **页面 JS 报错会让 hash 路由整段失效、所有截图长得一模一样**（这是个很有用的信号）。
    定位办法：复制一份，在 `<script>` 开头注入
    `window.onerror=function(m,u,l,c){document.title="ERR|"+m+"|L"+l;};`，
    再 `--dump-dom | grep '<title>'` 直接读出错误信息与行号，比猜快得多。
  - **要量布局（间距/尺寸）就写 JS 探针，别靠肉眼看截图**：往临时副本 `</body>` 前塞一段脚本，
    把 `getBoundingClientRect()` + `getComputedStyle()` 的结果拼进 `document.title`，再
    `--dump-dom | grep '<title>'`。判断"间距不对"这类模糊反馈时，一次测量胜过十次猜测。
    注意探针里**不能直接用页面 IIFE 里的 `$`**（作用域外），要自己写 `document.querySelector`；
    并且取"上一个可见兄弟"时**要从头遍历到目标为止、记最后一个高度 > 0 的**，用
    `previousElementSibling` 会撞上 `display:none` 的块或排在后头的元素。
  - **内联 SVG 图标整块不显示 → 优先怀疑 `stroke`**：`fill="none"` 的描边图标若既没有
    `stroke="currentColor"` 属性、外层也没有 `svg{stroke:currentColor}` 的 CSS 规则，就是全透明看不见。
    一劳永逸的兜底是写一条 `svg[fill="none"]{stroke:currentColor}`（子元素显式写了
    `stroke="none"` 的优先级更高，不会被误伤）。

  ⚠️ **`--dump-dom` 读不到结果时的替代法（2026-09-13 踩）**：用 Python `subprocess` 调 msedge 时
  `--dump-dom` 的 stdout 可能完全为空（管道 / 文件重定向都一样）。此时改成：**让探针把结果渲染进页面**
  （`document.body.innerHTML = '<pre>…</pre>'`，并把整段逻辑包 `try{…}catch(e){out.push('ERROR: '+e.message)}`），
  再用 `--screenshot` 截图、Read 看图。图里能一次看到全部测量值，比反复试 dump-dom 快。
  注意探针里不能用页面 IIFE 私有的 `$` / 变量，只能用 `document.*`。

- **bash 的 coreutils 可能整体不可用**（`grep` / `head` / `tail` / `mkdir` / `ls` 报
  `command not found`，只剩 shell 内建与绝对路径的 exe）。这时：文件操作走 Read/Grep/Glob 工具，
  目录创建、复制、检查文件是否落地、输出重定向后的读取都用 Python 一次搞定，
  不要再用 `|` 管道拼命令（管道右侧失败会把整条命令的结论带偏）。

- **不要把大 HTML 传给文件展示工具**：会自动开实时预览面板，用户会觉得卡。
  只传关键截图 + 文档，HTML 路径在文字里说明即可。

- **同一轮里对同一个文件并行发多个 Edit 会互相覆盖**（踩过一次：一批 6 个 Edit 全部回「成功」，
  实际只有最后 1~2 个落盘，导致 HTML 缺 id、JS 取到 null 抛异常）。
  规则：**改同一个文件一次只发一个 Edit，改完立刻 grep 验证**；大批量改动改成 Write 一个
  Python 补丁脚本再执行。注意 Python `s.replace()` 补丁也可能出现「报 MISS 却已生效」「报 ok 却重复插入」，
  脚本跑完必须**扫重复 + 扫缺失**，不能只看它打印了什么。

- **Python 与 Node 一律用托管版本**，不要用系统里那份：
  - Python：`C:/Users/29776/.workbuddy/binaries/python/versions/3.13.12/python.exe`
  - Node：`C:/Users/29776/.workbuddy/binaries/node/versions/22.22.2-2/node.exe`
  - **例外：托管 Python 没装 `PIL`/Pillow**（`ModuleNotFoundError: No module named 'PIL'`）。
    要做**图像处理/像素分析/裁切放大**时直接改用系统那份
    `C:/Users/29776/AppData/Local/Programs/Python/Python312/python.exe`（实测**有 PIL**），
    省掉建 venv 装 Pillow 的步骤。

- **PowerShell 工具在本机可能抓不到 stdout**（`Write-Output "hi"` 也返回空、exit code 0）。
  凡是需要看输出的脚本，**一律让脚本把结果写进文件、再用 Read 读**，
  不要指望工具返回的 stdout；报错同理（`2> err.txt` 后读文件，注意 PS 重定向写出的是 UTF-16，
  Read 会报 "Cannot display content of binary file"，需先 `Get-Content -Encoding Unicode` 转 UTF-8）。

- **PowerShell 命令行里出现 `%` 会被安全策略判成 cmd 变量语法而整条拦截**
  （报 `cmd.exe %VAR% environment variable syntax is not PowerShell syntax`）。
  常见触发：**URL 里中文文件名的百分号编码**（`file:///…/%E4%B8%BB…html`）。
  做法：先把目标文件**复制成纯 ASCII 临时名**（如 `_shot.html`）再 `file:///…/_shot.html`。

- **用浏览器批量截图时，颜色定位比记坐标稳**：想裁出某个组件，先用 Pillow 按「主题主色」
  做连通域找出所有同色方块（`abs(r-0x57)<30 and …`），再按边长筛出 26/56/96px 的磁贴，
  最后 `resize(NEAREST)` 放大 8~10 倍目视校验——比手算坐标可靠，也不受窗口居中/滚动影响。

- **⚠️ 沙箱代理对「大的 git 传输」不稳（2026-09-24 踩，浪费了近 40 分钟）**：全局 `~/.gitconfig` 里
  `http.proxy=127.0.0.1:31180` / `https.proxy=127.0.0.1:31181`。小请求完全正常（`git ls-remote` 1.9 秒返回），
  但 `git fetch` 一超过约 **14 MB 就断**，而且**第一次几乎拉完才断、后续几次一个字节都收不到**；
  特征是 `.git/objects/pack/tmp_pack_*` 停在原地**不再增长**（`git count-objects -v` 会把它们报成 garbage）。
  重试、`--depth`、`http.lowSpeedLimit` 都救不了 —— **别在这上面耗时间**。
  绕法：**找一个已经含全部所需对象的本地仓库当「捐赠者」**，完全不走网络：

  ```bash
  # 1) 先算需要哪些对象，并确认捐赠者一个不缺（--batch-check 不会触发惰性下载）
  git -C <捐赠者> rev-list --objects <新SHA> --not <旧SHA>
  git -C <捐赠者> cat-file --batch-check   # 逐行喂上面的 SHA，确认没有 "missing"
  # 2) 从本地路径 fetch（走 pack 传输，拿到的仍是完整克隆，不是 partial clone）
  git -C <目标> fetch <捐赠者绝对路径> +refs/heads/main:refs/remotes/origin/main
  ```

  事后记得删掉卡死留下的 `tmp_pack_*`（一次就 30 MB+）。
  另：`git stash push -u -m "…" -- <pathspec…>` 在 2.55 上可用，是「清空工作树但不丢东西」的干净办法
  （`-u` 连未跟踪文件一起收，pathspec 之外的文件留在原地）。

- **把网页里的某个组件导出成「精确像素 + 透明背景」的 PNG**（给用户当切图自己改）：
  Edge 无头两条关键参数——`--window-size=W,H` 直接决定 PNG 尺寸；**`--default-background-color=00000000`
  才会保留 alpha**（不加就是白底）。做法：页面里加一个 `?only=<组件>` 模式，JS 把根容器宽高改成组件
  尺寸、强制 `scale(1)` 且偏移 0，CSS 只留目标组件（其余 `display:none`），窗口按组件尺寸开，拍出来就是 1:1。
  **窗口尺寸实测能小到 340×1080、1580×111，不会被夹到最小尺寸。**
  校验尺寸/alpha 不需要 PIL：读 PNG 头 `struct.unpack('>II', b[16:24])` 取宽高，`b[25]` 是 colorType
  （6=RGBA / 2=RGB）——**全不透明的图会被自动编码成 RGB(2)，别误判成「丢了透明通道」**。
  两个必踩的坑：① 导出模式**不要复用页面里「透明模式」的隐藏规则**，否则会把本该入镜的底栏一起藏掉、
  导出空白图；②   导出静态图必须**冻结动画并给动画元素写 inline 高度**，否则频谱柱会停在随机高度、
  迷你频谱缩成一条看着像省略号。现成脚本：`<项目>/.workbuddy/tools/export_png.py`。

## WinXShell 界面（wxsUI）

- WinPE 里那套设置面板／小工具界面**不是 HTML**，是作者的 XML 布局 + Lua 逻辑引擎。
  一个组件 = `wxsUI/<Name>.zip`，内部为 `main.xml` + `parts/page.*.xml` + `parts/page.*.lua` + `themes/` + `locales/`。
- 改这类界面的完整套路（解包 → 改 XML/Lua → 按原压缩参数回填 → 清缓存 → 实机启动截图验证，含现成脚本）见技能 **winxshell-wxsui-edit**。
- **🔴 头号坑：改完没生效 99% 是 `%TEMP%\wxs*.tmp` 解包缓存**。WinXShell 启动组件时把 `<Name>.zip`
  解包到 `%TEMP%\wxsXXXX.tmp\` 并**长期复用**（按 `main.jcfg` 的 `"name"` 认，**zip 变新也不刷新**），
  所以改完 zip、重启 WinXShell 甚至重启电脑都还是旧界面。**必须把 `%TEMP%\wxs*.tmp` 移走**（PE 里不用，%TEMP% 是内存盘）。
  排查：`set WINXSHELL_LOGFILE=…` + `WinXShell_x64.exe -console -ui -jcfg wxsUI\X.zip`，日志里出现
  `Temp\wxsXXXX.tmp\main.lua` 就是读的缓存。
- **本机就能实机验证，别说「没法验证」**：`WinXShell_x64.exe -ui -jcfg wxsUI\UI_Settings.zip -systeminfo`
  直接跑，用 `PrintWindow(hwnd, hdc, 2)` 抓窗口（被遮挡也能抓，窗口类名固定 `WinXShell-<Name>-Wnd`）。
  抓图脚本：技能里的 `scripts/capture_ui.py`（纯 ctypes，不需要 Pillow）。
- 想确认「到底是哪份文件在渲染」，**插入 `text="ZZPROBE1"` 标记再截图**，一步区分「改错文件」与「缓存未刷」。
- 本机副本：`C:\Users\29776\Downloads\WinXShell_RC5.1.6_beta6\WinXShell`（同级 `WinXShell_RC5.1.6_beta6.7z` 是快照，改完要一起同步）。
  PE 里那份 WinXShell 是**独立的另一份副本、往往版本更老**，只改本机这份在 PE 里不生效。
- 另外两个易踩点：① 同一元素允许写多个 `style=`（合并而非覆盖，别当非法 XML 修，标准 XML 解析器会误报）；
  ② 图标必须用 Segoe Fluent Icons 字形，目标 PE 得带字体；③ **XML/Lua 里不要加注释**，改动说明写在回复里。

## 用户的 PE / 虚拟机环境（PanDa PE）

- **PE 工程**：`E:\MakeISO-UEFI-CA2023\`。`boot\` 就是 PE 的文件系统根（`Program Files\WinXShell\…`），
  `MakeISO.bat` 干两件事：`wimlib-bin\wimlib-imagex.exe capture <工程>\boot <工程>\sources_dir\boot\boot.wim
  DriverPE WinPE_Enterprise --compress=lzx:100 --boot --check`，再 `bin\oscdimg.exe … -lWinPE sources_dir <out>.iso`。
  5833 个文件 / 1.7GB，全程约 35 秒。同级还有 `E:\MakeISO-11PE-25H2`（另一套模板，boot 里没有 WinXShell）。
- **PE 里的 wxsUI 是解包目录，不是 zip**（`boot\Program Files\WinXShell\wxsUI\UI_Settings\`）。
  PE 的 `WinXShell.exe` 曾是 2026-03-09 的老版本；已换成 RC5.1.6 beta6 的 `WinXShell_x64.exe`（改名）。
  换 UI 文件必须同版本，否则 `os.info('HWMEM')` 这种新键取到 nil → 整页空白。
- **验证用 VM**：`D:\vmx\Win7x64.vmx`（2GB/2vCPU，guest 分辨率 1024×768）。CD-ROM 挂
  `E:\MakeISO-UEFI-CA2023\DriverPE_Win11_25H2_基础版_v0.5_PanDaTech_CA2023.iso`（`.vmx` 里 `sata0:1.fileName`）。
  **BIOS 引导顺序默认硬盘优先会掉进 Win7**，要 PE 得自己调引导顺序；VM 开着时 ISO 被锁，替换要先关机。
  `vmrun.exe` 在 `D:\Program Files\VMWare\vmrun.exe`（`vmrun -T ws list|start|stop "<vmx>"`）。
- **想在客户机里点击/截图**：枚举 `VMware.GuestWindow`（**子窗口，要用 EnumChildWindows**）取客户机屏幕矩形，
  按 1024×768 换算坐标后用 `SetCursorPos` + `mouse_event` 点击；直接抓该矩形即可得到客户机画面。
  **三个坑**：VM 是 DirectX，`PrintWindow` 抓出来是黑的；`ShowWindow(SW_RESTORE)` 会把最大化的 VM 窗口
  还原成小窗（显示区缩成 320×240）；`SetForegroundWindow`/`SetActiveWindow` 会让 VM 窗口**直接最小化**（坐标 -31599）。
  正确组合：`ShowWindow(SW_MAXIMIZE)` + `SetWindowPos(HWND_TOP, …, SWP_NOACTIVATE)`，现成脚本
  `~/.workbuddy/skills/winxshell-wxsui-edit/scripts/vm_shot.py`。**PowerShell 的 COM/Add-Type 在本机被拦**，走 ctypes。
- **系统信息页的「正确形态」**（用户 2026-09-26 确认）：**3 张卡片**（处理器/内存/存储，**不要显卡卡**）、
  **卡片底部不要彩色条**、图标是**右下角大号淡色**（`Segoe_Fluent_Icons@44` + `#93BAD9` + `valign="bottom"` + `align="right"`）、
  三卡等宽（**不写 width 让其均分，列间距用 `<Control width="8" />` 而不是 padding**）。
  用户把卡片称作「Tab」，「Tab 从 4 变 3」= 卡片去掉显卡那一张。
- 用户很在意「别把测试窗口留在我桌面上」——**每次截完图必须 taskkill**。

## Ethereal 主题 → 触摸键盘（HKeyboard）UI 迁移

规范文件：`C:\Users\29776\WorkBuddy\2026-09-25-19-32-21\Ethereal风格迁移到触摸键盘-规范.md`
（含全部令牌的 **sRGB 换算值**、组件配方、状态机、动效、Win32 落地要点）。

- Ethereal 的色板**只能以 oklch 源码为准**：`theme-work/src/styles/variables.css`（浅色在 `:root`，深色在 `:root.dark` 第 120 行起）。
  要 sRGB 就自己换算，别去网上找"同款配色表"。
- hue=250 实测换算：`primary` 浅 `#53A3F2` / 深 `#63B3FF`；`page-bg` 浅 `#EAEFF5` / 深 `#090E13`；
  `card-bg` 浅 `#FFFFFF` / 深 `#181E24`；键态递进 浅 `#FFFFFF→#F6F9FC→#D0E0F2` / 深 `#181E24→#222F3C→#2D3C4C`；
  强调键（`.btn-regular`）浅 `#E2F0FF`+字 `#3275B4` / 深 `#283747`+字 `#7CB3EB`；15% 主色描边 浅 `#E5F1FD` / 深 `#233445`。
- **两个必须记住的对比度坑**：「实心主色+白字」浅 2.66:1 / 深 2.23:1 **不达标**，强调键要走 `.btn-regular` 的"浅底深字"配方；
  浅色下主色当文字只有 2.30:1，主色只能用于填充/描边/图标。
  另：`:root.dark` **漏了 `--title-active`**，深色下仍是浅色版 `#4F84BA`（≈4.37:1，边缘值）。
- 组件事实标准：卡片圆角 **16px**(`--radius-large`)、小元素 **8px**(`rounded-lg`，全主题 70 处)、
  过渡统一 **150ms `cubic-bezier(0.4,0,0.2,1)`**、**无边框无阴影**（层级只靠 `page-bg` / `card-bg` 明度差，深色下 `shadow-none`）、
  分组标题 = 18px bold + 左侧 `4×16px` 主色圆角条（`before:left-[-16px] h-4 w-1 rounded-md bg-(--primary)`）。
- 磨砂配方：`saturate(180%) blur(20px)` + 浅 `rgba(255,255,255,0.62)` / 深 `rgba(0,0,0,0.65)`。
  Win32 侧优先用 DWM `DWMWA_SYSTEMBACKDROP_TYPE`；常驻键盘面板建议直接不透明，只给弹出层做模糊。
- 动效档位可照搬：`data-speed = relaxed/balanced/snappy` → 280/200/120ms（疾速档"只位移不做透明门控"）。



