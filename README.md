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
- **内嵌 MiSans 字体（按需精简）**：键盘与设置界面统一使用内嵌字体，离线 / WinPE 环境无需系统字体即可获得一致外观。
- **智能焦点感应自动呼出（默认开启）**：深入透视 Caret 闪烁光标，精准识别 QQ、微信、Chrome、Notepad、Office 等输入框，点击输入框秒级自动滑出，离焦自动收回。可在右键菜单中勾选“自动呼出”启停，也可用 `-auto` / `-noauto` 命令行参数指定。
- **修饰键智能组合 & 输入法一键切换**：`Shift` / `Ctrl` / `Alt` / `Win` 均可点击锁定后再组合其它按键发送；连续第 2 次点击 `Shift` 可一键切换中英文输入法。
- **深色 / 浅色主题切换**：支持深色、浅色两套主题，支持跟随系统自动切换，也可通过命令行参数强制指定；使用 `-wallpaper` 参数可让高亮按钮颜色跟随系统壁纸自动提取的强调色（默认关闭）。
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
| `-wallpaper` | 高亮按钮颜色跟随系统壁纸自动提取的强调色（默认关闭） |

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

从主界面“菜单”按钮或托盘右键“设置”打开，左侧 Tab（常规 / 主题 / 关于，关于在最底），设置项带圆角面板，**修改即时生效**；配置保存在 exe 同目录 `HKeyboard.ini`（首次启动自动生成、按需写入），窗口大小 / 主题 / 键盘布局也会自动记录并在下次启动恢复：

- **常规**：自动呼出开关；关闭按钮（×）行为——直接退出程序 / 隐藏到系统托盘，可勾选“记住我的选择”持久化（重启后仍生效）；**键盘布局**下拉选择 全尺寸 / 小键盘，并可勾选“顶部显示 F1~F12 键”。
- **主题**：下拉框选择 跟随系统 / 深色主题 / 浅色主题；高亮按钮跟随壁纸强调色（对应 `-theme:system` / `-dark` / `-light` / `-wallpaper`）。
- **关于**：Logo、项目名称、版本号（含 64 位 / 32 位架构），底部项目地址（点击用浏览器打开）。

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