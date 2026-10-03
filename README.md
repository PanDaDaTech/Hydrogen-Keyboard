<div align="center">

<img src="./winres/main.ico" alt="HKeyboard" width="20%" />

# 轻键 Hydrogen Keyboard

[![GitHub Release](https://img.shields.io/github/v/release/PanDaDaTech/Hydrogen-Keyboard?label=%E6%9C%80%E6%96%B0%E7%89%88%E6%9C%AC)](https://github.com/PanDaDaTech/Hydrogen-Keyboard/releases)
[![GitHub last commit](https://img.shields.io/github/last-commit/PanDaDaTech/Hydrogen-Keyboard?label=%E4%B8%8A%E6%AC%A1%E6%8F%90%E4%BA%A4)](https://github.com/PanDaDaTech/Hydrogen-Keyboard/commits)
[![GitHub Actions Workflow Status](https://img.shields.io/github/actions/workflow/status/PanDaDaTech/Hydrogen-Keyboard/build.yml?label=CI%E6%9E%84%E5%BB%BA)](https://github.com/PanDaDaTech/Hydrogen-Keyboard/actions)
[![License](https://img.shields.io/github/license/PanDaDaTech/Hydrogen-Keyboard?label=%E5%BC%80%E6%BA%90%E8%AE%B8%E5%8F%AF)](https://github.com/PanDaDaTech/Hydrogen-Keyboard/blob/main/LICENSE)
</div>

Windows 屏幕键盘，单文件、无第三方运行时依赖，从 XP 到 11 都能跑。

## 功能

- **点击即上屏**：原生 Win32 GDI 双缓冲绘制，按键响应无明显延迟；退格、删除、空格与方向键支持按住连发。
- **分辨率自适应**：1080P / 2K / 4K 下按 DPI 等比缩放（125% 至 250%），窗口八方向可自由拖拽，键帽与字号同步放大；圆角按系统分支，Windows 11 用系统原生圆角、旧系统自绘。
- **过渡动效**：窗口出现与隐藏走系统原生位移与透明度渐变，主键盘、设置窗与关闭提示窗统一 400ms 渐显渐隐。
- **完整 QWERTY 与 Fn 层**：标准全键盘布局；点 `Fn` 后数字行切换为 F1 至 F12 功能键。
- **字体**：使用 [MiSans Medium](https://hyperos.mi.com/font)。离线与 WinPE 环境不装系统字体也能得到一致外观。
- **矢量图标**：设置页与键面共 67 个图标全部自绘，不依赖图标字体；任意尺寸、任意色相不变形，XP 与 Win11 观感一致。
- **修饰键组合**：`Shift` / `Ctrl` / `Alt` / `Win` 点击锁定后可与其他键组合；连续点两次 `Shift` 切换中英文输入法。
- **自动呼出**（默认开启）：识别输入框的插入光标，点击 QQ、微信、Chrome、记事本、Office 等应用的输入框自动滑出，离焦收回。右键菜单可勾选开关，也可用 `-auto` / `-noauto` 指定。
- **单一色相配色**：面板、键帽、设置页全部自绘，配色由单一色相驱动，拖动「主题色相」即可整体换色。深色 / 浅色 / 跟随系统可实时切换；`-wallpaper` 让色相跟随系统壁纸强调色。
- **兼容微软拼音 / 五笔**：按键以虚拟键码加扫描码经 `SendInput` 发送，走 TSF 组合管线，组字正常。
- **轻量兼容**：单文件，按架构覆盖 Windows XP 及以上（32 位）/ Windows 7 及以上（64 位）/ Windows 10 及以上（ARM64）；适配 WinPE，支持托盘常驻与后台静默运行。

## 命令行参数

可集成到 WinPE 启动脚本、第三方 Shell 或快捷方式中。

| 参数 | 含义 |
| :--- | :--- |
| `-h` / `-help` / `-?` | 显示参数帮助（只弹帮助框，不启动主界面） |
| `-show` | 启动时直接显示键盘 |
| `-hide` / `-min` / `-tray` | 启动后静默隐藏到系统托盘 |
| `-touchonly` | 触摸屏专属模式（非触摸设备启动后静默退出） |
| `-auto` | 开启「点击输入框自动呼出」 |
| `-noauto` | 关闭「自动呼出」 |
| `-dark` | 强制深色主题 |
| `-light` | 强制浅色主题 |
| `-theme:system` | 主题跟随系统（默认） |
| `-wallpaper` | 主题色相跟随系统壁纸强调色（默认关闭） |

### 常用示例

```bat
:: 触摸屏设备静默自启（驻留托盘，点击输入框自动弹出）
HKeyboard_x64.exe -hide -touchonly

:: 强制浅色主题并直接显示
HKeyboard_x64.exe -light -show

:: 关闭自动呼出并直接显示
HKeyboard_x64.exe -noauto -show
```

## 设置页面

从标题栏「设置」按钮或托盘右键菜单打开，四个 Tab：常规 / 布局 / 主题 / 关于。改动即时生效，配置写入 exe 同目录的 `HKeyboard.ini`（删除该文件即可恢复默认），窗口大小、主题与键盘布局下次启动自动恢复。

- **常规**：自动呼出与自动收起开关；关闭方式（直接退出或隐藏到托盘，可勾选记住选择）；顶部功能键行；Shift 符号显示方式（数字键显示双符号，或只在按 Shift 时显示顶部符号）；界面语言（中英文，键盘与设置页同步）。
- **布局**：三档键盘布局（默认 / 小键盘 / 全尺寸）；Fn 网页布局把 `! @ # $ % ^ & * ( )` 和 `_ + { } | : " < >` 铺成两行键面，打网址、填表单不用先按 Shift，另带 6 个网址后缀键。标题栏的「小键盘」按钮用来切换布局、显示或隐藏右侧数字区；窗口变窄时数字区会自动收起。
- **主题**：深色 / 浅色 / 跟随系统；主界面透明度六档（100% 至 50%）；整体配色由单一色相驱动，含 5 个预设色板、色相滑轨与 HEX 输入；高亮颜色可单独自定义（HEX 或色板直选）。
- **关于**：版本串（`内部版本号_构建日期`，日期为北京时间）、架构、开源许可与项目地址。

## 编译指南

纯 Win32 API，无第三方运行时依赖。

- **编译器**：MSVC（Visual Studio 2019 / 2022）
- **本地编译**：运行根目录的 `build_cpp.bat`，产出 x86 / x64 / arm64 三套二进制与 7z 发布包；`debug` 模式会生成 PDB 与 `*_debug.exe`。
- **ARM64**：`build_cpp.bat` 同时产出 `HKeyboard_arm64.exe`；CI 在 x64 宿主上交叉编译，真机验证需要 ARM64 Windows 设备。
- **CI 构建**：`.github/workflows/build.yml` 负责推送、拉取请求、标签与手动构建。
- **发布**：`.github/workflows/release.yml` 仅手动触发，填写版本标签后创建 GitHub Release（默认草稿）。

### XP 兼容依赖

构建依赖 [YY-Thunks](https://github.com/Chuyu-Team/YY-Thunks)（WinXP API 桩）与 [VC-LTL](https://github.com/Chuyu-Team/VC-LTL)（静态 CRT 链接）。

- 本地找不到这两个依赖时，`build_cpp.bat` 会自动从 NuGet 下载到 `deps/`（YY-Thunks 1.2.2、VC-LTL 5.3.1）。
- 可用环境变量 `TOOLCHAIN_ROOT` 指定自定义路径（如 `set TOOLCHAIN_ROOT=D:\MyTools`），脚本会在 `%TOOLCHAIN_ROOT%\YY-Thunks\` 与 `%TOOLCHAIN_ROOT%\VC-LTL\` 下查找。
- GitHub Actions 上同样自动下载，无需配置。

## 致谢

- [NB_TouchKeyboard](https://github.com/zwj4031/NB_TouchKeyboard)：项目源码参考
- [MiSans](https://hyperos.mi.com/font)：界面字体
- [Fuwari](https://github.com/saicaca/fuwari) 与 [Ethereal](https://github.com/AloneNanNan/Halo-Theme-Ethereal)：UI 设计灵感来源
- [liangnijian](https://github.com/liangnijian)：测试与反馈
- [狼人72105](https://bbs.wuyou.net/home.php?mod=space&uid=738814)：部分合理化建议
- [sairen139](https://bbs.wuyou.net/home.php?mod=space&uid=738817)：Fn 网页层设计参考
