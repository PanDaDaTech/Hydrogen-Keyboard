// hk_icons_extra.rs —— HKeyboard 专用图标（Rust 制作工具里没有的）
//
// 为什么单独一个文件
// ------------------
// 图标几何的单一真源是 PanDaPE-Maker 的 crates/panda-app/src/icons.rs，那里有 54 个
// 可复用的图标。但下面这些是**键盘自己的界面才需要**的，塞进制作工具的表里没有意义，
// 所以单独放这里，用与 icons.rs 完全相同的语法（同样的 24 单位网格、同样的 1.9 描边、
// 同样的 round/round 线帽线接、同样的 {FG} 颜色占位符），由 svg_to_gdi.py 合并编译。
//
// 注意：**不要在这里或任何地方放熊猫（Panda）**。Panda 是 PanDaPE-Maker 的品牌标记，
// HKeyboard 的品牌标识是 winres/main.ico（一把灰色键盘），界面里不出现任何熊猫图形。
// svg_to_gdi.py 默认 --exclude Panda，即使真源里还有也会被丢掉。
//
// 这几个不是 Rust 的 enum 分支，只是同样的 `Glyph::Name => "..."` 文本形式，
// 方便复用同一个解析器；不需要真的能编译成 Rust。

Glyph::Keyboard => "<rect x=\"2.4\" y=\"5.6\" width=\"19.2\" height=\"12.8\" rx=\"2.4\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M6.4 9.4h2M11 9.4h2M15.6 9.4h2\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M8 14.6h8\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>",

Glyph::Caret => "<rect x=\"2.6\" y=\"6.4\" width=\"18.8\" height=\"11.2\" rx=\"2.2\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M7.6 9.4v5.2\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M11.6 10.6h6.2M11.6 13.6h4\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>",

Glyph::KeyboardMark => "<rect x=\"1.4\" y=\"4.6\" width=\"21.2\" height=\"14.8\" rx=\"3\" fill=\"{FG}\" stroke=\"none\"/><rect x=\"4\" y=\"7.2\" width=\"3.1\" height=\"2.2\" rx=\".8\" fill=\"{HOLE}\" stroke=\"none\"/><rect x=\"8.3\" y=\"7.2\" width=\"3.1\" height=\"2.2\" rx=\".8\" fill=\"{HOLE}\" stroke=\"none\"/><rect x=\"12.6\" y=\"7.2\" width=\"3.1\" height=\"2.2\" rx=\".8\" fill=\"{HOLE}\" stroke=\"none\"/><rect x=\"16.9\" y=\"7.2\" width=\"3.1\" height=\"2.2\" rx=\".8\" fill=\"{HOLE}\" stroke=\"none\"/><rect x=\"4\" y=\"10.9\" width=\"3.1\" height=\"2.2\" rx=\".8\" fill=\"{HOLE}\" stroke=\"none\"/><rect x=\"8.3\" y=\"10.9\" width=\"3.1\" height=\"2.2\" rx=\".8\" fill=\"{HOLE}\" stroke=\"none\"/><rect x=\"12.6\" y=\"10.9\" width=\"3.1\" height=\"2.2\" rx=\".8\" fill=\"{HOLE}\" stroke=\"none\"/><rect x=\"16.9\" y=\"10.9\" width=\"3.1\" height=\"2.2\" rx=\".8\" fill=\"{HOLE}\" stroke=\"none\"/><rect x=\"4\" y=\"14.6\" width=\"3.1\" height=\"2.2\" rx=\".8\" fill=\"{HOLE}\" stroke=\"none\"/><rect x=\"8.3\" y=\"14.6\" width=\"7.4\" height=\"2.2\" rx=\".8\" fill=\"{HOLE}\" stroke=\"none\"/><rect x=\"16.9\" y=\"14.6\" width=\"3.1\" height=\"2.2\" rx=\".8\" fill=\"{HOLE}\" stroke=\"none\"/>",

Glyph::Shift => "<path d=\"M12 5.2 4.8 11.9h3.9v6.6h6.6v-6.6h3.9L12 5.2Z\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>",

Glyph::Hamburger => "<path d=\"M4.6 7.4h14.8M4.6 12h14.8M4.6 16.6h14.8\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>",

// ---------- 对制作工具图标的键盘化微调 ----------
// 这里只保留**几何本身就不对**的那一个，其余一律照抄 icons.rs。

// 最小化：icons.rs 画在 y=8，而 12 网格的中心是 6 —— 和居中的 Close 并排时明显偏低
// （实机反馈「关闭键下手异常且对不齐」）。同一网格、同一描边重画一版，
// 靠 bodies.update() 的先后覆盖生效（small = {'Minimize','Close'} 按名字定 12 网格）。
//
// ⛔ 不要因为「看着不对」来改 Close / Hamburger 的坐标：它们当时的毛病在 DrawHkIcon
// 把同一个 <path> 的多个子路径连成了一条（缺 StartFigure），去看 SKILL.md §六。
Glyph::Minimize => "<path d=\"M2 6h8\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.3\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>",

// ---------- 键面图标（「按键图标样式」用） ----------
// 小键盘：圆角方框 + 3×3 键点（标题栏「小键盘」按钮用）。
// 刻意不画底部宽键（那是 Keyboard 图标的空间键画法），3×3 点阵才是无歧义的「数字/小键盘」。
// 九个子路径分别写在三条 <path> 里，靠 DrawHkIcon 的 StartFigure 断开。
Glyph::Numpad => "<rect x=\"3.2\" y=\"3.2\" width=\"17.6\" height=\"17.6\" rx=\"3.6\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M6.6 8.2h1.8M11.1 8.2h1.8M15.6 8.2h1.8\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M6.6 12h1.8M11.1 12h1.8M15.6 12h1.8\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M6.6 15.8h1.8M11.1 15.8h1.8M15.6 15.8h1.8\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>",

// 方向键：icons.rs 只有 ArrowUp，没有另外三向。这里用**同一个几何旋转**得到，
// 四个箭头因此逐像素同源（不会出现"上箭头细、下箭头粗"）。rotate(a cx cy) 的
// a 是顺时针角度（SVG 的 y 轴向下）。同时也不要直接复用 icons.rs 的
// Back / Forward —— 那是"返回/前进"（带长尾巴的箭头），与键盘方向键的观感不同。

Glyph::ArrowDown => "<g transform=\"rotate(180 12 12)\"><path d=\"M12 19.4V4.9\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M6.6 10.3 12 4.9l5.4 5.4\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></g>",

Glyph::ArrowLeft => "<g transform=\"rotate(-90 12 12)\"><path d=\"M12 19.4V4.9\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M6.6 10.3 12 4.9l5.4 5.4\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></g>",

Glyph::ArrowRight => "<g transform=\"rotate(90 12 12)\"><path d=\"M12 19.4V4.9\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M6.6 10.3 12 4.9l5.4 5.4\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/></g>",

// 退格：左侧带尖角的五边形 + 内部 ×（Windows/macOS 通用画法）
Glyph::Backspace => "<path d=\"M9.6 6.2h8.6a2.2 2.2 0 0 1 2.2 2.2v7.2a2.2 2.2 0 0 1-2.2 2.2H9.6L4.2 12l5.4-5.8Z\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M12.8 10 15.8 14\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M15.8 10 12.8 14\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>",

// Tab：向右箭头 + 右侧竖挡板（业界标准 ⇥）
Glyph::Tab => "<path d=\"M5 12h9.4\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M10.8 8.2 14.6 12l-3.8 3.8\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M18.2 6.6v10.8\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>",

// CapsLock：方框内的 A。
// 刻意**不做** ⇪（Shift 轮廓 + 下横线）：Caps 与 Shift 在键盘上相邻，
// 20px 下那根 6.8 宽的横线区分不出来；A in box 才是无歧义的。
Glyph::CapsLock => "<rect x=\"3.6\" y=\"3.6\" width=\"16.8\" height=\"16.8\" rx=\"3.2\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M8.8 16.6 12 8l3.2 8.6\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M10.1 13.8h3.8\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.7\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>",

// Enter：↵（向右下折再向左的拐角 + 左向箭头）
Glyph::Enter => "<path d=\"M18.4 5.6v6.1a2.6 2.6 0 0 1-2.6 2.6H6.7\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/><path d=\"M10.7 10.5 6.7 14.3l4 3.8\" fill=\"none\" stroke=\"{FG}\" stroke-width=\"1.9\" stroke-linecap=\"round\" stroke-linejoin=\"round\"/>",
