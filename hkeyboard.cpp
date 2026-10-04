// hkeyboard.cpp - HKeyboard 轻键 (Pure Win32 C++)
// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <objbase.h>
#include <oleacc.h>
#include <uiautomation.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "resource.h"
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")
#include "hk_icons_generated.h"   // 自绘图标几何（由 svg_to_gdi.py 生成，替代图标字体）

// 当前编译架构（关于页显示用）
#ifdef _M_ARM64
#define HK_ARCH L"arm64"
#elif defined(_M_X64)
#define HK_ARCH L"64位"
#else
#define HK_ARCH L"32位"
#endif

// 主题色相取值范围：上限是 359 而非 360，否则滑轨末端会被取模打回 0
#define HKB_DEFAULT_HUE  300
#define HKB_HUE_MIN      0
#define HKB_HUE_MAX      359

// 界面语言与主题色相（需在 ArchName / ApplyTheme 之前声明，供其读取）
int         g_lang = 0;                    // 语言：0=中文 1=English
int         g_hue = HKB_DEFAULT_HUE;       // 主题色相 0..359（oklch 色相角，单旋钮换肤）

// 关于页架构显示（随语言切换 64/32位或 64/32-bit）
static const wchar_t* ArchName() {
#ifdef _M_ARM64
    return L"arm64";
#elif defined(_M_X64)
    return g_lang ? L"64-bit" : HK_ARCH;
#else
    return g_lang ? L"32-bit" : HK_ARCH;
#endif
}

// ==========================================================================
// 诊断开关
// --------------------------------------------------------------------------
// 定义 HK_DIAG 时才会编译下面那些**排查专用**的代码：
//   -sendtest    注入方式对照实验（裸进程，4 种注入方式）
//   -envtest     在完整环境里只做纯注入，二分定位环境干扰
//   -noscapture  跳过 OnLDown 里的 SetCapture
//   -norepaint   跳过点击后的重绘
//   -diag        记录每次按键注入前后的系统状态到 diag.txt
//
// ⚠ **正式发布版一律不定义这个宏** —— 上面这些参数与代码一行都不会
//   编进 exe，命令行只保留正式参数（-show / -hide / -dark / -help ...）。
//   需要排查时在编译命令里加 /DHK_DIAG 单独构建一份诊断版即可。
//
// 保留它们的原因：issue #3（Win10 微软拼音打不出中文）就是靠这一整套
// 逐层二分才定位到的 —— 先 -sendtest 排除注入代码，再 -envtest 排除运行
// 环境，最后 -diag 记录"注入那一刻的系统状态"才抓到真正的差异
// （鼠标左键按下期间微软拼音拒绝处理注入的按键）。丢了下次还得重写。
// 排查方法见仓库根目录的 ISSUE3_ANALYSIS.md。
// ==========================================================================

// 关于页显示的版本串 = "<VER_FILEVERSION_STR>_<构建日期>"，如 "2.0_20261002"。
//
// ⚠ 只有日期、不带时分 —— 这是**正式版**该有的样子：用户只需要知道
//   "哪一天发布的"。精确到分秒的构建戳只在排查时需要，走 `-sendtest`
//   那条诊断路径输出（见 BuildStampText），不往正式 UI 里塞。
//
// 日期为什么要在源码里做时区换算：
//   __DATE__ / __TIME__ 是**编译器本地时间**，而 CI（GitHub Actions runner）的宿主时区是 UTC，
//   用户在 UTC+8。直接印 __DATE__ 会在「北京时间 00:00~08:00」这段把日期显示成前一天。
//   所以这里把 __DATE__/__TIME__ 当 UTC 处理，+8 小时后再取日期，并处理跨日/跨月/跨年。
//   注意本机没有 C/C++ 编译器，实际构建都在 CI 上。
static const wchar_t* BuildDateBeijing() {
    static wchar_t buf[16];
    if (buf[0]) return buf;                     // 只算一次（静态缓存）

    // __DATE__ 形如 "Oct  2 2026"（日 <10 前面补空格），__TIME__ 形如 "18:26:31"
    static const char mon[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int mm = 1;
    for (int i = 0; i < 12; i++)
        if (mon[i * 3] == __DATE__[0] && mon[i * 3 + 1] == __DATE__[1]
            && mon[i * 3 + 2] == __DATE__[2]) { mm = i + 1; break; }
    int dd = (__DATE__[4] == L' ') ? (__DATE__[5] - '0')
                                   : (__DATE__[4] - '0') * 10 + (__DATE__[5] - '0');
    int yy = (__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100
           + (__DATE__[9] - '0') * 10 + (__DATE__[10] - '0');
    int hh = (__TIME__[0] - '0') * 10 + (__TIME__[1] - '0');
    int mi = (__TIME__[3] - '0') * 10 + (__TIME__[4] - '0');

    dd += (hh * 60 + mi + 8 * 60) / (24 * 60);  // UTC → UTC+8，跨日就进位
    int dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mm == 2 && ((yy % 4 == 0 && yy % 100 != 0) || yy % 400 == 0)) dim[1] = 29;
    if (dd > dim[mm - 1]) { dd -= dim[mm - 1]; if (++mm > 12) { mm = 1; yy++; } }

    swprintf(buf, 16, L"%04d%02d%02d", yy, mm, dd);
    return buf;
}

#ifdef HK_DIAG

// 精确构建时刻（"20261003.2314"），**只给诊断路径用**，不进正式 exe。
// 存在的理由：同一天会为排查构建很多次，只看到 "20261003" 无法分辨
// 手里那个 exe 是哪一次构建的产物 —— 排查时这会白白浪费好几轮。
static const wchar_t* BuildStampText() {
    static wchar_t buf[24];
    if (buf[0]) return buf;

    static const char mon[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    int mm = 1;
    for (int i = 0; i < 12; i++)
        if (mon[i * 3] == __DATE__[0] && mon[i * 3 + 1] == __DATE__[1]
            && mon[i * 3 + 2] == __DATE__[2]) { mm = i + 1; break; }
    int dd = (__DATE__[4] == ' ') ? (__DATE__[5] - '0')
                                  : (__DATE__[4] - '0') * 10 + (__DATE__[5] - '0');
    int yy = (__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100
           + (__DATE__[9] - '0') * 10 + (__DATE__[10] - '0');
    int hh = (__TIME__[0] - '0') * 10 + (__TIME__[1] - '0');
    int mi = (__TIME__[3] - '0') * 10 + (__TIME__[4] - '0');

    int total = hh * 60 + mi + 8 * 60;
    dd += total / (24 * 60);
    int bh = (total / 60) % 24, bm = total % 60;
    int dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mm == 2 && ((yy % 4 == 0 && yy % 100 != 0) || yy % 400 == 0)) dim[1] = 29;
    while (dd > dim[mm - 1]) { dd -= dim[mm - 1]; if (++mm > 12) { mm = 1; yy++; } }

    swprintf(buf, 24, L"%04d%02d%02d.%02d%02d", yy, mm, dd, bh, bm);
    return buf;
}

#endif  // HK_DIAG  —— 正式版里 BuildStampText 整个不存在，也不留空壳

// 日期取值入口。换构建环境（本地编译、或想钉死某个日期）时用
// -DHK_BUILD_DATE=L"20261002" 覆盖即可，不必改上面的换算。
// 摆在函数定义之后：#define 的宏体会引用 BuildDateBeijing()，虽然宏只在调用点展开，
// 但把 #define 放在定义之前会让「先用后定义」的静态检查报 WARN。
#ifndef HK_BUILD_DATE
#define HK_BUILD_DATE BuildDateBeijing()
#endif

#pragma comment(linker,"\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "ole32.lib")

int g_ww = 980, g_wh = 320;
int g_headerH = 36;
int g_keyGap = 4;
int g_keyHeight = 46;

#define KEY_AREA_X   g_keyAreaX
#define KEY_AREA_W   (g_ww - g_keyAreaX * 2)

#define TIMER_FOCUS     8820
#define TIMER_EXIT      8822
#define TIMER_REPEAT    8826
#define TIMER_WINDOW_ANIM 8828
#define TIMER_SETTINGS_ANIM 8827
#define TIMER_WIN_FADE 8831
#define WM_TRAY         (WM_APP + 100)
#define WM_FOCUS_EVENT  (WM_APP + 101)
#define WM_SHOW_KEYBOARD (WM_APP + 102)

#ifdef HK_DIAG
// -envtest 专用：驱动"注入→退出"两个阶段（说明见 EnvTestInject）
#define TIMER_ENVTEST   8833
// -envtest 的运行标志与前向声明。
// ⚠ 前向声明是必需的：WM_TIMER 的处理在主 WndProc 里（文件靠前），
//   而 EnvTestInject 定义在 WinMain 附近（文件靠后）。
BOOL g_envTest = FALSE;
int  g_envTestStage = 0;
static void EnvTestInject();

// ---- 排查开关：二分「点击按键」时究竟是哪个动作破坏了 IME 组字 ----
//
// 已确认的事实（2026-10-04）：
//   · 点击**标题栏**（真实鼠标）→ 无害，IME 正常
//   · 点击**按键**（真实鼠标）  → 微软拼音不组字（出英文）
//   · `-envtest` 里用**注入**的鼠标点击按键 → 正常
//   ⇒ 问题出在「真实鼠标点中按键」才会走到的那几行里。
//
// 而 `OnLDown` 中点标题栏不会执行、点按键才会执行的只有三件事：
//   SetCapture(hWnd) / DoKeyAction(k) / InvalidateRect(...)
// 其中 DoKeyAction → SendKey 已被 -envtest 证明等价于纯注入（能出中文），
// 所以嫌疑集中在**另两个系统级副作用**上。这两个开关用来把它们分开：
//
//   -noscapture  跳过 OnLDown 里的 SetCapture（会改变系统鼠标捕获状态）
//   -norepaint   跳过点击相关的 InvalidateRect（会触发一次全键盘重绘）
BOOL g_noSetCapture = FALSE;
BOOL g_noClickRepaint = FALSE;
#endif  // HK_DIAG

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#ifndef WM_DWMCOLORIZATIONCOLORCHANGED
#define WM_DWMCOLORIZATIONCOLORCHANGED 0x0320
#endif

#define ID_MENU_TOGGLE 10001
#define ID_MENU_AUTO   10002
#define ID_MENU_ABOUT  10008
#define ID_MENU_EXIT   10009
#define ID_MENU_SETTINGS 10010

// 布局 Tab 命中码（S_HIT_TAB0=常规 TAB1=主题 TAB2=关于，视觉顺序：常规/布局/主题/关于）
#define S_HIT_TABL 5
#define S_HIT_AUTOHIDE 110   // 注意：97~102 已被透明度下拉占用

// ========== Ethereal 主题令牌（单一色相驱动） ==========
// 取自 Halo Ethereal：所有中性色都挂在同一个色相角上，换主题＝换一个 --hue；
// 层级感只靠「面板底略深 + 键帽/卡片纯白」的明度差建立，不用描边也不用阴影。

struct HkbTokens {
    DWORD pageBg;            // 窗口 / 键盘面板底（page-bg）
    DWORD cardBg;            // 普通键帽 / 卡片（card-bg）
    DWORD keyOutline;        // 键帽轮廓
    DWORD keyOutlineHover;   // 键帽悬停轮廓
    DWORD btnRegularBg;      // 修饰键底
    DWORD btnRegularBgHover;
    DWORD btnRegularBgActive;
    DWORD btnCardBgHover;    // 普通键悬停底
    DWORD btnPlainBgHover;   // 次要键（收起 / 网址后缀）底
    DWORD btnContent;        // 修饰键文字
    DWORD controlBg;         // 输入框 / 下拉 / 开关轨道底
    DWORD floatPanelBg;      // 弹出层底
    DWORD primary;           // 强调色
    DWORD primaryFg;         // 强调底上的文字
    DWORD lineDivider;       // 弱分隔线
    DWORD metaDivider;       // 次强分隔线
    DWORD text;              // 主文字
    DWORD textMuted;         // 弱化文字
    DWORD titleActive;       // title-active：比主色深一档、比正文淡（关于页章节标题已下线，暂留）
};

// oklch(L, C, h) → sRGB，与 panda-core 的 Tokens::with_hue 结果一致
static double OklchEncode(double x) {
    return x <= 0.0031308 ? 12.92 * x : 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}
static BYTE OklchChannel(double x) {
    if (x < 0.0) x = 0.0; else if (x > 1.0) x = 1.0;
    double v = OklchEncode(x);
    if (v < 0.0) v = 0.0; else if (v > 1.0) v = 1.0;
    return (BYTE)(v * 255.0 + 0.5);
}
static DWORD OklchToBgr(double L, double chroma, double hueDeg) {
    const double PI = 3.14159265358979323846;
    double rad = hueDeg * PI / 180.0;
    double a = chroma * cos(rad);
    double b = chroma * sin(rad);
    double lp = L + 0.3963377774 * a + 0.2158037573 * b;
    double mp = L - 0.1055613458 * a - 0.0638541728 * b;
    double sp = L - 0.0894841775 * a - 1.2914855480 * b;
    double l = lp * lp * lp, m = mp * mp * mp, s = sp * sp * sp;
    double r  =  4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s;
    double g  = -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s;
    double bl = -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s;
    return RGB(OklchChannel(r), OklchChannel(g), OklchChannel(bl));
}

// sRGB → oklch 色相角（迁移旧高亮色 / 取壁纸强调色色相 / HEX 输入用）
static double OklchHueOfBgr(DWORD bgr) {
    const double PI = 3.14159265358979323846;
    double rgb[3] = { GetRValue(bgr) / 255.0, GetGValue(bgr) / 255.0, GetBValue(bgr) / 255.0 };
    for (int i = 0; i < 3; i++)
        rgb[i] = rgb[i] <= 0.04045 ? rgb[i] / 12.92 : pow((rgb[i] + 0.055) / 1.055, 2.4);
    double l = 0.4122214708 * rgb[0] + 0.5363325363 * rgb[1] + 0.0514459929 * rgb[2];
    double m = 0.2119034982 * rgb[0] + 0.6806995451 * rgb[1] + 0.1073969566 * rgb[2];
    double s = 0.0883024619 * rgb[0] + 0.2817188376 * rgb[1] + 0.6299787005 * rgb[2];
    l = pow(l, 1.0 / 3.0); m = pow(m, 1.0 / 3.0); s = pow(s, 1.0 / 3.0);
    double a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s;
    double b = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s;
    double h = atan2(b, a) * 180.0 / PI;
    if (h < 0.0) h += 360.0;
    return h;
}

static DWORD BlendColor(DWORD from, DWORD to, double value) {
    int r = (int)(GetRValue(from) + (GetRValue(to) - GetRValue(from)) * value + 0.5);
    int g = (int)(GetGValue(from) + (GetGValue(to) - GetGValue(from)) * value + 0.5);
    int b = (int)(GetBValue(from) + (GetBValue(to) - GetBValue(from)) * value + 0.5);
    return RGB(r, g, b);
}

// 把黑/白按 alpha 覆盖到实色底上（等价 rgba 覆盖），返回预混实色
static DWORD OverlayBgr(DWORD base, BOOL white, double alpha) {
    return BlendColor(base, white ? 0xFFFFFF : 0x000000, alpha);
}

static Gdiplus::Color GpColorFromBgr(DWORD color) {
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

// 色相预览色：与主色同一配方（oklch 0.70 / 0.14），用于色板与色相滑轨
static DWORD HueAccentBgr(int hue) {
    if (hue < HKB_HUE_MIN) hue = HKB_HUE_MIN;
    if (hue > HKB_HUE_MAX) hue = HKB_HUE_MAX;
    return OklchToBgr(0.70, 0.14, (double)hue);
}

// 由色相派生全部令牌。轮廓与分隔线一律预混成实色：
// GDI+ 往 32bpp BI_RGB DIB 里画半透明形状时 alpha 语义不可靠，实色还能让 XP 与 Win11 字节级一致。
static void HkbTokensBuild(int hue, BOOL dark, HkbTokens* out) {
    double h = (double)hue;
    out->primary   = dark ? OklchToBgr(0.75, 0.140, h) : OklchToBgr(0.70, 0.140, h);
    out->pageBg    = dark ? OklchToBgr(0.16, 0.014, h) : OklchToBgr(0.95, 0.010, h);
    out->cardBg    = dark ? OklchToBgr(0.23, 0.015, h) : RGB(255, 255, 255);
    out->btnRegularBg       = dark ? OklchToBgr(0.33, 0.035, h) : OklchToBgr(0.95, 0.025, h);
    out->btnRegularBgHover  = dark ? OklchToBgr(0.38, 0.040, h) : OklchToBgr(0.90, 0.050, h);
    out->btnRegularBgActive = dark ? OklchToBgr(0.43, 0.045, h) : OklchToBgr(0.85, 0.080, h);
    out->btnCardBgHover     = dark ? OklchToBgr(0.30, 0.030, h) : OklchToBgr(0.98, 0.005, h);
    out->btnPlainBgHover    = dark ? OklchToBgr(0.30, 0.035, h) : OklchToBgr(0.95, 0.025, h);
    out->btnContent         = dark ? OklchToBgr(0.75, 0.100, h) : OklchToBgr(0.55, 0.120, h);
    out->text               = dark ? OklchToBgr(0.85, 0.020, h) : OklchToBgr(0.25, 0.020, h);
    out->floatPanelBg       = dark ? OklchToBgr(0.17, 0.012, h) : RGB(255, 255, 255);

    out->keyOutline      = BlendColor(out->cardBg, out->text, 0.20);
    out->keyOutlineHover = BlendColor(out->cardBg, out->primary, 0.45);
    out->controlBg       = BlendColor(out->cardBg, out->text, 0.18);
    out->lineDivider     = OverlayBgr(out->pageBg, dark, 0.08);
    out->metaDivider     = OverlayBgr(out->pageBg, dark, 0.20);
    out->textMuted       = OverlayBgr(out->pageBg, dark, 0.60);   // t60：black/white @ 60%
    // 章节标题（制作工具的 title-active，oklch 0.60/0.10）：比主色 0.70/0.14 深且淡一档。
    // 18 DIP 的标题压在浅色页底上，直接用主色会偏「按钮字」；深一档才有标题的份量。
    out->titleActive     = dark ? OklchToBgr(0.75, 0.100, h) : OklchToBgr(0.60, 0.100, h);
    // Enter / 确认键：浅色底压深字（5.7:1），深色底用黑 70% 混主色（5.4:1）
    out->primaryFg       = dark ? BlendColor(out->primary, RGB(0, 0, 0), 0.70) : out->text;
}

// Theme mode: 0 = follow system, 1 = force dark, 2 = force light
static int g_themeMode = 0;
// 主界面透明度（%，100=不透明）：分层窗口实现，所有系统均可用
static int g_mainOpacity = 100;
static DWORD g_winBuild = 0;      // 系统Build号（RtlGetVersion，0=未知）
// 是否启用“主题色相跟随系统壁纸强调色”（仅通过 -wallpaper 命令行参数开启，默认关闭）
static BOOL g_wallpaperAccent = FALSE;
static HkbTokens g_themeBuf;
static const HkbTokens* g_theme = &g_themeBuf;

static BOOL IsSystemDarkMode() {
    HKEY hKey;
    DWORD val = 1, sz = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AppsUseLightTheme", NULL, NULL, (LPBYTE)&val, &sz);
        RegCloseKey(hKey);
    }
    return (val == 0);
}

static BOOL IsDarkThemeActive() {
    if (g_themeMode == 1) return TRUE;
    if (g_themeMode == 2) return FALSE;
    return IsSystemDarkMode();
}

// 读取系统 DWM 强调色并转为 GDI COLORREF (BGR)。
// 注册表值为 ABGR (0xAABBGGRR) 布局，注意与 COLORREF (0x00BBGGRR) 的字节序转换。
// 优先级（Win11 实测）：
//   1. HKCU\...\DWM\AccentColor        —— Win11 22H2+ 当前强调色（与 AccentColorMenu 一致）
//   2. HKCU\...\Explorer\Accent\AccentColorMenu —— 资源管理器强调色备用源
//   3. HKCU\...\DWM\ColorizationColor  —— 旧系统回退（可能残留旧主题色）
static DWORD AbgrToBgr(DWORD val) {
    return (((val >> 16) & 0xFF) << 16) | (val & 0xFF00) | (val & 0xFF);
}

// 旧 DWM（Win7/8 与 Win10+ 的 ColorizationColor）为 ARGB (0xAARRGGBB) 布局
static DWORD ArgbToBgr(DWORD val) {
    return ((val & 0xFF) << 16) | (val & 0xFF00) | ((val >> 16) & 0xFF);
}

static DWORD GetWallpaperAccentBgr() {
    HKEY hKey;
    DWORD val = 0, sz = sizeof(val);

    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\DWM",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AccentColor", NULL, NULL, (LPBYTE)&val, &sz);
        RegCloseKey(hKey);
    }
    if ((val & 0xFFFFFF) != 0) return AbgrToBgr(val);

    val = 0; sz = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AccentColorMenu", NULL, NULL, (LPBYTE)&val, &sz);
        RegCloseKey(hKey);
    }
    if ((val & 0xFFFFFF) != 0) return AbgrToBgr(val);

    // 备用：ColorizationColor（Win7 起即存在，ARGB 布局；Win7 无 AccentColor 系列键，壁纸派生强调色由此获得）
    val = 0; sz = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\DWM",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"ColorizationColor", NULL, NULL, (LPBYTE)&val, &sz);
        RegCloseKey(hKey);
    }
    if ((val & 0xFFFFFF) != 0) return ArgbToBgr(val);
    return 0;
}

static void ApplyTheme() {
    int hue = g_hue;
    if (hue < HKB_HUE_MIN) hue = HKB_HUE_MIN;
    if (hue > HKB_HUE_MAX) hue = HKB_HUE_MAX;
    if (g_wallpaperAccent) {
        DWORD accent = GetWallpaperAccentBgr();
        if (accent != 0) {
            hue = (int)(OklchHueOfBgr(accent) + 0.5);
            if (hue > HKB_HUE_MAX) hue = HKB_HUE_MAX;
        }
    }
    HkbTokensBuild(hue, IsDarkThemeActive(), &g_themeBuf);
    g_theme = &g_themeBuf;
}

// 设置 / 关闭提示窗口句柄：主题刷新时需要一并重绘（统一定义在 RefreshThemeAndRepaint 之前）
static HWND g_settingsHwnd = 0;
static HWND g_closePromptHwnd = 0;

// 重新应用主题；颜色确实发生变化时刷新窗口（三个窗口一起，避免只换主键盘留下残留配色）
static void RefreshThemeAndRepaint(HWND hWnd) {
    HkbTokens before = g_themeBuf;
    ApplyTheme();
    if (memcmp(&before, &g_themeBuf, sizeof(HkbTokens)) != 0) {
        InvalidateRect(hWnd, 0, TRUE);
        if (g_settingsHwnd && IsWindow(g_settingsHwnd))
            RedrawWindow(g_settingsHwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
        if (g_closePromptHwnd && IsWindow(g_closePromptHwnd))
            RedrawWindow(g_closePromptHwnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
    }
}

// Convenience macros to access current theme tokens
#define C_BG           (g_theme->pageBg)
#define C_KEY          (g_theme->cardBg)
#define C_KEY_BORDER   (g_theme->keyOutline)
#define C_BORDER_HOVER (g_theme->keyOutlineHover)
#define C_DARK         (g_theme->controlBg)
#define C_HOVER        (g_theme->btnCardBgHover)
#define C_HOT          (g_theme->primary)
#define C_WHITE        (g_theme->text)
#define C_DIM          (g_theme->textMuted)
#define C_ON_PRIMARY   (g_theme->primaryFg)
#define C_BTN_CONTENT  (g_theme->btnContent)
#define C_REGULAR      (g_theme->btnRegularBg)
#define C_REGULAR_HOV  (g_theme->btnRegularBgHover)
#define C_REGULAR_ACT  (g_theme->btnRegularBgActive)
#define C_PLAIN        (g_theme->btnPlainBgHover)
#define C_FLOAT        (g_theme->floatPanelBg)
#define C_LINE_DIV     (g_theme->lineDivider)
#define C_META         (g_theme->metaDivider)
#define C_TITLE        (g_theme->titleActive)

enum KeyType {
    K_NORMAL, K_LETTER, K_MOD, K_CAPS,
    K_SPECIAL, K_ARROW, K_SPACE, K_HIDE, K_DOCK, K_MIN, K_CLOSE,
    // 网页层的符号键：键面固定画**一个**符号（主符号或副符号，由 symShift 决定），
    // 点击直接输出那个字符，不依赖 Shift。数字行那 10 个格子在网页层里要的
    // 就是 ! @ # $ % ^ & * ( )，而普通键必须「先按 Shift 再按数字」，所以单独一类。
    K_SYM
};

// 键面标签的水平对齐：只对「宽键」有意义。
// 默认布局里左列的 Esc / Tab / Caps / Shift 贴左，右列的 Backspace / Del / Enter /
// Shift 贴右 —— 参考图里这些键的名字都贴着各自那一侧的边缘，而不是飘在键的正中。
enum KeyAlign { KA_CENTER = 0, KA_LEFT = 1, KA_RIGHT = 2 };

// symShift：仅 K_SYM 有效 —— TRUE 取键盘布局的副（Shift）符号，FALSE 取主符号。
// 网页层同时要 `[ ] \ ; '`（主符号格）与 `{ } | : "`（副符号格），一个 bool 就能分开。
// align：键面标签的对齐（见 KeyAlign），由布局表给出，绘制时落在 DrawTextKey。
struct KeyDef { int x, y, w, h; short vk; KeyType type; unsigned char symShift; unsigned char align; };

// C++ 函数前置声明
static void ShowKB(BOOL show, BOOL isManual = FALSE);
static void ToggleKB();
static void UserHideKeyboard();        // 临时收起（自动呼出仍然有效）
static void HideToTray();              // 永久隐藏到托盘（单击托盘图标恢复）
// 关闭对话框的两项复用设置页的分段控件（定义在文件后半段，故此处前置声明）
static int  SegmentedItemWAtDpi(const wchar_t* item, double dpi);
static int  CloseSegItems(const wchar_t** out);
static void HandleCloseAction(HWND hWnd);
static void ExitApplicationAnimated();
static void OpenClosePrompt();
static void RecreateFontsAndLayout();
static double GetSystemDpiScale();
static void InitWindowSizeForDpi();
static void SendKey(BYTE vk, BOOL sh, BOOL ct, BOOL al, BOOL win = FALSE);
static HWND GetFocusedInputControl();
static void UpdateAutoVisibility();
static BOOL LoadLayoutWindowRect(RECT* out);
static BOOL LayoutRectOnScreen(const RECT& rc);
// 文字量宽（定义在绘制函数区，RecreateFontsAndLayout 里量「Backspace」要提前声明）
static int MeasureTextW(HDC dc, const wchar_t* s, HFONT f);
static int MeasureTextAdvW(HDC dc, const wchar_t* s, HFONT f);   // 布局宽度（键面标签对齐要用）
// 配置读写（实现位于文件后半段，此处提前声明供键位处理逻辑调用）
static void GetConfigPath(wchar_t* buf, int cch);
static void IniSetInt(const wchar_t* section, const wchar_t* key, int val);
static int  IniGetInt(const wchar_t* section, const wchar_t* key, int def);

// Global state
HINSTANCE   g_hInst = 0;
HWND        g_hWnd = 0;
enum WindowMotionFinish { MOTION_NONE, MOTION_HIDE, MOTION_DESTROY };
struct WindowMotion {
    HWND hWnd;
    int x;
    int fromY;
    int toY;
    UINT duration;
    LONGLONG started;
    int lastY;
    WindowMotionFinish finish;
    BOOL active;
};
static WindowMotion g_mainMotion = {};

// 启动预热期：这段时间内不评估自动呼出，也不做任何跨进程焦点探测。
//
// ⚠ 用户实测：「刚打开的时候，有时候在没输入状态会触发自动呼出的逻辑，
//   之后再呼出键盘就直接卡死。」
//
//   启动瞬间前台窗口可能正处在切换/初始化中 —— GetGUIThreadInfo 拿不到
//   焦点信息、大应用尚未响应、桌面/外壳还没就绪 —— 此时任何判断依据都不可靠，
//   容易在**没有输入框的地方误弹键盘**。而误弹一次就会启动窗口动画，
//   万一那时又撞上跨进程探测阻塞，状态就卡住了。
//   预热期一过自然恢复；手动呼出（点托盘 / -show）不受这里影响。
#define AUTOSHOW_WARMUP_MS 1200
static DWORD g_appStartTick = 0;
static WindowMotion g_settingsMotion = {};
static WindowMotion g_promptMotion = {};

// 高精度毫秒时钟：动画进度改用 QueryPerformanceCounter 计算，
// 避免 GetTickCount() 约 15.6ms 的粗粒度让位移一顿一顿。
static LONGLONG QpcNowMs() {
    static LONGLONG freq = 0;
    if (freq == 0) {
        LARGE_INTEGER f;
        QueryPerformanceFrequency(&f);
        freq = f.QuadPart;
    }
    LARGE_INTEGER c;
    QueryPerformanceCounter(&c);
    return (LONGLONG)((c.QuadPart * 1000) / freq);
}
static BOOL g_exiting = FALSE;
HICON       g_hTrayIcon = 0;
BOOL        g_vis = FALSE;
BOOL        g_manualShow = FALSE;
BOOL        g_manualHide = FALSE;      // 用户显式收起（×隐藏到托盘）后不自动弹出，直到手动重新显示
ULONG_PTR   g_detectedInputToken = 0;   // 最近一次输入焦点识别结果
int         g_hideDelayMs = 300;       // 失焦后的自动隐藏延迟（用户实测 1 秒太慢，收到 300ms）
DWORD       g_lastNonInput = 0;        // 最近一次离焦时刻（自动隐藏延迟用）

// 语言切换：g_lang=0 简体中文，1 English；返回当前语言对应的文案
static const wchar_t* T(const wchar_t* zh, const wchar_t* en) { return g_lang ? en : zh; }
BOOL        g_sh = FALSE, g_ct = FALSE, g_al = FALSE, g_cp = FALSE;
BOOL        g_winKey = FALSE;
int         g_winCount = 0;           // Win 键状态：0=空闲 1=锁定（等待 Win+组合键）
DWORD       g_lastWinTick = 0;        // 最近一次 Win 键点击时刻（状态超时复位用）
HHOOK       g_kbHook = 0;             // 实体键盘低级钩子（监控 Win/Shift/Caps 状态同步显示）
HHOOK       g_mouseHook = 0;          // 全局鼠标低级钩子（只记录"用户点了哪里"）

// ===== 自动呼出的触发条件（2026-10-04 用户要求重做）=====
// 用户要的是「**点击输入框**才弹」，而不是「切到有输入框的窗口就弹」——
// 原话：「有时候我切换窗口的目的又不是为了呼出键盘，而是查看其他内容」。
//
// 仅靠 EVENT_OBJECT_FOCUS 区分不出来：切窗口时新窗口的控件同样会获得焦点、
// 发出完全相同的事件。所以引入"用户操作"作为必要条件：
//   · 鼠标点击 —— 还要求**落点在输入控件所属的窗口内**。否则点任务栏、
//     点别的窗口切过去，本身也是一次点击，照样会被算成"操作"而误弹。
//   · 键盘操作 —— 不校验落点，直接放行（用户明确要求"键盘聚焦到输入框
//     （Tab / Ctrl+F / 自动聚焦）时也要弹"）。
#define AUTOSHOW_INPUT_WINDOW_MS 1500
// 失焦"防抖"窗口：焦点探测会**爆发式**失败（用户日志实测：80ms 内连续 6 条
// noInput，fg 为空 —— GetForegroundWindow 瞬时返回 NULL）。失焦必须**持续**
// 超过这个时长才算真的失焦，否则会把 g_userHidInInput（手动收起标记）误清，
// 用户看到的是「手动收起后过几秒键盘自己弹回来」。
#define AUTOSHOW_INPUT_GRACE_MS 500
static DWORD g_lastClickTick = 0;      // 最后一次真实鼠标左键点击
static POINT g_lastClickPt = {0, 0};   // 它的屏幕坐标
static DWORD g_lastKeyTick = 0;        // 最后一次真实键盘按键

// 前台窗口的变化时刻 —— 用来区分"切换窗口"与"窗口内的焦点转移"。
// ⚠ 用户要的是「点输入框 / 键盘聚焦 / **网页自动聚焦搜索框**」都弹，
//   唯独「仅切换窗口」不弹。而后两者在焦点事件上**完全一样**：
//   都是"某个控件获得了焦点"。只能靠"前台窗口刚刚变过没有"来区分 ——
//   变了就要求有用户操作，没变（焦点在同一个窗口内部转移）就放行。
// ⚠ 用**状态**而不是时间窗：切换窗口后进入"等待用户操作"状态，
//   **只有"点输入框"或"敲键盘"才解除**。
//   一开始用时间窗（比如 1.5s 内要求有操作），但那样切窗口 1.5 秒后封锁就过期，
//   键盘照样会自己弹出来 —— 等于没解决问题。
static HWND g_lastFg = NULL;
static BOOL g_fgAwaitUserInput = FALSE;
// 最近一次点击**落点所属的顶层窗口**。在鼠标钩子里当场算好 ——
// ⚠ 不要等到 UpdateAutoVisibility 里再 WindowFromPoint：那时窗口可能已经变了，
//   而且判断"点的是不是输入控件"若依赖 GetFocusedInputControl 的返回值，
//   会因为**焦点转移滞后**而误判（用户实测的「点输入框反而被收掉/不弹」）。
static HWND  g_lastClickTopHwnd = NULL;
// 连续"没有输入焦点"的评估次数。焦点探测（GetGUIThreadInfo / hwndCaret）
// 会间歇性返回 NULL，直接据此隐藏会让键盘闪烁（用户看到的"莫名其妙回弹"）。
// 连续几次都拿不到才认为真的失焦。
static int   g_noInputStreak = 0;
// 失焦开始的时刻（首次越过"连续确认"门槛时记录，输入焦点恢复时清零）。
// 与 AUTOSHOW_INPUT_GRACE_MS 配合，滤掉爆发式抖动 —— 见其定义处说明。
static DWORD g_noInputSinceTick = 0;
BOOL        g_physShift = FALSE;      // 实体 Shift 是否按住（仅显示同步，不影响虚拟键逻辑）
BOOL        g_physWin = FALSE;        // 实体 Win 是否按住（仅显示同步）
// 实体 NumLock 锁定态（GetAsyncKeyState(VK_NUMLOCK) 的 bit0），全尺寸数字区的 Num 键跟着它高亮
BOOL        g_physNum = FALSE;
BOOL        g_physFn = FALSE;         // 预留接口：Fn 实体键状态（多数键盘不产生按键事件，后续按需扩展）
BOOL        g_af = TRUE;
// 自动隐藏：**点击输入框以外的区域时自动收起键盘**（ini: General/AutoHide）。
// ⚠ 2026-10-04 语义变更：这个开关原来控制的是"收起后在同一输入框内不自动回弹"
//   （防回弹），名不副实 —— 用户实测「键盘不会自动隐藏，只能手动最小化」。
//   现在它管的是**真正的自动隐藏**；防回弹改为**无条件生效**（见 UserHideKeyboard
//   与 UpdateAutoVisibility 里的说明：它是必要条件，没有它手动收起会立刻被弹回）。
//   ini 键名沿用 AutoHide，老配置继续可用。
BOOL        g_afAutoHide = TRUE;
static BOOL g_userHidInInput = FALSE;  // 用户刚在输入状态下手动收起（自动收起开启时不回弹）
static ULONG_PTR g_hiddenInputToken = 0; // 手动收起时所在的输入控件标识
static DWORD     g_userHidInTick = 0;    // 手动收起的时刻（防回弹的超时兜底用）

// ⚠⚠ 临时诊断（定位自动呼出问题用，**定位完即删**）：
//   带 -afdiag 启动时把每次判断的关键量和 ShowKB 的调用来源追加到 afdiag.txt。
//   不传该参数时 g_afLogPath 为空，除一次判空外零开销。
// ⚠ 用**路径**而不是常驻 FILE*：日志改用"每次追加打开、写完即关"。
//   原因：原来用 `L"w"` 常驻打开，只要有一次重复启动（脚本杀进程的间隙、
//   手动又点了一次 exe），新实例就会**把已有内容整个截断** ——
//   表现就是 afdiag.txt 只剩一个 BOM、一行都没有。
//   改成追加后，多实例、中途被杀都不会丢内容。
static wchar_t g_afLogPath[MAX_PATH] = {0};
static int     g_afLogLines = 0;      // 已写行数（够多就停，别把磁盘写爆）
#define AFLOG_MAX_LINES 4000

static const char* AfClsName(HWND h) {
    static char buf[80];
    buf[0] = 0;
    if (h) GetClassNameA(h, buf, 80);
    return buf;
}

// 诊断：GetFocusedInputControl 的中间结果（每次调用刷新，AfLog 时读出）
static char     g_dbgFocusCls[80] = {0};
static int      g_dbgHaveGui = -1;
static int      g_dbgIsInput = -1;
static unsigned g_dbgFlags = 0;
static HWND     g_dbgCaret = NULL;
// accessibility 探测的三段结果：
//   g_dbgResp    响应性预检是否通过（0 = 被 IsWindowResponsive 挡掉）
//   g_dbgAcc     AccessibleObjectFromWindow 的 HRESULT（0 = S_OK）
//   g_dbgAccFound 可编辑焦点是否命中
static int g_dbgResp = -1;
static int g_dbgAcc = 0;
static int g_dbgAccFound = -1;
// 类名快速路径的**补查**（诊断专用）：类名命中后（QQ / 浏览器 / Electron 走这条路）
// 再补一次 accessibility 查询，验证"a11y 能否分辨『焦点在输入框』与『焦点在
// 空白处』" —— 这两者光看窗口类名分辨不出来，而它正是"点空白自动隐藏"的关键。
//   g_dbgExtraMs       补查耗时 ms（-1 = 本 tick 没跑）
//   g_dbgExtraOk       补查结果（-1 = 没跑 / 0 = 未找到可编辑焦点 / 1 = 找到）
//   g_dbgExtraDue      本 tick 是否安排补查（UpdateAutoVisibility 在用户操作窗口内置位）
//   g_dbgExtraLastTick 上次补查时刻（降频用；跨进程 COM 有成本）
static int      g_dbgExtraMs = -1;
static int      g_dbgExtraOk = -1;
static BOOL     g_dbgExtraDue = FALSE;
static DWORD    g_dbgExtraLastTick = 0;
// a11y 焦点诊断：get_accFocus 返回的 VARIANT 类型与最后一个判定过的 role。
// ⚠ 决定"QQ / 浏览器能不能用 a11y 精判"的关键数据：
//   fvt = 0(VT_EMPTY)   → a11y 没给出焦点信息（未启用 / 无焦点）
//   fvt = 3(VT_I4)      → 焦点是子元素 ID（child id）
//   fvt = 9(VT_DISPATCH)→ 焦点是对象，接着看 role 是否在可编辑白名单里
//   frole               → 实际拿到的 role（白名单目前只有 0x2A / 0x34）
static int      g_dbgFocusVt = -1;
static LONG     g_dbgFocusRole = -1;
// 诊断：最近一次 UIA 探测的结果（UiaHasTextFocus 写，AfLog 读 —— 必须定义在此之前）
static int      g_dbgUiaHr = -999;   // GetFocusedElement 的 HRESULT
static int      g_dbgUiaCt = -1;     // ControlType（50004=Edit 50030=Document）
static int      g_dbgUiaKf = -1;     // CurrentHasKeyboardFocus
static int      g_dbgUiaTp = -1;     // IsTextPatternAvailable 属性
// 「该前台窗口（Chrome/Electron 系）是否**没有可用的 a11y 通道**」——
// 由 GetFocusedInputControl 在每次探测后更新，驱动 IsInputControl 里
// Chrome_WidgetWin 的类名兜底是否生效。详见该函数内的说明。
BOOL            g_chromeClassFallback = TRUE;
// 诊断：最近一次 EVENT_OBJECT_FOCUS 的时刻与来源（WinEventProc 更新，AfLog 读出）。
// ⚠ 要验证的假设：「点输入框」会触发 FOCUS 事件、「点空白处」不触发 ——
//   如果成立，这就是区分两者的天然信号（比轮询 a11y 更可靠）。
static DWORD    g_lastFocusEvTick = 0;
static HWND     g_lastFocusEvHwnd = NULL;   // ⚠ 必须是 HWND（x64 下 64 位，DWORD 会截断）
// 键盘窗口的**实际**可见性与矩形。
// ⚠ 这是用来验证「程序以为显示着、用户却看不到」这个猜想的：
//   `g_vis` 只是软件标志位，和 `IsWindowVisible` 可能不一致。
static int g_dbgVisWnd = -1;
static int g_dbgRc[4] = {0, 0, 0, 0};
// 诊断：ShowKB 的调用来源（调用点自己设，ShowKB 里读出）。
// ⚠ 用户日志里 SHOW/HIDE 成对交替、且都是 isManual=TRUE —— 最像
//   「有东西在反复启动第二个实例，触发了 WM_SHOW_KEYBOARD 转发」。
//   这个标记能直接指出是谁调的。
static const char* g_dbgShowFrom = "(startup)";

// ⚠⚠ 用 Win32 API（CreateFileW / WriteFile）写日志，**不用 CRT 流**。
//   原因：`_wfopen_s(..., L"a, ccs=UTF-8")` 在这里无论如何都写不出内容 ——
//   文件被创建（BOM 在），但 fprintf 一个字都没落盘，实测三次都是空文件。
//   与其继续跟 CRT 的 `ccs` 转码较劲，直接拿内核 API 追加，行为完全可控。
//   日志内容全是 ASCII，也不需要任何编码转换。
static void AfWriteRaw(const char* line) {
    if (!g_afLogPath[0] || g_afLogLines >= AFLOG_MAX_LINES) return;
    HANDLE h = CreateFileW(g_afLogPath, FILE_APPEND_DATA,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0;
    WriteFile(h, line, (DWORD)strlen(line), &wr, NULL);
    CloseHandle(h);
    g_afLogLines++;
}

static void AfLog(const char* tag, HWND fg, HWND fgTop, HWND input,
                  BOOL recentClick, BOOL clickInFg, BOOL byKey,
                  BOOL await, BOOL vis, BOOL motion, const char* decision) {
    if (!g_afLogPath[0] || g_afLogLines >= AFLOG_MAX_LINES) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    DWORD fevNow = g_lastFocusEvTick ? (GetTickCount() - g_lastFocusEvTick) : 0xFFFFFFFFu;
    // feh：最近一次 FOCUS 事件的来源窗口是否**就是当前前台顶层窗口**
    // （避免把"其它应用弹窗抢焦点"的噪音误当成"用户聚焦了输入框"）
    int fehMatch = (g_lastFocusEvHwnd && fgTop && g_lastFocusEvHwnd == fgTop) ? 1 : 0;
    char buf[512];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "%02d:%02d:%02d.%03d %-7s fg=%-22s fgTop=%p click=%p "
                "rc=%d cif=%d key=%d await=%d input=%p vis=%d mot=%d | "
                "gui=%d focusCls=%-22s isInput=%d caret=%p "
                "resp=%d hr=0x%X found=%d | xms=%d xok=%d | fvt=%d frole=0x%X fev=%u feh=%d | uct=%d ukf=%d utp=%d | "
                "visWnd=%d rect=%d,%d %dx%d => %s\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, tag,
                AfClsName(fg), (void*)fgTop, (void*)g_lastClickTopHwnd,
                (int)recentClick, (int)clickInFg,
                (int)byKey, (int)await, (void*)input, (int)vis, (int)motion,
                g_dbgHaveGui, g_dbgFocusCls, g_dbgIsInput,
                (void*)g_dbgCaret, g_dbgResp, (unsigned)g_dbgAcc, g_dbgAccFound,
                g_dbgExtraMs, g_dbgExtraOk,
                g_dbgFocusVt, (unsigned)g_dbgFocusRole, (unsigned)fevNow, fehMatch,
                g_dbgUiaCt, g_dbgUiaKf, g_dbgUiaTp,
                g_dbgVisWnd, g_dbgRc[0], g_dbgRc[1], g_dbgRc[2], g_dbgRc[3],
                decision);
    AfWriteRaw(buf);
}

// 直接写一行（不带那些字段，用于阶段标记）
static void AfNote(const char* text) {
    if (!g_afLogPath[0] || g_afLogLines >= AFLOG_MAX_LINES) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    char buf[256];
    _snprintf_s(buf, sizeof(buf), _TRUNCATE, "%02d:%02d:%02d.%03d === %s\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, text);
    AfWriteRaw(buf);
}
BOOL        g_closeToTray = FALSE;     // × 关闭行为：TRUE=隐藏到托盘，FALSE=直接退出（默认直接退出）
BOOL        g_rememberClose = FALSE;   // 记住“× 关闭行为”的选择（持久化到注册表）
int         g_layoutMode = 0;          // 键盘布局：0=默认 1=小键盘 2=全尺寸（完整）
int         g_prevLayout = 0;          // 123 按钮切到小键盘前的布局（会话内记忆）
BOOL        g_showNumBtn = TRUE;       // 标题栏是否显示 123 小键盘切换按钮
BOOL        g_npHidden = FALSE;        // 完整布局：数字区隐藏（标题栏按钮，持久化）
BOOL        g_npHiddenAuto = FALSE;    // 完整布局：窗口过窄时自动隐藏数字区（不持久化）
BOOL        g_fnWebLayout = FALSE;     // 按 Fn 切换到上网常用布局（否则为数字行 F1~F12 层）
BOOL        g_showFKeys = FALSE;       // 顶部显示 F1~F12 键
BOOL        g_shiftSymbols = TRUE;     // 按 Shift 时显示特殊符号（否则显示数字）
int         g_keyIconStyle = 2;        // 键面始终「图标+文字」；「仅文字」模式已按实机反馈下线
DWORD       g_lht = 0;
int         g_hk = -1, g_pk = -1;
static int  g_hdrHov = -1;            // 标题栏按钮悬停（HDR_*，-1=无）
int         g_repeatKeyIdx = -1;
BOOL        g_tracking = FALSE;
BOOL        g_tray = FALSE;
HWINEVENTHOOK g_winHook = 0;
HWINEVENTHOOK g_fgHook = 0;
HANDLE      g_mutex = 0;
HFONT       g_f12 = 0, g_f13 = 0, g_f14 = 0;   // 键面字体（单字重，见字体显示方案 v6）
HFONT       g_f10 = 0, g_f9  = 0;              // 键面字体「深降档」：只给 1u 键塞不下的长标签用（见 FitKeyFont）
HFONT       g_f8 = 0, g_f7 = 0, g_f6 = 0;      // 更深的兜底档：极端高宽比下的 1u 窄键（见 FitKeyFont）
int         g_bkspTextW = 0;                   // 「Backspace」在 g_f12 档下的实测容纳宽（退格标签判据）
static HFONT g_sfBig = 0, g_sfRow = 0, g_sfCtrl = 0, g_sfBase = 0, g_sfMeta = 0;   // 设置/关闭窗口固定字号字体
static HFONT g_sfSec = 0;                                                          // 14 档大字号（关于页产品名）
static Gdiplus::PrivateFontCollection* g_gdipFonts = NULL;   // 内嵌字体的 GDI+ 视角（GDI 注册的字 GDI+ 看不见）
static HANDLE g_fontReg = 0;
static BOOL   g_fontReady = FALSE;     // 内嵌字体注册成功（失败回退系统字体）
NOTIFYICONDATAW g_nid;

// ===== GDI+ 平滑绘图（抗锯齿圆形，避免 GDI Ellipse 锯齿） =====
static ULONG_PTR g_gdiplusToken = 0;
static void InitGdiPlus() {
    Gdiplus::GdiplusStartupInput in;
    Gdiplus::GdiplusStartup(&g_gdiplusToken, &in, NULL);
}
static void ShutdownGdiPlus() {
    if (g_gdiplusToken) Gdiplus::GdiplusShutdown(g_gdiplusToken);
}
static void DrawCircleAA(HDC dc, int x, int y, int r, DWORD fill) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::SolidBrush br(Gdiplus::Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
    g.FillEllipse(&br, (Gdiplus::REAL)(x - r), (Gdiplus::REAL)(y - r),
                  (Gdiplus::REAL)(r * 2), (Gdiplus::REAL)(r * 2));
}

// Fn 功能键层：TRUE 时数字行显示为 F1~F12（或按设置切换到上网布局）
BOOL        g_fnLayer = FALSE;

#define MAX_KEYS 160
KeyDef g_keys[MAX_KEYS];
int g_nk = 0;
int g_keyAreaX = 6;               // 键区左右边距（随 DPI，圆角窗口防裁切）
UINT g_taskbarCreatedMsg = 0;     // TaskbarCreated：任务栏重建后恢复托盘图标

static double GetSystemDpiScale() {
    HDC hdc = GetDC(NULL);
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    if (dpiY < 96) dpiY = 96;
    return (double)dpiY / 96.0;
}

static void InitWindowSizeForDpi() {
    double dpiScale = GetSystemDpiScale();
    if (g_layoutMode == 1) {        // 小键盘：紧凑尺寸
        g_ww = (int)(430 * dpiScale);
        g_wh = (int)(320 * dpiScale);
    } else if (g_layoutMode == 2) { // 全尺寸（完整键盘）：6 行，主区+导航区+数字区
        // 1280×404：宽高比接近常见全尺寸板，键帽不会被横向拉扁
        g_ww = (int)(1280 * dpiScale);
        g_wh = (int)(404 * dpiScale);
    } else {                        // 全尺寸
        g_ww = (int)(980 * dpiScale);
        g_wh = (int)(320 * dpiScale);
    }
}

static int AddKey(int x, int y, int w, int h, short vk, KeyType type, BOOL symShift = FALSE,
                  int align = KA_CENTER) {
    if (g_nk >= MAX_KEYS) return g_nk;
    KeyDef* k = &g_keys[g_nk++];
    k->x = x; k->y = y; k->w = w; k->h = h; k->vk = vk; k->type = type;
    k->symShift = symShift ? 1 : 0;
    k->align = (unsigned char)align;
    return g_nk;
}

// ============================================================================
// 小键盘布局（layoutMode==1，430×320 DIP）：4 列数字盘 + 底部动作行，共 6 行
// ----------------------------------------------------------------------------
//     Bksp   /      *      -
//     7      8      9      +      ← `+` 竖跨 Row1~Row2
//     4      5      6
//     1      2      3      Enter  ← `Enter` 竖跨 Row3~Row4
//     0 (横跨 2 列)         .
//     Tab    Shift  空格 (横跨 2 列)
//
//   没有采用 3×3 的 T9 九宫格：小键盘最常见的用法是连续敲数字（端口号 / IP / 金额），
//   保持「7-8-9 在上、1-2-3 在下」与实体数字区一致的排布最不容易按错；顺带也腾出了
//   一整行放 Tab / Shift / 空格 —— 纯数字输入里这三个组合键同样要按。
//   两套排布都实机出图对比过，用户选了这一套（T9 那版连同它的符号页已删除）。
// ============================================================================

// 小键盘：4 列 × 5 行数字盘（含 + 与 .）+ 底行 Tab / Shift / 空格
static void BuildNumpadPlus(int y) {
    const int colW  = (KEY_AREA_W - 3 * g_keyGap) / 4;
    const int pitch = g_keyHeight + g_keyGap;
    int yy = y;

    // Row 0: 回退, /, *, -
    {
        short v[4] = {0x08, 0x6F, 0x6A, 0x6D};
        KeyType t[4] = {K_SPECIAL, K_NORMAL, K_NORMAL, K_NORMAL};
        int cx = KEY_AREA_X;
        for (int i = 0; i < 4; i++) { AddKey(cx, yy, colW, g_keyHeight, v[i], t[i]); cx += colW + g_keyGap; }
    }
    yy += pitch;

    // Row 1: 7, 8, 9, +（+ 跨 2 行）
    {
        int h2 = g_keyHeight * 2 + g_keyGap;
        int cx = KEY_AREA_X;
        for (int i = 0; i < 3; i++) { AddKey(cx, yy, colW, g_keyHeight, (short)(0x67 + i), K_NORMAL); cx += colW + g_keyGap; }
        AddKey(cx, yy, colW, h2, 0x6B, K_NORMAL);
    }
    yy += pitch;

    // Row 2: 4, 5, 6
    {
        int cx = KEY_AREA_X;
        for (int i = 0; i < 3; i++) { AddKey(cx, yy, colW, g_keyHeight, (short)(0x64 + i), K_NORMAL); cx += colW + g_keyGap; }
    }
    yy += pitch;

    // Row 3: 1, 2, 3, Enter（Enter 跨 2 行）
    {
        int h2 = g_keyHeight * 2 + g_keyGap;
        int cx = KEY_AREA_X;
        for (int i = 0; i < 3; i++) { AddKey(cx, yy, colW, g_keyHeight, (short)(0x61 + i), K_NORMAL); cx += colW + g_keyGap; }
        AddKey(cx, yy, colW, h2, 0x0D, K_SPECIAL);
    }
    yy += pitch;

    // Row 4: 0（跨 2 列）, .
    {
        int cx = KEY_AREA_X;
        AddKey(cx, yy, colW * 2 + g_keyGap, g_keyHeight, 0x60, K_NORMAL);
        cx += 2 * (colW + g_keyGap);
        AddKey(cx, yy, colW, g_keyHeight, 0x6E, K_NORMAL);
    }
    yy += pitch;

    // Row 5（新增）: Tab, Shift, 空格（空格跨 2 列，正好收满右边）
    {
        int cx = KEY_AREA_X;
        AddKey(cx, yy, colW, g_keyHeight, 0x09, K_SPECIAL);  cx += colW + g_keyGap;
        AddKey(cx, yy, colW, g_keyHeight, 0xA0, K_MOD);      cx += colW + g_keyGap;
        AddKey(cx, yy, colW * 2 + g_keyGap, g_keyHeight, 0x20, K_SPACE);
    }
}

static void BuildNumpad(int y) {
    BuildNumpadPlus(y);
}

// 完整键盘布局（104 键）：主区 + 导航区 + 数字区（6 行：F 行 + 主区 5 行）
// ============================================================================

// 数字区当前是否隐藏：「用户手动按 Tab / 标题栏按钮」与「窗口过窄自动隐藏」是两件事，
// 合成一个生效值再往外用 —— 只在 G 处改一份，绘制 / 命中 / 帧缓存签名才不会互相打架。
static BOOL NumpadHidden() { return (g_npHidden || g_npHiddenAuto); }

// 窄屏自适应：键帽 44px 是触摸安全下限，反解「22u + 2gap + 2×键区边距」得 1090 DIP；
// 低于这个宽度就把数字区收起来（复用同一套显隐机制，而不是让键位溢出窗口）。
// 只在全尺寸布局下生效；由窗口宽度推导，因此不落盘。
// ⚠ 这个阈值同时是「取消隐藏」时必须把窗口拉到的宽度 —— 见 SetFullNumpadHidden，
//    两处各写一份数值就会得到「点了按钮没反应」。
#define FULL_NUMPAD_MIN_W_DIP 1090
static void UpdateNumpadAuto(int ww, double dpiScale) {
    if (g_layoutMode != 2) { g_npHiddenAuto = FALSE; return; }
    g_npHiddenAuto = (ww < (int)(FULL_NUMPAD_MIN_W_DIP * dpiScale));
}

// ============================================================================
// 全尺寸（ANSI 104）布局：1u 单位网格
//
// 列位是单一真源：主区 15u / 导航区 3u / 数字区 4u，块间 1 个 gap；
// 1u 由键区宽度反解一次，所有 x = KEY_AREA_X + round(列位 × u) + 跨块 gap。
// 于是「列永远对齐、行尾永远齐平」，也不再需要逐行 rem / 逐行除法。
//
// 旧写法是每行各算各的「剩余宽度 ÷ 键数」，同一把键盘上 1u 键的实际宽度
// 从 46px 游走到 73px（差 59%），字母键最多偏出一整个键位 —— 这次连根换掉。
//
// vrow = 2 表示竖向跨两行（数字区的 + 与回车）。
// ============================================================================
struct KbKeySpec { float col; float w; short vk; KeyType type; unsigned char vrow; };
struct KbRowSpec { const KbKeySpec* keys; int n; };

#define FULL_U_MAIN  15.0f
#define FULL_U_NAV    3.0f
#define FULL_U_NUM    4.0f

// 列位（单位 u）→ 像素 x；跨块（导航区 / 数字区）补一个 gap
static int FullColX(float col, double u, int gap, const float* bc, int nb) {
    int b = 0;
    for (int i = 0; i < nb; i++) if (bc[i] <= col + 0.001f) b++;
    return KEY_AREA_X + (int)floor(col * u + 0.5) + b * gap;
}

// 放一个键：宽度由「列位到列位」直接相减，不再由「本行键数」除出来。
// base 是这一块（主区 0 / 导航区 15 / 数字区 18）的起始列，表里写块内列位更好读，
// 但换算必须用全局列位 —— 否则导航区/数字区会被画到主区左边那一列上。
static void FullPut(float base, const KbKeySpec& s, int yy, int kh, double u, int gap,
                    const float* bc, int nb) {
    int xa = FullColX(base + s.col, u, gap, bc, nb);
    int xb = FullColX(base + s.col + s.w, u, gap, bc, nb);
    AddKey(xa, yy, xb - xa - gap, kh, s.vk, s.type);
}

// ---- 主区 6 行（列位严格按 ANSI 104，每行合计 15u）--------------------
static const KbKeySpec kFullMain0[] = {      // F 行：Esc + F1~F12（三组），无 Del
    // Del 只在导航区出现（真 104 的 F 行没有 Del）—— 之前两处都有，整把键盘出现两个 Del。
    // 腾出来的 2u 平分给三处组间缝（Esc|F1、F4|F5、F8|F9 各 2/3 u）：
    // 既不留空档，F 键又还都是 1u（14.0000 + 1.00 = 15.00，正好收在主区右边缘）。
    { 0.0000f, 1.00f, 0x1B, K_SPECIAL, 1 },
    { 1.6667f, 1.00f, 0x70, K_NORMAL , 1 }, { 2.6667f, 1.00f, 0x71, K_NORMAL , 1 },
    { 3.6667f, 1.00f, 0x72, K_NORMAL , 1 }, { 4.6667f, 1.00f, 0x73, K_NORMAL , 1 },
    { 6.3333f, 1.00f, 0x74, K_NORMAL , 1 }, { 7.3333f, 1.00f, 0x75, K_NORMAL , 1 },
    { 8.3333f, 1.00f, 0x76, K_NORMAL , 1 }, { 9.3333f, 1.00f, 0x77, K_NORMAL , 1 },
    { 11.0000f, 1.00f, 0x78, K_NORMAL, 1 }, { 12.0000f, 1.00f, 0x79, K_NORMAL, 1 },
    { 13.0000f, 1.00f, 0x7A, K_NORMAL, 1 }, { 14.0000f, 1.00f, 0x7B, K_NORMAL, 1 },
};
static const KbKeySpec kFullMain1[] = {      // 数字行
    { 0.00f, 1.00f, 0xC0, K_NORMAL , 1 },
    { 1.00f, 1.00f, 0x31, K_NORMAL , 1 }, { 2.00f, 1.00f, 0x32, K_NORMAL , 1 },
    { 3.00f, 1.00f, 0x33, K_NORMAL , 1 }, { 4.00f, 1.00f, 0x34, K_NORMAL , 1 },
    { 5.00f, 1.00f, 0x35, K_NORMAL , 1 }, { 6.00f, 1.00f, 0x36, K_NORMAL , 1 },
    { 7.00f, 1.00f, 0x37, K_NORMAL , 1 }, { 8.00f, 1.00f, 0x38, K_NORMAL , 1 },
    { 9.00f, 1.00f, 0x39, K_NORMAL , 1 }, { 10.00f, 1.00f, 0x30, K_NORMAL, 1 },
    { 11.00f, 1.00f, 0xBD, K_NORMAL, 1 }, { 12.00f, 1.00f, 0xBB, K_NORMAL, 1 },
    { 13.00f, 2.00f, 0x08, K_SPECIAL, 1 },
};
static const KbKeySpec kFullMain2[] = {      // Tab 行（qwerty）
    { 0.00f, 1.50f, 0x09, K_SPECIAL, 1 },
    { 1.50f, 1.00f, 0x51, K_LETTER , 1 }, { 2.50f, 1.00f, 0x57, K_LETTER , 1 },
    { 3.50f, 1.00f, 0x45, K_LETTER , 1 }, { 4.50f, 1.00f, 0x52, K_LETTER , 1 },
    { 5.50f, 1.00f, 0x54, K_LETTER , 1 }, { 6.50f, 1.00f, 0x59, K_LETTER , 1 },
    { 7.50f, 1.00f, 0x55, K_LETTER , 1 }, { 8.50f, 1.00f, 0x49, K_LETTER , 1 },
    { 9.50f, 1.00f, 0x4F, K_LETTER , 1 }, { 10.50f, 1.00f, 0x50, K_LETTER, 1 },
    { 11.50f, 1.00f, 0xDB, K_NORMAL , 1 }, { 12.50f, 1.00f, 0xDD, K_NORMAL , 1 },
    { 13.50f, 1.50f, 0xDC, K_NORMAL , 1 },
};
static const KbKeySpec kFullMain3[] = {      // Caps 行（asdf）
    { 0.00f, 1.75f, 0x14, K_CAPS, 1 },
    { 1.75f, 1.00f, 0x41, K_LETTER, 1 }, { 2.75f, 1.00f, 0x53, K_LETTER, 1 },
    { 3.75f, 1.00f, 0x44, K_LETTER, 1 }, { 4.75f, 1.00f, 0x46, K_LETTER, 1 },
    { 5.75f, 1.00f, 0x47, K_LETTER, 1 }, { 6.75f, 1.00f, 0x48, K_LETTER, 1 },
    { 7.75f, 1.00f, 0x4A, K_LETTER, 1 }, { 8.75f, 1.00f, 0x4B, K_LETTER, 1 },
    { 9.75f, 1.00f, 0x4C, K_LETTER, 1 }, { 10.75f, 1.00f, 0xBA, K_NORMAL, 1 },
    { 11.75f, 1.00f, 0xDE, K_NORMAL, 1 }, { 12.75f, 2.25f, 0x0D, K_SPECIAL, 1 },
};
static const KbKeySpec kFullMain4[] = {      // Shift 行（zxcv）
    { 0.00f, 2.25f, 0xA0, K_MOD, 1 },
    { 2.25f, 1.00f, 0x5A, K_LETTER, 1 }, { 3.25f, 1.00f, 0x58, K_LETTER, 1 },
    { 4.25f, 1.00f, 0x43, K_LETTER, 1 }, { 5.25f, 1.00f, 0x56, K_LETTER, 1 },
    { 6.25f, 1.00f, 0x42, K_LETTER, 1 }, { 7.25f, 1.00f, 0x4E, K_LETTER, 1 },
    { 8.25f, 1.00f, 0x4D, K_LETTER, 1 }, { 9.25f, 1.00f, 0xBC, K_NORMAL, 1 },
    { 10.25f, 1.00f, 0xBE, K_NORMAL, 1 }, { 11.25f, 1.00f, 0xBF, K_NORMAL, 1 },
    { 12.25f, 2.75f, 0xA1, K_MOD, 1 },
};
static const KbKeySpec kFullMain5[] = {      // Ctrl 行（含 6.25u 空格）
    { 0.00f, 1.25f, 0x00, K_SPECIAL, 1 }, { 1.25f, 1.25f, 0x11, K_MOD, 1 },
    { 2.50f, 1.25f, 0x5B, K_SPECIAL, 1 }, { 3.75f, 1.25f, 0x12, K_MOD, 1 },
    { 5.00f, 6.25f, 0x20, K_SPACE  , 1 },
    { 11.25f, 1.25f, 0x12, K_MOD, 1 }, { 12.50f, 1.25f, 0x5D, K_MOD, 1 },
    { 13.75f, 1.25f, 0x11, K_MOD, 1 },
};
// 不带 Fn 的同一行：全尺寸布局本身就常驻 F 行（kFullMain0），底行的 Fn 只用来切
// 「Fn 网页层」，而 Fn 在默认布局里的正职（显示 F1~F12）在这里已经由 F 行完成了。
// 用户反馈「启用顶部显示 F 键时就应该隐藏 Fn 按钮」——全尺寸就是那个状态。
// 腾出来的 1.25u 直接并进空格（3.75 + 7.50 = 11.25，右半段列位一字不动）。
static const KbKeySpec kFullMain5NoFn[] = {
    { 0.00f, 1.25f, 0x11, K_MOD, 1 },
    { 1.25f, 1.25f, 0x5B, K_SPECIAL, 1 }, { 2.50f, 1.25f, 0x12, K_MOD, 1 },
    { 3.75f, 7.50f, 0x20, K_SPACE  , 1 },
    { 11.25f, 1.25f, 0x12, K_MOD, 1 }, { 12.50f, 1.25f, 0x5D, K_MOD, 1 },
    { 13.75f, 1.25f, 0x11, K_MOD, 1 },
};
// Fn 网页层：Shift 行的字母键换网址后缀键
static const KbKeySpec kFullWebShift[] = {
    { 0.00f, 2.25f, 0xA0, K_MOD, 1 },
    { 2.25f, 1.00f, 0x200, K_SPECIAL, 1 }, { 3.25f, 1.00f, 0x201, K_SPECIAL, 1 },
    { 4.25f, 1.00f, 0x202, K_SPECIAL, 1 }, { 5.25f, 1.00f, 0x203, K_SPECIAL, 1 },
    { 6.25f, 1.00f, 0x204, K_SPECIAL, 1 }, { 7.25f, 1.00f, 0x205, K_SPECIAL, 1 },
    { 8.25f, 1.00f, 0x0BF, K_NORMAL , 1 }, { 9.25f, 1.00f, 0x0BC, K_NORMAL , 1 },
    { 10.25f, 1.00f, 0x0BE, K_NORMAL, 1 }, { 11.25f, 1.00f, 0x02F, K_NORMAL, 1 },
    { 12.25f, 2.75f, 0xA1, K_MOD, 1 },
};
static const KbRowSpec kFullMainRows[6] = {
    { kFullMain0, (int)(sizeof(kFullMain0) / sizeof(KbKeySpec)) },
    { kFullMain1, (int)(sizeof(kFullMain1) / sizeof(KbKeySpec)) },
    { kFullMain2, (int)(sizeof(kFullMain2) / sizeof(KbKeySpec)) },
    { kFullMain3, (int)(sizeof(kFullMain3) / sizeof(KbKeySpec)) },
    { kFullMain4, (int)(sizeof(kFullMain4) / sizeof(KbKeySpec)) },
    { kFullMain5, (int)(sizeof(kFullMain5) / sizeof(KbKeySpec)) },
};
// Fn 网页层用到的那一行
#define kFullWebShiftN ((int)(sizeof(kFullWebShift) / sizeof(KbKeySpec)))

// ---- 导航区 3u：行位与主区对齐（PrtSc 在 F 行、Ins 在数字行、Del 在 Tab 行、
//      ↑ 在 Shift 行、←↓→ 在 Ctrl 行）—— 这一点现状就是对的，保持不动 ----
static const KbKeySpec kFullNav0[] = {
    // ⚠ ScrLk 必须是 VK_SCROLL(0x91)：一直写成 0x46，而 0x46 是字母 F ——
    //    这一格等于在按 f，从来没切换过滚动锁。字母 f 是 K_LETTER，两者靠 type 区分。
    { 0.00f, 1.00f, 0x2C, K_SPECIAL, 1 }, { 1.00f, 1.00f, 0x91, K_SPECIAL, 1 },
    { 2.00f, 1.00f, 0x13, K_SPECIAL, 1 },
};
static const KbKeySpec kFullNav1[] = {
    { 0.00f, 1.00f, 0x2D, K_SPECIAL, 1 }, { 1.00f, 1.00f, 0x24, K_SPECIAL, 1 },
    { 2.00f, 1.00f, 0x21, K_SPECIAL, 1 },
};
static const KbKeySpec kFullNav2[] = {
    { 0.00f, 1.00f, 0x2E, K_SPECIAL, 1 }, { 1.00f, 1.00f, 0x23, K_SPECIAL, 1 },
    { 2.00f, 1.00f, 0x22, K_SPECIAL, 1 },
};
static const KbKeySpec kFullNav4[] = { { 1.00f, 1.00f, 0x26, K_ARROW, 1 } };
static const KbKeySpec kFullNav5[] = {
    { 0.00f, 1.00f, 0x25, K_ARROW, 1 }, { 1.00f, 1.00f, 0x28, K_ARROW, 1 },
    { 2.00f, 1.00f, 0x27, K_ARROW, 1 },
};

// ---- 数字区 4u：5 行，对齐主区的「数字行 ↔ Ctrl 行」（= 真实 104）--------
// 旧写法把 NumLock 行放在 F 行，整块被拉成 6 行，回车底边停在「0 行之上整整一行」，
// 与 0 键、与键盘底边都不齐；F 行右侧那 4 列现在空着（真 104 那里就是空的）。
static const KbKeySpec kFullNum0[] = {
    // 保留 0x90(NumLock)：数字区要跟真 104 对齐，那一格就是 NumLock；主区本来就有 2u 的
    // 退格，而这里每个键只有 1u（56.9 DIP），「Backspace」降到底档也要 154px、只有 89px
    // 可用 —— 换成回退键只能画成图标，不如留给 NumLock（也是全程序唯一能开 NumLock 的地方）。
    { 0.00f, 1.00f, 0x90, K_SPECIAL, 1 }, { 1.00f, 1.00f, 0x6F, K_NORMAL, 1 },
    { 2.00f, 1.00f, 0x6A, K_NORMAL , 1 }, { 3.00f, 1.00f, 0x6D, K_NORMAL, 1 },
};
static const KbKeySpec kFullNum1[] = {       // + 跨「789 行 + 456 行」
    { 0.00f, 1.00f, 0x67, K_NORMAL, 1 }, { 1.00f, 1.00f, 0x68, K_NORMAL, 1 },
    { 2.00f, 1.00f, 0x69, K_NORMAL, 1 }, { 3.00f, 1.00f, 0x6B, K_NORMAL, 2 },
};
static const KbKeySpec kFullNum2[] = {
    { 0.00f, 1.00f, 0x64, K_NORMAL, 1 }, { 1.00f, 1.00f, 0x65, K_NORMAL, 1 },
    { 2.00f, 1.00f, 0x66, K_NORMAL, 1 },
};
static const KbKeySpec kFullNum3[] = {       // 回车跨「123 行 + 0 行」
    { 0.00f, 1.00f, 0x61, K_NORMAL, 1 }, { 1.00f, 1.00f, 0x62, K_NORMAL, 1 },
    { 2.00f, 1.00f, 0x63, K_NORMAL, 1 }, { 3.00f, 1.00f, 0x0D, K_SPECIAL, 2 },
};
static const KbKeySpec kFullNum4[] = {
    { 0.00f, 2.00f, 0x60, K_NORMAL, 1 }, { 2.00f, 1.00f, 0x6E, K_NORMAL, 1 },
};

// webFn：Fn 网页布局层——Shift 行字母键换为网址后缀键
// 数字区显隐取「生效值」= 用户按 Tab 手动隐藏 || 窗口过窄自动隐藏（见 NumpadHidden）
static void BuildComplete(int y, BOOL webFn) {
    const int  gap = g_keyGap;
    const int  KH  = g_keyHeight;
    const int  h2  = KH * 2 + gap;
    const BOOL npVisible = !NumpadHidden();

    // 1u 反解：宽度 = 22u + 2*gap（隐藏数字区时 18u + gap）。用 double，不截断。
    const int    nb    = npVisible ? 2 : 1;
    const float  units = FULL_U_MAIN + FULL_U_NAV + (npVisible ? FULL_U_NUM : 0.0f);
    const double u     = (double)(KEY_AREA_W - nb * gap) / (double)units;
    const float  bc[2] = { FULL_U_MAIN, FULL_U_MAIN + FULL_U_NAV };

    // 主区 6 行
    // 底行 Fn 的取舍：全尺寸布局常驻 F 行，Fn 只剩「切 Fn 网页层」这一个作用 ——
    // 网页布局没开时它就是废键，按用户要求隐掉（腾出的宽度并进空格）；
    // 开着时保留，否则打开网页布局后就没有回到主键位的路了。
    const BOOL hideFn = !g_fnWebLayout;
    const int  noFnN  = (int)(sizeof(kFullMain5NoFn) / sizeof(KbKeySpec));
    for (int r = 0; r < 6; r++) {
        int yy = y + r * (KH + gap);
        if (r == 4 && webFn) {                 // Fn 网页层换掉 Shift 行
            for (int i = 0; i < kFullWebShiftN; i++)
                FullPut(0.0f, kFullWebShift[i], yy, KH, u, gap, bc, nb);
            continue;
        }
        if (r == 5 && hideFn) {                // 无 Fn 的 Ctrl 行
            for (int i = 0; i < noFnN; i++)
                FullPut(0.0f, kFullMain5NoFn[i], yy, KH, u, gap, bc, nb);
            continue;
        }
        const KbRowSpec& row = kFullMainRows[r];
        for (int i = 0; i < row.n; i++)
            FullPut(0.0f, row.keys[i], yy, row.keys[i].vrow == 2 ? h2 : KH, u, gap, bc, nb);
    }

    // 导航区（6 行里第 4 行空着 —— 那里是真键盘的品牌位）
    {
        static const struct { const KbKeySpec* k; int n; int r; } nav[6] = {
            { kFullNav0, 3, 0 }, { kFullNav1, 3, 1 }, { kFullNav2, 3, 2 },
            { NULL,      0, 3 }, { kFullNav4, 1, 4 }, { kFullNav5, 3, 5 },
        };
        for (int i = 0; i < 6; i++) {
            if (!nav[i].k) continue;
            int yy = y + nav[i].r * (KH + gap);
            for (int j = 0; j < nav[i].n; j++)
                FullPut(FULL_U_MAIN, nav[i].k[j], yy, KH, u, gap, bc, nb);
        }
    }

    // 数字区 5 行：从「数字行」起，不是 F 行
    if (npVisible) {
        static const struct { const KbKeySpec* k; int n; } num[5] = {
            { kFullNum0, 4 }, { kFullNum1, 4 }, { kFullNum2, 3 },
            { kFullNum3, 4 }, { kFullNum4, 2 },
        };
        for (int i = 0; i < 5; i++) {
            int yy = y + (i + 1) * (KH + gap);
            for (int j = 0; j < num[i].n; j++) {
                const KbKeySpec& s = num[i].k[j];
                FullPut(FULL_U_MAIN + FULL_U_NAV, s, yy, s.vrow == 2 ? h2 : KH, u, gap, bc, nb);
            }
        }
    }
}

// Fn 网页布局层（默认布局下按 Fn 切换）
//
// 结构照用户给的参考图（clipboard-2026-10-01 …648Z）：把「要按 Shift 才出得来」的符号
// 直接铺成两行键面，打网址 / 填表单不用先按 Shift 再回头找键：
//   Row 0: Esc, `, F1~F12, Backspace
//   Row 1: Tab, ! @ # $ % ^ & * ( ) [ ] \, Del
//   Row 2: Caps, _ + { } | : " < > ; ', Enter
//   Row 3: Shift, www. .com .cn .org .cc .net, ? , . /, ↑, Shift
//   Row 4: Fn, Ctrl, Win, Alt, 空格, Alt, Menu, Ctrl, ←, ↓, →
//
// 旧写法把字母行留在 Row1/Row2（qwerty / asdf），只在 Shift 行塞网址后缀 ——
// 结果是「上网时要的数字与符号一个都打不出来」，正是用户说的「布局太怪」。
// 符号键一律用 K_SYM：键面固定画一个符号、点击直接输出它，不再依赖 g_sh/g_shiftSymbols，
// 所以这个层里点 Shift 不会把键面换成符号（用户明确要求「点击 Shift 键就不用高亮显示符号了」）。
//
// 符号格 = (基础虚拟键, 取不取副符号)。取副符号的格子点击时自动带 Shift 发出。
struct WebSymSpec { short vk; unsigned char shifted; };
// Row1：数字行 10 格的副符号 + 主符号的 [ ] 与反斜杠
static const WebSymSpec kWebSym1[13] = {
    { 0x31, 1 }, { 0x32, 1 }, { 0x33, 1 }, { 0x34, 1 }, { 0x35, 1 },
    { 0x36, 1 }, { 0x37, 1 }, { 0x38, 1 }, { 0x39, 1 }, { 0x30, 1 },
    { 0xDB, 0 }, { 0xDD, 0 }, { 0xDC, 0 },
};
// Row2：- = [ ] \ ; ' , . 的副符号占前 9 格，最后两格给 ; 与 ' 的主符号
static const WebSymSpec kWebSym2[11] = {
    { 0xBD, 1 }, { 0xBB, 1 }, { 0xDB, 1 }, { 0xDD, 1 }, { 0xDC, 1 },
    { 0xBA, 1 }, { 0xDE, 1 }, { 0xBC, 1 }, { 0xBE, 1 },
    { 0xBA, 0 }, { 0xDE, 0 },
};

// ============================================================================
// 默认布局（layoutMode==0，980×320 DIP）：1u 单位网格
//
// 列位是单一真源：每行合计 15.5u，1u 的列距由窗口宽度反解一次，
//   左右留白各 0.30u  →  2×0.30u + 15.5u = g_ww + gap  →  u = (g_ww + gap) / 16.1
// 所有 x = KEY_AREA_X + round(列位 × u)，键面宽 = 两个列位各自取整后相减再扣一个 gap。
// 于是「每行白键严格等宽、行尾永远齐平、↑ 与 ↓ 自动落在同一列」。
//
// 旧写法是每行各算各的「剩余宽度 ÷ 键数」，四行白键因此各不一样
// （980 DIP 实测 57.3 / 59.1 / 67.5 / 71.3 DIP，最大差 24.6%）——
// 用户要求「白色的数字、字母、按钮大小统一」，差异的根源就在这里。
//
// 宽键按真键盘比例：Esc 1u、Backspace 1.5u、Tab 1.5u、Del 1u、Caps 2u、
// Enter 2.5u、LShift 2.5u、↑ 1u、RShift 2u、空格 5.5u。
// 参考图实测的是 Tab 1.5u / Caps 2u / LShift 2.4u / RShift 1.8u / Enter 2.5u，
// 只有 Shift 取整成 2.5 与 2 —— 这样 ↑ 与 ↓ 才正好同列（13.5u 处）。
//
// 底行键序 = 参考图：Ctrl Fn Win Alt [空格] Menu Alt ← ↓ → Ctrl
// （Menu 落在空格右侧、右 Alt 之前；最后一个 Ctrl 贴到最右端）。
// ============================================================================
#define DEF_ROW_U   15.5    // 每行合计 u 数
#define DEF_PAD_U    0.30   // 左右留白（u 的倍数）

// col：起始列位（u）；w：跨几个 u；align：见 KeyAlign；symShift：仅 K_SYM 用。
struct DefKeySpec { float col; float w; short vk; KeyType type; KeyAlign align; unsigned char symShift; };

// 放一行：键面宽由「列位到列位」直接相减，不再由「本行键数」除出来。
static void DefPutRow(int y, const DefKeySpec* ks, int n, double u) {
    const int gap = g_keyGap;
    for (int i = 0; i < n; i++) {
        int xa = KEY_AREA_X + (int)floor((double)ks[i].col * u + 0.5);
        int xb = KEY_AREA_X + (int)floor((double)(ks[i].col + ks[i].w) * u + 0.5);
        AddKey(xa, y, xb - xa - gap, g_keyHeight, ks[i].vk, ks[i].type,
               ks[i].symShift ? TRUE : FALSE, ks[i].align);
    }
}

// ---- F 行（可选，g_showFKeys）：Esc + F1~F12 + Del = 15.5u ----
// Del 取 1.5u，与下一行的 Backspace 同宽；腾出来的 1u 平分给三处组间缝
// （Esc|F1、F4|F5、F8|F9 各 1/3 u）—— F 键因此仍全是 1u，末格 F12 又正好与
// 数字行的 `=` 列位重合。与全尺寸布局的 F 行是同一套做法。
static const DefKeySpec kDefRowF[] = {
    {  0.0000f, 1.00f, 0x1B, K_SPECIAL, KA_LEFT  , 0 },
    {  1.3333f, 1.00f, 0x70, K_NORMAL , KA_CENTER, 0 },
    {  2.3333f, 1.00f, 0x71, K_NORMAL , KA_CENTER, 0 },
    {  3.3333f, 1.00f, 0x72, K_NORMAL , KA_CENTER, 0 },
    {  4.3333f, 1.00f, 0x73, K_NORMAL , KA_CENTER, 0 },
    {  5.6667f, 1.00f, 0x74, K_NORMAL , KA_CENTER, 0 },
    {  6.6667f, 1.00f, 0x75, K_NORMAL , KA_CENTER, 0 },
    {  7.6667f, 1.00f, 0x76, K_NORMAL , KA_CENTER, 0 },
    {  8.6667f, 1.00f, 0x77, K_NORMAL , KA_CENTER, 0 },
    { 10.0000f, 1.00f, 0x78, K_NORMAL , KA_CENTER, 0 },
    { 11.0000f, 1.00f, 0x79, K_NORMAL , KA_CENTER, 0 },
    { 12.0000f, 1.00f, 0x7A, K_NORMAL , KA_CENTER, 0 },
    { 13.0000f, 1.00f, 0x7B, K_NORMAL , KA_CENTER, 0 },
    { 14.0000f, 1.50f, 0x2E, K_SPECIAL, KA_RIGHT , 0 },
};
#define kDefRowFN ((int)(sizeof(kDefRowF) / sizeof(DefKeySpec)))

// ---- Row0：Esc, `, 1~0, -, =, Backspace = 15.5u ----
static const DefKeySpec kDefRow0[] = {
    {  0.00f, 1.00f, 0x1B, K_SPECIAL, KA_LEFT  , 0 },
    {  1.00f, 1.00f, 0xC0, K_NORMAL , KA_CENTER, 0 },
    {  2.00f, 1.00f, 0x31, K_NORMAL , KA_CENTER, 0 },
    {  3.00f, 1.00f, 0x32, K_NORMAL , KA_CENTER, 0 },
    {  4.00f, 1.00f, 0x33, K_NORMAL , KA_CENTER, 0 },
    {  5.00f, 1.00f, 0x34, K_NORMAL , KA_CENTER, 0 },
    {  6.00f, 1.00f, 0x35, K_NORMAL , KA_CENTER, 0 },
    {  7.00f, 1.00f, 0x36, K_NORMAL , KA_CENTER, 0 },
    {  8.00f, 1.00f, 0x37, K_NORMAL , KA_CENTER, 0 },
    {  9.00f, 1.00f, 0x38, K_NORMAL , KA_CENTER, 0 },
    { 10.00f, 1.00f, 0x39, K_NORMAL , KA_CENTER, 0 },
    { 11.00f, 1.00f, 0x30, K_NORMAL , KA_CENTER, 0 },
    { 12.00f, 1.00f, 0xBD, K_NORMAL , KA_CENTER, 0 },
    { 13.00f, 1.00f, 0xBB, K_NORMAL , KA_CENTER, 0 },
    { 14.00f, 1.50f, 0x08, K_SPECIAL, KA_RIGHT , 0 },
};
#define kDefRow0N ((int)(sizeof(kDefRow0) / sizeof(DefKeySpec)))

// 开了 F 行就不再重复画 Esc，空出来的 1u 并进退格（1.5 → 2.5u），行仍是 15.5u。
static const DefKeySpec kDefRow0NoEsc[] = {
    {  0.00f, 1.00f, 0xC0, K_NORMAL , KA_CENTER, 0 },
    {  1.00f, 1.00f, 0x31, K_NORMAL , KA_CENTER, 0 },
    {  2.00f, 1.00f, 0x32, K_NORMAL , KA_CENTER, 0 },
    {  3.00f, 1.00f, 0x33, K_NORMAL , KA_CENTER, 0 },
    {  4.00f, 1.00f, 0x34, K_NORMAL , KA_CENTER, 0 },
    {  5.00f, 1.00f, 0x35, K_NORMAL , KA_CENTER, 0 },
    {  6.00f, 1.00f, 0x36, K_NORMAL , KA_CENTER, 0 },
    {  7.00f, 1.00f, 0x37, K_NORMAL , KA_CENTER, 0 },
    {  8.00f, 1.00f, 0x38, K_NORMAL , KA_CENTER, 0 },
    {  9.00f, 1.00f, 0x39, K_NORMAL , KA_CENTER, 0 },
    { 10.00f, 1.00f, 0x30, K_NORMAL , KA_CENTER, 0 },
    { 11.00f, 1.00f, 0xBD, K_NORMAL , KA_CENTER, 0 },
    { 12.00f, 1.00f, 0xBB, K_NORMAL , KA_CENTER, 0 },
    { 13.00f, 2.50f, 0x08, K_SPECIAL, KA_RIGHT , 0 },
};
#define kDefRow0NoEscN ((int)(sizeof(kDefRow0NoEsc) / sizeof(DefKeySpec)))

// ---- Row1：Tab, q~p, [, ], \, Del = 15.5u ----
static const DefKeySpec kDefRow1[] = {
    {  0.00f, 1.50f, 0x09, K_SPECIAL, KA_LEFT  , 0 },
    {  1.50f, 1.00f, 0x51, K_LETTER , KA_CENTER, 0 },
    {  2.50f, 1.00f, 0x57, K_LETTER , KA_CENTER, 0 },
    {  3.50f, 1.00f, 0x45, K_LETTER , KA_CENTER, 0 },
    {  4.50f, 1.00f, 0x52, K_LETTER , KA_CENTER, 0 },
    {  5.50f, 1.00f, 0x54, K_LETTER , KA_CENTER, 0 },
    {  6.50f, 1.00f, 0x59, K_LETTER , KA_CENTER, 0 },
    {  7.50f, 1.00f, 0x55, K_LETTER , KA_CENTER, 0 },
    {  8.50f, 1.00f, 0x49, K_LETTER , KA_CENTER, 0 },
    {  9.50f, 1.00f, 0x4F, K_LETTER , KA_CENTER, 0 },
    { 10.50f, 1.00f, 0x50, K_LETTER , KA_CENTER, 0 },
    { 11.50f, 1.00f, 0xDB, K_NORMAL , KA_CENTER, 0 },
    { 12.50f, 1.00f, 0xDD, K_NORMAL , KA_CENTER, 0 },
    { 13.50f, 1.00f, 0xDC, K_NORMAL , KA_CENTER, 0 },
    { 14.50f, 1.00f, 0x2E, K_SPECIAL, KA_RIGHT , 0 },
};
#define kDefRow1N ((int)(sizeof(kDefRow1) / sizeof(DefKeySpec)))

// 开了 F 行就不再重复画 Del，空出来的 1u 并进反斜杠（1 → 2u），行仍是 15.5u。
static const DefKeySpec kDefRow1NoDel[] = {
    {  0.00f, 1.50f, 0x09, K_SPECIAL, KA_LEFT  , 0 },
    {  1.50f, 1.00f, 0x51, K_LETTER , KA_CENTER, 0 },
    {  2.50f, 1.00f, 0x57, K_LETTER , KA_CENTER, 0 },
    {  3.50f, 1.00f, 0x45, K_LETTER , KA_CENTER, 0 },
    {  4.50f, 1.00f, 0x52, K_LETTER , KA_CENTER, 0 },
    {  5.50f, 1.00f, 0x54, K_LETTER , KA_CENTER, 0 },
    {  6.50f, 1.00f, 0x59, K_LETTER , KA_CENTER, 0 },
    {  7.50f, 1.00f, 0x55, K_LETTER , KA_CENTER, 0 },
    {  8.50f, 1.00f, 0x49, K_LETTER , KA_CENTER, 0 },
    {  9.50f, 1.00f, 0x4F, K_LETTER , KA_CENTER, 0 },
    { 10.50f, 1.00f, 0x50, K_LETTER , KA_CENTER, 0 },
    { 11.50f, 1.00f, 0xDB, K_NORMAL , KA_CENTER, 0 },
    { 12.50f, 1.00f, 0xDD, K_NORMAL , KA_CENTER, 0 },
    { 13.50f, 2.00f, 0xDC, K_NORMAL , KA_CENTER, 0 },
};
#define kDefRow1NoDelN ((int)(sizeof(kDefRow1NoDel) / sizeof(DefKeySpec)))

// ---- Row2：Caps, a~l, ;, ', Enter = 15.5u ----
static const DefKeySpec kDefRow2[] = {
    {  0.00f, 2.00f, 0x14, K_CAPS  , KA_LEFT  , 0 },
    {  2.00f, 1.00f, 0x41, K_LETTER, KA_CENTER, 0 },
    {  3.00f, 1.00f, 0x53, K_LETTER, KA_CENTER, 0 },
    {  4.00f, 1.00f, 0x44, K_LETTER, KA_CENTER, 0 },
    {  5.00f, 1.00f, 0x46, K_LETTER, KA_CENTER, 0 },
    {  6.00f, 1.00f, 0x47, K_LETTER, KA_CENTER, 0 },
    {  7.00f, 1.00f, 0x48, K_LETTER, KA_CENTER, 0 },
    {  8.00f, 1.00f, 0x4A, K_LETTER, KA_CENTER, 0 },
    {  9.00f, 1.00f, 0x4B, K_LETTER, KA_CENTER, 0 },
    { 10.00f, 1.00f, 0x4C, K_LETTER, KA_CENTER, 0 },
    { 11.00f, 1.00f, 0xBA, K_NORMAL, KA_CENTER, 0 },
    { 12.00f, 1.00f, 0xDE, K_NORMAL, KA_CENTER, 0 },
    { 13.00f, 2.50f, 0x0D, K_SPECIAL, KA_RIGHT, 0 },
};
#define kDefRow2N ((int)(sizeof(kDefRow2) / sizeof(DefKeySpec)))

// ---- Row3：LShift, z~m, ,, ., /, ↑, RShift = 15.5u ----
// ↑ 落在 12.5u 处，与下一行的 ↓ 完全同列（无需额外对齐变量）
static const DefKeySpec kDefRow3[] = {
    {  0.00f, 2.50f, 0xA0, K_MOD   , KA_LEFT  , 0 },
    {  2.50f, 1.00f, 0x5A, K_LETTER, KA_CENTER, 0 },
    {  3.50f, 1.00f, 0x58, K_LETTER, KA_CENTER, 0 },
    {  4.50f, 1.00f, 0x43, K_LETTER, KA_CENTER, 0 },
    {  5.50f, 1.00f, 0x56, K_LETTER, KA_CENTER, 0 },
    {  6.50f, 1.00f, 0x42, K_LETTER, KA_CENTER, 0 },
    {  7.50f, 1.00f, 0x4E, K_LETTER, KA_CENTER, 0 },
    {  8.50f, 1.00f, 0x4D, K_LETTER, KA_CENTER, 0 },
    {  9.50f, 1.00f, 0xBC, K_NORMAL, KA_CENTER, 0 },
    { 10.50f, 1.00f, 0xBE, K_NORMAL, KA_CENTER, 0 },
    { 11.50f, 1.00f, 0xBF, K_NORMAL, KA_CENTER, 0 },
    { 12.50f, 1.00f, 0x26, K_ARROW , KA_CENTER, 0 },
    { 13.50f, 2.00f, 0xA1, K_MOD   , KA_RIGHT , 0 },
};
#define kDefRow3N ((int)(sizeof(kDefRow3) / sizeof(DefKeySpec)))

// ---- Row4：底行（参考图键序）----
// 带 Fn：Ctrl Fn Win Alt [空格 5.5u] Menu Alt ← ↓ → Ctrl = 15.5u
static const DefKeySpec kDefRow4[] = {
    {  0.00f, 1.00f, 0x11, K_MOD    , KA_CENTER, 0 },
    {  1.00f, 1.00f, 0x00, K_SPECIAL, KA_CENTER, 0 },   // Fn
    {  2.00f, 1.00f, 0x5B, K_SPECIAL, KA_CENTER, 0 },   // Win
    {  3.00f, 1.00f, 0x12, K_MOD    , KA_CENTER, 0 },
    {  4.00f, 5.50f, 0x20, K_SPACE  , KA_CENTER, 0 },
    {  9.50f, 1.00f, 0x5D, K_MOD    , KA_CENTER, 0 },   // Menu（空格右侧、右 Alt 之前）
    { 10.50f, 1.00f, 0x12, K_MOD    , KA_CENTER, 0 },
    { 11.50f, 1.00f, 0x25, K_ARROW  , KA_CENTER, 0 },
    { 12.50f, 1.00f, 0x28, K_ARROW  , KA_CENTER, 0 },
    { 13.50f, 1.00f, 0x27, K_ARROW  , KA_CENTER, 0 },
    { 14.50f, 1.00f, 0x11, K_MOD    , KA_CENTER, 0 },
};
#define kDefRow4N ((int)(sizeof(kDefRow4) / sizeof(DefKeySpec)))

// 开了 F 行就藏 Fn（Fn 的正职「出 F1~F12」已由顶行完成），空出来的 1u 并进空格
// （5.5 → 6.5u）；右半段列位一字不动，↑↓ 同列的约束继续成立。
static const DefKeySpec kDefRow4NoFn[] = {
    {  0.00f, 1.00f, 0x11, K_MOD    , KA_CENTER, 0 },
    {  1.00f, 1.00f, 0x5B, K_SPECIAL, KA_CENTER, 0 },   // Win
    {  2.00f, 1.00f, 0x12, K_MOD    , KA_CENTER, 0 },
    {  3.00f, 6.50f, 0x20, K_SPACE  , KA_CENTER, 0 },
    {  9.50f, 1.00f, 0x5D, K_MOD    , KA_CENTER, 0 },   // Menu
    { 10.50f, 1.00f, 0x12, K_MOD    , KA_CENTER, 0 },
    { 11.50f, 1.00f, 0x25, K_ARROW  , KA_CENTER, 0 },
    { 12.50f, 1.00f, 0x28, K_ARROW  , KA_CENTER, 0 },
    { 13.50f, 1.00f, 0x27, K_ARROW  , KA_CENTER, 0 },
    { 14.50f, 1.00f, 0x11, K_MOD    , KA_CENTER, 0 },
};
#define kDefRow4NoFnN ((int)(sizeof(kDefRow4NoFn) / sizeof(DefKeySpec)))

static void BuildFnSurf(int y, double u) {
    // Row 0: Esc, `, F1~F12, Backspace (15 keys)
    {
        DefKeySpec ks[15];
        static const short v0[13] = {0xC0, 0x70,0x71,0x72,0x73,0x74,0x75,0x76,
                                     0x77,0x78,0x79,0x7A,0x7B};
        ks[0] = DefKeySpec{ 0.00f, 1.00f, 0x1B, K_SPECIAL, KA_LEFT, 0 };
        for (int i = 0; i < 13; i++) ks[1 + i] = DefKeySpec{ (float)(1.00f + i), 1.00f, v0[i], K_NORMAL, KA_CENTER, 0 };
        ks[14] = DefKeySpec{ 14.00f, 1.50f, 0x08, K_SPECIAL, KA_RIGHT, 0 };
        DefPutRow(y, ks, 15, u);
        y += g_keyHeight + g_keyGap;
    }

    // Row 1: Tab, 13 个符号格, Del (15 keys)
    {
        DefKeySpec ks[15];
        ks[0] = DefKeySpec{ 0.00f, 1.50f, 0x09, K_SPECIAL, KA_LEFT, 0 };
        for (int i = 0; i < 13; i++) {
            ks[1 + i] = DefKeySpec{ (float)(1.50f + i), 1.00f, kWebSym1[i].vk, K_SYM,
                                      KA_CENTER, (unsigned char)(kWebSym1[i].shifted ? 1 : 0) };
        }
        ks[14] = DefKeySpec{ 14.50f, 1.00f, 0x2E, K_SPECIAL, KA_RIGHT, 0 };
        DefPutRow(y, ks, 15, u);
        y += g_keyHeight + g_keyGap;
    }

    // Row 2: Caps, 11 个符号格, Enter (13 keys)
    {
        DefKeySpec ks[13];
        ks[0] = DefKeySpec{ 0.00f, 2.00f, 0x14, K_CAPS, KA_LEFT, 0 };
        for (int i = 0; i < 11; i++) {
            ks[1 + i] = DefKeySpec{ (float)(2.00f + i), 1.00f, kWebSym2[i].vk, K_SYM,
                                      KA_CENTER, (unsigned char)(kWebSym2[i].shifted ? 1 : 0) };
        }
        ks[12] = DefKeySpec{ 13.00f, 2.50f, 0x0D, K_SPECIAL, KA_RIGHT, 0 };
        DefPutRow(y, ks, 13, u);
        y += g_keyHeight + g_keyGap;
    }

    // Row 3: Shift, 网址后缀×6, ? , . /, ↑, Shift (13 keys)
    {
        // ⚠ `/` 的虚拟键码是 VK_OEM_2 = 0xBF，**不是** 0x2F —— 0x2F 是 ASCII 的 '/'，
        //    不是虚拟键码：GetSymForKey 查不到它 → 键面空白；点下去也发不出任何字符。
        static const DefKeySpec ks[13] = {
            {  0.00f, 2.50f, 0xA0 , K_MOD    , KA_LEFT  , 0 },
            {  2.50f, 1.00f, 0x200, K_SPECIAL, KA_CENTER, 0 },
            {  3.50f, 1.00f, 0x201, K_SPECIAL, KA_CENTER, 0 },
            {  4.50f, 1.00f, 0x202, K_SPECIAL, KA_CENTER, 0 },
            {  5.50f, 1.00f, 0x203, K_SPECIAL, KA_CENTER, 0 },
            {  6.50f, 1.00f, 0x204, K_SPECIAL, KA_CENTER, 0 },
            {  7.50f, 1.00f, 0x205, K_SPECIAL, KA_CENTER, 0 },
            {  8.50f, 1.00f, 0xBF , K_SYM    , KA_CENTER, 1 },   // ?（取副符号）
            {  9.50f, 1.00f, 0xBC , K_SYM    , KA_CENTER, 0 },
            { 10.50f, 1.00f, 0xBE , K_SYM    , KA_CENTER, 0 },
            { 11.50f, 1.00f, 0xBF , K_SYM    , KA_CENTER, 0 },
            { 12.50f, 1.00f, 0x26 , K_ARROW  , KA_CENTER, 0 },
            { 13.50f, 2.00f, 0xA1 , K_MOD    , KA_RIGHT , 0 },
        };
        DefPutRow(y, ks, 13, u);
        y += g_keyHeight + g_keyGap;
    }

    // Row 4: 与默认布局的底行完全同一张表 —— 网页层只是字母区换了内容，
    // 底行的键序/宽度必须逐像素一致，否则切层时下半张键盘会跳一下。
    // Fn 必须留：它是这个层的唯一出口（「已有 F 行就藏 Fn」的规则不适用于这里）。
    DefPutRow(y, kDefRow4, kDefRow4N, u);
}

// 页头里胶囊按钮的位置（DIP）：上留白 10 + 按钮高 28，底边 = 38。
// 键位竖向留白以「按钮底边」为基准，所以这两个常量必须与 GetHeaderMetrics 同源
// —— 两处各写一遍数值，改一处就会让整个键盘在窗口里偏上/偏下。
#define HDR_BTN_TOP_DIP 10
#define HDR_BTN_H_DIP   28

static void BuildKeys() {
    g_nk = 0;

    double dpiScale = GetSystemDpiScale();
    double baseW = 980.0 * dpiScale;
    double baseH = 320.0 * dpiScale;

    UpdateNumpadAuto(g_ww, dpiScale);   // 窄屏自动收起数字区（在算 rows / 建键位之前）

    double scaleX = (double)g_ww / baseW;
    double scaleY = (double)g_wh / baseH;

    // 页头只随 DPI 缩放，**不随窗口高度缩放** —— 标题栏不该在窗口拉高时变厚。
    // 44 = 按钮上留白 10 + 按钮 28 + 6 页头内部留白（见 HDR_BTN_* 与下面的竖向留白）。
    g_headerH = (int)(44.0 * dpiScale); if (g_headerH < 34) g_headerH = 34;
    g_keyGap = (int)(4.0 * dpiScale * scaleX); if (g_keyGap < 2) g_keyGap = 2;
    g_keyAreaX = (int)(10 * dpiScale); if (g_keyAreaX < 6) g_keyAreaX = 6;

    // 行数：全尺寸 6 行；小键盘 6 行；默认 5 行 + 可选 F1~F12 顶行
    BOOL webSurf = g_fnLayer && g_fnWebLayout && g_layoutMode == 0;
    int rows = (g_layoutMode == 2) ? 6
             : (g_layoutMode == 1) ? 6
             : 5 + (g_showFKeys && !webSurf ? 1 : 0);

    // 竖向留白统一成 10 DIP（与左右留白同节奏），基准取**页头里胶囊按钮的底边**，不是页头盒底：
    // 页头盒 44 DIP 里按钮只占 10+28=38，余下 6 DIP 是页头内部留白，以盒底为基准会让
    // 「按钮 → 第一排键」比「最后一排键 → 窗口底」小一截。
    // 行高是整数除法，余数不再全堆到底部，改为上下各分一半（误差 ≤1px）。
    const int padV   = (int)(10 * dpiScale);
    const int hdrBot = (int)((HDR_BTN_TOP_DIP + HDR_BTN_H_DIP) * dpiScale);   // 胶囊按钮底边
    int vAvail = (g_wh - padV) - (hdrBot + padV);
    if (vAvail < 20 * rows) vAvail = 20 * rows;          // 窗口极矮时兜底，别算出负键高
    vAvail -= (rows - 1) * g_keyGap;
    g_keyHeight = vAvail / rows;
    if (g_keyHeight < 20) g_keyHeight = 20;
    vAvail -= g_keyHeight * rows;
    int vRem = vAvail < 0 ? 0 : vAvail;                  // 上下各分一半的余量（px）

    // 第一排键的 y：按钮底边 + padV，再加「上下各分一半」的余量
    int y = hdrBot + padV + vRem / 2;

    if (g_layoutMode == 1) {   // 小键盘
        BuildNumpad(y);
        return;
    }

    if (g_layoutMode == 2) {   // 完整键盘（主区+导航区+数字区；Fn 网页层换 Shift 行网址键）
        BuildComplete(y, g_fnLayer && g_fnWebLayout);
        return;
    }

    // ===== 默认布局（layoutMode==0）与其 Fn 网页层：统一 1u 单位网格 =====
    // 留白与 u 互相依赖（u 要用键区宽度反解，而键区宽度要先减去留白），先联立解出 u：
    //   2×0.30u + 15.5u = g_ww + gap   →   u = (g_ww + gap) / 16.1
    // 表与逐键列位见文件上方「默认布局：1u 单位网格」那一段。
    const double u = ((double)g_ww + g_keyGap) / (DEF_ROW_U + 2 * DEF_PAD_U);
    g_keyAreaX = (int)(DEF_PAD_U * u + 0.5);
    if (g_keyAreaX < 6) g_keyAreaX = 6;

    // Fn 网页布局层（按 Fn 键切换；只有默认布局有这一层）
    if (g_fnLayer && g_fnWebLayout) {
        BuildFnSurf(y, u);
        return;
    }

    // F1~F12 顶行（可选）：Esc + F1~F12 + Del
    if (g_showFKeys) {
        DefPutRow(y, kDefRowF, kDefRowFN, u);
        y += g_keyHeight + g_keyGap;
    }

    // Row 0：数字行（开着 F 行时不再重复画 Esc，腾出的 1u 并进退格）
    DefPutRow(y, g_showFKeys ? kDefRow0NoEsc : kDefRow0,
                 g_showFKeys ? kDefRow0NoEscN : kDefRow0N, u);
    y += g_keyHeight + g_keyGap;

    // Row 1：Tab 行（开着 F 行时不再重复画 Del，腾出的 1u 并进反斜杠）
    DefPutRow(y, g_showFKeys ? kDefRow1NoDel : kDefRow1,
                 g_showFKeys ? kDefRow1NoDelN : kDefRow1N, u);
    y += g_keyHeight + g_keyGap;

    // Row 2：Caps 行
    DefPutRow(y, kDefRow2, kDefRow2N, u);
    y += g_keyHeight + g_keyGap;

    // Row 3：Shift 行（↑ 落在 12.5u 处，与下一行的 ↓ 同列）
    DefPutRow(y, kDefRow3, kDefRow3N, u);
    y += g_keyHeight + g_keyGap;

    // Row 4：底行（开着 F 行时藏 Fn，腾出的 1u 并进空格）
    DefPutRow(y, g_showFKeys ? kDefRow4NoFn : kDefRow4,
                 g_showFKeys ? kDefRow4NoFnN : kDefRow4N, u);
}

// 全尺寸布局下「数字区显隐」的唯一入口：标题栏「小键盘」按钮与 Tab 键都走这里。
// 之前两条路径各改一遍 g_npHidden，迟早分叉；收成一个函数（故放在 BuildKeys 之后）。
// ⚠ 顺序：needW 用闭式解，绝不能在改 g_ww 之前去读 g_keyGap —— 它由 g_ww 反算，
//    这时还是旧值，算出的 needW 会偏小，数字区照样被挤出去。
static void SetFullNumpadHidden(HWND hWnd, BOOL hidden) {
    g_npHidden = hidden;
    IniSetInt(L"Keyboard", L"NpHidden", hidden ? 1 : 0);
    if (!hidden) {
        // 「显示数字区」必须先保证窗口够宽：生效值 = g_npHidden || g_npHiddenAuto，
        // 而窄屏自动收起是**由窗口宽度算出来的**（ww < 1090 DIP 就收起）。
        // 旧写法只拉到 700 DIP，窗口在 700~1090 之间时 g_npHiddenAuto 仍是 TRUE，
        // 于是点标题栏那个「小键盘」按钮看起来完全没反应（实机反馈「按钮点了不更新状态」）。
        // 这里直接拉到自动收起的阈值以上，数字区才真的回得来。
        int needW = (int)(FULL_NUMPAD_MIN_W_DIP * GetSystemDpiScale()) + 8;
        if (g_ww < needW) {                     // 只加宽，不缩窄（缩回去是「手比你快」）
            g_ww = needW;
            if (hWnd && IsWindow(hWnd)) {
                SetWindowPos(hWnd, NULL, 0, 0, g_ww, g_wh,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            }
        }
    }
    BuildKeys();
    if (hWnd && IsWindow(hWnd)) InvalidateRect(hWnd, NULL, TRUE);
}

// 注册内嵌字体（MiSans Medium 精简子集，界面唯一字面）到当前进程；失败则回退系统字体
// ⚠ 子集是按「源码全部 UI 字符」生成的（tools/subset_font.py，从 build/font-backup 的
//   全量字体子集化）。**新增任何 UI 文案后必须重跑 `python tools/subset_font.py`**，
//   或先跑 `--check` 校验 —— 否则新文案里表外的字会回退系统宋体，
//   同一行 MiSans 与宋体混排（v2.0 关于页「社区交流」的「社/流」即此，实机确认）。
static void LoadEmbeddedFonts() {
    HRSRC hr = FindResourceW(g_hInst, MAKEINTRESOURCEW(IDR_FONT), MAKEINTRESOURCEW(10));  // RT_RCDATA
    if (!hr) return;
    HGLOBAL hg = LoadResource(g_hInst, hr);
    if (!hg) return;
    void* data = LockResource(hg);
    DWORD sz = SizeofResource(g_hInst, hr);
    if (!data || sz == 0) return;
    DWORD n = 0;
    HANDLE h = AddFontMemResourceEx(data, sz, NULL, &n);
    if (h && n > 0) {
        g_fontReg = h;
        g_fontReady = TRUE;
        // 文字改走 GDI+ 绘制后，GDI+ 也得能看见这个内存字体（两边各自注册一次）
        if (!g_gdipFonts) {
            g_gdipFonts = new Gdiplus::PrivateFontCollection();
            g_gdipFonts->AddMemoryFont(data, (INT)sz);
        }
    }
}
// 创建 UI 字体。界面只有一个字面（MiSans Medium），族里**不存在** 700：
// 请求 FW_BOLD 只会拿到 GDI 的仿真加粗（轮廓等距外扩），实测墨迹 +30~38%、
// 连通域 21→16（笔画被填死）、字宽 +6~13% —— 中文小字号受害最重，
// 而且「量宽用粗体、绘制用常规」会让 FitKeyFont 误判降档。
// 所以本函数**只提供 FW_NORMAL**，层级一律由「字号 + 颜色」承担（见字体显示方案 v6）。
static HFONT MakeFont(double size) {
    HDC hdc = GetDC(0);
    int h = -MulDiv((int)(size * 10 + 0.5), 96, 720);
    ReleaseDC(0, hdc);
    const wchar_t* face = g_fontReady ? L"MiSans" : L"Microsoft YaHei";
    return CreateFontW(h, 0, 0, 0, FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, face);
}

// 设置页六档：18 大标题 / 14 章节标题 / 12.5 行主文本 / 11 控件标签 / 10 行描述 / 9 元信息。
// 单字重下层级完全由「字号 + 颜色」承担，相邻档至少差 1pt。
// 基准对齐 Windows 设置页：本窗口只有 700 DIP 宽（系统设置约 1024），字号不能照抄它的绝对值，
// 但也不该按 1024 的宽窗口取值 —— 这一档比首版整体下移一档，阶梯形状不变。
// 14 这一档现在只由关于页的产品名用（制作工具同一处是 18 DIP、页标题 26，比例 0.69；
// 本窗口页标题 18，按同一比例落在 12.4 —— 但那与行主文本同号，层级就没了）。
static void InitFixedFonts() {
    double dpi = GetSystemDpiScale();
    g_sfBig  = MakeFont(18 * dpi);     // 页面大标题
    g_sfSec  = MakeFont(17 * dpi);     // 关于页产品名（章节标题已下线，这一档只剩它在用）
    g_sfRow  = MakeFont(12.5 * dpi);   // 行主文本
    g_sfCtrl = MakeFont(11 * dpi);     // 控件标签：Tab / 开关「开·关」/ 分段 / 按钮 / 弹窗
    g_sfBase = MakeFont(10 * dpi);     // 行描述
    g_sfMeta = MakeFont(9 * dpi);      // 版本号 / Copyright
}

static void RecreateFontsAndLayout() {
    if (g_f12) DeleteObject(g_f12);
    if (g_f13) DeleteObject(g_f13);
    if (g_f14) DeleteObject(g_f14);
    if (g_f10) DeleteObject(g_f10);
    if (g_f9)  DeleteObject(g_f9);
    if (g_f8)  DeleteObject(g_f8);
    if (g_f7)  DeleteObject(g_f7);
    if (g_f6)  DeleteObject(g_f6);

    double dpiScale = GetSystemDpiScale();

    // 字号跟「键高」走，不跟窗口高度走 —— 这一条就是默认布局的字号规则。
    // 先算布局拿到 g_keyHeight，再按它定字号：默认布局 5 行 @320 DIP 的键高是 48，
    // 正好是参考值（finalFontScale = dpiScale，与改动前完全一致，默认布局观感不变）；
    // 全尺寸 6 行、同样窗口高度下键更矮，字号自动按比例收回来 —— 否则键面标签会比默认
    // 布局明显偏大，挤到只能逐键降档，「字体大小不统一」就是这么来的。
    BuildKeys();

    // 字号跟「键高」走，但增长**封顶在 90 DIP**：窗口 8 向可拖拽，键高被拉到 125 DIP
    // （980×700）时按比例算出的基准字号是 64pt，而 104 DIP 宽的 Esc / Caps / Enter 可用宽
    // 只有 171px —— 五档降档全放不下，下方 GDI+ 在 NoWrap 下就把标签**整字截断**
    // （Esc → Es、Caps → Cap、Enter → Ente、Shift → Sh、Ctrl → Ct、Fn → F）。
    // 封顶后大键上的文字偏小，但标签永远完整；48~90 DIP 这一段（含全尺寸 53 DIP）比例不变。
    const double kFontRefKeyH  = 48.0;   // 默认布局（5 行 / 320 DIP）的键高，字号基准
    const double kFontGrowCapH = 90.0;   // 字号随键高增长的上限（DIP）
    double keyHDip = (double)g_keyHeight / dpiScale;
    if (keyHDip > kFontGrowCapH) keyHDip = kFontGrowCapH;

    // ⚠ 基准只能取键高，**不要再掺键宽**：试过 min(键高, 1u 键宽)，窗口拖窄时整盘字都跟着缩，
    // 实机反馈「反而字体太小」，而且修饰键的标签被挤到整字截断（Esc→Es、Del→D）。
    // 键宽不够是**逐键**问题，交给 FitKeyFont 的降档阶梯处理，不要动全局基准。
    double finalFontScale = dpiScale * (keyHDip / kFontRefKeyH);
    if (finalFontScale < 0.4 * dpiScale) finalFontScale = 0.4 * dpiScale;

    // 单字重：g_f14 是唯一主档，修饰键与普通键的区分交给底色 + 文字色（与设置页 Tab 同一套逻辑）。
    g_f12 = MakeFont((int)(12 * finalFontScale + 0.5));   // 四舍五入，别让非整数缩放累积偏差
    g_f13 = MakeFont((int)(13 * finalFontScale + 0.5));
    g_f14 = MakeFont((int)(14 * finalFontScale + 0.5));   // 主档
    // 深降档：全尺寸布局的 1u 键是**方形**（52 DIP 宽 × 53 DIP 高），字号却按键高算，
    // 于是「PrtSc / ScrLk / Pause / Home / Enter」这类 4~5 字标签在 12pt 档也放不下，
    // GDI+ NoWrap 会**整字丢弃尾部**（PrtSc → PrtS、Home → Hom、Pause → Pa）。
    // 实测（MiSans-Medium，em 36/33/30/25/22）：1u 可用宽 80px 时，「Home」需要
    //   97(f14) / 91(f13) / 87(f12) / 81(f11) / 74(f10) / 68(f9)
    // —— 12pt 地板不够，必须在下面再补两档，否则这些键只能画残缺标签。
    g_f10 = MakeFont((int)(10 * finalFontScale + 0.5));
    g_f9  = MakeFont((int)( 9 * finalFontScale + 0.5));
    // 更深的兜底三档：键「宽」不跟着窗口长高，所以把窗口拖得极高时，1u 键会变成
    // 「窄而高」（1280×900 DIP 时键宽 99px、高 238px）。这时即使字号增长已封顶在
    // 90 DIP，9pt 档仍放不下 PrtSc / Home / PgUp（实测 116px > 89px 可用宽）。
    // 阶梯一路降到 6pt 才能保证「任何尺寸下标签都完整」；正常尺寸下前几档就已命中，
    // 不会走到这里（全尺寸 2240×707 的出图在加深阶梯前后逐字节相同）。
    g_f8 = MakeFont((int)(8 * finalFontScale + 0.5));
    g_f7 = MakeFont((int)(7 * finalFontScale + 0.5));
    g_f6 = MakeFont((int)(6 * finalFontScale + 0.5));

    // 「Backspace」在最小字号档（g_f12）下的实际容纳宽，字体一改就重量一次。
    // 退格标签要不要缩写成 Bksp**只能**跟这个实测值比：
    //   字号是跟键高走的（finalFontScale = dpiScale × keyHeight/48），而窗口 8 向可拖拽，
    //   把键盘拖高之后同一个键宽就放不下整词了 —— 此时 GDI+ 在 NoWrap 下**把尾部字符整字丢掉**
    //   （「Backspace」直接变成「Backsp」，不是裁一半），实机反馈的「退格文本放不下」就是它。
    //   反过来按 DIP 画条线（曾经写过 `k->w >= 90*dpi`）等于偷偷假设「键高 ≈ 49 DIP」，必翻车。
    {
        HDC dc = GetDC(0);
        g_bkspTextW = MeasureTextW(dc, L"Backspace", g_f12);
        ReleaseDC(0, dc);
    }
}

static void Fill(HDC dc, int x, int y, int w, int h, DWORD c) {
    if (w <= 0 || h <= 0) return;
    Gdiplus::Graphics graphics(dc);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)));
    graphics.FillRectangle(&brush, x, y, w, h);
}

static void DrawRoundRectAlpha(HDC dc, int x, int y, int w, int h, DWORD fillC,
                               DWORD borderC, int radius, BYTE fillAlpha, BYTE borderAlpha) {
    if (w <= 0 || h <= 0) return;
    float r = (float)radius;
    float maxR = ((float)(w < h ? w : h) - 1.0f) / 2.0f;
    if (r < 1.0f) r = 1.0f;
    if (r > maxR) r = maxR;

    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::GraphicsPath path;
    float d = r * 2.0f;
    float fx = (float)x + 0.5f, fy = (float)y + 0.5f;
    float fw = (float)w - 1.0f, fh = (float)h - 1.0f;
    path.AddArc(fx, fy, d, d, 180.0f, 90.0f);
    path.AddArc(fx + fw - d, fy, d, d, 270.0f, 90.0f);
    path.AddArc(fx + fw - d, fy + fh - d, d, d, 0.0f, 90.0f);
    path.AddArc(fx, fy + fh - d, d, d, 90.0f, 90.0f);
    path.CloseFigure();

    Gdiplus::Color fill(fillAlpha, GetRValue(fillC), GetGValue(fillC), GetBValue(fillC));
    Gdiplus::Color border(borderAlpha, GetRValue(borderC), GetGValue(borderC), GetBValue(borderC));
    Gdiplus::SolidBrush brush(fill);
    Gdiplus::Pen pen(border, 1.0f);
    g.FillPath(&brush, &path);
    g.DrawPath(&pen, &path);
}

static void DrawRoundRect(HDC dc, int x, int y, int w, int h, DWORD fillC, DWORD borderC, int radius) {
    DrawRoundRectAlpha(dc, x, y, w, h, fillC, borderC, radius, 255, 255);
}

typedef HRESULT (WINAPI *DwmSetWindowAttributeProc)(HWND, DWORD, LPCVOID, DWORD);

static DwmSetWindowAttributeProc GetDwmSetWindowAttribute() {
    static HMODULE dwm = NULL;
    static DwmSetWindowAttributeProc proc = NULL;
    static BOOL initialized = FALSE;
    if (!initialized) {
        initialized = TRUE;
        dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) proc = (DwmSetWindowAttributeProc)GetProcAddress(dwm, "DwmSetWindowAttribute");
    }
    return proc;
}

static BOOL TryApplyWin11RoundedWindow(HWND hWnd) {
    DwmSetWindowAttributeProc setAttr = GetDwmSetWindowAttribute();
    if (!setAttr) return FALSE;
    const DWORD DWMWA_WINDOW_CORNER_PREFERENCE_VALUE = 33;
    const int DWMWCP_ROUND_VALUE = 2;
    HRESULT hr = setAttr(hWnd, DWMWA_WINDOW_CORNER_PREFERENCE_VALUE,
                         &DWMWCP_ROUND_VALUE, sizeof(DWMWCP_ROUND_VALUE));
    if (FAILED(hr)) return FALSE;
    SetWindowRgn(hWnd, NULL, TRUE);   // 清除旧式区域裁剪，交由 DWM 平滑合成圆角
    return TRUE;
}

static void ApplyRoundedWindow(HWND hWnd, int logicalRadius) {
    (void)logicalRadius;
    // Win11+ 保留系统原生 DWM 圆角；Win10 及以下沿用系统样式（直角）。
    // 圆角观感主要由面板 16px / 键帽 8px 的内圆角提供，窗口外框跟随系统即可。
    if (g_winBuild >= 22000) {
        if (TryApplyWin11RoundedWindow(hWnd)) return;
    }
    SetWindowRgn(hWnd, NULL, TRUE);
}

// 窗口离屏画布（双缓冲）。文字不再单独走蒙版合成：材质早已移除，目标就是不透明的
// 32bpp DIB，直接用 GDI+ 画上去即可（见 DrawTextGp）。
struct WindowPaintSurfaceLocal {
    HDC dc;
    HDC memory;
    HBITMAP bitmap;
    HBITMAP oldBitmap;
};

// 所有窗口统一使用 32bpp 顶向下 DIB 画布（双缓冲）：浅色 / 深色 / XP / Win11 观感一致。
static WindowPaintSurfaceLocal BeginWindowPaintSurface(HDC target, HWND hWnd, const RECT& rc) {
    WindowPaintSurfaceLocal surface = {};
    (void)hWnd;

    int w = rc.right - rc.left, h = rc.bottom - rc.top;
    if (w > 0 && h > 0) {
        BITMAPINFO bmi = {};
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        void* bits = NULL;
        surface.memory = CreateCompatibleDC(target);
        surface.bitmap = CreateDIBSection(target, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
        if (surface.memory && surface.bitmap && bits) {
            surface.oldBitmap = (HBITMAP)SelectObject(surface.memory, surface.bitmap);
            surface.dc = surface.memory;
            return surface;
        }
        // DIB 创建失败：回退普通兼容位图（GDI 直绘文字）
        if (surface.memory) { DeleteDC(surface.memory); surface.memory = NULL; }
        if (surface.bitmap) { DeleteObject(surface.bitmap); surface.bitmap = NULL; }
    }

    surface.memory = CreateCompatibleDC(target);
    surface.bitmap = CreateCompatibleBitmap(target, w, h);
    surface.oldBitmap = (HBITMAP)SelectObject(surface.memory, surface.bitmap);
    surface.dc = surface.memory;
    return surface;
}

static void EndWindowPaintSurface(WindowPaintSurfaceLocal* surface) {
    if (!surface) return;
    if (surface->memory) {
        SelectObject(surface->memory, surface->oldBitmap);
        if (surface->bitmap) DeleteObject(surface->bitmap);
        DeleteDC(surface->memory);
    }
}

// 统一铺不透明面板底色（page-bg），全部内容都画在它上面
static void ClearWindowBackBuffer(HDC dc, HWND hWnd, int w, int h) {
    (void)hWnd;
    Fill(dc, 0, 0, w, h, C_BG);
}

// 检测系统版本（RtlGetVersion 不受兼容性清单影响）
// Win11 = Build 22000+（窗口保留系统原生圆角）；其余版本沿用系统样式（直角）
typedef LONG (WINAPI *RtlGetVersionFn)(OSVERSIONINFOW *);
static void DetectWinVersion() {
    HMODULE nt = GetModuleHandleW(L"ntdll.dll");
    RtlGetVersionFn fn = nt ? (RtlGetVersionFn)GetProcAddress(nt, "RtlGetVersion") : NULL;
    OSVERSIONINFOW vi;
    ZeroMemory(&vi, sizeof(vi));
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (fn && fn(&vi) == 0 && vi.dwBuildNumber > 0) {
        g_winBuild = vi.dwBuildNumber;
    } else {
        OSVERSIONINFOW fallback;
        ZeroMemory(&fallback, sizeof(fallback));
        fallback.dwOSVersionInfoSize = sizeof(fallback);
        if (GetVersionExW(&fallback)) g_winBuild = fallback.dwBuildNumber;
    }
}

// 主界面透明度（分层窗口统一透明）：Win2000+ 均支持，所有系统都可用
static void ApplyWindowOpacity(HWND hWnd, BOOL enable) {
    if (!hWnd || !IsWindow(hWnd)) return;
    LONG ex = GetWindowLongW(hWnd, GWL_EXSTYLE);
    BOOL layered = (ex & WS_EX_LAYERED) != 0;
    if (enable) {
        if (!layered) SetWindowLongW(hWnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        BYTE a = (BYTE)(g_mainOpacity * 255 / 100);
        SetLayeredWindowAttributes(hWnd, 0, a, LWA_ALPHA);
    } else if (layered) {
        SetWindowLongW(hWnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    }
}

// ===== 统一文字绘制：GDI+ 无网格拟合抗锯齿 =====
// GDI 的 TrueType 光栅器把笔画 snap 到整数像素，于是「字号连续变化、笔画粗细不连续」：
// 实测竖画/em 在 13→14 DIP 骤降 22%，12 DIP 甚至比 13 更粗 —— 单字重下无法建立层级。
// GDI+ 的 AntiAlias 不做网格拟合，笔画按 em 线性缩放（实测波动 ±5%）。
// 从 HFONT 取 em 像素高：GDI 的 lfHeight 负值就是 em 高，与 GDI+ UnitPixel 同义。
static float FontEmPx(HFONT f) {
    LOGFONTW lf = {};
    if (!f || !GetObjectW(f, sizeof(lf), &lf)) return 0.0f;
    return (float)(lf.lfHeight < 0 ? -lf.lfHeight : lf.lfHeight);
}

// 拉丁小写里带下降部的字符。这类串的墨迹本来就低，垂直补偿要另算（见 TextInkShiftPx）。
static BOOL TextHasDescender(const wchar_t* s) {
    if (!s) return FALSE;
    for (const wchar_t* p = s; *p; ++p) {
        wchar_t c = *p;
        if (c == L'g' || c == L'j' || c == L'p' || c == L'q' || c == L'y') return TRUE;
    }
    return FALSE;
}

// 单行垂直居中时，把文字矩形下移这个量，墨迹中心才落到矩形中心上。
//
// 起因：GDI+ 的 LineAlign=Center 居中「行盒」，实测基线固定在「矩形中心 + 0.2936em」
// （MiSans-Medium hhea asc/desc = 1.044/0.282em，行盒 1.326em）。而汉字与无下降部的拉丁字
// 并不占满下降部，墨迹中心因此比矩形中心高（实测，24pt 归一化）：
//     汉字（设置/自动呼出/小键盘）  -0.089 ~ -0.107 em
//     大写·数字·无降部拉丁（A/1/Tab/Del）  -0.071 ~ -0.080 em
//     带下降部（Caps/Backspace/Settings/Layout）  +0.018 ~ +0.027 em
// 而**图标是按自身包围盒居中的**（`DrawHkIcon` 的路径上下对称），于是只要文字不补偿，
// 「图标 + 文字」这一对就整体对不齐 —— 实机反馈的「图标文本对不齐」即此。
// 两类各取一个折中值：无下降部 +0.085em（下移），有下降部 -0.025em（≈ 不下移）。
static int TextInkShiftPx(float emPx, const wchar_t* s) {
    if (emPx <= 0.0f) return 0;
    float v = emPx * (TextHasDescender(s) ? -0.025f : 0.085f);
    return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

// GDI+ 单行水平对齐模式。贴边标签必须用 Near/Far 而不是 Center：
// 见 DrawTextKey 里关于「矩形边 vs 墨迹边」的说明。
enum TextAlignMode { TAL_Near = 0, TAL_Center = 1, TAL_Far = 2 };

static BOOL DrawTextGp(HDC dc, int x, int y, int w, int h, const wchar_t* s,
                       float emPx, DWORD color, int align, BOOL wrap) {
    if (!s || !s[0] || w <= 0 || h <= 0 || emPx <= 0.0f) return FALSE;
    if (!g_gdipFonts) return FALSE;          // GDI+ 私有字体未就绪 → 调用方回退 GDI 直绘

    Gdiplus::Graphics g(dc);
    // 高 DPI 用无 gridfit 的 AntiAlias（笔画线性、层级可预测）；
    // 低 DPI（<150%）小字号太糊，退回带 gridfit 的 AntiAliasGridFit 保清晰。
    g.SetTextRenderingHint(GetSystemDpiScale() >= 1.5
        ? Gdiplus::TextRenderingHintAntiAlias
        : Gdiplus::TextRenderingHintAntiAliasGridFit);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);

    Gdiplus::FontFamily family(g_fontReady ? L"MiSans" : L"Microsoft YaHei", g_gdipFonts);
    if (!family.IsAvailable()) return FALSE;
    Gdiplus::Font font(&family, emPx, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    if (font.GetLastStatus() != Gdiplus::Ok) return FALSE;

    Gdiplus::StringFormat fmt;
    fmt.SetFormatFlags(Gdiplus::StringFormatFlagsNoClip);   // 下降部不被裁
    if (!wrap)
        fmt.SetFormatFlags((Gdiplus::StringFormatFlags)(fmt.GetFormatFlags()
                                                        | Gdiplus::StringFormatFlagsNoWrap));
    fmt.SetAlignment(align == TAL_Center ? Gdiplus::StringAlignmentCenter
                    : align == TAL_Far    ? Gdiplus::StringAlignmentFar
                                          : Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color),
                                                  GetGValue(color), GetBValue(color)));
    Gdiplus::RectF rc((Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)w, (Gdiplus::REAL)h);
    // 垂直居中时补偿「行盒中心 vs 墨迹中心」的差（见 TextInkShiftPx）。
    // 不做这一步，所有居中文字都会整体偏高 1~3px；和图标并排时最刺眼。
    // 折行文本同样要补：首行的上升部与末行的下降部一样没占满行盒，偏移是同一个量
    // （唯一的折行调用点是设置行的长描述，补上它才与不折行的描述同高）。
    rc.Y += (Gdiplus::REAL)TextInkShiftPx(emPx, s);
    g.DrawString(s, -1, &font, rc, &fmt, &brush);
    return TRUE;    // Graphics 析构时自动 Flush
}

static void DrawTextC(HDC dc, int x, int y, int w, int h, const wchar_t* s, HFONT f, DWORD c) {
    if (DrawTextGp(dc, x, y, w, h, s, FontEmPx(f), c, TAL_Center, FALSE)) return;
    RECT r = {x, y, x + w, y + h};        // 回退：GDI+ 未就绪 / 字体不可用
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// 左对齐版。原定义在设置页那一段，因 DrawTextKey 的贴边标签要用而提到这里
// （DrawTextC / DrawTextR / DrawTextL 三者相邻，贴边与居中的行为差异一眼可比）。
static void DrawTextL(HDC dc, int x, int y, int w, int h, const wchar_t* s, HFONT f, DWORD c,
                      BOOL wrap = FALSE) {
    if (DrawTextGp(dc, x, y, w, h, s, FontEmPx(f), c, TAL_Near, wrap)) return;
    RECT r = {x, y, x + w, y + h};        // 回退：GDI+ 未就绪 / 字体不可用
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, (wrap ? (DT_LEFT | DT_WORDBREAK | DT_TOP)
                                   : (DT_LEFT | DT_VCENTER | DT_SINGLELINE)) | DT_NOPREFIX);
}

// 右对齐版（贴右边缘的键面标签用，与 DrawTextL 成对）。
static void DrawTextR(HDC dc, int x, int y, int w, int h, const wchar_t* s, HFONT f, DWORD c) {
    if (DrawTextGp(dc, x, y, w, h, s, FontEmPx(f), c, TAL_Far, FALSE)) return;
    RECT r = {x, y, x + w, y + h};        // 回退：GDI+ 未就绪 / 字体不可用
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// 按键的对齐方式把标签摆到键面的左端 / 右端（k->align，见 KeyAlign）。
// 参考图里左列的 Esc / Tab / Caps / Shift 名字贴着左边缘、右列的 Backspace / Del /
// Enter / Shift 贴着右边缘 —— 宽键居中会让名字独自飘在一条缝上，反而看不出是哪个键。
//
// ⚠ 贴边锚点只有一个：DrawKeyLabel（图标+文字那一路）的 edge = 6 DIP。
//   两条路径必须共用同一个值，否则同一列的 Esc（纯文字）与 Tab / Caps（带图标）
//   留白会差一倍 —— 这是踩过的坑，记在这里免得下次又改回去。
//
// ⚠ 贴边看的是**墨迹边缘**，所以这里不能用 DrawTextC（它在矩形里居中）：
//   DrawTextC 会按 MeasureTextW 的「容纳宽度」给矩形，而容纳宽度比实际推进宽约
//   0.2em（175% DPI 下 ≈ 10px），居中之后墨迹又往键心缩回去半个余量 ——
//   Esc 的墨迹离键左因此变成 inset(10 DIP) + 10px ≈ 27px，而带图标的 Tab 只有 14px。
//   故左对齐用 DrawTextL、右对齐用 DrawTextR（GDI+ Near / Far），让墨迹边直接贴上去。
//
// ⚠ 但 Near / Far 对齐的是 GDI+ 的「容纳框」，它比推进框（advance）两侧各宽
//   约 0.2em / 2 ≈ 5px；不补偿的话右贴边会凭空多出这 5px（实测 Del 的右侧留白
//   从 11 涨到 18px）。故把 (tw − adv) / 2 这圈内缩量加回矩形，让对齐边落在
//   「推进边」上 —— 与 DrawKeyLabel 里「整组按 adv 计算宽度」是同一个坐标基准，
//   两条路径的贴边留白因此严格一致（都等于 edge + 该字形自身的边距）。
//   （曾经只把右侧改成「减去容纳宽度」，结果左 24 / 右 11，图标键与文字键分了家，
//     木已成舟地劣化了默认档 —— 那个提交已被回退，切勿再走那条路。）
static void DrawTextKey(HDC dc, const KeyDef* k, const wchar_t* s, HFONT f, DWORD c) {
    if (!s || !s[0]) return;

    if (k->align == KA_CENTER) {              // 居中：整键矩形，与双符号键同源
        DrawTextC(dc, k->x, k->y, k->w, k->h, s, f, c);
        return;
    }

    // 贴边留白：与 DrawKeyLabel 的 edge 同一常量。
    int edge = (int)(6 * GetSystemDpiScale());

    int tw  = MeasureTextW(dc, s, f);         // 容纳宽度（含 GDI+ 两侧留白）
    int adv = MeasureTextAdvW(dc, s, f);      // 推进宽度（真实布局宽度）
    if (tw <= 0) return;
    if (adv <= 0) adv = tw;
    int p = (tw > adv) ? (tw - adv) / 2 : 0;  // 容纳框相对推进框的单侧内缩量

    int x = k->x + edge - p;
    int w = k->w - 2 * edge + 2 * p;
    if (w < 8) { x = k->x; w = k->w; }        // 极窄键（理论上不会出现）退回整键
    if (k->align == KA_LEFT) DrawTextL(dc, x, k->y, w, k->h, s, f, c);
    else                     DrawTextR(dc, x, k->y, w, k->h, s, f, c);
}

// 双符号键绘制：上=副符号（Shift 未触发时灰色，触发后白色），下=主字符（始终正常显示）
static void DrawKeyDual(HDC dc, int x, int y, int w, int h,
                        wchar_t baseCh, wchar_t shiftCh,
                        HFONT fBase, HFONT fShift, DWORD baseC, DWORD shiftC) {
    wchar_t buf[2] = {0, 0};

    // 副符号（键上半部）
    buf[0] = shiftCh;
    RECT rt = {x, y, x + w, y + h / 2};
    if (!DrawTextGp(dc, rt.left, rt.top, rt.right - rt.left, rt.bottom - rt.top,
                          buf, FontEmPx(fShift), shiftC, TAL_Center, FALSE)) {
        SelectObject(dc, fShift);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, shiftC);
        DrawTextW(dc, buf, -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 主字符（键下半部）
    buf[0] = baseCh;
    RECT rb = {x, y + h / 2, x + w, y + h};
    if (!DrawTextGp(dc, rb.left, rb.top, rb.right - rb.left, rb.bottom - rb.top,
                          buf, FontEmPx(fBase), baseC, TAL_Center, FALSE)) {
        SelectObject(dc, fBase);
        SetTextColor(dc, baseC);
        DrawTextW(dc, buf, -1, &rb, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

// ===== 自绘图标运行层（替代图标字体）=====
// 几何来自 hk_icons_generated.h，与 PanDaPE-Maker 的 icons.rs 同源（24 网格 / 1.9 描边 / round）。
// 收益：任意尺寸、任意色相都可用，加一个图标＝加一段顶点数据，不用重跑字体子集化。
static const HkIconDef& HkIcon(int id) { return k_hkIcons[id]; }

// ⚠ 文字宽度有**两个不能混用**的用途：
//   · 容纳宽度 = GDI+ MeasureString（本函数）—— DrawString 在 NoWrap 下按这个宽度判断放不放得下，
//     所以「绘制矩形的宽」必须 ≥ 它；量出来的值比 advance 两侧各多约 0.2em。
//   · 布局宽度 = advance（用 MeasureTextAdvW）—— 排版 / 居中 / 图标与文字的间距必须用它，
//     否则那 0.2em 的余量会被当成真实文字宽度：图标与文字之间凭空多出空隙、整组还会偏左
//     （标题栏 pill 实测：图标↔文字间距 19px，而设计值是 10px）。
//
// 实测（「常规」，11pt @175%，MiSans-Medium）：
//   GDI 量 52px（26px/字，与 hmtx 的 1000/1000 = 1.0em 一致，这是 advance，是**对的**）
//   GDI+ 需要 61.4px（30.7px/字）—— 多出的是 GDI+ 自己的保守余量。
// 历史事故：Tab 矩形按 GDI 量出 52 + 4dip = 59px 当绘制宽度，GDI+ 判定放不下两个字，
// 四个 Tab 各只剩第一个字（常规/布局/主题/关于 → 常/布/主/关）。
static int MeasureTextGp(HDC dc, const wchar_t* s, HFONT f) {
    if (!s || !s[0] || !f || !g_gdipFonts) return 0;
    float emPx = FontEmPx(f);
    if (emPx <= 0.0f) return 0;

    Gdiplus::Graphics g(dc);
    Gdiplus::FontFamily family(g_fontReady ? L"MiSans" : L"Microsoft YaHei", g_gdipFonts);
    if (!family.IsAvailable()) return 0;
    Gdiplus::Font font(&family, emPx, Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
    if (font.GetLastStatus() != Gdiplus::Ok) return 0;

    // 与 DrawTextGp 的单行分支用同一组 flag，否则量出来的宽度和绘制的判定条件不一致
    Gdiplus::StringFormat fmt;
    fmt.SetFormatFlags((Gdiplus::StringFormatFlags)
                       (Gdiplus::StringFormatFlagsNoWrap | Gdiplus::StringFormatFlagsNoClip));

    Gdiplus::RectF box(0.0f, 0.0f, 4096.0f, emPx * 4.0f);
    Gdiplus::RectF out;
    if (g.MeasureString(s, -1, &font, box, &fmt, &out) != Gdiplus::Ok) return 0;

    int w = (int)out.Width;                 // 向上取整：整数宽度才够 DrawString 判定「放得下」
    if ((float)w < out.Width) w++;
    return w > 0 ? w + 1 : 0;               // 再留 1px 呼吸空间
}

// 布局宽度：GDI 的 GetTextExtentPoint32W 返回的理论推进宽（advance），即「文字实际占多宽」。
// 排版、居中、图标与文字的间距都用它 —— DrawString 的绘制宽度不要用它，会丢字。
static int MeasureTextAdvW(HDC dc, const wchar_t* s, HFONT f) {
    if (!s || !s[0] || !f) return 0;
    HFONT old = (HFONT)SelectObject(dc, f);
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
    SelectObject(dc, old);
    return sz.cx;
}

// 容纳宽度：绘制矩形的宽必须 ≥ 它（见上方说明）。所有历史调用点用的都是这个语义。
static int MeasureTextW(HDC dc, const wchar_t* s, HFONT f) {
    if (!s || !s[0] || !f) return 0;

    int gp = MeasureTextGp(dc, s, f);       // 首选：与绘制同源
    if (gp > 0) return gp;

    // 回退：GDI+ 未就绪（启动早期 g_gdipFonts 还没建）时仍用 GDI。
    // 此时文字也画不了 GDI+，会走 DrawTextL/C 的 GDI 回退分支，两边依然同源。
    return MeasureTextAdvW(dc, s, f);
}

// 键面标签自适应字号：救「PrtSc / ScrLk / Pause / Home / PgUp / PgDn / Enter」这类
// 塞不进 1u 键的长标签。全尺寸布局的导航区与数字区键宽只有与字母键相同的 1u。
//
// 阶梯五档：基础档 → 13 → 12 → 10 → 9（9 是地板）。
//
// ⚠ 这里曾经「刻意只有三档」（地板 12），理由是「掉到 9pt 会让同一排导航键冒出 4 种字号」。
//   但那个结论是按 52px 键宽算的（注释里「最宽的 Pause 约 48px，仍在 52px 键内」），
//   而实机 1u 是 ~90px、可用宽 80px —— 12pt 档下 Home 要 87px、Pause 要 98px，
//   照样放不下，GDI+ NoWrap 会把尾部**整字丢掉**（实机截图：PrtSc→PrtS、Home→Hom、Pause→Pa）。
//   残缺标签比「同一排字号不齐」严重得多，所以补回 10 / 9 两档：
//   每个键取「放得下的最大档」，放得下的键仍停在 14，只有真正长的标签才往下掉。
// 真正决定观感的是上面 RecreateFontsAndLayout 的字号缩放：字号跟键高走，这里只是收尾。
static HFONT FitKeyFont(HDC dc, const wchar_t* s, int maxW) {
    if (!s || !s[0] || maxW <= 0) return g_f14;
    // 8 档降档阶梯：正常尺寸命中前几档，越靠后只在「键窄而窗口极高」时才用得上。
    // 地板给到 6pt 是为了兑现「标签永远完整」（见 RecreateFontsAndLayout 里的说明）。
    HFONT ladder[8] = { g_f14, g_f13, g_f12, g_f10, g_f9, g_f8, g_f7, g_f6 };
    for (int i = 0; i < 8; i++)
        if (ladder[i] && MeasureTextW(dc, s, ladder[i]) <= maxW) return ladder[i];
    return g_f6;
}

// 图标定义在网格坐标里，描边宽度也是网格单位：缩放后必须把 pen 宽度乘回 k，
// 否则 16px 图标的线会连带缩细，与制作工具的观感对不上。
static void DrawHkIcon(HDC dc, float x, float y, float size,
                       const HkIconDef& def, DWORD fg, DWORD hole) {
    if (!def.nodes || def.grid <= 0.0f || size <= 0.0f) return;

    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

    const float k = size / def.grid;
    Gdiplus::Matrix mat(k, 0.0f, 0.0f, k, x, y);

    const HkIconNode* n = def.nodes;
    while (n->op != HKIC_END) {
        if (n->op != HKIC_BEGIN) { ++n; continue; }
        HkIconPaint paint = (HkIconPaint)(int)n->a;
        float strokeW = n->b > 0.0f ? n->b : 1.9f;
        DWORD color = ((int)n->c) ? hole : fg;
        ++n;

        Gdiplus::GraphicsPath path;
        float px = 0.0f, py = 0.0f;
        BOOL have = FALSE;   // 当前子路径是否已有可用起点
        while (n->op != HKIC_BEGIN && n->op != HKIC_END) {
            switch (n->op) {
            case HKIC_MOVE:
                // ⛔ GDI+ 的 GraphicsPath::AddLine 是「接到当前点」的：同一个 <path> 里第二个
                // 子路径若直接 AddLine，会被从上一个子路径的终点拉一条线连过来（符号和汉堡都
                // 因此画错 —— X 多一条边、三条杠之间多两道斜线）。换子路径必须先 StartFigure()
                // 显式断开。实测 PathTypes 才是判据：断开前 0,1,1,1；断开后 0,1,0,1。
                if (path.GetPointCount() > 0) path.StartFigure();
                px = n->a; py = n->b; have = TRUE;
                break;
            case HKIC_LINE:
                if (have) path.AddLine(px, py, n->a, n->b);
                px = n->a; py = n->b; have = TRUE;
                break;
            case HKIC_CUBIC:
                if (have) path.AddBezier(px, py, n->a, n->b, n->c, n->d, n->e, n->f);
                px = n->e; py = n->f; have = TRUE;
                break;
            case HKIC_CLOSE:
                if (path.GetPointCount() > 0) path.CloseFigure();
                break;
            default:
                break;
            }
            ++n;
        }
        if (path.GetPointCount() <= 0) continue;
        path.Transform(&mat);

        if (paint == HKIC_FILL) {
            Gdiplus::SolidBrush brush(GpColorFromBgr(color));
            g.FillPath(&brush, &path);
        } else {
            Gdiplus::Pen pen(GpColorFromBgr(color), (Gdiplus::REAL)(k * strokeW));
            pen.SetLineCap(Gdiplus::LineCapRound, Gdiplus::LineCapRound, Gdiplus::DashCapRound);
            pen.SetLineJoin(Gdiplus::LineJoinRound);
            g.DrawPath(&pen, &path);
        }
    }
}

// 图标 tile：圆角 10、底 C_REGULAR，内容为 iconSize 的矢量图标
// （iconId < 0 时画 text —— 功能键行那类没有对应图标、但需要方块的行用它）
static void DrawIconTile(HDC dc, int x, int y, int size, int iconSize, int iconId, const wchar_t* text) {
    DrawRoundRect(dc, x, y, size, size, C_REGULAR, C_REGULAR, (int)(10 * GetSystemDpiScale()));
    if (iconId >= 0) {
        int pad = (size - iconSize) / 2;
        DrawHkIcon(dc, (float)(x + pad), (float)(y + pad), (float)iconSize,
                   HkIcon(iconId), C_BTN_CONTENT, C_BTN_CONTENT);
    } else if (text) {
        DrawTextC(dc, x, y, size, size, text, g_sfCtrl, C_BTN_CONTENT);
    }
}

// 键 → 图标；返回 NULL 表示该键不参与图标化（永远走文字）
// 硬规则：双符号键、字母数字、F1~F12、Esc/Ctrl/Alt/Fn 及各缩写键都没有公认图形，保持文字。
static const HkIconDef* KeyIconFor(const KeyDef* k) {
    if (!k) return NULL;
    if (k->type == K_HIDE) return &HkIcon(HKICON_CHEVRONDOWN);
    switch (k->vk) {
    case 0x5D: return &HkIcon(HKICON_HAMBURGER);                        // Menu
    case 0x10: case 0xA0: case 0xA1: return &HkIcon(HKICON_SHIFT);      // Shift
    case 0x14: return &HkIcon(HKICON_CAPSLOCK);                         // Caps
    case 0x09: return &HkIcon(HKICON_TAB);                              // Tab
    case 0x0D: return &HkIcon(HKICON_ENTER);                            // Enter
    case 0x08: return &HkIcon(HKICON_BACKSPACE);                        // 退格
    case 0x25: return &HkIcon(HKICON_ARROWLEFT);
    case 0x26: return &HkIcon(HKICON_ARROWUP);
    case 0x27: return &HkIcon(HKICON_ARROWRIGHT);
    case 0x28: return &HkIcon(HKICON_ARROWDOWN);
    }
    return NULL;
}

// 这个键有没有可画的图形。Win 键是特例：Windows 徽标字体里没有对应字形，
// 一直是矢量特绘（见 DrawKeyGlyph），所以它不在 KeyIconFor 的 switch 里。
static BOOL KeyHasGlyph(const KeyDef* k) {
    if (!k) return FALSE;
    if (k->vk == 0x5B) return TRUE;                     // Win
    return KeyIconFor(k) != NULL;
}

// 键面图形。size 是正方形边长（与图标盒同义），图形在盒内居中。
static void DrawKeyGlyph(HDC dc, int x, int y, int size, const KeyDef* k, DWORD color) {
    if (!k || size <= 0) return;
    if (k->vk == 0x5B) {
        // Win11 风格四格徽标：四块圆角方块 + 中间一条缝。
        // ⚠ 外框要比 size 收一圈：Menu 的三条杠在 24 网格里只占 2..22（≈83%），
        //    而这里原来铺满整个 size —— 同样 size 下 Win 明显比 Menu 满一圈
        //    （实机反馈「Menu 键和 Win 键的图标大小需一致」）。按 0.78 缩后再居中。
        int box = (int)(size * 0.78 + 0.5);
        int sq = (int)(box * 0.44 + 0.5);
        int gp = (int)(box * 0.12 + 0.5);
        if (sq < 2) sq = 2;
        if (gp < 1) gp = 1;
        int total = sq * 2 + gp;
        int ox = x + (size - total) / 2;
        int oy = y + (size - total) / 2;
        int rr = (int)(sq * 0.18);
        DrawRoundRect(dc, ox, oy, sq, sq, color, color, rr);
        DrawRoundRect(dc, ox + sq + gp, oy, sq, sq, color, color, rr);
        DrawRoundRect(dc, ox, oy + sq + gp, sq, sq, color, color, rr);
        DrawRoundRect(dc, ox + sq + gp, oy + sq + gp, sq, sq, color, color, rr);
        return;
    }
    const HkIconDef* ic = KeyIconFor(k);
    if (ic) DrawHkIcon(dc, (float)x, (float)y, (float)size, *ic, color, color);
}

// 键面标签的图标形态；返回 FALSE 表示该键不参与图标化（调用方继续走文字路径）。
// 颜色只有 textC 一个来源，所以「普通/修饰/按下」三态与深色主题都自动跟随，零分支。
// 注意：局部变量不要叫 small / pure —— <windows.h> 的 rpcndr.h 里有 `#define small char`。
//
// 三档落位（默认「图标+文字」，见 g_keyIconStyle）：
//   1) 图标 + 文字并排；宽度不够就**逐档降字号**（用户要求「挤不了就缩小字体」）；
//   2) 一路降到最小档仍放不下 → 只画图形（用户明确允许的最后一档，也是方向键的常态）；
//   3) 该键本身没有公认图形 → 返回 FALSE，交回纯文字路径。
// 旧实现里没有第 1 档的降字号：放不下就直接退回纯文字，于是参考图上
// 「⌫ Backspace / ⇥ Tab / ⇪ Caps」这类「图形 + 名字」的键全都丢了图形。
static BOOL DrawKeyLabel(HDC dc, const KeyDef* k, HFONT f, const wchar_t* text, DWORD color) {
    if (!KeyHasGlyph(k)) return FALSE;

    // 尺寸一律**按键高**取值（原来写死 15/20 DIP）：窗口缩小、键帽变矮时，固定尺寸的图标
    // 会挤掉同排文字，[图标 + 文字] 放不下就只能退成纯图标 —— 即缩小窗口后的图标显示异常。
    // 用键高做基准，DPI 与窗口缩放天然同步（48 DIP 键 ≈ 原来的 15/20 DIP）。
    int iconInline = k->h * 31 / 100;
    int iconOnly   = k->h * 42 / 100;
    int gap        = k->h * 8 / 100;
    int edge       = k->h * 12 / 100;   // 「图标+文字」整组两侧的呼吸边距

    // 方向键：键面标签本身就是那个箭头，画成「图标 + ←」等于同一个信息写两遍，只画图形。
    BOOL hasText = (k->type != K_ARROW) && text && text[0];

    if (hasText) {
        // 降档阶梯：从调用方已经挑好的档位开始往下找第一个放得下的组合。
        HFONT ladder[8] = { f, g_f13, g_f12, g_f10, g_f9, g_f8, g_f7, g_f6 };
        for (int i = 0; i < 8; i++) {
            HFONT ff = ladder[i];
            if (!ff) continue;
            int tw = MeasureTextW(dc, text, ff);      // 容纳宽度：绘制矩形的宽必须 ≥ 它
            if (tw <= 0) continue;
            if (iconInline + gap + tw > k->w - 2 * edge) continue;

            int adv = MeasureTextAdvW(dc, text, ff);  // 布局宽度：整组按它居中
            int drawn = (adv > 0) ? adv : tw;
            int groupW = iconInline + gap + drawn;
            int gx;
            if (k->align == KA_LEFT)       gx = k->x + edge;
            else if (k->align == KA_RIGHT) gx = k->x + k->w - edge - groupW;
            else                           gx = k->x + (k->w - groupW) / 2;
            if (gx < k->x) gx = k->x;

            // 图标与文字的**纵向**对齐：图标按自身包围盒居中（DrawHkIcon 的路径上下对称），
            // 文字按「墨迹框」居中。串里带下降部（Caps / Bksp / Backspace…）时墨迹下缘被
            // 降部拖低，墨迹中心比字身（cap 高）中心低约「降部深的一半」，图标照键框居中
            // 就会显得比字母低一截（实测 175% DPI 下 Caps 的图标低 4.5px）。
            // 带降部时把图标上移这半个降部深（MiSans 墨迹降部 ≈ 0.21em，故取 0.105em）；
            // 无降部的串（Tab / Enter / Menu / Win / Shift）墨迹中心＝字身中心，不补偿。
            int iconY = k->y + (k->h - iconInline) / 2;
            if (TextHasDescender(text))
                iconY -= (int)(0.105f * FontEmPx(ff) + 0.5f);

            DrawKeyGlyph(dc, gx, iconY, iconInline, k, color);
            // 与标题栏 pill 同一套：文字矩形左移半个余量，墨迹才正好接在间距之后
            DrawTextC(dc, gx + iconInline + gap + drawn / 2 - (tw + 4) / 2, k->y, tw + 4, k->h,
                      text, ff, color);
            return TRUE;
        }
    }

    // 只画图形（没有文字 / 箭头键 / 降到底仍放不下）
    // 只有**方向键**用更大的独占尺寸（键面本来就是那个箭头，撑满键帽更好看）；
    // 其余没名字的键（Win / Menu 徽标）沿用 inline 档 —— 同排的字都在，放大的徽标会大一号。
    iconOnly = (k->type == K_ARROW) ? (k->h * 42 / 100) : iconInline;
    if (iconOnly > k->w - 2 * edge) iconOnly = k->w - 2 * edge;
    if (iconOnly < 6) iconOnly = 6;
    int gx = k->x + (k->w - iconOnly) / 2;
    if (k->align == KA_LEFT)       gx = k->x + edge;
    else if (k->align == KA_RIGHT) gx = k->x + k->w - edge - iconOnly;
    DrawKeyGlyph(dc, gx, k->y + (k->h - iconOnly) / 2, iconOnly, k, color);
    return TRUE;
}

static wchar_t GetSymForKey(short vk, BOOL shifted) {
    struct { short vk; wchar_t n, s; } map[] = {
        {0x31,L'1',L'!'},{0x32,L'2',L'@'},{0x33,L'3',L'#'},{0x34,L'4',L'$'},{0x35,L'5',L'%'},
        {0x36,L'6',L'^'},{0x37,L'7',L'&'},{0x38,L'8',L'*'},{0x39,L'9',L'('},{0x30,L'0',L')'},
        {0xBD,L'-',L'_'},{0xBB,L'=',L'+'},{0xDB,L'[',L'{'},{0xDD,L']',L'}'},{0xDC,L'\\',L'|'},
        {0xBA,L';',L':'},{0xDE,L'\'',L'"'},{0xBC,L',',L'<'},{0xBE,L'.',L'>'},{0xBF,L'/',L'?'},{0xC0,L'`',L'~'},
    };
    for (size_t i = 0; i < sizeof(map)/sizeof(map[0]); i++)
        if (map[i].vk == vk) return shifted ? map[i].s : map[i].n;
    return 0;
}

// Fn 层映射：数字行物理键 (1~0 和 - =) 对应 F1~F12，返回 0 表示无映射
static int FnMap(short vk) {
    switch (vk) {
        case 0x31: return 1;  case 0x32: return 2;  case 0x33: return 3;
        case 0x34: return 4;  case 0x35: return 5;  case 0x36: return 6;
        case 0x37: return 7;  case 0x38: return 8;  case 0x39: return 9;
        case 0x30: return 10; case 0xBD: return 11; case 0xBB: return 12;
    }
    return 0;
}

static const wchar_t* LetterKeyText(short vk) {
    BOOL upper = g_cp;
    if (g_sh || g_physShift) upper = !upper;
    static wchar_t buf[2];
    buf[0] = (vk >= 0x41 && vk <= 0x5A) ? (upper ? vk : vk + 32) : vk;
    buf[1] = 0;
    return buf;
}

// 网页布局的网址后缀键（vk 0x200 起为索引哨兵）
static const wchar_t* g_domainTexts[6] = { L"www.", L".com", L".cn", L".org", L".cc", L".net" };

// 网页布局层是否生效（Fn 层 + 「Fn 网页布局」开关）。
// 这一层的键面是「画什么就发什么」——符号已经全铺在键面上（K_SYM），
// 所以**整个层都不随 Shift 换字**。
// ⚠ 判定点有两处，必须同源，否则会出现「大部分键面不动、个别键（如 `~）还在跟着变」：
//   1) KeyText 里 K_NORMAL 的 GetSymForKey(vk, g_sh && g_shiftSymbols)
//   2) DrawKeys 里 K_NORMAL 的双符号换面（baseCh / shiftCh / shiftOn）
static BOOL IsWebFnLayer() { return (g_fnLayer && g_fnWebLayout); }

static const wchar_t* KeyText(const KeyDef* k) {
    static wchar_t buf[16];
    if (k->vk >= 0x200 && k->vk <= 0x205) return g_domainTexts[k->vk - 0x200];
    // 网页层符号键：键面固定画一个符号，与 Shift、g_shiftSymbols 都无关
    if (k->type == K_SYM) {
        wchar_t ch = GetSymForKey(k->vk, k->symShift ? TRUE : FALSE);
        if (ch) { buf[0] = ch; buf[1] = 0; return buf; }
    }
    if (k->type == K_LETTER) {
        return LetterKeyText(k->vk);
    }
    if (k->type == K_NORMAL) {
        if (g_fnLayer && !g_fnWebLayout) {
            int fn = FnMap(k->vk);
            if (fn) { swprintf(buf, 16, L"F%d", fn); return buf; }
        }
        wchar_t ch = GetSymForKey(k->vk, (g_sh && g_shiftSymbols && !IsWebFnLayer()) ? TRUE : FALSE);
        if (ch) { buf[0] = ch; buf[1] = 0; return buf; }
    }
    // F1~F12 顶行 / 小键盘数字
    if (k->vk >= 0x70 && k->vk <= 0x7B) { swprintf(buf, 16, L"F%d", k->vk - 0x6F); return buf; }
    if (k->vk >= 0x60 && k->vk <= 0x69) { buf[0] = (wchar_t)(L'0' + (k->vk - 0x60)); buf[1] = 0; return buf; }
    switch (k->vk) {
        case 0x6A: return L"*";
        case 0x6B: return L"+";
        case 0x6D: return L"-";
        case 0x6E: return L".";
        case 0x6F: return L"/";
        case 0x90: return L"Num";
        case 0x1B: return L"Esc";
        case 0x2E: return L"Del";
        // 退格：宽键显示全称 Backspace，窄键用 Bksp。
        // 判据 = 「可用宽 vs 实测文字宽」，与 FitKeyFont 同源（两边都走 MeasureTextW，
        // 也就是 GDI+ 的容器宽，绘制的 NoWrap 判据一致）：
        //   可用宽 = 键宽 − 两侧各 3 DIP 安全边距。
        //   g_bkspTextW = 字体重建时量一次的「Backspace」在 g_f12 档下的容纳宽。
        // 字体还没建完时（极早的一次绘制）退回几何判据 —— 实测「放得下」⇔ 键宽 ≥ 1.83 × 键高。
        case 0x08: {
            // 默认布局的退格键只有 1.75u，文字被挤到只剩「Bksp」——实机反馈直接不要文字，
            // 只留 ⌫ 图形（图形本身已经说清楚）。全尺寸 / 小键盘里键够宽，保留 Backspace。
            if (g_layoutMode == 0) return L"";
            int avail = k->w - (int)(6 * GetSystemDpiScale());
            if (g_bkspTextW > 0) return (avail >= g_bkspTextW) ? L"Backspace" : L"";
            return (k->w >= (int)(1.9 * k->h)) ? L"Backspace" : L"";
        }
        case 0x09: return L"Tab";
        case 0x0D: return L"Enter";
        case 0x14: return L"Caps";
        case 0x10: case 0xA0: case 0xA1: return L"Shift";
        case 0x11: return L"Ctrl";
        case 0x12: return L"Alt";
        // Win / Menu：键面只留图形，不再带名字（实机反馈：这两个键的文字描述去掉）。
        // 键面本身就是公认图形（四格徽标 / 三条杠），名字是冗余信息，去掉后与方向键一样干净。
        case 0x5B: return L"";
        case 0x5D: return L"";
        // 独立小键盘是 4 列网格，纯空白的一格看着像画错了（参考图那里写着「空格」）；
        // 默认布局 / 全尺寸的空格键很宽，沿用屏幕键盘的惯例不写字。
        case 0x20: return (g_layoutMode == 1) ? T(L"\x7A7A\x683C", L"Space") : L"";
        case 0x25: return L"\x2190";
        case 0x26: return L"\x2191";
        case 0x27: return L"\x2192";
        case 0x28: return L"\x2193";
        case 0x2C: return L"PrtSc";
        case 0x91: return L"ScrLk";   // VK_SCROLL（原来是 0x46 = 字母 F，已修）
        case 0x13: return L"Pause";
        case 0x2D: return L"Ins";
        case 0x24: return L"Home";
        case 0x21: return L"PgUp";
        case 0x23: return L"End";
        case 0x22: return L"PgDn";
        case 0xC0: return L"\x60";
    }

    if (k->type == K_HIDE) return T(L"\x6536\x8D77", L"Hide");
    if (k->type == K_SPACE) return L"";
    // vk==0 是 Fn 键（Fn 网页层 / F1~F12 层的入口）
    if (k->type == K_SPECIAL && k->vk == 0) return L"Fn";
    return L"";
}

static BOOL IsActive(const KeyDef* k) {
    if (k->vk == 0x14 && g_cp) return TRUE;
    // NumLock：跟实体键盘的锁定灯走（GetAsyncKeyState(VK_NUMLOCK) 的 bit0）
    if (k->vk == 0x90 && g_physNum) return TRUE;
    if ((k->vk == VK_SHIFT || k->vk == VK_LSHIFT || k->vk == VK_RSHIFT) && (g_sh || g_physShift)) return TRUE;
    if (k->vk == 0x11 && g_ct) return TRUE;
    if (k->vk == VK_LWIN && (g_winKey || g_physWin)) return TRUE;   // 锁定(等 Win+快捷键)或实体 Win 按下时高亮
    if (k->vk == 0x12 && g_al) return TRUE;
    if (k->type == K_SPECIAL && k->vk == 0 && g_fnLayer) return TRUE;
    return FALSE;
}

// ========== IME-Compatible Input Injection ==========
// ⚠ 必须用 SendInput（keybd_event 已废弃且拦不到高权限窗口），扫描码走
//   MapVirtualKeyW(MAPVK_VK_TO_VSC) —— 部分 IME 依赖正确扫描码。
//
// ⚠⚠ **注入必须分段，down/up 之间要让出消息循环**（Win10 微软拼音不收，issue #3）
//
//   现象：Win10 + 微软拼音打不出中文，但**微信拼音正常**、**Win11 正常**、
//   **Win7 正常**，而且**同一台机器上资源管理器搜索框里能打中文、
//   Edge 地址栏和记事本里不能**。
//   这组对照说明注入通路本身是通的（英文/数字全部正常），微软拼音的状态机
//   只是不接受这种「一个 SendInput 里down+up 全部灌完、零延时」的节奏。
//
//   原理：微软拼音的 TSF 前端要在 WM_KEYDOWN 之后**跑一轮自己的消息循环**
//   才开始组字；down 与 up 在同一个 SendInput 里同批到达时，它还没来得及
//   进入组字状态就收到了 KEYUP，于是整串被丢掉 —— 不上屏、不出候选窗。
//   微信拼音的实现更宽容，Win11 / Win7 的 IME 容错更高，所以都不复现。
//   Chromium（Edge）与纯文本控件对按键节奏最敏感，资源管理器的搜索框反而宽容，
//   于是出现「换个窗口就能用」的迷惑现象。
//
//   佐证：本文件原有的 ToggleImeLang / SendWinToggle **都**是「分两次发送 +
//   Sleep(50)」并在注释里写明了「避免过快 down+up 被 IME 忽略」，
//   偏偏 SendKey 这条最常走的路径漏了 —— 现在按同样的思路补上。
//
//   ⚠ 延时不能省，也不能给太大：太大则长按连发（TIMER_REPEAT 40ms 一个 tick）
//   会因为 Sleep 累积而拖慢。1ms 是「足够让出消息循环」与「不拖慢连发」的折中。
#define KEY_INJECT_GAP_MS 1

// gapMs = 0 表示「不分段、一次性 SendInput 灌完」，即v2.0 修复前的行为。
// 只给**连续大量注入**的场景用（Fn 层的网址后缀键TypeDomainText、长按连发）：
// 那里本来就不会触发中文组字，逐键 1ms 的延时只会累积成几十毫秒的卡顿。
// ⚠ 绝不要给「用户敲单个键」用 —— 那正是 issue #3 的场景，需要让出消息循环。
static void SendKeyGap(BYTE vk, BOOL sh, BOOL ct, BOOL al, BOOL win, DWORD gapMs) {
    if (gapMs == 0) {
        // 快速路径：还原成一次性批量注入（与修复前逐字一致）
        INPUT inputs[12] = {};
        int count = 0;
        UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);
        BOOL isExt = (vk == VK_RCONTROL || vk == VK_RMENU ||
                      vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
                      vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
                      vk == VK_INSERT || vk == VK_DELETE || vk == VK_LWIN || vk == VK_RWIN ||
                      vk == VK_NUMLOCK);
        DWORD ext = isExt ? KEYEVENTF_EXTENDEDKEY : 0;
        if (ct) { INPUT& i = inputs[count++]; i.type = INPUT_KEYBOARD;
                  i.ki.wVk = VK_CONTROL; i.ki.wScan = (WORD)MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC); }
        if (al) { INPUT& i = inputs[count++]; i.type = INPUT_KEYBOARD; i.ki.wVk = VK_MENU;
                  i.ki.wScan = (WORD)MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC); i.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_SCANCODE; }
        // ⚠ Shift 一律用**右 Shift**（0x36），与 SendKey / ToggleImeLang 一致。
        //   微软拼音的切换键默认绑在右 Shift 上；发左 Shift（0x2A）它不认。
        if (sh) { INPUT& i = inputs[count++]; i.type = INPUT_KEYBOARD; i.ki.wVk = VK_RSHIFT;
                  i.ki.wScan = 0x36; i.ki.dwFlags = KEYEVENTF_SCANCODE; }
        if (win) { INPUT& i = inputs[count++]; i.type = INPUT_KEYBOARD; i.ki.wVk = VK_LWIN;
                   i.ki.wScan = (WORD)MapVirtualKeyW(VK_LWIN, MAPVK_VK_TO_VSC); i.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_SCANCODE; }
        { INPUT& i = inputs[count++]; i.type = INPUT_KEYBOARD; i.ki.wVk = vk; i.ki.wScan = (WORD)sc; i.ki.dwFlags = ext | KEYEVENTF_SCANCODE; }
        { INPUT& i = inputs[count++]; i.type = INPUT_KEYBOARD; i.ki.wVk = vk; i.ki.wScan = (WORD)sc; i.ki.dwFlags = ext | KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP; }
        if (win) { INPUT& i = inputs[count++]; i.type = INPUT_KEYBOARD; i.ki.wVk = VK_LWIN;
                   i.ki.wScan = (WORD)MapVirtualKeyW(VK_LWIN, MAPVK_VK_TO_VSC); i.ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY | KEYEVENTF_SCANCODE; }
        if (sh) { INPUT& i = inputs[count++]; i.type = INPUT_KEYBOARD; i.ki.wVk = VK_RSHIFT;
                  i.ki.wScan = 0x36; i.ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_SCANCODE; }
        if (al) { INPUT& i = inputs[count++]; i.type = INPUT_KEYBOARD; i.ki.wVk = VK_MENU;
                  i.ki.wScan = (WORD)MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC); i.ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY | KEYEVENTF_SCANCODE; }
        if (ct) { INPUT& i = inputs[count++]; i.type = INPUT_KEYBOARD; i.ki.wVk = VK_CONTROL;
                  i.ki.wScan = (WORD)MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC); i.ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_SCANCODE; }
        SendInput(count, inputs, sizeof(INPUT));
        return;
    }
    // 正常路径：下面那个 SendKey 的分段实现
    SendKey(vk, sh, ct, al, win);
}

static void SendKey(BYTE vk, BOOL sh, BOOL ct, BOOL al, BOOL win) {
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);

    // 判断扩展键（右 Ctrl/Alt、方向键、Win 等；右 Shift 不带 E0 扩展标志）
    // NumLock 也在这张表里：真键盘上它是 E0 45，少了这个标志部分环境收不到 / 不切换。
    BOOL isExtended = (vk == VK_RCONTROL || vk == VK_RMENU ||
                       vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
                       vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
                       vk == VK_INSERT || vk == VK_DELETE || vk == VK_LWIN || vk == VK_RWIN ||
                       vk == VK_NUMLOCK);

    DWORD extFlag = isExtended ? KEYEVENTF_EXTENDEDKEY : 0;

    // ---- 第 1 段：修饰键按下（一次 SendInput，顺序 ct → al → sh → win）----
    {
        INPUT mods[4] = {};
        int mc = 0;
        if (ct) {
            mods[mc].type = INPUT_KEYBOARD;
            mods[mc].ki.wVk = VK_CONTROL;
            mods[mc].ki.wScan = (WORD)MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC);
            mods[mc].ki.dwFlags = KEYEVENTF_SCANCODE;
            mc++;
        }
        if (al) {
            mods[mc].type = INPUT_KEYBOARD;
            mods[mc].ki.wVk = VK_MENU;
            mods[mc].ki.wScan = (WORD)MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC);
            mods[mc].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_SCANCODE;
            mc++;
        }
        if (sh) {
            // ⚠⚠ **必须用右 Shift（VK_RSHIFT / 扫描码 0x36），不能用 VK_SHIFT。**
            //   实测（2026-10-03 Win10）：用 VK_SHIFT（→扫描码 0x2A =左 Shift）
            //   时，**按 Shift 切不了中英文**。
            //   原因：微软拼音的切换键默认绑定在**右 Shift** 上。
            //   补上 KEYEVENTF_SCANCODE 后系统按扫描码反查 VK，
            //   0x2A 反查出来是 VK_LSHIFT，IME 自然不认。
            //   ⇒与 ToggleImeLang 保持一致（它用 VK_RSHIFT / 0x36）。
            //   另：右 Shift 本身**不是**扩展键（不带 0xE0），
            //   所以这里不能加 KEYEVENTF_EXTENDEDKEY。
            mods[mc].type = INPUT_KEYBOARD;
            mods[mc].ki.wVk = VK_RSHIFT;
            mods[mc].ki.wScan = 0x36;              // 右 Shift 标准扫描码
            mods[mc].ki.dwFlags = KEYEVENTF_SCANCODE;
            mc++;
        }
        if (win) {
            mods[mc].type = INPUT_KEYBOARD;
            mods[mc].ki.wVk = VK_LWIN;
            mods[mc].ki.wScan = (WORD)MapVirtualKeyW(VK_LWIN, MAPVK_VK_TO_VSC);
            mods[mc].ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_SCANCODE;
            mc++;
        }
        if (mc > 0) SendInput(mc, mods, sizeof(INPUT));
        if (mc > 0) Sleep(KEY_INJECT_GAP_MS);
    }

    // ---- 第 2 段：目标键 down ----
    //
    // ⚠⚠⚠ **必须带 KEYEVENTF_SCANCODE**，否则 `wScan` 是死字段（issue #3 根因）
    //
    //   Win32 语义：**不设 KEYEVENTF_SCANCODE 时，系统只认 `wVk`，
    //   `wScan` 会被完全忽略**。而我们下面把 `wScan` 填了却没设这个标志，
    //   于是它从来没生效过 —— 文件顶部注释「扫描码走 MapVirtualKeyW，
    //   部分 IME 依赖正确扫描码」说的事情，实际上**一件都没发生**。
    //
    //   佐证：本文件里**能正常工作**的 `ToggleImeLang` / `SendWinToggle`
    //   都老老实实设了 `dwFlags = KEYEVENTF_SCANCODE`：
    //       in.ki.dwFlags = KEYEVENTF_SCANCODE;              // ToggleImeLang
    //       in.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    //   偏偏最常走的 SendKey 这条漏了。
    //
    //   为什么这会让微软拼音丢字：TSF 前端从 `WM_KEYDOWN` 的 `lParam`
    //   （bit 16~23 = 扫描码、bit 24 = 扩展键标志）判断按了哪个物理键。
    //   只给 `wVk` 时系统要自己反推扫描码，在非美式布局 / IME 激活态下
    //   反推结果可能与真键盘不同 ⇒ IME 认不出这串按键 ⇒ 不组字、不上屏。
    //
    //   设定 SCANCODES 后 `wVk` 会被忽略，所以**保持同时赋值无害**，
    //   且能让意图在代码里保持清晰。
    {
        INPUT down = {};
        down.type = INPUT_KEYBOARD;
        down.ki.wVk = vk;
        down.ki.wScan = (WORD)sc;
        down.ki.dwFlags = extFlag | KEYEVENTF_SCANCODE;
        SendInput(1, &down, sizeof(INPUT));
    }

    // 关键延时：给 IME 的 TSF 前端跑一轮消息循环、进入组字状态
    Sleep(KEY_INJECT_GAP_MS);

    // ---- 第 3 段：目标键 up ----
    {
        INPUT up = {};
        up.type = INPUT_KEYBOARD;
        up.ki.wVk = vk;
        up.ki.wScan = (WORD)sc;
        up.ki.dwFlags = extFlag | KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        SendInput(1, &up, sizeof(INPUT));
    }

    // ---- 第 4 段：修饰键抬起（逆序win → sh → al → ct，与按下相反）----
    {
        INPUT mods[4] = {};
        int mc = 0;
        if (win) {
            mods[mc].type = INPUT_KEYBOARD;
            mods[mc].ki.wVk = VK_LWIN;
            mods[mc].ki.wScan = (WORD)MapVirtualKeyW(VK_LWIN, MAPVK_VK_TO_VSC);
            mods[mc].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY | KEYEVENTF_SCANCODE;
            mc++;
        }
        if (sh) {
            // 与第 1 段的按下**必须成对**：同样是右 Shift / 扫描码 0x36。
            // 若按下发左 Shift、抬起发右 Shift（或反过来），
            // 系统会认为有另一个 Shift 被按下/释放，状态会错乱。
            mods[mc].type = INPUT_KEYBOARD;
            mods[mc].ki.wVk = VK_RSHIFT;
            mods[mc].ki.wScan = 0x36;              // 右 Shift 标准扫描码
            mods[mc].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_SCANCODE;
            mc++;
        }
        if (al) {
            mods[mc].type = INPUT_KEYBOARD;
            mods[mc].ki.wVk = VK_MENU;
            mods[mc].ki.wScan = (WORD)MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC);
            mods[mc].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY | KEYEVENTF_SCANCODE;
            mc++;
        }
        if (ct) {
            mods[mc].type = INPUT_KEYBOARD;
            mods[mc].ki.wVk = VK_CONTROL;
            mods[mc].ki.wScan = (WORD)MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC);
            mods[mc].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_SCANCODE;
            mc++;
        }
        if (mc > 0) SendInput(mc, mods, sizeof(INPUT));
    }
}

// 输入法中英文切换：由左右 Shift 的第 2 次点击触发（用右 Shift 扫描码，与真实右 Shift 一致）。
// 采用“纯扫描码 + 按下/抬起分两次发送”：
//  - KEYEVENTF_SCANCODE 直接注入物理扫描码，不受键盘布局映射影响，IME 能识别为真实右 Shift；
//  - 按下与抬起之间留出间隔，避免过快 down+up 被微软拼音/搜狗等 IME 忽略。
static void ToggleImeLang() {
    UINT sc = MapVirtualKeyW(VK_RSHIFT, MAPVK_VK_TO_VSC);
    if (sc == 0) sc = 0x36;  // 右 Shift 标准扫描码

    INPUT in = {};
    in.type = INPUT_KEYBOARD;

    // 按下右 Shift
    in.ki.wScan = (WORD)sc;
    in.ki.dwFlags = KEYEVENTF_SCANCODE;
    SendInput(1, &in, sizeof(INPUT));

    // 给 IME 足够时间处理按键事件
    Sleep(50);

    // 抬起右 Shift（IME 一般在抬起时完成中英文切换）
    in.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

// 发送一次 Win 键（开/关开始菜单）：按下与抬起分两次注入并留出间隔，
// 确保系统可靠识别切换，避免快速连点时注入被合并/吞掉导致“关闭又弹开”。
static void SendWinToggle() {
    UINT sc = MapVirtualKeyW(VK_LWIN, MAPVK_VK_TO_VSC);
    if (sc == 0) sc = 0x5B;  // 左 Win 标准扫描码

    INPUT in = {};
    in.type = INPUT_KEYBOARD;

    // 按下 Win
    in.ki.wVk = VK_LWIN;
    in.ki.wScan = (WORD)sc;
    in.ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
    SendInput(1, &in, sizeof(INPUT));

    // 给开始菜单足够时间处理切换
    Sleep(50);

    // 抬起 Win
    in.ki.dwFlags = KEYEVENTF_EXTENDEDKEY | KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

// 清除 Win 锁定：解锁并重置点击计数（使用 Win+快捷键或检测到开始菜单时调用）。
// ⚠ 别再用「注入 Esc 关开始菜单」那套：曾这么做过，但注入的 Win 键在部分环境下
//   「能开不能关」，后来改成按 Win 键本身开合，锁定态只由这里统一清。
static void ClearWinLock() {
    g_winKey = FALSE;
    g_winCount = 0;
}

// ========== 开始菜单可见性检测（IAppVisibility，Win8+ 官方 API） ==========
// Win10/11 的开始菜单是 UWP/XAML 窗口（Windows.UI.Core.CoreWindow），用 FindWindow/
// IsWindowVisible 无法可靠检测（"Start" 窗口长期保持可见属性且不进入前台）。
// 这里改用系统自身逻辑 IAppVisibility::IsLauncherVisible 判断开始菜单是否显示，
// 与 Win8 及以上的系统实现保持一致。
static const GUID CLSID_AppVisibility =
    {0x7E5FE3D9, 0x985F, 0x4908, {0x91, 0xF9, 0xEE, 0x19, 0xF9, 0xFD, 0x15, 0x14}};
static const GUID IID_IAppVisibility =
    {0x2246EA2D, 0xCAEA, 0x4444, {0xA3, 0xC4, 0x6D, 0xE8, 0x27, 0xE4, 0x43, 0x13}};

// IAppVisibility vtable（不依赖 shobjidl_core.h，手动声明）
// [0] QueryInterface  [1] AddRef  [2] Release
// [3] GetAppVisibilityOnMonitor  [4] IsLauncherVisible  [5] Advise  [6] Unadvise
typedef struct AppVisibilityVtbl {
    HRESULT (STDMETHODCALLTYPE *QueryInterface)(void*, REFIID, void**);
    ULONG   (STDMETHODCALLTYPE *AddRef)(void*);
    ULONG   (STDMETHODCALLTYPE *Release)(void*);
    HRESULT (STDMETHODCALLTYPE *GetAppVisibilityOnMonitor)(void*, HMONITOR, int*);
    HRESULT (STDMETHODCALLTYPE *IsLauncherVisible)(void*, BOOL*);
    HRESULT (STDMETHODCALLTYPE *Advise)(void*, void*, DWORD*);
    HRESULT (STDMETHODCALLTYPE *Unadvise)(void*, DWORD);
} AppVisibilityVtbl;

typedef struct AppVisibility {
    AppVisibilityVtbl* lpVtbl;
} AppVisibility;

static BOOL IsStartMenuOpen() {
    static AppVisibility* s_av = NULL;  // 缓存 COM 实例，避免每次重复创建
    static BOOL s_comReady = FALSE;
    static BOOL s_comTried = FALSE;

    if (!s_comTried) {
        s_comTried = TRUE;
        // 主 UI 线程初始化 STA COM；S_FALSE 表示本线程已初始化，同样可用
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        s_comReady = SUCCEEDED(hr);
    }
    if (!s_comReady) return FALSE;

    if (!s_av) {
        HRESULT hr = CoCreateInstance(CLSID_AppVisibility, NULL, CLSCTX_INPROC_SERVER,
                                      IID_IAppVisibility, (void**)&s_av);
        if (FAILED(hr)) return FALSE;  // 无此 API 的系统（XP/WinPE）返回 FALSE，回退原行为
    }

    BOOL vis = FALSE;
    return (SUCCEEDED(s_av->lpVtbl->IsLauncherVisible(s_av, &vis)) && vis);
}

// 逐字符输入网址文本（仅支持域名用到的字母与“.”）
static void TypeDomainText(const wchar_t* s) {
    for (; *s; ++s) {
        wchar_t c = *s;
        short vk;
        BOOL sh = FALSE;
        if (c >= L'a' && c <= L'z') {
            vk = (short)(c - L'a' + 0x41);
            sh = (GetKeyState(VK_CAPITAL) & 1) != 0;   // 大写锁定时字母需按 Shift 还原小写
        } else if (c == L'.') {
            vk = 0xBE;
        } else {
            continue;
        }
        // 网址后缀是「一次性把整串打完」，不会进中文组字 —— 走 gap=0 的快速路径，
        // 否则每个字符 2 次 Sleep(1)，20 字符的域名就是 40ms 的卡顿。
        SendKeyGap((BYTE)vk, sh, FALSE, FALSE, FALSE, 0);
    }
}

#ifdef HK_DIAG
// ---- -diag：最小诊断，记录"注入前后"的关键系统状态 ----
//
// 用途：issue #3 已经缩小到「用户**手动点击**按键 → 注入的按键不被微软拼音
// 组字」，而**同样的注入由定时器触发时却正常**。两者在代码路径上已被逐一
// 证明等价（SendKey ≡ SendTestKey、SetCapture/重绘均已排除），
// 所以差异只可能在**注入那一刻的系统状态**上。
//
// 只记录跨进程可取、且与"输入法为什么会拒绝组字"直接相关的几项：
//   前台窗口类名 / 焦点窗口 / 光标窗口(hwndCaret) / **鼠标左键是否按下** / 键盘布局
//
// ⚠ 重点看 LBTN：
//   用户手动点击时，注入发生在 WM_LBUTTONDOWN 的处理过程中，
//   此刻鼠标左键**必然是按下状态**；而定时器触发的注入不是。
//   若两边的 LBTN 不同，差异就找到了 —— 那说明微软拼音在
//   "鼠标按键按下期间"会拒绝处理注入的键盘事件。
BOOL g_diag = FALSE;

static void DiagSnap(const wchar_t* tag) {
    if (!g_diag) return;

    wchar_t self[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wchar_t* slash = wcsrchr(self, L'\\');
    if (slash) *(slash + 1) = 0;
    wchar_t path[MAX_PATH * 2] = {0};
    _snwprintf_s(path, MAX_PATH * 2, _TRUNCATE, L"%lsdiag.txt", self);

    FILE* f = NULL;
    if (_wfopen_s(&f, path, L"a, ccs=UTF-8") != 0 || !f) return;

    HWND fg = GetForegroundWindow();
    char fgCls[64] = {0};
    if (fg) GetClassNameA(fg, fgCls, 64);

    DWORD tid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
    GUITHREADINFO gi = {0};
    gi.cbSize = sizeof(gi);
    BOOL ok = (tid && GetGUIThreadInfo(tid, &gi));

    BOOL lbtn = ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0);
    HKL hkl = tid ? GetKeyboardLayout(tid) : NULL;

    fwprintf(f, L"%-9ls fg=%-22hs focus=%-10p caret=%-10p LBTN=%d hkl=%p\n",
             tag, fgCls,
             (void*)(ok ? gi.hwndFocus : NULL),
             (void*)(ok ? gi.hwndCaret : NULL),
             (int)lbtn, (void*)hkl);
    fclose(f);
}
#endif  // HK_DIAG

// ⚠⚠⚠ **注入前必须等鼠标左键抬起** —— issue #3 的真正根因（2026-10-04 定位）
//
// 实测依据（diag.txt 记录的"注入前后系统状态"）：
//     用户**真实**鼠标点击  -> LBTN=1  -> 微软拼音不组字（出英文）
//     **注入**鼠标点击      -> LBTN=0  -> 正常组字
//     定时器触发注入        -> LBTN=0  -> 正常组字
//   三者其余状态**完全相同**：同一个前台窗口(Notepad)、同一个 focus/caret、
//   同一个键盘布局(0x08040804 = zh-CN)。唯一差异就是 LBTN。
//
//   最有说服力的一点：同样是 `[before]/[after]` 两组，一组 LBTN=1、
//   一组 LBTN=0 —— 说明 **SendInput 注入的鼠标按下不会被系统记为"按下"**，
//   所以自动化测试永远测不出这个问题。
//
// ⇒ 结论：**微软拼音（Win10）在"鼠标左键按下期间"拒绝处理注入的键盘事件。**
//   这一条同时解释了全部现象：手动点击必然失败、自动化注入必然成功、
//   物理键盘不受影响、Win11/Win7/第三方输入法对鼠标状态不敏感。
//
// ⚠ 实现上必须用 PeekMessage 泵消息，**不能死 Sleep**：
//   `OnLDown` 是在 WM_LBUTTONDOWN 的处理过程中调用的，此刻左键必然按下；
//   而 WM_LBUTTONUP 还得靠消息循环派发才能到达 —— 死等的话它永远收不到，
//   必然一路等到超时。泵消息同时保证界面不卡。
//
// ⚠ 加防重入：泵消息会派发 WM_LBUTTONUP -> OnLUp，若用户此刻又点了别的键，
//   会重入 WndProc -> DoKeyAction -> 本函数。没有这个标志会递归下去。
static BOOL g_inWaitLButton = FALSE;
// 长按连发中：跳过左键等待。
// ⚠ 连发时用户**一直按着**鼠标，等下去只会每次都耗满 250ms 超时，
//   把连发拖成每 250ms 一次。而连发本身是"重复同一个字符"
//   （退格/删除/空格/方向键），不走组字 —— 真正需要组字的是**第一次**
//   注入，那一次在 OnLDown 里、用户刚按下，等待很短就能过。
static BOOL g_inRepeat = FALSE;

static void WaitForLeftButtonUp() {
    if (g_inWaitLButton) return;                              // 防重入
    if (g_inRepeat) return;                                   // 连发中不等
    if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) return;     // 没按下，直接走

    g_inWaitLButton = TRUE;
    DWORD start = GetTickCount();
    while ((GetAsyncKeyState(VK_LBUTTON) & 0x8000) &&
           GetTickCount() - start < 250) {                    // 250ms 上限，别卡死
        MSG msg;
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            // ⚠ WM_QUIT 必须自己再投回去：PeekMessage 会把它取走，
            //   而 DispatchMessage 不处理它 —— 取走后没人管，程序就再
            //   也收不到退出信号了。
            if (msg.message == WM_QUIT) {
                PostQuitMessage((int)msg.wParam);
                break;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        Sleep(1);
    }
    g_inWaitLButton = FALSE;
}

static void DoKeyAction(const KeyDef* k) {
    if (!k) return;
    // ⚠ 所有按键都在此统一等待 —— 空格尤其重要：它负责把候选框里的中文上屏，
    //   若在左键按下期间注入，中文同样上不去（这正是 issue #3 的主诉之一）。
    WaitForLeftButtonUp();
    switch (k->type) {
    case K_LETTER:
#ifdef HK_DIAG
        DiagSnap(L"[before]");
#endif
        if (g_ct || g_al || g_winKey) {
            SendKey(k->vk, g_sh, g_ct, g_al, g_winKey);
            g_sh = FALSE; g_ct = FALSE; g_al = FALSE; ClearWinLock();
        } else {
            BOOL us = g_sh ? !(GetKeyState(VK_CAPITAL) & 1) : FALSE;
            SendKey(k->vk, us, FALSE, FALSE);
            if (g_sh) g_sh = FALSE;
            ClearWinLock();   // 普通键也退出 Win 锁定/切换状态
        }
#ifdef HK_DIAG
        DiagSnap(L"[after ]");
#endif
        break;
    case K_NORMAL:
        if (g_fnLayer && !g_fnWebLayout) {
            int fn = FnMap(k->vk);
            if (fn) {
                SendKey((BYTE)(0x6F + fn), g_sh, g_ct, g_al, g_winKey);  // VK_F1=0x70
                g_fnLayer = FALSE;
                g_sh = FALSE; g_ct = FALSE; g_al = FALSE; ClearWinLock();
                InvalidateRect(g_hWnd, 0, TRUE);
                break;
            }
        }
        SendKey(k->vk, g_sh, g_ct, g_al, g_winKey);
        g_sh = FALSE; g_ct = FALSE; g_al = FALSE; ClearWinLock();
        break;
    case K_SPECIAL:
        if (k->vk >= 0x200 && k->vk <= 0x205) {   // 网址后缀键
            TypeDomainText(g_domainTexts[k->vk - 0x200]);
            g_sh = FALSE; g_ct = FALSE; g_al = FALSE; ClearWinLock();
            break;
        }
        // Tab 就是 Tab：全尺寸布局下「显示 / 隐藏数字区」的唯一入口是标题栏那个
        // 「小键盘」按钮（用户要求彻底下线 Fn + Tab 手势与对应的设置行）。
        if (k->vk == 0) {  // Fn 键：切换 F1~F12 功能层（或网页布局层）
            g_fnLayer = !g_fnLayer;
            if (g_fnLayer) g_sh = FALSE;
            BuildKeys();   // 网页布局层是独立键位表，必须重建
            InvalidateRect(g_hWnd, 0, TRUE);
            break;
        }
        if (k->vk == VK_LWIN) {
            // Win 键参考大写键（Caps）的开关逻辑：
            //  第 1 次点击：锁定并高亮（等待 Win+组合键，再点其它键发送 Win+按键）；
            //  第 2 次点击：轻按一次 Win 键（打开开始菜单），解除锁定、取消高亮；
            //  第 3 次起：每次点击都轻按 Win 键（开始菜单随点击开/关交替），不再失步。
            if (g_winKey) {
                g_winKey = FALSE;
                g_winCount = 0;
                SendWinToggle();                 // 轻按 Win 键
            } else if (IsStartMenuOpen()) {
                SendWinToggle();                 // 菜单开着 → 轻按关闭
            } else {
                g_winKey = TRUE;                 // 锁定，等待 Win+组合键
                g_winCount = 1;
            }
            g_lastWinTick = GetTickCount();
            g_fnLayer = FALSE;
            BuildKeys();   // 若正处网页布局层，退出后需重建键位表
            InvalidateRect(g_hWnd, 0, TRUE);
            break;
        }
        // NumLock：先本地翻状态，让键面高亮立刻跟上（注入的事件被自己的钩子忽略，
        // 钩子里的 VK_NUMLOCK 分支只对实体键生效，所以这里必须自己记一笔）。
        if (k->vk == 0x90) g_physNum = !g_physNum;
        SendKey(k->vk, g_sh, g_ct, g_al, g_winKey);
        g_sh = FALSE; g_ct = FALSE; g_al = FALSE; ClearWinLock();
        break;
    case K_SYM:
        // 网页层符号键：键面画什么就发什么。symShift=1 的格子（! @ # …）内部自动带 Shift，
        // 所以不需要用户先点 Shift，点击结果与键面永远一致。
        SendKey(k->vk, k->symShift ? TRUE : FALSE, g_ct, g_al, g_winKey);
        g_sh = FALSE; g_ct = FALSE; g_al = FALSE; ClearWinLock();
        break;
    case K_MOD:
        if (k->vk == VK_RSHIFT || k->vk == VK_SHIFT || k->vk == VK_LSHIFT) {
            // 左右 Shift 状态机（状态以 g_sh 为准）：
            //  - 未处于 Shift 状态：点击进入 Shift 锁定（后续按键为 Shift+组合键）；
            //  - 已处于 Shift 状态：再次点击切换中/英输入法，并退出 Shift 锁定。
            // 任意 Shift+组合键使用后会退出 Shift 状态，因此下次点击 Shift 可再次正常进入，不会失步。
            //
            // ⚠ 网页布局层里 Shift 不再兼作「退出 Fn 层」：
            //   那个层的符号本来就全铺在键面上（K_SYM），Shift 只用来做
            //   Shift+方向键这类组合；原来无条件 `g_fnLayer = FALSE`，
            //   点一下 Shift 整层就退回默认布局，实机反馈「点了 Shift 键面全变了」。
            //   退出的唯一入口留给那一层 Row4 的 Fn 键。
            BOOL keepFn = (g_fnLayer && g_fnWebLayout);
            BOOL wasFn = g_fnLayer;
            if (g_sh) {
                g_sh = FALSE;
                if (!keepFn) g_fnLayer = FALSE;
                ToggleImeLang();
            } else {
                g_sh = TRUE;
                if (!keepFn) g_fnLayer = FALSE;
            }
            if (wasFn && !keepFn) {
                BuildKeys();   // 退出网页布局层后需重建键位表
                InvalidateRect(g_hWnd, 0, TRUE);
            }
        } else if (k->vk == 0x11) {
            g_ct = !g_ct;
        } else if (k->vk == 0x12) {
            g_al = !g_al;
        }
        break;
    case K_CAPS:
        SendKey(0x14, FALSE, FALSE, FALSE, g_winKey);
        ClearWinLock();
        g_cp = (GetKeyState(VK_CAPITAL) & 1) != 0;
        break;
    case K_ARROW:
        SendKey(k->vk, g_sh, g_ct, g_al, g_winKey);
        g_sh = FALSE; g_ct = FALSE; g_al = FALSE; ClearWinLock();
        break;
    case K_SPACE:
        SendKey(0x20, g_sh, g_ct, g_al, g_winKey);
        g_sh = FALSE; g_ct = FALSE; g_al = FALSE; ClearWinLock();
        break;
    case K_HIDE: UserHideKeyboard(); break;   // 收起键：临时挡路，遇到输入框仍会回来
    default: break;
    }
}
// ========== Header Layout & Dynamic DPI Positioning ==========
#define HDR_DOCK  1000
#define HDR_MIN   1003
#define HDR_CLOSE 1004
#define HDR_NUM   1005   // 123 按钮：切换小键盘
#define HDR_HIDE  1006   // ⏑ 按钮：永久隐藏到托盘（单击托盘图标恢复）

// 标题栏几何：绘制与命中必须取自同一份数据。
// 这里历史上抄过两份（DrawHeader / HitHeader 各写一遍），改宽度就会出「按钮画在左、
// 热区在右」的 bug；今后任何改动只改这里。
struct HeaderMetrics {
    int btnY, btnH;
    int wMenu, wMin, wHide, wNum, wClose;
    int xMenu, xMin, xHide, xNum, xClose;
    int xTitle, wTitle;
    BOOL numBtnVisible;
};

// 页头胶囊与其中文字共用 g_f12，而 g_f12 = 12pt × 键高/48 —— 所以胶囊尺寸必须按同一个比例
// 缩放，否则字号一大文字就撑出主色底（实机图：「小键盘」三个字溢出胶囊）。只放大不缩小：
// 窄窗口下字号本来就不缩（基准是键高），胶囊也不该缩。上限与字号封顶（90 DIP）一致。
static double HeaderFontRatio() {
    double r = ((double)g_keyHeight / GetSystemDpiScale()) / 48.0;
    if (r < 1.0) r = 1.0;
    if (r > 90.0 / 48.0) r = 90.0 / 48.0;
    return r;
}

static HeaderMetrics GetHeaderMetrics() {
    double dpiScale = GetSystemDpiScale();
    double dpi = dpiScale;
    HeaderMetrics hm = {};
    double fr = HeaderFontRatio();
    hm.btnH = (int)(HDR_BTN_H_DIP * dpi * fr);
    // 按钮上留白固定 10 DIP（原来是 (header-btnH)/2 居中，页头 36 时只剩 4 DIP，贴顶）；
    // 页头被压得很矮时退回居中，免得按钮溢出页头。
    hm.btnY = (int)(HDR_BTN_TOP_DIP * dpi);
    if (hm.btnY + hm.btnH > g_headerH) {
        hm.btnY = (g_headerH - hm.btnH) / 2;
        if (hm.btnY < 0) hm.btnY = 0;
    }

    int gap     = (int)(8 * dpi);
    int rMargin = (int)(8 * dpi);

    // 「设置」和「小键盘」两个按钮都是「图标 + 文字」，宽度按内容给足，
    // 两者共用 DrawHeaderPill 绘制（形状、配色、留白一致）。
    // 基值 78 / 96 是「12pt 档、1em ≈ 16 DIP」下量出来的（中文 设置 = 2em、小键盘 = 3em），
    // 再乘 fr 跟上字号 —— 字号来自键高，所以窗口拉高时胶囊同步变大，不会越看越小。
    hm.wMenu  = (int)(78 * dpi * fr);
    hm.wClose = (int)(32 * dpi);
    hm.wMin   = (int)(32 * dpi);
    hm.wHide  = (int)(32 * dpi);
    hm.wNum   = (int)(96 * dpi * fr);

    hm.xClose = g_ww - rMargin - hm.wClose;
    hm.xMin   = hm.xClose - gap - hm.wMin;
    // ⏑ 夹在最小化与「小键盘」之间：两个「收起」按钮相邻，手感才连贯
    hm.xHide  = hm.xMin - gap - hm.wHide;
    // 全尺寸布局下也常驻：它是数字区唯一的界面开关（也给「窄屏自动收起」留了恢复入口）。
    hm.numBtnVisible = g_showNumBtn;
    hm.xNum   = hm.numBtnVisible ? (hm.xHide - gap - hm.wNum) : hm.xHide;
    hm.xMenu  = (int)(6 * dpi);

    hm.xTitle = hm.xMenu + hm.wMenu + gap;
    hm.wTitle = (hm.numBtnVisible ? hm.xNum : hm.xHide) - hm.xTitle - gap;
    return hm;
}

static int HitHeader(int x, int y) {
    if (y < 0 || y >= g_headerH) return -1;
    HeaderMetrics hm = GetHeaderMetrics();
    if (y < hm.btnY || y >= hm.btnY + hm.btnH) return -1;

    if (x >= hm.xClose && x < hm.xClose + hm.wClose) return HDR_CLOSE;
    if (x >= hm.xMin && x < hm.xMin + hm.wMin) return HDR_MIN;
    if (x >= hm.xHide && x < hm.xHide + hm.wHide) return HDR_HIDE;
    if (hm.numBtnVisible && x >= hm.xNum && x < hm.xNum + hm.wNum) return HDR_NUM;
    if (x >= hm.xMenu && x < hm.xMenu + hm.wMenu) return HDR_DOCK;
    return -1;
}

// 「小键盘」按钮当前算不算「开着的」：
//   独立小键盘布局 —— 整块界面就是小键盘，算开；
//   全尺寸布局     —— 数字区可见才算开（窄屏自动收起时按钮不该显示成激活态）；
//   默认布局       —— 没开。
// 这个语义让同一个按钮在三种布局下自洽：它回答的始终是「小键盘现在开着吗」。
static BOOL NumBtnActive() {
    if (g_layoutMode == 1) return TRUE;
    if (g_layoutMode == 2) return !NumpadHidden();
    return FALSE;
}

// 标题栏图标的外框尺寸（DIP）。**不能**所有图标共用一个 size：几何包围盒差得很远
// （Numpad 17.6/24、Gear 22.4/24；最小化与关闭用的还是 12 网格，各占 8/12、6/12），
// 同一个外框画出来齿轮比小键盘大 27%、「—」又比「×」宽一截 —— 实机看着就是「一大一小」。
// 这里按「想让它在视觉上多大」反解外框，各图标的可见尺寸就统一了。
static int HeaderIconBox(int id, double dpi) {
    double box;
    switch (id) {
    case HKICON_GEAR:     box = 14.0 * 24.0 / 22.4; break;   // 可见 ≈ 14 DIP
    case HKICON_NUMPAD:   box = 14.0 * 24.0 / 17.6; break;   // 可见 ≈ 14 DIP
    case HKICON_MINIMIZE: box = 12.5 * 12.0 / 8.0;  break;   // 横线长 ≈ 12.5 DIP
    case HKICON_CHEVRONDOWN: box = 13.0 * 24.0 / 12.0; break; // 箭头宽 ≈ 13 DIP（24 网格，可见 6..18）
    case HKICON_CLOSE:    box = 10.5 * 12.0 / 6.0;  break;   // 叉宽 ≈ 10.5 DIP
    default:              box = 17.0; break;
    }
    return (int)(box * dpi + 0.5);
}

// 标题栏胶囊按钮：底色 + 矢量图标 + 文字，三者同一配方。
// 「设置」与「小键盘」共用这一份 —— 两个按钮的观感必须一致。
// 设置按钮恒定显示「齿轮 + 设置」（设置页页头也是齿轮，两处观感才对得上）。
// 宽度由调用方从 HeaderMetrics 取（绘制与命中同源，见 GetHeaderMetrics 上方的说明）。
// active：「小键盘开着」的实底态（主色底 + 主色上的文字，零新增令牌）。
static void DrawHeaderPill(HDC dc, const HeaderMetrics& hm, int x, int w, BOOL hov,
                           const HkIconDef& icon, int iconId,
                           const wchar_t* label, BOOL active) {
    DWORD bg, fg;
    if (active) {
        // 激活态悬停必须「向白混」：向 cardBg 混在深色主题下只有 4.54:1（踩在 AA 线上）
        bg = hov ? BlendColor(C_HOT, RGB(255, 255, 255), 0.12) : C_HOT;
        fg = C_ON_PRIMARY;
    } else {
        bg = hov ? C_REGULAR_HOV : C_REGULAR;
        fg = C_BTN_CONTENT;
    }
    DrawRoundRect(dc, x, hm.btnY, w, hm.btnH, bg, bg, hm.btnH / 2);

    double dpi  = GetSystemDpiScale();
    int iconSz  = (int)(HeaderIconBox(iconId, dpi) * HeaderFontRatio());   // 与胶囊和文字同比例
    int gap     = (int)(7 * dpi * HeaderFontRatio());
    int tw     = MeasureTextW(dc, label, g_f12);       // 容纳宽度 → 绘制矩形用它（见 MeasureTextW）
    int adv    = MeasureTextAdvW(dc, label, g_f12);    // 布局宽度 → 排版必须用它
    if (adv <= 0) adv = tw;
    // 按「图标 + 间距 + 文字实际占宽」整组居中。若用容纳宽度，GDI+ 那两侧各约 0.2em 的余量
    // 会被当成文字宽度：图标被挤到贴左、图标与文字之间凭空多出近一倍空隙
    // （实测 19px，设计值 10px）。
    int cx = x + (w - (iconSz + gap + adv)) / 2;
    DrawHkIcon(dc, (float)cx, (float)(hm.btnY + (hm.btnH - iconSz) / 2), (float)iconSz,
               icon, fg, fg);
    // DrawTextC 是把墨迹居中在矩形里的，所以矩形要再左移「(矩形宽 - 文字宽)/2」，
    // 墨迹才会正好落在间距之后（否则整段文字右移半个余量）。
    DrawTextC(dc, cx + iconSz + gap + adv / 2 - (tw + 4) / 2, hm.btnY, tw + 4, hm.btnH,
              label, g_f12, fg);
}

static void DrawHeaderMenuButton(HDC dc, const HeaderMetrics& hm) {
    DrawHeaderPill(dc, hm, hm.xMenu, hm.wMenu, (g_hdrHov == HDR_DOCK),
                   HkIcon(HKICON_GEAR), HKICON_GEAR,
                   T(L"\x8BBE\x7F6E", L"Settings"), FALSE);
}

static void DrawHeader(HDC dc) {
    // 标题栏与主界面一体化：不单独铺底色，统一由 ClearWindowBackBuffer 的面板底色呈现。
    double dpiScale = GetSystemDpiScale();
    HeaderMetrics hm = GetHeaderMetrics();

    DrawHeaderMenuButton(dc, hm);

    if (hm.wTitle > 40) {
        DrawTextC(dc, hm.xTitle, 0, hm.wTitle, g_headerH, L"", g_f12, C_DIM);
    }

    // 小键盘按钮：三种布局下都常驻（全尺寸下它是数字区唯一的界面开关）。
    // 激活态 = 「小键盘开着」：独立小键盘布局或在全尺寸下数字区可见时是主色实底。
    if (hm.numBtnVisible) {
        DrawHeaderPill(dc, hm, hm.xNum, hm.wNum, (g_hdrHov == HDR_NUM),
                       HkIcon(HKICON_NUMPAD), HKICON_NUMPAD,
                       T(L"\x5C0F\x952E\x76D8", L"Numpad"), NumBtnActive());
    }

    // 最小化 / ⏑隐藏 / 关闭：改用矢量图标（与设置页、关闭提示窗口同一套图形，不再手绘线条）
    // 外框按各自图标的可见尺寸反解（见 HeaderIconBox），三者观感才一样大。
    int szMin  = HeaderIconBox(HKICON_MINIMIZE, dpiScale);
    int szHide = HeaderIconBox(HKICON_CHEVRONDOWN, dpiScale);
    int szCls  = HeaderIconBox(HKICON_CLOSE, dpiScale);
    int hoverR = (int)(6 * dpiScale);
    if (g_hdrHov == HDR_MIN) {
        DrawRoundRect(dc, hm.xMin, hm.btnY, hm.wMin, hm.btnH,
                      C_REGULAR_HOV, C_REGULAR_HOV, hoverR);
    }
    DrawHkIcon(dc, (float)(hm.xMin + (hm.wMin - szMin) / 2),
               (float)(hm.btnY + (hm.btnH - szMin) / 2), (float)szMin,
               HkIcon(HKICON_MINIMIZE), C_DIM, C_DIM);
    if (g_hdrHov == HDR_HIDE) {
        DrawRoundRect(dc, hm.xHide, hm.btnY, hm.wHide, hm.btnH,
                      C_REGULAR_HOV, C_REGULAR_HOV, hoverR);
    }
    DrawHkIcon(dc, (float)(hm.xHide + (hm.wHide - szHide) / 2),
               (float)(hm.btnY + (hm.btnH - szHide) / 2), (float)szHide,
               HkIcon(HKICON_CHEVRONDOWN), C_DIM, C_DIM);
    if (g_hdrHov == HDR_CLOSE) {
        DrawRoundRect(dc, hm.xClose, hm.btnY, hm.wClose, hm.btnH,
                      C_REGULAR_HOV, C_REGULAR_HOV, hoverR);
    }
    DrawHkIcon(dc, (float)(hm.xClose + (hm.wClose - szCls) / 2),
               (float)(hm.btnY + (hm.btnH - szCls) / 2), (float)szCls,
               HkIcon(HKICON_CLOSE), C_DIM, C_DIM);
}

static void DrawKeys(HDC dc) {
    for (int i = 0; i < g_nk; i++) {
        const KeyDef* k = &g_keys[i];
        BOOL active = IsActive(k);
        BOOL pressed = (i == g_pk);
        BOOL hover = (i == g_hk);

        BOOL isDomain = (k->vk >= 0x200 && k->vk <= 0x205);   // 网址后缀键
        // 修饰与功能键：Esc/Tab/Caps/Shift/Ctrl/Alt/Win/Fn/Menu/方向键等
        BOOL isMod = FALSE;
        if (!isDomain) {
            KeyType mt[] = {K_SPECIAL, K_CAPS, K_MOD, K_ARROW};
            for (size_t j = 0; j < sizeof(mt)/sizeof(mt[0]); j++) {
                if (k->type == mt[j]) { isMod = TRUE; break; }
            }
        }
        BOOL isPlain = (k->type == K_HIDE) || isDomain;       // 收起 / 次要键

        // 键帽状态机：底色 / 轮廓 / 文字三路同时变（Ethereal 的按钮配方）。
        // 触摸没有 hover 的预览语义，按下即直接给出「按下」态，无需逐帧插值。
        // Enter（K_SPECIAL）与退格、Tab、Shift 同配方：不再给它主色实底——
        // 实底在触摸键盘上会被当成「已经按下了」，与真正的按下态撞车。
        DWORD bg, outline, textC;
        if (active || pressed) {
            bg = C_REGULAR_ACT; outline = C_BORDER_HOVER; textC = C_WHITE;
        } else if (isPlain) {
            bg = hover ? C_REGULAR_HOV : C_PLAIN;
            outline = C_LINE_DIV; textC = C_DIM;
        } else if (isMod) {
            if (hover) { bg = C_REGULAR_HOV; outline = C_BORDER_HOVER; textC = C_WHITE; }
            else       { bg = C_REGULAR;     outline = C_KEY_BORDER;   textC = C_BTN_CONTENT; }
        } else {
            if (hover) { bg = C_HOVER; outline = C_BORDER_HOVER; textC = C_WHITE; }
            else       { bg = C_KEY;   outline = C_KEY_BORDER;   textC = C_WHITE; }
        }

        DrawRoundRect(dc, k->x, k->y, k->w, k->h, bg, outline, 8);

        const wchar_t* txt = KeyText(k);
        // 界面只有一个字面：不加粗。修饰键靠底色（C_REGULAR）与文字色（C_BTN_CONTENT）
        // 区分，与设置页 Tab 的选中态同一套逻辑 —— 少了「有些字粗有些字细」的杂音。
        // 放不下时按 FitKeyFont 降档 —— 全尺寸布局的导航区只有 1u 宽。
        // 全部键（含退格）走同一个降档阶梯：不再给退格单独用大号字，
        // 否则「Backspace」这个长词会顶出键帽。
        HFONT f = FitKeyFont(dc, txt, k->w - (int)(6 * GetSystemDpiScale()));

        // 双符号键（数字行/标点）：同时显示主字符与副符号，副符号随 Shift 灰/白；
        // Fn 层（非网页布局）时仅数字行/-/= 键改为显示 F1~F12（不显示双符号），其余标点键双符号显示不变。
        //
        // ⚠ 网页布局层（IsWebFnLayer）整个不做「随 Shift 换面」：
        //   那一层的符号是全铺在键面上的（K_SYM，键面画什么就发什么），
        //   再让残留的双符号键（如 `~）跟着 Shift 换面，就变成
        //   「点一下 Shift，键面反而变花」—— 正是用户要求去掉的行为。
        //   与 KeyText 的同名判定共用 IsWebFnLayer()，两处不能各写一份。
        const BOOL webLayer = IsWebFnLayer();
        wchar_t baseCh = 0, shiftCh = 0;
        if (k->type == K_NORMAL && !webLayer && !(g_fnLayer && FnMap(k->vk) != 0)) {
            baseCh = GetSymForKey(k->vk, FALSE);
            shiftCh = GetSymForKey(k->vk, TRUE);
        }
        // 未按 Shift：双符号显示（数字 + 顶部特殊符号，副符号置灰）；
        // 按 Shift：开启“仅显示特殊符号”时只显示顶部符号（不显示数字），关闭时仍显示数字。
        BOOL shiftOn = (g_sh || g_physShift);
        BOOL dual = (baseCh && shiftCh && shiftCh != baseCh);

        // Menu 与 Win 不再在这里特判：它们和 Tab/Caps/Shift 一样走 DrawKeyLabel 的
        // 「图形 + 名字」（Win 徽标由 DrawKeyGlyph 矢量特绘 —— 字体里没有那个字形）。
        // 图标样式选「文字」时 DrawKeyLabel 返回 FALSE，两者自然落回纯文字。
        if (dual) {
            // 双符号键在三态下都是文字：主字符 + 副符号本身就是两个信息，图标化会毁数据
            if (shiftOn) {
                wchar_t single[2] = { g_shiftSymbols ? shiftCh : baseCh, 0 };
                DrawTextC(dc, k->x, k->y, k->w, k->h, single, f, textC);
            } else {
                DrawKeyDual(dc, k->x, k->y, k->w, k->h, baseCh, shiftCh, f, g_f12, textC, C_DIM);
            }
        } else if (!DrawKeyLabel(dc, k, f, txt, textC)) {
            DrawTextKey(dc, k, txt, f, textC);
        }
    }
}
static int HitKey(int x, int y) {
    for (int i = 0; i < g_nk; i++) {
        const KeyDef* k = &g_keys[i];
        if (x >= k->x && x < k->x + k->w && y >= k->y && y < k->y + k->h)
            return i;
    }
    return -1;
}

// ========== 主窗口帧缓存（首帧预热 + 动画/重绘零开销呈现） ==========
// 每次重绘都全量 GDI+ 绘制（几十个圆角矩形 + 文字 alpha 混合）是启动动画
// 卡顿与材质首帧黑屏的根源：首帧绘制发生在窗口可见之后，DWM 先合成出
// 一帧空/黑内容，且首帧绘制耗时直接表现为“顿一下”。
// 引入 32bpp 顶向下 DIB 帧缓存：绘制状态签名未变时直接 memcpy/BitBlt
// 呈现；窗口显示前即可预热渲染完成，首帧与动画期间无可见绘制开销。
static HBITMAP   g_kbCacheBmp = 0;
static HDC       g_kbCacheDc = 0;
static RGBQUAD*  g_kbCacheBits = 0;
static int       g_kbCacheRow = 0;
static int       g_kbCacheW = 0, g_kbCacheH = 0;

struct KbFrameSig {
    int w, h, hk, pk, hdrHov, layoutMode, nk, hue, keyIconStyle;
    DWORD themeBg;
    float dpi;
    BOOL sh, ct, al, cp, winKey, physShift, physWin, physNum, fnLayer, showFKeys,
         fnWebLayout, shiftSymbols, lang;
    // 数字区显隐（生效值）：之前没进签名，靠「17 个键消失 → nk 变」侥幸触发重绘；
    // 窄屏自动收起与手动收起是两条路径，显式纳入才不依赖这个巧合。
    BOOL npHidden;
    BOOL operator!=(const KbFrameSig& o) const { return memcmp(this, &o, sizeof(*this)) != 0; }
};
static KbFrameSig g_kbSig = {};

static void RenderKbFrameInto(HDC refDc, int w, int h) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;
    void* bits = NULL;
    HBITMAP bmp = CreateDIBSection(refDc, &bmi, DIB_RGB_COLORS, &bits, NULL, 0);
    if (!bmp || !bits) { if (bmp) DeleteObject(bmp); return; }
    HDC mem = CreateCompatibleDC(refDc);
    if (!mem) { DeleteObject(bmp); return; }
    HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

    Fill(mem, 0, 0, w, h, C_BG);
    DrawHeader(mem);
    DrawKeys(mem);

    SelectObject(mem, old);
    DeleteDC(mem);
    if (g_kbCacheBmp) DeleteObject(g_kbCacheBmp);
    if (g_kbCacheDc) DeleteDC(g_kbCacheDc);
    g_kbCacheBmp = bmp;
    g_kbCacheDc = CreateCompatibleDC(refDc);
    if (g_kbCacheDc) SelectObject(g_kbCacheDc, bmp);
    g_kbCacheBits = (RGBQUAD*)bits;
    g_kbCacheRow = w;
    g_kbCacheW = w;
    g_kbCacheH = h;
}

static void EnsureKbFrameCache(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd) || g_ww <= 0 || g_wh <= 0) return;
    // 必须零初始化：签名是 memcmp 全字节比较，结构体里有填充字节，
    // 留着不确定值会让缓存永远打不中（每帧都全量重绘）。
    KbFrameSig sig = {};
    sig.w = g_ww; sig.h = g_wh;
    sig.hk = g_hk; sig.pk = g_pk; sig.hdrHov = g_hdrHov;
    sig.layoutMode = g_layoutMode; sig.nk = g_nk;
    sig.themeBg = g_themeBuf.pageBg;
    sig.hue = g_hue;
    sig.keyIconStyle = g_keyIconStyle;   // 漏掉这一项 → 切换图标样式后主键盘不刷新（看似"没生效"）
    sig.dpi = (float)GetSystemDpiScale();
    sig.sh = g_sh; sig.ct = g_ct; sig.al = g_al; sig.cp = g_cp;
    sig.winKey = g_winKey; sig.physShift = g_physShift; sig.physWin = g_physWin;
    sig.physNum = g_physNum;             // 实体 NumLock 灯变 → 数字区 Num 键高亮要跟上
    sig.fnLayer = g_fnLayer; sig.showFKeys = g_showFKeys; sig.fnWebLayout = g_fnWebLayout;
    sig.shiftSymbols = g_shiftSymbols; sig.lang = g_lang;
    sig.npHidden = NumpadHidden();
    if (g_kbCacheBmp && g_kbCacheW == g_ww && g_kbCacheH == g_wh && !(sig != g_kbSig)) return;

    HDC dc = GetDC(hWnd);
    RenderKbFrameInto(dc, g_ww, g_wh);
    ReleaseDC(hWnd, dc);
    g_kbSig = sig;
}

static void ReleaseKbFrameCache() {
    if (g_kbCacheBmp) { DeleteObject(g_kbCacheBmp); g_kbCacheBmp = 0; }
    if (g_kbCacheDc) { DeleteDC(g_kbCacheDc); g_kbCacheDc = 0; }
    g_kbCacheBits = 0;
    g_kbCacheRow = g_kbCacheW = g_kbCacheH = 0;
}

static void StopWindowMotion(WindowMotion* motion) {
    if (!motion || !motion->active) return;
    if (motion->hWnd && IsWindow(motion->hWnd))
        KillTimer(motion->hWnd, TIMER_WINDOW_ANIM);
    motion->active = FALSE;
}

static void StartWindowMotion(WindowMotion* motion, HWND hWnd, int x,
                              int fromY, int toY, UINT duration,
                              WindowMotionFinish finish) {
    if (!motion || !hWnd || !IsWindow(hWnd)) return;
    StopWindowMotion(motion);

    motion->hWnd = hWnd;
    motion->x = x;
    motion->fromY = fromY;
    motion->toY = toY;
    motion->duration = duration ? duration : 1;
    motion->started = QpcNowMs();
    motion->lastY = fromY;
    motion->finish = finish;
    motion->active = TRUE;

    // 显示前先在隐藏状态下同步完成首帧绘制：DWM 首次合成时窗口内容已就绪，
    // 消除首帧全量绘制造成的可见顿挫
    RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    SetWindowPos(hWnd, HWND_TOPMOST, x, fromY, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    RedrawWindow(hWnd, NULL, NULL,
                 RDW_INVALIDATE | RDW_ERASE | RDW_FRAME | RDW_UPDATENOW);
    UpdateWindow(hWnd);
    if (!SetTimer(hWnd, TIMER_WINDOW_ANIM, 15, NULL)) {
        SetWindowPos(hWnd, HWND_TOPMOST, x, toY, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE);
        motion->active = FALSE;
        if (finish == MOTION_HIDE) ShowWindow(hWnd, SW_HIDE);
        else if (finish == MOTION_DESTROY) DestroyWindow(hWnd);
    }
}

static BOOL TickWindowMotion(WindowMotion* motion, HWND hWnd) {
    if (!motion || !motion->active || motion->hWnd != hWnd) return FALSE;

    LONGLONG elapsed = QpcNowMs() - motion->started;
    double t = (double)elapsed / (double)motion->duration;
    if (t > 1.0) t = 1.0;
    double eased = t * t * (3.0 - 2.0 * t);
    int y = motion->fromY + (int)((motion->toY - motion->fromY) * eased + 0.5);
    if (y != motion->lastY) {
        SetWindowPos(hWnd, HWND_TOPMOST, motion->x, y, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
        motion->lastY = y;
    }

    if (t < 1.0) return TRUE;

    WindowMotionFinish finish = motion->finish;
    KillTimer(hWnd, TIMER_WINDOW_ANIM);
    motion->active = FALSE;
    if (finish == MOTION_HIDE) {
        ShowWindow(hWnd, SW_HIDE);
    } else if (finish == MOTION_DESTROY) {
        DestroyWindow(hWnd);
    }
    return TRUE;
}

static void ShowKB(BOOL show, BOOL isManual) {
    // 诊断（-afdiag）：谁在显示/隐藏键盘
    {
        char sb[96];
        _snprintf_s(sb, sizeof(sb), _TRUNCATE, "%s|manual=%d",
                    g_dbgShowFrom, (int)isManual);
        AfLog("ShowKB", GetForegroundWindow(), NULL, NULL, 0, 0, 0, 0, g_vis,
              g_mainMotion.active, sb);
        g_dbgShowFrom = "(?)";
    }
    if (!g_hWnd) return;
    if (g_exiting && show) return;
    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int sx, targetY;
    RECT saved;
    if (LoadLayoutWindowRect(&saved) && LayoutRectOnScreen(saved)) {
        sx = saved.left;        // 智能记忆上次打开的位置
        targetY = saved.top;
    } else {
        sx = work.left + ((work.right - work.left) - g_ww) / 2;
        targetY = work.bottom - g_wh - 6;
    }

    if (show) {
        if (isManual) {
            g_manualShow = TRUE;
            g_manualHide = FALSE;
            g_userHidInInput = FALSE;   // 手动重新显示后恢复常规自动呼出逻辑
        }
        if (g_vis) {
            StopWindowMotion(&g_mainMotion);
            SetWindowPos(g_hWnd, HWND_TOPMOST, sx, targetY, g_ww, g_wh,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
            RedrawWindow(g_hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
            return;
        }
        RECT current = {0};
        int fromY = work.bottom;
        if (g_mainMotion.active && GetWindowRect(g_hWnd, &current)) fromY = current.top;
        g_vis = TRUE;
        // 主键盘呼出保留自绘底部上滑动画
        StartWindowMotion(&g_mainMotion, g_hWnd, sx, fromY, targetY, 220, MOTION_NONE);
    } else {
        if (!g_vis) return;
        g_manualShow = FALSE;
        g_hdrHov = -1;
        g_lht = GetTickCount();
        RECT current = {0};
        int fromY = targetY;
        if (GetWindowRect(g_hWnd, &current)) fromY = current.top;
        g_vis = FALSE;
        StartWindowMotion(&g_mainMotion, g_hWnd, sx, fromY, work.bottom, 150, MOTION_HIDE);
    }
}

static void ToggleKB() { ShowKB(!g_vis, TRUE); }

// 临时收起（「收起」键 K_HIDE 与标题栏 HDR_MIN 共用）：
//   自动收起开启时，若焦点在输入框里记下该控件，同一输入框内不再自动回弹
//   （焦点换到别的框后恢复自动呼出）。
//
// ⚠ 这里**故意不置 g_manualHide** —— 自动呼出是这个键盘的核心功能，
// 最小化只是「暂时不要」，不能等同于「我不想看到它」。
// 「明确不想看到」的语义由标题栏的 ⏑ 按钮（HideToTray）承担，见其定义。
static void UserHideKeyboard() {
    // ⚠ 防回弹记录**无条件生效**（原来受 g_afAutoHide 控制，但那个开关已改为
    //   管"自动隐藏"）。防回弹是必要条件：没有它，用户手动收起后会被
    //   UpdateAutoVisibility 立刻弹回来，"手动收起"这个动作就失效了。
    {
        HWND input = GetFocusedInputControl();
        if (input) {
            g_userHidInInput = TRUE;
            g_hiddenInputToken = g_detectedInputToken;
            g_userHidInTick = GetTickCount();
        }
    }
    ShowKB(FALSE, TRUE);
}

// 永久隐藏到托盘：置 g_manualHide，UpdateAutoVisibility 的
// `if (!g_manualHide && !g_vis) ShowKB(TRUE)` 就再也弹不回来。
// 恢复入口 = 单击托盘图标（走 ToggleKB → ShowKB(TRUE, TRUE)，
// isManual 分支会清 g_manualHide）。
static void HideToTray() {
    g_manualHide = TRUE;
    ShowKB(FALSE, TRUE);
}

static void ExitApplicationAnimated() {
    // 主键盘退出保留自绘下滑动画（工具窗口无法触发 DWM 原生过渡）
    if (!g_hWnd || !IsWindow(g_hWnd)) return;
    if (g_exiting) return;
    g_exiting = TRUE;
    if (!IsWindowVisible(g_hWnd)) {
        DestroyWindow(g_hWnd);
        return;
    }
    RECT work = {0}, current = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    GetWindowRect(g_hWnd, &current);
    g_vis = FALSE;
    StartWindowMotion(&g_mainMotion, g_hWnd, current.left, current.top,
                      work.bottom, 160, MOTION_DESTROY);
}

// × 关闭：已记住选择则直接按所记方式执行，否则弹出关闭方式提示窗口
static void HandleCloseAction(HWND hWnd) {
    (void)hWnd;
    if (g_rememberClose) {
        if (g_closeToTray) {
            g_manualHide = TRUE;      // 显式隐藏到托盘后不再自动弹出
            ShowKB(FALSE, FALSE);
        } else if (g_hWnd && IsWindow(g_hWnd)) {
            ExitApplicationAnimated();
        }
        return;
    }
    OpenClosePrompt();
}

static HICON LoadMainIcon(int size) {
    HICON h = (HICON)LoadImageA(g_hInst, MAKEINTRESOURCE(100), IMAGE_ICON, size, size, LR_DEFAULTCOLOR);
    if (!h) {
        h = (HICON)LoadImageA(NULL, "winres\\main.ico", IMAGE_ICON, size, size, LR_LOADFROMFILE);
    }
    if (!h) {
        h = (HICON)LoadImageA(NULL, "main.ico", IMAGE_ICON, size, size, LR_LOADFROMFILE);
    }
    if (!h) {
        h = LoadIconA(NULL, IDI_APPLICATION);
    }
    return h;
}

static void AddTray() {
    if (g_tray) return;
    memset(&g_nid, 0, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hWnd;
    g_nid.uID = 1003;
    g_nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    if (!g_hTrayIcon) g_hTrayIcon = LoadMainIcon(16);
    g_nid.hIcon = g_hTrayIcon;
    wcscpy(g_nid.szTip, T(L"\x8F7B\x952E", L"HKeyboard"));
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    g_tray = TRUE;
}

static void ShowHelpDialog(HWND hWnd) {
    MessageBoxW(hWnd,
        T(
        L"\x3010\x547D\x4EE4\x884C\x53C2\x6570\x8BF4\x660E (CLI Parameters)\x3011\n"
        L"  -h / -help / -? : \x663E\x793A\x672C\x547D\x4EE4\x884C\x53C2\x6570\x5E2E\x52A9\n"
        L"  -show      : \x542F\x52A8\x65F6\x76F4\x63A5\x5F39\x51FA\x663E\x793A\x952E\x76D8\n"
        L"  -hide      : \x542F\x52A8\x65F6\x9759\x9ED8\x9690\x85CF\x5230\x7CFB\x7EDF\x6258\x76D8\n"
        L"  -min / -tray: \x6700\x5C0F\x5316\x9A7B\x7559\x6258\x76D8\n"
        L"  -touchonly : \x89E6\x6478\x5C4F\x4E13\x5C5E\xFF0C\x975E\x89E6\x6478\x8BBE\x5907\x81EA\x52A8\x9000\x51FA\n"
        L"  -auto      : \x9ED8\x8BA4\x542F\x7528\x70B9\x51FB\x7F16\x8F91\x6846\x81EA\x52A8\x547C\x51FA\n"
        L"  -noauto    : \x9ED8\x8BA4\x5173\x95ED\x70B9\x51FB\x7F16\x8F91\x6846\x81EA\x52A8\x547C\x51FA\n"
        L"  -dark      : \x5F3A\x5236\x6DF1\x8272\x4E3B\x9898\n"
        L"  -light     : \x5F3A\x5236\x6D45\x8272\x4E3B\x9898\n"
        L"  -theme:system : \x8DDF\x968F\x7CFB\x7EDF\x4E3B\x9898\xFF08\x9ED8\x8BA4\xFF09\n"
        L"  -wallpaper   : \x4E3B\x9898\x8272\x76F8\x8DDF\x968F\x7CFB\x7EDF\x58C1\x7EB8\x81EA\x52A8\x63D0\x53D6\x7684\x5F3A\x8C03\x8272\xFF08\x9ED8\x8BA4\x5173\x95ED\xFF09",
        L"[Command-line Parameters]\n"
        L"  -h / -help / -? : Show this help\n"
        L"  -show      : Show the keyboard on startup\n"
        L"  -hide      : Start hidden in the system tray\n"
        L"  -min / -tray: Minimize to the tray\n"
        L"  -touchonly : Touch-screen only; exits on non-touch devices\n"
        L"  -auto      : Enable auto pop-up when clicking an input box\n"
        L"  -noauto    : Disable auto pop-up when clicking an input box\n"
        L"  -dark      : Force dark theme\n"
        L"  -light     : Force light theme\n"
        L"  -theme:system : Follow the system theme (default)\n"
        L"  -wallpaper   : Theme hue follows the wallpaper accent (default off)"),
        T(L"\x547D\x4EE4\x884C\x53C2\x6570\x5E2E\x52A9", L"Command-line Parameters"),
        MB_OK | MB_ICONINFORMATION);
}


// ========== 设置页面（分 Tab） ==========
#define S_HIT_NONE           0
#define S_HIT_CLOSE          1
#define S_HIT_TAB0           2
#define S_HIT_TAB1           3
#define S_HIT_TAB2           4
#define S_HIT_AUTO           10
// 下面这些 *_DROP 命中码现在表示「点了该行的画框选择」，段下标在点击时按 x 算
#define S_HIT_LAYOUT_DROP    14
#define S_HIT_FKEYS          18
#define S_HIT_FNWEB          94
#define S_HIT_NPBTN          24   // 布局 Tab：显示标题栏 123 切换按钮
#define S_HIT_NPBTN          24   // 布局 Tab：标题栏显示小键盘按钮
#define S_HIT_SHIFTSYM       19
#define S_HIT_THEME_DROP     20
#define S_HIT_URL            30
#define S_HIT_LICENSE        32   // 关于 Tab：开源许可行的「查看」按钮
#define S_HIT_CLOSE_DROP     70
#define S_HIT_LANG_DROP      50
#define S_HIT_HL_DROP        60
#define S_HIT_HL_BOX         64
#define S_HIT_HL_HUE         65
#define S_HIT_HL_PAL0        80
#define S_HIT_REMEMBER       95
#define S_HIT_OPACITY_DROP   96

static int  g_sTab = 0;        // 0=常规 1=布局 2=主题 3=关于（见 k_settingsTabHits 的映射）
static int  g_sHov = -1;       // 悬停元素，-1=无
static BOOL g_sTracking = FALSE;
static BOOL g_settingsClosing = FALSE;
static BOOL g_settingsMoving = FALSE;
// 设置页各行的命中码（分段控件一个码，段下标在点击时按 x 算）
static BOOL g_hlEditFocus = FALSE;     // HEX 输入框是否处于编辑态
static wchar_t g_hlEditBuf[8] = {0};   // 编辑中的 HEX 文本（#RRGGBB）
static int g_hlSliderDrag = S_HIT_NONE;
static const wchar_t* g_langNames[2] = { L"简体中文", L"English" };
static const wchar_t* g_langNamesEn[2] = { L"Chinese", L"English" };   // 与中文侧「简体中文 / English」对称
static const wchar_t* g_hlModeNames[2] = { L"自定义色相", L"跟随壁纸" };
static const wchar_t* g_hlModeNamesEn[2] = { L"Custom Hue", L"Follow Wallpaper" };
static const wchar_t* g_themeNames[3] = { L"跟随系统", L"深色主题", L"浅色主题" };
static const wchar_t* g_themeNamesEn[3] = { L"Follow System", L"Dark Theme", L"Light Theme" };
static const int g_opacityValues[6] = { 100, 90, 80, 70, 60, 50 };
static const wchar_t* g_opacityNames[6] = { L"100%（不透明）", L"90%", L"80%", L"70%", L"60%", L"50%" };
static const wchar_t* g_opacityNamesEn[6] = { L"100% (Opaque)", L"90%", L"80%", L"70%", L"60%", L"50%" };
static const wchar_t* g_layoutNames[3] = { L"默认", L"小键盘", L"全尺寸" };
static const wchar_t* g_layoutNamesEn[3] = { L"Default", L"Numpad", L"Full" };

static int g_switchAnimHit = S_HIT_NONE;
static LONGLONG g_switchAnimStart = 0;
static BOOL g_switchAnimFrom = FALSE;
static BOOL g_switchAnimTo = FALSE;

// 关闭对话框的单选圆点已改为「勾选方块 + 整行底色」语法（见 PromptDraw），
// 原来的 DrawRadio（圆环 + 挖空 + 实心点）随之无调用点，删除。
//
// 开关按钮（on=开启；通常右侧对齐显示）
static void DrawSwitch(HDC dc, int x, int y, int w, int h, BOOL on) {
    DrawRoundRect(dc, x, y, w, h, on ? C_HOT : C_DARK, C_KEY_BORDER, h / 2);
    int knob = h - 6;
    int kx = on ? x + w - knob - 3 : x + 3;
    DrawRoundRect(dc, kx, y + 3, knob, knob, on ? C_KEY : C_DIM, on ? C_KEY : C_DIM, knob / 2);
}

// ========== 设置页度量与布局 ==========
// 语法对齐制作工具：一个 tab 就是一张卡，卡内逐行，行间画 1px 分隔线。
// 行高不再是常数（下拉/分段行 40 高，比开关行高），所以行位置由累计高度算出；
// 绘制与命中都走下面同一组函数，避免两边各算一遍。
struct SettingsMetrics {
    double dpi;
    int W, H;
    int margin;
    int titleY, titleH, headIcon;      // 页面标题 + 页面头 38×38 图标 tile
    int closeX, closeY, closeW, closeH;
    int tabsY, tabH, tabGap;           // tab 高 42、间隙 28
    int contentX, contentY, contentW;
    int rowPadY;                       // 行上下内边距 12
    int tileSize, tileGap;             // 行图标 30×30 + 与文字的间距 18
    int comboW, comboH;
    int switchW, switchH;
};

static SettingsMetrics GetSettingsMetrics(HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    SettingsMetrics m = {};
    m.dpi = GetSystemDpiScale();
    m.W = rc.right; m.H = rc.bottom;
    m.margin = (int)(30 * m.dpi);
    // 顶部留白比左右边距略小即可：原来只有 10 DIP，页头会顶在窗口上沿，
    // 与 30 DIP 的左右/下边距不成比例。这里给到 20 DIP，其余间距按同一节奏排。
    m.titleY = (int)(20 * m.dpi);
    m.titleH = (int)(34 * m.dpi);
    m.headIcon = (int)(38 * m.dpi);
    m.closeW = m.closeH = (int)(28 * m.dpi);
    m.closeX = m.W - m.margin - m.closeW;
    m.closeY = m.titleY + (m.titleH - m.closeH) / 2;   // 与页面标题垂直居中对齐
    m.tabsY = m.titleY + m.headIcon + (int)(10 * m.dpi);
    m.tabH = (int)(42 * m.dpi);
    m.tabGap = (int)(28 * m.dpi);
    m.contentX = m.margin;
    m.contentY = m.tabsY + m.tabH + (int)(14 * m.dpi);
    m.contentW = m.W - m.margin * 2;
    m.rowPadY = (int)(12 * m.dpi);
    m.tileSize = (int)(30 * m.dpi);
    m.tileGap = (int)(18 * m.dpi);
    m.comboW = (int)(176 * m.dpi);
    m.comboH = (int)(40 * m.dpi);
    m.switchW = (int)(46 * m.dpi);
    m.switchH = (int)(26 * m.dpi);
    return m;
}

// 全尺寸(2) / 小键盘(1) 布局下「不存在」的行 —— 它们只对默认布局有意义：
//   常规 Tab：功能键行、Shift 符号     布局 Tab：Fn 网页布局
// 这些行**高度给 0**、绘制与命中也一并跳过：SettingsRowRect 是按行高累加出来的，
// 高度归零后后面的行自动上移，绘制 / 命中 / 点击读的仍是同一份几何，不会出现「画一行点一行」。
//
// ⚠⚠ 布局 Tab 的 index 是**按绘制顺序从 0 数**的，别把注释当行号：
//   0 = 键盘布局   1 = Fn 网页布局   2 = 小键盘按钮
//   这里原来写的是 `index == 2`（注释却写着「Fn 网页布局」），于是小键盘 / 全尺寸布局下
//   **藏错了行**：Fn 网页布局照画（小键盘按钮被挤到它下面），而真正该藏的
//   「小键盘按钮」高度归 0 却仍被绘制 —— 绘制处拿到的 row 高度为 0，
//   SettingsRowHeadH 返回 0，文字块整体上移约 44 DIP，直接压在 Fn 行的描述上
//   （实机截图：三行文字糊成一团，卡片底部还露出去）。
static BOOL SettingsRowHidden(int tab, int index) {
    BOOL onlyDefault = (g_layoutMode != 0);
    if (tab == 0) {
        int closeRow = g_af ? 2 : 1;
        if (index == closeRow + 2) return onlyDefault;   // 功能键行
        if (index == closeRow + 3) return onlyDefault;   // Shift 符号
        return FALSE;
    }
    // Fn 网页布局两条件任一成立就藏：
    //  a) 非默认布局 —— 小键盘 / 全尺寸布局压根没有 Fn 层
    //  b) 开了功能键行 —— 此时底排走 kDefRow4NoFn，**Fn 键被移除**、空出的 1u 并进空格
    //     （5.5 → 6.5u，见 BuildDefaultKeys）。键都没了，留着这行开关纯属摆设。
    //
    // ⚠ 只藏 UI 行，**不改 g_fnWebLayout 的持久化值**：用户关掉功能键行后，
    //   原来的网页布局设置立刻原样恢复，不用重新开一次。这是「隐藏入口」不是「禁用功能」。
    //   （反过来若在这里顺手把 g_fnWebLayout 置 FALSE，就成了静默改用户配置。）
    if (tab == 3) return (onlyDefault || g_showFKeys) && index == 1;
    return FALSE;
}

// 每行的「控件高」，单位 DIP（不乘 dpi）：开关 26 / 下拉与分段 40；无控件行给 0
static int SettingsRowCtrlDip(int tab, int index) {
    if (tab == 0) {                                 // 常规
        int closeRow = g_af ? 2 : 1;
        if (index == closeRow) return 40;           // 关闭按钮下拉
        if (index == closeRow + 4) return 40;       // 界面语言下拉
        return 26;
    }
    if (tab == 3) {                                 // 布局
        if (index == 0) return 40;                  // 键盘布局分段
        return 26;
    }
    if (tab == 1) return 40;                        // 主题：模式 / 透明度 / 色相 都是下拉行
    return 0;
}

// 描述**可能**需要两行的行（文案本身长，窗口窄时一行放不下会被硬截断）：
// 行高、绘制、命中三处都读这一份判断，才不会出现「字画到行外 / 热区对不上」。
static BOOL SettingsRowDescWraps(int tab, int index) {
    // Fn 网页布局（换文案后变长）/ 小键盘按钮：可能折成两行
    return (tab == 3 && (index == 1 || index == 2));
}

// 这两行的描述文案。必须是**唯一**的定义处：行高要不要按两行留白，靠实测这段文本
// 来定（见 SettingsRowDescTwoLines），绘制处再抄一遍就会两边不一致。
static const wchar_t* SettingsRowDescText(int tab, int index) {
    if (tab != 3) return NULL;
    if (index == 1)
        return T(L"按 Fn 切换布局，显示常用符号和常用网站前后缀",
                 L"Press Fn to switch layout: common symbols and website prefixes/suffixes on the keys");
    if (index == 2)
        return T(L"在标题栏显示圆角按钮：在默认布局上切换小键盘布局，全尺寸布局下则显示/隐藏数字区",
                 L"Pill button in the title bar: switches to the numpad layout in the default layout, "
                 L"shows/hides the numpad section in the full-size layout");
    return NULL;
}

// 「需要两行」只是可能性 —— 中文短、英文长，窗口宽度也会变。真放得下一行时还按两行
// 留白，描述下方就会空出一大块（实机 30 DIP，一眼可见）：行高 80 DIP 而内容只占 52。
// 所以行高按实测决定：一行 → 64 DIP，两行 → 80 DIP。
// 命中测试与窗口高度都走 SettingsRowHeight，因此这个判断必须与绘制**同一时刻**成立。
static BOOL SettingsRowDescTwoLines(const SettingsMetrics& m, int tab, int index) {
    if (!SettingsRowDescWraps(tab, index)) return FALSE;
    const wchar_t* desc = SettingsRowDescText(tab, index);
    if (!desc || !desc[0]) return FALSE;
    // 文本列可用宽：卡片右内边距(20) − 开关(46) −「开/关」与开关之间的留白(42)
    // − 控件前间距(12)，左边是文字列起点（tile 内边距 20 + tile 30 + tileGap 18）。
    int tx = m.contentX + (int)(20 * m.dpi) + m.tileSize + m.tileGap;
    int right = m.contentX + m.contentW - (int)(20 * m.dpi) - m.switchW
                - (int)(42 * m.dpi) - (int)(12 * m.dpi);
    HDC dc = GetDC(0);
    // 字体还没建时（极早的尺寸询问）按两行算：宁可多留 16 DIP，也不要把字挤出窗口。
    int need = g_sfBase ? MeasureTextAdvW(dc, desc, g_sfBase) : 0x7fffffff;
    ReleaseDC(0, dc);
    return need > right - tx;
}

// 行高 = (12 + max(图标 tile 30, 控件高, 文字块高) + 12) 个 DIP，最后统一乘 dpi。
// ⚠ 全长度必须同单位再乘 dpi：这里曾经写成 `m.rowPadY * 2 + content`，
//   其中 rowPadY 已乘过 dpi、content 还是 DIP，于是高 DPI 下行高偏小 ——
//   表现为整张卡比预期矮一截、底部留一大片空白、两行文字挤在一起。
// 主题 tab 的色相行是可展开行：展开态固定 210 DIP（内含分隔线 + 色板 + 滑轨 + HEX）
static int SettingsRowHeight(const SettingsMetrics& m, int index) {
    if (SettingsRowHidden(g_sTab, index)) return 0;      // 见 SettingsRowHidden：后续行随之上移
    if (g_sTab == 1 && index == 2 && !g_wallpaperAccent) return (int)(210 * m.dpi);
    int contentDip = SettingsRowCtrlDip(g_sTab, index);
    if (contentDip < 30) contentDip = 30;   // 图标 tile
    // 文字块：标题 18 + 间距 6 + 描述 16 = 40。间距不能省 —— 标题的下缘和描述的上缘
    // 会顶在一起（实机反馈「文本和描述的间距对吗」）；文字蒙版本身还上下各留了 4px。
    if (contentDip < 40) contentDip = 40;
    // 描述真会折成两行时内容高 40 → 58（多 18 DIP）。原来这里是「可能折行就留」，
    // 于是中文单行的「小键盘按钮」行高 80 DIP、内容只占 52，描述下方空 30 DIP。
    if (SettingsRowDescTwoLines(m, g_sTab, index) && contentDip < 58)
        contentDip = 58;                    // 标题 20 + 间距 6 + 描述两行 32（与 DrawSettingRowContent 的块高一致）
    return (int)((12 + contentDip + 12) * m.dpi);
}

static int SettingsRowCount(int tab) {
    if (tab == 0) return g_af ? 7 : 6;
    if (tab == 3) return 3;   // 布局 Tab：键盘布局 / Fn 网页布局 / 小键盘按钮
    if (tab == 1) return 3;
    return 0;
}

static int SettingsRowTop(const SettingsMetrics& m, int index) {
    int y = m.contentY;
    for (int i = 0; i < index; i++) y += SettingsRowHeight(m, i);
    return y;
}

static RECT SettingsRowRect(const SettingsMetrics& m, int index) {
    int y = SettingsRowTop(m, index);
    RECT r = {m.contentX, y, m.contentX + m.contentW, y + SettingsRowHeight(m, index)};
    return r;
}

// 整张卡：行的 12px 上下内边距就是卡片的内边距，卡片自身不再留白
static RECT SettingsCardRect(const SettingsMetrics& m) {
    RECT r = {m.contentX, m.contentY, m.contentX + m.contentW, m.contentY};
    int n = SettingsRowCount(g_sTab);
    for (int i = 0; i < n; i++) r.bottom += SettingsRowHeight(m, i);
    return r;
}

// 卡片：圆角 16、底 C_KEY、无描边无阴影；行间 1px 分隔线（左右各缩进 20），最后一行下方不画
static void DrawSettingsCard(HDC dc, const SettingsMetrics& m) {
    RECT card = SettingsCardRect(m);
    DrawRoundRect(dc, card.left, card.top, card.right - card.left, card.bottom - card.top,
                  C_KEY, C_KEY, (int)(16 * m.dpi));
    int n = SettingsRowCount(g_sTab);
    for (int i = 0; i + 1 < n; i++) {
        if (SettingsRowHidden(g_sTab, i + 1)) continue;   // 隐藏行（全尺寸/小键盘下）不画分隔线
        int y = SettingsRowRect(m, i + 1).top;
        Fill(dc, card.left + (int)(20 * m.dpi), y,
             (card.right - card.left) - (int)(40 * m.dpi), 1, C_LINE_DIV);
    }
}

// 行内元素定位：tile 在左，文字块起点 = 卡左 + 20 + 30 + 18
static int SettingsRowTileX(const SettingsMetrics& m) { return m.contentX + (int)(20 * m.dpi); }
static int SettingsRowTextX(const SettingsMetrics& m) { return SettingsRowTileX(m) + m.tileSize + m.tileGap; }

// 行的「头部」高度：图标 tile、文字块、行内控件（开关 / 分段 / 下拉）**共用同一段居中**。
//
// ⚠ 折叠行必须用**整行高**，不能写死 `rowPadY*2 + 40`（= 64 DIP）。
//   布局页「Fn 网页布局」「小键盘按钮」两行的描述会折成两行，整行高 80 DIP。
//   写死 64 时，多出的 16 DIP 全被推到行底，而头部又贴在行顶 ——
//   实机量到 tile 中心 23.61 / 文字块心 32.82 / 行心 40.59：
//   标题距行顶只剩 5.18，描述第二行下方空 20.73，**整块内容浮在上半部**。
//   改成整行居中后三者都落在行心 40.59，顶底各 12.95 对称。
//
//   原文写的理由是「控件若按整行居中，就会比 tile 与文字块低 8 DIP」——
//   那是**控件按整行、tile 按 64 头部**两种基准混用才有的偏差。
//   三者统一读同一个 SettingsRowHeadH 之后差恒为 0。别再为了这个 8 DIP 拆成两套基准。
//
// ⚠⚠ **可展开行是例外，标题区必须贴在行顶**（头部高固定 64 DIP）。
//   主题页色相行展开后高 210 DIP，行内结构是：
//       0..64   标题区（tile + 「主题色相」+ 描述 + 模式分段）
//       64      分隔线
//       76..192 展开区（色板 76..102 / 滑轨 124..138 / HEX 160..192）
//   展开区的坐标全是**相对 row.top 的硬编码 DIP**，不随头部高度变化。
//   若标题区也按 210 居中，文字块盒顶会从 11 掉到 84、描述盒顶到 110，
//   **直接压在色板（76..102）上** —— 实机就是这个现象：
//   「主题色相」四个字被色块盖住，标题与描述叠在色板那一行。
//   所以这里必须区分：色相展开行（主题页 index 2）给 64，其他折叠行给整行高。
//   ⚠ 别去掉这个分支 —— 「整行居中」只对**折叠行**成立。
static int SettingsRowHeadH(const SettingsMetrics& m, const RECT& row) {
    int headH = m.rowPadY * 2 + (int)(40 * m.dpi);
    // 色相可展开行：标题区固定 64 DIP，展开区坐标是相对 row.top 的硬编码（见上方说明）。
    if (g_sTab == 1) return headH;
    // ⚠⚠ 折叠行返回**整行高 rowH**，不要写成 `headH > rowH ? headH : rowH` 之类的
    //   「取小值兜底」—— 那个写法在 headH(111px) < rowH(141px) 时会取 111，
    //   等于又按 64 DIP 头部居中，把 a8f61c8 修的 bug 原样带回来（实测距行顶 5.18）。
    //   不折行的行 headH == rowH（64 == 63.33），两者等价，所以直接返回 rowH 无副作用。
    return row.bottom - row.top;
}

static int SettingsComboY(const SettingsMetrics& m, const RECT& row) {
    return row.top + (SettingsRowHeadH(m, row) - m.comboH) / 2;
}

// 与 SettingsComboY 等价：头部高 = rowPadY*2 + comboH，在头部里居中后的偏移正好是 rowPadY。
static int SettingsComboYTop(const SettingsMetrics& m, const RECT& row) {
    return row.top + m.rowPadY;
}

// 开关行的文字块右界：给「开/关」文字（34）+ 开关（46）让位，
// 否则长描述会压到开关上（标签宽度按控件自适应，不写死）
static int SettingsSwitchTextRight(const SettingsMetrics& m, const RECT& row) {
    return row.right - (int)(20 * m.dpi) - m.switchW - (int)(42 * m.dpi);
}

// 行悬停底色：铺满整行、左右直接贴到卡片边缘（不再内缩，否则会切到图标 tile），
// 圆角取卡片同款半径 —— 首行/末行因此与卡片圆角正好嵌套，不会凸出到卡片外面。
static void DrawSettingsRowHover(HDC dc, const SettingsMetrics& m, const RECT& row) {
    int h = row.bottom - row.top;
    int r = (int)(16 * m.dpi);
    if (r > h / 2) r = h / 2;
    DrawRoundRect(dc, row.left, row.top, row.right - row.left, h,
                  C_REGULAR_HOV, C_REGULAR_HOV, r);
}

// 行内容：图标 tile + 两行文字块。ctrlLeft > 0 时文字块右侧让位给右对齐的控件
static void DrawSettingRowContent(HDC dc, const SettingsMetrics& m, const RECT& row,
                                  int iconId, const wchar_t* glyph,
                                  const wchar_t* title, const wchar_t* desc,
                                  BOOL hover, int ctrlLeft, BOOL descWrap = FALSE,
                                  BOOL noTile = FALSE) {
    // noTile：把这一行画成**子项**样式（左侧不画图标 tile）。
    // 文字位置不动 —— 子项标题与父项标题左对齐，视觉上的"内缩"来自少了个图标，
    // 参考 UU 远程设置页那种「父行带图标 / 子行不带图标」的层级表达。
    // ⚠ 不能只靠 iconId<0 表示：DrawIconTile 在 iconId<0 且无文字时仍会画一个
    //   空的圆角 tile（见其实现），所以必须显式跳过调用。
    // ⚠ 隐藏行（SettingsRowHidden 命中）的高度是 0，**不能进来**。
    //   行高 0 时 SettingsRowHeadH 返回 0，tile / 文字块 / 控件全部算出负偏移，
    //   直接糊在上一行上（实机截图：布局页三行文字叠成一团）。
    //   绘制处虽已逐处判 SettingsRowHidden，这里再兜一道 —— 将来新增行忘了加判断，
    //   最坏也只是「这一行不画」，不会再退化成满屏错位。
    if (row.bottom <= row.top) return;

    if (hover) DrawSettingsRowHover(dc, m, row);

    // 图标 tile 与文字块都按**折叠行整行**居中（见 SettingsRowHeadH）：行因为描述折行
    // 变高时，tile / 文字 / 控件三者一起下移，仍中心对中心。
    // 色相可展开行是例外（头部固定 64 DIP），见 SettingsRowHeadH 的说明。
    int headH = SettingsRowHeadH(m, row);
    int ty = row.top + (headH - m.tileSize) / 2;
    if (!noTile)
        DrawIconTile(dc, SettingsRowTileX(m), ty, m.tileSize, (int)(17 * m.dpi), iconId, glyph);

    int tx = SettingsRowTextX(m);
    int rightLimit = (ctrlLeft > 0) ? ctrlLeft - (int)(12 * m.dpi) : row.right - (int)(20 * m.dpi);
    int tw = rightLimit - tx;
    if (tw < (int)(60 * m.dpi)) tw = (int)(60 * m.dpi);

    // 兜底：右侧控件特别宽时文本列会被挤到画不下标题，GDI+ 在 NoWrap 下会把标题右侧
    // 整段裁掉（主题页「主界面透明度」就曾被裁成「主界面透」）。真的放不下时，允许标题
    // 一直画到控件左缘为止 —— 只吃掉那 12 DIP 的行内间距，不压住控件本身。
    // 宁可标题与控件贴在一起，也不要出现半截标题。
    int needTitle = MeasureTextW(dc, title, g_sfRow);
    if (ctrlLeft > 0 && tw < needTitle) {
        int hard = ctrlLeft - tx;
        if (hard > tw) tw = hard;
    }

    // 文字块与 tile「中心对中心」。tile 30 DIP，文字块 42 DIP（标题盒 20 + 间隙 6 +
    // 描述盒 16），**共用同一个 top 不行** —— 那样 tile 中心会比文字块中心高 6 DIP。
    // 块内两个盒的中心分别落在 块顶+10（标题）与 块顶+26+8（描述）处，块心 = 块顶+22。
    // 折行时描述占 32 DIP（两行），块心 = 块顶+29；两种块高各算各的，别共用 22。
    // （「行盒中心 vs 墨迹中心」的补偿已在 DrawTextGp 里统一做掉，这里按盒中心算即可。）
    int blockBottom = descWrap ? ((int)(26 * m.dpi) + (int)(32 * m.dpi))
                               : ((int)(26 * m.dpi) + (int)(16 * m.dpi));
    int textMid = blockBottom / 2;
    int tyText = ty + m.tileSize / 2 - textMid;

    DrawTextL(dc, tx, tyText, tw, (int)(20 * m.dpi), title, g_sfRow, C_WHITE);
    if (desc && desc[0]) {
        // 折行与不折行**同一个描述盒顶 26 DIP**，标题↔描述的间距因此恒定。
        // 原来折行把盒顶上移到 18 DIP，想让描述第一行对齐不折行的位置，但 18 已在
        // 标题盒（高 20）的底缘之下，标题下缘与描述上缘直接压在一起：实机量到标题↔描述
        // 1.7 DIP、描述两行之间 4.6 DIP，节奏反了（肉眼就是「标题粘住描述」）。
        if (descWrap)
            DrawTextL(dc, tx, tyText + (int)(26 * m.dpi), tw, (int)(32 * m.dpi), desc, g_sfBase, C_DIM, TRUE);
        else
            DrawTextL(dc, tx, tyText + (int)(26 * m.dpi), tw, (int)(16 * m.dpi), desc, g_sfBase, C_DIM);
    }
}

// ===== tab strip：文字宽 + 固定间隙，左起排布（不是固定宽度格子） =====
static void SettingsTabLabels(const wchar_t* out[4]) {
    out[0] = T(L"常规", L"General");
    out[1] = T(L"布局", L"Layout");
    out[2] = T(L"主题", L"Theme");
    out[3] = T(L"关于", L"About");
}

static const int k_settingsTabHits[4] = {S_HIT_TAB0, S_HIT_TABL, S_HIT_TAB1, S_HIT_TAB2};

static int MeasureTabWidth(HDC dc, const wchar_t* label, double dpi) {
    return MeasureTextW(dc, label, g_sfCtrl) + (int)(4 * dpi);
}

// 返回每个 tab 的矩形（顺序：常规 / 布局 / 主题 / 关于）
static void SettingsTabRects(const SettingsMetrics& m, const wchar_t* labels[4], RECT out[4]) {
    HDC dc = GetDC(0);
    int x = m.contentX + (int)(4 * m.dpi);
    for (int i = 0; i < 4; i++) {
        int w = MeasureTabWidth(dc, labels[i], m.dpi);
        out[i].left = x; out[i].top = m.tabsY;
        out[i].right = x + w; out[i].bottom = m.tabsY + m.tabH;
        x += w + m.tabGap;
    }
    ReleaseDC(0, dc);
}

static void DrawTabStrip(HDC dc, const SettingsMetrics& m) {
    const wchar_t* labels[4];
    SettingsTabLabels(labels);
    RECT tr[4];
    SettingsTabRects(m, labels, tr);
    int rule  = (int)(3 * m.dpi); if (rule < 2) rule = 2;   // 选中项下的主色横线
    int inset = (int)(3 * m.dpi);                           // 横线与文字之间的空隙
    int active = (g_sTab == 0) ? 0 : (g_sTab == 3 ? 1 : (g_sTab == 1 ? 2 : 3));
    for (int i = 0; i < 4; i++) {
        BOOL on = (i == active);
        DrawTextC(dc, tr[i].left, tr[i].top, tr[i].right - tr[i].left, m.tabH - rule - inset,
                  labels[i], g_sfCtrl, (on || g_sHov == k_settingsTabHits[i]) ? C_WHITE : C_DIM);
    }
    // strip 下缘 1px 分隔线；选中项底部主色横线压在它上面
    Fill(dc, m.contentX, m.tabsY + m.tabH, m.contentW, 1, C_LINE_DIV);
    if (active >= 0) {
        DrawRoundRect(dc, tr[active].left, m.tabsY + m.tabH - rule,
                      tr[active].right - tr[active].left, rule, C_HOT, C_HOT, rule / 2);
    }
}

// ===== 分段控件（三选一）：轨道 C_REGULAR + 选中段 C_KEY/C_BTN_CONTENT =====
// 选中段文字用 btn_content 而不是 primary：#53A3F2 在白卡上只有 2.66:1。

// 单个分段的宽度 = 文字「布局宽度」+ 左右各 14 DIP 内边距。
// 必须用 advance（MeasureTextAdvW）而不是 DrawString 的容纳宽度（MeasureTextW）：
// 容纳宽度两侧各含约 0.2em 的绘制安全边距，把它当成设计留白加进去，每段会凭空宽出约
// 10px（em 26px 时）。主题页「主界面透明度」那一行的分段控件有 6 段，多出来的宽度把
// 左侧文本列挤到只剩 142px，而标题「主界面透明度」需要 174px —— 于是被裁成「主界面透」。
// 段宽只喂给绘制与命中两条路径（三处共用本函数），且恒大于容纳宽度，不会造成段内文字被裁。
static int SegmentedItemWAtDpi(const wchar_t* item, double dpi) {
    HDC dc = GetDC(0);
    int adv = MeasureTextAdvW(dc, item, g_sfCtrl);
    if (adv <= 0) adv = MeasureTextW(dc, item, g_sfCtrl);   // GDI+ 未就绪时的回退
    ReleaseDC(0, dc);
    return adv + (int)(28 * dpi);
}
static int SegmentedItemW(HDC dc, const wchar_t* item, double dpi) {
    int adv = MeasureTextAdvW(dc, item, g_sfCtrl);
    if (adv <= 0) adv = MeasureTextW(dc, item, g_sfCtrl);   // GDI+ 未就绪时的回退
    return adv + (int)(28 * dpi);
}

static int SegmentedWidth(const SettingsMetrics& m, const wchar_t** items, int count) {
    HDC dc = GetDC(0);
    int w = (int)(6 * m.dpi);   // 轨道左右 padding 3
    for (int i = 0; i < count; i++) w += SegmentedItemW(dc, items[i], m.dpi);
    ReleaseDC(0, dc);
    return w;
}

static void DrawSegmented(HDC dc, const SettingsMetrics& m, const RECT& r,
                          const wchar_t** items, int count, int sel) {
    int pad = (int)(3 * m.dpi);
    DrawRoundRect(dc, r.left, r.top, r.right - r.left, r.bottom - r.top,
                  C_REGULAR, C_REGULAR, (int)(10 * m.dpi));
    int x = r.left + pad;
    int h = (r.bottom - r.top) - pad * 2;
    for (int i = 0; i < count; i++) {
        int w = SegmentedItemW(dc, items[i], m.dpi);
        if (i == sel) {
            DrawRoundRect(dc, x, r.top + pad, w, h, C_KEY, C_KEY, (int)(8 * m.dpi));
        }
        DrawTextC(dc, x, r.top + pad, w, h, items[i], g_sfCtrl, (i == sel) ? C_BTN_CONTENT : C_DIM);
        x += w;
    }
}

// 命中分段：返回段下标，未命中返回 -1（绘制与命中共用同一套段宽算法）
static int SegmentedHitIndex(const SettingsMetrics& m, const RECT& r,
                             const wchar_t** items, int count, int x) {
    HDC dc = GetDC(0);
    int pad = (int)(3 * m.dpi);
    int cx = r.left + pad;
    int hit = -1;
    for (int i = 0; i < count; i++) {
        int w = SegmentedItemW(dc, items[i], m.dpi);
        if (x >= cx && x < cx + w) { hit = i; break; }
        cx += w;
    }
    ReleaseDC(0, dc);
    return hit;
}

// ===== 设置行的「画框选择」（替代下拉框）=====
// 各选择类行统一走这几个函数：右对齐、垂直居中、宽度按选项文字量出来。
// 绘制与命中读同一份矩形算法（RowSegRect），不会再出现「画在左、热区在右」。
static RECT RowSegRect(const SettingsMetrics& m, const RECT& row,
                       const wchar_t** items, int count) {
    int w = SegmentedWidth(m, items, count);
    int x = row.right - (int)(20 * m.dpi) - w;
    int y = SettingsComboY(m, row);
    RECT r = {x, y, x + w, y + m.comboH};
    return r;
}
static int RowSegIndex(const SettingsMetrics& m, const RECT& row,
                       const wchar_t** items, int count, int x) {
    RECT r = RowSegRect(m, row, items, count);
    return SegmentedHitIndex(m, r, items, count, x);
}

// 各选择行的选项文案（中 / 英），返回项数
static int CloseSegItems(const wchar_t** out) {
    out[0] = T(L"退出程序", L"Exit");
    out[1] = T(L"隐藏到托盘", L"Tray");
    return 2;
}
static int LangSegItems(const wchar_t** out) {
    out[0] = T(L"简体中文", L"Chinese");
    out[1] = L"English";
    return 2;
}
static int LayoutSegItems(const wchar_t** out) {
    for (int i = 0; i < 3; i++) out[i] = g_lang ? g_layoutNamesEn[i] : g_layoutNames[i];
    return 3;
}
static int ThemeSegItems(const wchar_t** out) {
    for (int i = 0; i < 3; i++) out[i] = g_lang ? g_themeNamesEn[i] : g_themeNames[i];
    return 3;
}
// 透明度：下拉里那句「100%（不透明）」做成分段太宽，统一成纯百分比
static int OpacitySegItems(const wchar_t** out) {
    static const wchar_t* pct[6] = { L"100%", L"90%", L"80%", L"70%", L"60%", L"50%" };
    for (int i = 0; i < 6; i++) out[i] = pct[i];
    return 6;
}
// 色相行的模式选择（绘制在行顶部，与展开后的色板/滑轨对齐）
static int HueModeSegItems(const wchar_t** out) {
    out[0] = T(L"自定义", L"Custom");
    out[1] = T(L"跟随壁纸", L"Wallpaper");
    return 2;
}
static RECT RowSegRectTop(const SettingsMetrics& m, const RECT& row,
                          const wchar_t** items, int count) {
    RECT r = RowSegRect(m, row, items, count);
    r.top = SettingsComboYTop(m, row);
    r.bottom = r.top + m.comboH;
    return r;
}
// 顶部对齐版的分段命中（色相行的控件在行顶部，与展开后的色板/滑轨对齐）
static int RowSegIndexTop(const SettingsMetrics& m, const RECT& row,
                          const wchar_t** items, int count, int x) {
    RECT r = RowSegRectTop(m, row, items, count);
    return SegmentedHitIndex(m, r, items, count, x);
}

// 「按键图标样式」行已下线（实机反馈：只保留「图标+文字」）；g_keyIconStyle 固定为 2，
// 见 LoadConfig 与 DrawKeyLabel。

static RECT SettingsHexRect(const SettingsMetrics& m, const RECT& row) {
    int w = (int)(150 * m.dpi), h = (int)(32 * m.dpi);
    RECT r = {row.left + (int)(74 * m.dpi), row.top + (int)(160 * m.dpi),
              row.left + (int)(74 * m.dpi) + w, row.top + (int)(160 * m.dpi) + h};
    return r;
}

static void SettingsPaletteMetrics(const SettingsMetrics& m, const RECT& row,
                                   int* x, int* y, int* size, int* gap) {
    *x = row.left + (int)(30 * m.dpi);
    *y = row.top + (int)(76 * m.dpi);
    *size = (int)(26 * m.dpi);
    *gap = (int)(8 * m.dpi);
}

static RECT SettingsColorSliderRect(const SettingsMetrics& m, const RECT& row) {
    int x = row.left + (int)(30 * m.dpi);
    int y = row.top + (int)(124 * m.dpi);
    RECT r = {x, y, row.right - (int)(30 * m.dpi), y + (int)(14 * m.dpi)};
    return r;
}

static RECT SettingsSwitchRect(const SettingsMetrics& m, int hit) {
    // 常规 Tab 行序（自动隐藏行仅在自动呼出开启时存在）：
    //   g_af 开：0=自动呼出 1=自动隐藏 2/3=关闭按钮/记住选择 4=功能键行 5=Shift符号 6=界面语言
    //   g_af 关：0=自动呼出 1/2=关闭按钮/记住选择 3=功能键行 4=Shift符号 5=界面语言
    // 布局 Tab（「按键图标样式」行已删）：0=键盘布局 1=Fn 网页布局 2=小键盘按钮
    int rowIndex;
    if (hit == S_HIT_AUTO) rowIndex = 0;
    else if (hit == S_HIT_AUTOHIDE) rowIndex = 1;
    else if (hit == S_HIT_FNWEB) rowIndex = 1;
    else if (hit == S_HIT_NPBTN) rowIndex = 2;
    else if (hit == S_HIT_REMEMBER) rowIndex = g_af ? 3 : 2;
    else if (hit == S_HIT_FKEYS) rowIndex = g_af ? 4 : 3;
    else rowIndex = g_af ? 5 : 4;
    RECT row = SettingsRowRect(m, rowIndex);
    row.left = row.right - (int)(120 * m.dpi);
    return row;
}

static void BeginSwitchAnimation(HWND hWnd, int hit, BOOL from, BOOL to) {
    if (g_settingsMoving) return;
    g_switchAnimHit = hit;
    g_switchAnimStart = QpcNowMs();
    g_switchAnimFrom = from;
    g_switchAnimTo = to;
    SetTimer(hWnd, TIMER_SETTINGS_ANIM, 16, NULL);
}

static void DrawSettingSwitch(HDC dc, const SettingsMetrics& m, const RECT& row, BOOL on, int hit) {
    int x = row.right - (int)(20 * m.dpi) - m.switchW;
    // 开关与「开/关」文字跟 tile / 文字块用同一条基准（见 SettingsRowHeadH）。
    // 原来这里刻意按 64 DIP 的「头部」居中、理由是「否则会比标题低 8 DIP」——
    // 那是控件与 tile 用了两套基准才有的偏差。三者统一后差为 0，而折行行
    // 不再出现「内容全浮在上半部、行底空 20 DIP」的问题。
    // ⚠ 本函数只在常规页 / 布局页用（那里没有可展开行），所以不用管色相行的例外。
    int headH = SettingsRowHeadH(m, row);
    int y = row.top + (headH - m.switchH) / 2;
    DrawTextC(dc, x - (int)(42 * m.dpi), row.top, (int)(34 * m.dpi), headH,
              on ? T(L"开", L"On") : T(L"关", L"Off"), g_sfCtrl, C_WHITE);
    double value = on ? 1.0 : 0.0;
    if (g_switchAnimHit == hit && !g_settingsMoving) {
        double t = (double)(QpcNowMs() - g_switchAnimStart) / 180.0;
        if (t > 1.0) t = 1.0;
        t = t * t * (3.0 - 2.0 * t);
        double from = g_switchAnimFrom ? 1.0 : 0.0;
        double to = g_switchAnimTo ? 1.0 : 0.0;
        value = from + (to - from) * t;
    }
    DWORD track = BlendColor(C_DARK, C_HOT, value);
    DrawRoundRect(dc, x, y, m.switchW, m.switchH, track, C_KEY_BORDER, m.switchH / 2);
    int pad = (int)(3 * m.dpi);                  // 圆钮四周的留白（DIP，跟 dpi 走）
    int knob = m.switchH - pad * 2;
    int travel = m.switchW - knob - pad * 2;
    int kx = x + pad + (int)(travel * value + 0.5);
    DWORD knobColor = BlendColor(C_DIM, C_KEY, value);
    DrawRoundRect(dc, kx, y + pad, knob, knob, knobColor, knobColor, knob / 2);
}

// 预设色相（对齐 panda-core 的 PRESET_HUES），色板按 oklch(0.70 0.14 H) 渲染
static const int g_presetHues[5] = { 250, 210, 180, 300, 30 };

// 色相滑轨：彩虹渐变直接用主色的 oklch 配方铺满，与色板、滑钮颜色完全一致
static void DrawHueSlider(HDC dc, const RECT& r, int hue) {
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    if (w <= 1 || h <= 1) return;

    {
        Gdiplus::Graphics g(dc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);

        Gdiplus::GraphicsPath path;
        Gdiplus::REAL radius = (Gdiplus::REAL)h / 2.0f;
        Gdiplus::REAL diameter = radius * 2.0f;
        Gdiplus::REAL x = (Gdiplus::REAL)r.left + 0.5f;
        Gdiplus::REAL y = (Gdiplus::REAL)r.top + 0.5f;
        Gdiplus::REAL width = (Gdiplus::REAL)w - 1.0f;
        Gdiplus::REAL height = (Gdiplus::REAL)h - 1.0f;
        path.AddArc(x, y, diameter, diameter, 90.0f, 180.0f);
        path.AddArc(x + width - diameter, y, diameter, diameter, 270.0f, 180.0f);
        path.CloseFigure();
        g.SetClip(&path);

        for (int i = 0; i < 6; i++) {
            Gdiplus::REAL left = x + width * (Gdiplus::REAL)i / 6.0f;
            Gdiplus::REAL right = x + width * (Gdiplus::REAL)(i + 1) / 6.0f + 1.0f;
            Gdiplus::LinearGradientBrush brush(
                Gdiplus::PointF(left, y), Gdiplus::PointF(right, y),
                GpColorFromBgr(OklchToBgr(0.70, 0.14, i * 60.0)),
                GpColorFromBgr(OklchToBgr(0.70, 0.14, (i + 1) * 60.0)));
            g.FillRectangle(&brush, left, y, right - left, height);
        }
        g.ResetClip();
        Gdiplus::Pen border(GpColorFromBgr(C_KEY_BORDER), 1.0f);
        g.DrawPath(&border, &path);
    }

    double selected = (double)hue / (double)HKB_HUE_MAX;
    if (selected < 0.0) selected = 0.0; if (selected > 1.0) selected = 1.0;
    int cx = r.left + (int)((w - 1) * selected + 0.5);
    int cy = (r.top + r.bottom) / 2;
    int radius = h / 2 + 3;
    DrawCircleAA(dc, cx, cy, radius, C_WHITE);
    DrawCircleAA(dc, cx, cy, radius - 2, HueAccentBgr(hue));
}

// ===== 十六进制颜色输入辅助（GDI 用 BGR 存储，#RRGGBB 为 RGB） =====
static int HexVal(wchar_t c) {
    if (c >= L'0' && c <= L'9') return c - L'0';
    if (c >= L'a' && c <= L'f') return c - L'a' + 10;
    if (c >= L'A' && c <= L'F') return c - L'A' + 10;
    return -1;
}
static BOOL ParseHexToBgr(const wchar_t* s, DWORD* out) {
    int i = 0;
    if (s[0] == L'#') i = 1;
    int len = 0;
    while (s[i + len] && len < 6) len++;
    if (len != 6) return FALSE;
    int rgb = 0;
    for (int k = 0; k < 6; k++) {
        int v = HexVal(s[i + k]);
        if (v < 0) return FALSE;
        rgb = (rgb << 4) | v;
    }
    *out = (DWORD)(((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF));
    return TRUE;
}
static void HexFromBgr(DWORD bgr, wchar_t* out) {
    int r = bgr & 0xFF, g = (bgr >> 8) & 0xFF, b = (bgr >> 16) & 0xFF;
    swprintf(out, 8, L"#%02X%02X%02X", r, g, b);
}
static int OpacityIndex() {
    for (int i = 0; i < 6; i++) if (g_opacityValues[i] == g_mainOpacity) return i;
    return 0;
}
// 主题色相下拉当前选项：0=自定义色相 1=跟随壁纸
static int HlSel() {
    return g_wallpaperAccent ? 1 : 0;
}
// 关闭按钮操作下拉当前文案
static const wchar_t* CloseActionName() {
    return T(g_closeToTray ? L"隐藏到系统托盘" : L"直接退出程序",
             g_closeToTray ? L"Hide to tray" : L"Exit program");
}
// ===== 关于页几何（与制作工具 PanDa PE 的 about 页同一套语法）=====
// 卡片内边距上下 14 / 左右 20、堆叠间距 6、身份卡「56 DIP 标识网格 + 产品名 + 版本」、
// 行「标题 + 说明 + 右侧 34 DIP 圆钮」、许可行「标签 + MIT + 查看按钮」。
// 下面全部是 DIP，实机再乘 dpi。
//
// ⚠ 上下内边距是 **14 不是 10**：制作工具的许可卡高 78 DIP、许可行 46，反推出 padY = 16；
//   本窗口许可行同样是 46，把 padY 提到 14 后三张卡的留白节奏与它一致。
//   改 10 时身份卡只有 76 高，文字块上下各剩 12.7 DIP，描述贴着卡底，看着像「掉下去」。
static int AboutPadY(const SettingsMetrics& m) { return (int)(14 * m.dpi); }      // .card-pad 上下
static int AboutPadX(const SettingsMetrics& m) { return (int)(20 * m.dpi); }      // .card-pad 左右
static int AboutGap(const SettingsMetrics& m) { return (int)(6 * m.dpi); }        // 卡与卡之间的间距

// 页内一行：上 12 + 内容 36 + 下 12；末行只给下 2（制作工具 form_card 的
// `.field{padding:12px 0}` + `:last-child{padding-bottom:2px}`）。
// 内容 36 = 标题盒 15 + 说明盒顶偏移 23 + 说明盒 13。偏移取 23 而非 20：本窗口行标题
// （g_sfCtrl ≈ 14.7 DIP）墨迹比制作工具的 14px 高，偏移 20 时两行墨迹只隔 4.6 DIP
// （制作工具 8.6），肉眼是「标题和说明糊在一起」。
static int AboutRowH(const SettingsMetrics& m, BOOL last) {
    return m.rowPadY + (int)(36 * m.dpi) + (last ? (int)(2 * m.dpi) : m.rowPadY);
}

static RECT AboutRowRect(const SettingsMetrics& m, const RECT& card, int index) {
    int h0 = AboutRowH(m, FALSE);
    int top = card.top + AboutPadY(m) + (index == 0 ? 0 : h0 + 1);   // 两行之间 1px 分隔线
    int h = (index == 0) ? h0 : AboutRowH(m, TRUE);
    RECT r = {card.left, top, card.right, top + h};
    return r;
}

// 链接卡只有「项目地址」一行，按**非末行**高度算（12 + 36 + 12，上下各 10 DIP）。
// 原来按末行算（底部只给 2），高亮块下边距 0、贴着卡片底边。
static int AboutLinksCardH(const SettingsMetrics& m) {
    return AboutPadY(m) * 2 + AboutRowH(m, FALSE);
}

// 许可行：上 12 + 按钮 32 + 下 2；卡高 = 上下 10 + 行。
static int AboutLicenceRowH(const SettingsMetrics& m) {
    return m.rowPadY + (int)(32 * m.dpi) + (int)(2 * m.dpi);
}
static int AboutLicenceCardH(const SettingsMetrics& m) {
    return AboutPadY(m) * 2 + AboutLicenceRowH(m);
}

static RECT AboutLicenceRowRect(const SettingsMetrics& m, const RECT& card) {
    RECT r = {card.left, card.top + AboutPadY(m), card.right,
              card.top + AboutPadY(m) + AboutLicenceRowH(m)};
    return r;
}

// 「查看」按钮：高 32 / 圆角 8 / 左右内边距各 14（制作工具的 .btn-sm）。
// 宽度按文字**推进宽**算（容纳宽度两侧各含约 0.2em 的绘制余量，当内边距用会凭空宽 10px），
// 绘制与命中读同一份 —— 量宽用 GetDC(0)，与 SegmentedWidth 同法。
static int AboutBtnW(const wchar_t* label, double dpi) {
    HDC dc = GetDC(0);
    int adv = MeasureTextAdvW(dc, label, g_sfCtrl);
    if (adv <= 0) adv = MeasureTextW(dc, label, g_sfCtrl);   // GDI+ 未就绪时的回退
    ReleaseDC(0, dc);
    return adv + (int)(28 * dpi);
}

static RECT AboutLicenceBtnRect(const SettingsMetrics& m, int rowTop) {
    int w = AboutBtnW(T(L"查看", L"View"), m.dpi);
    int h = (int)(32 * m.dpi);
    int x = m.contentX + m.contentW - AboutPadX(m) - w;
    int y = rowTop + ((AboutLicenceRowH(m) - h) / 2);
    RECT r = {x, y, x + w, y + h};
    return r;
}

// 关于 tab 的卡与章节：绘制与命中必须取自同一份几何，避免两边各算一遍
struct AboutLayout {
    RECT card1;   // 身份卡
    RECT card2;   // 链接卡（一行：项目地址）
    RECT card3;   // 许可卡（一行）
    int  mark;    // 身份标识绘制边长（56 DIP 网格）
};

static AboutLayout GetAboutLayout(const SettingsMetrics& m) {
    AboutLayout a = {};
    int padY = AboutPadY(m), gap = AboutGap(m);
    int y;

    // 身份标识按 56 DIP 的绘制网格算：卡高 = 上下 14 + 56 = 84。
    // 「标题 + 版本号」的文字块按卡纵心居中，与标识同轴。
    a.mark = (int)(56 * m.dpi);
    a.card1.left = m.contentX;
    a.card1.top = m.contentY;
    a.card1.right = m.contentX + m.contentW;
    a.card1.bottom = a.card1.top + padY * 2 + a.mark;

    y = a.card1.bottom + gap;
    a.card2.left = m.contentX; a.card2.right = m.contentX + m.contentW;
    a.card2.top = y;           a.card2.bottom = y + AboutLinksCardH(m);

    y = a.card2.bottom + gap;
    a.card3.left = m.contentX; a.card3.right = m.contentX + m.contentW;
    a.card3.top = y;           a.card3.bottom = y + AboutLicenceCardH(m);
    return a;
}

// ===== 窗口高度跟随当前 tab 的内容 =====
// 设置窗宽度固定，但各 tab 的内容高度差很多（常规 7 行 vs 关于两张卡）。
// 高度按「内容底部 + 一个 margin」算：切 tab / 改行数后收一次边，
// 短 tab 底部就不会留一大片空白。
static int SettingsDesiredHeight(HWND hWnd) {
    SettingsMetrics m = GetSettingsMetrics(hWnd);
    int bottom;
    if (g_sTab == 2) {
        AboutLayout al = GetAboutLayout(m);
        bottom = al.card3.bottom + (int)(18 * m.dpi) * 2;   // 版权行（18 间距 + 18 行高）
    } else {
        bottom = SettingsCardRect(m).bottom;
    }
    int want = bottom + m.margin;
    int minH = (int)(300 * m.dpi);
    if (want < minH) want = minH;
    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int maxH = work.bottom - work.top;
    if (maxH > 0 && want > maxH) want = maxH;
    return want;
}

// 收到刚好包住内容（保持左上角不动，避免切 tab 时窗口左右乱跳）
static void SettingsFitHeight(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return;
    int want = SettingsDesiredHeight(hWnd);
    RECT rc;
    if (!GetWindowRect(hWnd, &rc)) return;
    if (rc.bottom - rc.top == want) return;
    SetWindowPos(hWnd, NULL, 0, 0, rc.right - rc.left, want,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

// 链接行：左「标题 + 说明」，右一枚 34 DIP 圆钮，圆钮里的图标 = 去哪里。
//
// ⚠ 高亮**只作用在圆钮上，不铺整行**（实测制作工具：链接卡整卡纯白 (255,255,255)，
//   三行的行背景在 hover 下都不变色，只有圆钮底色从 btn_regular_bg 变到
//   btn_regular_bg_hover）。原来这里调 DrawSettingsRowHover 把整行铺成 C_REGULAR_HOV，
//   实测高亮块 547×60 DIP 横跨整张卡，比圆钮大一个量级；更糟的是圆钮也用同一个
//   C_REGULAR_HOV —— 两者同色，圆钮在 hover 态直接「消失」在行底里。
//
//   命中区仍是整行（触摸场景下按钮不能只有 34×34 DIP）。
static void DrawAboutLinkRow(HDC dc, const SettingsMetrics& m, const RECT& row,
                             int iconId, const wchar_t* title, const wchar_t* desc, BOOL hover) {
    int btn = (int)(34 * m.dpi);
    int bx = row.right - AboutPadX(m) - btn;
    int by = row.top + ((row.bottom - row.top) - btn) / 2;   // 圆钮在行内居中
    DrawRoundRect(dc, bx, by, btn, btn, hover ? C_REGULAR_HOV : C_REGULAR,
                  hover ? C_REGULAR_HOV : C_REGULAR, btn / 2);
    int isz = (int)(17 * m.dpi);
    DrawHkIcon(dc, (float)(bx + (btn - isz) / 2), (float)(by + (btn - isz) / 2), (float)isz,
               HkIcon(iconId), C_BTN_CONTENT, C_BTN_CONTENT);

    // 文字块（标题盒 15 + 说明盒 13，顶偏移 23 → 块高 36）与圆钮中心对中心。
    // 标题用 g_sfCtrl（11pt ≈ 14.7 DIP）而不是 g_sfRow：制作工具的行标题是 14px，
    // 而章节标题是 18 —— 层级比 1.29。本窗口 g_sfRow 实际是 16.7 DIP，与章节的 18.7
    // 只差 1.12 倍，行标题会顶到章节标题上，读起来像同级。
    int tx = row.left + AboutPadX(m);
    int tw = bx - (int)(12 * m.dpi) - tx;
    int blockH = (int)(36 * m.dpi);
    int tyText = row.top + ((row.bottom - row.top) - blockH) / 2;
    DrawTextL(dc, tx, tyText, tw, (int)(15 * m.dpi), title, g_sfCtrl, C_WHITE);
    if (desc && desc[0])
        DrawTextL(dc, tx, tyText + (int)(23 * m.dpi), tw, (int)(13 * m.dpi), desc, g_sfBase, C_DIM);
}

// 许可行：「许可协议」标签列（110 DIP，与制作工具 widgets::field 的标签列同宽）+ 主色值
// + 右侧 32 DIP「查看」按钮。标签与值分两列而不是连成一句：MIT 是**值**，颜色交回主色，
// 扫一行就知道许可是什么；按钮才是动作。
static void DrawAboutLicenceRow(HDC dc, const SettingsMetrics& m, const RECT& row, BOOL hover) {
    int labelW = (int)(110 * m.dpi);
    int lx = row.left + AboutPadX(m);
    RECT btn = AboutLicenceBtnRect(m, row.top);
    int bw = btn.right - btn.left, bh = btn.bottom - btn.top;

    DrawRoundRect(dc, btn.left, btn.top, bw, bh, hover ? C_REGULAR_HOV : C_REGULAR,
                  hover ? C_REGULAR_HOV : C_REGULAR, (int)(8 * m.dpi));
    DrawTextC(dc, btn.left, btn.top, bw, bh, T(L"查看", L"View"), g_sfCtrl, C_BTN_CONTENT);

    DrawTextL(dc, lx, row.top, labelW, row.bottom - row.top,
              T(L"开源许可", L"Licence"), g_sfCtrl, C_WHITE);
    int vx = lx + labelW + (int)(18 * m.dpi);
    DrawTextL(dc, vx, row.top, btn.left - (int)(12 * m.dpi) - vx, row.bottom - row.top,
              L"MIT", g_sfCtrl, C_HOT);
}

static void SettingsDraw(HDC dc, HWND hWnd) {
    SettingsMetrics m = GetSettingsMetrics(hWnd);

    // 页面头：38×38 图标 tile + 26px 标题。
    // 关于是一块独立页面，标题与图标都跟着改成「关于 / Info」；其余三个 tab 同属「设置」。
    BOOL aboutTab = (g_sTab == 2);
    DrawIconTile(dc, m.margin, m.titleY, m.headIcon, (int)(20 * m.dpi),
                 aboutTab ? HKICON_INFO : HKICON_GEAR, NULL);
    int titleX = m.margin + m.headIcon + (int)(16 * m.dpi);
    // 标题盒与 38 DIP 的 tile 中心对齐：原来两者共用同一个 top，tile 中心比标题墨迹中心
    // 低 3.7 DIP（实机量过），大字号的偏差最扎眼。titleH(34) 比 headIcon(38) 矮 4，居中即对称。
    DrawTextL(dc, titleX, m.titleY + (m.headIcon - m.titleH) / 2, m.closeX - (int)(12 * m.dpi) - titleX, m.titleH,
              aboutTab ? T(L"关于", L"About") : T(L"设置", L"Settings"), g_sfBig, C_WHITE);
    if (g_sHov == S_HIT_CLOSE) {
        DrawRoundRect(dc, m.closeX, m.closeY, m.closeW, m.closeH,
                      C_REGULAR_HOV, C_REGULAR_HOV, (int)(6 * m.dpi));
    }
    {
        int sz = (int)(18 * m.dpi);
        DrawHkIcon(dc, (float)(m.closeX + (m.closeW - sz) / 2),
                   (float)(m.closeY + (m.closeH - sz) / 2), (float)sz,
                   HkIcon(HKICON_CLOSE), C_DIM, C_DIM);
    }

    DrawTabStrip(dc, m);

    // 一个 tab 就是一张卡；关于 tab 自己画两张卡，不进这条路径
    if (g_sTab != 2) DrawSettingsCard(dc, m);

    if (g_sTab == 0) {
        // 行序（自动收起行仅在自动呼出开启时存在，隐藏时后续行上移一行）：
        //   0=自动呼出 1=自动收起 2/3=关闭按钮/记住选择 4=功能键行 5=Shift符号 6=界面语言
        int closeRow = g_af ? 2 : 1;

        RECT r0 = SettingsRowRect(m, 0);
        DrawSettingRowContent(dc, m, r0, HKICON_CARET, NULL,
                              T(L"自动呼出", L"Auto Pop-up"),
                              T(L"点击输入框时自动弹出键盘", L"Show the keyboard when an input gets focus"),
                              g_sHov == S_HIT_AUTO, SettingsSwitchTextRight(m, r0));
        DrawSettingSwitch(dc, m, r0, g_af, S_HIT_AUTO);

        if (g_af) {
            RECT ra = SettingsRowRect(m, 1);
            // 子项样式：不画图标 tile，标题与「自动呼出」的标题左对齐
            DrawSettingRowContent(dc, m, ra, -1, NULL,
                                  T(L"自动隐藏", L"Auto Hide"),
                                  T(L"点击输入框以外时自动隐藏键盘", L"Hide the keyboard when clicking outside the input"),
                                  g_sHov == S_HIT_AUTOHIDE, SettingsSwitchTextRight(m, ra),
                                  FALSE, TRUE);
            DrawSettingSwitch(dc, m, ra, g_afAutoHide, S_HIT_AUTOHIDE);
        }

        RECT r1 = SettingsRowRect(m, closeRow);
        const wchar_t* cseg[2];
        int csegn = CloseSegItems(cseg);
        RECT csegR = RowSegRect(m, r1, cseg, csegn);
        DrawSettingRowContent(dc, m, r1, HKICON_CLOSE, NULL,
                             T(L"选择关闭方式", L"Close Action"),
                              T(L"选择关闭窗口时执行的操作", L"Choose what happens when the window is closed"),
                              g_sHov == S_HIT_CLOSE_DROP, csegR.left);
        DrawSegmented(dc, m, csegR, cseg, csegn, g_closeToTray ? 1 : 0);

        RECT r2 = SettingsRowRect(m, closeRow + 1);
        DrawSettingRowContent(dc, m, r2, HKICON_CHECK, NULL,
                             T(L"记住我的选择", L"Remember My Choice"),
                             T(L"记住选择关闭方式，下次直接执行", L"Remember the action and skip asking next time"),
                             g_sHov == S_HIT_REMEMBER, SettingsSwitchTextRight(m, r2));
        DrawSettingSwitch(dc, m, r2, g_rememberClose, S_HIT_REMEMBER);

        RECT r;
        // 功能键行 / Shift 符号：全尺寸与小键盘布局下不存在（见 SettingsRowHidden）
        if (!SettingsRowHidden(0, closeRow + 2)) {
        r = SettingsRowRect(m, closeRow + 2);
        DrawSettingRowContent(dc, m, r, -1, L"F",   // 功能键行保留手绘 F
                              T(L"功能键行", L"Function Key Row"),
                              T(L"在键盘顶部显示 F1~F12 和 Del", L"Show F1~F12 and Del above the keyboard"),
                              g_sHov == S_HIT_FKEYS, SettingsSwitchTextRight(m, r));
        DrawSettingSwitch(dc, m, r, g_showFKeys, S_HIT_FKEYS);

        r = SettingsRowRect(m, closeRow + 3);
        DrawSettingRowContent(dc, m, r, HKICON_SHIFT, NULL,
                              T(L"Shift 符号", L"Shift Symbols"),
                              T(L"按下 Shift 后数字键仅显示特殊符号", L"Show only symbols while Shift is held"),
                              g_sHov == S_HIT_SHIFTSYM, SettingsSwitchTextRight(m, r));
        DrawSettingSwitch(dc, m, r, g_shiftSymbols, S_HIT_SHIFTSYM);
        }

        r = SettingsRowRect(m, closeRow + 4);
        const wchar_t* gseg[2];
        int gsegn = LangSegItems(gseg);
        RECT gsegR = RowSegRect(m, r, gseg, gsegn);
        DrawSettingRowContent(dc, m, r, HKICON_GLOBE, NULL,
                              T(L"界面语言", L"Language"),
                              T(L"切换设置与键盘的显示语言", L"Language of settings and keyboard"),
                              g_sHov == S_HIT_LANG_DROP, gsegR.left);
        DrawSegmented(dc, m, gsegR, gseg, gsegn, g_lang);
    } else if (g_sTab == 3) {
        // 布局 Tab：0=键盘布局 1=Fn 网页布局 2=小键盘按钮（「按键图标样式」行已删）
        // （原第 5 行「Fn + Tab 切换小键盘」已下线：全尺寸下显隐数字区只留标题栏那一个入口）
        RECT r = SettingsRowRect(m, 0);
        const wchar_t* lseg[3];
        int lsegn = LayoutSegItems(lseg);
        RECT lsegR = RowSegRect(m, r, lseg, lsegn);
        DrawSettingRowContent(dc, m, r, HKICON_KEYBOARD, NULL,
                              T(L"键盘布局", L"Keyboard Layout"),
                              T(L"选择主键盘的按键排列", L"Choose the main keyboard arrangement"),
                              g_sHov == S_HIT_LAYOUT_DROP, lsegR.left);
        DrawSegmented(dc, m, lsegR, lseg, lsegn, g_layoutMode);

        // Fn 网页布局：全尺寸与小键盘布局下不存在（见 SettingsRowHidden）
        if (!SettingsRowHidden(3, 1)) {
        r = SettingsRowRect(m, 1);
        DrawSettingRowContent(dc, m, r, HKICON_GLOBE, NULL,
                              T(L"Fn 网页布局", L"Fn Web Layout"),
                              SettingsRowDescText(3, 1),
                              g_sHov == S_HIT_FNWEB, SettingsSwitchTextRight(m, r),
                              SettingsRowDescTwoLines(m, 3, 1));
        DrawSettingSwitch(dc, m, r, g_fnWebLayout, S_HIT_FNWEB);
        }

        r = SettingsRowRect(m, 2);
        DrawSettingRowContent(dc, m, r, HKICON_NUMPAD, NULL,
                              T(L"小键盘按钮", L"Numpad Button"),
                              SettingsRowDescText(3, 2),
                              g_sHov == S_HIT_NPBTN, SettingsSwitchTextRight(m, r),
                              SettingsRowDescTwoLines(m, 3, 2));
        DrawSettingSwitch(dc, m, r, g_showNumBtn, S_HIT_NPBTN);
    } else if (g_sTab == 1) {
        RECT r = SettingsRowRect(m, 0);
        const wchar_t* tseg[3];
        int tsegn = ThemeSegItems(tseg);
        RECT tsegR = RowSegRect(m, r, tseg, tsegn);
        DrawSettingRowContent(dc, m, r, HKICON_CONTRAST, NULL,
                              T(L"主题模式", L"Theme Mode"),
                              T(L"跟随系统，或固定使用深色、浅色主题", L"Follow Windows or use a fixed dark or light theme"),
                              FALSE, tsegR.left);
        DrawSegmented(dc, m, tsegR, tseg, tsegn, g_themeMode);

        r = SettingsRowRect(m, 1);
        const wchar_t* oseg[6];
        int osegn = OpacitySegItems(oseg);
        RECT osegR = RowSegRect(m, r, oseg, osegn);
        DrawSettingRowContent(dc, m, r, HKICON_PANEL, NULL,
                              T(L"主界面透明度", L"Keyboard Opacity"),
                              T(L"调整不透明度", L"Adjust the window opacity"),
                              FALSE, osegR.left);
        DrawSegmented(dc, m, osegR, oseg, osegn, OpacityIndex());

        // 主题色相：可展开行（展开时行高 210，行内是分隔线 + 色板 + 滑轨 + HEX）
        BOOL customHue = !g_wallpaperAccent;
        r = SettingsRowRect(m, 2);
        const wchar_t* hseg[2];
        int hsegn = HueModeSegItems(hseg);
        RECT hsegR = RowSegRectTop(m, r, hseg, hsegn);
        DrawSettingRowContent(dc, m, r, HKICON_PALETTE, NULL,
                              T(L"主题色相", L"Theme Hue"),
                              T(L"一个色相统一调整面板、按键与强调色", L"One hue recolors the panel, keys and accent"),
                              FALSE, hsegR.left);
        DrawSegmented(dc, m, hsegR, hseg, hsegn, HlSel());

        if (customHue) {
            // 分隔线是「标题区 / 展开区」的分界，要落在两者正中间：上方到描述盒底
            // （块顶+52）、下方到色板顶（76），两边都留 12 DIP。原来画在 56，
            // 离描述只有 4 DIP —— 实机上看着像给描述加的下划线。
            Fill(dc, r.left + (int)(20 * m.dpi), r.top + (int)(64 * m.dpi),
                 r.right - r.left - (int)(40 * m.dpi), 1, C_META);

            int palX, palY, palS, palGap;
            SettingsPaletteMetrics(m, r, &palX, &palY, &palS, &palGap);
            for (int i = 0; i < 5; i++) {
                DWORD color = HueAccentBgr(g_presetHues[i]);
                int px = palX + i * (palS + palGap);
                BOOL selected = abs(g_hue - g_presetHues[i]) <= 6;   // 老配置的色相未必正好落在预设值上
                // 选中 = 外层主色环 + 2px 卡色间隔环（色板是圆角方块，不再是圆）
                if (selected || g_sHov == S_HIT_HL_PAL0 + i) {
                    int ring = selected ? (int)(4 * m.dpi) : (int)(3 * m.dpi);
                    DWORD ringC = selected ? C_HOT : C_REGULAR_HOV;
                    DrawRoundRect(dc, px - ring, palY - ring, palS + ring * 2, palS + ring * 2,
                                  ringC, ringC, (int)(12 * m.dpi));
                    DrawRoundRect(dc, px - ring + (int)(2 * m.dpi), palY - ring + (int)(2 * m.dpi),
                                  palS + ring * 2 - (int)(4 * m.dpi), palS + ring * 2 - (int)(4 * m.dpi),
                                  C_KEY, C_KEY, (int)(10 * m.dpi));
                }
                DrawRoundRect(dc, px, palY, palS, palS, color, color, (int)(8 * m.dpi));
                if (selected) {
                    HPEN pen = CreatePen(PS_SOLID, 2, C_ON_PRIMARY);
                    HPEN old = (HPEN)SelectObject(dc, pen);
                    int cx = px + palS / 2, cy = palY + palS / 2;
                    MoveToEx(dc, cx - (int)(5 * m.dpi), cy, NULL);
                    LineTo(dc, cx - (int)(1 * m.dpi), cy + (int)(4 * m.dpi));
                    LineTo(dc, cx + (int)(6 * m.dpi), cy - (int)(5 * m.dpi));
                    SelectObject(dc, old); DeleteObject(pen);
                }
            }

            RECT slider = SettingsColorSliderRect(m, r);
            DrawHueSlider(dc, slider, g_hue);

            RECT input = SettingsHexRect(m, r);
            int inputW = input.right - input.left, inputH = input.bottom - input.top;
            DWORD preview = HueAccentBgr(g_hue);
            int colorCx = r.left + (int)(42 * m.dpi);
            int colorCy = input.top + inputH / 2;
            DrawCircleAA(dc, colorCx, colorCy, (int)(11 * m.dpi), C_KEY_BORDER);
            DrawCircleAA(dc, colorCx, colorCy, (int)(9 * m.dpi), preview);
            DrawTextC(dc, r.left + (int)(55 * m.dpi), input.top, (int)(18 * m.dpi), inputH,
                      L"#", g_sfBase, C_DIM);
            DrawRoundRect(dc, input.left, input.top, inputW, inputH,
                          g_hlEditFocus ? C_HOVER : C_DARK,
                          g_hlEditFocus ? C_HOT : C_BORDER_HOVER, (int)(12 * m.dpi));
            wchar_t hexbuf[8];
            if (g_hlEditFocus) wcscpy(hexbuf, g_hlEditBuf); else HexFromBgr(preview, hexbuf);
            const wchar_t* shown = hexbuf[0] == L'#' ? hexbuf + 1 : hexbuf;
            BOOL showHint = g_hlEditFocus && shown[0] == 0;
            DrawTextL(dc, input.left + (int)(12 * m.dpi), input.top, inputW - (int)(20 * m.dpi), inputH,
                      showHint ? L"RRGGBB" : shown, g_sfBase, showHint ? C_DIM : C_WHITE);
        }
    } else {
        // 关于 tab：身份卡 → 链接卡（项目地址）→ 「开源许可」→ 许可卡，版权行在最下方居中。
        // 语法整体照制作工具（PanDa PE）的关于页：身份卡是「方块 + 产品名 + 版本」，
        // 链接行是「标题 + 说明 + 目的地图标圆钮」，许可是独立一行。见 GetAboutLayout。
        AboutLayout al = GetAboutLayout(m);
        DrawRoundRect(dc, al.card1.left, al.card1.top,
                      al.card1.right - al.card1.left, al.card1.bottom - al.card1.top,
                      C_KEY, C_KEY, (int)(16 * m.dpi));

        // 身份标识：矢量 KeyboardMark（主色键盘体 + 挖空键块），不用位图。
        // 纵向**居中于卡片本身**，而不是居中于「标题 + 描述」的行盒：
        //   卡高 76 = 上下内边距 10 + 标识 56，标识盒正好落在内边距上；
        //   文字块（标题盒 32 + 间隙 6 + 描述盒 18 = 56）也按卡纵心居中，两者同轴。
        //   直接由卡高算，改字号或改卡高都自洽。
        //
        // ⚠ hole 色必须与 fg 不同（这里传 C_ON_PRIMARY = primaryFg 浅色下的深色正文色）：
        //   键块是**独立子路径**，靠 hole 色「挖」出来才看得见键盘。同色 ⇒ 挖空失效，
        //   56×56 的位置里只剩一个实心方块，键块全部消失（实机踩过）。
        // ⚠ 也**别再往外套一层圆角方块**。制作工具的 brand_tile 是「色块 + 另一枚图形」，
        //   它那枚是熊猫头，与色块同色系但形状完全不同；KeyboardMark 本身就是完整剪影，
        //   套色块后只会在 56 DIP 的位置里塞进一条 49×34 的扁带，中间还多一圈冗余底色。
        int my = al.card1.top + (al.card1.bottom - al.card1.top - al.mark) / 2;
        // ⚠ 文字起点用 **13.3 DIP**（原 18）：al.mark 是 56 DIP 的**绘制网格**，而
        //   KeyboardMark 的着色只占网格的 88%（实测墨迹 49.52 DIP，网格内左留白 2.48）。
        //   按网格量间距会多算 6.5 DIP，这是「图标贴着文字」的成因。
        //   制作工具那边实测视觉间距 18.98（tile 满格，网格=墨迹，不存在这个偏差）；
        //   本窗口同样要 18.98，就得从 18 往下减：18 − 6.5(网格/墨迹差) + 1.8(GDI+ 与
        //   字形左边距的差，实测反解) ≈ 13.3。改完实测 18.98±0.3。
        //   ⚠ 别再套一层圆角方块去「填满」网格 —— 那会让 49×34 的扁带缩在色块里。
        DrawHkIcon(dc, (float)(al.card1.left + AboutPadX(m)), (float)my, (float)al.mark,
                   HkIcon(HKICON_KEYBOARDMARK), C_HOT, C_ON_PRIMARY);

        // 产品名用 g_sfSec（17pt ≈ 22.7 DIP）而不是页面标题那一档：制作工具的名称 20 /
        // 页标题 26（0.77），本窗口页标题是 18pt，若卡里的名字也用 18pt 就和页面大标题
        // 同号，两行字重一样、互相打架。17pt 保住层级差（0.94），又比 20pt 少占 5 DIP
        // 高度 —— 这一项是「描述别掉下去」的关键，见下。
        //
        // ⚠ 文字块高度是 **49 DIP**（标题盒 28 + 间隙 3 + 描述盒 18），不是 53。
        //   「标题↔描述中心距」= 墨迹间隙 + 两个墨迹高的一半，而汉字墨迹比拉丁高得多：
        //   20pt 下「HKeyboard 轻键」墨迹 29.36 DIP，制作工具的「PanDa PE」只有 15.43
        //   （纯拉丁 cap height）。中心距因此被撑到 29.36 对 22.57，描述被推下去。
        //   标题压到 17pt 后墨迹降到 24.18，中心距收到 27.05，块才排得进 84 高的卡。
        //   ⚠ 别想着「把盒顶间隙调成 0 补回来」：汉字墨迹几乎撑满标题盒，间隙归零后
        //   标题与描述的字面会贴到一起。间隙 3 是下限。
        //
        // ⚠ 文字块整体**下移 1.7 DIP**。盒高 49 按卡纵心居中只是「盒子居中」，
        //   而 17pt 标题的墨迹在 28 DIP 的盒里只留 0.1 DIP 上留白（行盒 1.326em = 29.17
        //   比盒高还大 1.17，GDI+ 居中后墨迹几乎贴着盒顶），只居中盒子会让整块偏上。
        //   下移 1.7 后实测顶 19.58 / 底 17.85，墨迹块心落在卡心 +0.87 ——
        //   制作工具那边是 +1.72（同为「块心略低于卡心」），所以纵向不必再补。
        int tx = al.card1.left + AboutPadX(m) + al.mark + (int)(13.3 * m.dpi);
        int ty = al.card1.top + ((al.card1.bottom - al.card1.top) - (int)(49 * m.dpi)) / 2
                 + (int)(1.7 * m.dpi + 0.5);
        int tw = al.card1.right - AboutPadX(m) - tx;
        DrawTextL(dc, tx, ty, tw, (int)(28 * m.dpi),
                  T(L"HKeyboard 轻键", L"HKeyboard"), g_sfSec, C_WHITE);
        wchar_t meta[96];
        // 版本串 = 内部版本号 + 构建日期（北京时间），例如 "v2.0_20261002"
        swprintf(meta, 96, T(L"轻量屏幕键盘 · v%hs_%ls (%ls)", L"Lightweight screen keyboard · v%hs_%ls (%ls)"),
                 VER_FILEVERSION_STR, HK_BUILD_DATE, ArchName());
        DrawTextL(dc, tx, ty + (int)(31 * m.dpi), tw, (int)(18 * m.dpi), meta, g_sfMeta, C_DIM);

        DrawRoundRect(dc, al.card2.left, al.card2.top,
                      al.card2.right - al.card2.left, al.card2.bottom - al.card2.top,
                      C_KEY, C_KEY, (int)(16 * m.dpi));
        // 说明句代替网址：图标已经说明「去哪儿」，说明句用来讲清「去那儿干什么」。
        DrawAboutLinkRow(dc, m, AboutRowRect(m, al.card2, 0), HKICON_GITHUB,
                         T(L"项目地址", L"Project URL"),
                         T(L"源码、版本发布与使用说明", L"Source, releases and documentation"),
                         g_sHov == S_HIT_URL);

        DrawRoundRect(dc, al.card3.left, al.card3.top,
                      al.card3.right - al.card3.left, al.card3.bottom - al.card3.top,
                      C_KEY, C_KEY, (int)(16 * m.dpi));
        DrawAboutLicenceRow(dc, m, AboutLicenceRowRect(m, al.card3), g_sHov == S_HIT_LICENSE);

        // 版权行：文字逐字保留（含 2026 与结尾句点），9px C_DIM 居中，放在最后一张卡下方
        DrawTextC(dc, m.contentX, al.card3.bottom + (int)(18 * m.dpi), m.contentW, (int)(18 * m.dpi),
                  L"Copyright 2019-2026 PanDaTech. All Rights Reserved.", g_sfMeta, C_DIM);
    }
}

static int SettingsHitTest(HWND hWnd, int x, int y) {
    SettingsMetrics m = GetSettingsMetrics(hWnd);
    if (x >= m.closeX && x < m.closeX + m.closeW && y >= m.closeY && y < m.closeY + m.closeH) return S_HIT_CLOSE;

    // tab 条：与绘制共用同一套矩形（文字宽 + 固定间隙）
    {
        const wchar_t* labels[4];
        SettingsTabLabels(labels);
        RECT tr[4];
        SettingsTabRects(m, labels, tr);
        for (int i = 0; i < 4; i++) {
            if (x >= tr[i].left && x < tr[i].right && y >= tr[i].top && y < tr[i].bottom)
                return k_settingsTabHits[i];
        }
    }

    if (g_sTab == 0) {
        RECT r;
        int closeRow = g_af ? 2 : 1;   // 自动收起行仅在自动呼出开启时存在

        r = SettingsRowRect(m, 0);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_AUTO;
        if (g_af) {
            r = SettingsRowRect(m, 1);
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_AUTOHIDE;
        }

        // 画框选择：只有控件本身是热区（行内其余位置不再触发切换）
        r = SettingsRowRect(m, closeRow);
        { const wchar_t* it[2]; int n = CloseSegItems(it);
          RECT sr = RowSegRect(m, r, it, n);
          if (x >= sr.left && x < sr.right && y >= sr.top && y < sr.bottom) return S_HIT_CLOSE_DROP; }

        r = SettingsRowRect(m, closeRow + 1);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_REMEMBER;

        if (!SettingsRowHidden(0, closeRow + 2)) {       // 全尺寸 / 小键盘下这两行不存在
            r = SettingsRowRect(m, closeRow + 2);
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_FKEYS;
            r = SettingsRowRect(m, closeRow + 3);
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_SHIFTSYM;
        }

        r = SettingsRowRect(m, closeRow + 4);
        { const wchar_t* it[2]; int n = LangSegItems(it);
          RECT sr = RowSegRect(m, r, it, n);
          if (x >= sr.left && x < sr.right && y >= sr.top && y < sr.bottom) return S_HIT_LANG_DROP; }
    } else if (g_sTab == 3) {
        // 布局 Tab：0=键盘布局 1=Fn 网页布局 2=小键盘按钮（「按键图标样式」行已删）
        // （原第 5 行「Fn + Tab 切换小键盘」已下线：全尺寸下显隐数字区只留标题栏那一个入口）
        RECT r;
        // 下拉列表优先命中
        r = SettingsRowRect(m, 0);
        { const wchar_t* it[3]; int n = LayoutSegItems(it);
          RECT sr = RowSegRect(m, r, it, n);
          if (x >= sr.left && x < sr.right && y >= sr.top && y < sr.bottom) return S_HIT_LAYOUT_DROP; }

        if (!SettingsRowHidden(3, 1)) {                  // 全尺寸 / 小键盘下不存在
            r = SettingsRowRect(m, 1);
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_FNWEB;
        }

        r = SettingsRowRect(m, 2);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_NPBTN;
    } else if (g_sTab == 1) {
        RECT r;
        r = SettingsRowRect(m, 0);
        { const wchar_t* it[3]; int n = ThemeSegItems(it);
          RECT sr = RowSegRect(m, r, it, n);
          if (x >= sr.left && x < sr.right && y >= sr.top && y < sr.bottom) return S_HIT_THEME_DROP; }

        r = SettingsRowRect(m, 1);
        { const wchar_t* it[6]; int n = OpacitySegItems(it);
          RECT sr = RowSegRect(m, r, it, n);
          if (x >= sr.left && x < sr.right && y >= sr.top && y < sr.bottom) return S_HIT_OPACITY_DROP; }

        r = SettingsRowRect(m, 2);
        { const wchar_t* it[2]; int n = HueModeSegItems(it);
          RECT sr = RowSegRectTop(m, r, it, n);
          if (x >= sr.left && x < sr.right && y >= sr.top && y < sr.bottom) return S_HIT_HL_DROP; }

        if (!g_wallpaperAccent) {
            RECT input = SettingsHexRect(m, r);
            if (x >= input.left && x < input.right && y >= input.top && y < input.bottom) return S_HIT_HL_BOX;
            int palX, palY, palS, palGap;
            SettingsPaletteMetrics(m, r, &palX, &palY, &palS, &palGap);
            for (int i = 0; i < 5; i++) {
                int px = palX + i * (palS + palGap);
                if (x >= px && x < px + palS && y >= palY && y < palY + palS) return S_HIT_HL_PAL0 + i;
            }
            RECT slider = SettingsColorSliderRect(m, r);
            int pad = (int)(8 * m.dpi);
            if (x >= slider.left && x < slider.right && y >= slider.top - pad && y < slider.bottom + pad)
                return S_HIT_HL_HUE;
        }
    } else {
        // 关于 tab：两个链接行整行可点（卡片位置与绘制同源）；
        // 许可行的热区只有「查看」按钮本身 —— 行内的「许可协议 / MIT」是读数，不是动作
        AboutLayout al = GetAboutLayout(m);
        RECT r0 = AboutRowRect(m, al.card2, 0);
        if (x >= r0.left && x < r0.right && y >= r0.top && y < r0.bottom) return S_HIT_URL;
        RECT rb = AboutLicenceBtnRect(m, AboutLicenceRowRect(m, al.card3).top);
        if (x >= rb.left && x < rb.right && y >= rb.top && y < rb.bottom) return S_HIT_LICENSE;
    }
    return S_HIT_NONE;
}

// ========== 配置文件（exe 同目录 HKeyboard.ini，便携式） ==========
static void GetConfigPath(wchar_t* buf, int cch) {
    GetModuleFileNameW(NULL, buf, cch);
    wchar_t* slash = wcsrchr(buf, L'\\');
    if (slash) wcscpy(slash + 1, L"HKeyboard.ini");
}

static void IniSetInt(const wchar_t* section, const wchar_t* key, int val) {
    wchar_t path[MAX_PATH];
    GetConfigPath(path, MAX_PATH);
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", val);
    WritePrivateProfileStringW(section, key, buf, path);
}

static int IniGetInt(const wchar_t* section, const wchar_t* key, int def) {
    wchar_t path[MAX_PATH];
    GetConfigPath(path, MAX_PATH);
    wchar_t buf[16];
    swprintf(buf, 16, L"%d", def);
    GetPrivateProfileStringW(section, key, buf, buf, 16, path);
    return _wtoi(buf);
}

// ===== 各布局独立记忆窗口大小与位置（避免切换布局后界面错乱） =====
// [Window] Layout{n}X / Layout{n}Y / Layout{n}Width / Layout{n}Height，n=布局序号
static BOOL LoadLayoutWindowRect(RECT* out) {
    wchar_t key[40];
    swprintf(key, 40, L"Layout%dWidth", g_layoutMode);
    int w = IniGetInt(L"Window", key, 0);
    swprintf(key, 40, L"Layout%dHeight", g_layoutMode);
    int h = IniGetInt(L"Window", key, 0);
    if (w < 300 || h < 150) return FALSE;
    swprintf(key, 40, L"Layout%dX", g_layoutMode);
    int x = IniGetInt(L"Window", key, -32000);
    swprintf(key, 40, L"Layout%dY", g_layoutMode);
    int y = IniGetInt(L"Window", key, -32000);
    if (x <= -32000 || y <= -32000) return FALSE;
    out->left = x; out->top = y; out->right = x + w; out->bottom = y + h;
    return TRUE;
}

static BOOL LayoutRectOnScreen(const RECT& rc) {
    int vx = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int vr = vx + GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int vb = vy + GetSystemMetrics(SM_CYVIRTUALSCREEN);
    // 至少大部分区域在虚拟屏幕内，防止记忆了失效坐标
    return rc.right > vx + 60 && rc.left < vr - 60 &&
           rc.bottom > vy + 20 && rc.top < vb - 20;
}

static void SaveWindowState() {
    if (!g_hWnd || !IsWindow(g_hWnd)) return;
    RECT rc;
    if (!GetWindowRect(g_hWnd, &rc)) return;
    wchar_t key[40];
    swprintf(key, 40, L"Layout%dX", g_layoutMode);        IniSetInt(L"Window", key, rc.left);
    swprintf(key, 40, L"Layout%dY", g_layoutMode);        IniSetInt(L"Window", key, rc.top);
    swprintf(key, 40, L"Layout%dWidth", g_layoutMode);    IniSetInt(L"Window", key, rc.right - rc.left);
    swprintf(key, 40, L"Layout%dHeight", g_layoutMode);   IniSetInt(L"Window", key, rc.bottom - rc.top);
}

// 首次启动自动生成 HKeyboard.ini（含默认值），之后按需写入。
// 已存在的旧配置执行一次性升级：清除早期版本写入的未缩放默认窗口尺寸，
// 交给 InitWindowSizeForDpi 按 DPI 重算（避免高 DPI 首启窗口过小）。
static void EnsureConfigFile() {
    wchar_t path[MAX_PATH];
    GetConfigPath(path, MAX_PATH);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) {   // 已存在
        int ver = IniGetInt(L"General", L"ConfigVersion", 0);
        if (ver < 2) {
            IniSetInt(L"Window", L"Width", 0);
            IniSetInt(L"Window", L"Height", 0);
        }
        if (ver < 3) {
            WritePrivateProfileStringW(L"General", L"HideDelay", NULL, path);
            IniSetInt(L"General", L"ConfigVersion", 3);
        }
        if (ver < 4) {
            // “自动收起”键名调整：AutoHideOnBlur -> AutoHide（语义：收起后同输入框内不回弹）
            WritePrivateProfileStringW(L"General", L"AutoHideOnBlur", NULL, path);
            IniSetInt(L"General", L"AutoHide", g_afAutoHide ? 1 : 0);
            IniSetInt(L"General", L"ConfigVersion", 4);
        }
        if (ver < 5) {
            // 材质子系统已移除：清掉废弃键，避免“设置页没有该项却仍在套材质”的幽灵状态。
            // 旧 General/HighlightColor 保留不删——它只作为 Theme/Hue 缺失时的色相迁移来源。
            WritePrivateProfileStringW(L"Theme", L"Material", NULL, path);
            IniSetInt(L"General", L"ConfigVersion", 5);
        }
        if (ver < 6) {
            // 默认「按键图标样式」由「文字」改为「图标+文字」（参考图里 Tab / Caps /
            // Shift / 退格 / 回车这些宽键都是「图形 + 名字」）。
            // 旧配置里几乎都写着 0 —— 那是旧默认值灌进去的，不代表用户的选择，
            // 所以这里一并改成 2；不喜欢可在设置页一键切回「文字」。
            IniSetInt(L"Keyboard", L"KeyIconStyle", 2);
            IniSetInt(L"General", L"ConfigVersion", 6);
        }
        return;
    }
    IniSetInt(L"General", L"RememberClose", 0);
    IniSetInt(L"General", L"CloseToTray", 0);
    IniSetInt(L"Theme", L"Mode", 0);
    IniSetInt(L"Theme", L"Wallpaper", 0);
    IniSetInt(L"Theme", L"Hue", HKB_DEFAULT_HUE);
    IniSetInt(L"Theme", L"Opacity", 100);
    IniSetInt(L"Keyboard", L"Layout", 0);
    IniSetInt(L"Keyboard", L"FKeys", 0);
    IniSetInt(L"Keyboard", L"FnWebLayout", 0);
    IniSetInt(L"Keyboard", L"KeyIconStyle", 2);
    IniSetInt(L"General", L"ShiftSymbols", 1);
    IniSetInt(L"General", L"Language", 0);
    IniSetInt(L"General", L"AutoPopup", 1);
    IniSetInt(L"General", L"AutoHide", 1);
    IniSetInt(L"General", L"ConfigVersion", 6);
}

// 读取上次的窗口大小 / 主题 / 关闭行为
static void LoadConfig() {
    g_rememberClose = (IniGetInt(L"General", L"RememberClose", 0) != 0);
    if (g_rememberClose)
        g_closeToTray = (IniGetInt(L"General", L"CloseToTray", 0) != 0);
    // 窗口大小与位置按布局记忆恢复（见 WinMain / ApplyKeyboardLayout）
    int tm = IniGetInt(L"Theme", L"Mode", -1);
    if (tm >= 0 && tm <= 2) g_themeMode = tm;
    g_mainOpacity = IniGetInt(L"Theme", L"Opacity", 100);
    if (g_mainOpacity < 50 || g_mainOpacity > 100) g_mainOpacity = 100;
    g_wallpaperAccent = (IniGetInt(L"Theme", L"Wallpaper", 0) != 0);
    // 主题色相：老配置没有 Theme/Hue 时，由旧“高亮颜色”的色相换算得到
    int hue = IniGetInt(L"Theme", L"Hue", -1);
    if (hue < HKB_HUE_MIN || hue > HKB_HUE_MAX) {
        hue = (int)(OklchHueOfBgr((DWORD)IniGetInt(L"General", L"HighlightColor", 0xD47800)) + 0.5);
        if (hue > HKB_HUE_MAX) hue = HKB_HUE_MAX;
    }
    g_hue = hue;
    g_layoutMode = IniGetInt(L"Keyboard", L"Layout", 0);
    if (g_layoutMode < 0 || g_layoutMode > 2) g_layoutMode = 0;
    g_showNumBtn = IniGetInt(L"Keyboard", L"ShowNumBtn", 1) != 0;
    g_npHidden = IniGetInt(L"Keyboard", L"NpHidden", 0) != 0;
    g_showFKeys = (IniGetInt(L"Keyboard", L"FKeys", 0) != 0);
    g_fnWebLayout = (IniGetInt(L"Keyboard", L"FnWebLayout", 0) != 0);
    // 键面固定「图标+文字」：不再提供「仅文字」模式（实机反馈），设置页也没有对应行了。
    // 这里仍然读一次 ini 只为了让老配置不残留（值一律归一到 2）。
    g_keyIconStyle = 2;                          // 老配置里残留的 0 / 1 一并归一
    g_shiftSymbols = (IniGetInt(L"General", L"ShiftSymbols", 1) != 0);
    g_hideDelayMs = 300;    // 失焦后的自动隐藏延迟（见定义处说明）
    g_lang = IniGetInt(L"General", L"Language", 0);
    if (g_lang < 0 || g_lang > 1) g_lang = 0;
    g_af = (IniGetInt(L"General", L"AutoPopup", 1) != 0);
    g_afAutoHide = (IniGetInt(L"General", L"AutoHide", 1) != 0);
}

// 持久化“× 关闭行为”选择
static void SaveCloseSettings() {
    IniSetInt(L"General", L"RememberClose", g_rememberClose ? 1 : 0);
    IniSetInt(L"General", L"CloseToTray", g_closeToTray ? 1 : 0);
}

// 持久化主题选择（设置页 / 菜单修改时调用）
static void SaveThemeConfig() {
    IniSetInt(L"Theme", L"Mode", g_themeMode);
    IniSetInt(L"Theme", L"Wallpaper", g_wallpaperAccent ? 1 : 0);
    IniSetInt(L"Theme", L"Hue", g_hue);
}

// 持久化键盘布局设置
static void SaveLayoutConfig() {
    IniSetInt(L"Keyboard", L"Layout", g_layoutMode);
    IniSetInt(L"Keyboard", L"FKeys", g_showFKeys ? 1 : 0);
    IniSetInt(L"Keyboard", L"FnWebLayout", g_fnWebLayout ? 1 : 0);
    IniSetInt(L"Keyboard", L"KeyIconStyle", g_keyIconStyle);
}

// 应用键盘布局：保存设置、重建按键；resetSize=TRUE 时按布局与 DPI 重置窗口大小
// （仅布局模式切换调用；功能键行等只增减键行的开关保持当前窗口大小）
static void ApplyKeyboardLayout(BOOL resetSize) {
    SaveLayoutConfig();
    if (resetSize) {
        RECT saved;
        // 完整布局 6 行结构需要更高窗口：旧版本记忆的 5 行高度作废，按 DPI 重算
        if (LoadLayoutWindowRect(&saved) && LayoutRectOnScreen(saved)
            && !(g_layoutMode == 2 && saved.bottom - saved.top < (int)(350 * GetSystemDpiScale()))) {
            g_ww = saved.right - saved.left;    // 恢复该布局记忆的大小
            g_wh = saved.bottom - saved.top;
        } else {
            InitWindowSizeForDpi();
        }
    }
    if (g_hWnd && IsWindow(g_hWnd)) {
        RecreateFontsAndLayout();
        if (resetSize)
            SetWindowPos(g_hWnd, HWND_TOPMOST, 0, 0, g_ww, g_wh, SWP_NOMOVE | SWP_NOACTIVATE);
        InvalidateRect(g_hWnd, NULL, TRUE);
    }
}

// HEX 编辑：输入完整合法 #RRGGBB 时取其色相实时换肤
static void TryApplyHexEdit(HWND hWnd) {
    DWORD bgr;
    if (!ParseHexToBgr(g_hlEditBuf, &bgr)) return;
    int hue = (int)(OklchHueOfBgr(bgr) + 0.5);
    if (hue > HKB_HUE_MAX) hue = HKB_HUE_MAX;
    g_hue = hue;
    g_wallpaperAccent = FALSE;
    ApplyTheme();
    SaveThemeConfig();
    if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
    (void)hWnd;
}
static void CommitHexEdit(HWND hWnd) {
    TryApplyHexEdit(hWnd);   // 合法则应用，非法则保留原色
    g_hlEditFocus = FALSE;
    InvalidateRect(hWnd, NULL, TRUE);
}

// 拖动色相滑轨：0..359 闭区间（上限不能是 360，否则末端会被取模打回 0）
static void UpdateHueSlider(HWND hWnd, int mouseX) {
    SettingsMetrics m = GetSettingsMetrics(hWnd);
    RECT row = SettingsRowRect(m, 2);
    RECT slider = SettingsColorSliderRect(m, row);
    double p = (double)(mouseX - slider.left) / (double)(slider.right - slider.left - 1);
    if (p < 0.0) p = 0.0; if (p > 1.0) p = 1.0;

    g_hue = (int)(p * (double)HKB_HUE_MAX + 0.5);
    if (g_hue > HKB_HUE_MAX) g_hue = HKB_HUE_MAX;
    g_wallpaperAccent = FALSE;
    g_hlEditFocus = FALSE;
    ApplyTheme();
    SaveThemeConfig();
    if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
    RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
}

static void SettingsApplyHit(HWND hWnd, int hit, int x) {
    BOOL themeChanged = FALSE;
    BOOL layoutChanged = FALSE;        // 布局模式切换：按布局重置窗口大小
    BOOL keyRowsChanged = FALSE;       // 仅增减键行（功能键行）：保持窗口大小
    switch (hit) {
    case S_HIT_AUTO:
        BeginSwitchAnimation(hWnd, hit, g_af, !g_af);
        g_af = !g_af;
        IniSetInt(L"General", L"AutoPopup", g_af ? 1 : 0);
        if (g_af) UpdateAutoVisibility();
        break;
    case S_HIT_AUTOHIDE:
        BeginSwitchAnimation(hWnd, hit, g_afAutoHide, !g_afAutoHide);
        g_afAutoHide = !g_afAutoHide;
        IniSetInt(L"General", L"AutoHide", g_afAutoHide ? 1 : 0);
        break;
    case S_HIT_CLOSE_DROP: {   // 画框选择：按点击的段直接应用
        SettingsMetrics cm = GetSettingsMetrics(hWnd);
        RECT cr = SettingsRowRect(cm, g_af ? 2 : 1);
        const wchar_t* it[2]; int n = CloseSegItems(it);
        int idx = RowSegIndex(cm, cr, it, n, x);
        if (idx >= 0) {
            g_closeToTray = (idx == 1);
            if (g_rememberClose) SaveCloseSettings();
        }
        break;
    }
    case S_HIT_REMEMBER:
        BeginSwitchAnimation(hWnd, hit, g_rememberClose, !g_rememberClose);
        g_rememberClose = !g_rememberClose;
        SaveCloseSettings();   // 持久化“记住我的选择”
        break;
    case S_HIT_LAYOUT_DROP: {
        // 画框选择：直接按点击的段应用
        SettingsMetrics lm = GetSettingsMetrics(hWnd);
        RECT lr = SettingsRowRect(lm, 0);
        const wchar_t* it[3]; int n = LayoutSegItems(it);
        int idx = RowSegIndex(lm, lr, it, n, x);
        if (idx >= 0 && idx != g_layoutMode) { g_layoutMode = idx; layoutChanged = TRUE; }
        break;
    }
    case S_HIT_FKEYS:
        BeginSwitchAnimation(hWnd, hit, g_showFKeys, !g_showFKeys);
        g_showFKeys = !g_showFKeys;
        keyRowsChanged = TRUE;
        break;
    case S_HIT_FNWEB:
        BeginSwitchAnimation(hWnd, hit, g_fnWebLayout, !g_fnWebLayout);
        g_fnWebLayout = !g_fnWebLayout;
        if (!g_fnWebLayout && g_fnLayer) {
            // 关闭网页布局时若停留在该层则退出 Fn 层
            g_fnLayer = FALSE;
        }
        IniSetInt(L"Keyboard", L"FnWebLayout", g_fnWebLayout ? 1 : 0);
        if (g_hWnd && IsWindow(g_hWnd)) {
            BuildKeys();   // 网页布局层切换需重建键位表
            InvalidateRect(g_hWnd, NULL, TRUE);
        }
        break;
    case S_HIT_NPBTN:
        BeginSwitchAnimation(hWnd, hit, g_showNumBtn, !g_showNumBtn);
        g_showNumBtn = !g_showNumBtn;
        IniSetInt(L"Keyboard", L"ShowNumBtn", g_showNumBtn ? 1 : 0);
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);   // 主窗口标题栏按钮显隐
        break;
    case S_HIT_SHIFTSYM:
        BeginSwitchAnimation(hWnd, hit, g_shiftSymbols, !g_shiftSymbols);
        g_shiftSymbols = !g_shiftSymbols;
        IniSetInt(L"General", L"ShiftSymbols", g_shiftSymbols ? 1 : 0);
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
        break;
    case S_HIT_THEME_DROP: {
        SettingsMetrics tm = GetSettingsMetrics(hWnd);
        RECT tr = SettingsRowRect(tm, 0);
        const wchar_t* it[3]; int n = ThemeSegItems(it);
        int idx = RowSegIndex(tm, tr, it, n, x);
        if (idx >= 0 && idx != g_themeMode) { g_themeMode = idx; themeChanged = TRUE; }
        break;
    }
    case S_HIT_OPACITY_DROP: {
        SettingsMetrics om = GetSettingsMetrics(hWnd);
        RECT orr = SettingsRowRect(om, 1);
        const wchar_t* it[6]; int n = OpacitySegItems(it);
        int idx = RowSegIndex(om, orr, it, n, x);
        if (idx >= 0) {
            g_mainOpacity = g_opacityValues[idx];
            IniSetInt(L"Theme", L"Opacity", g_mainOpacity);
            if (g_hWnd && IsWindow(g_hWnd)) {   // 立即应用透明度
                ApplyWindowOpacity(g_hWnd, g_mainOpacity < 100);
                RedrawWindow(g_hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
            }
        }
        break;
    }
    case S_HIT_LANG_DROP: {
        SettingsMetrics gm = GetSettingsMetrics(hWnd);
        RECT gr = SettingsRowRect(gm, (g_af ? 2 : 1) + 4);
        const wchar_t* it[2]; int n = LangSegItems(it);
        int idx = RowSegIndex(gm, gr, it, n, x);
        if (idx >= 0 && idx != g_lang) {
            g_lang = idx;
            IniSetInt(L"General", L"Language", g_lang);
            if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);   // 主键盘文本立即切换
        }
        break;
    }
    case S_HIT_HL_DROP: {
        SettingsMetrics hm = GetSettingsMetrics(hWnd);
        RECT hr = SettingsRowRect(hm, 2);
        const wchar_t* it[2]; int n = HueModeSegItems(it);
        int idx = RowSegIndexTop(hm, hr, it, n, x);   // 色相行的控件在行顶部
        if (idx >= 0) {
            g_wallpaperAccent = (idx == 1);
            if (g_wallpaperAccent) g_hlEditFocus = FALSE;
            ApplyTheme();
            SaveThemeConfig();
            if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
        }
        break;
    }
    case S_HIT_HL_BOX:
        if (!g_wallpaperAccent) {
            if (g_hlEditFocus) {
                CommitHexEdit(hWnd);
            } else {
                g_hlEditFocus = TRUE;
                HexFromBgr(HueAccentBgr(g_hue), g_hlEditBuf);
                SetFocus(hWnd);
            }
        }
        break;
    case S_HIT_HL_PAL0:
    case S_HIT_HL_PAL0 + 1:
    case S_HIT_HL_PAL0 + 2:
    case S_HIT_HL_PAL0 + 3:
    case S_HIT_HL_PAL0 + 4:
        g_hue = g_presetHues[hit - S_HIT_HL_PAL0];
        g_wallpaperAccent = FALSE;
        g_hlEditFocus = FALSE;
        ApplyTheme();
        SaveThemeConfig();
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
        break;
    case S_HIT_URL:
        ShellExecuteW(NULL, L"open", L"https://github.com/PanDaDaTech/Hydrogen-Keyboard", NULL, NULL, SW_SHOWNORMAL);
        break;
    case S_HIT_LICENSE:
        ShellExecuteW(NULL, L"open", L"https://github.com/PanDaDaTech/Hydrogen-Keyboard/blob/main/LICENSE", NULL, NULL, SW_SHOWNORMAL);
        break;
    default: return;
    }
    if (themeChanged) {
        ApplyTheme();                                   // 立即换肤
        SaveThemeConfig();                              // 持久化主题选择
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
    }
    if (layoutChanged) ApplyKeyboardLayout(TRUE);       // 应用布局并按布局重置窗口大小
    if (keyRowsChanged) ApplyKeyboardLayout(FALSE);     // 仅重建键行，保持当前窗口大小
    SettingsFitHeight(hWnd);                            // 行数可能变了（自动收起/布局/色相展开）：高度跟着收边
    RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE); // 设置页立即刷新
}

// ========== 窗口渐显/渐隐（记事本风格 fade） ==========
// AnimateWindow 的 AW_BLEND 在 Win8+ 的 DWM 合成窗口上无效。改用所有
// "窗口淡入淡出"工具的标准做法：临时给窗口加 WS_EX_LAYERED，用
// SetLayeredWindowAttributes 对整个窗口（含材质背景）做整体透明度渐变，
// 任何主题/材质/模式下都必然可见；动画结束移除分层属性恢复正常呈现。
struct WinFade {
    BOOL     active;
    BOOL     closing;    // TRUE=渐隐（结束后销毁窗口）
    LONGLONG start;
};
static WinFade g_settingsFade = {};
static WinFade g_promptFade = {};
#define WIN_FADE_MS 180

static void StartWindowFade(HWND hWnd, WinFade& f, BOOL closing) {
    f.active = TRUE;
    f.closing = closing;
    f.start = QpcNowMs();
    LONG ex = GetWindowLongW(hWnd, GWL_EXSTYLE);
    if (!(ex & WS_EX_LAYERED)) {
        SetWindowLongW(hWnd, GWL_EXSTYLE, ex | WS_EX_LAYERED);
        SetLayeredWindowAttributes(hWnd, 0, closing ? 255 : 0, LWA_ALPHA);
    }
    SetTimer(hWnd, TIMER_WIN_FADE, 10, NULL);
}

// 渐变结束/取消后的恢复：设置页 / 关闭提示窗口恢复不透明
static void FinishWindowFade(HWND hWnd, WinFade& f) {
    f.active = FALSE;
    KillTimer(hWnd, TIMER_WIN_FADE);
    if (f.closing) {
        DestroyWindow(hWnd);   // 渐隐至全透明后销毁
        return;
    }
    LONG ex = GetWindowLongW(hWnd, GWL_EXSTYLE);
    SetWindowLongW(hWnd, GWL_EXSTYLE, ex & ~WS_EX_LAYERED);
    RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
}

static void CancelWindowFade(HWND hWnd, WinFade& f) {
    BOOL wasClosing = f.closing;
    f.active = FALSE;
    KillTimer(hWnd, TIMER_WIN_FADE);
    f.closing = FALSE;
    FinishWindowFade(hWnd, f);
    (void)wasClosing;
}

static void TickWindowFade(HWND hWnd, WinFade& f) {
    double t = (QpcNowMs() - f.start) / (double)WIN_FADE_MS;
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    double e = t * t * (3.0 - 2.0 * t);
    BYTE a = f.closing ? (BYTE)(255 * (1.0 - e) + 0.5) : (BYTE)(255 * e + 0.5);
    SetLayeredWindowAttributes(hWnd, 0, a, LWA_ALPHA);
    if (t >= 1.0) FinishWindowFade(hWnd, f);
}

static void CloseSettingsAnimated(HWND hWnd) {
    // 记事本式渐隐：淡出结束后由 TickWindowFade 销毁窗口
    if (!hWnd || !IsWindow(hWnd) || g_settingsClosing) return;
    g_settingsClosing = TRUE;
    StartWindowFade(hWnd, g_settingsFade, TRUE);
}

static void SettingsOnClick(HWND hWnd, int x, int y) {
    if (g_settingsClosing) return;   // 渐隐关闭中不再响应点击
    int hit = SettingsHitTest(hWnd, x, y);
    if (g_hlEditFocus && hit != S_HIT_HL_BOX) CommitHexEdit(hWnd);   // 点击其它位置时提交 HEX 编辑
    if (hit == S_HIT_HL_HUE) {
        g_hlSliderDrag = hit;
        SetCapture(hWnd);
        UpdateHueSlider(hWnd, x);
        return;
    }
    if (hit == S_HIT_CLOSE) { SendMessageW(hWnd, WM_CLOSE, 0, 0); return; }
    if (hit == S_HIT_TABL || (hit >= S_HIT_TAB0 && hit <= S_HIT_TAB2)) {
        // Tab 即时切换（不做逐帧过渡，避免配色异常）
        g_sTab = (hit == S_HIT_TABL) ? 3 : (hit - S_HIT_TAB0);
        SettingsFitHeight(hWnd);   // 各 tab 内容高度不同：切 tab 时把窗口收到刚好包住内容
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
        return;
    }
    if (hit != S_HIT_NONE) {
        SettingsApplyHit(hWnd, hit, x);   // 画框选择需要点击的横向位置来定位段
    }
}

static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE:
        ApplyRoundedWindow(hWnd, 14);
        {
            // 显式确保 DWM 窗口过渡未被禁用（属性 3 默认开启，防御性设置）
            DwmSetWindowAttributeProc setAttr = GetDwmSetWindowAttribute();
            if (setAttr) {
                const DWORD DWMWA_TRANSITIONS_FORCEDISABLED = 3;
                BOOL disable = FALSE;
                setAttr(hWnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disable, sizeof(disable));
            }
        }
        return 0;
    case WM_SIZE:
        ApplyRoundedWindow(hWnd, 14);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        WindowPaintSurfaceLocal surface = BeginWindowPaintSurface(dc, hWnd, rc);
        ClearWindowBackBuffer(surface.dc, hWnd, rc.right, rc.bottom);
        SettingsDraw(surface.dc, hWnd);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, surface.dc, 0, 0, SRCCOPY);
        EndWindowPaintSurface(&surface);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SettingsOnClick(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        return 0;
    case WM_LBUTTONUP:
        if (g_hlSliderDrag != S_HIT_NONE) {
            UpdateHueSlider(hWnd, GET_X_LPARAM(l));
            g_hlSliderDrag = S_HIT_NONE;
            if (GetCapture() == hWnd) ReleaseCapture();
            return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        if (g_hlSliderDrag != S_HIT_NONE) {
            UpdateHueSlider(hWnd, GET_X_LPARAM(l));
            return 0;
        }
        if (!g_sTracking) { TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hWnd, 0}; TrackMouseEvent(&tme); g_sTracking = TRUE; }
        int hov = SettingsHitTest(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (hov != g_sHov) { g_sHov = hov; InvalidateRect(hWnd, NULL, TRUE); }
        return 0;
    }
    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hWnd, &pt);
        if (g_sTab == 2) {
            int hv = SettingsHitTest(hWnd, pt.x, pt.y);
            if (hv == S_HIT_URL || hv == S_HIT_LICENSE) {
                SetCursor(LoadCursor(NULL, IDC_HAND));
                return TRUE;
            }
        }
        break;
    }
    case WM_MOUSELEAVE:
        g_sTracking = FALSE;
        if (g_sHov != -1) { g_sHov = -1; InvalidateRect(hWnd, NULL, TRUE); }
        return 0;
    case WM_CAPTURECHANGED:
        g_hlSliderDrag = S_HIT_NONE;
        return 0;
    case WM_ENTERSIZEMOVE:
        StopWindowMotion(&g_settingsMotion);
        g_settingsMoving = TRUE;
        KillTimer(hWnd, TIMER_SETTINGS_ANIM);
        g_switchAnimHit = S_HIT_NONE;
        return 0;
    case WM_EXITSIZEMOVE:
        g_settingsMoving = FALSE;
        InvalidateRect(hWnd, NULL, FALSE);
        return 0;
    case WM_TIMER:
        if (w == TIMER_WINDOW_ANIM) {
            TickWindowMotion(&g_settingsMotion, hWnd);
            return 0;
        }
        if (w == TIMER_WIN_FADE) {
            TickWindowFade(hWnd, g_settingsFade);
            return 0;
        }
        if (w == TIMER_SETTINGS_ANIM) {
            int hit = g_switchAnimHit;
            if (hit == S_HIT_NONE || g_settingsMoving ||
                QpcNowMs() - g_switchAnimStart >= 180) {
                KillTimer(hWnd, TIMER_SETTINGS_ANIM);
                g_switchAnimHit = S_HIT_NONE;
            }
            if (hit != S_HIT_NONE) {
                SettingsMetrics m = GetSettingsMetrics(hWnd);
                RECT dirty = SettingsSwitchRect(m, hit);
                RedrawWindow(hWnd, &dirty, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
            }
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            if (g_hlEditFocus) { g_hlEditFocus = FALSE; InvalidateRect(hWnd, NULL, TRUE); }
            else SendMessageW(hWnd, WM_CLOSE, 0, 0);
            return 0;
        }
        if (g_hlEditFocus && w == VK_BACK) {
            int len = (int)wcslen(g_hlEditBuf);
            if (len > 0) { g_hlEditBuf[len - 1] = 0; TryApplyHexEdit(hWnd); }
            InvalidateRect(hWnd, NULL, TRUE);
            return 0;
        }
        if (g_hlEditFocus && w == VK_RETURN) { CommitHexEdit(hWnd); return 0; }
        break;
    case WM_CHAR:
        if (g_hlEditFocus) {
            wchar_t c = (wchar_t)w;
            if (c == L'#' && g_hlEditBuf[0] == 0) { g_hlEditBuf[0] = L'#'; g_hlEditBuf[1] = 0; }
            else if (HexVal(c) >= 0) {
                int len = (int)wcslen(g_hlEditBuf);
                if (len < 7) { g_hlEditBuf[len] = c; g_hlEditBuf[len + 1] = 0; }
            }
            TryApplyHexEdit(hWnd);
            InvalidateRect(hWnd, NULL, TRUE);
            return 0;
        }
        break;
    case WM_KILLFOCUS:
        if (g_hlEditFocus) CommitHexEdit(hWnd);
        break;
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ScreenToClient(hWnd, &pt);
        SettingsMetrics m = GetSettingsMetrics(hWnd);
        if (pt.y >= 0 && pt.y < m.tabsY) {
            if (SettingsHitTest(hWnd, pt.x, pt.y) != S_HIT_CLOSE) return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_CLOSE: CloseSettingsAnimated(hWnd); return 0;
    case WM_DESTROY:
        StopWindowMotion(&g_settingsMotion);
        KillTimer(hWnd, TIMER_SETTINGS_ANIM);
        g_settingsFade = {};
        g_settingsHwnd = NULL;
        g_settingsClosing = FALSE;
        g_settingsMoving = FALSE;
        g_sHov = -1;
        g_sTracking = FALSE;
        g_hlEditFocus = FALSE;
        g_hlSliderDrag = S_HIT_NONE;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, w, l);
}

static void OpenSettingsTab(int tab) {
    g_sTab = (tab >= 0 && tab <= 2) ? tab : 0;   // 0=常规 1=主题 2=关于
    if (g_settingsHwnd && IsWindow(g_settingsHwnd)) {
        if (g_settingsFade.active) {
            // 渐隐中途重新打开：取消渐隐并恢复不透明
            CancelWindowFade(g_settingsHwnd, g_settingsFade);
        }
        if (!IsWindowVisible(g_settingsHwnd)) {
            // 先置为全透明，再显示并渐显
            StartWindowFade(g_settingsHwnd, g_settingsFade, FALSE);
            ShowWindow(g_settingsHwnd, SW_SHOW);
        }
        SetForegroundWindow(g_settingsHwnd);
        SettingsFitHeight(g_settingsHwnd);   // 可能是从菜单直接指定 tab 打开的，高度要跟着走
        InvalidateRect(g_settingsHwnd, NULL, TRUE);   // 切到指定 Tab 后刷新
        return;
    }
    double dpi = GetSystemDpiScale();
    int w = (int)(700 * dpi), h = (int)(575 * dpi);
    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + ((work.right - work.left) - w) / 2;
    int y = work.top + ((work.bottom - work.top) - h) / 2;
    g_settingsHwnd = CreateWindowExW(WS_EX_TOPMOST, L"HKeyboardSettings", T(L"设置", L"Settings"), WS_POPUP,
        x, y, w, h, NULL, NULL, g_hInst, NULL);
    if (g_settingsHwnd) {
        // 高度先按当前 tab 的内容收边，再按新高度重新垂直居中（否则会偏上）
        SettingsFitHeight(g_settingsHwnd);
        RECT rc;
        if (GetWindowRect(g_settingsHwnd, &rc)) {
            int nh = rc.bottom - rc.top;
            int ny = work.top + ((work.bottom - work.top) - nh) / 2;
            SetWindowPos(g_settingsHwnd, HWND_TOPMOST, rc.left, ny,
                         rc.right - rc.left, nh, SWP_NOACTIVATE);
        }
        // 先置为全透明，再显示并渐显（避免闪现一帧不透明内容）
        StartWindowFade(g_settingsHwnd, g_settingsFade, FALSE);
        ShowWindow(g_settingsHwnd, SW_SHOW);
        SetForegroundWindow(g_settingsHwnd);
    }
}

static void OpenSettings() { OpenSettingsTab(0); }   // 默认打开“常规”Tab

// ========== 关闭方式提示窗口 ==========
#define P_HIT_NONE      0
#define P_HIT_CLOSE     1
#define P_HIT_DIRECT    2
#define P_HIT_TRAY      3
#define P_HIT_REMEMBER  4
#define P_HIT_OK        5
#define P_HIT_CANCEL    6

static int  g_pChoice = 0;       // 0=直接退出 1=隐藏到托盘
static BOOL g_pRemember = FALSE;
static int  g_pHov = -1;
static BOOL g_pTracking = FALSE;
static BOOL g_promptClosing = FALSE;

static void ClosePromptAnimated(HWND hWnd) {
    // 记事本式渐隐：淡出结束后由 TickWindowFade 销毁窗口
    if (!hWnd || !IsWindow(hWnd) || g_promptClosing) return;
    g_promptClosing = TRUE;
    StartWindowFade(hWnd, g_promptFade, TRUE);
}

// 关闭方式提示窗口的几何 —— **绘制与命中必须读同一份**（PromptDraw / PromptHitTest）。
//
// 原来两边各算一遍，尺寸改一处就漏一处（老坑：按钮画在右边、热区还在左边）。
// 全部 DIP，乘 dpi 后给像素。
//
// 结构照搬设置页的「标题 + 卡片 + 行」语法（见用户实机图）：
//   标题行   图标块 + 「是否关闭轻键」         （落在 pageBg 淡紫底上，不进卡片）
//   卡片     C_KEY 白底 + 16 圆角，两行内容（**均无描述**，按用户要求）：
//              选择关闭方式 [✕] 标题   右对齐分段控件
//              记住我的选择 [✓] 标题   右对齐开关
//   按钮区   分隔线 + 确定 / 取消            （也在淡紫底上）
// 这样对话框就是设置页「常规」Tab 的一个特写：卡片、图标块、分段、开关全是同一套令牌，
// 不再是「卡片里摆一堆孤立控件」的另一套语法。
struct PromptLayout {
    int pad;        // 左右内边距（也等于上下留白）
    int closeW, closeH, closeX, closeY;   // 右上关闭钮
    int headIcon;   // 标题图标块边长（与设置页 headIcon 同为 38）
    int titleY;     // 标题行盒顶
    int titleH;     // 标题行高

    int cardX, cardY, cardW, cardH, cardR;   // 白色卡片
    int rowPadY;    // 行上下内边距（与设置页 rowPadY 同为 12）
    int tile;       // 行图标块边长（设置页 tileSize = 30）
    int tileGap;    // 图标块↔文字块间距（设置页 tileGap = 18）
    int rowH;       // 单行高（12 + 30 + 12）
    int titleH2;    // 行内标题行高

    int segRowY;    // 「关闭按钮」行盒顶
    int segH;       // 分段控件高（设置页 comboH = 40）
    int segX;       // 分段轨道左缘
    int segW;       // 分段轨道宽（按两项文字量出来）

    int remRowY;    // 「记住我的选择」行盒顶
    int swW, swH, swX;

    int lineY;      // 卡片与按钮区之间的分隔线
    int btnW, btnH, okX, cancelX, btnY;
};

// 纵向推进链（单位 DIP；窗口高 287 = 上下各 20 + 内容 247）：
//   20 标题行(38) → 58 +18 → 76 卡片顶
//   卡片内：12(上留白) + 54(行) + 1(行间分隔线) + 54(行) + 12(下留白) = 133，圆角 16
//   → 76 + 133 = 209 卡片底 → +16 → 225 分隔线 → +12 → 237 按钮(30)
//   → 267 +20 底留白 = 287
// ⚠ 卡片高**必须含上下 rowPadY**。原来漏成 rowH*2+1 = 109，让行 2 底超出
//   卡片底 12 DIP（卡片内上留白 24.6 / 下留白 0.6，肉眼是「下面顶到边了」）。
// ⚠ 改任何一个间距都要重算这条链，并同步 OpenClosePrompt 里的窗口高。
static PromptLayout PromptComputeLayout(int W, double dpi) {
    PromptLayout L;
    L.pad      = (int)(20 * dpi);

    L.closeW = (int)(28 * dpi);
    L.closeH = (int)(28 * dpi);
    L.closeX = W - L.pad - L.closeW;
    L.closeY = (int)(5 * dpi);

    L.headIcon = (int)(38 * dpi);
    L.titleY   = L.pad;
    L.titleH   = L.headIcon;

    L.rowPadY  = (int)(12 * dpi);
    L.tile     = (int)(30 * dpi);
    L.tileGap  = (int)(18 * dpi);
    L.rowH     = L.rowPadY * 2 + L.tile;
    L.titleH2  = (int)(18 * dpi);

    L.cardX = L.pad;
    L.cardY = L.titleY + L.titleH + (int)(18 * dpi);
    L.cardR = (int)(16 * dpi);
    L.cardW = W - L.pad * 2;
    // ⚠ 必须含上下 rowPadY：`rowPadY*2 + rowH*2 + 1`。
    //   原来漏成`rowH*2 + 1`，行 2 底超出卡片底 12 DIP ——
    //   卡片内上留白 24.6 / 下留白 0.6，肉眼就是「下面顶到边了」（用户实测发现）。
    L.cardH = L.rowPadY * 2 + L.rowH * 2 + 1;

    // 卡片内两行
    L.segRowY = L.cardY + L.rowPadY;
    L.remRowY = L.segRowY + L.rowH + 1;

    L.segH = (int)(40 * dpi);   // 与设置页 comboH 一致
    {
        // 段宽按文字量（SegmentedItemW 与设置页分段共用同一份算法）
        static const wchar_t* items[2];
        CloseSegItems(items);
        int w = (int)(6 * dpi);
        for (int i = 0; i < 2; i++) w += SegmentedItemWAtDpi(items[i], dpi);
        L.segW = w;
    }
    // ⚠ 右对齐必须减掉 segW **自身宽度**（设置页 RowSegRect 就是
    //   x = row.right - 20*dpi - w）。只减到「右边界」会让分段控件
    //   整体向右溢出 segW 像素，末段被窗口裁掉。
    L.segX = W - L.pad - (int)(20 * dpi) - L.segW;
    L.swW = (int)(46 * dpi);   // 与设置页 switchW 一致
    L.swH = (int)(26 * dpi);
    L.swX = W - L.pad - (int)(20 * dpi) - L.swW;

    L.lineY = L.cardY + L.cardH + (int)(16 * dpi);
    L.btnW  = (int)(84 * dpi);
    L.btnH  = (int)(30 * dpi);
    L.btnY  = L.lineY + (int)(12 * dpi);
    L.cancelX = W - L.pad - L.btnW;
    L.okX     = L.cancelX - (int)(10 * dpi) - L.btnW;
    return L;
}

static void PromptDraw(HDC dc, HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    int W = rc.right, H = rc.bottom;
    double dpi = GetSystemDpiScale();
    (void)H;
    PromptLayout L = PromptComputeLayout(W, dpi);

    // 窗口底：**pageBg**（淡紫），与设置页页面底同一层 ——
    // 白卡片浮在它上面构成层级。原来的纯白底把卡片「吸」进去了，看着像一张大表格。
    DrawRoundRect(dc, 0, 0, W, H, C_BG, C_BG, (int)(14 * dpi));

    // 标题行：图标块 + 「是否关闭轻键」（落在 pageBg 上，不进卡片 —— 与设置页页头同一语法）
    //图标用 DIALOGINFO（ⓘ圆圈信息），语义上是「询问」而不是「关闭」——
    //   HKICON_CLOSE 已经被右上角那个 ✕ 用了，两处同图标会让人以为是同一个按钮。
    DrawRoundRect(dc, L.pad, L.titleY, L.headIcon, L.headIcon,
                  C_REGULAR, C_REGULAR, (int)(9 * dpi));
    {
        int isz = (int)(17 * dpi);
        DrawHkIcon(dc, (float)(L.pad + (L.headIcon - isz) / 2),
                   (float)(L.titleY + (L.headIcon - isz) / 2), (float)isz,
                   HkIcon(HKICON_DIALOGINFO), C_BTN_CONTENT, C_BTN_CONTENT);
    }
    DrawTextL(dc, L.pad + L.headIcon + (int)(12 * dpi), L.titleY,
              L.closeX - (L.pad + L.headIcon) - (int)(12 * dpi), L.titleH,
              T(L"是否关闭轻键", L"Close HKeyboard?"), g_sfCtrl, C_WHITE);

    // 右上关闭钮：平时不铺底，悬停才给一层 btn_regular_bg_hover（与设置页一致）
    if (g_pHov == P_HIT_CLOSE) {
        DrawRoundRect(dc, L.closeX, L.closeY, L.closeW, L.closeH,
                      C_REGULAR_HOV, C_REGULAR_HOV, (int)(6 * dpi));
    }
    {
        int sz = (int)(14 * dpi);
        DrawHkIcon(dc, (float)(L.closeX + (L.closeW - sz) / 2),
                   (float)(L.closeY + (L.closeH - sz) / 2), (float)sz,
                   HkIcon(HKICON_CLOSE), C_DIM, C_DIM);
    }

    // 卡片：cardBg 白底 + 16 圆角，无描边无阴影（与设置页 DrawSettingsCard 同一配方）
    DrawRoundRect(dc, L.cardX, L.cardY, L.cardW, L.cardH, C_KEY, C_KEY, L.cardR);
    // 行间分隔线（左右各缩进 20 —— 与设置页一致）
    Fill(dc, L.cardX + (int)(20 * dpi), L.segRowY + L.rowH,
         L.cardW - (int)(40 * dpi), 1, C_LINE_DIV);

    // 两行内容的文字块左缘（tile 右 + tileGap）
    int tx = L.cardX + (int)(20 * dpi) + L.tile + L.tileGap;
    int txMax = L.segX - (int)(12 * dpi);

    // ---- 行 1：选择关闭方式（描述已按要求去掉，标题在行盒内垂直居中）----
    {
        int ry = L.segRowY + (L.rowH - L.tile) / 2;
        DrawRoundRect(dc, L.cardX + (int)(20 * dpi), ry, L.tile, L.tile,
                      C_REGULAR, C_REGULAR, (int)(8 * dpi));
        int isz = (int)(15 * dpi);
        DrawHkIcon(dc, (float)(L.cardX + (int)(20 * dpi) + (L.tile - isz) / 2),
                   (float)(ry + (L.tile - isz) / 2), (float)isz,
                   HkIcon(HKICON_CLOSE), C_BTN_CONTENT, C_BTN_CONTENT);
        // ⚠ 没有描述了，标题必须按**整个行盒**居中（不是 titleH2 居中后再偏上），
        //   否则字会偏行盒中心 3 DIP —— 与右侧分段控件/开关不在一条基线上。
        int ty = L.segRowY + (L.rowH - L.titleH2) / 2;
        DrawTextL(dc, tx, ty, txMax - tx, L.titleH2,
                  T(L"选择关闭方式", L"Close action"), g_sfCtrl, C_WHITE);
    }
    // 行 1 右侧的分段控件（与设置页 DrawSegmented 同一配方）
    {
        const wchar_t* items[2];
        CloseSegItems(items);
        int sy = L.segRowY + (L.rowH - L.segH) / 2;
        int pad3 = (int)(3 * dpi);
        DrawRoundRect(dc, L.segX, sy, L.segW, L.segH,
                      C_REGULAR, C_REGULAR, (int)(10 * dpi));
        int x = L.segX + pad3;
        int ih = L.segH - pad3 * 2;
        for (int i = 0; i < 2; i++) {
            int w = SegmentedItemWAtDpi(items[i], dpi);
            if (i == g_pChoice) {
                DrawRoundRect(dc, x, sy + pad3, w, ih, C_KEY, C_KEY, (int)(8 * dpi));
            }
            DrawTextC(dc, x, sy + pad3, w, ih, items[i], g_sfCtrl,
                      (i == g_pChoice) ? C_BTN_CONTENT : C_DIM);
            x += w;
        }
    }

    // ---- 行 2：记住我的选择（描述已按要求去掉）----
    {
        int ry = L.remRowY + (L.rowH - L.tile) / 2;
        DrawRoundRect(dc, L.cardX + (int)(20 * dpi), ry, L.tile, L.tile,
                      C_REGULAR, C_REGULAR, (int)(8 * dpi));
        int isz = (int)(15 * dpi);
        DrawHkIcon(dc, (float)(L.cardX + (int)(20 * dpi) + (L.tile - isz) / 2),
                   (float)(ry + (L.tile - isz) / 2), (float)isz,
                   HkIcon(HKICON_CHECK), C_BTN_CONTENT, C_BTN_CONTENT);
        // ⚠ 同行 1：按整个行盒居中，别按 titleH2 居中
        int ty = L.remRowY + (L.rowH - L.titleH2) / 2;
        DrawTextL(dc, tx, ty, L.swX - (int)(12 * dpi) - tx, L.titleH2,
                  T(L"记住我的选择", L"Remember my choice"), g_sfCtrl, C_WHITE);
    }
    DrawSwitch(dc, L.swX, L.remRowY + (L.rowH - L.swH) / 2, L.swW, L.swH, g_pRemember);

    // 卡片与按钮区之间的分隔线
    Fill(dc, L.pad, L.lineY, W - L.pad * 2, 1, C_LINE_DIV);

    DrawRoundRect(dc, L.okX, L.btnY, L.btnW, L.btnH,
                  (g_pHov == P_HIT_OK) ? C_REGULAR_ACT : C_HOT, C_KEY_BORDER, (int)(6 * dpi));
    DrawTextC(dc, L.okX, L.btnY, L.btnW, L.btnH, T(L"确定", L"OK"), g_sfCtrl, C_ON_PRIMARY);
    // 「取消」改 btnRegularBg：白底描边会与主按钮同权重，主次拉不开
    DrawRoundRect(dc, L.cancelX, L.btnY, L.btnW, L.btnH,
                  (g_pHov == P_HIT_CANCEL) ? C_REGULAR_HOV : C_REGULAR, C_KEY_BORDER, (int)(6 * dpi));
    DrawTextC(dc, L.cancelX, L.btnY, L.btnW, L.btnH, T(L"取消", L"Cancel"), g_sfCtrl, C_BTN_CONTENT);
}

static int PromptHitTest(HWND hWnd, int x, int y) {
    RECT rc; GetClientRect(hWnd, &rc);
    int W = rc.right;
    double dpi = GetSystemDpiScale();
    // ⚠ 与 PromptDraw 读**同一个** PromptComputeLayout —— 原来两边各算一遍，
    // 改尺寸必然漏一处（老坑：按钮画在右边、热区还在左边）。
    PromptLayout L = PromptComputeLayout(W, dpi);

    if (x >= L.closeX && x < L.closeX + L.closeW &&
        y >= L.closeY && y < L.closeY + L.closeH) return P_HIT_CLOSE;

    // 分段控件：命中按段宽算，与绘制读同一套段宽算法（SegmentedItemWAtDpi）
    if (y >= L.segRowY && y < L.segRowY + L.rowH &&
        x >= L.segX && x < L.segX + L.segW) {
        const wchar_t* items[2];
        CloseSegItems(items);
        int cx = L.segX + (int)(3 * dpi);
        for (int i = 0; i < 2; i++) {
            int w = SegmentedItemWAtDpi(items[i], dpi);
            if (x >= cx && x < cx + w) return i == 0 ? P_HIT_DIRECT : P_HIT_TRAY;
            cx += w;
        }
    }

    // 「记住我的选择」：整行都是热区（与设置页的开关行一致，点行内空白也能切）
    if (x >= L.cardX && x < L.cardX + L.cardW &&
        y >= L.remRowY && y < L.remRowY + L.rowH) return P_HIT_REMEMBER;

    if (x >= L.okX && x < L.okX + L.btnW && y >= L.btnY && y < L.btnY + L.btnH) return P_HIT_OK;
    if (x >= L.cancelX && x < L.cancelX + L.btnW && y >= L.btnY && y < L.btnY + L.btnH) return P_HIT_CANCEL;
    return P_HIT_NONE;
}

static void PromptOnClick(HWND hWnd, int x, int y) {
    if (g_promptClosing) return;   // 渐隐关闭中不再响应点击
    int hit = PromptHitTest(hWnd, x, y);
    switch (hit) {
    case P_HIT_CLOSE:
    case P_HIT_CANCEL:
        ClosePromptAnimated(hWnd);
        return;
    case P_HIT_DIRECT:   g_pChoice = 0; break;
    case P_HIT_TRAY:     g_pChoice = 1; break;
    case P_HIT_REMEMBER: g_pRemember = !g_pRemember; break;
    case P_HIT_OK: {
        g_closeToTray = (g_pChoice == 1);
        g_rememberClose = g_pRemember;
        SaveCloseSettings();          // 持久化选择与“记住我的选择”标志
        ClosePromptAnimated(hWnd);
        if (g_closeToTray) {
            g_manualHide = TRUE;      // 显式隐藏到托盘后不再自动弹出
            ShowKB(FALSE, FALSE);
        } else if (g_hWnd && IsWindow(g_hWnd)) {
            ExitApplicationAnimated();
        }
        return;
    }
    default: return;
    }
    InvalidateRect(hWnd, NULL, TRUE);
}

static LRESULT CALLBACK PromptWndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE:
        ApplyRoundedWindow(hWnd, 14);
        {
            // 显式确保 DWM 窗口过渡未被禁用（属性 3 默认开启，防御性设置）
            DwmSetWindowAttributeProc setAttr = GetDwmSetWindowAttribute();
            if (setAttr) {
                const DWORD DWMWA_TRANSITIONS_FORCEDISABLED = 3;
                BOOL disable = FALSE;
                setAttr(hWnd, DWMWA_TRANSITIONS_FORCEDISABLED, &disable, sizeof(disable));
            }
        }
        return 0;
    case WM_SIZE:
        ApplyRoundedWindow(hWnd, 14);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        WindowPaintSurfaceLocal surface = BeginWindowPaintSurface(dc, hWnd, rc);
        ClearWindowBackBuffer(surface.dc, hWnd, rc.right, rc.bottom);
        PromptDraw(surface.dc, hWnd);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, surface.dc, 0, 0, SRCCOPY);
        EndWindowPaintSurface(&surface);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        PromptOnClick(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        return 0;
    case WM_MOUSEMOVE: {
        if (!g_pTracking) { TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hWnd, 0}; TrackMouseEvent(&tme); g_pTracking = TRUE; }
        int hov = PromptHitTest(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (hov != g_pHov) { g_pHov = hov; InvalidateRect(hWnd, NULL, TRUE); }
        return 0;
    }
    case WM_MOUSELEAVE:
        g_pTracking = FALSE;
        if (g_pHov != -1) { g_pHov = -1; InvalidateRect(hWnd, NULL, TRUE); }
        return 0;
    case WM_TIMER:
        if (w == TIMER_WINDOW_ANIM) {
            TickWindowMotion(&g_promptMotion, hWnd);
            return 0;
        }
        if (w == TIMER_WIN_FADE) {
            TickWindowFade(hWnd, g_promptFade);
            return 0;
        }
        break;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { ClosePromptAnimated(hWnd); return 0; }
        break;
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ScreenToClient(hWnd, &pt);
        int hdr = (int)(36 * GetSystemDpiScale());
        if (pt.y >= 0 && pt.y < hdr) {
            if (PromptHitTest(hWnd, pt.x, pt.y) != P_HIT_CLOSE) return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_CLOSE: ClosePromptAnimated(hWnd); return 0;
    case WM_DESTROY:
        StopWindowMotion(&g_promptMotion);
        g_closePromptHwnd = NULL;
        g_promptClosing = FALSE;
        g_promptFade = {};
        g_pHov = -1;
        g_pTracking = FALSE;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, w, l);
}

static void OpenClosePrompt() {
    if (g_closePromptHwnd && IsWindow(g_closePromptHwnd)) {
        if (g_promptFade.active) {
            // 渐隐中途重新打开：取消渐隐并恢复不透明
            CancelWindowFade(g_closePromptHwnd, g_promptFade);
            RedrawWindow(g_closePromptHwnd, NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
        }
        SetForegroundWindow(g_closePromptHwnd);
        return;
    }
    g_pChoice = g_closeToTray ? 1 : 0;
    g_pRemember = g_rememberClose;
    double dpi = GetSystemDpiScale();
    // 460x287：与 PromptComputeLayout 的推进链对齐 ——
    // 20 + 38(标题行) + 18 + 133(卡片：上下留白 24 + 两行 108 + 分隔线 1)
    // + 16 + 1(分隔线) + 12 + 30(按钮) + 20 = 287。上下留白各 20 DIP。
    //
    // ⚠ 宽度按**实机字宽**（MiSans 11pt）反推。描述去掉后约束变成了分段控件：
    //   行1 标题「选择关闭方式」= 90 DIP，但右侧分段控件 segW=197 DIP 才是瓶颈 ——
    //   tx(88) + 间距(12) + segW(197) + 右留白(40) = 337 DIP 起步。
    //   取 460 留 33 DIP 余量；设置页是 705 DIP，这里取其 65%。
    //   （曾经按「带描述」的口径定过 540，去掉描述后收窄，否则右边空一大片。）
    // ⚠ 改这个数必须同步 PromptComputeLayout 里的推进链。
    int w = (int)(460 * dpi), h = (int)(287 * dpi);
    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + ((work.right - work.left) - w) / 2;
    int y = work.top + ((work.bottom - work.top) - h) / 2;
    g_closePromptHwnd = CreateWindowExW(WS_EX_TOPMOST, L"HKeyboardClosePrompt", T(L"是否关闭轻键", L"Close HKeyboard?"), WS_POPUP,
        x, y, w, h, NULL, NULL, g_hInst, NULL);
    if (g_closePromptHwnd) {
        // 先置为全透明，再显示并渐显（避免闪现一帧不透明内容）
        StartWindowFade(g_closePromptHwnd, g_promptFade, FALSE);
        ShowWindow(g_closePromptHwnd, SW_SHOW);
        SetForegroundWindow(g_closePromptHwnd);
    }
}

static void ShowMenu(HWND hWnd) {
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    // “显示轻键”仅在自动呼出关闭且键盘隐藏时提供：
    // 自动呼出开启时键盘由呼出逻辑自动管理，无需手动呼出入口
    if (!g_af && !g_vis) {
        AppendMenuW(m, MF_STRING, ID_MENU_TOGGLE, T(L"\x663E\x793A\x8F7B\x952E", L"Show Keyboard"));
    }

    // 自动呼出：菜单勾选项（主界面不再显示开关按钮）
    AppendMenuW(m, MF_STRING | (g_af ? MF_CHECKED : 0), ID_MENU_AUTO, T(L"\x81EA\x52A8\x547C\x51FA", L"Auto Pop-up"));

    AppendMenuW(m, MF_STRING, ID_MENU_SETTINGS, T(L"\x8BBE\x7F6E", L"Settings"));

    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_MENU_ABOUT, T(L"\x5173\x4E8E", L"About"));
    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_MENU_EXIT, T(L"\x5173\x95ED\x8F7B\x952E", L"Close HKeyboard"));   // 关闭轻键

    SetForegroundWindow(hWnd);
    int id = TrackPopupMenu(m, TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_RETURNCMD, pt.x, pt.y, 0, hWnd, NULL);
    DestroyMenu(m);

    if (id == ID_MENU_TOGGLE) {
        g_dbgShowFrom = "tray-menu2"; ToggleKB();
    } else if (id == ID_MENU_AUTO) {
        g_af = !g_af;
        IniSetInt(L"General", L"AutoPopup", g_af ? 1 : 0);
        if (g_af) UpdateAutoVisibility();
        InvalidateRect(hWnd, 0, TRUE);
    } else if (id == ID_MENU_SETTINGS) {
        OpenSettings();
    } else if (id == ID_MENU_ABOUT) {
        OpenSettingsTab(2);   // 跳转到设置“关于”Tab
    } else if (id == ID_MENU_EXIT) {
        ExitApplicationAnimated();
    }
}

typedef HRESULT (WINAPI *AccessibleObjectFromWindowProc)(HWND, DWORD, REFIID, void**);

static const IID IID_IUnknownLocal =
    {0x00000000, 0x0000, 0x0000, {0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46}};
static const IID IID_IAccessibleLocal =
    {0x618736e0, 0x3c3d, 0x11cf, {0x81, 0x0c, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};

static AccessibleObjectFromWindowProc GetAccessibleObjectFromWindow() {
    static HMODULE module = NULL;
    static AccessibleObjectFromWindowProc proc = NULL;
    static BOOL initialized = FALSE;
    if (!initialized) {
        initialized = TRUE;
        module = LoadLibraryW(L"oleacc.dll");
        if (module) proc = (AccessibleObjectFromWindowProc)GetProcAddress(module, "AccessibleObjectFromWindow");
    }
    return proc;
}

static BOOL EnsureAccessibilityCom() {
    static BOOL initialized = FALSE;
    static BOOL ready = FALSE;
    if (!initialized) {
        initialized = TRUE;
        HRESULT hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
        ready = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
    return ready;
}

static ULONG_PTR AccessibleIdentityToken(IAccessible* acc, VARIANT child) {
    if (!acc) return 0;
    LONG left = 0, top = 0, width = 0, height = 0;
    if (SUCCEEDED(acc->accLocation(&left, &top, &width, &height, child)) && (width > 0 || height > 0)) {
        ULONG_PTR token = (ULONG_PTR)(DWORD)left;
        token = token * 16777619u ^ (ULONG_PTR)(DWORD)top;
        token = token * 16777619u ^ (ULONG_PTR)(DWORD)width;
        token = token * 16777619u ^ (ULONG_PTR)(DWORD)height;
        return token ? token : 1;
    }

    IUnknown* identity = NULL;
    ULONG_PTR token = 0;
    if (SUCCEEDED(acc->QueryInterface(IID_IUnknownLocal, (void**)&identity)) && identity) {
        token = (ULONG_PTR)identity;
        identity->Release();
    }
    return token;
}

static BOOL AccessibleRoleIsEditable(IAccessible* acc, VARIANT child) {
    if (!acc) return FALSE;
    VARIANT role, state;
    ZeroMemory(&role, sizeof(role));
    ZeroMemory(&state, sizeof(state));
    if (FAILED(acc->get_accRole(child, &role)) || role.vt != VT_I4) return FALSE;
    g_dbgFocusRole = role.lVal;   // 诊断：记录实际 role（见 g_dbgFocusVt 处说明）

    const LONG ROLE_SYSTEM_TEXT_LOCAL = 0x2A;
    const LONG ROLE_SYSTEM_SPINBUTTON_LOCAL = 0x34;
    if (role.lVal != ROLE_SYSTEM_TEXT_LOCAL && role.lVal != ROLE_SYSTEM_SPINBUTTON_LOCAL) return FALSE;

    if (SUCCEEDED(acc->get_accState(child, &state)) && state.vt == VT_I4) {
        const LONG STATE_SYSTEM_READONLY_LOCAL = 0x40;
        if ((state.lVal & STATE_SYSTEM_READONLY_LOCAL) != 0) return FALSE;
    }
    return TRUE;
}

// ===== UI Automation 焦点探测（Chrome / Electron 系专用）=====
//
// ⚠ 为什么需要它（2026-10-04 第七~九轮实测）：
//   MSAA 那套 `get_accFocus` + childId 在 Chrome / Electron 上走不通 ——
//   日志 746 次采样里 `frole=0x2A`（TEXT，白名单内）只有 23 行（3%），
//   `frole=0x0F`（GROUPING，容器）有 526 行。原因见 AccessibleHasEditableFocus
//   里的说明：焦点落在容器元素上，childId 语义又别扭，下钻穿不进去。
//
//   UIA 直接给答案：`GetFocusedElement()` 拿当前焦点元素，再看它的
//   `CurrentControlType` 是不是可编辑类（Edit / Document）。
//   —— **不碰 childId、不递归**，Chrome 的 a11y provider 原生支持。
//
// ⚠ 兼容性：UIA 需要 Vista+（本程序最低 XP）。XP 上 CoCreateInstance 会失败，
//   此时返回 FALSE，调用方自动退回类名兜底 / MSAA 路径 —— 行为与之前一致。
static IUIAutomation* g_uia = NULL;
static BOOL g_uiaTried = FALSE;

static BOOL EnsureUia() {
    if (g_uiaTried) return g_uia != NULL;
    g_uiaTried = TRUE;
    if (!EnsureAccessibilityCom()) return FALSE;   // COM 没初始化就免谈
    HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), NULL,
                                  CLSCTX_INPROC_SERVER, __uuidof(IUIAutomation),
                                  (void**)&g_uia);
    if (FAILED(hr)) g_uia = NULL;
    return g_uia != NULL;
}

// 焦点元素是否是可编辑控件
static BOOL UiaHasTextFocus() {
    if (!EnsureUia()) return FALSE;
    IUIAutomationElement* el = NULL;
    HRESULT hr = g_uia->GetFocusedElement(&el);
    g_dbgUiaHr = (int)hr;
    g_dbgUiaCt = -1; g_dbgUiaKf = -1; g_dbgUiaTp = -1;
    if (FAILED(hr) || !el) return FALSE;
    CONTROLTYPEID ct = 0;
    BOOL hasKf = FALSE;
    el->get_CurrentControlType(&ct);
    el->get_CurrentHasKeyboardFocus(&hasKf);
    g_dbgUiaCt = (int)ct;
    g_dbgUiaKf = hasKf ? 1 : 0;
    // `IsTextPatternAvailable` 是**能力（capability）**，UIA 里没有
    // `get_CurrentIsTextPatternAvailable` 方法，只能走 GetCurrentPropertyValue。
    //
    // ⚠⚠ 但这里**不能调 `VariantClear`** —— 它在 `oleaut32.lib`，而本项目的
    //   链接行（build_cpp.bat / build.yml）**没有 oleaut32**（只有 ole32）。
    //   arm64 job 因此报 `LNK2019: unresolved external __imp_VariantClear`。
    //   ⇒ 改用 `GetCurrentPropertyValue` 的**布尔版**重载
    //     `GetCurrentPropertyValueEx`？—— 那个也要 VARIANT。
    //   ⇒ 最稳的办法：**不查这个属性**。它只是诊断用的辅助信息，
    //     判定的关键只有 `ControlType` + `HasKeyboardFocus`（两者都是
    //     纯 getter，不需要 VARIANT、不需要额外 lib）。
    //     需要看这个值时，用 UIA 的 `CurrentIsTextPatternAvailable` 由
    //     **调用方**（有 lib 的地方）查，或直接看 ControlType 即可。
    g_dbgUiaTp = -2;   // -2 = 本程序不查（避免引入 oleaut32 依赖）
    el->Release();
    if (!hasKf) return FALSE;
    // ⚠⚠⚠ 2026-10-04 第十轮：**`Document`(50030) 不能当"可编辑"判据** ——
    //   第九轮把 `50004 || 50030` 都算作可编辑，实测造成「点空白不收」。
    //
    //   用户日志 786 次采样的交叉表把话说尽了：
    //
    //       uct      hasInput   noInput
    //       50030 Document   330        56     ← 点空白时照样是 Document！
    //       50026 Text       155        35     ← 同上
    //       50004 Edit       116         0     ← 唯一干净的信号
    //
    //   原因：浏览器/Chromium 里**整张网页永远有一个 Document 元素持有键盘
    //   焦点**（body/documentElement），哪怕焦点在空白处也一样。
    //   所以 `Document` 表达的是"焦点在这个窗口内"，不是"焦点在可编辑区"。
    //
    //   ⇒ 只有 **Edit(50004)** 才是真正的"可编辑"信号：
    //     用户日志里 116 次 hasInput、**0 次 noInput**，区分度 100%。
    //     传统 Win32 文本框、以及 Chromium 系真输入框（<input>/<textarea>）
    //     都报这个类型。
    if (ct == 50004) return TRUE;      // Edit —— 唯一可编辑信号
    return FALSE;
}

static BOOL AccessibleHasEditableFocus(IAccessible* acc, int depth, ULONG_PTR* token) {
    // ⚠ 深度上限 3 → 6（2026-10-04）：Chrome 系的 a11y 树很深
    //   （窗口 → 文档 → 容器 → 输入框…），3 层可能永远够不到焦点元素。
    //   放宽后配合 fvt/frole 诊断看实际能走到哪一层。
    if (!acc || depth > 6) return FALSE;
    VARIANT focus;
    ZeroMemory(&focus, sizeof(focus));
    if (FAILED(acc->get_accFocus(&focus))) { g_dbgFocusVt = -2; return FALSE; }
    g_dbgFocusVt = (int)focus.vt;   // 诊断：记录返回类型（见其定义处说明）

    VARIANT self;
    ZeroMemory(&self, sizeof(self));
    self.vt = VT_I4;
    self.lVal = 0; // CHILDID_SELF

    if (focus.vt == VT_I4) {
        if (AccessibleRoleIsEditable(acc, focus)) {
            if (token) *token = AccessibleIdentityToken(acc, focus) ^ ((ULONG_PTR)(DWORD)focus.lVal << 4);
            return TRUE;
        }
        // ⚠⚠⚠ 2026-10-04 第七轮：**这里以前是 `return FALSE;`** —— 根因就在这。
        //
        //   用户日志实测（Chrome 系应用，908 次采样）：
        //       frole = 0x0F = ROLE_SYSTEM_GROUPING   739 次（81%）
        //       frole = 0x29 = ROLE_SYSTEM_ANIMATION  168 次
        //       frole = 0x2A = ROLE_SYSTEM_TEXT（白名单内）  0 次
        //   也就是说 **Chrome 从不把焦点放在输入框本身，而是放在一个"容器"元素上**，
        //   真正的输入框是那个容器的**子节点**。
        //
        //   而 VT_I4 这条分支不匹配就返回 FALSE，把容器整个判死 ——
        //   于是「QQ / 浏览器 / Electron 的输入框在 a11y 树里永远认不出来」，
        //   `IsAccessibleInputWindow` 恒 FALSE，键盘只能靠类名粗判兜着，
        //   而类名粗判又分不出"点在输入框"还是"点在空白处"。
        //
        //   ⚠ 注意 `VT_DISPATCH` 分支一直是**会递归**的（`depth+1`），
        //     只有 VT_I4 分支漏了 —— 两条路径行为不一致，这次的 bug 正在于此。
        //
        //   ⇒ 修法：角色不匹配时，用 get_accChild 把这个 child 的 IAccessible
        //     取下来**继续往下钻**，与 VT_DISPATCH 分支保持一致。
        //     focus.lVal == 0 是 CHILDID_SELF（就是自身），无子可下钻。
        // ⚠ get_accChild 的原型（CI x86 编译器给出，oleacc.h:596）：
        //     HRESULT get_accChild(VARIANT varID, IDispatch **ppdispChild);
        //   **两参数、IDispatch** 是出参** —— 既不是三参数，也不是返回值
        //   （前两次分别报 C2660 / C2440，别再凭印象写）。
        //
        //   ⚠ 2026-10-04 第九轮实测：**MSAA 这条路在 Chrome 上走不通。**
        //     第七轮加的下钻在这两个条件下同时失效：
        //       · `get_accFocus` 返回 VT_I4 时，lVal 只是"焦点在本对象下的
        //         childId"，`get_accChild` 拿它当 varID 去问，语义并不保证
        //         返回焦点对象本身（MSAA 这套 childId/varID 的组合语义很别扭）
        //       · 实测 746 行日志里 `frole=0x2A`（TEXT）只有 23 行（3%），
        //         `frole=0x0F`（GROUPING）有 526 行 —— 下钻基本没穿透
        //     ⇒ Chrome / Electron 系**改用 UI Automation**（见 UiaHasTextFocus）：
        //       `IUIAutomation::GetFocusedElement()` 直接给焦点元素，
        //       再用 `CurrentControlType` / `CurrentIsKeyboardFocusable` 判断，
        //       完全绕开 MSAA 的 childId 语义。
        //     MSAA 保留给传统 Win32 控件（Edit / RichEdit / Scintilla 等），
        //     那条路是好的 —— 别把它的逻辑一起改坏。
        if (focus.lVal == 0) return FALSE;
        VARIANT varId;
        ZeroMemory(&varId, sizeof(varId));
        varId.vt = VT_I4;
        varId.lVal = focus.lVal;              // 焦点元素的 childId
        IDispatch* pdispChild = NULL;
        if (SUCCEEDED(acc->get_accChild(varId, &pdispChild)) && pdispChild) {
            IAccessible* childAcc = NULL;
            HRESULT hrChild = pdispChild->QueryInterface(IID_IAccessibleLocal,
                                                         (void**)&childAcc);
            pdispChild->Release();
            if (SUCCEEDED(hrChild) && childAcc) {
                BOOL deeper = AccessibleHasEditableFocus(childAcc, depth + 1, token);
                childAcc->Release();
                if (deeper) return TRUE;
            }
        }
        return FALSE;
    }

    if (focus.vt == VT_DISPATCH && focus.pdispVal) {
        IAccessible* focused = NULL;
        HRESULT hr = focus.pdispVal->QueryInterface(IID_IAccessibleLocal, (void**)&focused);
        focus.pdispVal->Release();
        if (FAILED(hr) || !focused) return FALSE;

        ULONG_PTR identity = AccessibleIdentityToken(focused, self);
        BOOL editable = AccessibleRoleIsEditable(focused, self);
        if (!editable) editable = AccessibleHasEditableFocus(focused, depth + 1, token);
        if (editable && token && *token == 0) *token = identity;
        focused->Release();
        return editable;
    }
    return FALSE;
}

// 目标窗口所属线程是否还在正常处理消息。
//
// ⚠⚠ 存在的理由（用户实测的「自动呼出卡死」）：
//   下面 IsAccessibleInputWindow 里的 AccessibleObjectFromWindow 是**跨进程
//   COM 调用**，目标进程一旦无响应（弹模态框、正在启动、死循环），它会阻塞
//   很久甚至不返回。而这个探测由 **50ms 的焦点轮询**驱动 ——
//
//     UI 线程僵死
//       → WM_TIMER 得不到处理（它在消息队列里优先级很低）
//       → 窗口滑动动画停在半路，g_mainMotion.active 一直是 TRUE
//       → UpdateAutoVisibility 开头那道「动画期间不评估」的保护**永久生效**
//       → 自动呼出彻底失效
//
//   症状完全吻合用户的描述：**整个程序完全无响应**、**有时恢复有时不恢复**
//   （目标恢复响应 / 永久挂着）、**切换窗口和长时间没用后最容易出现**。
//
//   SMTO_ABORTIFHUNG 会在目标 hung 时立刻返回，把「无限期阻塞」压成
//   「有界的最多 timeoutMs」。这一步挡在 COM 调用之前，是性价比最高的防线。
static BOOL IsWindowResponsive(HWND hw, UINT timeoutMs) {
    if (!hw || !IsWindow(hw)) return FALSE;
    DWORD_PTR res = 0;
    return SendMessageTimeoutW(hw, WM_NULL, 0, 0,
                               SMTO_ABORTIFHUNG | SMTO_BLOCK,
                               timeoutMs, &res) != 0;
}

static BOOL IsAccessibleInputWindow(HWND hWnd, ULONG_PTR* token) {
    g_dbgResp = 0; g_dbgAcc = -100; g_dbgAccFound = -1;
    // ⚠ 先确认目标还活着，再做跨进程 COM 探测（见 IsWindowResponsive 的说明）。
    //   100ms 是权衡值：正常进程的 WM_NULL 在 1ms 内返回，繁忙进程几十毫秒也够；
    //   真 hung 的会被 SMTO_ABORTIFHUNG 立刻打回。
    if (!IsWindowResponsive(hWnd, 100)) return FALSE;
    g_dbgResp = 1;

    AccessibleObjectFromWindowProc proc = GetAccessibleObjectFromWindow();
    if (!proc || !EnsureAccessibilityCom() || !hWnd) return FALSE;

#ifdef HK_DIAG
    LONGLONG probeT0 = QpcNowMs();
#endif
    IAccessible* root = NULL;
    HRESULT hr = proc(hWnd, OBJID_CLIENT, IID_IAccessibleLocal, (void**)&root);
    g_dbgAcc = (int)hr;
#ifdef HK_DIAG
    // 记一笔慢探测：即使做了响应性预检，COM 调用仍可能因为树很大而慢。
    // 排查「卡死」时先看 diag.txt 里有没有这行 —— 有就说明元凶在这。
    {
        LONGLONG dt = QpcNowMs() - probeT0;
        if (dt >= 150) {
            wchar_t buf[64] = {0};
            _snwprintf_s(buf, 64, _TRUNCATE, L"[slowprobe] %lld ms", dt);
            DiagSnap(buf);
        }
    }
#endif
    if (FAILED(hr) || !root) return FALSE;

    ULONG_PTR detected = 0;
    BOOL result = AccessibleHasEditableFocus(root, 0, &detected);
    g_dbgAccFound = result ? 1 : 0;
    root->Release();
    if (result && token) *token = detected ? detected : (ULONG_PTR)hWnd;
    return result;
}

// 系统外壳界面：任务栏、开始菜单、搜索、托盘折叠弹窗（^）等，
// 一律不视为输入区域，也跳过代价较高的跨进程 Accessibility 探测。
// 注意：CoreWindow 同时是 UWP 应用窗口类，不能排除，否则 UWP 输入框失效。
static BOOL IsShellSurfaceClass(const char* cls) {
    if (!cls) return FALSE;
    return strstr(cls, "Shell_") || strstr(cls, "Progman") || strstr(cls, "WorkerW") ||
           strstr(cls, "Taskbar") || strstr(cls, "TrayNotify") || strstr(cls, "MSTaskSwWClass") ||
           strstr(cls, "NotifyIconOverflowWindow") || strstr(cls, "XamlExplorerHost") ||
           strstr(cls, "ShellExperienceHost") ||
           strstr(cls, "SearchHost") || strstr(cls, "StartMenu") ||
           strstr(cls, "TopLevelWindowForOverflowXamlIsland");
}

static BOOL IsInputControl(HWND hw) {
    if (!hw || !IsWindow(hw)) return FALSE;
    char buf[128] = {0};
    GetClassNameA(hw, buf, 128);

    if (IsShellSurfaceClass(buf)) return FALSE;

    if (strstr(buf, "Edit") || strstr(buf, "Rich") || strstr(buf, "Scintilla") ||
        strstr(buf, "TextBox") || strstr(buf, "Console") || strstr(buf, "Omnibox") ||
        strstr(buf, "Search") || strstr(buf, "InputSite") || strstr(buf, "TXGuiFoundation"))
        return TRUE;

    // ⚠⚠⚠ 2026-10-04 第七轮：Chromium/Electron（NTQQ / Chrome / Edge / WorkBuddy）
    //   原先在这里**无条件** `return TRUE` —— 类名粗判。
    //
    //   后果（用户日志实证，QQ 段 406/406 = 100%）：不管焦点实际在输入框还是
    //   页面空白，都判"有输入焦点" ⇒ 「点空白自动隐藏」对 Chrome 系**天然失效**，
    //   只能靠 `AccessibleHasEditableFocus` 的 VT_I4 下钻修复来分辨。
    //
    //   ⇒ 现在改为：**先让 a11y 精判说话**（见 GetFocusedInputControl 里
    //     `g_chromeClassNoA11y` 的用法）；只有该进程**根本不提供 a11y**
    //     （COM 不可用）时才退回这条类名兜底，避免把兼容性也一起砍掉 ——
    //     2026-10-04 之前已经因为"砍降级路径"把浏览器/UWP/Electron 的
    //     自动呼出全废过一次（那次是砍 `if (!haveGuiInfo) return NULL`）。
    if (strstr(buf, "Chrome_RenderWidgetHostHWND") || strstr(buf, "Chrome_WidgetWin")) {
        extern BOOL g_chromeClassFallback;
        return g_chromeClassFallback;
    }

    return FALSE;
}

static BOOL IsOwnForegroundWindow(HWND fg) {
    if (!fg) return FALSE;
    if (fg == g_hWnd || fg == g_settingsHwnd || fg == g_closePromptHwnd) return TRUE;
    return (g_settingsHwnd && IsChild(g_settingsHwnd, fg)) ||
           (g_closePromptHwnd && IsChild(g_closePromptHwnd, fg));
}

// 只返回当前前台线程中真实获得输入焦点的控件。浏览器/Qt/Afx 顶层窗口不再
// 一概视为输入框，避免焦点离开文本区域后键盘仍持续显示。
static HWND GetFocusedInputControl() {
    g_detectedInputToken = 0;
    HWND fg = GetForegroundWindow();
    if (!fg || IsOwnForegroundWindow(fg)) return NULL;

    // 系统外壳界面（桌面/任务栏/开始菜单/托盘弹窗等）直接跳过，
    // 避免每 50ms 轮询都发起跨进程 COM 调用（动画卡顿的主要来源）
    {
        char fgClass[128] = {0};
        GetClassNameA(fg, fgClass, 128);
        if (IsShellSurfaceClass(fgClass)) return NULL;
    }

    DWORD tid = GetWindowThreadProcessId(fg, NULL);
    GUITHREADINFO gi = {sizeof(gi)};
    BOOL haveGuiInfo = GetGUIThreadInfo(tid, &gi);
    HWND focus = haveGuiInfo && gi.hwndFocus ? gi.hwndFocus : fg;

    // 诊断：记录这次判断的关键中间量（见 g_dbgFocusCls 的说明）
    {
        strncpy_s(g_dbgFocusCls, sizeof(g_dbgFocusCls), AfClsName(focus), _TRUNCATE);
        g_dbgHaveGui = haveGuiInfo ? 1 : 0;
        g_dbgIsInput = IsInputControl(focus) ? 1 : 0;
        g_dbgFlags   = haveGuiInfo ? (unsigned)gi.flags : 0;
        g_dbgCaret   = haveGuiInfo ? gi.hwndCaret : NULL;
    }
    if (haveGuiInfo) {
        // ⚠⚠⚠ 2026-10-04 第七轮：Chromium/Electron 系（NTQQ / Chrome / Edge /
        //   WorkBuddy）**不再走类名粗判**，改由 a11y 精判决定。
        //
        //   为什么必须改：类名判据只能回答"这个窗口是不是 Chromium 系"，
        //   回答不了"焦点此刻在输入框里还是页面上"。用户日志实证 QQ 段
        //   `hasInput` 406/406 = 100% —— 永远判"有输入焦点"，
        //   于是「点空白自动隐藏」对这类应用**永远不触发**。
        //
        //   为什么**不能**简单删掉：2026-10-04 之前砍降级路径（`if (!haveGuiInfo)
        //   return NULL`）曾把浏览器 / UWP / Electron 的自动呼出**全废**，
        //   用户实测「NTQQ 有时可以有时不行、浏览器/UWP/Electron 完全不弹」。
        //   ⇒ 保留类名作为**兜底**，但只在"a11y 通道确实不可用"时才用它。
        {
            char cbuf[128] = {0};
            GetClassNameA(focus, cbuf, 128);
            if (strstr(cbuf, "Chrome_RenderWidgetHostHWND") || strstr(cbuf, "Chrome_WidgetWin")) {
                // ⚠ 第九轮：**优先走 UIA**（`GetFocusedElement` 直接给焦点元素，
                //   不碰 MSAA 那套别扭的 childId）。UIA 不可用（XP / COM 失败）
                //   才退回 MSAA，再退回类名兜底。
                ULONG_PTR tok = 0;
                BOOL editable = UiaHasTextFocus();
                BOOL uiaOk = EnsureUia();
                if (!editable && !uiaOk) editable = IsAccessibleInputWindow(focus, &tok);
                // ⚠⚠⚠ 2026-10-04 第八轮：**上一版这里的判据是错的，直接造成回归**
                //   （用户实测「输入框反而不弹出了」）。
                //
                //   上一版写的是「`hr == S_OK` 就认为 a11y 通道可用 → 一律
                //   `return NULL`」。**错在把"拿到了根对象"当成"树里有可编辑元素"**：
                //   `AccessibleObjectFromWindow` 返回 S_OK 只说明 COM 通道打通了，
                //   完全不代表遍历能找到输入框。日志实测（746 次采样）：
                //       hr = 0x0 (S_OK)      725 行  ← 我据此判定"通道可用"
                //       frole = 0x2A (TEXT)    23 行  ← 真正走到白名单的只有 3%
                //   于是「焦点明明在输入框上」也被 `return NULL` 判成"点在空白"
                //   → 永不弹出。**"能问"不等于"问出了想要的答案"。**
                //
                //   ⇒ 修正后的判据（只认"证据"，不认"通道"）：
                //     · `editable == TRUE`              -> 找到可编辑元素，判有焦点
                //     · `hr != S_OK`（连根对象都拿不到）   -> 判定为"a11y 不可用"
                //     · `hr == S_OK` 但没找到            -> **不确定**，
                //       退回类名兜底（宁可多弹，不要不弹）
                // ⚠⚠⚠ 第十轮：判据是 **`uiaOk`**（UIA 能不能用），不是 `hrOk`。
                //   UIA 一旦创建成功（只有 XP 会失败），`GetFocusedElement`
                //   就能明确回答"焦点是不是 Edit"。此时答"不是"就是**可信的
                //   否定答案**（点在空白处），必须尊重 ——
                //   否则类名兜底又把它放回来，判据等于没生效
                //   （第八轮那次「输入框反而不弹」的回归就是这么来的）。
                if (uiaOk) {
                    g_chromeClassFallback = FALSE;      // UIA 说了算
                    if (editable) {
                        g_detectedInputToken = tok ? tok : (ULONG_PTR)focus;
                        return focus;
                    }
                    return NULL;                        // 明确不在 Edit 上
                }
                // UIA 不可用（XP / COM 失败）-> 退回 MSAA，最后才用类名兜底
                g_chromeClassFallback = TRUE;
                if (editable) {
                    g_detectedInputToken = tok ? tok : (ULONG_PTR)focus;
                    return focus;
                }
                if (hrOk) {
                    // S_OK 但没找到可编辑元素：a11y **没有给出否定答案**
                    // （Chrome 的树把焦点放在 GROUPING 容器上，本函数下钻能力
                    //   还不足以穿透到真正的输入框）。此时不能判"无输入焦点"。
                    //   → 退回类名兜底，保持旧行为（能弹），等下钻能力补齐
                    //     再收紧。**宁可多弹一次，不能一次都不弹。**
                    g_chromeClassFallback = TRUE;
                } else {
                    // 连根对象都拿不到（COM 不可用 / 目标无 a11y）
                    g_chromeClassFallback = TRUE;
                }
            }
        }
        if (IsInputControl(focus)) {
            g_detectedInputToken = (ULONG_PTR)focus;
            // 诊断（-afdiag）：类名快速路径补查一次 accessibility。
            // ⚠ 纯验证用，**不影响返回值**。仅在诊断模式 + 用户操作窗口内
            //   （g_dbgExtraDue）+ 距上次补查 ≥300ms 时执行。
            if (g_dbgExtraDue && g_afLogPath[0]) {
                DWORD nx = GetTickCount();
                if (nx - g_dbgExtraLastTick >= 300) {
                    g_dbgExtraLastTick = nx;
                    ULONG_PTR tok2 = 0;
                    BOOL ok2 = IsAccessibleInputWindow(focus, &tok2);
                    g_dbgExtraMs = (int)(GetTickCount() - nx);
                    g_dbgExtraOk = ok2 ? 1 : 0;
                }
            }
            return focus;
        }

        if (gi.hwndCaret || (gi.flags & GUI_CARETBLINKING) != 0) {
            if (gi.hwndCaret && IsWindow(gi.hwndCaret)) {
                g_detectedInputToken = (ULONG_PTR)gi.hwndCaret;
                return gi.hwndCaret;
            }
            g_detectedInputToken = (ULONG_PTR)focus;
            return focus;
        }
    }

    // ⚠⚠⚠ 下面这一段是**必需**的兜底路径，不要因为"怕误判"就把它砍掉。
    //
    //   拿不到 GUI 线程信息时，focus 会退化成"整个前台窗口"，随后对它做
    //   accessibility 探测 —— NTQQ / 浏览器 / UWP / Electron 的输入框**本来
    //   就不在 gi.hwndFocus 里**（焦点落在渲染宿主/组件窗口上），只能靠探测
    //   整个窗口才认得出。资源管理器的搜索栏也走这条。
    //
    //   ⚠ 2026-10-04 踩过：曾经在 `haveGuiInfo` 失败时直接 `return NULL`，
    //     结果把这些应用的自动呼出**全砍了** —— 用户实测「NTQQ 有时可以有时
    //     不行、浏览器 / UWP / Electron 无法正常呼出、资源管理器搜索栏也不行」。
    //     已经回退过了，**别再改回 return NULL**。
    //     误判问题改用「启动预热期」（AUTOSHOW_WARMUP_MS）解决，那条不影响这里。
    ULONG_PTR token = 0;
    if (IsAccessibleInputWindow(focus, &token) || (focus != fg && IsAccessibleInputWindow(fg, &token))) {
        g_detectedInputToken = token ? token : (ULONG_PTR)focus;
        return focus;
    }
    return NULL;
}

static void UpdateAutoVisibility() {
    if (!g_af || !g_hWnd) return;
    // 启动预热期内不评估（见 g_appStartTick 的说明）：此刻前台状态还没稳定，
    // 判断不可靠，容易在没输入框的地方误弹键盘并连带卡住状态。
    if (g_appStartTick && GetTickCount() - g_appStartTick < AUTOSHOW_WARMUP_MS) return;
    // 窗口滑动动画期间不做焦点评估：焦点探测可能触发跨进程 COM 调用，
    // 在启动动画中执行会造成可感知的卡顿；动画结束后下个轮询周期再评估。
    if (g_mainMotion.active) {
        // ⚠⚠ 兜底：动画靠 WM_TIMER（15ms）推进，而 WM_TIMER 在消息队列里
        //   优先级很低 —— 一旦 UI 线程被跨进程调用阻塞过久，定时器没能收尾，
        //   `g_mainMotion.active` 就会一直是 TRUE，上面那道 return 于是让
        //   **自动呼出永久失效**。这正是用户实测「有时候不会恢复」的路径。
        //   这里按时间戳补一次收尾，保证无论定时器发生什么都能自愈。
        //   余量给足（动画时长 + 1000ms），避免误判正常的慢帧。
        if (QpcNowMs() - g_mainMotion.started >
            (LONGLONG)g_mainMotion.duration + 1000) {
            HWND mh = g_mainMotion.hWnd;
            WindowMotionFinish f = g_mainMotion.finish;
            int mx = g_mainMotion.x, my = g_mainMotion.toY;
            g_mainMotion.active = FALSE;          // 先清标志，避免重入
            if (mh && IsWindow(mh)) {
                KillTimer(mh, TIMER_WINDOW_ANIM);
                SetWindowPos(mh, HWND_TOPMOST, mx, my, 0, 0,
                             SWP_NOSIZE | SWP_NOACTIVATE);
                if (f == MOTION_HIDE) ShowWindow(mh, SW_HIDE);
                else if (f == MOTION_DESTROY) DestroyWindow(mh);
            }
        }
        return;
    }

    // 前台窗口变了 -> 进入"等待用户操作"状态（见 g_fgAwaitUserInput 的说明）
    {
        HWND fgNow = GetForegroundWindow();
        if (fgNow != g_lastFg) { g_lastFg = fgNow; g_fgAwaitUserInput = TRUE; }
    }

    DWORD nowTick = GetTickCount();

    // ⚠⚠⚠ 全部判断都不再依赖 `input`（GetFocusedInputControl 的返回值
    //   滞后于焦点转移，用它做"点击落点"比较会误判 —— 用户实测
    //   「点了输入框反而被收掉 / 再点也不弹」就是这条）。
    //   改用「点击落点所属顶层窗口 == 当前前台窗口」这个稳定得多的判据。
    HWND fgNow = GetForegroundWindow();
    HWND fgTop = fgNow ? GetAncestor(fgNow, GA_ROOT) : NULL;

    // 前台窗口变了 -> 进入"等待用户操作"状态
    if (fgNow != g_lastFg) { g_lastFg = fgNow; g_fgAwaitUserInput = TRUE; }

    BOOL recentClick = (g_lastClickTick && nowTick - g_lastClickTick <= AUTOSHOW_INPUT_WINDOW_MS);
    BOOL byKey       = (g_lastKeyTick && nowTick - g_lastKeyTick <= AUTOSHOW_INPUT_WINDOW_MS);
    // 点击落点是否在当前前台窗口内（点在任务栏 / 桌面 / 别的应用上都不算）
    BOOL clickInFg   = (recentClick && g_lastClickTopHwnd && fgTop &&
                        g_lastClickTopHwnd == fgTop);

    // 用户主动操作（点在前台窗口内 / 敲键盘）-> 解除"切换窗口"的封锁
    if (byKey || clickInFg) g_fgAwaitUserInput = FALSE;

    // 诊断：本 tick 默认无补查；用户操作窗口内才安排（须在 GetFocusedInputControl
    // 之前清/置，否则会把补查刚写入的结果清掉）
    g_dbgExtraMs = -1;
    g_dbgExtraOk = -1;
    g_dbgExtraDue = (g_afLogPath[0] && (recentClick || byKey)) ? TRUE : FALSE;
    HWND input = GetFocusedInputControl();
    // 诊断（-afdiag）：记录这次评估的全部输入量
    {
        g_dbgVisWnd = (g_hWnd && IsWindowVisible(g_hWnd)) ? 1 : 0;
        RECT wr = {0, 0, 0, 0};
        if (g_hWnd) GetWindowRect(g_hWnd, &wr);
        g_dbgRc[0] = (int)wr.left;  g_dbgRc[1] = (int)wr.top;
        g_dbgRc[2] = (int)(wr.right - wr.left);
        g_dbgRc[3] = (int)(wr.bottom - wr.top);
    }
    AfLog("eval", fgNow, fgTop, input, recentClick, clickInFg, byKey,
          g_fgAwaitUserInput, g_vis, g_mainMotion.active,
          input ? "hasInput" : "noInput");
    if (input) {
        g_lastNonInput = 0;
        g_noInputStreak = 0;
        g_noInputSinceTick = 0;   // 输入焦点恢复，失焦计时清零（见失焦分支防抖说明）
        g_manualShow = FALSE;

        // 防回弹：用户刚在这个输入框里手动收起过，别立刻弹回来。
        // ⚠ token 对不上也先按住，直到超时才放行（原来无条件清标记，
        //   只要有一次对不上防回弹就永久失效 —— 用户看到的"莫名其妙回弹"）。
        // ⚠ 2026-10-04：**删掉"8 秒超时自动弹回"** —— 用户实测「手动收起后
        //   过几秒键盘自己弹回来」，日志里三次收起后 3.7 / 7.1 / 8.0 秒全部
        //   弹回，与 8 秒超时吻合。用户的心智模型是「收起后不自动出现，除非我
        //   再点输入框 / 按键」。清除路径保留三条：
        //     · 用户按键（byKey —— 明确要用键盘）
        //     · token 变化（焦点换到别的输入控件 → 用户明显换了目标）
        //     · 失焦分支的"真的失焦"（见那里的防抖说明）
        if (g_userHidInInput) {
            BOOL sameInput = (g_hiddenInputToken && g_detectedInputToken == g_hiddenInputToken);
            if (sameInput && !byKey) return;
            g_userHidInInput = FALSE;
        }

        // 自动隐藏（2026-10-04 收窄，修"键盘自激振荡"）：
        //   ⚠ 原来接受"点击落点 != 前台窗口"作为"点到别处"的依据，但那个条件
        //     在真实场景里误报率极高 —— 用户日志实测 click=0x411504 vs
        //     fgTop=0xA163C 持续不一致（应用多窗口 / NOACTIVATE 窗口 / 激活
        //     延迟都会造成），于是每一拍评估都触发"隐藏"、下一拍"自动呼出"
        //     又满足条件 —— 形成周期恰好等于动画时长（220ms / 150ms）的
        //     **自激振荡**（用户看到的"键盘乱闪"）。
        //   ⇒ 只在落点**确实是系统外壳表面**（桌面 / 任务栏 / 托盘，由
        //     IsShellSurfaceClass 识别）时才立即收起。这是唯一能**确定**用户
        //     在"别处"的信号。应用内的"点空白收起"由失焦逻辑与后续判据修复承担。
        if (g_afAutoHide && g_vis && recentClick && g_lastClickTopHwnd) {
            char hitCls[128] = {0};
            GetClassNameA(g_lastClickTopHwnd, hitCls, 128);
            if (IsShellSurfaceClass(hitCls)) {
                ShowKB(FALSE, FALSE);
                return;
            }
        }

        // 仍处于"切换窗口后还没操作过"的状态 -> 不弹
        // （网页自动聚焦搜索框、同窗口内 Tab 跳转不受影响：那些情况下
        //   前台窗口没变过，g_fgAwaitUserInput 一直是 FALSE）
        if (g_fgAwaitUserInput) return;
        if (!g_manualHide && !g_vis) ShowKB(TRUE, FALSE);
        return;
    }

    HWND fg = GetForegroundWindow();
    if (fg == g_settingsHwnd || fg == g_closePromptHwnd) return;

    // ⚠ 连续确认：焦点探测会间歇性返回 NULL（hwndCaret 是瞬态量），
    //   只凭一次就拿去隐藏会让键盘闪烁 —— 用户看到的"莫名其妙回弹"。
    //   连续 4 次（约 200ms，叠加下面的 hideDelay 已足够）都没焦点才继续。
    g_noInputStreak++;
    if (g_noInputStreak < 4) return;
    if (g_noInputSinceTick == 0) g_noInputSinceTick = nowTick;

    // ⚠⚠ 防抖（2026-10-04 用户日志实测，修的是「手动收起后过几秒键盘自己弹回来」）：
    //   焦点探测会**爆发式**失败 —— 日志里出现过 80ms 内连续 6 条 noInput 的采样，
    //   根因是 `GetForegroundWindow` 瞬时返回 NULL。只按"连续次数"判定时，
    //   这种抖动会穿过上面那道门槛，把 g_userHidInInput（手动收起标记）清掉，
    //   防回弹于是永久失效 —— 下一次轮到 input 有效就直接弹回来了。
    //   ⇒ 改为按**失焦持续时间**判定：只有失焦真的持续了
    //     AUTOSHOW_INPUT_GRACE_MS 以上，才认为焦点确实离开了。
    //   ⚠ 这里**只保护这一句**：下面的隐藏流程保持原有节奏（80ms 级的爆发抖动
    //     本来就穿不过 300ms 的 hideDelay），避免把隐藏时机整体拖慢。
    if (nowTick - g_noInputSinceTick >= AUTOSHOW_INPUT_GRACE_MS)
        g_userHidInInput = FALSE;
    if (g_lastNonInput == 0) g_lastNonInput = GetTickCount();
    if (g_lastNonInput == 0) g_lastNonInput = GetTickCount();
    if (!g_vis) {
        // 隐藏滑动被打断（如拖动标题栏）后窗口可能仍残留可见：直接收尾藏到任务栏底部
        if (!(g_mainMotion.active && g_mainMotion.finish == MOTION_HIDE) &&
            IsWindowVisible(g_hWnd))
            ShowWindow(g_hWnd, SW_HIDE);
        return;
    }
    if (g_manualHide) return;
    if (GetTickCount() - g_lastNonInput < (DWORD)g_hideDelayMs) return;
    if (g_manualShow) return;

    ShowKB(FALSE, FALSE);
}

static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    (void)hook; (void)idObject;
    (void)dwEventThread; (void)dwmsEventTime;
    if (!g_af || !g_hWnd) return;
    if (event == EVENT_OBJECT_FOCUS) {
        g_lastFocusEvTick = GetTickCount();
        g_lastFocusEvHwnd = hwnd;
    }
    if (event == EVENT_OBJECT_FOCUS || event == EVENT_SYSTEM_FOREGROUND ||
        (event == EVENT_OBJECT_SHOW && idObject == OBJID_CARET))
        PostMessage(g_hWnd, WM_FOCUS_EVENT, 0, 0);
    (void)idChild;
}

// ===== 全局鼠标监控（WH_MOUSE_LL） =====
// 只做一件事：记录"用户刚在哪里点过鼠标"，供自动呼出判断某次焦点变化是不是
// 用户主动点出来的。**不改变任何鼠标行为**，永远 CallNextHookEx 放行。
// 只记左键 —— 右键/中键不用于聚焦输入框。
static LRESULT CALLBACK PhysMouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        const MSLLHOOKSTRUCT* m = (const MSLLHOOKSTRUCT*)lParam;
        if (!(m->flags & LLMHF_INJECTED) && wParam == WM_LBUTTONDOWN) {
            g_lastClickTick = GetTickCount();
            g_lastClickPt = m->pt;
            // 当场记下落点的顶层窗口（供 UpdateAutoVisibility 判断"点的是不是当前前台窗口"）
            HWND h = WindowFromPoint(m->pt);
            g_lastClickTopHwnd = h ? GetAncestor(h, GA_ROOT) : NULL;
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// ===== 实体键盘状态监控（WH_KEYBOARD_LL） =====
// 同步显示：实体 Win/Shift/Caps 键做到哪一步，程序显示就对应哪一步；
// Fn 预留接口（多数键盘 Fn 不产生按键事件，后续按需扩展 g_physFn）。
static LRESULT CALLBACK PhysKeyHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        const KBDLLHOOKSTRUCT* p = (const KBDLLHOOKSTRUCT*)lParam;
        // 忽略本程序 SendInput 注入的事件，避免与虚拟键逻辑互相干扰
        if (!(p->flags & LLKHF_INJECTED)) {
            // 真实按键 = 用户主动操作，记一笔供自动呼出判断（见 g_lastKeyTick）
            g_lastKeyTick = GetTickCount();
            BOOL down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            BOOL up   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);
            if (down || up) {
                BOOL changed = FALSE;
                switch (p->vkCode) {
                case VK_LSHIFT: case VK_RSHIFT:
                    if (g_physShift != down) { g_physShift = down; changed = TRUE; }
                    break;
                case VK_LWIN: case VK_RWIN:
                    if (g_physWin != down) { g_physWin = down; changed = TRUE; }
                    if (down) {
                        // 实体 Win 键已由系统弹出/关闭开始菜单，重置虚拟计数避免失步
                        if (g_winCount != 0 || g_winKey) { g_winCount = 0; g_winKey = FALSE; changed = TRUE; }
                    }
                    break;
                case VK_CAPITAL:
                    if (down) { g_cp = !g_cp; changed = TRUE; }
                    break;
                // NumLock 与 Caps 同为锁存键：keydown 时系统还没翻状态（钩子在系统处理之前），
                // 所以先按「取反」记一笔，真实值由 WM_TIMER 的 GetAsyncKeyState 自校正兜住。
                case VK_NUMLOCK:
                    if (down) { g_physNum = !g_physNum; changed = TRUE; }
                    break;
                // 预留接口：Fn 等其它实体键状态后续在此扩展（g_physFn）
                default:
                    break;
                }
                if (changed && g_hWnd && IsWindow(g_hWnd)) {
                    InvalidateRect(g_hWnd, NULL, TRUE);
                }
            }
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

static void OnLDown(HWND hWnd, int x, int y) {
    int hh = HitHeader(x, y);
    if (hh >= 0) {
        switch (hh) {
        case HDR_DOCK: OpenSettings(); break;
        // 最小化 = 临时收起，自动呼出仍然有效
        case HDR_MIN: UserHideKeyboard(); break;
        // ⏑ = 永久隐藏到托盘，只有单击托盘图标才回来
        case HDR_HIDE: HideToTray(); break;
        case HDR_CLOSE: HandleCloseAction(hWnd); break;
        case HDR_NUM:   // 「小键盘」按钮：语义统一为「小键盘开着吗」
            if (g_layoutMode == 2) {
                // 全尺寸布局：切换右侧数字区。
                // ⚠ 取反的必须是**生效状态** NumpadHidden()，不是手动标志 g_npHidden ——
                //   按钮显示（NumBtnActive → !NumpadHidden()）用的是生效状态，
                //   点击却翻转 g_npHidden，两者在「窄屏自动收起」时会分叉：
                //   窗口 700 DIP（自动收起中、g_npHidden=FALSE）时点一下算的是「再隐藏一次」，
                //   界面毫无变化，用户看到的就是「按钮点了不更新状态」。
                SetFullNumpadHidden(hWnd, !NumpadHidden());
            } else if (g_layoutMode == 1) {
                g_layoutMode = g_prevLayout;   // 独立小键盘 → 切回上次用的布局
                ApplyKeyboardLayout(TRUE);
            } else {
                g_prevLayout = g_layoutMode;   // 默认布局 → 切到独立小键盘
                g_layoutMode = 1;
                ApplyKeyboardLayout(TRUE);
            }
            break;
        }
        return;
    }

    int ki = HitKey(x, y);
    if (ki < 0) return;
    g_pk = ki;
    // ⚠ 正式版里这两句恒为真（`#ifdef` 整块不编译），保持原行为不变；
    //   诊断版可以用 -noscapture / -norepaint 临时跳过它们做二分。
#ifdef HK_DIAG
    if (!g_noSetCapture)
#endif
    SetCapture(hWnd);   // 捕获鼠标，防止开始菜单等出现时抢走鼠标抬起消息导致键一直高亮
    const KeyDef* k = &g_keys[ki];
    DoKeyAction(k);

    if (k->vk == 0x08 || k->vk == 0x2E || k->vk == 0x20 || k->type == K_ARROW) {
        g_repeatKeyIdx = ki;
        SetTimer(hWnd, TIMER_REPEAT, 350, NULL);
    }
#ifdef HK_DIAG
    if (!g_noClickRepaint)
#endif
    InvalidateRect(hWnd, 0, TRUE);
}

static void OnLUp(HWND hWnd, int x, int y) {
    (void)x;
    (void)y;
    KillTimer(hWnd, TIMER_REPEAT);
    g_repeatKeyIdx = -1;
    if (GetCapture() == hWnd) ReleaseCapture();

    if (g_pk >= 0) {
        g_pk = -1;
        InvalidateRect(hWnd, 0, TRUE);
    }
}

static void OnMMove(HWND hWnd, int x, int y) {
    int hh = HitHeader(x, y);
    if (hh != g_hdrHov) {
        g_hdrHov = hh;
        InvalidateRect(hWnd, 0, TRUE);
    }
    int nk = HitKey(x, y);
    if (nk != g_hk) {
        g_hk = nk;
        InvalidateRect(hWnd, 0, TRUE);
    }
    if (!g_tracking) {
        TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hWnd, 0};
        TrackMouseEvent(&tme);
        g_tracking = TRUE;
    }
}
static BOOL IsTouchDevice() {
    int maxTouches = GetSystemMetrics(95);
    if (maxTouches > 0) return TRUE;
    int digitizer = GetSystemMetrics(94);
    if ((digitizer & 0x80) != 0) return TRUE;
    return FALSE;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    // 任务栏重建（explorer 重启 / 托盘图标被系统折叠清理）后恢复托盘图标
    if (g_taskbarCreatedMsg && msg == g_taskbarCreatedMsg) {
        if (g_tray) {
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            g_tray = FALSE;
        }
        AddTray();
        return 0;
    }
    switch (msg) {
    case WM_CREATE: {
        g_hWnd = hWnd;
        RecreateFontsAndLayout();
        SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST);
        ApplyRoundedWindow(hWnd, 10);
        ApplyWindowOpacity(hWnd, g_mainOpacity < 100);   // 分层窗口整体透明（所有系统可用）
        EnsureKbFrameCache(hWnd);   // 窗口可见前预热渲染首帧，消除启动黑帧与首帧卡顿
        g_taskbarCreatedMsg = RegisterWindowMessageW(L"TaskbarCreated");
        g_winHook = SetWinEventHook(EVENT_OBJECT_SHOW, EVENT_OBJECT_FOCUS, 0, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
        g_fgHook = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, 0, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
        // 实体键盘状态监控：安装低级键盘钩子（只监控 Win/Shift/Caps，Fn 预留接口）
        g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, PhysKeyHookProc, g_hInst, 0);
        // 全局鼠标钩子：只记录点击落点，供自动呼出判断"这次是不是用户点出来的"
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, PhysMouseHookProc, g_hInst, 0);
        {
            char buf[128];
            _snprintf_s(buf, 128, _TRUNCATE,
                        "hooks: mouse=%p kb=%p (NULL means FAILED)",
                        (void*)g_mouseHook, (void*)g_kbHook);
            AfNote(buf);
        }
        g_cp = (GetKeyState(VK_CAPITAL) & 1) != 0;  // 启动时同步 CapsLock 状态
        SetTimer(hWnd, TIMER_FOCUS, 50, 0);
        return 0;
    }
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_GETMINMAXINFO: {
        LPMINMAXINFO mmi = (LPMINMAXINFO)l;
        // 下限按「1u 键不低于触摸安全线、标签不被挤到截断」反解（三种布局各一套）：
        //   全尺寸  870 × 300 —— 键帽 44px 的触摸安全线，数字区收起后的宽度
        //   默认    480 × 240 —— 13 键一行、每键 ≥30 DIP；5 行 + 页头 ≈ 240
        //   小键盘  330 × 260 —— 4 列数字键
        // 上限不设：交给系统（= 屏幕工作区）。硬编码上限在竖屏 / 多屏下会变成「能拖但拖不出去」。
        // 注意「退格标签放不下」不是靠这里兜的 —— 标签判据本身就是实测文字宽（见 KeyText）。
        double dpiScale = GetSystemDpiScale();
        int minW, minH;
        if (g_layoutMode == 2)      { minW = 870; minH = 300; }
        else if (g_layoutMode == 1) { minW = 330; minH = 260; }
        else                        { minW = 480; minH = 240; }
        mmi->ptMinTrackSize.x = (int)(minW * dpiScale);
        mmi->ptMinTrackSize.y = (int)(minH * dpiScale);
        return 0;
    }
    case WM_DPICHANGED: {
        RECT* prcNew = (RECT*)l;
        SetWindowPos(hWnd, HWND_TOPMOST,
            prcNew->left, prcNew->top,
            prcNew->right - prcNew->left,
            prcNew->bottom - prcNew->top,
            SWP_NOACTIVATE);
        RecreateFontsAndLayout();
        InvalidateRect(hWnd, NULL, TRUE);
        return 0;
    }
    case WM_SIZE: {
        g_ww = LOWORD(l);
        g_wh = HIWORD(l);
        if (g_ww > 0 && g_wh > 0) {
            RecreateFontsAndLayout();
            ApplyRoundedWindow(hWnd, 10);
            InvalidateRect(hWnd, NULL, TRUE);
        }
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        StopWindowMotion(&g_mainMotion);
        return 0;
    case WM_EXITSIZEMOVE:
        SaveWindowState();   // 按当前布局记忆窗口大小与位置
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hWnd, &ps);
        RECT rc = {0, 0, g_ww, g_wh};
        EnsureKbFrameCache(hWnd);
        BOOL useKbCache = g_kbCacheBmp && g_kbCacheDc && g_kbCacheBits &&
                          g_kbCacheW == g_ww && g_kbCacheH == g_wh;
        if (useKbCache) {
            BitBlt(dc, 0, 0, g_ww, g_wh, g_kbCacheDc, 0, 0, SRCCOPY);
        } else {
            // 缓存创建失败时回退到逐帧全量绘制
            WindowPaintSurfaceLocal surface = BeginWindowPaintSurface(dc, hWnd, rc);
            ClearWindowBackBuffer(surface.dc, hWnd, g_ww, g_wh);
            DrawHeader(surface.dc);
            DrawKeys(surface.dc);
            BitBlt(dc, 0, 0, g_ww, g_wh, surface.dc, 0, 0, SRCCOPY);
            EndWindowPaintSurface(&surface);
        }
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_NCHITTEST: {
        POINT pt = { GET_X_LPARAM(l), GET_Y_LPARAM(l) };
        ScreenToClient(hWnd, &pt);
        int b = (int)(8 * GetSystemDpiScale());

        if (pt.x < b && pt.y < b) return HTTOPLEFT;
        if (pt.x >= g_ww - b && pt.y < b) return HTTOPRIGHT;
        if (pt.x < b && pt.y >= g_wh - b) return HTBOTTOMLEFT;
        if (pt.x >= g_ww - b && pt.y >= g_wh - b) return HTBOTTOMRIGHT;
        if (pt.x < b) return HTLEFT;
        if (pt.x >= g_ww - b) return HTRIGHT;
        if (pt.y < b) return HTTOP;
        if (pt.y >= g_wh - b) return HTBOTTOM;

        if (pt.y >= 0 && pt.y < g_headerH) {
            if (HitHeader(pt.x, pt.y) < 0) return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK: OnLDown(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    case WM_LBUTTONUP: OnLUp(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    case WM_CAPTURECHANGED:
        // 鼠标捕获被夺走（如开始菜单弹出）时清除按下状态，避免键一直高亮
        g_pk = -1;
        g_hdrHov = -1;
        g_tracking = FALSE;
        KillTimer(hWnd, TIMER_REPEAT);
        g_repeatKeyIdx = -1;
        InvalidateRect(hWnd, 0, TRUE);
        return 0;
    case WM_MOUSEMOVE: OnMMove(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    case WM_MOUSELEAVE:
        g_tracking = FALSE;
        if (g_hdrHov != -1 || g_hk != -1) {
            g_hdrHov = -1;
            g_hk = -1;
            InvalidateRect(hWnd, 0, TRUE);
        }
        return 0;
    case WM_FOCUS_EVENT:
        UpdateAutoVisibility();
        return 0;
    case WM_SHOW_KEYBOARD:
        // 诊断：这个分支来自单实例消息转发 —— 每次有第二个 HKeyboard 启动，
        // 就会往已有实例发这条消息（w = 新实例是否要求隐藏）。
        g_dbgShowFrom = "WM_SHOW_KEYBOARD";
        if (w) {
            ApplyTheme();
            ShowKB(TRUE, TRUE);
        } else {
            ShowKB(FALSE, TRUE);
        }
        return 0;
    case WM_SETTINGCHANGE: {
        // 跟随系统主题自动切换（themeMode==0 或开启壁纸强调色时生效）；
        // 壁纸更换时也立即刷新强调色（SPI_SETDESKWALLPAPER）
        if (g_themeMode == 0 || g_wallpaperAccent) {
            if (w == SPI_SETDESKWALLPAPER) {
                if (g_wallpaperAccent) RefreshThemeAndRepaint(hWnd);
            } else if (l != 0) {
                const wchar_t* section = (const wchar_t*)l;
                if (wcscmp(section, L"ImmersiveColorSet") == 0) {
                    RefreshThemeAndRepaint(hWnd);
                }
            }
        }
        return 0;
    }
    case WM_DWMCOLORIZATIONCOLORCHANGED: {
        // 系统壁纸强调色变化时刷新高亮按钮颜色（仅 -wallpaper 开启时生效）
        if (g_wallpaperAccent) {
            RefreshThemeAndRepaint(hWnd);
        }
        return 0;
    }
    case WM_TIMER:
        if (w == TIMER_WINDOW_ANIM) {
            TickWindowMotion(&g_mainMotion, hWnd);
            return 0;
#ifdef HK_DIAG
        } else if (w == TIMER_ENVTEST) {
            // -envtest：两阶段 —— 先注入，再退出。说明见 EnvTestInject。
            if (g_envTestStage == 0) {
                g_envTestStage = 1;
                SetTimer(hWnd, TIMER_ENVTEST, 3000, NULL);
                EnvTestInject();
            } else {
                KillTimer(hWnd, TIMER_ENVTEST);
                DestroyWindow(hWnd);
            }
            return 0;
#endif  // HK_DIAG
        } else if (w == TIMER_REPEAT) {
            SetTimer(hWnd, TIMER_REPEAT, 40, NULL);
            if (g_pk >= 0 && g_pk == g_repeatKeyIdx) {
                const KeyDef* k = &g_keys[g_pk];
                // ⚠ 标记为"连发中"，让 WaitForLeftButtonUp 直接返回 ——
                //   连发时用户一直按着鼠标，等下去每次都要耗满 250ms 超时。
                g_inRepeat = TRUE;
                DoKeyAction(k);
                g_inRepeat = FALSE;
            } else {
                KillTimer(hWnd, TIMER_REPEAT);
            }
        } else if (w == TIMER_FOCUS) {
            // 诊断：定时器心跳（每 100 次 = 约 5 秒记一次，确认轮询确实在跑）
            {
                static int beat = 0;
                if (++beat >= 100) { beat = 0; AfNote("timer alive (TIMER_FOCUS)"); }
            }
            // Win 锁定/高亮状态与开始菜单状态同步：
            // 开始菜单（无论由本键盘还是任务栏打开）一旦显示，即清除 Win 锁定，避免高亮残留。
            if (g_winKey && IsStartMenuOpen()) {
                ClearWinLock();
                InvalidateRect(hWnd, 0, TRUE);
            }

            // Win 状态超时自动复位：锁定/开始菜单已开未操作 8 秒即回到空闲，
            // 防止一直高亮，以及残留状态导致“第一次点击就弹开始菜单”。
            if (g_winCount != 0 && (GetTickCount() - g_lastWinTick) > 8000) {
                ClearWinLock();
                InvalidateRect(hWnd, 0, TRUE);
            }

            // 实体键状态自校正：钩子偶尔漏掉 keyup/keydown 时，按物理键实际状态修正显示，避免高亮残留
            {
                BOOL pShift = ((GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0) ||
                              ((GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0);
                BOOL pWin   = ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0) ||
                              ((GetAsyncKeyState(VK_RWIN) & 0x8000) != 0);
                // NumLock / CapsLock 是锁存键：GetKeyState 的 bit0 就是「锁定态」本身（与
                // K_CAPS 分支读法一致），且不受前台窗口提权影响，比 GetAsyncKeyState 稳。
                BOOL pNum   = (GetKeyState(VK_NUMLOCK) & 1) != 0;
                BOOL pCaps  = (GetKeyState(VK_CAPITAL) & 1) != 0;
                if (g_physShift != pShift || g_physWin != pWin ||
                    g_physNum != pNum || g_cp != pCaps) {
                    g_physShift = pShift;
                    g_physWin = pWin;
                    g_physNum = pNum;
                    g_cp = pCaps;
                    InvalidateRect(hWnd, 0, TRUE);
                }
            }

            UpdateAutoVisibility();
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_MENU_TOGGLE: g_dbgShowFrom = "tray-menu"; ToggleKB(); break;
        case ID_MENU_AUTO:
            g_af = !g_af;
            IniSetInt(L"General", L"AutoPopup", g_af ? 1 : 0);
            if (g_af) UpdateAutoVisibility();
            InvalidateRect(hWnd, 0, TRUE);
            break;
        case ID_MENU_SETTINGS: OpenSettings(); break;
        case ID_MENU_ABOUT: OpenSettingsTab(2); break;
        case ID_MENU_EXIT: ExitApplicationAnimated(); break;
        }
        return 0;
    case WM_TRAY:
        if (l == WM_LBUTTONUP || l == WM_LBUTTONDBLCLK) {
            g_dbgShowFrom = "tray-click"; ToggleKB();
        } else if (l == WM_RBUTTONUP) {
            ShowMenu(hWnd);
        }
        return 0;
    case WM_CLOSE: HandleCloseAction(hWnd); return 0;
    case WM_DESTROY:
        StopWindowMotion(&g_mainMotion);
        KillTimer(hWnd, TIMER_FOCUS);
        KillTimer(hWnd, TIMER_REPEAT);
        ReleaseKbFrameCache();
        if (g_winHook) { UnhookWinEvent(g_winHook); g_winHook = 0; }
        if (g_fgHook) { UnhookWinEvent(g_fgHook); g_fgHook = 0; }
        if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = 0; }
        if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = 0; }
        if (g_tray) {   // 显式删除托盘图标，避免程序退出后图标残留到鼠标悬停才消失
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            g_tray = FALSE;
        }
        DeleteObject(g_f12); DeleteObject(g_f13); DeleteObject(g_f14);
        DeleteObject(g_f10); DeleteObject(g_f9);
        DeleteObject(g_f8); DeleteObject(g_f7); DeleteObject(g_f6);
        DeleteObject(g_sfBig); DeleteObject(g_sfRow); DeleteObject(g_sfCtrl);
        DeleteObject(g_sfBase); DeleteObject(g_sfMeta);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, w, l);
}

// 命令行参数整词匹配：避免 "-h" 误匹配 "-hide"/"-help"
static BOOL HasArg(const char* cmd, const char* arg) {
    if (!cmd || !arg) return FALSE;
    size_t alen = strlen(arg);
    if (alen == 0) return FALSE;
    const char* p = cmd;
    while ((p = strstr(p, arg)) != NULL) {
        BOOL leftOk = (p == cmd) || (p[-1] == ' ' || p[-1] == '\t');
        char after = p[alen];
        BOOL rightOk = (after == 0 || after == ' ' || after == '\t');
        if (leftOk && rightOk) return TRUE;
        p += alen;
    }
    return FALSE;
}

#ifdef HK_DIAG
// ========== 注入方式对照实验（-sendtest）==========
//
// ⚠⚠⚠ 为什么需要这个入口（2026-10 加）：
//
//   issue #3（Win10 + 微软拼音打不出中文）排查了多轮都没解决，
//   根本困难在于**无法区分下面三种可能**：
//
//     (a) 注入方式本身与微软拼音不兼容
//     (b) HKeyboard 的其它部分（键盘钩子 / 定时器 / 焦点轮询）
//         干扰了输入
//     (c) 只是节奏或某个参数不对
//
//   用户每轮只能"实机试一次"，一轮几十分钟，而且结果无法归因 ——
//   改了三次都没效果，就是因为一直分不清是哪个原因。
//
//   这个入口把变量降到**最低**：不装钩子、不建窗口、不建互斥体、
//   不轮询焦点、不读剪贴板 —— 只有裸的注入调用。
//   然后用 4 种方式各打一次 nihao，看哪一种能让微软拼音组字上屏。
//
//   一次实验就能定方向：
//     · 某一种成功  ⇒ 照那种方式改 SendKey，问题解决
//     · 四种全失败  ⇒ SendInput 这条路走不通，得换注入途径
//                      （低级键盘钩子 / TSF API / 剪贴板粘贴）
//     · 四种全成功  ⇒ 注入没问题，是 HKeyboard 的其它部分在干扰，
//                      回头查钩子与定时器
//
// 用法：
//   1. 打开记事本，把光标点进**空白**编辑区
//   2. 确认输入法是微软拼音**中文态**
//   3. 运行  HKeyboard_x64.exe -sendtest
//   4. 3 秒内切回记事本（程序会等这 3 秒）
//   5. 看记事本里出现了什么
//
// 结果判读（记事本里从上到下依次是 4 种方式的结果，用空行分开）：
//   "你好"   = 该方式**有效**
//   "nihao"  = 该方式下 IME 没组字
//   该段空白 = 按键根本没送达
//
// 同目录会生成 sendtest_readme.txt（含构建戳与判读说明）。
static void SendTestKey(BYTE vk, int method) {
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);

    if (method == 0) {
        // 方式1：VK 两段（HKeyboard 最早的做法，不带 SCANCODES）
        INPUT a = {};
        a.type = INPUT_KEYBOARD;
        a.ki.wVk = vk;
        a.ki.wScan = (WORD)sc;
        SendInput(1, &a, sizeof(INPUT));
        Sleep(1);
        a.ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(1, &a, sizeof(INPUT));
    } else if (method == 1) {
        // 方式2：VK + KEYEVENTF_SCANCODE 两段（HKeyboard 当前的做法）
        INPUT a = {};
        a.type = INPUT_KEYBOARD;
        a.ki.wVk = vk;
        a.ki.wScan = (WORD)sc;
        a.ki.dwFlags = KEYEVENTF_SCANCODE;
        SendInput(1, &a, sizeof(INPUT));
        Sleep(1);
        a.ki.dwFlags = KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        SendInput(1, &a, sizeof(INPUT));
    } else if (method == 2) {
        // 方式3：keybd_event（老 API，但很多 IME 对它更宽容）
        keybd_event(vk, (BYTE)sc, 0, 0);
        Sleep(1);
        keybd_event(vk, (BYTE)sc, KEYEVENTF_KEYUP, 0);
    } else {
        // 方式4：一次性批量（down+up 塞进同一个 SendInput，零间隔）
        INPUT a[2] = {};
        a[0].type = INPUT_KEYBOARD;
        a[0].ki.wVk = vk;
        a[0].ki.wScan = (WORD)sc;
        a[1] = a[0];
        a[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, a, sizeof(INPUT));
    }
}

static void RunSendTest() {
    // 先落一份说明，免得用户看完记事本却不知道怎么判读
    wchar_t self[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wchar_t* slash = wcsrchr(self, L'\\');
    if (slash) *(slash + 1) = 0;
    wchar_t note[MAX_PATH * 2] = {0};
    _snwprintf_s(note, MAX_PATH * 2, _TRUNCATE, L"%lssendtest_readme.txt", self);

    FILE* f = NULL;
    if (_wfopen_s(&f, note, L"w, ccs=UTF-8") == 0 && f) {
        fwprintf(f, L"HKeyboard 注入方式对照实验   构建=%ls\n\n", BuildStampText());
        fwprintf(f, L"记事本里从上到下依次是 4 种方式的结果，空行分开：\n");
        fwprintf(f, L"  第 1 段 = 方式1  VK 两段，不带 SCANCODES（最早的做法）\n");
        fwprintf(f, L"  第 2 段 = 方式2  VK + KEYEVENTF_SCANCODE（当前版本的做法）\n");
        fwprintf(f, L"  第 3 段 = 方式3  keybd_event\n");
        fwprintf(f, L"  第 4 段 = 方式4  一次性批量（down+up 同一个 SendInput）\n\n");
        fwprintf(f, L"判读：\n");
        fwprintf(f, L"  出现「你好」  = 该方式有效\n");
        fwprintf(f, L"  出现 nihao    = 该方式下 IME 没组字\n");
        fwprintf(f, L"  该段空白      = 按键没送达\n\n");
        fwprintf(f, L"哪一种成功就照那种改 SendKey。\n");
        fwprintf(f, L"四种全失败 = SendInput 走不通，需要换注入途径。\n");
        fwprintf(f, L"四种全成功 = 注入没问题，是 HKeyboard 其它部分在干扰。\n");
        fclose(f);
    }

    Sleep(3000);   // 给用户切回记事本的时间

    static const BYTE kLetters[5] = { 'N', 'I', 'H', 'A', 'O' };
    for (int m = 0; m < 4; m++) {
        for (int i = 0; i < 5; i++) {
            SendTestKey(kLetters[i], m);
            Sleep(30);
        }
        Sleep(200);
        SendTestKey(VK_SPACE, m);      // 让 IME 上屏
        Sleep(200);
        SendTestKey(VK_RETURN, m);     // 两个回车 = 空行分隔
        Sleep(80);
        SendTestKey(VK_RETURN, m);
        Sleep(500);
    }
}

// ---- -envtest：在 HKeyboard 的**完整环境**里只做纯注入 ----
//
// 背景：`-sendtest` 已证明，在**什么都不装**的裸进程里，四种纯注入
// 方式**全部成功**（记事本四段都是「你好」）。⇒ 注入代码本身没问题，
// 问题出在 HKeyboard 的**运行环境**或**调用方式**上。
//
// 这个入口把问题一分为二：让 HKeyboard **正常启动** —— 窗口、低层键盘
// 钩子、两个 WinEvent 钩子、焦点定时器、托盘图标全都装好，和平时一模一样 ——
// 但**只用纯注入**打字，既不经过 SendKey，也不需要用户点击按键。
//
//   · 若记事本里出现「你好」 ⇒ 环境无辜，问题在 SendKey 的代码路径，
//                              或者「用户点击按键」这个动作本身
//   · 若出现 nihao          ⇒ 环境里有东西在干扰注入，
//                              再逐个摘掉（钩子 / 窗口 / 定时器）即可定位
//
// ⚠ 用定时器触发，**不在 WinMain 里 Sleep**。真实按键也是"短暂阻塞 UI 线程
//   后立即返回"，用定时器才与它等价；直接 Sleep 会把消息循环堵很久，
//   那是另一种（不真实的）状态，测出来的东西不可信。
// 下面 EnvTestInject 用到的三个辅助函数定义在其后，先声明。
// （顺序上它们必须排在 EnvTestInject 之后 —— 因为要用到 HitKey /
//   g_keys 等前面定义的东西，而把 EnvTestInject 挪到最后又要动
//   WinMain 附近的结构，声明在前最省事。）
static void SendNihao();
static BOOL FindKeyPos(BYTE vk, int* outX, int* outY);
static void ClickOwnKey(BYTE vk);

static void EnvTestInject() {
    // 落一份构建戳，方便确认这次实验跑的是哪一版。
    // （-sendtest 已有同样的机制，实测确实用上了：能确认用户手上的 exe 是哪次构建）
    {
        wchar_t self[MAX_PATH] = {0};
        GetModuleFileNameW(NULL, self, MAX_PATH);
        wchar_t* slash = wcsrchr(self, L'\\');
        if (slash) *(slash + 1) = 0;
        wchar_t note[MAX_PATH * 2] = {0};
        _snwprintf_s(note, MAX_PATH * 2, _TRUNCATE, L"%lsenvtest_readme.txt", self);
        FILE* f = NULL;
        if (_wfopen_s(&f, note, L"w, ccs=UTF-8") == 0 && f) {
            fwprintf(f, L"HKeyboard -envtest   构建=%ls\n\n", BuildStampText());
            fwprintf(f, L"记事本里会有两段，用空行分开：\n");
            fwprintf(f, L"  第 1 段 = 纯注入打 nihao + 空格   （基线，此前已确认成功）\n");
            fwprintf(f, L"  第 2 段 = 用**真实鼠标依次点击**键盘上的 N I H A O，再点空格\n");
            fwprintf(f, L"           —— 这等价于你自己手打一遍，是最直接的复现\n\n");
            fwprintf(f, L"判读（只看「你好」两个字在不在一起）：\n");
            fwprintf(f, L"  两段都出现「你好」  = 点击路径也没问题，需继续找差异\n");
            fwprintf(f, L"  第 2 段是 nihao     = **点击路径就是元凶**，定位完成\n");
            fwprintf(f, L"  第 2 段是 n + 你好  = 点击的那个 n 没进 IME，之后才正常\n\n");

            // ---- 键表诊断（关键！）----
            //
            // ⚠ 上一轮"点 N 键"那版，用户反馈两段都是「你好」——
            //   但第 2 段里**看不到那个本该多出来的 n**。
            //   若真如此，说明 ClickOwnKey 根本没点下去，而它的前提是
            //   FindKeyPos 能找到 N 键的坐标；FindKeyPos 又依赖 HitKey。
            //   ⇒ 把键表的真实数据打出来：只要 k->x/y 是"格"单位（1.x、2.x）
            //     而不是客户区像素，HitKey 就永远匹配不上，
            //     **HKeyboard 的鼠标点击会整体失效** —— 那才是根因。
            fwprintf(f, L"---- 键表诊断（排查用）----\n");
            fwprintf(f, L"g_nk=%d  g_ww=%d  g_wh=%d  g_keyAreaX=%d\n",
                     g_nk, g_ww, g_wh, g_keyAreaX);
            for (int i = 0; i < g_nk && i < 6; i++) {
                // ⚠ KeyDef.x/y/w/h 是 **int**（见其定义），这里必须用 %d。
                //   写成 %.2f 会让 varargs 把一个 int 当 double 读，
                //   结果是垃圾值甚至崩溃 —— 排查工具自己先崩就没意义了。
                fwprintf(f, L"  keys[%d] vk=0x%02X  x=%d y=%d w=%d h=%d type=%d\n",
                         i, g_keys[i].vk, g_keys[i].x, g_keys[i].y,
                         g_keys[i].w, g_keys[i].h, (int)g_keys[i].type);
            }
            int nx = 0, ny = 0;
            BOOL nf = FindKeyPos('N', &nx, &ny);
            fwprintf(f, L"FindKeyPos(N) -> %ls  (x=%d y=%d)\n",
                     nf ? L"found" : L"**NOT FOUND**", nx, ny);
            fwprintf(f, L"（若这里是 NOT FOUND，或上面的 x/y 是 1.x 2.x 这种"
                        L"「格」单位而不是几十上百的像素，\n"
                        L"  就说明 HitKey 匹配不上 —— HKeyboard 的点击会整体失效）\n");
            fclose(f);
        }
    }

    // ---- 阶段 1：纯注入（基线）----
    //
    // ⚠ 这里的 DiagSnap 是关键对照：它记录的是**定时器触发注入**时的系统状态，
    //   与用户**手动点击按键**时（DoKeyAction 里那次 DiagSnap）形成对照。
    //   两边只有一处可能不同 —— LBTN（鼠标左键是否按下）—— 那就是元凶所在。
    DiagSnap(L"[env-bef]");
    SendNihao();
    DiagSnap(L"[env-aft]");
    SendTestKey(VK_RETURN, 1);
    SendTestKey(VK_RETURN, 1);
    Sleep(700);

    // ---- 阶段 2：**完全用真实点击**打一遍 nihao + 空格 ----
    //
    // ⚠⚠ 上一版这里只点了一个 N 键，然后接纯注入 —— **那个设计有歧义**：
    //   点 N 若成功进入组字（拼音串 "n"），后续注入的 "nihao" 会追加成
    //   "nnihao"，上屏结果不可预期；点 N 若失败（打出英文 n），后面的
    //   纯注入照样能独立组出「你好」。两种情况都会看到「你好」，
    //   却指向完全相反的结论 —— 等于白测。
    //
    //   ⇒ 改成**全程真实点击**：五个字母 + 空格全部由鼠标点击产生。
    //     这就和用户手打一模一样，结果无歧义：
    //       出「你好」⇒ 点击路径正常；出 nihao ⇒ 点击路径就是元凶。
    {
        static const BYTE kL[5] = { 'N', 'I', 'H', 'A', 'O' };
        for (int i = 0; i < 5; i++) {
            ClickOwnKey(kL[i]);
            Sleep(100);
        }
        Sleep(200);
        ClickOwnKey(VK_SPACE);
    }
}

// 打一遍 nihao + 空格（方式2 = 当前版本 SendKey 的做法）。
static void SendNihao() {
    static const BYTE kL[5] = { 'N', 'I', 'H', 'A', 'O' };
    for (int i = 0; i < 5; i++) {
        SendTestKey(kL[i], 1);
        Sleep(30);
    }
    Sleep(200);
    SendTestKey(VK_SPACE, 1);       // 上屏
}

// 反查某个 vk 对应的按键在**窗口坐标**里的位置。
//
// ⚠ 刻意不自己算"格单位 → dpiScale → scaleX"那套换算：那一串乘除
//   容易写错，而且各布局（默认/小键盘/网页层）还不一样。
//   直接拿现成的 HitKey 反查，得到的坐标一定和点击处理用的是同一套。
static BOOL FindKeyPos(BYTE vk, int* outX, int* outY) {
    for (int y = 0; y < g_wh; y += 2) {
        for (int x = 0; x < g_ww; x += 2) {
            int ki = HitKey(x, y);
            if (ki >= 0 && g_keys[ki].vk == vk) {
                if (outX) *outX = x;
                if (outY) *outY = y;
                return TRUE;
            }
        }
    }
    return FALSE;
}

// 对键盘上的某个键做一次**真实鼠标点击**（走 SendInput，与用户手点等价）。
static void ClickOwnKey(BYTE vk) {
    if (!g_hWnd || !IsWindow(g_hWnd)) return;
    int kx = 0, ky = 0;
    if (!FindKeyPos(vk, &kx, &ky)) return;

    POINT pt = { kx, ky };
    ClientToScreen(g_hWnd, &pt);

    int sw = GetSystemMetrics(SM_CXSCREEN);
    int sh = GetSystemMetrics(SM_CYSCREEN);
    if (sw < 2 || sh < 2) return;

    INPUT mi = {};
    mi.type = INPUT_MOUSE;
    mi.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
    mi.mi.dx = (LONG)(((LONGLONG)pt.x * 65535) / (sw - 1));
    mi.mi.dy = (LONG)(((LONGLONG)pt.y * 65535) / (sh - 1));
    SendInput(1, &mi, sizeof(INPUT));
    Sleep(60);

    mi.mi.dwFlags = MOUSEEVENTF_LEFTDOWN | MOUSEEVENTF_ABSOLUTE;
    SendInput(1, &mi, sizeof(INPUT));
    Sleep(60);

    mi.mi.dwFlags = MOUSEEVENTF_LEFTUP | MOUSEEVENTF_ABSOLUTE;
    SendInput(1, &mi, sizeof(INPUT));
    Sleep(120);
}
#endif  // HK_DIAG —— 以上全部诊断代码在正式版里不参与编译

// 高精度计时器分辨率（动态加载 winmm，避免新增链接依赖）
// SetTimer 默认受 ~15.6ms 系统计时粒度限制，动画会一顿一顿；
// 进程级调到 1ms 让窗口滑动定时器按请求间隔触发。
struct TimePeriodApi {
    HMODULE module;
    ULONG (WINAPI *begin)(UINT);
    ULONG (WINAPI *end)(UINT);
};
static TimePeriodApi g_timePeriod = {};

static void InitTimePeriodApi() {
    g_timePeriod.module = LoadLibraryW(L"winmm.dll");
    if (g_timePeriod.module) {
        g_timePeriod.begin = (ULONG (WINAPI*)(UINT))GetProcAddress(g_timePeriod.module, "timeBeginPeriod");
        g_timePeriod.end = (ULONG (WINAPI*)(UINT))GetProcAddress(g_timePeriod.module, "timeEndPeriod");
    }
}

int WINAPI WinMain(HINSTANCE hI, HINSTANCE, LPSTR cmd, int) {
    g_hInst = hI;
    // 启动预热期的起点（见 g_appStartTick 的说明）
    g_appStartTick = GetTickCount();

    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        typedef BOOL (WINAPI *SetDpiAwareProc)();
        SetDpiAwareProc pSetDPIAware = (SetDpiAwareProc)GetProcAddress(hUser32, "SetProcessDPIAware");
        if (pSetDPIAware) pSetDPIAware();
    }

    InitTimePeriodApi();
    if (g_timePeriod.begin) g_timePeriod.begin(1);   // 提升动画定时器精度

    // 系统版本检测（图标字体按系统版本选择）
    DetectWinVersion();

    InitGdiPlus();       // 初始化 GDI+ （抗锯齿圆形绘图）
    LoadEmbeddedFonts();   // 注册内嵌字体（MiSans Medium 精简子集），失败自动回退系统字体
    InitFixedFonts();      // 设置/关闭窗口固定字号字体

    BOOL fShow   = (strstr(cmd, "-show") != NULL);
    BOOL fHide   = (strstr(cmd, "-hide") != NULL || strstr(cmd, "-min") != NULL || strstr(cmd, "-tray") != NULL);
    BOOL tOnly   = (strstr(cmd, "-touchonly") != NULL);

    BOOL fAuto   = (strstr(cmd, "-auto") != NULL);
    BOOL fNoAuto = (strstr(cmd, "-noauto") != NULL);

    BOOL fDark   = (strstr(cmd, "-dark") != NULL);
    BOOL fLight  = (strstr(cmd, "-light") != NULL);
    BOOL fWall   = (strstr(cmd, "-wallpaper") != NULL);
    BOOL fHelp   = (HasArg(cmd, "-h") || HasArg(cmd, "-help") || HasArg(cmd, "-?"));

    // 主题参数解析
    if (fDark) g_themeMode = 1;
    else if (fLight) g_themeMode = 2;
    else g_themeMode = 0;  // 默认跟随系统
    g_wallpaperAccent = fWall;  // 壁纸强调色默认关闭，仅 -wallpaper 开启
    BOOL fThemeCli = (fDark || fLight || HasArg(cmd, "-theme:system"));   // 命令行是否显式指定主题

    // -h / -help / -?：仅显示命令行参数帮助，不打开主界面
    if (fHelp) {
        ShowHelpDialog(NULL);
        return 0;
    }

#ifdef HK_DIAG
    // ---- 以下全是排查参数，正式版不编译（见文件顶部"诊断开关"说明）----

    // -sendtest：注入方式对照实验（说明见 RunSendTest 上方的大段注释）。
    // ⚠⚠ 必须在 `CreateMutexW` **之前**返回。否则若托盘里已有实例在跑，
    //   就会被单实例逻辑转发消息后静默退出，实验根本跑不起来 ——
    //   而用户看到的现象会是"运行了但没反应"，又是一次无谓的往返。
    if (HasArg(cmd, "-sendtest")) {
        RunSendTest();
        return 0;
    }

    // -envtest：在**完整环境**里只做纯注入（说明见 EnvTestInject）。
    // 与 -sendtest 的区别：那个什么都不装，这个全装（窗口/钩子/定时器）。
    // ⚠ 不能在这里 return —— 要让它正常启动到消息循环，
    //   再由 TIMER_ENVTEST 驱动注入，这样环境才是"真在跑的 HKeyboard"。
    g_envTest = HasArg(cmd, "-envtest") ? TRUE : FALSE;

    // 排查开关：二分「点击按键」时哪个动作破坏了 IME 组字（见其定义处说明）
    g_noSetCapture    = HasArg(cmd, "-noscapture") ? TRUE : FALSE;
    g_noClickRepaint  = HasArg(cmd, "-norepaint")  ? TRUE : FALSE;
    // -diag：记录每次字母键注入前后的系统状态到 diag.txt（见 DiagSnap）
    g_diag            = HasArg(cmd, "-diag")       ? TRUE : FALSE;
    // -envtest 一律开启记录：它的两次注入就是"手动点击"的对照组，
    // 少了这个没法比较（见 DiagSnap 的说明）。
    if (g_envTest) g_diag = TRUE;
#endif  // HK_DIAG

    // ⚠ -afdiag 必须放在 #endif **外面** —— 它要能在正式版里工作。
    //   自动呼出诊断日志（临时，定位完删）
    if (HasArg(cmd, "-afdiag")) {
        wchar_t lp[MAX_PATH] = {0};
        GetModuleFileNameW(NULL, lp, MAX_PATH);
        wchar_t* sl = wcsrchr(lp, L'\\');
        if (sl) *(sl + 1) = 0;
        wcscat_s(lp, MAX_PATH, L"afdiag.txt");
        // 先把路径存好（AfNote 要用），再用 CREATE_ALWAYS 清一次文件
        wcscpy_s(g_afLogPath, MAX_PATH, lp);
        HANDLE h0 = CreateFileW(lp, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, NULL);
        if (h0 != INVALID_HANDLE_VALUE) CloseHandle(h0);
        {
            char buf[160];
            _snprintf_s(buf, 160, _TRUNCATE,
                        "started with -afdiag, build %s %s, pid=%lu",
                        __DATE__, __TIME__, (unsigned long)GetCurrentProcessId());
            AfNote(buf);
        }
    }

    BOOL isTouch = IsTouchDevice();

    if (tOnly && !isTouch) return 0;
    AfNote("--- passed single-instance check, creating window ---");

    g_mutex = CreateMutexW(0, FALSE, L"HKeyboard_Mutex");
    if (g_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        // 诊断：上次 afdiag.txt 只有 BOM、一行都没有，最可能就是走的这条路 ——
        // 旧实例还在跑，新进程转发消息后静默退出，日志自然空白。
        AfNote("!!! ANOTHER INSTANCE ALREADY RUNNING -> exit now");
        CloseHandle(g_mutex);
        HWND ew = FindWindowW(L"HKeyboard", 0);
        if (ew) {
            DWORD_PTR result = 0;
            SendMessageTimeoutW(ew, WM_SHOW_KEYBOARD, fHide ? FALSE : TRUE, 0,
                                SMTO_ABORTIFHUNG, 1500, &result);
        }
        g_exiting = FALSE;
        return 0;
    }

    HICON hAppIcon = LoadMainIcon(32);
    WNDCLASSEXW wc = {sizeof(wc), CS_DBLCLKS, WndProc, 0, 0, hI,
        hAppIcon, LoadCursor(0, IDC_ARROW), (HBRUSH)GetStockObject(BLACK_BRUSH), 0, L"HKeyboard", hAppIcon};
    RegisterClassExW(&wc);
    WNDCLASSEXW wcs = {sizeof(wcs), CS_DBLCLKS, SettingsWndProc, 0, 0, hI,
        hAppIcon, LoadCursor(0, IDC_ARROW), (HBRUSH)GetStockObject(BLACK_BRUSH), 0, L"HKeyboardSettings", hAppIcon};
    RegisterClassExW(&wcs);
    WNDCLASSEXW wcp = {sizeof(wcp), CS_DBLCLKS, PromptWndProc, 0, 0, hI,
        hAppIcon, LoadCursor(0, IDC_ARROW), (HBRUSH)GetStockObject(BLACK_BRUSH), 0, L"HKeyboardClosePrompt", hAppIcon};
    RegisterClassExW(&wcp);

    // 配置：首次启动自动生成 HKeyboard.ini（系统环境已在前置检测）
    EnsureConfigFile();
    LoadConfig();
    if (fNoAuto) g_af = FALSE;
    else if (fAuto) g_af = TRUE;
    if (fThemeCli) g_themeMode = fDark ? 1 : (fLight ? 2 : 0);   // 命令行主题优先
    if (fWall) g_wallpaperAccent = TRUE;
    // 窗口尺寸：优先恢复当前布局记忆的大小；无有效记忆时按布局与 DPI 计算默认值
    {
        RECT saved;
        if (LoadLayoutWindowRect(&saved) && LayoutRectOnScreen(saved)) {
            g_ww = saved.right - saved.left;
            g_wh = saved.bottom - saved.top;
        } else {
            InitWindowSizeForDpi();
        }
    }
    ApplyTheme();

    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    HWND hWnd = CreateWindowExW(WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,
        L"HKeyboard", T(L"\x8F7B\x952E", L"HKeyboard"), WS_POPUP,
        work.left + ((work.right - work.left) - g_ww) / 2,
        work.bottom - g_wh - 6,
        g_ww, g_wh, 0, 0, hI, 0);
    if (!hWnd) return 1;

    AddTray();   // 无论是否触屏/隐藏模式，始终创建托盘图标，以便从托盘恢复

    if (!fHide) {
        ShowKB(TRUE, TRUE);
    } else {
        g_vis = FALSE;
        ShowWindow(hWnd, SW_HIDE);
    }

#ifdef HK_DIAG
    // -envtest：3 秒后开始注入（给用户切回记事本、放进光标的时间）。
    // 此刻窗口、低层键盘钩子、两个 WinEvent 钩子、焦点定时器、托盘
    // 全都已装好 —— 与真实使用状态完全一致，只差"用户点击按键"那一下。
    if (g_envTest) {
        SetTimer(hWnd, TIMER_ENVTEST, 3000, NULL);
    }
#endif  // HK_DIAG

    MSG msg;
    while (GetMessage(&msg, 0, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    if (g_fontReg) RemoveFontMemResourceEx(g_fontReg);
    if (g_gdipFonts) { delete g_gdipFonts; g_gdipFonts = NULL; }
    if (g_timePeriod.end) g_timePeriod.end(1);
    ShutdownGdiPlus();
    return (int)msg.wParam;
}
