## 注意
**本次更新修复了多个自动隐藏与字号显示问题，建议所有用户更新。**

## 更新日志
- 增加 Shift 双击锁定选项，连续两次点击 Shift 即切换中英文输入法，设置页可开关；
- 修复 自动隐藏的多个问题：点击输入框不弹出、莫名回弹、收起太慢、手动收起后再点输入框不弹；
- 修复 Chromium 系应用里自动呼出失效的问题，改用 UI Automation 判定输入焦点；
- 修复 小键盘锁定态「时好时坏、无法锁定」的问题，改为单次注入并读回系统真实锁定态；
- 修复 数字区 NumLock 高亮与实际锁定状态不一致的问题；
- 修复 点按数字区按键卡顿的问题，去掉点击时的固定等待，改为按需重绘；
- 优化 键面字号统一到区块级，主区 / 导航区 / 数字区各自成组、各算一档，不再出现同排字号大小不一；
- 优化 全尺寸布局主区字号放大，字母与修饰键在更宽的键帽下自动取更大档位；
- 修复 全尺寸导航区同一排字号不齐的问题，修正列宽计算偏差；
- 修复 设置页界面语言一项文字被裁切的问题；
- 修复 设置页部分开关动画重绘到错误行的问题；
- 其他已更新但未列出的细节完善。

**Full Changelog**: https://github.com/PanDaDaTech/Hydrogen-Keyboard/compare/v2.0_20261004...v2.0_20261005

## 发行产物

- `HKeyboard_x86.exe` —— 32 位，Windows XP 及以上
- `HKeyboard_x64.exe` —— 64 位，Windows 7 及以上
- `HKeyboard_arm64.exe` —— ARM64 原生版，Windows 10 及以上
- `HKeyboard_X86_X64_Arm64.7z` —— 三架构合并包

- 配置保存在 exe 同目录的 `HKeyboard.ini`，本次结构已扩展。如升级后行为异常，删除该文件即可恢复默认值。


## 致谢

- [NB_TouchKeyboard](https://github.com/zwj4031/NB_TouchKeyboard) —— 提供项目源代码参考
- [liangnijian](https://github.com/liangnijian) —— 测试与反馈
- [狼人72105](https://bbs.wuyou.net/home.php?mod=space&uid=738814) —— 一些合理化建议
- [sairen139](https://bbs.wuyou.net/home.php?mod=space&uid=738817) —— Fn 网页层设计参考
- 392179839 —— 网友的热心测试和建议反馈
- [Fuwari](https://github.com/saicaca/fuwari) & [Ethereal](https://github.com/AloneNanNan/Halo-Theme-Ethereal) —— UI 库设计灵感来源
