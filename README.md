# 屏幕键盘 (Screen Keyboard)

> 作者：江南一根葱 & PanDaTech

> 专为 Windows 11 / 10 及 WinPE 离线环境打造的**极轻量级、原生 Win32 C++ 触控键盘**。
> 无任何重型 UI 框架依赖，内存占用仅 **~3MB**，二进制文件仅 **~30KB**，支持 4K 高分屏自适应与完整 QWERTY 键盘布局。

---

## 核心亮点 (Key Features)

- **零延迟响应**：原生 Win32 GDI 双缓冲绘制，按压 0ms 瞬间直出上屏；退格、删除、空格与方向键支持高频连发 (Auto-Repeat)。
- **4K 高分屏 & 矢量自适应**：原生适配 1080P / 2K / 4K（125%~250% DPI 缩放），支持 8 方向边框自由拖拽拉大拉小，按键与字号全矢量等比放缩。
- **完整 QWERTY 与 Fn 功能层**：提供常用全键盘布局，点击 `Fn` 后数字行可快速切换为 `F1`~`F12`。
- **智能焦点感应自动呼出**：深入透视 Caret 闪烁光标，精准识别 QQ、微信、Chrome、Notepad、Office 等输入框，点击秒级自动滑出，离焦自动收回。
- **物理键盘对齐 & 输入法一键切换**：严格区分左右 Shift；左 Shift 保留原有修饰键功能，点击右 Shift 向当前输入法发送 `VK_RSHIFT` 脉冲以切换中英文状态。
- **深色 / 浅色主题切换**：支持深色、浅色两套主题，支持跟随系统自动切换，也可通过命令行参数强制指定；使用 `-wallpaper` 参数可让高亮按钮颜色跟随系统壁纸自动提取的强调色（默认关闭）。
- **兼容微软拼音 / 五笔输入法**：按键通过 `SendInput` 虚拟键码 (VK) + 正确扫描码发送，完整经过 TSF 组合管线，拼音/五笔组字无障碍。
- **极致轻量与兼容**：兼容 Windows XP ~ Windows 11，适配 WinPE 维护环境，支持系统托盘常驻与后台静默运行。

---

## 命令行参数说明 (CLI Parameters)

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
UI_TouchKeyboard_x64.exe -hide -touchonly

:: 2. 强制浅色主题并直接显示
UI_TouchKeyboard_x64.exe -light -show

:: 3. 关闭自动呼出并直接显示
UI_TouchKeyboard_x64.exe -noauto -show
```

---

## 键盘快捷技巧

- **中英文一键切换**：点击键盘右侧的 `Shift` 键切换当前输入法的中英文状态；左侧 `Shift` 功能保持不变。
- **Win 快捷键**：第一次点击 `Win` 键会锁定并高亮，随后点击其他按键可发送 `Win+按键`；锁定状态下再次点击 `Win` 会打开开始菜单。
- **F1~F12 功能层**：点击 `Fn` 后，数字行的 `1`~`0`、`-`、`=` 会对应显示并发送 `F1`~`F12`；选择功能键后自动退出 Fn 层。
- **自由放缩**：鼠标或手指按住键盘四周任意边框或角落拖动，即可自由调整键盘大小。

---

## 编译指南 (Build Instructions)

本项目采用纯 Win32 API 编写，无第三方运行时依赖。

- **编译器**：MSVC (Visual Studio 2019 / 2022)
- **本地编译**：直接运行根目录下的 `build_cpp.bat`，生成 x86 / x64 双架构二进制程序及 7z 发布包。
- **常规 CI 构建**：`.github/workflows/build.yml` 保持原有推送、拉取请求、标签及手动构建流程。
- **按需发布**：`.github/workflows/release.yml` 仅支持手动触发；填写版本标签后才会构建并创建 GitHub Release，默认创建为草稿，不会替代或自动触发现有 Build 工作流。

### XP 兼容依赖（自动下载）

构建脚本依赖 [YY-Thunks](https://github.com/Chuyu-Team/YY-Thunks)（WinXP API 桩）和 [VC-LTL](https://github.com/Chuyu-Team/VC-LTL)（静态 CRT 链接）以实现 Windows XP 兼容。

- 若本地未找到这两个依赖，`build_cpp.bat` 会**自动从 NuGet 下载**到 `deps/` 目录（YY-Thunks 1.2.2 + VC-LTL 5.3.1）。
- 也可通过环境变量 `TOOLCHAIN_ROOT` 指定自定义路径（如 `set TOOLCHAIN_ROOT=D:\MyTools`），脚本会在 `%TOOLCHAIN_ROOT%\YY-Thunks\` 和 `%TOOLCHAIN_ROOT%\VC-LTL\` 下查找。
- GitHub Actions 中由 workflow 自动下载，无需手动配置。

---

## 开源协议 (License)

本项目基于 [GPL-3.0-or-later](LICENSE) 开源。
