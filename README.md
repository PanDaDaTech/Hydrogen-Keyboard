<div align="center">

<img src="./winres/main.ico" alt="HKeyboard" width="20%" />

# 轻键 Hydrogen Keyboard

[![GitHub Release](https://img.shields.io/github/v/release/PanDaDaTech/Hydrogen-Keyboard?label=%E6%9C%80%E6%96%B0%E7%89%88%E6%9C%AC)](https://github.com/PanDaDaTech/Hydrogen-Keyboard/releases)
[![GitHub last commit](https://img.shields.io/github/last-commit/PanDaDaTech/Hydrogen-Keyboard?label=%E4%B8%8A%E6%AC%A1%E6%8F%90%E4%BA%A4)](https://github.com/PanDaDaTech/Hydrogen-Keyboard/commits)
[![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/PanDaDaTech/Hydrogen-Keyboard/build.yml?label=CI%E6%9E%84%E5%BB%BA)](https://github.com/PanDaDaTech/Hydrogen-Keyboard/actions)
[![License](https://img.shields.io/github/license/PanDaDaTech/Hydrogen-Keyboard?label=%E5%BC%80%E6%BA%90%E8%AE%B8%E5%8F%AF)](https://github.com/PanDaDaTech/Hydrogen-Keyboard/blob/main/LICENSE)
</div>

## 致谢
- [NB_TouchKeyboard](https://github.com/zwj4031/NB_TouchKeyboard)：提供项目源代码参考

## 核心亮点

- **基于 NB_TouchKeyboard 项目增强修改开发，并运用 Github Actions 实现在线构建**
- **零延迟响应**：原生 Win32 GDI 双缓冲绘制，按压 0ms 瞬间直出上屏；退格、删除、空格与方向键支持高频连发 (Auto-Repeat)。
- **4K 高分屏 & 矢量自适应**：原生适配 1080P / 2K / 4K（125%~250% DPI 缩放），支持 8 方向边框自由拖拽拉大拉小，按键与字号全矢量等比放缩。
- **完整 QWERTY 与 Fn 功能层**：提供标准 QWERTY 全键盘布局，点击 `Fn` 后数字行 `1`~`0`、`-`、`=` 可快速切换为 `F1`~`F12`。
- **内嵌 MiSans Medium（单一字重，精简子集 ~145 KB）**：界面里不存在任何字重变化 —— 族里没有 700，请求 FW_BOLD 只会拿到 GDI 的仿真加粗（笔画外扩、字宽 +6~13%），所以层级全部由**字号 + 颜色**承担（18 / 17 / 12.5 / 11 / 10 / 9 六档）。离线 / WinPE 环境无需系统字体即可获得一致外观。
- **全矢量自绘图标**：设置页与键面图标全部由自绘路径绘制（几何与 PanDaPE-Maker 同源），不再依赖系统图标字体或内嵌图标字体；任意尺寸、任意色相都不变形，XP 与 Win11 观感一致。
- **键面固定「图标+文字」**：方向键、退格、Tab、CapsLock、Enter、Shift、菜单等有公认图形的键带矢量图标 + 键名；字母数字、F1~F12、双符号键（`1`/`!`）与各缩写键（Esc / Ctrl / Alt / Fn / PgUp …）保持纯文字。位置不够时先自动降字号、仍放不下才只画图形。
- **智能焦点感应自动呼出（默认开启）**：深入透视 Caret 闪烁光标，精准识别 QQ、微信、Chrome、Notepad、Office 等输入框，点击输入框秒级自动滑出，离焦自动收回。可在右键菜单中勾选“自动呼出”启停，也可用 `-auto` / `-noauto` 命令行参数指定。
- **修饰键智能组合 & 输入法一键切换**：`Shift` / `Ctrl` / `Alt` / `Win` 均可点击锁定后再组合其它按键发送；连续第 2 次点击 `Shift` 可一键切换中英文输入法。
- **单一色相驱动的统一自绘 UI**：面板、键帽、设置页全部自绘（不套用系统视觉样式），配色由**单一色相**驱动——拖动设置页的「主题色相」即可整体换色；深色 / 浅色 / 跟随系统实时切换，也可用命令行参数强制指定；`-wallpaper` 可让色相跟随系统壁纸自动提取的强调色（默认关闭）。
- **兼容微软拼音 / 五笔输入法**：按键通过 `SendInput` 虚拟键码 (VK) + 正确扫描码发送，完整经过 TSF 组合管线，拼音 / 五笔组字无障碍。
- **极致轻量与兼容**：兼容 Windows XP ~ Windows 11，适配 WinPE 维护环境，支持系统托盘常驻与后台静默运行。

## 命令行参数说明

支持以下启动参数，方便集成到 WinPE 启动脚本、第三方 Shell 或快捷方式中：

| 参数 | 含义说明 |
| :--- | :--- |
| `-h` / `-help` / `-?` | 显示命令行参数帮助（仅弹出帮助框，不启动主界面） |
| `-show` | 启动时直接弹出显示键盘 |
| `-hide` / `-min` / `-tray` | 启动后静默隐藏到右下角系统托盘 |
| `-touchonly` | **触摸屏专属模式**（非触摸设备启动自动静默退出，不占用任何内存） |
| `-auto` | 默认开启“点击编辑框自动呼出”功能 |
| `-noauto` | 默认关闭“自动呼出”功能 |
| `-dark` | 强制使用**深色**主题 |
| `-light` | 强制使用**浅色**主题 |
| `-theme:system` | 主题跟随系统自动切换（默认行为） |
| `-wallpaper` | 主题色相跟随系统壁纸自动提取的强调色（默认关闭） |

### 常用启动示例

```bat
:: 1. 触摸屏设备静默自启（驻留托盘，点击输入框自动弹显）
HKeyboard_x64.exe -hide -touchonly

:: 2. 强制浅色主题并直接显示
HKeyboard_x64.exe -light -show

:: 3. 关闭自动呼出并直接显示
HKeyboard_x64.exe -noauto -show
```

## 设置页面

从主界面标题栏的「设置」按钮或托盘右键「设置」打开，顶部 Tab（常规 / 布局 / 主题 / 关于），每个 Tab 是一张卡片、行间用 1px 分隔线，**修改即时生效**；配置保存在 exe 同目录 `HKeyboard.ini`（首次启动自动生成、按需写入），窗口大小 / 主题 / 键盘布局也会自动记录并在下次启动恢复：

- **常规**：自动呼出开关；自动收起；选择关闭方式（直接退出程序 / 隐藏到系统托盘），可勾选“记住我的选择”持久化（重启后仍生效）；顶部功能键行（F1~F12）；Shift 符号显示方式；界面语言。
- **布局**：**键盘布局**分段选择 默认 / 小键盘 / 全尺寸（完整）。三种布局都按**1u 单位网格**排布（键位由 `round(列位 × u)` 得出，所以列永远对齐、行尾永远齐平）：默认布局每行合计 15.5u、左右留白各 0.30u，白色数字 / 字母键**逐行严格等宽**，宽键按真键盘比例（Esc 1u、Backspace 1.5u、Tab 1.5u、Del 1u、Caps 2u、Enter 2.5u、Shift 2.5u/2u、空格 5.5u），↑ 与 ↓ 自动同列；左列 Esc / Tab / Caps / Shift 的名字贴左、右列 Backspace / Del / Enter / Shift 贴右。全尺寸按 ANSI 104（主区 15u + 导航区 3u + 数字区 4u）。Fn 网页布局（把 `! @ # $ % ^ & * ( )` 与 `_ + { } | : " < >` 铺成两行键面，打网址 / 填表单不用先按 Shift，另带 6 个网址后缀键）；标题栏「小键盘」按钮（与「设置」同款：图标 + 文字）—— 默认布局下切到小键盘、在小键盘里切回上次的布局、全尺寸下显示或隐藏右侧数字区（数字区开着时按钮为主色实底）。**全尺寸下这是显隐数字区的唯一入口**（原「Fn + Tab」手势与对应设置行已下线，Tab 始终输入制表符）。「功能键行」「Shift 符号」「Fn 网页布局」三行在全尺寸 / 小键盘布局下自动隐藏。窗口窄于 1090 DIP 时数字区自动收起（键帽不小于 44px 触摸安全线），点按钮会自动把窗口拉宽到能放得下数字区（只加宽不缩窄）。
- **主题**：分段选择 跟随系统 / 深色主题 / 浅色主题；主界面透明度（100% ~ 50%，六档，分层窗口实时生效）；**主题色相**——5 个预设色板 + 色相滑轨 + HEX 输入，改动即时整体换色（对应 `-theme:system` / `-dark` / `-light` / `-wallpaper`）。
- **关于**：与制作工具（PanDa PE）改版后的关于页同一套语法 —— 身份卡（矢量键盘标识 + 项目名称 + 版本串）、「开源许可」章节（许可协议 / MIT / 查看 → LICENSE）。项目地址为整行可点的链接卡，右侧圆钮上是目的地自己的图标，说明位是一句用途而不是网址；章节标题 = 主色圆角条 + 标题。版本串 = **内部版本号_构建日期**（如 `2.0_20261002`，日期为**北京时间**；内部版本号在 `resource.h` 的 `VER_FILEVERSION*`，日期由构建时间换算，可用 `-DHK_BUILD_DATE=L"YYYYMMDD"` 覆盖），另附架构（64 位 / 32 位）；卡片下方为版权信息。

## 编译指南

本项目采用纯 Win32 API 编写，无第三方运行时依赖。

- **编译器**：MSVC (Visual Studio 2019 / 2022)
- **本地编译**：直接运行根目录下的 `build_cpp.bat`，生成 x86 / x64 / arm64 三架构二进制程序及 7z 发布包。
- **ARM64（Windows on ARM）**：`build_cpp.bat` 同时产出 `HKeyboard_arm64.exe`（ARM64 原生版）；CI 在 x64 宿主上交叉编译，真机功能验证需 ARM64 Windows 设备。
- **常规 CI 构建**：`.github/workflows/build.yml` 保持原有推送、拉取请求、标签及手动构建流程。
- **按需发布**：`.github/workflows/release.yml` 仅支持手动触发；填写版本标签后才会构建并创建 GitHub Release，默认创建为草稿，不会替代或自动触发现有 Build 工作流。

### XP 兼容依赖

构建脚本依赖 [YY-Thunks](https://github.com/Chuyu-Team/YY-Thunks)（WinXP API 桩）和 [VC-LTL](https://github.com/Chuyu-Team/VC-LTL)（静态 CRT 链接）以实现 Windows XP 兼容。

- 若本地未找到这两个依赖，`build_cpp.bat` 会**自动从 NuGet 下载**到 `deps/` 目录（YY-Thunks 1.2.2 + VC-LTL 5.3.1）。
- 也可通过环境变量 `TOOLCHAIN_ROOT` 指定自定义路径（如 `set TOOLCHAIN_ROOT=D:\MyTools`），脚本会在 `%TOOLCHAIN_ROOT%\YY-Thunks\` 和 `%TOOLCHAIN_ROOT%\VC-LTL\` 下查找。
- 在 GitHub Actions 构建时会自动从 NuGet 中自动下载依赖，无需手动配置。

## 使用到的项目：
- [NB_TouchKeyboard](https://github.com/zwj4031/NB_TouchKeyboard)
- [YY-Thunks](https://github.com/Chuyu-Team/YY-Thunks)
- [VC-LTL](https://github.com/Chuyu-Team/VC-LTL)