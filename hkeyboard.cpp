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

enum KeyType {
    K_NORMAL, K_LETTER, K_MOD, K_CAPS,
    K_SPECIAL, K_ARROW, K_SPACE, K_HIDE, K_DOCK, K_MIN, K_CLOSE
};

struct KeyDef { int x, y, w, h; short vk; KeyType type; };

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
BOOL        g_npTabToggle = TRUE;      // 完整布局：Tab 键显示/隐藏数字区
BOOL        g_npHidden = FALSE;        // 完整布局：数字区隐藏（用户按 Tab / 标题栏按钮，持久化）
BOOL        g_npHiddenAuto = FALSE;    // 完整布局：窗口过窄时自动隐藏数字区（不持久化）
BOOL        g_fnWebLayout = FALSE;     // 按 Fn 切换到上网常用布局（否则为数字行 F1~F12 层）
BOOL        g_showFKeys = FALSE;       // 顶部显示 F1~F12 键
BOOL        g_shiftSymbols = TRUE;     // 按 Shift 时显示特殊符号（否则显示数字）
int         g_keyIconStyle = 0;        // 按键图标样式：0=文字（默认）1=图标 2=图标+文字（ini Keyboard/KeyIconStyle）
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
static HFONT g_sfBig = 0, g_sfRow = 0, g_sfCtrl = 0, g_sfBase = 0, g_sfMeta = 0;   // 设置/关闭窗口固定字号字体
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

static int AddKey(int x, int y, int w, int h, short vk, KeyType type) {
    if (g_nk >= MAX_KEYS) return g_nk;
    KeyDef* k = &g_keys[g_nk++];
    k->x = x; k->y = y; k->w = w; k->h = h; k->vk = vk; k->type = type;
    return g_nk;
}

// 小键盘布局（4 列 × 5 行，支持跨行/跨列）
static void BuildNumpad(int y) {
    int colW = (KEY_AREA_W - 3 * g_keyGap) / 4;
    int x = KEY_AREA_X;

    // Row 0: NumLock, /, *, -
    {
        short v[4] = {0x90, 0x6F, 0x6A, 0x6D};
        KeyType t[4] = {K_SPECIAL, K_NORMAL, K_NORMAL, K_NORMAL};
        int cx = x;
        for (int i = 0; i < 4; i++) { AddKey(cx, y, colW, g_keyHeight, v[i], t[i]); cx += colW + g_keyGap; }
    }
    y += g_keyHeight + g_keyGap;

    // Row 1: 7, 8, 9, +（+ 跨 2 行）
    {
        int h2 = g_keyHeight * 2 + g_keyGap;
        int cx = x;
        for (int i = 0; i < 3; i++) { AddKey(cx, y, colW, g_keyHeight, (short)(0x67 + i), K_NORMAL); cx += colW + g_keyGap; }
        AddKey(cx, y, colW, h2, 0x6B, K_NORMAL);
    }
    y += g_keyHeight + g_keyGap;

    // Row 2: 4, 5, 6
    {
        int cx = x;
        for (int i = 0; i < 3; i++) { AddKey(cx, y, colW, g_keyHeight, (short)(0x64 + i), K_NORMAL); cx += colW + g_keyGap; }
    }
    y += g_keyHeight + g_keyGap;

    // Row 3: 1, 2, 3, Enter（Enter 跨 2 行）
    {
        int h2 = g_keyHeight * 2 + g_keyGap;
        int cx = x;
        for (int i = 0; i < 3; i++) { AddKey(cx, y, colW, g_keyHeight, (short)(0x61 + i), K_NORMAL); cx += colW + g_keyGap; }
        AddKey(cx, y, colW, h2, 0x0D, K_SPECIAL);
    }
    y += g_keyHeight + g_keyGap;

    // Row 4: 0（跨 2 列）, .
    {
        int cx = x;
        AddKey(cx, y, colW * 2 + g_keyGap, g_keyHeight, 0x60, K_NORMAL);
        cx += colW * 2 + g_keyGap + g_keyGap;
        AddKey(cx, y, colW, g_keyHeight, 0x6E, K_NORMAL);
    }
}

// 完整键盘布局（104 键）：主区 + 导航区 + 数字区（6 行：F 行 + 主区 5 行）
// ============================================================================

// 数字区当前是否隐藏：「用户手动按 Tab / 标题栏按钮」与「窗口过窄自动隐藏」是两件事，
// 合成一个生效值再往外用 —— 只在 G 处改一份，绘制 / 命中 / 帧缓存签名才不会互相打架。
static BOOL NumpadHidden() { return (g_npHidden || g_npHiddenAuto); }

// 窄屏自适应：键帽 44px 是触摸安全下限，反解「22u + 2gap + 2×键区边距」得 1090 DIP；
// 低于这个宽度就把数字区收起来（复用同一套显隐机制，而不是让键位溢出窗口）。
// 只在全尺寸布局下生效；由窗口宽度推导，因此不落盘。
static void UpdateNumpadAuto(int ww, double dpiScale) {
    if (g_layoutMode != 2) { g_npHiddenAuto = FALSE; return; }
    g_npHiddenAuto = (ww < (int)(1090 * dpiScale));
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
    for (int r = 0; r < 6; r++) {
        int yy = y + r * (KH + gap);
        if (r == 4 && webFn) {                 // Fn 网页层换掉 Shift 行
            for (int i = 0; i < kFullWebShiftN; i++)
                FullPut(0.0f, kFullWebShift[i], yy, KH, u, gap, bc, nb);
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

// Fn 网页布局层：整体结构跟随当前布局样式（全尺寸/常用），
// 仅行1 数字键换为 F1~F12、行4 字母键换为网址后缀键
static void BuildFnSurf(int y, double dpiScale, double scaleX) {
    // Fn 网页层结构跟随全尺寸布局（含 Menu 键）：行2 带 Del、行5 带 Menu
    // Row 1: Esc, `, F1~F12, Backspace (15 keys)
    {
        int wEsc = (int)(50 * dpiScale * scaleX);
        int wBksp = (int)(104 * dpiScale * scaleX);   // 要放得下整词 Backspace（不再缩写成 Bksp）
        int fixed = wEsc + wBksp;
        int aw = (KEY_AREA_W - fixed - 14 * g_keyGap) / 13;
        int rem = KEY_AREA_W - fixed - 14 * g_keyGap - aw * 13;
        int x = KEY_AREA_X;
        AddKey(x, y, wEsc, g_keyHeight, 0x1B, K_SPECIAL); x += wEsc + g_keyGap;
        for (int i = 0; i < 13; i++) {
            int w = aw + (i < rem ? 1 : 0);
            if (i == 0) AddKey(x, y, w, g_keyHeight, 0xC0, K_NORMAL);
            else AddKey(x, y, w, g_keyHeight, (short)(0x70 + i - 1), K_NORMAL);   // F1~F12
            x += w + g_keyGap;
        }
        AddKey(x, y, wBksp, g_keyHeight, 0x08, K_SPECIAL);
        y += g_keyHeight + g_keyGap;
    }

    // Row 2: Tab, q-p, [, ], \, Del (15 keys)
    {
        int wTab = (int)(68 * dpiScale * scaleX);
        int wDel = (int)(68 * dpiScale * scaleX);
        int fixed = wTab + wDel;
        int aw = (KEY_AREA_W - fixed - 14 * g_keyGap) / 13;
        int rem = KEY_AREA_W - fixed - 14 * g_keyGap - aw * 13;
        int w[15]; w[0] = wTab;
        for (int i = 1; i <= 13; i++) w[i] = aw + (i <= rem ? 1 : 0);
        w[14] = wDel;
        short v[15] = {0x09,0x51,0x57,0x45,0x52,0x54,0x59,0x55,0x49,0x4F,0x50,0xDB,0xDD,0xDC,0x2E};
        KeyType t[15] = {K_SPECIAL,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_NORMAL,K_NORMAL,K_NORMAL,K_SPECIAL};
        int x = KEY_AREA_X;
        for (int i = 0; i < 15; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
        y += g_keyHeight + g_keyGap;
    }

    // Row 3: Caps, a-l, ;, ', Enter (13 keys)，与全尺寸布局一致
    {
        int wCaps = (int)(80 * dpiScale * scaleX);
        int wEnter = (int)(90 * dpiScale * scaleX);   // Win11 风格：自然宽度，不强制竖列对齐
        int fixed = wCaps + wEnter;
        int aw = (KEY_AREA_W - fixed - 12 * g_keyGap) / 11;
        int rem = KEY_AREA_W - fixed - 12 * g_keyGap - aw * 11;
        int w[13]; w[0] = wCaps;
        for (int i = 1; i <= 11; i++) w[i] = aw + (i <= rem ? 1 : 0);
        w[12] = wEnter;
        short v[13] = {0x14,0x41,0x53,0x44,0x46,0x47,0x48,0x4A,0x4B,0x4C,0xBA,0xDE,0x0D};
        KeyType t[13] = {K_CAPS,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_NORMAL,K_NORMAL,K_SPECIAL};
        int x = KEY_AREA_X;
        for (int i = 0; i < 13; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
        y += g_keyHeight + g_keyGap;
    }

    // Row 4: Shift, 网址后缀×6, ? , ., ↑, Shift (12 keys)
    {
        int wLSh = (int)(95 * dpiScale * scaleX);
        int wUp = (int)(52 * dpiScale * scaleX);   // 与 ↓ 同宽对齐
        int wRSh = (int)(52 * dpiScale * scaleX);   // 右 Shift 与 → 同宽，保证 ↓ 正对 ↑（十字对齐）
        int fixed = wLSh + wRSh + wUp;
        int aw = (KEY_AREA_W - fixed - 11 * g_keyGap) / 9;
        int rem = KEY_AREA_W - fixed - 11 * g_keyGap - aw * 9;
        int w[12]; w[0] = wLSh;
        for (int i = 1; i <= 9; i++) w[i] = aw + (i <= rem ? 1 : 0);
        w[10] = wUp; w[11] = wRSh;
        short v[12] = {0xA0, 0x200,0x201,0x202,0x203,0x204,0x205, 0xBF,0xBC,0xBE, 0x26, 0xA1};
        KeyType t[12] = {K_MOD, K_SPECIAL,K_SPECIAL,K_SPECIAL,K_SPECIAL,K_SPECIAL,K_SPECIAL, K_NORMAL,K_NORMAL,K_NORMAL, K_ARROW, K_MOD};
        int x = KEY_AREA_X;
        for (int i = 0; i < 12; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
        y += g_keyHeight + g_keyGap;
    }

    // Row 5: Fn, Ctrl, Win, Alt, 空格, Alt, Menu, Ctrl, ←, ↓, → (11 keys)
    {
        int wFn  = (int)(46 * dpiScale * scaleX);
        int wCtl = (int)(56 * dpiScale * scaleX);
        int wWin = (int)(46 * dpiScale * scaleX);
        int wAlt = (int)(58 * dpiScale * scaleX);
        int wMenu = (int)(56 * dpiScale * scaleX);
        int wArw = (int)(52 * dpiScale * scaleX);   // ← 与 ↑/↓ 同宽
        int wUp   = (int)(52 * dpiScale * scaleX);   // ↓ 与上方 ↑ 同宽
        int wRSh  = (int)(52 * dpiScale * scaleX);   // → 与右 Shift 同宽（十字对齐约束）
        int leftOfArrows = wFn + wCtl + wWin + wAlt + wAlt + wMenu + wCtl;
        int spaceW = KEY_AREA_W - leftOfArrows - wArw - wUp - wRSh - 10 * g_keyGap;
        if (spaceW < 60) spaceW = 60;
        int w[11] = {wFn, wCtl, wWin, wAlt, spaceW, wAlt, wMenu, wCtl, wArw, wUp, wRSh};
        short v[11] = {0, 0x11, 0x5B, 0x12, 0x20, 0x12, 0x5D, 0x11, 0x25, 0x28, 0x27};
        KeyType t[11] = {K_SPECIAL, K_MOD, K_SPECIAL, K_MOD, K_SPACE, K_MOD, K_MOD, K_MOD, K_ARROW, K_ARROW, K_ARROW};
        int x = KEY_AREA_X;
        for (int i = 0; i < 11; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
    }
}

static void BuildKeys() {
    g_nk = 0;

    double dpiScale = GetSystemDpiScale();
    double baseW = 980.0 * dpiScale;
    double baseH = 320.0 * dpiScale;

    UpdateNumpadAuto(g_ww, dpiScale);   // 窄屏自动收起数字区（在算 rows / 建键位之前）

    double scaleX = (double)g_ww / baseW;
    double scaleY = (double)g_wh / baseH;

    // 页头只随 DPI 缩放，**不随窗口高度缩放** —— 标题栏不该在窗口拉高时变厚。
    // 44 = 按钮上留白 10 + 按钮 28 + 6，剩下的下留白由第一排键 y 里的
    // `g_keyGap + 2` 补齐，所以「按钮底 → 第一排键」任何窗口尺寸下都稳定在约 12 DIP。
    g_headerH = (int)(44.0 * dpiScale); if (g_headerH < 34) g_headerH = 34;
    g_keyGap = (int)(4.0 * dpiScale * scaleX); if (g_keyGap < 2) g_keyGap = 2;
    // 键区左右留白 10 DIP。底部不能直接填 10：行高是整数除法，且第一排键的 y 里
    // 有 `g_keyGap + 2`，所以「实际」底边距 = bottomPad − g_keyGap − 2 + 余数。
    // 要让实际底边距也等于 10，设定值必须补回 g_keyGap + 2。
    g_keyAreaX = (int)(10 * dpiScale); if (g_keyAreaX < 6) g_keyAreaX = 6;
    int bottomPad = (int)(16 * dpiScale); if (bottomPad < 8) bottomPad = 8;

    // 行数：全尺寸 5 行 + 可选 F1~F12 顶行；小键盘 5 行；完整布局固定 6 行（F 行 + 主区 5 行）
    BOOL webSurf = g_fnLayer && g_fnWebLayout && g_layoutMode == 0;
    int rows = (g_layoutMode == 2) ? 6 : 5 + (g_layoutMode == 0 && g_showFKeys && !webSurf ? 1 : 0);
    g_keyHeight = (g_wh - g_headerH - bottomPad - (rows - 1) * g_keyGap) / rows;
    if (g_keyHeight < 20) g_keyHeight = 20;

    int y = g_headerH + g_keyGap + 2;

    if (g_layoutMode == 1) {   // 小键盘
        BuildNumpad(y);
        return;
    }

    if (g_layoutMode == 2) {   // 完整键盘（主区+导航区+数字区；Fn 网页层换 Shift 行网址键）
        BuildComplete(y, g_fnLayer && g_fnWebLayout);
        return;
    }

    // Fn 网页布局层（按 Fn 键切换，全尺寸布局下生效）
    if (g_fnLayer && g_fnWebLayout) {
        BuildFnSurf(y, dpiScale, scaleX);
        return;
    }

    // F1~F12 顶行（可选）：Esc, F1~F12, Del（F12 后面为 Del）
    if (g_showFKeys) {
        int wEsc = (int)(56 * dpiScale * scaleX);
        int wDel = (int)(56 * dpiScale * scaleX);
        int fixed = wEsc + wDel;
        int aw = (KEY_AREA_W - fixed - 13 * g_keyGap) / 12;
        int rem = KEY_AREA_W - fixed - 13 * g_keyGap - aw * 12;
        int x = KEY_AREA_X;
        AddKey(x, y, wEsc, g_keyHeight, 0x1B, K_SPECIAL); x += wEsc + g_keyGap;
        for (int i = 0; i < 12; i++) {
            int w = aw + (i < rem ? 1 : 0);
            AddKey(x, y, w, g_keyHeight, (short)(0x70 + i), K_NORMAL);
            x += w + g_keyGap;
        }
        AddKey(x, y, wDel, g_keyHeight, 0x2E, K_SPECIAL);   // Del
        y += g_keyHeight + g_keyGap;
    }

    // ===== Win10 屏幕键盘风格布局 =====
    // Row 0: Esc, `, 1-0, -, =, Backspace  (15 keys)；F 行开启时隐藏原 Esc
    {
        int wEsc = (int)(50 * dpiScale * scaleX);
        int wBksp = (int)(104 * dpiScale * scaleX);   // 要放得下整词 Backspace（不再缩写成 Bksp）
        if (g_showFKeys) {
            // 无 Esc：`, 1-0, -, =, Backspace (14 keys)
            int aw = (KEY_AREA_W - wBksp - 13 * g_keyGap) / 13;
            int rem = KEY_AREA_W - wBksp - 13 * g_keyGap - aw * 13;
            short v[14] = {0xC0,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,0xBD,0xBB,0x08};
            KeyType t[14] = {K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_SPECIAL};
            int x = KEY_AREA_X;
            for (int i = 0; i < 13; i++) { AddKey(x, y, aw + (i < rem ? 1 : 0), g_keyHeight, v[i], t[i]); x += aw + (i < rem ? 1 : 0) + g_keyGap; }
            AddKey(x, y, wBksp, g_keyHeight, 0x08, K_SPECIAL);
        } else {
            int fixed = wEsc + wBksp;
            int aw = (KEY_AREA_W - fixed - 14 * g_keyGap) / 13;
            int rem = KEY_AREA_W - fixed - 14 * g_keyGap - aw * 13;
            int w[15]; w[0] = wEsc;
            for (int i = 1; i <= 13; i++) w[i] = aw + (i <= rem ? 1 : 0);
            w[14] = wBksp;
            short v[15] = {0x1B,0xC0,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,0xBD,0xBB,0x08};
            KeyType t[15] = {K_SPECIAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_SPECIAL};
            int x = KEY_AREA_X;
            for (int i = 0; i < 15; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
        }
    }
    y += g_keyHeight + g_keyGap;

    // Row 1: Tab, q-p, [, ], \, Del  (15 keys)；F 行开启时隐藏原 Del
    {
        int wTab = (int)(68 * dpiScale * scaleX);
        int wDel = (int)(68 * dpiScale * scaleX);
        if (g_showFKeys) {
            // 无 Del：Tab, q-p, [, ], \ (14 keys)
            int aw = (KEY_AREA_W - wTab - 13 * g_keyGap) / 13;
            int rem = KEY_AREA_W - wTab - 13 * g_keyGap - aw * 13;
            int w[14]; w[0] = wTab;
            for (int i = 1; i <= 13; i++) w[i] = aw + (i <= rem ? 1 : 0);
            short v[14] = {0x09,0x51,0x57,0x45,0x52,0x54,0x59,0x55,0x49,0x4F,0x50,0xDB,0xDD,0xDC};
            KeyType t[14] = {K_SPECIAL,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_NORMAL,K_NORMAL,K_NORMAL};
            int x = KEY_AREA_X;
            for (int i = 0; i < 14; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
        } else {
            int fixed = wTab + wDel;
            int aw = (KEY_AREA_W - fixed - 14 * g_keyGap) / 13;
            int rem = KEY_AREA_W - fixed - 14 * g_keyGap - aw * 13;
            int w[15]; w[0] = wTab;
            for (int i = 1; i <= 13; i++) w[i] = aw + (i <= rem ? 1 : 0);
            w[14] = wDel;
            short v[15] = {0x09,0x51,0x57,0x45,0x52,0x54,0x59,0x55,0x49,0x4F,0x50,0xDB,0xDD,0xDC,0x2E};
            KeyType t[15] = {K_SPECIAL,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_NORMAL,K_NORMAL,K_NORMAL,K_SPECIAL};
            int x = KEY_AREA_X;
            for (int i = 0; i < 15; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
        }
    }
    y += g_keyHeight + g_keyGap;

    // Row 2: Caps, a-l, ;, ', Enter  (13 keys)
    {
        int wCaps = (int)(80 * dpiScale * scaleX);
        int wEnter = (int)(90 * dpiScale * scaleX);   // Win11 风格：自然宽度，不强制竖列对齐
        int fixed = wCaps + wEnter;
        int aw = (KEY_AREA_W - fixed - 12 * g_keyGap) / 11;
        int rem = KEY_AREA_W - fixed - 12 * g_keyGap - aw * 11;
        int w[13]; w[0] = wCaps;
        for (int i = 1; i <= 11; i++) w[i] = aw + (i <= rem ? 1 : 0);
        w[12] = wEnter;
        short v[13] = {0x14,0x41,0x53,0x44,0x46,0x47,0x48,0x4A,0x4B,0x4C,0xBA,0xDE,0x0D};
        KeyType t[13] = {K_CAPS,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_NORMAL,K_NORMAL,K_SPECIAL};
        int x = KEY_AREA_X;
        for (int i = 0; i < 13; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
    }
    y += g_keyHeight + g_keyGap;

    int xUp = 0;   // ↑ 键左边界，第 5 行的 ↓ 键与其对齐
    // Row 3: LShift, z-m, ,, ., /, ↑, RShift  (13 keys)
    {
        int wLSh = (int)(95 * dpiScale * scaleX);
        int wUp = (int)(52 * dpiScale * scaleX);   // 与 ↓ 同宽对齐
        int wRSh = (int)(52 * dpiScale * scaleX);   // 右 Shift 与 → 同宽，保证 ↓ 正对 ↑（十字对齐）
        int fixed = wLSh + wRSh + wUp;
        int aw = (KEY_AREA_W - fixed - 12 * g_keyGap) / 10;
        int rem = KEY_AREA_W - fixed - 12 * g_keyGap - aw * 10;
        int w[13]; w[0] = wLSh;
        for (int i = 1; i <= 10; i++) w[i] = aw + (i <= rem ? 1 : 0);
        w[11] = wUp; w[12] = wRSh;
        short v[13] = {0xA0,0x5A,0x58,0x43,0x56,0x42,0x4E,0x4D,0xBC,0xBE,0xBF,0x26,0xA1};
        KeyType t[13] = {K_MOD,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_NORMAL,K_NORMAL,K_NORMAL,K_ARROW,K_MOD};
        int x = KEY_AREA_X;
        for (int i = 0; i < 13; i++) {
            if (i == 11) xUp = x;   // 记录 ↑ 起始 x，供第 4 行对齐
            AddKey(x, y, w[i], g_keyHeight, v[i], t[i]);
            x += w[i] + g_keyGap;
        }
    }
    y += g_keyHeight + g_keyGap;

    // Row 4: (Fn), Ctrl, Win, Alt, 空格, Alt, Menu, Ctrl, ←, ↓, →；F 行开启时隐藏 Fn
    {
        int wFn  = (int)(46 * dpiScale * scaleX);
        int wCtl = (int)(56 * dpiScale * scaleX);
        int wWin = (int)(46 * dpiScale * scaleX);
        int wAlt = (int)(58 * dpiScale * scaleX);
        int wMenu = (int)(56 * dpiScale * scaleX);
        int wArw = (int)(52 * dpiScale * scaleX);   // ← 与 ↑/↓ 同宽
        int wUp   = (int)(52 * dpiScale * scaleX);   // ↓ 与上方 ↑ 同宽
        int wRSh  = (int)(52 * dpiScale * scaleX);   // → 与右 Shift 同宽（十字对齐约束）
        if (g_showFKeys) {
            // 无 Fn：Ctrl, Win, Alt, 空格, Alt, Menu, Ctrl, ←, ↓, → (10 keys)
            int leftOfArrows = wCtl + wWin + wAlt + wAlt + wMenu + wCtl;
            int spaceW = KEY_AREA_W - leftOfArrows - wArw - wUp - wRSh - 9 * g_keyGap;
            if (spaceW < 60) spaceW = 60;
            int w[10] = {wCtl, wWin, wAlt, spaceW, wAlt, wMenu, wCtl, wArw, wUp, wRSh};
            short v[10] = {0x11, 0x5B, 0x12, 0x20, 0x12, 0x5D, 0x11, 0x25, 0x28, 0x27};
            KeyType t[10] = {K_MOD, K_SPECIAL, K_MOD, K_SPACE, K_MOD, K_MOD, K_MOD, K_ARROW, K_ARROW, K_ARROW};
            int x = KEY_AREA_X;
            for (int i = 0; i < 10; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
        } else {
            // 有 Fn：Fn, Ctrl, Win, Alt, 空格, Alt, Menu, Ctrl, ←, ↓, → (11 keys)
            int leftOfArrows = wFn + wCtl + wWin + wAlt + wAlt + wMenu + wCtl;
            int spaceW = KEY_AREA_W - leftOfArrows - wArw - wUp - wRSh - 10 * g_keyGap;
            if (spaceW < 60) spaceW = 60;
            int w[11] = {wFn, wCtl, wWin, wAlt, spaceW, wAlt, wMenu, wCtl, wArw, wUp, wRSh};
            short v[11] = {0, 0x11, 0x5B, 0x12, 0x20, 0x12, 0x5D, 0x11, 0x25, 0x28, 0x27};
            KeyType t[11] = {K_SPECIAL, K_MOD, K_SPECIAL, K_MOD, K_SPACE, K_MOD, K_MOD, K_MOD, K_ARROW, K_ARROW, K_ARROW};
            int x = KEY_AREA_X;
            for (int i = 0; i < 11; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
        }
    }
}

// 全尺寸布局下「数字区显隐」的唯一入口：标题栏「小键盘」按钮与 Tab 键都走这里。
// 之前两条路径各改一遍 g_npHidden，迟早分叉；收成一个函数（故放在 BuildKeys 之后）。
// ⚠ 顺序：needW 用闭式解，绝不能在改 g_ww 之前去读 g_keyGap —— 它由 g_ww 反算，
//    这时还是旧值，算出的 needW 会偏小，数字区照样被挤出去。
static void SetFullNumpadHidden(HWND hWnd, BOOL hidden) {
    g_npHidden = hidden;
    IniSetInt(L"Keyboard", L"NpHidden", hidden ? 1 : 0);
    if (!hidden) {
        // 主区保底 400 DIP 反解：ww ≥ 700·dpi 时才放得下整块数字区
        int needW = (int)(700.0 * GetSystemDpiScale()) + 8;
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

// 设置页五档：22 大标题 / 15 行主文本 / 12 控件标签 / 11 行描述 / 10 元信息。
// 单字重下层级完全由「字号 + 颜色」承担，相邻档至少差 1pt。
// 「控件标签」是独立一档：Tab、开关的「开/关」、分段、按钮和行描述原来挤在同一个 10.5pt 上，
// 于是整页只剩三档、层级是平的。
static void InitFixedFonts() {
    double dpi = GetSystemDpiScale();
    g_sfBig  = MakeFont(22 * dpi);   // 页面大标题
    g_sfRow  = MakeFont(15 * dpi);   // 行主文本
    g_sfCtrl = MakeFont(12 * dpi);   // 控件标签：Tab / 开·关 / 分段 / 按钮
    g_sfBase = MakeFont(11 * dpi);   // 行描述
    g_sfMeta = MakeFont(10 * dpi);   // 版本号 / Copyright
}

static void RecreateFontsAndLayout() {
    if (g_f12) DeleteObject(g_f12);
    if (g_f13) DeleteObject(g_f13);
    if (g_f14) DeleteObject(g_f14);

    double dpiScale = GetSystemDpiScale();

    // 字号跟「键高」走，不跟窗口高度走 —— 这一条就是默认布局的字号规则。
    // 先算布局拿到 g_keyHeight，再按它定字号：默认布局 5 行 @320 DIP 的键高是 48，
    // 正好是参考值（finalFontScale = dpiScale，与改动前完全一致，默认布局观感不变）；
    // 全尺寸 6 行、同样窗口高度下键更矮，字号自动按比例收回来 —— 否则键面标签会比默认
    // 布局明显偏大，挤到只能逐键降档，「字体大小不统一」就是这么来的。
    BuildKeys();

    double refKeyH = 48.0 * dpiScale;              // 默认布局（5 行 / 320 DIP）的键高
    double finalFontScale = dpiScale * ((double)g_keyHeight / refKeyH);
    if (finalFontScale < 0.4 * dpiScale) finalFontScale = 0.4 * dpiScale;

    // 单字重：g_f14 是唯一主档，修饰键与普通键的区分交给底色 + 文字色（与设置页 Tab 同一套逻辑）。
    g_f12 = MakeFont((int)(12 * finalFontScale + 0.5));   // 四舍五入，别让非整数缩放累积偏差
    g_f13 = MakeFont((int)(13 * finalFontScale + 0.5));
    g_f14 = MakeFont((int)(14 * finalFontScale + 0.5));   // 主档
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

static BOOL DrawTextGp(HDC dc, int x, int y, int w, int h, const wchar_t* s,
                       float emPx, DWORD color, BOOL center, BOOL wrap) {
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
    fmt.SetAlignment(center ? Gdiplus::StringAlignmentCenter : Gdiplus::StringAlignmentNear);
    fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);

    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(color),
                                                  GetGValue(color), GetBValue(color)));
    Gdiplus::RectF rc((Gdiplus::REAL)x, (Gdiplus::REAL)y, (Gdiplus::REAL)w, (Gdiplus::REAL)h);
    g.DrawString(s, -1, &font, rc, &fmt, &brush);
    return TRUE;    // Graphics 析构时自动 Flush
}

static void DrawTextC(HDC dc, int x, int y, int w, int h, const wchar_t* s, HFONT f, DWORD c) {
    if (DrawTextGp(dc, x, y, w, h, s, FontEmPx(f), c, TRUE, FALSE)) return;
    RECT r = {x, y, x + w, y + h};        // 回退：GDI+ 未就绪 / 字体不可用
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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
                          buf, FontEmPx(fShift), shiftC, TRUE, FALSE)) {
        SelectObject(dc, fShift);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, shiftC);
        DrawTextW(dc, buf, -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 主字符（键下半部）
    buf[0] = baseCh;
    RECT rb = {x, y + h / 2, x + w, y + h};
    if (!DrawTextGp(dc, rb.left, rb.top, rb.right - rb.left, rb.bottom - rb.top,
                          buf, FontEmPx(fBase), baseC, TRUE, FALSE)) {
        SelectObject(dc, fBase);
        SetTextColor(dc, baseC);
        DrawTextW(dc, buf, -1, &rb, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

// ===== 自绘图标运行层（替代图标字体）=====
// 几何来自 hk_icons_generated.h，与 PanDaPE-Maker 的 icons.rs 同源（24 网格 / 1.9 描边 / round）。
// 收益：任意尺寸、任意色相都可用，加一个图标＝加一段顶点数据，不用重跑字体子集化。
static const HkIconDef& HkIcon(int id) { return k_hkIcons[id]; }

static int MeasureTextW(HDC dc, const wchar_t* s, HFONT f) {
    if (!s || !s[0] || !f) return 0;
    HFONT old = (HFONT)SelectObject(dc, f);
    SIZE sz = {0, 0};
    GetTextExtentPoint32W(dc, s, (int)wcslen(s), &sz);
    SelectObject(dc, old);
    return sz.cx;
}

// 键面标签自适应字号：只用来救「PrtSc / ScrLk / Pause / Home / PgUp / PgDn」这类
// 塞不进 1u 键的长标签。全尺寸布局的导航区键宽只有与字母键相同的 52px，
// 而在默认布局里，这些位置是靠「导航区比字母键宽 59%」把矛盾盖住的。
//
// 阶梯刻意只有三档：基础档 → 13 → 12（12 是地板，不再往下掉）。
// 掉到 9pt 那种深度会让同一排导航键冒出 4 种字号，反而更「不统一」；
// 停在 12 时，最宽的 Pause 约 48px，仍在 52px 键内（两侧各 2px 余量）。
// 注意真正决定观感的是上面 RecreateFontsAndLayout 的字号缩放：字号跟键高走，
// 这里只是收尾，不是主力。
static HFONT FitKeyFont(HDC dc, const wchar_t* s, int maxW) {
    if (!s || !s[0] || maxW <= 0) return g_f14;
    HFONT ladder[3] = { g_f14, g_f13, g_f12 };
    for (int i = 0; i < 3; i++)
        if (MeasureTextW(dc, s, ladder[i]) <= maxW) return ladder[i];
    return ladder[2];
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

// 键面标签的图标形态；返回 FALSE 表示该键不参与图标化（调用方继续走文字路径）。
// 颜色只有 textC 一个来源，所以「普通/修饰/按下」三态与深色主题都自动跟随，零分支。
// 注意：局部变量不要叫 small / pure —— <windows.h> 的 rpcndr.h 里有 `#define small char`。
static BOOL DrawKeyLabel(HDC dc, const KeyDef* k, HFONT f, const wchar_t* text, DWORD color) {
    if (g_keyIconStyle == 0) return FALSE;
    const HkIconDef* ic = KeyIconFor(k);
    if (!ic) return FALSE;

    double dpi = GetSystemDpiScale();
    int iconOnly   = (int)(20 * dpi);
    int iconInline = (int)(18 * dpi);
    int gap        = (int)(6 * dpi);

    // 图标+文字（唯一的图标形态）：放得下就并排画，放不下**退回只画图标**。
    // 退格这类键在 1u 宽的布局里塞不下 [⌫ Backspace]，退回图标至少还能一眼认出是退格；
    // 之前直接放弃图标交给文字路径，反而长标签被裁掉半截 —— 实机反馈的「图标也看不到了」。
    if (text && text[0] && k->w >= (int)(40 * dpi)) {
        int tw = MeasureTextW(dc, text, f);
        if (iconInline + gap + tw <= k->w - (int)(16 * dpi)) {
            int total = iconInline + gap + tw;
            int x = k->x + (k->w - total) / 2;
            DrawHkIcon(dc, (float)x, (float)(k->y + (k->h - iconInline) / 2), (float)iconInline,
                       *ic, color, color);
            DrawTextC(dc, x + iconInline + gap, k->y, tw + 4, k->h, text, f, color);
            return TRUE;
        }
    }
    DrawHkIcon(dc, (float)(k->x + (k->w - iconOnly) / 2), (float)(k->y + (k->h - iconOnly) / 2),
               (float)iconOnly, *ic, color, color);
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

static const wchar_t* KeyText(const KeyDef* k) {
    static wchar_t buf[16];
    if (k->vk >= 0x200 && k->vk <= 0x205) return g_domainTexts[k->vk - 0x200];
    if (k->type == K_LETTER) {
        return LetterKeyText(k->vk);
    }
    if (k->type == K_NORMAL) {
        if (g_fnLayer && !g_fnWebLayout) {
            int fn = FnMap(k->vk);
            if (fn) { swprintf(buf, 16, L"F%d", fn); return buf; }
        }
        wchar_t ch = GetSymForKey(k->vk, g_sh && g_shiftSymbols);
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
        // 退格：始终显示全称 Backspace。放不下由 FitKeyFont 降档，不再缩写成 Bksp
        case 0x08: return L"Backspace";
        case 0x09: return L"Tab";
        case 0x0D: return L"Enter";
        case 0x14: return L"Caps";
        case 0x10: case 0xA0: case 0xA1: return L"Shift";
        case 0x11: return L"Ctrl";
        case 0x12: return L"Alt";
        case 0x5B: return L"";   // Win 键：矢量绘制 Windows 徽标，无文字
        case 0x5D: return L"Menu";       // 三条杠是它的图形；文字模式下要给出名字（原来返回空串）
        case 0x20: return L"";         // 空格键不显示文字
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
    if (k->type == K_SPECIAL && k->vk == 0) return L"Fn";
    return L"";
}

static BOOL IsActive(const KeyDef* k) {
    if (k->vk == 0x14 && g_cp) return TRUE;
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
    BOOL isExtended = (vk == VK_RCONTROL || vk == VK_RMENU ||
                       vk == VK_LEFT || vk == VK_RIGHT || vk == VK_UP || vk == VK_DOWN ||
                       vk == VK_HOME || vk == VK_END || vk == VK_PRIOR || vk == VK_NEXT ||
                       vk == VK_INSERT || vk == VK_DELETE || vk == VK_LWIN || vk == VK_RWIN);

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
        if (k->vk == 0x09 && g_layoutMode == 2 && g_npTabToggle) {
            // 完整布局：Tab 键显示/隐藏右侧数字区 —— 与标题栏「小键盘」按钮同一个入口
            SetFullNumpadHidden(g_hWnd, !g_npHidden);
            break;
        }
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
        SendKey(k->vk, g_sh, g_ct, g_al, g_winKey);
        g_sh = FALSE; g_ct = FALSE; g_al = FALSE; ClearWinLock();
        break;
    case K_MOD:
        if (k->vk == VK_RSHIFT || k->vk == VK_SHIFT || k->vk == VK_LSHIFT) {
            // 左右 Shift 状态机（状态以 g_sh 为准）：
            //  - 未处于 Shift 状态：点击进入 Shift 锁定（后续按键为 Shift+组合键）；
            //  - 已处于 Shift 状态：再次点击切换中/英输入法，并退出 Shift 锁定。
            // 任意 Shift+组合键使用后会退出 Shift 状态，因此下次点击 Shift 可再次正常进入，不会失步。
            BOOL wasFn = g_fnLayer;
            if (g_sh) {
                g_sh = FALSE;
                g_fnLayer = FALSE;
                ToggleImeLang();
            } else {
                g_sh = TRUE;
                g_fnLayer = FALSE;
            }
            if (wasFn) {
                BuildKeys();   // 若正处网页布局层，退出后需重建键位表
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

static HeaderMetrics GetHeaderMetrics() {
    double dpiScale = GetSystemDpiScale();
    double dpi = dpiScale;
    HeaderMetrics hm = {};
    hm.btnH = (int)(28 * dpi);
    // 按钮上留白固定 10 DIP（原来是 (header-btnH)/2 居中，页头 36 时只剩 4 DIP，贴顶）；
    // 页头被压得很矮时退回居中，免得按钮溢出页头。
    hm.btnY = (int)(10 * dpi);
    if (hm.btnY + hm.btnH > g_headerH) {
        hm.btnY = (g_headerH - hm.btnH) / 2;
        if (hm.btnY < 0) hm.btnY = 0;
    }

    int gap     = (int)(6 * dpi);
    int rMargin = (int)(6 * dpi);

    // 「设置」和「小键盘」两个按钮都是「图标 + 文字」，宽度按内容给足，
    // 两者共用 DrawHeaderPill 绘制（形状、配色、留白一致）。
    hm.wMenu  = (int)(68 * dpi);
    hm.wClose = (int)(28 * dpi);
    hm.wMin   = (int)(28 * dpi);
    hm.wNum   = (int)(84 * dpi);    // 图标 17 + 间距 6 + 文字（小键盘/Numpad）+ 与设置按钮同样的左右留白

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

// 标题栏胶囊按钮：底色 + 矢量图标 + 文字，三者同一配方。
// 「设置」与「小键盘」共用这一份 —— 两个按钮的观感必须一致。
// 设置按钮恒定显示「齿轮 + 设置」（设置页页头也是齿轮，两处观感才对得上）。
// 宽度由调用方从 HeaderMetrics 取（绘制与命中同源，见 GetHeaderMetrics 上方的说明）。
// active：「小键盘开着」的实底态（主色底 + 主色上的文字，零新增令牌）。
static void DrawHeaderPill(HDC dc, const HeaderMetrics& hm, int x, int w, BOOL hov,
                           const HkIconDef& icon, const wchar_t* label, BOOL active) {
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

    int iconSz = (int)(17 * GetSystemDpiScale());
    int gap    = (int)(6 * GetSystemDpiScale());
    int tw     = MeasureTextW(dc, label, g_f12);
    int cx     = x + (w - (iconSz + gap + tw)) / 2;
    DrawHkIcon(dc, (float)cx, (float)(hm.btnY + (hm.btnH - iconSz) / 2), (float)iconSz,
               icon, fg, fg);
    DrawTextC(dc, cx + iconSz + gap, hm.btnY, tw + 4, hm.btnH, label, g_f12, fg);
}

static void DrawHeaderMenuButton(HDC dc, const HeaderMetrics& hm) {
    DrawHeaderPill(dc, hm, hm.xMenu, hm.wMenu, (g_hdrHov == HDR_DOCK),
                   HkIcon(HKICON_GEAR), T(L"\x8BBE\x7F6E", L"Settings"), FALSE);
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
                       HkIcon(HKICON_NUMPAD), T(L"\x5C0F\x952E\x76D8", L"Numpad"),
                       NumBtnActive());
    }

    // 最小化 / 关闭：改用矢量图标（与设置页、关闭提示窗口同一套图形，不再手绘线条）
    int iconSz = (int)(18 * dpiScale);
    int hoverR = (int)(6 * dpiScale);
    if (g_hdrHov == HDR_MIN) {
        DrawRoundRect(dc, hm.xMin, hm.btnY, hm.wMin, hm.btnH,
                      C_REGULAR_HOV, C_REGULAR_HOV, hoverR);
    }
    DrawHkIcon(dc, (float)(hm.xMin + (hm.wMin - iconSz) / 2),
               (float)(hm.btnY + (hm.btnH - iconSz) / 2), (float)iconSz,
               HkIcon(HKICON_MINIMIZE), C_DIM, C_DIM);
    if (g_hdrHov == HDR_CLOSE) {
        DrawRoundRect(dc, hm.xClose, hm.btnY, hm.wClose, hm.btnH,
                      C_REGULAR_HOV, C_REGULAR_HOV, hoverR);
    }
    DrawHkIcon(dc, (float)(hm.xClose + (hm.wClose - iconSz) / 2),
               (float)(hm.btnY + (hm.btnH - iconSz) / 2), (float)iconSz,
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
        wchar_t baseCh = 0, shiftCh = 0;
        if (k->type == K_NORMAL && !(g_fnLayer && !g_fnWebLayout && FnMap(k->vk) != 0)) {
            baseCh = GetSymForKey(k->vk, FALSE);
            shiftCh = GetSymForKey(k->vk, TRUE);
        }
        // 未按 Shift：双符号显示（数字 + 顶部特殊符号，副符号置灰）；
        // 按 Shift：开启“仅显示特殊符号”时只显示顶部符号（不显示数字），关闭时仍显示数字。
        BOOL shiftOn = (g_sh || g_physShift);
        BOOL dual = (baseCh && shiftCh && shiftCh != baseCh);

        if (k->vk == 0x5D) {
            // Menu 键：原本靠图标字体的 \xE700 画汉堡，取消图标字体后一律改矢量，
            // 三种图标样式下外观一致（它本身没有可用文字，不参与「图标+文字」）。
            // 尺寸与键面 14px 标签同档（20 DIP）：三条杠的观感问题在渲染（连线），不在尺寸。
            double dpi = GetSystemDpiScale();
            int s = (int)(20 * dpi);
            DrawHkIcon(dc, (float)(k->x + (k->w - s) / 2), (float)(k->y + (k->h - s) / 2),
                       (float)s, HkIcon(HKICON_HAMBURGER), textC, textC);
        } else if (k->vk == 0x5B) {
            // Win 键：字体无 Windows 徽标字形，直接矢量绘制 Win11 风格四格徽标
            double u = (double)k->h * 0.32;   // 缩小：徽标占键高 32%
            int sq = (int)(u * 0.44);
            int gp = (int)(u * 0.12);
            if (sq < 2) sq = 2;
            int total = sq * 2 + gp;
            int ox = k->x + (k->w - total) / 2;
            int oy = k->y + (k->h - total) / 2;
            int rr = (int)(sq * 0.18);
            DrawRoundRect(dc, ox, oy, sq, sq, textC, textC, rr);
            DrawRoundRect(dc, ox + sq + gp, oy, sq, sq, textC, textC, rr);
            DrawRoundRect(dc, ox, oy + sq + gp, sq, sq, textC, textC, rr);
            DrawRoundRect(dc, ox + sq + gp, oy + sq + gp, sq, sq, textC, textC, rr);
        } else if (dual) {
            // 双符号键在三态下都是文字：主字符 + 副符号本身就是两个信息，图标化会毁数据
            if (shiftOn) {
                wchar_t single[2] = { g_shiftSymbols ? shiftCh : baseCh, 0 };
                DrawTextC(dc, k->x, k->y, k->w, k->h, single, f, textC);
            } else {
                DrawKeyDual(dc, k->x, k->y, k->w, k->h, baseCh, shiftCh, f, g_f12, textC, C_DIM);
            }
        } else if (!DrawKeyLabel(dc, k, f, txt, textC)) {
            DrawTextC(dc, k->x, k->y, k->w, k->h, txt, f, textC);
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
    BOOL sh, ct, al, cp, winKey, physShift, physWin, fnLayer, showFKeys,
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
    KbFrameSig sig;
    sig.w = g_ww; sig.h = g_wh;
    sig.hk = g_hk; sig.pk = g_pk; sig.hdrHov = g_hdrHov;
    sig.layoutMode = g_layoutMode; sig.nk = g_nk;
    sig.themeBg = g_themeBuf.pageBg;
    sig.hue = g_hue;
    sig.keyIconStyle = g_keyIconStyle;   // 漏掉这一项 → 切换图标样式后主键盘不刷新（看似"没生效"）
    sig.dpi = (float)GetSystemDpiScale();
    sig.sh = g_sh; sig.ct = g_ct; sig.al = g_al; sig.cp = g_cp;
    sig.winKey = g_winKey; sig.physShift = g_physShift; sig.physWin = g_physWin;
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
#define S_HIT_NPTAB          25   // 布局 Tab：Tab 键切换小键盘（仅完整布局显示）
#define S_HIT_KEYICON        26   // 布局 Tab：按键图标样式（分段控件，整条一个命中码）
#define S_HIT_SHIFTSYM       19
#define S_HIT_THEME_DROP     20
#define S_HIT_URL            30
#define S_HIT_FEEDBACK       31
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

static void DrawTextL(HDC dc, int x, int y, int w, int h, const wchar_t* s, HFONT f, DWORD c,
                      BOOL wrap = FALSE) {
    if (DrawTextGp(dc, x, y, w, h, s, FontEmPx(f), c, FALSE, wrap)) return;
    RECT r = {x, y, x + w, y + h};        // 回退：GDI+ 未就绪 / 字体不可用
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, (wrap ? (DT_LEFT | DT_WORDBREAK | DT_TOP)
                                   : (DT_LEFT | DT_VCENTER | DT_SINGLELINE)) | DT_NOPREFIX);
}

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

// 描述需要两行的行（文案本身就长，一行放不下会被硬截断）：
// 行高、绘制、命中三处都读这一份判断，才不会出现「字画到行外 / 热区对不上」。
static BOOL SettingsRowDescWraps(int tab, int index) {
    return (tab == 3 && (index == 3 || index == 4));   // 小键盘按钮 / Tab 切换小键盘
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
    if (SettingsRowDescWraps(g_sTab, index) && contentDip < 56)
        contentDip = 56;                    // 标题 18 + 间距 6 + 描述两行 32
    return (int)((12 + contentDip + 12) * m.dpi);
}

static int SettingsRowCount(int tab) {
    if (tab == 0) return g_af ? 7 : 6;
    if (tab == 3) return (g_layoutMode == 2) ? 5 : 4;
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

static int SettingsComboY(const SettingsMetrics& m, const RECT& row) {
    return row.top + (row.bottom - row.top - m.comboH) / 2;
}

// 展开行（主题色相编辑器）里的下拉不居中，而是与标题对齐
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
    int ty = row.top + SettingsRowPadY(m);
    DrawIconTile(dc, SettingsRowTileX(m), ty, m.tileSize, (int)(17 * m.dpi), iconId, glyph);

    int tx = SettingsRowTextX(m);
    int rightLimit = (ctrlLeft > 0) ? ctrlLeft - (int)(12 * m.dpi) : row.right - (int)(20 * m.dpi);
    int tw = rightLimit - tx;
    if (tw < (int)(60 * m.dpi)) tw = (int)(60 * m.dpi);
    DrawTextL(dc, tx, ty, tw, (int)(20 * m.dpi), title, g_sfRow, C_WHITE);
    if (desc && desc[0]) {
        if (descWrap)
            DrawTextL(dc, tx, ty + (int)(26 * m.dpi), tw, (int)(32 * m.dpi), desc, g_sfBase, C_DIM, TRUE);
        else
            DrawTextL(dc, tx, ty + (int)(26 * m.dpi), tw, (int)(16 * m.dpi), desc, g_sfBase, C_DIM);
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
                  labels[i], g_sfBase, (on || g_sHov == k_settingsTabHits[i]) ? C_WHITE : C_DIM);
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
static int SegmentedWidth(const SettingsMetrics& m, const wchar_t** items, int count) {
    HDC dc = GetDC(0);
    int w = (int)(6 * m.dpi);   // 轨道左右 padding 3
    for (int i = 0; i < count; i++) w += MeasureTextW(dc, items[i], g_sfCtrl) + (int)(36 * m.dpi);
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
        int w = MeasureTextW(dc, items[i], g_sfCtrl) + (int)(36 * m.dpi);
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
        int w = MeasureTextW(dc, items[i], g_sfCtrl) + (int)(36 * m.dpi);
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
    // 布局 Tab 行序：0=键盘布局 1=按键图标样式 2=Fn 网页布局 3=123 切换按钮 4=Tab 切换小键盘
    int rowIndex;
    if (hit == S_HIT_AUTO) rowIndex = 0;
    else if (hit == S_HIT_AUTOHIDE) rowIndex = 1;
    else if (hit == S_HIT_FNWEB) rowIndex = 2;
    else if (hit == S_HIT_NPBTN) rowIndex = 3;
    else if (hit == S_HIT_NPTAB) rowIndex = 4;
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
    int y = row.top + (row.bottom - row.top - m.switchH) / 2;
    DrawTextC(dc, x - (int)(42 * m.dpi), row.top, (int)(34 * m.dpi), row.bottom - row.top,
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
// 关于页链接行：行高 = 12*2 + max(tile 30, 圆形按钮 34, 两行文字 34)
static int AboutLinkRowHeight(const SettingsMetrics& m) {
    return m.rowPadY * 2 + (int)(34 * m.dpi);
}

static RECT AboutLinkRowRect(const SettingsMetrics& m, const RECT& card, int index) {
    int h = AboutLinkRowHeight(m);
    RECT r = {card.left, card.top + h * index, card.right, card.top + h * (index + 1)};
    return r;
}

// 关于 tab 的两张卡：绘制与命中必须取自同一份几何，避免两边各算一遍
struct AboutLayout {
    RECT card1;   // 身份卡
    RECT card2;   // 链接卡
    int  mark;    // 矢量键盘标识边长
};

static AboutLayout GetAboutLayout(const SettingsMetrics& m) {
    AboutLayout a = {};
    a.mark = (int)(44 * m.dpi);
    a.card1.left = m.contentX;
    a.card1.top = m.contentY;
    a.card1.right = m.contentX + m.contentW;
    a.card1.bottom = a.card1.top + (int)(16 * m.dpi) * 2 + a.mark;
    a.card2.left = m.contentX;
    a.card2.top = a.card1.bottom + (int)(12 * m.dpi);
    a.card2.right = m.contentX + m.contentW;
    a.card2.bottom = a.card2.top + AboutLinkRowHeight(m) * 2;
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
        bottom = al.card2.bottom + (int)(18 * m.dpi) * 2;   // 版权行（18 间距 + 18 行高）
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

// 链接行：整行可点（触摸场景下命中区必须够大），右侧 34×34 圆形 External 按钮
static void DrawAboutLinkRow(HDC dc, const SettingsMetrics& m, const RECT& row,
                             int iconId, const wchar_t* title, const wchar_t* desc, BOOL hover) {
    if (hover) DrawSettingsRowHover(dc, m, row);
    int ty = row.top + m.rowPadY;
    DrawIconTile(dc, SettingsRowTileX(m), ty, m.tileSize, (int)(17 * m.dpi), iconId, NULL);

    int btn = (int)(34 * m.dpi);
    int bx = row.right - (int)(20 * m.dpi) - btn;
    int by = row.top + (row.bottom - row.top - btn) / 2;
    DrawRoundRect(dc, bx, by, btn, btn, hover ? C_REGULAR_HOV : C_REGULAR,
                  hover ? C_REGULAR_HOV : C_REGULAR, btn / 2);
    int isz = (int)(17 * m.dpi);
    DrawHkIcon(dc, (float)(bx + (btn - isz) / 2), (float)(by + (btn - isz) / 2), (float)isz,
               HkIcon(HKICON_EXTERNAL), C_BTN_CONTENT, C_BTN_CONTENT);

    int tx = SettingsRowTextX(m);
    int tw = bx - (int)(12 * m.dpi) - tx;
    DrawTextL(dc, tx, ty, tw, (int)(18 * m.dpi), title, g_sfRow, C_WHITE);
    if (desc && desc[0])
        DrawTextL(dc, tx, ty + (int)(18 * m.dpi), tw, (int)(16 * m.dpi), desc, g_sfBase, C_DIM);
}

static void SettingsDraw(HDC dc, HWND hWnd) {
    SettingsMetrics m = GetSettingsMetrics(hWnd);

    // 页面头：38×38 图标 tile + 26px 标题。
    // 关于是一块独立页面，标题与图标都跟着改成「关于 / Info」；其余三个 tab 同属「设置」。
    BOOL aboutTab = (g_sTab == 2);
    DrawIconTile(dc, m.margin, m.titleY, m.headIcon, (int)(20 * m.dpi),
                 aboutTab ? HKICON_INFO : HKICON_GEAR, NULL);
    int titleX = m.margin + m.headIcon + (int)(16 * m.dpi);
    DrawTextL(dc, titleX, m.titleY, m.closeX - (int)(12 * m.dpi) - titleX, m.titleH,
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
        // 布局 Tab：0=键盘布局 1=按键图标样式 2=Fn 网页布局 3=123 按钮 4=Tab 切换小键盘
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
                              T(L"按 Fn 切换到上网常用布局", L"Press Fn to switch to the web-friendly layout"),
                              g_sHov == S_HIT_FNWEB, SettingsSwitchTextRight(m, r));
        DrawSettingSwitch(dc, m, r, g_fnWebLayout, S_HIT_FNWEB);

        r = SettingsRowRect(m, 3);
        DrawSettingRowContent(dc, m, r, HKICON_NUMPAD, NULL,
                              T(L"小键盘按钮", L"Numpad Button"),
                              T(L"在标题栏显示；默认布局切小键盘，全尺寸显隐数字区", L"Show it in the title bar; toggles the numpad section"),
                              g_sHov == S_HIT_NPBTN, SettingsSwitchTextRight(m, r), TRUE);
        DrawSettingSwitch(dc, m, r, g_showNumBtn, S_HIT_NPBTN);

        if (g_layoutMode == 2) {
            r = SettingsRowRect(m, 4);
            DrawSettingRowContent(dc, m, r, -1, L"F",   // Tab 切换小键盘同样保留手绘 F
                                  T(L"Tab 切换小键盘", L"Tab Toggles Numpad"),
                                  T(L"全尺寸布局下按 Tab 键显示或隐藏数字区", L"Press Tab in the full layout to show or hide the numpad"),
                                  g_sHov == S_HIT_NPTAB, SettingsSwitchTextRight(m, r), TRUE);
            DrawSettingSwitch(dc, m, r, g_npTabToggle, S_HIT_NPTAB);
        }
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
            Fill(dc, r.left + (int)(20 * m.dpi), r.top + (int)(56 * m.dpi),
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
        // 关于 tab：两张卡（身份 / 链接），版权行在卡片下方居中
        AboutLayout al = GetAboutLayout(m);
        DrawRoundRect(dc, al.card1.left, al.card1.top,
                      al.card1.right - al.card1.left, al.card1.bottom - al.card1.top,
                      C_KEY, C_KEY, (int)(16 * m.dpi));

        // 身份标识：矢量 KeyboardMark（主色键盘体 + 挖空键块），不用位图、不加 tile
        int my = al.card1.top + (int)(16 * m.dpi);
        DrawHkIcon(dc, (float)(al.card1.left + (int)(20 * m.dpi)), (float)my, (float)al.mark,
                   HkIcon(HKICON_KEYBOARDMARK), C_HOT, C_ON_PRIMARY);

        int tx = al.card1.left + (int)(20 * m.dpi) + al.mark + (int)(18 * m.dpi);
        int ty = al.card1.top + (int)(22 * m.dpi);
        int tw = al.card1.right - tx - (int)(20 * m.dpi);
        DrawTextL(dc, tx, ty, tw, (int)(26 * m.dpi),
                  T(L"HKeyboard 轻键", L"HKeyboard"), g_sfBig, C_WHITE);
        wchar_t meta[96];
        swprintf(meta, 96, T(L"轻量屏幕键盘 · v%hs (%ls)", L"Lightweight screen keyboard · v%hs (%ls)"),
                 VER_FILEVERSION_STR, ArchName());
        DrawTextL(dc, tx, ty + (int)(28 * m.dpi), tw, (int)(18 * m.dpi), meta, g_sfMeta, C_DIM);

        DrawRoundRect(dc, al.card2.left, al.card2.top,
                      al.card2.right - al.card2.left, al.card2.bottom - al.card2.top,
                      C_KEY, C_KEY, (int)(16 * m.dpi));
        Fill(dc, al.card2.left + (int)(20 * m.dpi), al.card2.top + AboutLinkRowHeight(m),
             (al.card2.right - al.card2.left) - (int)(40 * m.dpi), 1, C_LINE_DIV);
        DrawAboutLinkRow(dc, m, AboutLinkRowRect(m, al.card2, 0), HKICON_GITHUB,
                         T(L"项目地址", L"Project URL"),
                         L"github.com/PanDaDaTech/Hydrogen-Keyboard",
                         g_sHov == S_HIT_URL);
        DrawAboutLinkRow(dc, m, AboutLinkRowRect(m, al.card2, 1), HKICON_INFO,
                         T(L"问题反馈", L"Feedback"),
                         T(L"遇到 bug 或有建议，到 Issues 提一个", L"Report bugs or ideas on GitHub Issues"),
                         g_sHov == S_HIT_FEEDBACK);

        // 版权行：文字逐字保留（含 2026 与结尾句点），12px C_DIM 居中，放在卡片下方
        DrawTextC(dc, m.contentX, al.card2.bottom + (int)(18 * m.dpi), m.contentW, (int)(18 * m.dpi),
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
        // 布局 Tab：0=键盘布局 1=按键图标样式 2=Fn 网页布局 3=123 按钮 4=Tab 切换小键盘
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

        if (g_layoutMode == 2) {
            r = SettingsRowRect(m, 4);
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_NPTAB;
        }
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
        // 关于 tab：两个链接行整行可点（卡片位置与绘制同源）
        AboutLayout al = GetAboutLayout(m);
        RECT r0 = AboutLinkRowRect(m, al.card2, 0);
        if (x >= r0.left && x < r0.right && y >= r0.top && y < r0.bottom) return S_HIT_URL;
        RECT r1 = AboutLinkRowRect(m, al.card2, 1);
        if (x >= r1.left && x < r1.right && y >= r1.top && y < r1.bottom) return S_HIT_FEEDBACK;
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
    IniSetInt(L"Keyboard", L"KeyIconStyle", 0);
    IniSetInt(L"General", L"ShiftSymbols", 1);
    IniSetInt(L"General", L"Language", 0);
    IniSetInt(L"General", L"AutoPopup", 1);
    IniSetInt(L"General", L"AutoHide", 1);
    IniSetInt(L"General", L"ConfigVersion", 5);
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
    g_npTabToggle = IniGetInt(L"Keyboard", L"NpTabToggle", 1) != 0;
    g_npHidden = IniGetInt(L"Keyboard", L"NpHidden", 0) != 0;
    g_showFKeys = (IniGetInt(L"Keyboard", L"FKeys", 0) != 0);
    g_fnWebLayout = (IniGetInt(L"Keyboard", L"FnWebLayout", 0) != 0);
    g_keyIconStyle = IniGetInt(L"Keyboard", L"KeyIconStyle", 0);
    // 0=文字（默认，= 升级前的现状）2=图标+文字。
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
    case S_HIT_NPTAB:
        BeginSwitchAnimation(hWnd, hit, g_npTabToggle, !g_npTabToggle);
        g_npTabToggle = !g_npTabToggle;
        IniSetInt(L"Keyboard", L"NpTabToggle", g_npTabToggle ? 1 : 0);
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
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
    case S_HIT_FEEDBACK:
        ShellExecuteW(NULL, L"open", L"https://github.com/PanDaDaTech/Hydrogen-Keyboard/issues", NULL, NULL, SW_SHOWNORMAL);
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
            if (hv == S_HIT_URL || hv == S_HIT_FEEDBACK) {
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
                // 全尺寸布局：切换右侧数字区（与 Tab 键同一个入口）
                SetFullNumpadHidden(hWnd, !g_npHidden);
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
        // 允许自由缩小（小键盘布局等），仅挡住过小尺寸。
        // 全尺寸布局必须单独给下限：键帽 44px 的触摸安全线反解出 1090 宽（含数字区）/
        // 870 宽（数字区收起后）。写死 300 的话窗口一窄，整块数字区就被推出窗口看不见了。
        double dpiScale = GetSystemDpiScale();
        if (g_layoutMode == 2) {
            mmi->ptMinTrackSize.x = (int)(870 * dpiScale);
            mmi->ptMinTrackSize.y = (int)(300 * dpiScale);
        } else {
            mmi->ptMinTrackSize.x = (int)(300 * dpiScale);
            mmi->ptMinTrackSize.y = (int)(150 * dpiScale);
        }
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
                if (g_physShift != pShift || g_physWin != pWin) {
                    g_physShift = pShift;
                    g_physWin = pWin;
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
