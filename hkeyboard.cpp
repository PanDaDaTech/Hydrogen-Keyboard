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

// 关于页显示的版本串 = "<VER_FILEVERSION_STR>_<构建日期>"，如 "2.0_20261002"。
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
    DWORD titleActive;       // 章节标题（title-active：比主色深一档、比正文淡）
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
static void UserHideKeyboard();   // 手动收起（自动收起开启时同输入框内不回弹）
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
int         g_hideDelayMs = 1000;      // 自动隐藏延迟（固定 1 秒，不提供设置）
DWORD       g_lastNonInput = 0;        // 最近一次离焦时刻（自动隐藏延迟用）

// 语言切换：g_lang=0 简体中文，1 English；返回当前语言对应的文案
static const wchar_t* T(const wchar_t* zh, const wchar_t* en) { return g_lang ? en : zh; }
BOOL        g_sh = FALSE, g_ct = FALSE, g_al = FALSE, g_cp = FALSE;
BOOL        g_winKey = FALSE;
int         g_winCount = 0;           // Win 键状态：0=空闲 1=锁定（等待 Win+组合键）
DWORD       g_lastWinTick = 0;        // 最近一次 Win 键点击时刻（状态超时复位用）
HHOOK       g_kbHook = 0;             // 实体键盘低级钩子（监控 Win/Shift/Caps 状态同步显示）
BOOL        g_physShift = FALSE;      // 实体 Shift 是否按住（仅显示同步，不影响虚拟键逻辑）
BOOL        g_physWin = FALSE;        // 实体 Win 是否按住（仅显示同步）
// 实体 NumLock 锁定态（GetAsyncKeyState(VK_NUMLOCK) 的 bit0），全尺寸数字区的 Num 键跟着它高亮
BOOL        g_physNum = FALSE;
BOOL        g_physFn = FALSE;         // 预留接口：Fn 实体键状态（多数键盘不产生按键事件，后续按需扩展）
BOOL        g_af = TRUE;
BOOL        g_afAutoHide = TRUE;       // 自动呼出开启时，收起键盘后同一输入框内不自动回弹（ini: General/AutoHide）
static BOOL g_userHidInInput = FALSE;  // 用户刚在输入状态下手动收起（自动收起开启时不回弹）
static ULONG_PTR g_hiddenInputToken = 0; // 手动收起时所在的输入控件标识
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
int         g_keyIconStyle = 2;        // 按键图标样式：0=文字 2=图标+文字（默认，ini Keyboard/KeyIconStyle）
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
static HFONT g_sfSec = 0;                                                          // 章节标题（关于页「开源许可」）
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
        // 1280 宽下 1u ≈ 56.8px，键帽 52px（原来 1380×372 会算成 46~73px 的五种宽度）
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
// ↑ 落在 12.5u 处，与下一行的 ↓ 完全同列（旧写法要靠 xUp 变量把 ↓ 手动对齐）。
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
    // 旧写法是 `bottomPad = 16 DIP` 且第一排 y 里硬塞一个 `+2`（px！与 DIP 混算），
    // 实测小键盘布局上边距 8.67 / 下边距 14.67 DIP —— 肉眼就是「整体偏下、上下间距不合理」。
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
// 14 这一档是为关于页的章节标题（「社区交流 / 开源许可」）加的：制作工具的同一处是
// 18 DIP（页标题 26），比例 0.69；本窗口页标题 18，按同一比例落在 12.4 —— 但那与行主文本
// 同号，层级就没了。取 14：站得住「比行标题大一档」，又实实在在小于页面标题的 18。
static void InitFixedFonts() {
    double dpi = GetSystemDpiScale();
    g_sfBig  = MakeFont(18 * dpi);     // 页面大标题
    g_sfSec  = MakeFont(14 * dpi);     // 章节标题
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
        // Win11 风格四格徽标：四块圆角方块 + 中间一条缝
        int sq = (int)(size * 0.44 + 0.5);
        int gp = (int)(size * 0.12 + 0.5);
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
    if (g_keyIconStyle == 0) return FALSE;
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
            int avail = k->w - (int)(6 * GetSystemDpiScale());
            if (g_bkspTextW > 0) return (avail >= g_bkspTextW) ? L"Backspace" : L"Bksp";
            return (k->w >= (int)(1.9 * k->h)) ? L"Backspace" : L"Bksp";
        }
        case 0x09: return L"Tab";
        case 0x0D: return L"Enter";
        case 0x14: return L"Caps";
        case 0x10: case 0xA0: case 0xA1: return L"Shift";
        case 0x11: return L"Ctrl";
        case 0x12: return L"Alt";
        // Win 键：键面是矢量四格徽标 + 这个名字（见 DrawKeyGlyph）。
        // 原来这里返回空串（只有徽标、没有名字），「图标+文字」档下就少了一半信息。
        case 0x5B: return L"Win";
        case 0x5D: return L"Menu";       // 三条杠是它的图形；文字模式下要给出名字（原来返回空串）
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
// 修复 #2: 使用 SendInput 替代已废弃的 keybd_event()
// 修复 #5: 使用 MapVirtualKeyW (Unicode 版本) 并正确设置扫描码与扩展键标志
static void SendKey(BYTE vk, BOOL sh, BOOL ct, BOOL al, BOOL win) {
    INPUT inputs[12] = {};
    int count = 0;

    // 使用 MapVirtualKeyW 获取正确扫描码（修复 #5: 部分 IME 依赖正确扫描码）
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC);

    // 判断扩展键（右 Ctrl/Alt、方向键、Win 等；右 Shift 不带 E0 扩展标志）
    // NumLock 也在这张表里：真键盘上它是 E0 45，少了这个标志部分环境收不到 / 不切换。
    BOOL isExtended = (vk == VK_RCONTROL || vk == VK_RMENU ||
                       vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
                       vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
                       vk == VK_INSERT || vk == VK_DELETE || vk == VK_LWIN || vk == VK_RWIN ||
                       vk == VK_NUMLOCK);

    DWORD extFlag = isExtended ? KEYEVENTF_EXTENDEDKEY : 0;

    // 按下修饰键
    if (ct) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_CONTROL;
        inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC);
        count++;
    }
    if (al) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_MENU;
        inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC);
        inputs[count].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
        count++;
    }
    if (sh) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_SHIFT;
        inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC);
        count++;
    }
    if (win) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_LWIN;
        inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_LWIN, MAPVK_VK_TO_VSC);
        inputs[count].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;
        count++;
    }

    // 目标键 down + up（以 VK 形式发送，TSF/IME 可正确拦截 WM_KEYDOWN）
    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = vk;
    inputs[count].ki.wScan = (WORD)sc;
    inputs[count].ki.dwFlags = extFlag;
    count++;

    inputs[count].type = INPUT_KEYBOARD;
    inputs[count].ki.wVk = vk;
    inputs[count].ki.wScan = (WORD)sc;
    inputs[count].ki.dwFlags = extFlag | KEYEVENTF_KEYUP;
    count++;

    // 释放修饰键
    if (win) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_LWIN;
        inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_LWIN, MAPVK_VK_TO_VSC);
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY;
        count++;
    }
    if (sh) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_SHIFT;
        inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_SHIFT, MAPVK_VK_TO_VSC);
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        count++;
    }
    if (al) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_MENU;
        inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_MENU, MAPVK_VK_TO_VSC);
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP | KEYEVENTF_EXTENDEDKEY;
        count++;
    }
    if (ct) {
        inputs[count].type = INPUT_KEYBOARD;
        inputs[count].ki.wVk = VK_CONTROL;
        inputs[count].ki.wScan = (WORD)MapVirtualKeyW(VK_CONTROL, MAPVK_VK_TO_VSC);
        inputs[count].ki.dwFlags = KEYEVENTF_KEYUP;
        count++;
    }

    SendInput(count, inputs, sizeof(INPUT));
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

// 关闭开始菜单：发送 Esc。
// 开始菜单打开且处于前台时，Esc 是可靠关闭它的系统行为（注入的 Win 键在部分环境下“能开不能关”）。
static void CloseStartMenu() {
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wVk = VK_ESCAPE;
    in.ki.wScan = (WORD)MapVirtualKeyW(VK_ESCAPE, MAPVK_VK_TO_VSC);
    SendInput(1, &in, sizeof(INPUT));
    in.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &in, sizeof(INPUT));
}

// 清除 Win 锁定：解锁并重置点击计数（使用 Win+快捷键或检测到开始菜单时调用）
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
        SendKey((BYTE)vk, sh, FALSE, FALSE);
    }
}

static void DoKeyAction(const KeyDef* k) {
    if (!k) return;
    switch (k->type) {
    case K_LETTER:
        if (g_ct || g_al || g_winKey) {
            SendKey(k->vk, g_sh, g_ct, g_al, g_winKey);
            g_sh = FALSE; g_ct = FALSE; g_al = FALSE; ClearWinLock();
        } else {
            BOOL us = g_sh ? !(GetKeyState(VK_CAPITAL) & 1) : FALSE;
            SendKey(k->vk, us, FALSE, FALSE);
            if (g_sh) g_sh = FALSE;
            ClearWinLock();   // 普通键也退出 Win 锁定/切换状态
        }
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
    case K_HIDE: UserHideKeyboard(); break;
    default: break;
    }
}
// ========== Header Layout & Dynamic DPI Positioning ==========
#define HDR_DOCK  1000
#define HDR_MIN   1003
#define HDR_CLOSE 1004
#define HDR_NUM   1005   // 123 按钮：切换小键盘

// 标题栏几何：绘制与命中必须取自同一份数据。
// 这里历史上抄过两份（DrawHeader / HitHeader 各写一遍），改宽度就会出「按钮画在左、
// 热区在右」的 bug；今后任何改动只改这里。
struct HeaderMetrics {
    int btnY, btnH;
    int wMenu, wMin, wNum, wClose;
    int xMenu, xMin, xNum, xClose;
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
    hm.wNum   = (int)(96 * dpi * fr);

    hm.xClose = g_ww - rMargin - hm.wClose;
    hm.xMin   = hm.xClose - gap - hm.wMin;
    // 全尺寸布局下也常驻：它是数字区唯一的界面开关（也给「窄屏自动收起」留了恢复入口）。
    hm.numBtnVisible = g_showNumBtn;
    hm.xNum   = hm.numBtnVisible ? (hm.xMin - gap - hm.wNum) : hm.xMin;
    hm.xMenu  = (int)(6 * dpi);

    hm.xTitle = hm.xMenu + hm.wMenu + gap;
    hm.wTitle = (hm.numBtnVisible ? hm.xNum : hm.xMin) - hm.xTitle - gap;
    return hm;
}

static int HitHeader(int x, int y) {
    if (y < 0 || y >= g_headerH) return -1;
    HeaderMetrics hm = GetHeaderMetrics();
    if (y < hm.btnY || y >= hm.btnY + hm.btnH) return -1;

    if (x >= hm.xClose && x < hm.xClose + hm.wClose) return HDR_CLOSE;
    if (x >= hm.xMin && x < hm.xMin + hm.wMin) return HDR_MIN;
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

    // 最小化 / 关闭：改用矢量图标（与设置页、关闭提示窗口同一套图形，不再手绘线条）
    // 外框按各自图标的可见尺寸反解（见 HeaderIconBox），两者观感才一样大。
    int szMin  = HeaderIconBox(HKICON_MINIMIZE, dpiScale);
    int szCls  = HeaderIconBox(HKICON_CLOSE, dpiScale);
    int hoverR = (int)(6 * dpiScale);
    if (g_hdrHov == HDR_MIN) {
        DrawRoundRect(dc, hm.xMin, hm.btnY, hm.wMin, hm.btnH,
                      C_REGULAR_HOV, C_REGULAR_HOV, hoverR);
    }
    DrawHkIcon(dc, (float)(hm.xMin + (hm.wMin - szMin) / 2),
               (float)(hm.btnY + (hm.btnH - szMin) / 2), (float)szMin,
               HkIcon(HKICON_MINIMIZE), C_DIM, C_DIM);
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

// 用户主动收起（键盘“收起”键 / 标题栏最小化按钮）：
// 自动收起开启时，若当前焦点在输入框中，记录该输入控件，
// UpdateAutoVisibility 在同一输入框内不再自动回弹（焦点换框后恢复自动呼出）。
static void UserHideKeyboard() {
    if (g_afAutoHide) {
        HWND input = GetFocusedInputControl();
        if (input) {
            g_userHidInInput = TRUE;
            g_hiddenInputToken = g_detectedInputToken;
        }
    }
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
#define S_HIT_KEYICON        26   // 布局 Tab：按键图标样式（分段控件，整条一个命中码）
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

static int  g_sTab = 0;        // 0=常规 1=主题 2=关于
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

static void DrawRadio(HDC dc, int x, int cy, int r, BOOL on, DWORD bg) {
    // GDI+ 抗锯齿圆环：外圈 + 内圈挖空 + 选中实心点
    DrawCircleAA(dc, x, cy, r, C_DIM);
    DrawCircleAA(dc, x, cy, r - 2, bg);
    if (on) DrawCircleAA(dc, x, cy, r - 4, C_HOT);
}

// 开关按钮（on=开启；通常右侧对齐显示）
static void DrawSwitch(HDC dc, int x, int y, int w, int h, BOOL on) {
    DrawRoundRect(dc, x, y, w, h, on ? C_HOT : C_DARK, C_KEY_BORDER, h / 2);
    int knob = h - 6;
    int kx = on ? x + w - knob - 3 : x + 3;
    DrawRoundRect(dc, kx, y + 3, knob, knob, on ? C_KEY : C_DIM, on ? C_KEY : C_DIM, knob / 2);
}

static void DrawCheck(HDC dc, int x, int y, int s, BOOL on) {
    DrawRoundRect(dc, x, y, s, s, on ? C_HOT : C_KEY, C_KEY_BORDER, s / 3);
    if (on) {
        HPEN p = CreatePen(PS_SOLID, 2, C_ON_PRIMARY);
        HPEN op = (HPEN)SelectObject(dc, p);
        MoveToEx(dc, x + 3, y + s / 2, NULL);
        LineTo(dc, x + s / 2, y + s - 3);
        LineTo(dc, x + s - 2, y + 2);
        SelectObject(dc, op); DeleteObject(p);
    }
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

// 每行的「控件高」，单位 DIP（不乘 dpi）：开关 26 / 下拉与分段 40；无控件行给 0
static int SettingsRowCtrlDip(int tab, int index) {
    if (tab == 0) {                                 // 常规
        int closeRow = g_af ? 2 : 1;
        if (index == closeRow) return 40;           // 关闭按钮下拉
        if (index == closeRow + 4) return 40;       // 界面语言下拉
        return 26;
    }
    if (tab == 3) {                                 // 布局
        if (index == 0 || index == 1) return 40;    // 键盘布局下拉 / 按键图标样式分段
        return 26;
    }
    if (tab == 1) return 40;                        // 主题：模式 / 透明度 / 色相 都是下拉行
    return 0;
}

// 描述**可能**需要两行的行（文案本身长，窗口窄时一行放不下会被硬截断）：
// 行高、绘制、命中三处都读这一份判断，才不会出现「字画到行外 / 热区对不上」。
static BOOL SettingsRowDescWraps(int tab, int index) {
    // Fn 网页布局（换文案后变长）/ 小键盘按钮：可能折成两行
    return (tab == 3 && (index == 2 || index == 3));
}

// 这两行的描述文案。必须是**唯一**的定义处：行高要不要按两行留白，靠实测这段文本
// 来定（见 SettingsRowDescTwoLines），绘制处再抄一遍就会两边不一致。
static const wchar_t* SettingsRowDescText(int tab, int index) {
    if (tab != 3) return NULL;
    if (index == 2)
        return T(L"按 Fn 切换：常用符号与网址后缀直接铺在键面上，不用按 Shift",
                 L"Press Fn: common symbols and web suffixes are laid out on the keys, no Shift needed");
    if (index == 3)
        return T(L"在标题栏显示；默认布局切小键盘，全尺寸显隐数字区",
                 L"Show it in the title bar; toggles the numpad section");
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
    if (g_sTab == 1 && index == 2 && !g_wallpaperAccent) return (int)(210 * m.dpi);
    int contentDip = SettingsRowCtrlDip(g_sTab, index);
    if (contentDip < 30) contentDip = 30;   // 图标 tile
    // 文字块：标题 18 + 间距 6 + 描述 16 = 40。间距不能省 —— 标题的下缘和描述的上缘
    // 会顶在一起（实机反馈「文本和描述的间距对吗」）；文字蒙版本身还上下各留了 4px。
    if (contentDip < 40) contentDip = 40;
    // 只有描述**真的**会折成两行时才多留 16 DIP。原来这里是「可能折行就留」，
    // 于是中文单行的「小键盘按钮」行高 80 DIP、内容只占 52，描述下方空 30 DIP。
    if (SettingsRowDescTwoLines(m, g_sTab, index) && contentDip < 56)
        contentDip = 56;                    // 标题 18 + 间距 6 + 描述两行 32
    return (int)((12 + contentDip + 12) * m.dpi);
}

static int SettingsRowCount(int tab) {
    if (tab == 0) return g_af ? 7 : 6;
    if (tab == 3) return 4;   // 布局 Tab：键盘布局 / 按键图标样式 / Fn 网页布局 / 小键盘按钮
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
        int y = SettingsRowRect(m, i + 1).top;
        Fill(dc, card.left + (int)(20 * m.dpi), y,
             (card.right - card.left) - (int)(40 * m.dpi), 1, C_LINE_DIV);
    }
}

// 行内元素定位：tile 在左，文字块起点 = 卡左 + 20 + 30 + 18
static int SettingsRowTileX(const SettingsMetrics& m) { return m.contentX + (int)(20 * m.dpi); }
static int SettingsRowTextX(const SettingsMetrics& m) { return SettingsRowTileX(m) + m.tileSize + m.tileGap; }
static int SettingsRowPadY(const SettingsMetrics& m) { return m.rowPadY; }

// 控件统一右对齐，右边界 = 卡片右内边距（20）
static int SettingsComboX(const SettingsMetrics& m, const RECT& row) {
    return row.right - (int)(20 * m.dpi) - m.comboW;
}

// 行的「头部」高度 = 12 + 40 + 12 DIP。图标 tile、文字块、行内控件（开关 / 分段 / 下拉）
// 全都只在这段里居中，**行因为描述折行而变高时不跟着变**。
// 布局页的「Fn 网页布局」「小键盘按钮」两行：换文案后描述折成两行，整行高 80 DIP；
// 控件若按整行居中，就会比 tile 与文字块低 (80-64)/2 = 8 DIP（实机实测 8.3 DIP）。
// 主题页的色相行是可展开行（210 DIP），同理。
static int SettingsRowHeadH(const SettingsMetrics& m, const RECT& row) {
    int headH = m.rowPadY * 2 + (int)(40 * m.dpi);
    int rowH = row.bottom - row.top;
    return headH > rowH ? rowH : headH;
}

static int SettingsComboY(const SettingsMetrics& m, const RECT& row) {
    return row.top + (SettingsRowHeadH(m, row) - m.comboH) / 2;
}

// 与 SettingsComboY 等价：头部高 = rowPadY*2 + comboH，在头部里居中后的偏移正好是 rowPadY。
// 色相行历史上单独走过一个「与标题对齐」的分支，保留这个名字表达意图。
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
                                  BOOL hover, int ctrlLeft, BOOL descWrap = FALSE) {
    if (hover) DrawSettingsRowHover(dc, m, row);

    // 图标 tile 与文字块都按行的「头部」居中（见 SettingsRowHeadH）：行因为描述折行
    // 变高时头部不变，二者仍中心对中心。展开行（主题色相）也走同一条规则，不跟着整行跑。
    int headH = SettingsRowHeadH(m, row);
    int ty = row.top + (headH - m.tileSize) / 2;
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

    // 文字块必须与 tile「中心对中心」。tile 30 DIP、文字块 42 DIP，**共用同一个 top 是不行的**：
    // 那样 tile 中心会比文字块中心高 6 DIP（上一版就是这样，实机上看着图标偏低）。
    // 文字块内两行盒的中心分别落在 块顶+10dpi（标题盒 20）与 块顶+26dpi+8dpi（描述盒 16）处，
    // 于是块中心 = 块顶 + 22dpi。让块中心对齐 tile 中心，反解出块顶。
    // 这个 22dpi 与描述折不折行**无关**：折行时用「盒顶上移 8 DIP、盒高 32 DIP」的画法，
    // 让描述第一行仍落在 块顶+34dpi（见下），块中心因此不变。
    // （文字自身还有「行盒中心 vs 墨迹中心」的补偿，已在 DrawTextGp 里统一做掉，
    //   所以这里直接按盒中心算即可，不需要再叠一次偏移。）
    int textMid = ((int)(20 * m.dpi) / 2 + (int)(26 * m.dpi) + (int)(16 * m.dpi) / 2) / 2;
    int tyText = ty + m.tileSize / 2 - textMid;

    DrawTextL(dc, tx, tyText, tw, (int)(20 * m.dpi), title, g_sfRow, C_WHITE);
    if (desc && desc[0]) {
        if (descWrap)
            // 盒顶从 26 上移到 18 DIP、盒高保持 32 DIP：单行时描述墨迹中心仍是 块顶+34dpi，
            // 与不折行的行完全同高；真折成两行时第二行自然往下走（GDI+ 侧开了 NoClip 不裁）。
            // 原先直接用「盒顶 26 + 高 32」，单行也会被居中到 块顶+42dpi —— 于是同一页里
            // 「小键盘按钮」行的标题↔描述间距比上面几行大 8 DIP（实机肉眼可见，实测 +14px）。
            DrawTextL(dc, tx, tyText + (int)(18 * m.dpi), tw, (int)(32 * m.dpi), desc, g_sfBase, C_DIM, TRUE);
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

// 「按键图标样式」的两段文案与控件矩形：绘制 / 命中 / 点击必须共用（画与热区同源）。
// 只有两段：文字 / 图标+文字 —— 纯图标（键面只剩一个裸图形，没有文字兜底）已按实机反馈去掉。
// 段下标与 g_keyIconStyle 不再相等，收在下面两个小函数里，别在调用点各写一遍映射。
#define KEYICON_SEG_COUNT 2
static int KeyIconStyleOfSeg(int seg) { return (seg == 1) ? 2 : 0; }
static int KeyIconSegOfStyle(int style) { return (style == 2) ? 1 : 0; }

static void KeyIconSegItems(const wchar_t* out[KEYICON_SEG_COUNT]) {
    out[0] = T(L"文字", L"Text");
    out[1] = T(L"图标+文字", L"Icon+Text");
}

static RECT KeyIconSegRect(const SettingsMetrics& m, const wchar_t* items[KEYICON_SEG_COUNT]) {
    RECT row = SettingsRowRect(m, 1);
    int w = SegmentedWidth(m, items, KEYICON_SEG_COUNT);
    int x = row.right - (int)(20 * m.dpi) - w;
    int y = SettingsComboY(m, row);
    RECT r = {x, y, x + w, y + m.comboH};
    return r;
}

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
    // 常规 Tab 行序（自动收起行仅在自动呼出开启时存在）：
    //   g_af 开：0=自动呼出 1=自动收起 2/3=关闭按钮/记住选择 4=功能键行 5=Shift符号 6=界面语言
    //   g_af 关：0=自动呼出 1/2=关闭按钮/记住选择 3=功能键行 4=Shift符号 5=界面语言
    // 布局 Tab 行序：0=键盘布局 1=按键图标样式 2=Fn 网页布局 3=小键盘按钮
    int rowIndex;
    if (hit == S_HIT_AUTO) rowIndex = 0;
    else if (hit == S_HIT_AUTOHIDE) rowIndex = 1;
    else if (hit == S_HIT_FNWEB) rowIndex = 2;
    else if (hit == S_HIT_NPBTN) rowIndex = 3;
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
    // 开关与「开/关」文字跟 tile / 文字块用同一条基准（行的头部），
    // 不按整行居中 —— 否则描述折行的那一行里，开关会比标题低 8 DIP。
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
// 卡片内边距 16 / 20、堆叠间距 16、章节标题「4×16 主色圆角条 + 标签」、
// 行「标题 + 说明 + 右侧 34 DIP 圆钮」、许可行「标签 + MIT + 查看按钮」。
// 下面全部是 DIP，实机再乘 dpi。
static int AboutPadY(const SettingsMetrics& m) { return (int)(16 * m.dpi); }      // .card-pad 上下
static int AboutPadX(const SettingsMetrics& m) { return (int)(20 * m.dpi); }      // .card-pad 左右
static int AboutGap(const SettingsMetrics& m) { return (int)(10 * m.dpi); }       // 卡与卡之间的间距
static int AboutSectionH(const SettingsMetrics& m) { return (int)(16 * m.dpi); }  // 章节行高 = 主色条高

// 页内一行：上 12 + 内容 36 + 下 12；**最后一行只给下 2** —— 制作工具的 form_card 是
// `.field{padding:12px 0}` 配 `:last-child{padding-bottom:2px}`。末行多出来的 10 DIP 会变成
// 卡片底部的死白（一行字下面空 24 DIP，肉眼就是「卡片下半边空着」）。
// 内容 36 = 标题盒 15 + 说明盒顶偏移 23 + 说明盒 13：偏移取 23 而不是 20，
// 是因为本窗口的行标题（g_sfCtrl = 11pt ≈ 14.7 DIP）墨迹比制作工具的 14px 略高，
// 偏移 20 时两行墨迹只隔 4.6 DIP（制作工具 8.6），肉眼就是「标题和说明糊在一起」。
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

// 链接卡现在只有「项目地址」一行（「问题反馈」已按实机反馈去掉）；末行只给下 2 DIP，
// 与 AboutRowH 的 last 规则一致
static int AboutLinksCardH(const SettingsMetrics& m) {
    return AboutPadY(m) * 2 + AboutRowH(m, TRUE);
}

// 许可行：上 12 + 按钮 32 + 下 2；卡高 = 上下 16 + 行。
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
    RECT sec2;    // 「开源许可」章节标题行
    RECT card3;   // 许可卡（一行）
    int  mark;    // 矢量键盘标识边长
};

static AboutLayout GetAboutLayout(const SettingsMetrics& m) {
    AboutLayout a = {};
    int padY = AboutPadY(m), gap = AboutGap(m);
    int y;

    // 身份标识 56 DIP（= 制作工具的 ABOUT_TILE）：卡高 = 上下 16 + 标识 56 = 88，
    // 标识正好落在内边距上；「标题 + 说明」的文字块（26 + 6 + 18 = 50）按卡纵心居中，
    // 与标识同高同轴 —— 见 SettingsDraw 里为什么不能再拿行盒中心去对齐。
    a.mark = (int)(56 * m.dpi);
    a.card1.left = m.contentX;
    a.card1.top = m.contentY;
    a.card1.right = m.contentX + m.contentW;
    a.card1.bottom = a.card1.top + padY * 2 + a.mark;

    // 身份卡之后直接接链接卡（原来中间还有一节「社区交流」标题，已按实机反馈去掉）；
    // 「开源许可」保留章节标题，节奏仍是「卡 → 间距 16 → 章节 → 间距 16 → 卡」。
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

// 章节标题：4×16 主色圆角条 + 14 DIP 标题（制作工具 widgets::section 的同款语法：
// 条 4×16 / 圆角 2 / 间距 10 / 文字取 title-active）。主色实心条是整页唯一一处「不描边」的
// 强调块，分组由它说清楚，不再靠卡片之间空一行来表达。
static void DrawAboutSection(HDC dc, const SettingsMetrics& m, const RECT& r, const wchar_t* label) {
    int barW = (int)(4 * m.dpi), barH = (int)(16 * m.dpi);
    int by = r.top + ((r.bottom - r.top) - barH) / 2;
    DrawRoundRect(dc, r.left, by, barW, barH, C_HOT, C_HOT, (int)(2 * m.dpi));
    int lx = r.left + barW + (int)(10 * m.dpi);
    DrawTextL(dc, lx, r.top, r.right - lx, r.bottom - r.top, label, g_sfSec, C_TITLE);
}

// 链接行：左「标题 + 说明」，右一枚 34 DIP 主色淡底圆钮，圆钮里的图标 = 去哪里。
//
// ⚠ 这里刻意不再用「行首图标 tile + 通用外链箭头」那一套（那是旧样式，照的是制作工具改版前
//   的关于页）：行首 tile 和右侧箭头说的是同一件事（「这是个链接」，说两遍），说明位又放网址，
//   整行读下来是「地址 + 打开」—— 得先认字才知道点去哪儿。改版后的形态是
//   「标题 + 一句说明 + 目的地自己的图标」：圆圈里的 GitHub 标一眼就够，说明句用来讲清用途。
//   命中区仍是整行（触摸场景下按钮不能只有 34×34 DIP）。
static void DrawAboutLinkRow(HDC dc, const SettingsMetrics& m, const RECT& row,
                             int iconId, const wchar_t* title, const wchar_t* desc, BOOL hover) {
    if (hover) DrawSettingsRowHover(dc, m, row);

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
            DrawSettingRowContent(dc, m, ra, HKICON_CLOCK, NULL,
                                  T(L"自动收起", L"Auto Hide"),
                                  T(L"收起键盘后在同一输入框内不自动弹出", L"Stay hidden after minimizing in the same input"),
                                  g_sHov == S_HIT_AUTOHIDE, SettingsSwitchTextRight(m, ra));
            DrawSettingSwitch(dc, m, ra, g_afAutoHide, S_HIT_AUTOHIDE);
        }

        RECT r1 = SettingsRowRect(m, closeRow);
        const wchar_t* cseg[2];
        int csegn = CloseSegItems(cseg);
        RECT csegR = RowSegRect(m, r1, cseg, csegn);
        DrawSettingRowContent(dc, m, r1, HKICON_CLOSE, NULL,
                              T(L"关闭按钮", L"Close Button"),
                              T(L"选择关闭窗口时执行的操作", L"Choose what happens when the window is closed"),
                              g_sHov == S_HIT_CLOSE_DROP, csegR.left);
        DrawSegmented(dc, m, csegR, cseg, csegn, g_closeToTray ? 1 : 0);

        RECT r2 = SettingsRowRect(m, closeRow + 1);
        DrawSettingRowContent(dc, m, r2, HKICON_CHECK, NULL,
                              T(L"记住我的选择", L"Remember My Choice"),
                              T(L"记住关闭按钮的操作，下次直接执行", L"Remember the action and skip asking next time"),
                              g_sHov == S_HIT_REMEMBER, SettingsSwitchTextRight(m, r2));
        DrawSettingSwitch(dc, m, r2, g_rememberClose, S_HIT_REMEMBER);

        RECT r = SettingsRowRect(m, closeRow + 2);
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
        // 布局 Tab：0=键盘布局 1=按键图标样式 2=Fn 网页布局 3=小键盘按钮
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

        // 按键图标样式：分段控件二选一，改完立即重绘主键盘
        r = SettingsRowRect(m, 1);
        const wchar_t* seg[KEYICON_SEG_COUNT];
        KeyIconSegItems(seg);
        RECT segR = KeyIconSegRect(m, seg);
        DrawSettingRowContent(dc, m, r, HKICON_CARET, NULL,
                              T(L"按键图标样式", L"Key Icon Style"),
                              T(L"键面显示为文字，或图标与文字并存", L"Draw key faces as text, or icon plus text"),
                              FALSE, segR.left);
        DrawSegmented(dc, m, segR, seg, KEYICON_SEG_COUNT, KeyIconSegOfStyle(g_keyIconStyle));

        r = SettingsRowRect(m, 2);
        DrawSettingRowContent(dc, m, r, HKICON_GLOBE, NULL,
                              T(L"Fn 网页布局", L"Fn Web Layout"),
                              SettingsRowDescText(3, 2),
                              g_sHov == S_HIT_FNWEB, SettingsSwitchTextRight(m, r),
                              SettingsRowDescTwoLines(m, 3, 2));
        DrawSettingSwitch(dc, m, r, g_fnWebLayout, S_HIT_FNWEB);

        r = SettingsRowRect(m, 3);
        DrawSettingRowContent(dc, m, r, HKICON_NUMPAD, NULL,
                              T(L"小键盘按钮", L"Numpad Button"),
                              SettingsRowDescText(3, 3),
                              g_sHov == S_HIT_NPBTN, SettingsSwitchTextRight(m, r),
                              SettingsRowDescTwoLines(m, 3, 3));
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
        // 语法整体照制作工具（PanDa PE）改版后的关于页：章节标题带主色条、行是「标题 + 说明
        // + 目的地图标圆钮」、许可是独立一行；卡片内边距 16/20、堆叠间距 16，见 GetAboutLayout。
        AboutLayout al = GetAboutLayout(m);
        DrawRoundRect(dc, al.card1.left, al.card1.top,
                      al.card1.right - al.card1.left, al.card1.bottom - al.card1.top,
                      C_KEY, C_KEY, (int)(16 * m.dpi));

        // 身份标识：矢量 KeyboardMark（主色键盘体 + 挖空键块），不用位图。
        //
        // 纵向**居中于卡片本身**，而不是居中于「标题 + 描述」的行盒：
        //   卡高 88 = 上下内边距 16 + 标识 56，标识盒正好落在内边距上；
        //   文字块（标题盒 26 + 间隙 6 + 描述盒 18 = 50）也按卡纵心居中，两者同轴。
        //
        //   ⚠ 不要拿行盒中心去对齐。行盒中心比文字**墨迹**中心高约 2 DIP（标题带降部、
        //     墨迹向上溢出，描述墨迹只占 18 DIP 行盒里的 13 DIP），照行盒中心摆图标会低 3 DIP：
        //     实测图标上留白 50px、下留白 38px，肉眼就是「图标陷在卡片下半边」。
        //   直接由卡高算，改字号或改卡高都自洽。
        int my = al.card1.top + ((al.card1.bottom - al.card1.top) - al.mark) / 2;
        DrawHkIcon(dc, (float)(al.card1.left + AboutPadX(m)), (float)my, (float)al.mark,
                   HkIcon(HKICON_KEYBOARDMARK), C_HOT, C_ON_PRIMARY);

        // 产品名用 g_sfSec（14pt ≈ 18.7 DIP）而不是页面标题那一档：
        // 制作工具的关于页是 名称 20 / 页标题 26（0.77），本窗口若两者同号（都 18pt = 24 DIP），
        // 卡里的名字会和页面大标题打架。文字块 = 名 22 + 间隙 6 + 版本 18 = 46，按卡纵心居中。
        int tx = al.card1.left + AboutPadX(m) + al.mark + (int)(18 * m.dpi);
        int ty = al.card1.top + ((al.card1.bottom - al.card1.top) - (int)(46 * m.dpi)) / 2;
        int tw = al.card1.right - AboutPadX(m) - tx;
        DrawTextL(dc, tx, ty, tw, (int)(22 * m.dpi),
                  T(L"HKeyboard 轻键", L"HKeyboard"), g_sfSec, C_WHITE);
        wchar_t meta[96];
        // 版本串 = 内部版本号 + 构建日期（北京时间），例如 "v2.0_20261002"
        swprintf(meta, 96, T(L"轻量屏幕键盘 · v%hs_%ls (%ls)", L"Lightweight screen keyboard · v%hs_%ls (%ls)"),
                 VER_FILEVERSION_STR, HK_BUILD_DATE, ArchName());
        DrawTextL(dc, tx, ty + (int)(28 * m.dpi), tw, (int)(18 * m.dpi), meta, g_sfMeta, C_DIM);

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

        r = SettingsRowRect(m, closeRow + 2);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_FKEYS;
        r = SettingsRowRect(m, closeRow + 3);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_SHIFTSYM;

        r = SettingsRowRect(m, closeRow + 4);
        { const wchar_t* it[2]; int n = LangSegItems(it);
          RECT sr = RowSegRect(m, r, it, n);
          if (x >= sr.left && x < sr.right && y >= sr.top && y < sr.bottom) return S_HIT_LANG_DROP; }
    } else if (g_sTab == 3) {
        // 布局 Tab：0=键盘布局 1=按键图标样式 2=Fn 网页布局 3=小键盘按钮
        // （原第 5 行「Fn + Tab 切换小键盘」已下线：全尺寸下显隐数字区只留标题栏那一个入口）
        RECT r;
        // 下拉列表优先命中
        r = SettingsRowRect(m, 0);
        { const wchar_t* it[3]; int n = LayoutSegItems(it);
          RECT sr = RowSegRect(m, r, it, n);
          if (x >= sr.left && x < sr.right && y >= sr.top && y < sr.bottom) return S_HIT_LAYOUT_DROP; }

        // 按键图标样式：整条分段控件一个命中码，段下标在点击时按 x 算
        {
            const wchar_t* seg[KEYICON_SEG_COUNT];
            KeyIconSegItems(seg);
            RECT sr = KeyIconSegRect(m, seg);
            if (x >= sr.left && x < sr.right && y >= sr.top && y < sr.bottom) return S_HIT_KEYICON;
        }

        r = SettingsRowRect(m, 2);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_FNWEB;

        r = SettingsRowRect(m, 3);
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
            // 删除已废弃的自动隐藏延迟键（值传 NULL 即删除）
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
    g_keyIconStyle = IniGetInt(L"Keyboard", L"KeyIconStyle", 2);
    // 0=文字 2=图标+文字（默认）。
    // 旧配置里的 1（纯图标）已取消 —— 迁移到 2，别让它落回「文字」丢掉用户的选择。
    if (g_keyIconStyle == 1) g_keyIconStyle = 2;
    if (g_keyIconStyle != 2) g_keyIconStyle = 0;
    g_shiftSymbols = (IniGetInt(L"General", L"ShiftSymbols", 1) != 0);
    g_hideDelayMs = 1000;   // 自动隐藏延迟固定 1 秒
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
        // 画框选择：直接按点击的段应用（原来的「展开下拉 → 再点选项」两级已经作废）
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
    if (hit == S_HIT_KEYICON) {
        // 分段控件：按 x 定位具体段；改完存盘并立即重绘主键盘（三窗口联动的关键一步）
        SettingsMetrics km = GetSettingsMetrics(hWnd);
        const wchar_t* seg[KEYICON_SEG_COUNT];
        KeyIconSegItems(seg);
        RECT sr = KeyIconSegRect(km, seg);
        int idx = SegmentedHitIndex(km, sr, seg, KEYICON_SEG_COUNT, x);
        if (idx >= 0) {
            g_keyIconStyle = KeyIconStyleOfSeg(idx);
            IniSetInt(L"Keyboard", L"KeyIconStyle", g_keyIconStyle);
            if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
        }
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
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

static void PromptDraw(HDC dc, HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    int W = rc.right, H = rc.bottom;
    double dpi = GetSystemDpiScale();
    int hdr = (int)(36 * dpi);
    (void)hWnd;
    // 标题区不再铺独立底色，与窗口背景/材质一体化
    DrawTextL(dc, 14, 0, W - 90, hdr, T(L"关闭轻键", L"Close HKeyboard"), g_sfCtrl, C_WHITE);
    int bw = (int)(26 * dpi), bh = hdr - (int)(12 * dpi);
    int bx = W - bw - 8, by = (hdr - bh) / 2;
    // 与设置页 / 主键盘标题栏一致：平时不铺底，悬停才给一层 btn_regular_bg_hover
    if (g_pHov == P_HIT_CLOSE) {
        DrawRoundRect(dc, bx, by, bw, bh, C_REGULAR_HOV, C_REGULAR_HOV, (int)(6 * dpi));
    }
    {
        int sz = (int)(18 * dpi);
        DrawHkIcon(dc, (float)(bx + (bw - sz) / 2), (float)(by + (bh - sz) / 2), (float)sz,
                   HkIcon(HKICON_CLOSE), C_DIM, C_DIM);
    }

    int x0 = 20, y = hdr + 12, cw = W - 40;
    int rowH = (int)(24 * dpi);
    DrawTextL(dc, x0, y, cw, (int)(20 * dpi), T(L"请选择关闭方式：", L"Choose how to close:"), g_sfBase, C_DIM); y += (int)(22 * dpi);
    DrawRadio(dc, x0 + (int)(8 * dpi), y + rowH / 2, (int)(7 * dpi), g_pChoice == 0, C_BG);
    DrawTextL(dc, x0 + (int)(26 * dpi), y, cw - (int)(26 * dpi), rowH, T(L"直接退出程序", L"Exit program directly"), g_sfCtrl, C_WHITE);
    y += rowH;
    DrawRadio(dc, x0 + (int)(8 * dpi), y + rowH / 2, (int)(7 * dpi), g_pChoice == 1, C_BG);
    DrawTextL(dc, x0 + (int)(26 * dpi), y, cw - (int)(26 * dpi), rowH, T(L"隐藏到系统托盘", L"Hide to system tray"), g_sfCtrl, C_WHITE);
    y += rowH + (int)(4 * dpi);
    int swW = (int)(40 * dpi), swH = (int)(20 * dpi);
    int swX = x0 + cw - swW;
    DrawSwitch(dc, swX, y + (rowH - swH) / 2, swW, swH, g_pRemember);
    DrawTextL(dc, x0, y, (swX - 12) - x0, rowH, T(L"记住我的选择", L"Remember my choice"), g_sfCtrl, C_WHITE);
    y += rowH + (int)(8 * dpi);
    int bw2 = (int)(84 * dpi), bh2 = (int)(28 * dpi);
    int bxCancel = W - 20 - bw2;                    // 按钮右对齐
    int bxOk = bxCancel - (int)(12 * dpi) - bw2;
    DrawRoundRect(dc, bxOk, y, bw2, bh2, (g_pHov == P_HIT_OK) ? C_HOVER : C_HOT, C_KEY_BORDER, 6);
    DrawTextC(dc, bxOk, y, bw2, bh2, T(L"确定", L"OK"), g_sfCtrl, C_ON_PRIMARY);
    DrawRoundRect(dc, bxCancel, y, bw2, bh2, (g_pHov == P_HIT_CANCEL) ? C_HOVER : C_KEY, C_KEY_BORDER, 6);
    DrawTextC(dc, bxCancel, y, bw2, bh2, T(L"取消", L"Cancel"), g_sfCtrl, C_WHITE);
}

static int PromptHitTest(HWND hWnd, int x, int y) {
    RECT rc; GetClientRect(hWnd, &rc);
    int W = rc.right;
    double dpi = GetSystemDpiScale();
    int hdr = (int)(36 * dpi);
    int bw = (int)(26 * dpi), bh = hdr - (int)(12 * dpi);
    int bx = W - bw - 8, by = (hdr - bh) / 2;
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) return P_HIT_CLOSE;
    int x0 = 20, yy = hdr + 12, cw = W - 40;
    int rowH = (int)(24 * dpi);
    yy += (int)(22 * dpi);
    if (x >= x0 && x < x0 + cw && y >= yy && y < yy + rowH) return P_HIT_DIRECT;
    yy += rowH;
    if (x >= x0 && x < x0 + cw && y >= yy && y < yy + rowH) return P_HIT_TRAY;
    yy += rowH + (int)(4 * dpi);
    if (x >= x0 && x < x0 + cw && y >= yy && y < yy + rowH) return P_HIT_REMEMBER;
    yy += rowH + (int)(8 * dpi);
    int bw2 = (int)(84 * dpi), bh2 = (int)(28 * dpi);
    int bxCancel = W - 20 - bw2;                    // 与绘制一致（右对齐）
    int bxOk = bxCancel - (int)(12 * dpi) - bw2;
    if (x >= bxOk && x < bxOk + bw2 && y >= yy && y < yy + bh2) return P_HIT_OK;
    if (x >= bxCancel && x < bxCancel + bw2 && y >= yy && y < yy + bh2) return P_HIT_CANCEL;
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
    int w = (int)(300 * dpi), h = (int)(190 * dpi);
    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + ((work.right - work.left) - w) / 2;
    int y = work.top + ((work.bottom - work.top) - h) / 2;
    g_closePromptHwnd = CreateWindowExW(WS_EX_TOPMOST, L"HKeyboardClosePrompt", T(L"关闭轻键", L"Close HKeyboard"), WS_POPUP,
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
        ToggleKB();
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

    const LONG ROLE_SYSTEM_TEXT_LOCAL = 0x2A;
    const LONG ROLE_SYSTEM_SPINBUTTON_LOCAL = 0x34;
    if (role.lVal != ROLE_SYSTEM_TEXT_LOCAL && role.lVal != ROLE_SYSTEM_SPINBUTTON_LOCAL) return FALSE;

    if (SUCCEEDED(acc->get_accState(child, &state)) && state.vt == VT_I4) {
        const LONG STATE_SYSTEM_READONLY_LOCAL = 0x40;
        if ((state.lVal & STATE_SYSTEM_READONLY_LOCAL) != 0) return FALSE;
    }
    return TRUE;
}

static BOOL AccessibleHasEditableFocus(IAccessible* acc, int depth, ULONG_PTR* token) {
    if (!acc || depth > 3) return FALSE;
    VARIANT focus;
    ZeroMemory(&focus, sizeof(focus));
    if (FAILED(acc->get_accFocus(&focus))) return FALSE;

    VARIANT self;
    ZeroMemory(&self, sizeof(self));
    self.vt = VT_I4;
    self.lVal = 0; // CHILDID_SELF

    if (focus.vt == VT_I4) {
        if (AccessibleRoleIsEditable(acc, focus)) {
            if (token) *token = AccessibleIdentityToken(acc, focus) ^ ((ULONG_PTR)(DWORD)focus.lVal << 4);
            return TRUE;
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

static BOOL IsAccessibleInputWindow(HWND hWnd, ULONG_PTR* token) {
    AccessibleObjectFromWindowProc proc = GetAccessibleObjectFromWindow();
    if (!proc || !EnsureAccessibilityCom() || !hWnd) return FALSE;

    IAccessible* root = NULL;
    HRESULT hr = proc(hWnd, OBJID_CLIENT, IID_IAccessibleLocal, (void**)&root);
    if (FAILED(hr) || !root) return FALSE;

    ULONG_PTR detected = 0;
    BOOL result = AccessibleHasEditableFocus(root, 0, &detected);
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

    // Chromium/Electron（NTQQ、Chrome、Edge 等）：网页内容获得键盘焦点时
    // Win32 焦点落在渲染宿主/组件窗口上，视为输入区域
    if (strstr(buf, "Chrome_RenderWidgetHostHWND") || strstr(buf, "Chrome_WidgetWin"))
        return TRUE;

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
    if (haveGuiInfo) {
        if (IsInputControl(focus)) {
            g_detectedInputToken = (ULONG_PTR)focus;
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

    ULONG_PTR token = 0;
    if (IsAccessibleInputWindow(focus, &token) || (focus != fg && IsAccessibleInputWindow(fg, &token))) {
        g_detectedInputToken = token ? token : (ULONG_PTR)focus;
        return focus;
    }
    return NULL;
}

static void UpdateAutoVisibility() {
    if (!g_af || !g_hWnd) return;
    // 窗口滑动动画期间不做焦点评估：焦点探测可能触发跨进程 COM 调用，
    // 在启动动画中执行会造成可感知的卡顿；动画结束后下个轮询周期再评估。
    if (g_mainMotion.active) return;

    HWND input = GetFocusedInputControl();
    if (input) {
        g_lastNonInput = 0;
        g_manualShow = FALSE;
        // 自动收起开启：用户刚在该输入框中手动收起时不回弹；
        // 焦点换到其它输入控件后恢复正常自动呼出（token 同源比较）
        if (g_afAutoHide && g_userHidInInput &&
            g_hiddenInputToken && g_detectedInputToken == g_hiddenInputToken)
            return;
        g_userHidInInput = FALSE;
        if (!g_manualHide && !g_vis) ShowKB(TRUE, FALSE);
        return;
    }

    HWND fg = GetForegroundWindow();
    if (fg == g_settingsHwnd || fg == g_closePromptHwnd) return;

    g_userHidInInput = FALSE;   // 焦点已离开输入框，清除手动收起标记
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
    (void)hook; (void)hwnd; (void)idChild;
    (void)dwEventThread; (void)dwmsEventTime;
    if (!g_af || !g_hWnd) return;
    if (event == EVENT_OBJECT_FOCUS || event == EVENT_SYSTEM_FOREGROUND ||
        (event == EVENT_OBJECT_SHOW && idObject == OBJID_CARET))
        PostMessage(g_hWnd, WM_FOCUS_EVENT, 0, 0);
}

// ===== 实体键盘状态监控（WH_KEYBOARD_LL） =====
// 同步显示：实体 Win/Shift/Caps 键做到哪一步，程序显示就对应哪一步；
// Fn 预留接口（多数键盘 Fn 不产生按键事件，后续按需扩展 g_physFn）。
static LRESULT CALLBACK PhysKeyHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        const KBDLLHOOKSTRUCT* p = (const KBDLLHOOKSTRUCT*)lParam;
        // 忽略本程序 SendInput 注入的事件，避免与虚拟键逻辑互相干扰
        if (!(p->flags & LLKHF_INJECTED)) {
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
        case HDR_MIN: UserHideKeyboard(); break;   // 手动收起（自动收起开启时同输入框内不回弹）
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
    SetCapture(hWnd);   // 捕获鼠标，防止开始菜单等出现时抢走鼠标抬起消息导致键一直高亮
    const KeyDef* k = &g_keys[ki];
    DoKeyAction(k);

    if (k->vk == 0x08 || k->vk == 0x2E || k->vk == 0x20 || k->type == K_ARROW) {
        g_repeatKeyIdx = ki;
        SetTimer(hWnd, TIMER_REPEAT, 350, NULL);
    }
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
        } else if (w == TIMER_REPEAT) {
            SetTimer(hWnd, TIMER_REPEAT, 40, NULL);
            if (g_pk >= 0 && g_pk == g_repeatKeyIdx) {
                const KeyDef* k = &g_keys[g_pk];
                DoKeyAction(k);
            } else {
                KillTimer(hWnd, TIMER_REPEAT);
            }
        } else if (w == TIMER_FOCUS) {
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
        case ID_MENU_TOGGLE: ToggleKB(); break;
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
            ToggleKB();
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

    BOOL isTouch = IsTouchDevice();

    if (tOnly && !isTouch) return 0;

    g_mutex = CreateMutexW(0, FALSE, L"HKeyboard_Mutex");
    if (g_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
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
