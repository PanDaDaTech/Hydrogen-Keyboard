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
#include "resource.h"
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// 当前编译架构（关于页显示用）
#ifdef _M_ARM64
#define HK_ARCH L"arm64"
#elif defined(_M_X64)
#define HK_ARCH L"64位"
#else
#define HK_ARCH L"32位"
#endif

// 界面语言与高亮颜色（需在 ArchName / ApplyTheme 之前声明，供其读取）
int         g_lang = 0;                // 语言：0=中文 1=English
int         g_hlMode = 0;              // 高亮颜色：0=默认 1=自定义
int         g_hlColor = 0xD47800;      // 自定义高亮颜色（BGR）

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
#define WM_TRAY         (WM_APP + 100)
#define WM_FOCUS_EVENT  (WM_APP + 101)
#define WM_SHOW_KEYBOARD (WM_APP + 102)
#define WM_REAPPLY_MATERIAL (WM_APP + 103)

#ifndef WM_DPICHANGED
#define WM_DPICHANGED 0x02E0
#endif

#ifndef WM_DWMCOLORIZATIONCOLORCHANGED
#define WM_DWMCOLORIZATIONCOLORCHANGED 0x0320
#endif

#define ID_MENU_TOGGLE 10001
#define ID_MENU_AUTO   10002
#define ID_MENU_THEME  10004
#define ID_MENU_ABOUT  10008
#define ID_MENU_EXIT   10009
#define ID_MENU_SETTINGS 10010

// ========== Theme System ==========
struct ThemeColors {
    DWORD bg;
    DWORD hdr;
    DWORD key;
    DWORD keyBorder;
    DWORD dark;
    DWORD hover;
    DWORD hot;
    DWORD text;
    DWORD dim;
};

// Win11 Dark Theme (BGR format for GDI)
static const ThemeColors g_darkTheme = {
    0x1F1F1F,  // bg
    0x181818,  // hdr
    0x2C2C2C,  // key
    0x3A3A3A,  // keyBorder
    0x242424,  // dark
    0x383838,  // hover
    0xD47800,  // hot (Win11 accent blue #0078D4 in BGR)
    0xF5F5F5,  // text
    0xA0A0A0   // dim
};

// Win11 Light Theme (BGR format for GDI)
static const ThemeColors g_lightTheme = {
    0xF3F3F3,  // bg
    0xEAEAEA,  // hdr
    0xFFFFFF,  // key
    0xD6D6D6,  // keyBorder
    0xE8E8E8,  // dark
    0xF0F0F0,  // hover
    0xD47800,  // hot (Win11 accent blue #0078D4 in BGR)
    0x1A1A1A,  // text
    0x666666   // dim
};

// Theme mode: 0 = follow system, 1 = force dark, 2 = force light
static int g_themeMode = 0;
// Background material: 0 = off, 1 = Mica, 2 = Acrylic
static int g_materialMode = 0;
// 主界面透明度（%，100=不透明）：仅不支持系统 backdrop 的旧系统（PE/Win10 1803-）可用
static int g_mainOpacity = 100;
static BOOL g_isWinPE = FALSE;
static DWORD g_winBuild = 0;      // 系统Build号（RtlGetVersion，0=未知）
static BOOL g_isWin11 = FALSE;    // Win11（Build>=22000）：支持 Mica
static BOOL g_supportsMaterial = FALSE;  // Win10 1809+（Build>=17763）且非 PE：支持系统 backdrop
static void ApplyAllWindowMaterials();
// 是否启用“高亮按钮跟随系统壁纸强调色”（仅通过 -wallpaper 命令行参数开启，默认关闭）
static BOOL g_wallpaperAccent = FALSE;
static ThemeColors g_themeBuf;
static const ThemeColors* g_theme = &g_themeBuf;

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

static BOOL IsSystemBackdropDarkMode() {
    HKEY hKey;
    DWORD val = 1, sz = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"SystemUsesLightTheme", NULL, NULL, (LPBYTE)&val, &sz);
        RegCloseKey(hKey);
    }
    return val == 0;
}

static BOOL IsDarkThemeActive() {
    if (g_themeMode == 1) return TRUE;
    if (g_themeMode == 2) return FALSE;
    return g_materialMode != 0 ? IsSystemBackdropDarkMode() : IsSystemDarkMode();
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
    const ThemeColors* base;
    if (g_themeMode == 1) {
        base = &g_darkTheme;
    } else if (g_themeMode == 2) {
        base = &g_lightTheme;
    } else {
        base = IsDarkThemeActive() ? &g_darkTheme : &g_lightTheme;
    }

    g_themeBuf = *base;

    // 高亮色：自定义颜色优先；其次 -wallpaper 跟随壁纸强调色
    if (g_hlMode == 1) {
        g_themeBuf.hot = (DWORD)g_hlColor;
    } else if (g_wallpaperAccent) {
        DWORD accent = GetWallpaperAccentBgr();
        if (accent != 0) g_themeBuf.hot = accent;
    }

    g_theme = &g_themeBuf;
}

// 重新应用主题；颜色确实发生变化时刷新窗口
static void RefreshThemeAndRepaint(HWND hWnd) {
    ThemeColors before = g_themeBuf;
    ApplyTheme();
    if (memcmp(&before, &g_themeBuf, sizeof(ThemeColors)) != 0) {
        ApplyAllWindowMaterials();
        InvalidateRect(hWnd, 0, TRUE);
    }
}

// Convenience macros to access current theme colors
#define C_BG           (g_theme->bg)
#define C_HDR          (g_theme->hdr)
#define C_KEY          (g_theme->key)
#define C_KEY_BORDER   (g_theme->keyBorder)
#define C_DARK         (g_theme->dark)
#define C_HOVER        (g_theme->hover)
#define C_HOT          (g_theme->hot)
#define C_WHITE        (g_theme->text)
#define C_DIM          (g_theme->dim)

// Keep light material text readable without making every text tier the same
// near-black color. Explicitly selected Dark Theme keeps its original palette.
static DWORD ResolveFontColor(DWORD color) {
    if (g_materialMode != 0 && g_themeMode != 1) {
        if (color == C_DIM) return RGB(112, 112, 112);
        if (color == C_WHITE) return RGB(48, 48, 48);
    }
    return color;
}

enum KeyType {
    K_NORMAL, K_LETTER, K_MOD, K_CAPS,
    K_SPECIAL, K_ARROW, K_SPACE, K_HIDE, K_DOCK, K_MIN, K_CLOSE
};

struct KeyDef { int x, y, w, h; short vk; KeyType type; };

// C++ 函数前置声明
static void ShowKB(BOOL show, BOOL isManual = FALSE);
static void ToggleKB();
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

// Global state
HINSTANCE   g_hInst = 0;
HWND        g_hWnd = 0;
static HWND g_settingsHwnd = 0;
static HWND g_closePromptHwnd = 0;
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
BOOL        g_closeToTray = FALSE;     // × 关闭行为：TRUE=隐藏到托盘，FALSE=直接退出（默认直接退出）
BOOL        g_rememberClose = FALSE;   // 记住“× 关闭行为”的选择（持久化到注册表）
int         g_layoutMode = 0;          // 键盘布局：0=全尺寸 1=小键盘 2=常用
BOOL        g_fnWebLayout = FALSE;     // 按 Fn 切换到上网常用布局（否则为数字行 F1~F12 层）
BOOL        g_showFKeys = FALSE;       // 顶部显示 F1~F12 键
BOOL        g_shiftSymbols = TRUE;     // 按 Shift 时显示特殊符号（否则显示数字）
DWORD       g_lht = 0;
int         g_hk = -1, g_pk = -1;
static int  g_hdrHov = -1;            // 标题栏按钮悬停（HDR_*，-1=无）
int         g_repeatKeyIdx = -1;
BOOL        g_tracking = FALSE;
BOOL        g_tray = FALSE;
HWINEVENTHOOK g_winHook = 0;
HWINEVENTHOOK g_fgHook = 0;
HANDLE      g_mutex = 0;
HFONT       g_f12 = 0, g_f13 = 0, g_f13b = 0, g_f14 = 0, g_f14b = 0, g_f16b = 0, g_f18b = 0;
static HFONT g_sf12 = 0, g_sf13 = 0, g_sf13b = 0, g_sf14b = 0, g_sf20b = 0, g_sfIcon = 0;   // 设置/关闭窗口固定字号字体
static HANDLE g_fontRegRegular = 0;    // AddFontMemResourceEx 句柄（内嵌字体）
static HANDLE g_fontRegBold = 0;
static HANDLE g_fontRegMdl2 = 0;       // 内嵌 Segoe MDL2（Win10 以下系统图标字体）
static BOOL   g_fontReady = FALSE;     // 内嵌字体注册成功（失败回退系统字体）
static BOOL   g_mdl2Ready = FALSE;     // 内嵌 MDL2 注册成功（系统无 MDL2 时可用）
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

// 抗锯齿实心三角形（下拉框箭头等小图形）
static void DrawTriangleAA(HDC dc, int ax, int ay, int bx, int by, int cx, int cy, DWORD fill) {
    Gdiplus::Graphics g(dc);
    g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
    Gdiplus::Point pts[3] = { Gdiplus::Point(ax, ay), Gdiplus::Point(bx, by), Gdiplus::Point(cx, cy) };
    Gdiplus::SolidBrush br(Gdiplus::Color(255, GetRValue(fill), GetGValue(fill), GetBValue(fill)));
    g.FillPolygon(&br, pts, 3);
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
    } else {                        // 全尺寸/常用
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

// 常用布局（标准 87 键 TKL：无数字小键盘，可选 F1~F12 顶行）
// 始终保留 Esc 与 Fn：F 行开启时 Esc 位于顶行、Fn 隐藏；关闭时 Esc 在主键区行首、Fn 在底排
static void BuildCommon(int y, double dpiScale, double scaleX) {
    // F 行（可选）：Esc, F1~F12
    if (g_showFKeys) {
        int aw = (KEY_AREA_W - 12 * g_keyGap) / 13;
        int rem = KEY_AREA_W - 12 * g_keyGap - aw * 13;
        int x = KEY_AREA_X;
        for (int i = 0; i < 13; i++) {
            int w = aw + (i < rem ? 1 : 0);
            AddKey(x, y, w, g_keyHeight, (short)(i == 0 ? 0x1B : 0x70 + i - 1), (i == 0) ? K_SPECIAL : K_NORMAL);
            x += w + g_keyGap;
        }
        y += g_keyHeight + g_keyGap;
    }

    // Row 1: (Esc), `, 1-0, -, =, Backspace
    {
        int wEsc = (int)(50 * dpiScale * scaleX);
        int wBksp = (int)(80 * dpiScale * scaleX);
        if (!g_showFKeys) {
            // F 行关闭：Esc 保留在行首（15 键）
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
        } else {
            // F 行开启：Esc 已在顶行（14 键）
            int aw = (KEY_AREA_W - wBksp - 13 * g_keyGap) / 13;
            int rem = KEY_AREA_W - wBksp - 13 * g_keyGap - aw * 13;
            short v[14] = {0xC0,0x31,0x32,0x33,0x34,0x35,0x36,0x37,0x38,0x39,0x30,0xBD,0xBB,0x08};
            KeyType t[14] = {K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_NORMAL,K_SPECIAL};
            int x = KEY_AREA_X;
            for (int i = 0; i < 14; i++) {
                int w = (i == 13) ? wBksp : (aw + (i < rem ? 1 : 0));
                AddKey(x, y, w, g_keyHeight, v[i], t[i]);
                x += w + g_keyGap;
            }
        }
    }
    y += g_keyHeight + g_keyGap;

    // Row 2: Tab, Q-P, [, ], \ (14 keys)
    {
        int wTab = (int)(72 * dpiScale * scaleX);
        int aw = (KEY_AREA_W - wTab - 13 * g_keyGap) / 13;
        int rem = KEY_AREA_W - wTab - 13 * g_keyGap - aw * 13;
        short v[14] = {0x09,0x51,0x57,0x45,0x52,0x54,0x59,0x55,0x49,0x4F,0x50,0xDB,0xDD,0xDC};
        KeyType t[14] = {K_SPECIAL,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_NORMAL,K_NORMAL,K_NORMAL};
        int x = KEY_AREA_X;
        for (int i = 0; i < 14; i++) {
            int w = (i == 0) ? wTab : (aw + (i <= rem ? 1 : 0));
            AddKey(x, y, w, g_keyHeight, v[i], t[i]);
            x += w + g_keyGap;
        }
    }
    y += g_keyHeight + g_keyGap;

    // Row 3: Caps, A-L, ;, ', Enter (13 keys)
    {
        int wCaps = (int)(86 * dpiScale * scaleX);
        int wEnter = (int)(96 * dpiScale * scaleX);
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

    // Row 4: LShift, Z-M, ,, ., /, ↑, RShift (14 keys)
    {
        int wLSh = (int)(95 * dpiScale * scaleX);
        int wUp = (int)(52 * dpiScale * scaleX);
        int wRSh = wUp;
        int fixed = wLSh + wRSh + wUp;
        int aw = (KEY_AREA_W - fixed - 12 * g_keyGap) / 10;
        int rem = KEY_AREA_W - fixed - 12 * g_keyGap - aw * 10;
        int w[13]; w[0] = wLSh;
        for (int i = 1; i <= 10; i++) w[i] = aw + (i <= rem ? 1 : 0);
        w[11] = wUp; w[12] = wRSh;
        short v[13] = {0xA0,0x5A,0x58,0x43,0x56,0x42,0x4E,0x4D,0xBC,0xBE,0xBF,0x26,0xA1};
        KeyType t[13] = {K_MOD,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_LETTER,K_NORMAL,K_NORMAL,K_NORMAL,K_ARROW,K_MOD};
        int x = KEY_AREA_X;
        for (int i = 0; i < 13; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
    }
    y += g_keyHeight + g_keyGap;

    // Row 5: (Fn), Ctrl, Win, Alt, Space, Alt, Menu, Ctrl, ←, ↓, →
    // F 行开启时隐藏 Fn（Esc 已在顶行保留）
    {
        int wFn  = (int)(46 * dpiScale * scaleX);
        int wCtl = (int)(56 * dpiScale * scaleX);
        int wWin = (int)(46 * dpiScale * scaleX);
        int wAlt = (int)(58 * dpiScale * scaleX);
        int wMenu = (int)(56 * dpiScale * scaleX);
        int wArw = (int)(52 * dpiScale * scaleX);
        if (g_showFKeys) {
            int leftOfArrows = wCtl + wWin + wAlt + wAlt + wMenu + wCtl;
            int spaceW = KEY_AREA_W - leftOfArrows - wArw * 3 - 9 * g_keyGap;
            if (spaceW < 60) spaceW = 60;
            int w[10] = {wCtl, wWin, wAlt, spaceW, wAlt, wMenu, wCtl, wArw, wArw, wArw};
            short v[10] = {0x11, 0x5B, 0x12, 0x20, 0x12, 0x5D, 0x11, 0x25, 0x28, 0x27};
            KeyType t[10] = {K_MOD, K_SPECIAL, K_MOD, K_SPACE, K_MOD, K_MOD, K_MOD, K_ARROW, K_ARROW, K_ARROW};
            int x = KEY_AREA_X;
            for (int i = 0; i < 10; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
        } else {
            int leftOfArrows = wFn + wCtl + wWin + wAlt + wAlt + wMenu + wCtl;
            int spaceW = KEY_AREA_W - leftOfArrows - wArw * 3 - 10 * g_keyGap;
            if (spaceW < 60) spaceW = 60;
            int w[11] = {wFn, wCtl, wWin, wAlt, spaceW, wAlt, wMenu, wCtl, wArw, wArw, wArw};
            short v[11] = {0, 0x11, 0x5B, 0x12, 0x20, 0x12, 0x5D, 0x11, 0x25, 0x28, 0x27};
            KeyType t[11] = {K_SPECIAL, K_MOD, K_SPECIAL, K_MOD, K_SPACE, K_MOD, K_MOD, K_MOD, K_ARROW, K_ARROW, K_ARROW};
            int x = KEY_AREA_X;
            for (int i = 0; i < 11; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
        }
    }
}

// Fn 网页布局层：整体结构跟随当前布局样式（全尺寸/常用），
// 仅行1 数字键换为 F1~F12、行4 字母键换为网址后缀键
static void BuildFnSurf(int y, double dpiScale, double scaleX) {
    BOOL commonStyle = (g_layoutMode == 2);   // 常用布局：行2 无 Del、行5 带 Menu
    // Row 1: Esc, `, F1~F12, Backspace (15 keys)
    {
        int wEsc = (int)(50 * dpiScale * scaleX);
        int wBksp = (int)(68 * dpiScale * scaleX);
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

    // Row 2: Tab, q-p, [, ], \, Del (15 keys)；常用布局无 Del (14 keys)
    {
        int wTab = (int)(68 * dpiScale * scaleX);
        int wDel = (int)(68 * dpiScale * scaleX);
        if (commonStyle) {
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
        y += g_keyHeight + g_keyGap;
    }

    // Row 3: Caps, a-l, ;, ', Enter (13 keys)，与全尺寸布局一致
    {
        int wCaps = (int)(80 * dpiScale * scaleX);
        int wEnter = (int)(90 * dpiScale * scaleX);
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
        int wUp = (int)(52 * dpiScale * scaleX);
        int wRSh = wUp;
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

    // Row 5: Fn, Ctrl, Win, Alt, 空格, Alt, (Menu), Ctrl, ←, ↓, →
    // 常用布局带 Menu 键（11 keys）；全尺寸与原布局一致（10 keys）
    {
        int wFn  = (int)(46 * dpiScale * scaleX);
        int wCtl = (int)(56 * dpiScale * scaleX);
        int wWin = (int)(46 * dpiScale * scaleX);
        int wAlt = (int)(58 * dpiScale * scaleX);
        int wMenu = (int)(56 * dpiScale * scaleX);
        int wArw = (int)(52 * dpiScale * scaleX);
        if (commonStyle) {
            int leftOfArrows = wFn + wCtl + wWin + wAlt + wAlt + wMenu + wCtl;
            int spaceW = KEY_AREA_W - leftOfArrows - wArw * 3 - 10 * g_keyGap;
            if (spaceW < 60) spaceW = 60;
            int w[11] = {wFn, wCtl, wWin, wAlt, spaceW, wAlt, wMenu, wCtl, wArw, wArw, wArw};
            short v[11] = {0, 0x11, 0x5B, 0x12, 0x20, 0x12, 0x5D, 0x11, 0x25, 0x28, 0x27};
            KeyType t[11] = {K_SPECIAL, K_MOD, K_SPECIAL, K_MOD, K_SPACE, K_MOD, K_MOD, K_MOD, K_ARROW, K_ARROW, K_ARROW};
            int x = KEY_AREA_X;
            for (int i = 0; i < 11; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
        } else {
            int leftOfArrows = wFn + wCtl + wWin + wAlt + wAlt + wCtl;
            int spaceW = KEY_AREA_W - leftOfArrows - wArw * 3 - 9 * g_keyGap;
            if (spaceW < 60) spaceW = 60;
            int w[10] = {wFn, wCtl, wWin, wAlt, spaceW, wAlt, wCtl, wArw, wArw, wArw};
            short v[10] = {0, 0x11, 0x5B, 0x12, 0x20, 0x12, 0x11, 0x25, 0x28, 0x27};
            KeyType t[10] = {K_SPECIAL, K_MOD, K_SPECIAL, K_MOD, K_SPACE, K_MOD, K_MOD, K_ARROW, K_ARROW, K_ARROW};
            int x = KEY_AREA_X;
            for (int i = 0; i < 10; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
        }
    }
}

static void BuildKeys() {
    g_nk = 0;

    double dpiScale = GetSystemDpiScale();
    double baseW = 980.0 * dpiScale;
    double baseH = 320.0 * dpiScale;

    double scaleX = (double)g_ww / baseW;
    double scaleY = (double)g_wh / baseH;

    g_headerH = (int)(36.0 * dpiScale * scaleY); if (g_headerH < 28) g_headerH = 28;
    g_keyGap = (int)(4.0 * dpiScale * scaleX); if (g_keyGap < 2) g_keyGap = 2;
    // 圆角窗口四角会裁掉内容：键区左右与底部留出安全边距
    g_keyAreaX = (int)(10 * dpiScale); if (g_keyAreaX < 6) g_keyAreaX = 6;
    int bottomPad = (int)(16 * dpiScale); if (bottomPad < 8) bottomPad = 8;

    // 行数：全尺寸 5 行 + 可选 F1~F12 顶行；小键盘 5 行；Fn 网页布局固定 5 行
    BOOL webSurf = g_fnLayer && g_fnWebLayout && g_layoutMode != 1;
    int rows = 5 + (g_layoutMode != 1 && g_showFKeys && !webSurf ? 1 : 0);
    g_keyHeight = (g_wh - g_headerH - bottomPad - (rows - 1) * g_keyGap) / rows;
    if (g_keyHeight < 20) g_keyHeight = 20;

    int y = g_headerH + g_keyGap + 2;

    if (g_layoutMode == 1) {   // 小键盘
        BuildNumpad(y);
        return;
    }

    // Fn 网页布局层（按 Fn 键切换，全尺寸/常用布局下生效）
    if (g_fnLayer && g_fnWebLayout) {
        BuildFnSurf(y, dpiScale, scaleX);
        return;
    }

    if (g_layoutMode == 2) {   // 常用（标准 TKL）
        BuildCommon(y, dpiScale, scaleX);
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
        int wBksp = (int)(68 * dpiScale * scaleX);
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
        int wEnter = (int)(90 * dpiScale * scaleX);
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
        int wUp = (int)(52 * dpiScale * scaleX);
        int wRSh = wUp;
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

    // Row 4: Fn, Ctrl, Win, Alt, 空格, Alt, Ctrl, ←, ↓, →  (10 keys)；F 行开启时隐藏 Fn
    {
        int wFn  = (int)(46 * dpiScale * scaleX);
        int wCtl = (int)(56 * dpiScale * scaleX);
        int wWin = (int)(46 * dpiScale * scaleX);
        int wAlt = (int)(58 * dpiScale * scaleX);
        int wArw = (int)(52 * dpiScale * scaleX);
        if (g_showFKeys) {
            // 无 Fn：Ctrl, Win, Alt, 空格, Alt, Ctrl, ←, ↓, → (9 keys)
            int leftOfArrows = wCtl + wWin + wAlt + wAlt + wCtl;
            int spaceW = KEY_AREA_W - leftOfArrows - wArw * 3 - 8 * g_keyGap;
            if (spaceW < 60) spaceW = 60;
            int w[9] = {wCtl, wWin, wAlt, spaceW, wAlt, wCtl, wArw, wArw, wArw};
            short v[9] = {0x11, 0x5B, 0x12, 0x20, 0x12, 0x11, 0x25, 0x28, 0x27};
            KeyType t[9] = {K_MOD, K_SPECIAL, K_MOD, K_SPACE, K_MOD, K_MOD, K_ARROW, K_ARROW, K_ARROW};
            int x = KEY_AREA_X;
            for (int i = 0; i < 9; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
        } else {
            int leftOfArrows = wFn + wCtl + wWin + wAlt + wAlt + wCtl;
            int spaceW = KEY_AREA_W - leftOfArrows - wArw * 3 - 9 * g_keyGap;
            if (spaceW < 60) spaceW = 60;
            int w[10] = {wFn, wCtl, wWin, wAlt, spaceW, wAlt, wCtl, wArw, wArw, wArw};
            short v[10] = {0, 0x11, 0x5B, 0x12, 0x20, 0x12, 0x11, 0x25, 0x28, 0x27};
            KeyType t[10] = {K_SPECIAL, K_MOD, K_SPECIAL, K_MOD, K_SPACE, K_MOD, K_MOD, K_ARROW, K_ARROW, K_ARROW};
            int x = KEY_AREA_X;
            for (int i = 0; i < 10; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
        }
    }
}

// 注册内嵌字体（MiSans 精简版）到当前进程；失败则回退系统字体
static void LoadEmbeddedFonts() {
    struct { int id; HANDLE* slot; } fonts[3] = {
        { IDR_FONT_REGULAR, &g_fontRegRegular },
        { IDR_FONT_BOLD,    &g_fontRegBold },
        { IDR_FONT_MDL2,    &g_fontRegMdl2 },
    };
    for (int i = 0; i < 3; i++) {
        // Win10 及以上系统自带 Segoe MDL2/Fluent，跳过内嵌图标字体（省内存）
        if (fonts[i].id == IDR_FONT_MDL2 && g_winBuild >= 10240) continue;
        HRSRC hr = FindResourceW(g_hInst, MAKEINTRESOURCEW(fonts[i].id), MAKEINTRESOURCEW(10));  // RT_RCDATA
        if (!hr) continue;
        HGLOBAL hg = LoadResource(g_hInst, hr);
        if (!hg) continue;
        void* data = LockResource(hg);
        DWORD sz = SizeofResource(g_hInst, hr);
        if (!data || sz == 0) continue;
        DWORD n = 0;
        HANDLE h = AddFontMemResourceEx(data, sz, NULL, &n);
        if (h && n > 0) {
            *fonts[i].slot = h;
            if (fonts[i].id == IDR_FONT_MDL2) g_mdl2Ready = TRUE;
            else g_fontReady = TRUE;
        }
    }
}
// 创建 UI 字体。粗体请求后用 GetTextMetrics 校验实际匹配到的字重：
// 内嵌 MiSans Bold（或系统安装版的 Bold 字面）可用时得到真粗体；
// 只匹配到 Regular 字面时退回 FW_NORMAL，避免 GDI 仿真加粗产生的重影粗体。
static HFONT MakeFont(double size, BOOL bold) {
    HDC hdc = GetDC(0);
    int h = -MulDiv((int)(size * 10 + 0.5), 96, 720);
    ReleaseDC(0, hdc);
    const wchar_t* face = g_fontReady ? L"MiSans" : L"Microsoft YaHei";
    HFONT f = CreateFontW(h, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, face);
    if (bold) {
        HDC dc = GetDC(0);
        HFONT of = (HFONT)SelectObject(dc, f);
        TEXTMETRICW tm;
        BOOL ok = GetTextMetricsW(dc, &tm);
        SelectObject(dc, of);
        ReleaseDC(0, dc);
        if (!ok || tm.tmWeight < 550) {   // 实际匹配到 Regular 字面：放弃仿真加粗
            DeleteObject(f);
            f = CreateFontW(h, 0, 0, 0, FW_NORMAL,
                FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
                DEFAULT_PITCH | FF_DONTCARE, face);
        }
    }
    return f;
}

static HFONT MakeIconFont(double size) {
    HDC dc = GetDC(NULL);
    int dpi = GetDeviceCaps(dc, LOGPIXELSY);
    int height = -MulDiv((int)(size + 0.5), dpi, 72);
    ReleaseDC(NULL, dc);

    // 图标字体按系统环境选择：
    //   Win10 及以上：Fluent Icons（Win11）/ MDL2（Win10）
    //   Win10 以下（Win7/8.x）：MDL2 优先——系统未自带时由内嵌副本注册提供，
    //   Segoe UI Symbol 作为未注册成功的兜底
    const wchar_t* faces[4];
    int n = 0;
    if (g_winBuild >= 10240) {
        faces[n++] = L"Segoe Fluent Icons";
        faces[n++] = L"Segoe MDL2 Assets";
    } else {
        faces[n++] = L"Segoe MDL2 Assets";
        faces[n++] = L"Segoe UI Symbol";
    }
    if (g_winBuild < 10240 && g_mdl2Ready) n = 1;   // 内嵌 MDL2 已注册，仅保留首选
    for (int i = 0; i < n; i++) {
        HFONT font = CreateFontW(height, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, faces[i]);
        if (!font) continue;
        dc = GetDC(NULL);
        HFONT old = (HFONT)SelectObject(dc, font);
        wchar_t actual[LF_FACESIZE] = {0};
        GetTextFaceW(dc, LF_FACESIZE, actual);
        SelectObject(dc, old);
        ReleaseDC(NULL, dc);
        if (_wcsicmp(actual, faces[i]) == 0) return font;
        DeleteObject(font);
    }
    return MakeFont(size, FALSE);
}
// 设置/关闭窗口使用固定字号字体（不随主键盘窗口缩放，仅随 DPI）
static void InitFixedFonts() {
    double dpi = GetSystemDpiScale();
    g_sf12  = MakeFont(10.5 * dpi, 0);   // 灰色小字（提示/正文）
    g_sf13  = MakeFont(10.5 * dpi, 0);   // 行文本/开关标签
    g_sf13b = MakeFont(10.5 * dpi, 1);   // Tab / 按钮
    g_sf14b = MakeFont(11.5 * dpi, 1);   // 面板标题
    g_sf20b = MakeFont(20 * dpi, 1);     // 设置页大标题
    g_sfIcon = MakeIconFont(16);         // 紧凑 Windows 11 Fluent 图标
}

static void RecreateFontsAndLayout() {
    if (g_f12) DeleteObject(g_f12);
    if (g_f13) DeleteObject(g_f13);
    if (g_f13b) DeleteObject(g_f13b);
    if (g_f14) DeleteObject(g_f14);
    if (g_f14b) DeleteObject(g_f14b);
    if (g_f16b) DeleteObject(g_f16b);
    if (g_f18b) DeleteObject(g_f18b);

    double dpiScale = GetSystemDpiScale();
    double baseH = 320.0 * dpiScale;
    double scaleY = (double)g_wh / baseH;
    if (scaleY < 0.4) scaleY = 0.4;

    double finalFontScale = dpiScale * scaleY;

    g_f12  = MakeFont((int)(12 * finalFontScale), 0);
    g_f13  = MakeFont((int)(13 * finalFontScale), 0);
    g_f13b = MakeFont((int)(13 * finalFontScale), 1);
    g_f14  = MakeFont((int)(14 * finalFontScale), 0);
    g_f14b = MakeFont((int)(14 * finalFontScale), 1);
    g_f16b = MakeFont((int)(16 * finalFontScale), 1);
    g_f18b = MakeFont((int)(18 * finalFontScale), 1);

    BuildKeys();
}

static void Fill(HDC dc, int x, int y, int w, int h, DWORD c) {
    if (w <= 0 || h <= 0) return;
    Gdiplus::Graphics graphics(dc);
    Gdiplus::SolidBrush brush(Gdiplus::Color(255, GetRValue(c), GetGValue(c), GetBValue(c)));
    graphics.FillRectangle(&brush, x, y, w, h);
}

static void DrawLineAA(HDC dc, int x1, int y1, int x2, int y2, DWORD color, float width) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    Gdiplus::Pen pen(Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color)), width);
    graphics.DrawLine(&pen, x1, y1, x2, y2);
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

static void DrawAlphaSurface(HDC dc, int x, int y, int w, int h, DWORD color, BYTE alpha) {
    if (w <= 0 || h <= 0 || alpha == 0) return;
    Gdiplus::Graphics g(dc);
    Gdiplus::SolidBrush brush(Gdiplus::Color(alpha, GetRValue(color),
                                             GetGValue(color), GetBValue(color)));
    g.FillRectangle(&brush, x, y, w, h);
}

typedef HRESULT (WINAPI *DwmSetWindowAttributeProc)(HWND, DWORD, LPCVOID, DWORD);
struct DwmMarginsLocal { int left, right, top, bottom; };
typedef HRESULT (WINAPI *DwmExtendFrameIntoClientAreaProc)(HWND, const DwmMarginsLocal*);
typedef HRESULT (WINAPI *DwmFlushProc)();
typedef HANDLE PaintBufferHandleLocal;
typedef HRESULT (WINAPI *BufferedPaintInitProc)();
typedef PaintBufferHandleLocal (WINAPI *BeginBufferedPaintProc)(HDC, const RECT*, int, const void*, HDC*);
typedef HRESULT (WINAPI *EndBufferedPaintProc)(PaintBufferHandleLocal, BOOL);
typedef HRESULT (WINAPI *BufferedPaintClearProc)(PaintBufferHandleLocal, const RECT*);
typedef HRESULT (WINAPI *GetBufferedPaintBitsProc)(PaintBufferHandleLocal, RGBQUAD**, int*);

struct AccentPolicyLocal {
    int state;
    int flags;
    DWORD gradientColor;
    int animationId;
};
struct WindowCompositionAttribDataLocal {
    int attrib;
    PVOID data;
    SIZE_T size;
};
typedef BOOL (WINAPI *SetWindowCompositionAttributeProc)(HWND, WindowCompositionAttribDataLocal*);

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

static DwmExtendFrameIntoClientAreaProc GetDwmExtendFrameIntoClientArea() {
    static HMODULE dwm = NULL;
    static DwmExtendFrameIntoClientAreaProc proc = NULL;
    static BOOL initialized = FALSE;
    if (!initialized) {
        initialized = TRUE;
        dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) proc = (DwmExtendFrameIntoClientAreaProc)GetProcAddress(dwm, "DwmExtendFrameIntoClientArea");
    }
    return proc;
}

static DwmFlushProc GetDwmFlush() {
    static HMODULE dwm = NULL;
    static DwmFlushProc proc = NULL;
    static BOOL initialized = FALSE;
    if (!initialized) {
        initialized = TRUE;
        dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) proc = (DwmFlushProc)GetProcAddress(dwm, "DwmFlush");
    }
    return proc;
}

struct BufferedPaintApiLocal {
    BufferedPaintInitProc init;
    BeginBufferedPaintProc begin;
    EndBufferedPaintProc end;
    BufferedPaintClearProc clear;
    GetBufferedPaintBitsProc bits;
};

static const BufferedPaintApiLocal* GetBufferedPaintApi() {
    static HMODULE module = NULL;
    static BufferedPaintApiLocal api = {};
    static BOOL initialized = FALSE;
    if (!initialized) {
        initialized = TRUE;
        module = LoadLibraryW(L"uxtheme.dll");
        if (module) {
            api.init = (BufferedPaintInitProc)GetProcAddress(module, "BufferedPaintInit");
            api.begin = (BeginBufferedPaintProc)GetProcAddress(module, "BeginBufferedPaint");
            api.end = (EndBufferedPaintProc)GetProcAddress(module, "EndBufferedPaint");
            api.clear = (BufferedPaintClearProc)GetProcAddress(module, "BufferedPaintClear");
            api.bits = (GetBufferedPaintBitsProc)GetProcAddress(module, "GetBufferedPaintBits");
            if (!api.init || !api.begin || !api.end || FAILED(api.init())) {
                ZeroMemory(&api, sizeof(api));
            }
        }
    }
    return api.begin ? &api : NULL;
}

static SetWindowCompositionAttributeProc GetSetWindowCompositionAttribute() {
    static SetWindowCompositionAttributeProc proc = NULL;
    static BOOL initialized = FALSE;
    if (!initialized) {
        initialized = TRUE;
        HMODULE user32 = GetModuleHandleW(L"user32.dll");
        if (user32) proc = (SetWindowCompositionAttributeProc)GetProcAddress(user32, "SetWindowCompositionAttribute");
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
    // Win11+：保留系统原生 DWM 圆角；Win10 及以下（含对应版本 PE）强制直角窗口
    if (g_winBuild >= 22000) {
        if (TryApplyWin11RoundedWindow(hWnd)) return;
    }
    SetWindowRgn(hWnd, NULL, TRUE);
}

static BOOL IsMaterialApplied(HWND hWnd) {
    return hWnd && GetPropW(hWnd, L"HKeyboardMaterial") != NULL;
}

static BOOL g_alphaPaintActive = FALSE;
static BOOL g_bufferedPaintActive = FALSE;   // 当前画布是否为 DWM 缓冲透明画布（材质模式）
static RGBQUAD* g_alphaPaintBits = NULL;
static int g_alphaPaintRowPixels = 0;
static RECT g_alphaPaintRect = {0, 0, 0, 0};

struct WindowPaintSurfaceLocal {
    HDC dc;
    PaintBufferHandleLocal buffered;
    HDC memory;
    HBITMAP bitmap;
    HBITMAP oldBitmap;
    BOOL alpha;
    BOOL previousAlpha;
    BOOL previousBuffered;
    RGBQUAD* previousBits;
    int previousRowPixels;
    RECT previousRect;
};

static WindowPaintSurfaceLocal BeginWindowPaintSurface(HDC target, HWND hWnd, const RECT& rc) {
    WindowPaintSurfaceLocal surface = {};
    surface.previousAlpha = g_alphaPaintActive;
    surface.previousBuffered = g_bufferedPaintActive;
    surface.previousBits = g_alphaPaintBits;
    surface.previousRowPixels = g_alphaPaintRowPixels;
    surface.previousRect = g_alphaPaintRect;

    if (IsMaterialApplied(hWnd)) {
        const BufferedPaintApiLocal* api = GetBufferedPaintApi();
        if (api) {
            HDC bufferedDc = NULL;
            surface.buffered = api->begin(target, &rc, 2, NULL, &bufferedDc); // BPBF_TOPDOWNDIB
            if (surface.buffered && bufferedDc) {
                surface.dc = bufferedDc;
                surface.alpha = TRUE;
                g_alphaPaintActive = TRUE;
                g_bufferedPaintActive = TRUE;
                api->clear(surface.buffered, NULL);
                g_alphaPaintBits = NULL;
                g_alphaPaintRowPixels = 0;
                g_alphaPaintRect = rc;
                if (api->bits) {
                    RGBQUAD* bits = NULL;
                    int rowPixels = 0;
                    if (SUCCEEDED(api->bits(surface.buffered, &bits, &rowPixels)) &&
                        bits && rowPixels > 0) {
                        g_alphaPaintBits = bits;
                        g_alphaPaintRowPixels = rowPixels;
                    }
                }
                return surface;
            }
        }
    }

    // 无材质模式：同样使用 32bpp 顶向下 DIB，使文字统一走 alpha 混合路径，
    // 保证浅色/深色/材质各模式下的文字渲染观感完全一致。
    {
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
                surface.alpha = FALSE;
                g_alphaPaintActive = TRUE;
                g_bufferedPaintActive = FALSE;
                g_alphaPaintBits = (RGBQUAD*)bits;
                g_alphaPaintRowPixels = w;
                g_alphaPaintRect = rc;
                return surface;
            }
            // DIB 创建失败：回退普通兼容位图（GDI 直绘文字）
            if (surface.memory) { DeleteDC(surface.memory); surface.memory = NULL; }
            if (surface.bitmap) { DeleteObject(surface.bitmap); surface.bitmap = NULL; }
        }
    }

    surface.memory = CreateCompatibleDC(target);
    surface.bitmap = CreateCompatibleBitmap(target, rc.right - rc.left, rc.bottom - rc.top);
    surface.oldBitmap = (HBITMAP)SelectObject(surface.memory, surface.bitmap);
    surface.dc = surface.memory;
    surface.alpha = FALSE;
    g_alphaPaintActive = FALSE;
    g_bufferedPaintActive = FALSE;
    g_alphaPaintBits = NULL;
    g_alphaPaintRowPixels = 0;
    return surface;
}

static void EndWindowPaintSurface(WindowPaintSurfaceLocal* surface, BOOL commit) {
    if (!surface) return;
    if (surface->buffered) {
        const BufferedPaintApiLocal* api = GetBufferedPaintApi();
        if (api) api->end(surface->buffered, commit);
        g_alphaPaintActive = surface->previousAlpha;
        g_bufferedPaintActive = surface->previousBuffered;
        g_alphaPaintBits = surface->previousBits;
        g_alphaPaintRowPixels = surface->previousRowPixels;
        g_alphaPaintRect = surface->previousRect;
        return;
    }
    if (surface->memory) {
        SelectObject(surface->memory, surface->oldBitmap);
        if (surface->bitmap) DeleteObject(surface->bitmap);
        DeleteDC(surface->memory);
    }
    g_alphaPaintActive = surface->previousAlpha;
    g_bufferedPaintActive = surface->previousBuffered;
    g_alphaPaintBits = surface->previousBits;
    g_alphaPaintRowPixels = surface->previousRowPixels;
    g_alphaPaintRect = surface->previousRect;
}

static void ClearWindowBackBuffer(HDC dc, HWND hWnd, int w, int h) {
    // DWM backdrop requires a clean glass surface. Copying the previous window DC
    // reintroduces stale pixels and causes Tab/page ghosting after every repaint.
    // 仅缓冲透明画布（材质模式）跳过填充；无材质 DIB 画布必须填不透明底色。
    if (IsMaterialApplied(hWnd) && g_bufferedPaintActive) return;
    Fill(dc, 0, 0, w, h, IsMaterialApplied(hWnd) ? 0x000000 : C_BG);
}

static BOOL IsWindowsPE() {
    HKEY key = NULL;
    LONG result = RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\MiniNT", 0, KEY_READ, &key);
    if (result == ERROR_SUCCESS) RegCloseKey(key);
    return result == ERROR_SUCCESS;
}

// 检测系统版本（RtlGetVersion 不受兼容性清单影响）
// Win11 = Build 22000+（支持 Mica）；Win10 1809+ = Build 17763+（支持亚克力 backdrop）
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
    g_isWin11 = g_winBuild >= 22000;
    g_supportsMaterial = !g_isWinPE && g_winBuild >= 17763;
}

// 主界面透明度（分层窗口统一透明）：Win2000+ 均支持，
// 用于无系统 backdrop 的环境（Win7/8.x、精简版 Win10/11 PE 等）
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

static void ApplyWindowMaterial(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd)) return;

    DwmSetWindowAttributeProc setAttr = GetDwmSetWindowAttribute();
    DwmExtendFrameIntoClientAreaProc extendFrame = GetDwmExtendFrameIntoClientArea();
    DwmFlushProc flush = GetDwmFlush();
    SetWindowCompositionAttributeProc setComposition = GetSetWindowCompositionAttribute();
    BOOL applied = FALSE;
    BOOL extendBackdrop = FALSE;

    // Fully reset the previous material first. Mica and Acrylic use different
    // composition paths and otherwise leak state into each other when switched.
    if (setComposition) {
        AccentPolicyLocal disabled = {0, 0, 0, 0};
        WindowCompositionAttribDataLocal data = {19, &disabled, sizeof(disabled)};
        setComposition(hWnd, &data);
    }
    if (setAttr) {
        const DWORD DWMWA_SYSTEMBACKDROP_TYPE_VALUE = 38;
        int none = 1;
        setAttr(hWnd, DWMWA_SYSTEMBACKDROP_TYPE_VALUE, &none, sizeof(none));
    }
    if (extendFrame) {
        DwmMarginsLocal margins = {0, 0, 0, 0};
        extendFrame(hWnd, &margins);
    }
    RemovePropW(hWnd, L"HKeyboardMaterial");
    if (flush) flush();

    if (setAttr) {
        const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_VALUE = 20;
        BOOL dark = g_theme->bg == g_darkTheme.bg;
        if (FAILED(setAttr(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE_VALUE, &dark, sizeof(dark)))) {
            const DWORD DWMWA_USE_IMMERSIVE_DARK_MODE_OLD_VALUE = 19;
            setAttr(hWnd, DWMWA_USE_IMMERSIVE_DARK_MODE_OLD_VALUE, &dark, sizeof(dark));
        }
    }

    if (g_materialMode == 0) {
        // 无系统 backdrop 的环境（Win7/8.x、PE、Win10 1803-）：
        // 主界面透明度用分层窗口实现，兼容性最好
        ApplyWindowOpacity(hWnd, !g_supportsMaterial && hWnd == g_hWnd && g_mainOpacity < 100);
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
        return;
    }

    // Windows 11 native backdrops. Mica and Acrylic stay on the same DWM path.
    if (setAttr) {
        const DWORD DWMWA_SYSTEMBACKDROP_TYPE_VALUE = 38;
        int backdrop = g_materialMode == 1 ? 2 : 3;
        if (SUCCEEDED(setAttr(hWnd, DWMWA_SYSTEMBACKDROP_TYPE_VALUE,
                              &backdrop, sizeof(backdrop)))) {
            applied = TRUE;
            extendBackdrop = TRUE;
        }
    }

    // Compatibility fallback for Windows 10 or older Win11 builds.
    if (!applied && setComposition) {
        AccentPolicyLocal policy = {0, 0, 0, 0};
        BOOL dark = g_theme->bg == g_darkTheme.bg;
        policy.state = g_materialMode == 1 ? 5 : 4;
        policy.flags = 2;
        DWORD tintAlpha = g_materialMode == 1 ? (dark ? 0xE0 : 0xE4)
                                               : (dark ? 0xC0 : 0xC8);
        policy.gradientColor = (tintAlpha << 24) | (C_BG & 0x00FFFFFF);
        WindowCompositionAttribDataLocal data = {19, &policy, sizeof(policy)};
        if (setComposition(hWnd, &data)) {
            applied = TRUE;
            extendBackdrop = TRUE;
        }
    }

    if (extendFrame) {
        DwmMarginsLocal margins = {0, 0, 0, 0};
        if (extendBackdrop) margins.left = margins.right = margins.top = margins.bottom = -1;
        extendFrame(hWnd, &margins);
    }

    if (applied) {
        SetPropW(hWnd, L"HKeyboardMaterial", (HANDLE)(INT_PTR)1);
        ApplyWindowOpacity(hWnd, FALSE);   // 材质与分层透明互斥，切材质时清除
    } else {
        RemovePropW(hWnd, L"HKeyboardMaterial");
    }
    RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
}

static void ApplyAllWindowMaterials() {
    ApplyWindowMaterial(g_hWnd);
    ApplyWindowMaterial(g_settingsHwnd);
    ApplyWindowMaterial(g_closePromptHwnd);
}

static BOOL DrawMaterialText(HDC dc, int x, int y, int w, int h,
                             const wchar_t* text, HFONT font, DWORD color,
                             Gdiplus::StringAlignment alignment) {
    if (!text || !font || w <= 0 || h <= 0) return FALSE;
    if (!g_alphaPaintActive || !g_alphaPaintBits || g_alphaPaintRowPixels <= 0)
        return FALSE;

    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = w;
    bmi.bmiHeader.biHeight = -h;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* rawMask = NULL;
    HBITMAP maskBitmap = CreateDIBSection(dc, &bmi, DIB_RGB_COLORS, &rawMask, NULL, 0);
    if (!maskBitmap || !rawMask) {
        if (maskBitmap) DeleteObject(maskBitmap);
        return FALSE;
    }
    ZeroMemory(rawMask, (SIZE_T)w * (SIZE_T)h * sizeof(RGBQUAD));

    HDC maskDc = CreateCompatibleDC(dc);
    if (!maskDc) {
        DeleteObject(maskBitmap);
        return FALSE;
    }
    HBITMAP oldBitmap = (HBITMAP)SelectObject(maskDc, maskBitmap);
    HFONT oldFont = (HFONT)SelectObject(maskDc, font);
    SetBkMode(maskDc, TRANSPARENT);
    SetTextColor(maskDc, RGB(255, 255, 255));
    RECT maskRect = {0, 0, w, h};
    UINT flags = DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX;
    flags |= alignment == Gdiplus::StringAlignmentCenter ? DT_CENTER : DT_LEFT;
    int drawn = DrawTextW(maskDc, text, -1, &maskRect, flags);

    if (drawn > 0) {
        RGBQUAD* mask = (RGBQUAD*)rawMask;
        DWORD resolved = ResolveFontColor(color);
        int red = GetRValue(resolved);
        int green = GetGValue(resolved);
        int blue = GetBValue(resolved);
        int dstX = x - g_alphaPaintRect.left;
        int dstY = y - g_alphaPaintRect.top;
        int paintW = g_alphaPaintRect.right - g_alphaPaintRect.left;
        int paintH = g_alphaPaintRect.bottom - g_alphaPaintRect.top;
        if (paintW > g_alphaPaintRowPixels) paintW = g_alphaPaintRowPixels;

        int startX = dstX < 0 ? -dstX : 0;
        int startY = dstY < 0 ? -dstY : 0;
        int endX = w;
        int endY = h;
        if (dstX + endX > paintW) endX = paintW - dstX;
        if (dstY + endY > paintH) endY = paintH - dstY;

        for (int py = startY; py < endY; py++) {
            RGBQUAD* srcRow = mask + py * w;
            RGBQUAD* dstRow = g_alphaPaintBits + (dstY + py) * g_alphaPaintRowPixels;
            for (int px = startX; px < endX; px++) {
                BYTE alpha = srcRow[px].rgbBlue;
                if (alpha == 0) continue;
                RGBQUAD* dst = dstRow + dstX + px;
                int inverse = 255 - alpha;
                dst->rgbRed = (BYTE)((red * alpha + dst->rgbRed * inverse + 127) / 255);
                dst->rgbGreen = (BYTE)((green * alpha + dst->rgbGreen * inverse + 127) / 255);
                dst->rgbBlue = (BYTE)((blue * alpha + dst->rgbBlue * inverse + 127) / 255);
                dst->rgbReserved = (BYTE)(alpha + (dst->rgbReserved * inverse + 127) / 255);
            }
        }
    }

    SelectObject(maskDc, oldFont);
    SelectObject(maskDc, oldBitmap);
    DeleteDC(maskDc);
    DeleteObject(maskBitmap);
    return drawn > 0;
}

static void DrawTextC(HDC dc, int x, int y, int w, int h, const wchar_t* s, HFONT f, DWORD c) {
    if (DrawMaterialText(dc, x, y, w, h, s, f, c, Gdiplus::StringAlignmentCenter)) return;
    RECT r = {x, y, x + w, y + h};
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ResolveFontColor(c));
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
    if (!DrawMaterialText(dc, rt.left, rt.top, rt.right - rt.left, rt.bottom - rt.top,
                          buf, fShift, shiftC, Gdiplus::StringAlignmentCenter)) {
        SelectObject(dc, fShift);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, ResolveFontColor(shiftC));
        DrawTextW(dc, buf, -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }

    // 主字符（键下半部）
    buf[0] = baseCh;
    RECT rb = {x, y + h / 2, x + w, y + h};
    if (!DrawMaterialText(dc, rb.left, rb.top, rb.right - rb.left, rb.bottom - rb.top,
                          buf, fBase, baseC, Gdiplus::StringAlignmentCenter)) {
        SelectObject(dc, fBase);
        SetTextColor(dc, ResolveFontColor(baseC));
        DrawTextW(dc, buf, -1, &rb, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    }
}

// 判断 BGR 颜色是否为浅色，用于在高亮按钮上自动选择深/浅色文字以保证可读性
static BOOL IsLightColor(DWORD bgr) {
    int r = bgr & 0xFF;
    int g = (bgr >> 8) & 0xFF;
    int b = (bgr >> 16) & 0xFF;
    return (r * 299 + g * 587 + b * 114) / 1000 >= 150;
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
        case 0x08: return L"\x2190";
        case 0x09: return L"Tab";
        case 0x0D: return L"Enter";
        case 0x14: return L"Caps";
        case 0x10: case 0xA0: case 0xA1: return L"Shift";
        case 0x11: return L"Ctrl";
        case 0x12: return L"Alt";
        case 0x5B: return L"Win";
        case 0x5D: return L"Menu";
        case 0x20: return T(L"\x7A7A\x683C", L"Space");
        case 0x25: return L"\x2190";
        case 0x26: return L"\x2191";
        case 0x27: return L"\x2192";
        case 0x28: return L"\x2193";
        case 0xC0: return L"\x60";
    }

    if (k->type == K_HIDE) return T(L"\x6536\x8D77", L"Hide");
    if (k->type == K_SPACE) return T(L"\x7A7A\x683C", L"Space");
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
    case K_HIDE: ShowKB(FALSE); break;
    default: break;
    }
}
// ========== Header Layout & Dynamic DPI Positioning ==========
#define HDR_DOCK  1000
#define HDR_MIN   1003
#define HDR_CLOSE 1004

static int HitHeader(int x, int y) {
    if (y < 0 || y >= g_headerH) return -1;

    double dpiScale = GetSystemDpiScale();
    int rMargin = (int)(6 * dpiScale);
    int gap     = (int)(6 * dpiScale);
    int wClose = (int)(28 * dpiScale);
    int wMin   = (int)(28 * dpiScale);
    int wMenu  = (int)(48 * dpiScale);
    int btnH   = (int)(28 * dpiScale);
    if (btnH > g_headerH - 4) btnH = g_headerH - 4;
    int btnY = (g_headerH - btnH) / 2;

    if (y < btnY || y >= btnY + btnH) return -1;

    int xClose = g_ww - rMargin - wClose;
    int xMin   = xClose - gap - wMin;
    int xMenu  = (int)(6 * dpiScale);

    if (x >= xClose && x < xClose + wClose) return HDR_CLOSE;
    if (x >= xMin && x < xMin + wMin) return HDR_MIN;
    if (x >= xMenu && x < xMenu + wMenu) return HDR_DOCK;
    return -1;
}

static BOOL IsMainMaterialPaintActive() {
    return IsMaterialApplied(g_hWnd) && g_alphaPaintActive;
}

// 材质模式的半透明色调层：让窗口内容与 DWM Mica/亚克力背景融合为一体。
// 所有窗口（主键盘/设置/关闭提示）统一使用同一色调，观感一致；
// 主界面的 Mica 色调更轻，让壁纸色调透出更明显（亚克力本身效果强，无需减弱）。
static void DrawWindowMaterialTint(HDC dc, HWND hWnd, int w, int h) {
    if (!IsMaterialApplied(hWnd) || !g_alphaPaintActive) return;
    BOOL dark = g_theme->bg == g_darkTheme.bg;
    BOOL isMain = (hWnd == g_hWnd);
    BYTE alpha;
    if (g_materialMode == 1) {
        // Mica：主界面仅保留极轻色调，让壁纸强调色透出，与桌面连成一体
        alpha = isMain ? (dark ? 56 : 72) : (dark ? 104 : 132);
    } else {
        alpha = dark ? 76 : 104;
    }
    DrawAlphaSurface(dc, 0, 0, w, h, C_BG, alpha);
}

static void DrawHeader(HDC dc) {
    // 标题栏与主界面一体化：不再单独铺 C_HDR 底色，
    // 背景统一由 ClearWindowBackBuffer（纯色）或 DWM 材质（Mica/亚克力）呈现。
    double dpiScale = GetSystemDpiScale();
    int rMargin = (int)(6 * dpiScale);
    int gap     = (int)(6 * dpiScale);
    int btnH    = (int)(28 * dpiScale);
    if (btnH > g_headerH - 4) btnH = g_headerH - 4;
    int btnY = (g_headerH - btnH) / 2;

    int wClose = (int)(28 * dpiScale);
    int wMin   = (int)(28 * dpiScale);
    int wMenu  = (int)(48 * dpiScale);

    int xClose = g_ww - rMargin - wClose;
    int xMin   = xClose - gap - wMin;
    int xMenu  = (int)(6 * dpiScale);

    if (IsMainMaterialPaintActive())
        DrawRoundRectAlpha(dc, xMenu, btnY, wMenu, btnH, C_KEY, C_KEY_BORDER,
                           btnH / 2, 188, 150);
    else
        DrawRoundRect(dc, xMenu, btnY, wMenu, btnH, C_KEY, C_KEY_BORDER, btnH / 2);
    DrawTextC(dc, xMenu, btnY, wMenu, btnH, T(L"\x8BBE\x7F6E", L"Settings"), g_f12, C_WHITE);   // 菜单按钮 → 打开设置页

    int xTitle = xMenu + wMenu + gap;
    int wTitle = xMin - xTitle - gap;
    if (wTitle > 40) {
        DrawTextC(dc, xTitle, 0, wTitle, g_headerH, L"", g_f12, C_DIM);
    }

    // 最小化按钮：悬停时与设置页关闭按钮同款圆角底（C_HOVER + C_KEY_BORDER），图标为 AA 横线
    if (g_hdrHov == HDR_MIN) {
        if (IsMainMaterialPaintActive())
            DrawRoundRectAlpha(dc, xMin, btnY, wMin, btnH, C_HOVER, C_KEY_BORDER, 6, 196, 150);
        else
            DrawRoundRect(dc, xMin, btnY, wMin, btnH, C_HOVER, C_KEY_BORDER, 6);
    }
    {
        int cx = xMin + wMin / 2;
        int cy = btnY + btnH / 2;
        int half = (int)(5 * dpiScale);
        DrawLineAA(dc, cx - half, cy, cx + half, cy, C_DIM, 2.0f);
    }
    // 关闭按钮：与设置页关闭按钮同款样式（悬停圆角底 + AA 的 X 图标）
    if (g_hdrHov == HDR_CLOSE) {
        if (IsMainMaterialPaintActive())
            DrawRoundRectAlpha(dc, xClose, btnY, wClose, btnH, C_HOVER, C_KEY_BORDER, 6, 196, 150);
        else
            DrawRoundRect(dc, xClose, btnY, wClose, btnH, C_HOVER, C_KEY_BORDER, 6);
    }
    {
        int cx = xClose + wClose / 2;
        int cy = btnY + btnH / 2;
        int r  = (int)(5 * dpiScale); if (r < 4) r = 4;
        DrawLineAA(dc, cx - r, cy - r, cx + r, cy + r, C_DIM, 2.0f);
        DrawLineAA(dc, cx + r, cy - r, cx - r, cy + r, C_DIM, 2.0f);
    }
}

static void DrawKeys(HDC dc) {
    for (int i = 0; i < g_nk; i++) {
        const KeyDef* k = &g_keys[i];
        BOOL active = IsActive(k);
        BOOL pressed = (i == g_pk);
        BOOL hover = (i == g_hk);

        DWORD bg = C_KEY;
        if (active || pressed) bg = C_HOT;
        else if (hover) bg = C_HOVER;
        else if (!(k->vk >= 0x200 && k->vk <= 0x205)) {   // 网址后缀键显示为普通键
            int dt[] = {K_SPECIAL, K_CAPS, K_MOD, K_ARROW, K_HIDE};
            for (size_t j = 0; j < sizeof(dt)/sizeof(dt[0]); j++) {
                if (k->type == dt[j]) { bg = C_DARK; break; }
            }
        }

        if (IsMainMaterialPaintActive()) {
            BYTE fillAlpha = (active || pressed) ? 232 : (hover ? 204 : 180);
            DrawRoundRectAlpha(dc, k->x, k->y, k->w, k->h, bg, C_KEY_BORDER,
                               8, fillAlpha, 145);
        } else {
            DrawRoundRect(dc, k->x, k->y, k->w, k->h, bg, C_KEY_BORDER, 8);
        }

        const wchar_t* txt = KeyText(k);
        // 字体粗细跟随键面底色：白色普通键（字母/数字/标点/空格/网址后缀）用常规字重，
        // 深色修饰与功能键（Esc/Tab/Caps/Shift/Ctrl/Alt/Win/Fn/Menu/方向键等）保留粗体
        BOOL darkKey = FALSE;
        if (!(k->vk >= 0x200 && k->vk <= 0x205)) {   // 网址后缀键按普通键处理
            int dt[] = {K_SPECIAL, K_CAPS, K_MOD, K_ARROW, K_HIDE};
            for (size_t j = 0; j < sizeof(dt)/sizeof(dt[0]); j++) {
                if (k->type == dt[j]) { darkKey = TRUE; break; }
            }
        }
        HFONT f = darkKey ? g_f14b : g_f14;
        if (k->vk == 0x08) f = g_f18b;   // 退格：深色键，大号粗体箭头
        if (k->vk == 0x0D) f = g_f13b;   // Enter：深色键
        DWORD textC = (active || pressed) && IsLightColor(bg) ? 0x1A1A1A : C_WHITE;

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
        if (baseCh && shiftCh && shiftCh != baseCh) {
            if (shiftOn) {
                wchar_t single[2] = { g_shiftSymbols ? shiftCh : baseCh, 0 };
                DrawTextC(dc, k->x, k->y, k->w, k->h, single, f, textC);
            } else {
                DrawKeyDual(dc, k->x, k->y, k->w, k->h, baseCh, shiftCh, f, g_f12, textC, C_DIM);
            }
        } else {
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

    // 未显示过才应用材质；WM_CREATE 已应用时跳过，避免启动动画首帧重复切换背景卡顿
    if (!IsWindowVisible(hWnd) && !IsMaterialApplied(hWnd)) ApplyWindowMaterial(hWnd);
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
        else PostMessageW(hWnd, WM_REAPPLY_MATERIAL, 0, 0);
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
    } else {
        PostMessageW(hWnd, WM_REAPPLY_MATERIAL, 0, 0);
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
        }
        if (g_vis) {
            StopWindowMotion(&g_mainMotion);
            SetWindowPos(g_hWnd, HWND_TOPMOST, sx, targetY, g_ww, g_wh,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
            ApplyWindowMaterial(g_hWnd);
            RedrawWindow(g_hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE | RDW_FRAME);
            return;
        }
        RECT current = {0};
        int fromY = work.bottom;
        if (g_mainMotion.active && GetWindowRect(g_hWnd, &current)) fromY = current.top;
        g_vis = TRUE;
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

static void ExitApplicationAnimated() {
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
        L"  -wallpaper   : \x9AD8\x4EAE\x6309\x94AE\x989C\x8272\x8DDF\x968F\x7CFB\x7EDF\x58C1\x7EB8\x81EA\x52A8\x63D0\x53D6\x7684\x5F3A\x8C03\x8272\xFF08\x9ED8\x8BA4\x5173\x95ED\xFF09",
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
        L"  -wallpaper   : Highlight color follows the wallpaper accent (default off)"),
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
#define S_HIT_LAYOUT_DROP    14
#define S_HIT_LAYOUT_OPT0    15
#define S_HIT_LAYOUT_OPT1    16
#define S_HIT_LAYOUT_OPT2    17
#define S_HIT_FKEYS          18
#define S_HIT_FNWEB          94
#define S_HIT_SHIFTSYM       19
#define S_HIT_THEME_DROP     20
#define S_HIT_THEME_OPT0     21
#define S_HIT_THEME_OPT1     22
#define S_HIT_THEME_OPT2     23
#define S_HIT_URL            30
#define S_HIT_FEEDBACK       31
#define S_HIT_CLOSE_DROP     70
#define S_HIT_CLOSE_OPT0     71
#define S_HIT_CLOSE_OPT1     72
#define S_HIT_LANG_DROP       50
#define S_HIT_LANG_OPT0       51
#define S_HIT_LANG_OPT1       52
#define S_HIT_HL_DROP         60
#define S_HIT_HL_OPT0         61
#define S_HIT_HL_OPT1         62
#define S_HIT_HL_OPT2         63
#define S_HIT_HL_BOX          64
#define S_HIT_HL_HUE          65
#define S_HIT_HL_SAT          66
#define S_HIT_HL_VAL          67
#define S_HIT_HL_PAL0         80
#define S_HIT_MATERIAL_DROP   90
#define S_HIT_MATERIAL_OPT0   91
#define S_HIT_MATERIAL_OPT1   92
#define S_HIT_MATERIAL_OPT2   93
#define S_HIT_REMEMBER        95
#define S_HIT_OPACITY_DROP    96
#define S_HIT_OPACITY_OPT0    97   // ~ OPT5（6 档透明度）

static int  g_sTab = 0;        // 0=常规 1=主题 2=关于
static int  g_sHov = -1;       // 悬停元素，-1=无
static BOOL g_sTracking = FALSE;
static BOOL g_settingsClosing = FALSE;
static BOOL g_settingsMoving = FALSE;
static BOOL g_dropTheme = FALSE;    // 主题下拉是否展开
static BOOL g_dropLayout = FALSE;   // 布局下拉是否展开
static int  g_dropThemeHov = -1;
static int  g_dropLayoutHov = -1;
static BOOL g_dropLang = FALSE;        // 语言下拉
static int  g_dropLangHov = -1;
static BOOL g_dropHl = FALSE;          // 高亮颜色下拉
static int  g_dropHlHov = -1;
static BOOL g_dropClose = FALSE;        // 关闭按钮操作下拉
static int  g_dropCloseHov = -1;
static BOOL g_dropMaterial = FALSE;     // 背景材质下拉
static int  g_dropMaterialHov = -1;
static BOOL g_dropOpacity = FALSE;      // 主界面透明度下拉（旧系统）
static int  g_dropOpacityHov = -1;
static BOOL g_hlEditFocus = FALSE;     // HEX 输入框是否处于编辑态
static wchar_t g_hlEditBuf[8] = {0};   // 编辑中的 HEX 文本（#RRGGBB）
static int g_hlSliderDrag = S_HIT_NONE;
static const wchar_t* g_langNames[2] = { L"简体中文", L"English" };
static const wchar_t* g_langNamesEn[2] = { L"Simplified Chinese", L"English" };
static const wchar_t* g_hlModeNames[3] = { L"默认", L"跟随壁纸", L"自定义" };
static const wchar_t* g_hlModeNamesEn[3] = { L"Default", L"Follow Wallpaper", L"Custom" };
static const wchar_t* g_themeNames[3] = { L"跟随系统", L"深色主题", L"浅色主题" };
static const wchar_t* g_themeNamesEn[3] = { L"Follow System", L"Dark Theme", L"Light Theme" };
static const wchar_t* g_materialNames[3] = { L"关闭", L"Mica", L"亚克力" };
static const wchar_t* g_materialNamesEn[3] = { L"Off", L"Mica", L"Acrylic" };
static const int g_opacityValues[6] = { 100, 90, 80, 70, 60, 50 };
static const wchar_t* g_opacityNames[6] = { L"100%（不透明）", L"90%", L"80%", L"70%", L"60%", L"50%" };
static const wchar_t* g_opacityNamesEn[6] = { L"100% (Opaque)", L"90%", L"80%", L"70%", L"60%", L"50%" };
static const wchar_t* g_layoutNames[3] = { L"全尺寸", L"小键盘", L"常用" };
static const wchar_t* g_layoutNamesEn[3] = { L"Full", L"Numpad", L"Common" };

static int g_switchAnimHit = S_HIT_NONE;
static LONGLONG g_switchAnimStart = 0;
static BOOL g_switchAnimFrom = FALSE;
static BOOL g_switchAnimTo = FALSE;

static void DrawTextL(HDC dc, int x, int y, int w, int h, const wchar_t* s, HFONT f, DWORD c) {
    if (DrawMaterialText(dc, x, y, w, h, s, f, c, Gdiplus::StringAlignmentNear)) return;
    RECT r = {x, y, x + w, y + h};
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ResolveFontColor(c));
    DrawTextW(dc, s, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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
    DrawRoundRect(dc, kx, y + 3, knob, knob, on ? 0xFFFFFF : C_DIM, on ? 0xFFFFFF : C_DIM, knob / 2);
}

static void DrawCheck(HDC dc, int x, int y, int s, BOOL on) {
    DrawRoundRect(dc, x, y, s, s, on ? C_HOT : C_KEY, C_KEY_BORDER, s / 3);
    if (on) {
        HPEN p = CreatePen(PS_SOLID, 2, 0xFFFFFF);   // 勾固定白色，深浅色一致
        HPEN op = (HPEN)SelectObject(dc, p);
        MoveToEx(dc, x + 3, y + s / 2, NULL);
        LineTo(dc, x + s / 2, y + s - 3);
        LineTo(dc, x + s - 2, y + 2);
        SelectObject(dc, op); DeleteObject(p);
    }
}

static void SettingsTab(HDC dc, int x, int y, int w, int h, const wchar_t* label, BOOL active, BOOL hover) {
    DWORD text = active || hover ? C_WHITE : C_DIM;
    DrawTextC(dc, x, y, w, h - 5, label, g_sf13b, text);
    if (active) {
        int uw = (int)(34 * GetSystemDpiScale());
        DrawRoundRect(dc, x + (w - uw) / 2, y + h - 4, uw, 4, C_HOT, C_HOT, 2);
    }
}

// 圆角方框面板（设置项容器）
static void DrawPanel(HDC dc, int x, int y, int w, int h) {
    DrawRoundRect(dc, x, y, w, h, C_KEY, C_KEY_BORDER, 6);
}
// 可指定圆角半径的面板（键盘布局 / 主题页用大圆角）
static void DrawPanelR(HDC dc, int x, int y, int w, int h, int r) {
    DrawRoundRect(dc, x, y, w, h, C_KEY, C_KEY_BORDER, r);
}

struct SettingsMetrics {
    double dpi;
    int W, H;
    int margin;
    int titleY, titleH;
    int closeX, closeY, closeW, closeH;
    int tabsY, tabH, tabW, tabGap;
    int contentX, contentY, contentW;
    int rowH, rowGap;
    int comboW, comboH;
    int switchW, switchH;
};

static SettingsMetrics GetSettingsMetrics(HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    SettingsMetrics m = {};
    m.dpi = GetSystemDpiScale();
    m.W = rc.right; m.H = rc.bottom;
    m.margin = (int)(30 * m.dpi);
    m.titleY = (int)(16 * m.dpi);
    m.titleH = (int)(30 * m.dpi);
    m.closeW = m.closeH = (int)(28 * m.dpi);
    m.closeX = m.W - m.margin - m.closeW;
    m.closeY = (int)(15 * m.dpi);
    m.tabsY = (int)(57 * m.dpi);
    m.tabH = (int)(33 * m.dpi);
    m.tabW = (int)(80 * m.dpi);
    m.tabGap = (int)(8 * m.dpi);
    m.contentX = m.margin;
    m.contentY = (int)(110 * m.dpi);
    m.contentW = m.W - m.margin * 2;
    m.rowH = (int)(52 * m.dpi);
    m.rowGap = (int)(6 * m.dpi);
    m.comboW = (int)(172 * m.dpi);
    m.comboH = (int)(30 * m.dpi);
    m.switchW = (int)(42 * m.dpi);
    m.switchH = (int)(21 * m.dpi);
    return m;
}

static RECT SettingsRowRect(const SettingsMetrics& m, int index) {
    int y = m.contentY + index * (m.rowH + m.rowGap);
    RECT r = {m.contentX, y, m.contentX + m.contentW, y + m.rowH};
    return r;
}

static int SettingsComboX(const SettingsMetrics& m, const RECT& row) {
    return row.right - (int)(20 * m.dpi) - m.comboW;
}

static int SettingsComboY(const SettingsMetrics& m, const RECT& row) {
    return row.top + (row.bottom - row.top - m.comboH) / 2;
}

static int SettingsComboListY(const SettingsMetrics& m, int comboY, int itemH, int count) {
    int listH = itemH * count + 4;
    int below = comboY + m.comboH + 4;
    if (below + listH <= m.H - (int)(12 * m.dpi)) return below;
    int above = comboY - listH - 4;
    int minY = m.tabsY + m.tabH + 4;
    return above >= minY ? above : minY;
}

static RECT SettingsHighlightRect(const SettingsMetrics& m, BOOL expanded) {
    RECT r = SettingsRowRect(m, 2);
    if (expanded) r.bottom = r.top + (int)(260 * m.dpi);
    return r;
}

static RECT SettingsHexRect(const SettingsMetrics& m, const RECT& row) {
    int w = (int)(150 * m.dpi), h = (int)(32 * m.dpi);
    RECT r = {row.left + (int)(74 * m.dpi), row.top + (int)(216 * m.dpi),
              row.left + (int)(74 * m.dpi) + w, row.top + (int)(216 * m.dpi) + h};
    return r;
}

static void SettingsPaletteMetrics(const SettingsMetrics& m, const RECT& row,
                                   int* x, int* y, int* size, int* gap) {
    *x = row.left + (int)(30 * m.dpi);
    *y = row.top + (int)(70 * m.dpi);
    *size = (int)(26 * m.dpi);
    *gap = (int)(14 * m.dpi);
}

static RECT SettingsColorSliderRect(const SettingsMetrics& m, const RECT& row, int index) {
    int x = row.left + (int)(30 * m.dpi);
    int y = row.top + (int)((112 + index * 36) * m.dpi);
    RECT r = {x, y, row.right - (int)(30 * m.dpi), y + (int)(14 * m.dpi)};
    return r;
}

static void DrawSettingsIcon(HDC dc, int x, int y, int kind) {
    double dpi = GetSystemDpiScale();
    int size = (int)(24 * dpi);
    if (kind == 3) {
        int pad = (int)(3 * dpi);
        DrawRoundRect(dc, x + pad, y + pad, size - pad * 2, size - pad * 2,
                      C_KEY, C_WHITE, (int)(3 * dpi));
        DrawTextC(dc, x, y, size, size, L"F", g_sf13b, C_WHITE);
        return;
    }
    if (kind == 9) {
        int inset = (int)(3 * dpi);
        int pane = size - (int)(8 * dpi);
        DrawRoundRect(dc, x + inset, y + (int)(6 * dpi), pane, pane,
                      C_HOVER, C_DIM, (int)(3 * dpi));
        DrawRoundRect(dc, x + (int)(7 * dpi), y + inset, pane, pane,
                      C_KEY, C_WHITE, (int)(3 * dpi));
        return;
    }

    static const wchar_t* glyphs[9] = {
        L"\xE7C9", // TouchPointer
        L"\xE8BB", // ChromeClose
        L"\xE765", // KeyboardClassic
        L"",
        L"\xE752", // UpArrowShiftKey
        L"\xE823", // Clock
        L"\xE774", // Globe
        L"\xE771", // Personalize
        L"\xE790"  // Color
    };
    RECT r = {x, y, x + size, y + size};
    if (DrawMaterialText(dc, x, y, size, size, glyphs[kind], g_sfIcon,
                         C_WHITE, Gdiplus::StringAlignmentCenter)) return;
    SelectObject(dc, g_sfIcon);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, ResolveFontColor(C_WHITE));
    DrawTextW(dc, glyphs[kind], -1, &r, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

// 行内容（不含面板背景）：供独立行与分组面板内的子行复用
// icon < 0 表示无图标行（文本/描述位置与有图标的行保持一致）
static void DrawSettingRowContent(HDC dc, const RECT& row, int icon, const wchar_t* title,
                                  const wchar_t* desc, BOOL hover) {
    double dpi = GetSystemDpiScale();
    if (hover) {
        DrawRoundRectAlpha(dc, row.left + 1, row.top + 1,
                           row.right - row.left - 2, row.bottom - row.top - 2,
                           C_HOVER, C_HOVER, 8, 255, 255);
    }
    int iconSize = (int)(24 * dpi);
    if (icon >= 0)
        DrawSettingsIcon(dc, row.left + (int)(14 * dpi), row.top + (row.bottom - row.top - iconSize) / 2, icon);
    int tx = row.left + (int)(52 * dpi);
    int tw = row.right - tx - (int)(220 * dpi);
    DrawTextL(dc, tx, row.top + (int)(5 * dpi), tw, (int)(20 * dpi), title, g_sf14b, C_WHITE);
    DrawTextL(dc, tx, row.top + (int)(27 * dpi), tw, (int)(18 * dpi), desc, g_sf12, C_DIM);
}

static void DrawSettingRow(HDC dc, const RECT& row, int icon, const wchar_t* title,
                           const wchar_t* desc, BOOL hover) {
    DrawRoundRect(dc, row.left, row.top, row.right - row.left, row.bottom - row.top,
                  hover ? C_HOVER : C_KEY, hover ? C_HOT : C_KEY_BORDER, 8);
    DrawSettingRowContent(dc, row, icon, title, desc, FALSE);
}

static RECT SettingsSwitchRect(const SettingsMetrics& m, int hit) {
    // 常规 Tab 行序：0=自动呼出 1=关闭按钮 2=记住我的选择 3=键盘布局 4=功能键行 5=Fn网页布局 6=Shift符号 7=界面语言
    int rowIndex;
    if (hit == S_HIT_AUTO) rowIndex = 0;
    else if (hit == S_HIT_REMEMBER) rowIndex = 2;
    else if (hit == S_HIT_FKEYS) rowIndex = 4;
    else if (hit == S_HIT_FNWEB) rowIndex = 5;
    else rowIndex = 6;
    RECT row = SettingsRowRect(m, rowIndex);
    row.left = row.right - (int)(105 * m.dpi);
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

static DWORD BlendColor(DWORD from, DWORD to, double value) {
    int r = (int)(GetRValue(from) + (GetRValue(to) - GetRValue(from)) * value + 0.5);
    int g = (int)(GetGValue(from) + (GetGValue(to) - GetGValue(from)) * value + 0.5);
    int b = (int)(GetBValue(from) + (GetBValue(to) - GetBValue(from)) * value + 0.5);
    return RGB(r, g, b);
}

static void DrawSettingSwitch(HDC dc, const SettingsMetrics& m, const RECT& row, BOOL on, int hit) {
    int x = row.right - (int)(20 * m.dpi) - m.switchW;
    int y = row.top + (row.bottom - row.top - m.switchH) / 2;
    DrawTextC(dc, x - (int)(42 * m.dpi), row.top, (int)(34 * m.dpi), row.bottom - row.top,
              on ? T(L"开", L"On") : T(L"关", L"Off"), g_sf13, C_WHITE);
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
    int knob = m.switchH - 6;
    int travel = m.switchW - knob - 6;
    int kx = x + 3 + (int)(travel * value + 0.5);
    DWORD knobColor = BlendColor(C_DIM, 0xFFFFFF, value);
    DrawRoundRect(dc, kx, y + 3, knob, knob, knobColor, knobColor, knob / 2);
}

// 高亮色板（RGB）：与自定义编辑器顶部的常用主题色对应
static const int g_paletteRgb[5] = {
    0x3B82F6, 0x22C55E, 0x8B5CF6, 0xF43F5E, 0xB4A5F4
};
static DWORD RgbToBgr(int rgb) {
    return (DWORD)(((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF));
}

static DWORD HsvToBgr(double hue, double sat, double val) {
    while (hue < 0.0) hue += 360.0;
    while (hue >= 360.0) hue -= 360.0;
    if (sat < 0.0) sat = 0.0; if (sat > 1.0) sat = 1.0;
    if (val < 0.0) val = 0.0; if (val > 1.0) val = 1.0;

    double hh = hue / 60.0;
    int sector = (int)hh;
    double f = hh - sector;
    double p = val * (1.0 - sat);
    double q = val * (1.0 - sat * f);
    double t = val * (1.0 - sat * (1.0 - f));
    double r = val, g = t, b = p;
    switch (sector % 6) {
    case 0: r = val; g = t; b = p; break;
    case 1: r = q; g = val; b = p; break;
    case 2: r = p; g = val; b = t; break;
    case 3: r = p; g = q; b = val; break;
    case 4: r = t; g = p; b = val; break;
    case 5: r = val; g = p; b = q; break;
    }
    return RGB((int)(r * 255.0 + 0.5), (int)(g * 255.0 + 0.5), (int)(b * 255.0 + 0.5));
}

static void BgrToHsv(DWORD bgr, double* hue, double* sat, double* val) {
    double r = GetRValue(bgr) / 255.0;
    double g = GetGValue(bgr) / 255.0;
    double b = GetBValue(bgr) / 255.0;
    double maxv = r > g ? (r > b ? r : b) : (g > b ? g : b);
    double minv = r < g ? (r < b ? r : b) : (g < b ? g : b);
    double delta = maxv - minv;
    double h = 0.0;
    if (delta > 0.0001) {
        if (maxv == r) h = 60.0 * ((g - b) / delta);
        else if (maxv == g) h = 60.0 * (2.0 + (b - r) / delta);
        else h = 60.0 * (4.0 + (r - g) / delta);
        if (h < 0.0) h += 360.0;
    }
    *hue = h;
    *sat = maxv <= 0.0001 ? 0.0 : delta / maxv;
    *val = maxv;
}

static Gdiplus::Color GpColorFromBgr(DWORD color) {
    return Gdiplus::Color(255, GetRValue(color), GetGValue(color), GetBValue(color));
}

static void DrawColorSlider(HDC dc, const RECT& r, int kind, double hue, double sat, double val) {
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

        if (kind == 0) {
            for (int i = 0; i < 6; i++) {
                Gdiplus::REAL left = x + width * (Gdiplus::REAL)i / 6.0f;
                Gdiplus::REAL right = x + width * (Gdiplus::REAL)(i + 1) / 6.0f + 1.0f;
                Gdiplus::LinearGradientBrush brush(
                    Gdiplus::PointF(left, y), Gdiplus::PointF(right, y),
                    GpColorFromBgr(HsvToBgr(i * 60.0, 1.0, 1.0)),
                    GpColorFromBgr(HsvToBgr((i + 1) * 60.0, 1.0, 1.0)));
                g.FillRectangle(&brush, left, y, right - left, height);
            }
        } else {
            DWORD from = kind == 1 ? HsvToBgr(hue, 0.0, val) : HsvToBgr(hue, sat, 0.0);
            DWORD to = kind == 1 ? HsvToBgr(hue, 1.0, val) : HsvToBgr(hue, sat, 1.0);
            Gdiplus::LinearGradientBrush brush(
                Gdiplus::PointF(x, y), Gdiplus::PointF(x + width, y),
                GpColorFromBgr(from), GpColorFromBgr(to));
            g.FillRectangle(&brush, x, y, width, height);
        }
        g.ResetClip();
        Gdiplus::Pen border(GpColorFromBgr(C_KEY_BORDER), 1.0f);
        g.DrawPath(&border, &path);
    }

    double selected = kind == 0 ? hue / 359.0 : (kind == 1 ? sat : val);
    if (selected < 0.0) selected = 0.0; if (selected > 1.0) selected = 1.0;
    int cx = r.left + (int)((w - 1) * selected + 0.5);
    int cy = (r.top + r.bottom) / 2;
    int radius = h / 2 + 3;
    DrawCircleAA(dc, cx, cy, radius, C_WHITE);
    DrawCircleAA(dc, cx, cy, radius - 2, kind == 0 ? HsvToBgr(hue, 1.0, 1.0) :
                 (kind == 1 ? HsvToBgr(hue, sat, 1.0) : HsvToBgr(hue, sat, val)));
}

// 下拉框
static void DrawCombo(HDC dc, int x, int y, int w, int h, const wchar_t* text, BOOL open, BOOL hover) {
    DrawRoundRect(dc, x, y, w, h, (open || hover) ? C_HOVER : C_DARK, C_DIM, 6);
    DrawTextL(dc, x + 10, y, w - 30, h, text, g_sf13, C_WHITE);
    int ax = x + w - 14, ay = y + h / 2;
    DrawTriangleAA(dc, ax - 5, ay - 3, ax + 5, ay - 3, ax, ay + 3, C_DIM);
}

// 下拉列表（参考下拉菜单样式：悬停圆角高亮 + 选中项左侧强调条）
static void DrawComboList(HDC dc, int x, int y, int w, int itemH, const wchar_t** items, int count, int sel, int hov) {
    double dpi = GetSystemDpiScale();
    DrawRoundRect(dc, x, y, w, itemH * count + 4, C_DARK, C_DIM, 6);
    for (int i = 0; i < count; i++) {
        int iy = y + 2 + i * itemH;
        if (i == hov || i == sel) {
            // 圆角悬停高亮
            DrawRoundRectAlpha(dc, x + 3, iy + 1, w - 6, itemH - 2,
                               C_HOVER, C_HOVER, (int)(5 * dpi), 255, 255);
        }
        if (i == sel) {
            // 选中项左侧强调条
            int barH = (int)(14 * dpi);
            int barW = (int)(3 * dpi);
            int cy = iy + itemH / 2;
            DrawRoundRectAlpha(dc, x + (int)(8 * dpi), cy - barH / 2, barW, barH,
                               C_HOT, C_HOT, barW / 2, 255, 255);
        }
        DrawTextL(dc, x + (int)(17 * dpi), iy, w - (int)(24 * dpi), itemH, items[i], g_sf13, C_WHITE);
    }
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
// 材质选项随系统版本变化：Win11 = 关闭/Mica/亚克力，Win10 1809+ = 关闭/亚克力
static int MaterialOptionCount() {
    return g_isWin11 ? 3 : 2;
}
static int MaterialModeOf(int idx) {
    if (idx == 1 && !g_isWin11) return 2;   // 非 Win11 无 Mica，第二项为亚克力
    return idx;
}
static int MaterialIndexOf(int mode) {
    if (!g_isWin11 && mode == 2) return 1;
    return mode;
}
static const wchar_t* MaterialNameOf(int idx) {
    if (!g_isWin11 && idx == 1) return g_lang ? g_materialNamesEn[2] : g_materialNames[2];
    return g_lang ? g_materialNamesEn[idx] : g_materialNames[idx];
}
static int OpacityIndex() {
    for (int i = 0; i < 6; i++) if (g_opacityValues[i] == g_mainOpacity) return i;
    return 0;
}
// 高亮颜色下拉当前选项：0=默认 1=跟随壁纸 2=自定义
static int HlSel() {
    if (g_wallpaperAccent) return 1;
    return g_hlMode == 1 ? 2 : 0;
}
// 关闭按钮操作下拉当前文案
static const wchar_t* CloseActionName() {
    return T(g_closeToTray ? L"隐藏到系统托盘" : L"直接退出程序",
             g_closeToTray ? L"Hide to tray" : L"Exit program");
}
// 关于页底部两个链接的居中布局：
// “项目地址 | 问题反馈”同一行居中，分隔符 | 不参与超链接
static void AboutLinkLayout(int x0, int cw, int* px1, int* pw1, int* psep, int* psepW, int* px2, int* pw2) {
    HDC dc = GetDC(0);
    HFONT of = (HFONT)SelectObject(dc, g_sf12);
    SIZE s1, s2, ss;
    const wchar_t* t1 = T(L"项目地址", L"Project URL");
    const wchar_t* t2 = T(L"问题反馈", L"Feedback");
    GetTextExtentPoint32W(dc, t1, (int)wcslen(t1), &s1);
    GetTextExtentPoint32W(dc, t2, (int)wcslen(t2), &s2);
    GetTextExtentPoint32W(dc, L"|", 1, &ss);
    SelectObject(dc, of);
    ReleaseDC(0, dc);
    int gap = (int)(14 * GetSystemDpiScale());
    int total = s1.cx + gap + ss.cx + gap + s2.cx;
    int x = x0 + (cw - total) / 2;
    *px1 = x; *pw1 = s1.cx;
    *psep = x + s1.cx + gap; *psepW = ss.cx;
    *px2 = x + s1.cx + gap + ss.cx + gap; *pw2 = s2.cx;
}

static void SettingsDraw(HDC dc, HWND hWnd) {
    SettingsMetrics m = GetSettingsMetrics(hWnd);

    DrawTextL(dc, m.margin, m.titleY, m.W - m.margin * 2 - m.closeW - 12,
              m.titleH, T(L"设置", L"Settings"), g_sf20b, C_WHITE);
    if (g_sHov == S_HIT_CLOSE) {
        DrawRoundRect(dc, m.closeX, m.closeY, m.closeW, m.closeH, C_HOVER, C_KEY_BORDER, 6);
    }
    {
        int cx = m.closeX + m.closeW / 2, cy = m.closeY + m.closeH / 2;
        int r = (int)(5 * m.dpi);
        DrawLineAA(dc, cx - r, cy - r, cx + r, cy + r, C_DIM, 2.0f);
        DrawLineAA(dc, cx + r, cy - r, cx - r, cy + r, C_DIM, 2.0f);
    }

    int tabX = m.margin;
    SettingsTab(dc, tabX, m.tabsY, m.tabW, m.tabH, T(L"常规", L"General"), g_sTab == 0, g_sHov == S_HIT_TAB0);
    tabX += m.tabW + m.tabGap;
    SettingsTab(dc, tabX, m.tabsY, m.tabW, m.tabH, T(L"主题", L"Theme"), g_sTab == 1, g_sHov == S_HIT_TAB1);
    tabX += m.tabW + m.tabGap;
    SettingsTab(dc, tabX, m.tabsY, m.tabW, m.tabH, T(L"关于", L"About"), g_sTab == 2, g_sHov == S_HIT_TAB2);
    Fill(dc, m.margin, m.tabsY + m.tabH + 8, m.contentW, 1, C_KEY_BORDER);

    if (g_sTab == 0) {
        RECT r = SettingsRowRect(m, 0);
        DrawSettingRow(dc, r, 0, T(L"自动呼出", L"Auto Pop-up"),
                       T(L"点击输入框时自动弹出键盘", L"Show the keyboard when an input gets focus"), g_sHov == S_HIT_AUTO);
        DrawSettingSwitch(dc, m, r, g_af, S_HIT_AUTO);

        // 分组面板：关闭按钮 + 记住我的选择（子项无图标，参考分组设置样式）
        RECT r1 = SettingsRowRect(m, 1);
        RECT r2 = SettingsRowRect(m, 2);
        DrawRoundRect(dc, r1.left, r1.top, r1.right - r1.left, r2.bottom - r1.top,
                      C_KEY, C_KEY_BORDER, 8);
        // 分隔线（横跨面板）
        Fill(dc, r1.left + 1, r2.top - m.rowGap / 2 - 1, r1.right - r1.left - 2, 1, C_KEY_BORDER);
        DrawSettingRowContent(dc, r1, 1, T(L"关闭按钮", L"Close Button"),
                              T(L"选择关闭窗口时执行的操作", L"Choose what happens when the window is closed"),
                              g_sHov == S_HIT_CLOSE_DROP);
        DrawSettingRowContent(dc, r2, -1, T(L"记住我的选择", L"Remember My Choice"),
                              T(L"记住关闭按钮的操作，下次直接执行", L"Remember the close action and skip asking next time"),
                              g_sHov == S_HIT_REMEMBER);
        DrawCombo(dc, SettingsComboX(m, r1), SettingsComboY(m, r1), m.comboW, m.comboH,
                  CloseActionName(), g_dropClose, g_sHov == S_HIT_CLOSE_DROP);
        DrawSettingSwitch(dc, m, r2, g_rememberClose, S_HIT_REMEMBER);

        r = SettingsRowRect(m, 3);
        DrawSettingRow(dc, r, 2, T(L"键盘布局", L"Keyboard Layout"),
                       T(L"选择主键盘的按键排列", L"Choose the main keyboard arrangement"), FALSE);
        DrawCombo(dc, SettingsComboX(m, r), SettingsComboY(m, r), m.comboW, m.comboH,
                  g_lang ? g_layoutNamesEn[g_layoutMode] : g_layoutNames[g_layoutMode],
                  g_dropLayout, g_sHov == S_HIT_LAYOUT_DROP);

        r = SettingsRowRect(m, 4);
        DrawSettingRow(dc, r, 3, T(L"功能键行", L"Function Key Row"),
                       T(L"在键盘顶部显示 F1~F12 和 Del", L"Show F1~F12 and Del above the keyboard"), g_sHov == S_HIT_FKEYS);
        DrawSettingSwitch(dc, m, r, g_showFKeys, S_HIT_FKEYS);

        r = SettingsRowRect(m, 5);
        DrawSettingRow(dc, r, 2, T(L"Fn 网页布局", L"Fn Web Layout"),
                       T(L"按 Fn 切换到上网常用布局", L"Press Fn to switch to the web-friendly layout"), g_sHov == S_HIT_FNWEB);
        DrawSettingSwitch(dc, m, r, g_fnWebLayout, S_HIT_FNWEB);

        r = SettingsRowRect(m, 6);
        DrawSettingRow(dc, r, 4, T(L"Shift 符号", L"Shift Symbols"),
                       T(L"按下 Shift 后数字键仅显示特殊符号", L"Show only symbols on number keys while Shift is active"), g_sHov == S_HIT_SHIFTSYM);
        DrawSettingSwitch(dc, m, r, g_shiftSymbols, S_HIT_SHIFTSYM);

        r = SettingsRowRect(m, 7);
        DrawSettingRow(dc, r, 6, T(L"界面语言", L"Language"),
                       T(L"切换设置与键盘的显示语言", L"Change the language used by settings and keyboard"), FALSE);
        DrawCombo(dc, SettingsComboX(m, r), SettingsComboY(m, r), m.comboW, m.comboH,
                  g_lang ? g_langNamesEn[g_lang] : g_langNames[g_lang],
                  g_dropLang, g_sHov == S_HIT_LANG_DROP);
    } else if (g_sTab == 1) {
        RECT r = SettingsRowRect(m, 0);
        DrawSettingRow(dc, r, 7, T(L"主题模式", L"Theme Mode"),
                       T(L"跟随系统，或固定使用深色、浅色主题", L"Follow Windows or use a fixed dark or light theme"), FALSE);
        DrawCombo(dc, SettingsComboX(m, r), SettingsComboY(m, r), m.comboW, m.comboH,
                  g_lang ? g_themeNamesEn[g_themeMode] : g_themeNames[g_themeMode],
                  g_dropTheme, g_sHov == S_HIT_THEME_DROP);

        r = SettingsRowRect(m, 1);
        if (g_supportsMaterial) {
            DrawSettingRow(dc, r, 9, T(L"背景材质", L"Background Material"),
                           T(L"选择 Mica、亚克力或纯色背景", L"Choose Mica, Acrylic, or a solid background"), FALSE);
            DrawCombo(dc, SettingsComboX(m, r), SettingsComboY(m, r), m.comboW, m.comboH,
                      MaterialNameOf(MaterialIndexOf(g_materialMode)),
                      g_dropMaterial, g_sHov == S_HIT_MATERIAL_DROP);
        } else {
            // PE / XP~Win10 1803：无系统 backdrop，改为主界面透明度调整
            DrawSettingRow(dc, r, 9, T(L"主界面透明度", L"Keyboard Opacity"),
                           T(L"调整主界面的不透明度", L"Adjust the keyboard window opacity"), FALSE);
            int oi = OpacityIndex();
            DrawCombo(dc, SettingsComboX(m, r), SettingsComboY(m, r), m.comboW, m.comboH,
                      g_lang ? g_opacityNamesEn[oi] : g_opacityNames[oi],
                      g_dropOpacity, g_sHov == S_HIT_OPACITY_DROP);
        }

        BOOL customColor = HlSel() == 2;
        r = SettingsHighlightRect(m, customColor);
        DrawRoundRect(dc, r.left, r.top, r.right - r.left, r.bottom - r.top, C_KEY, C_KEY_BORDER, 8);
        DrawSettingsIcon(dc, r.left + (int)(14 * m.dpi), r.top + (int)(14 * m.dpi), 8);
        int tx = r.left + (int)(52 * m.dpi);
        int tw = r.right - tx - (int)(220 * m.dpi);
        DrawTextL(dc, tx, r.top + (int)(5 * m.dpi), tw, (int)(20 * m.dpi),
                  T(L"高亮颜色", L"Highlight Color"), g_sf14b, C_WHITE);
        DrawTextL(dc, tx, r.top + (int)(27 * m.dpi), tw, (int)(18 * m.dpi),
                  T(L"使用默认颜色、壁纸强调色或自定义颜色", L"Use the default, wallpaper accent, or a custom color"), g_sf12, C_DIM);
        DrawCombo(dc, SettingsComboX(m, r), SettingsComboY(m, SettingsRowRect(m, 2)),
                  m.comboW, m.comboH, g_lang ? g_hlModeNamesEn[HlSel()] : g_hlModeNames[HlSel()],
                  g_dropHl, g_sHov == S_HIT_HL_DROP);

        if (customColor) {
            Fill(dc, r.left + (int)(18 * m.dpi), r.top + (int)(56 * m.dpi),
                 r.right - r.left - (int)(36 * m.dpi), 1, C_KEY_BORDER);

            int palX, palY, palS, palGap;
            SettingsPaletteMetrics(m, r, &palX, &palY, &palS, &palGap);
            for (int i = 0; i < 5; i++) {
                DWORD color = RgbToBgr(g_paletteRgb[i]);
                int cx = palX + i * (palS + palGap) + palS / 2;
                int cy = palY + palS / 2;
                BOOL selected = color == (DWORD)g_hlColor;
                if (selected || g_sHov == S_HIT_HL_PAL0 + i)
                    DrawCircleAA(dc, cx, cy, palS / 2 + (int)(3 * m.dpi), selected ? C_WHITE : C_HOT);
                DrawCircleAA(dc, cx, cy, palS / 2, color);
                if (selected) {
                    DWORD check = IsLightColor(color) ? 0x202020 : 0xFFFFFF;
                    HPEN pen = CreatePen(PS_SOLID, 2, check);
                    HPEN old = (HPEN)SelectObject(dc, pen);
                    MoveToEx(dc, cx - (int)(5 * m.dpi), cy, NULL);
                    LineTo(dc, cx - (int)(1 * m.dpi), cy + (int)(4 * m.dpi));
                    LineTo(dc, cx + (int)(6 * m.dpi), cy - (int)(5 * m.dpi));
                    SelectObject(dc, old); DeleteObject(pen);
                }
            }

            double hue, sat, val;
            BgrToHsv((DWORD)g_hlColor, &hue, &sat, &val);
            for (int i = 0; i < 3; i++) {
                RECT slider = SettingsColorSliderRect(m, r, i);
                DrawColorSlider(dc, slider, i, hue, sat, val);
            }

            RECT input = SettingsHexRect(m, r);
            int inputW = input.right - input.left, inputH = input.bottom - input.top;
            int colorCx = r.left + (int)(42 * m.dpi);
            int colorCy = input.top + inputH / 2;
            DrawCircleAA(dc, colorCx, colorCy, (int)(11 * m.dpi), C_KEY_BORDER);
            DrawCircleAA(dc, colorCx, colorCy, (int)(9 * m.dpi), (DWORD)g_hlColor);
            DrawTextC(dc, r.left + (int)(55 * m.dpi), input.top, (int)(18 * m.dpi), inputH,
                      L"#", g_sf13b, C_DIM);
            DrawRoundRect(dc, input.left, input.top, inputW, inputH,
                          g_hlEditFocus ? C_HOVER : C_DARK, g_hlEditFocus ? C_HOT : C_KEY_BORDER, 6);
            wchar_t hexbuf[8];
            if (g_hlEditFocus) wcscpy(hexbuf, g_hlEditBuf); else HexFromBgr(g_hlColor, hexbuf);
            const wchar_t* shown = hexbuf[0] == L'#' ? hexbuf + 1 : hexbuf;
            BOOL showHint = g_hlEditFocus && shown[0] == 0;
            DrawTextL(dc, input.left + (int)(12 * m.dpi), input.top, inputW - (int)(20 * m.dpi), inputH,
                      showHint ? L"RRGGBB" : shown, g_sf13, showHint ? C_DIM : C_WHITE);
        }
    } else {
        int x0 = m.contentX, y = m.contentY + (int)(26 * m.dpi), cw = m.contentW;
        int logo = (int)(82 * m.dpi);
        HICON hIcon = LoadMainIcon(logo);
        if (hIcon) {
            DrawIconEx(dc, x0 + (cw - logo) / 2, y, hIcon, logo, logo, 0, NULL, DI_NORMAL);
            DestroyIcon(hIcon);
        }
        y += logo + (int)(24 * m.dpi);
        DrawTextC(dc, x0, y, cw, (int)(30 * m.dpi), T(L"HKeyboard 轻键", L"HKeyboard"), g_sf20b, C_WHITE);
        y += (int)(38 * m.dpi);
        wchar_t ver[64];
        swprintf(ver, 64, T(L"版本：v%hs (%ls)", L"Version: v%hs (%ls)"), VER_FILEVERSION_STR, ArchName());
        DrawTextC(dc, x0, y, cw, (int)(20 * m.dpi), ver, g_sf12, C_DIM);

        int uy = m.H - m.margin - (int)(20 * m.dpi);
        DrawTextC(dc, x0, uy - (int)(28 * m.dpi), cw, (int)(18 * m.dpi),
                  L"Copyright 2019-2026 PanDaTech. All Rights Reserved.", g_sf12, C_DIM);
        int x1, w1, x2, w2, sx, sw2;
        AboutLinkLayout(x0, cw, &x1, &w1, &sx, &sw2, &x2, &w2);
        DrawTextL(dc, x1, uy, w1, (int)(18 * m.dpi), T(L"项目地址", L"Project URL"), g_sf12, C_HOT);
        DrawTextL(dc, sx, uy, sw2, (int)(18 * m.dpi), L"|", g_sf12, C_DIM);
        DrawTextL(dc, x2, uy, w2, (int)(18 * m.dpi), T(L"问题反馈", L"Feedback"), g_sf12, C_HOT);
        if (g_sHov == S_HIT_URL || g_sHov == S_HIT_FEEDBACK) {
            int lx = g_sHov == S_HIT_URL ? x1 : x2;
            int lw = g_sHov == S_HIT_URL ? w1 : w2;
            HPEN pen = CreatePen(PS_SOLID, 1, C_HOT);
            HPEN old = (HPEN)SelectObject(dc, pen);
            MoveToEx(dc, lx, uy + (int)(16 * m.dpi), NULL); LineTo(dc, lx + lw, uy + (int)(16 * m.dpi));
            SelectObject(dc, old); DeleteObject(pen);
        }
    }

    // 下拉列表最后绘制，确保覆盖后续卡片。
    if (g_sTab == 0) {
        RECT r;
        if (g_dropClose) {
            r = SettingsRowRect(m, 1);
            const wchar_t* names[2] = {T(L"直接退出程序", L"Exit program"), T(L"隐藏到系统托盘", L"Hide to tray")};
            int cy = SettingsComboY(m, r);
            DrawComboList(dc, SettingsComboX(m, r), SettingsComboListY(m, cy, m.comboH, 2),
                          m.comboW, m.comboH, names, 2, g_closeToTray ? 1 : 0, g_dropCloseHov);
        }
        if (g_dropLayout) {
            r = SettingsRowRect(m, 3);
            const wchar_t* names[3];
            for (int i = 0; i < 3; i++) names[i] = g_lang ? g_layoutNamesEn[i] : g_layoutNames[i];
            int cy = SettingsComboY(m, r);
            DrawComboList(dc, SettingsComboX(m, r), SettingsComboListY(m, cy, m.comboH, 3),
                          m.comboW, m.comboH, names, 3, g_layoutMode, g_dropLayoutHov);
        }
        if (g_dropLang) {
            r = SettingsRowRect(m, 7);
            const wchar_t* names[2];
            for (int i = 0; i < 2; i++) names[i] = g_lang ? g_langNamesEn[i] : g_langNames[i];
            int cy = SettingsComboY(m, r);
            DrawComboList(dc, SettingsComboX(m, r), SettingsComboListY(m, cy, m.comboH, 2),
                          m.comboW, m.comboH, names, 2, g_lang, g_dropLangHov);
        }
    } else if (g_sTab == 1) {
        RECT r;
        if (g_dropTheme) {
            r = SettingsRowRect(m, 0);
            const wchar_t* names[3];
            for (int i = 0; i < 3; i++) names[i] = g_lang ? g_themeNamesEn[i] : g_themeNames[i];
            int cy = SettingsComboY(m, r);
            DrawComboList(dc, SettingsComboX(m, r), SettingsComboListY(m, cy, m.comboH, 3),
                          m.comboW, m.comboH, names, 3, g_themeMode, g_dropThemeHov);
        }
        if (g_dropMaterial && g_supportsMaterial) {
            r = SettingsRowRect(m, 1);
            int count = MaterialOptionCount();
            const wchar_t* names[3];
            for (int i = 0; i < count; i++) names[i] = MaterialNameOf(i);
            int cy = SettingsComboY(m, r);
            DrawComboList(dc, SettingsComboX(m, r), SettingsComboListY(m, cy, m.comboH, count),
                          m.comboW, m.comboH, names, count, MaterialIndexOf(g_materialMode), g_dropMaterialHov);
        }
        if (g_dropOpacity && !g_supportsMaterial) {
            r = SettingsRowRect(m, 1);
            const wchar_t* names[6];
            for (int i = 0; i < 6; i++) names[i] = g_lang ? g_opacityNamesEn[i] : g_opacityNames[i];
            int cy = SettingsComboY(m, r);
            DrawComboList(dc, SettingsComboX(m, r), SettingsComboListY(m, cy, m.comboH, 6),
                          m.comboW, m.comboH, names, 6, OpacityIndex(), g_dropOpacityHov);
        }
        if (g_dropHl) {
            r = SettingsRowRect(m, 2);
            const wchar_t* names[3];
            for (int i = 0; i < 3; i++) names[i] = g_lang ? g_hlModeNamesEn[i] : g_hlModeNames[i];
            int cy = SettingsComboY(m, r);
            DrawComboList(dc, SettingsComboX(m, r), SettingsComboListY(m, cy, m.comboH, 3),
                          m.comboW, m.comboH, names, 3, HlSel(), g_dropHlHov);
        }
    }
}

static int SettingsHitTest(HWND hWnd, int x, int y) {
    SettingsMetrics m = GetSettingsMetrics(hWnd);
    if (x >= m.closeX && x < m.closeX + m.closeW && y >= m.closeY && y < m.closeY + m.closeH) return S_HIT_CLOSE;

    int tabX = m.margin;
    for (int i = 0; i < 3; i++) {
        if (x >= tabX && x < tabX + m.tabW && y >= m.tabsY && y < m.tabsY + m.tabH) return S_HIT_TAB0 + i;
        tabX += m.tabW + m.tabGap;
    }

    if (g_sTab == 0) {
        RECT r;
        // 下拉列表优先命中，避免列表翻到上方或覆盖相邻卡片时被底层项目抢先处理。
        if (g_dropClose) {
            r = SettingsRowRect(m, 1);
            int comboX = SettingsComboX(m, r), comboY = SettingsComboY(m, r);
            int ly = SettingsComboListY(m, comboY, m.comboH, 2) + 2;
            for (int i = 0; i < 2; i++) {
                if (x >= comboX && x < comboX + m.comboW && y >= ly && y < ly + m.comboH) return S_HIT_CLOSE_OPT0 + i;
                ly += m.comboH;
            }
        }
        if (g_dropLayout) {
            r = SettingsRowRect(m, 3);
            int comboX = SettingsComboX(m, r), comboY = SettingsComboY(m, r);
            int ly = SettingsComboListY(m, comboY, m.comboH, 3) + 2;
            for (int i = 0; i < 3; i++) {
                if (x >= comboX && x < comboX + m.comboW && y >= ly && y < ly + m.comboH) return S_HIT_LAYOUT_OPT0 + i;
                ly += m.comboH;
            }
        }
        if (g_dropLang) {
            r = SettingsRowRect(m, 7);
            int comboX = SettingsComboX(m, r), comboY = SettingsComboY(m, r);
            int ly = SettingsComboListY(m, comboY, m.comboH, 2) + 2;
            for (int i = 0; i < 2; i++) {
                if (x >= comboX && x < comboX + m.comboW && y >= ly && y < ly + m.comboH) return S_HIT_LANG_OPT0 + i;
                ly += m.comboH;
            }
        }

        r = SettingsRowRect(m, 0);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_AUTO;

        r = SettingsRowRect(m, 1);
        int comboX = SettingsComboX(m, r), comboY = SettingsComboY(m, r);
        if (g_dropClose) {
            int ly = SettingsComboListY(m, comboY, m.comboH, 2) + 2;
            for (int i = 0; i < 2; i++) {
                if (x >= comboX && x < comboX + m.comboW && y >= ly && y < ly + m.comboH) return S_HIT_CLOSE_OPT0 + i;
                ly += m.comboH;
            }
        }
        // 整行命中：悬停高亮分组主行，点击任意处展开下拉
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_CLOSE_DROP;

        r = SettingsRowRect(m, 2);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_REMEMBER;

        r = SettingsRowRect(m, 3);
        comboX = SettingsComboX(m, r); comboY = SettingsComboY(m, r);
        if (g_dropLayout) {
            int ly = SettingsComboListY(m, comboY, m.comboH, 3) + 2;
            for (int i = 0; i < 3; i++) {
                if (x >= comboX && x < comboX + m.comboW && y >= ly && y < ly + m.comboH) return S_HIT_LAYOUT_OPT0 + i;
                ly += m.comboH;
            }
        }
        if (x >= comboX && x < comboX + m.comboW && y >= comboY && y < comboY + m.comboH) return S_HIT_LAYOUT_DROP;

        r = SettingsRowRect(m, 4);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_FKEYS;
        r = SettingsRowRect(m, 5);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_FNWEB;
        r = SettingsRowRect(m, 6);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_SHIFTSYM;

        r = SettingsRowRect(m, 7);
        comboX = SettingsComboX(m, r); comboY = SettingsComboY(m, r);
        if (g_dropLang) {
            int ly = SettingsComboListY(m, comboY, m.comboH, 2) + 2;
            for (int i = 0; i < 2; i++) {
                if (x >= comboX && x < comboX + m.comboW && y >= ly && y < ly + m.comboH) return S_HIT_LANG_OPT0 + i;
                ly += m.comboH;
            }
        }
        if (x >= comboX && x < comboX + m.comboW && y >= comboY && y < comboY + m.comboH) return S_HIT_LANG_DROP;
    } else if (g_sTab == 1) {
        RECT r;
        struct DropHit { BOOL open; int row; int count; int firstHit; } drops[3] = {
            {g_dropTheme, 0, 3, S_HIT_THEME_OPT0},
            {g_dropMaterial, 1, g_supportsMaterial ? MaterialOptionCount() : 0, S_HIT_MATERIAL_OPT0},
            {g_dropHl, 2, 3, S_HIT_HL_OPT0}
        };
        for (int d = 0; d < 3; d++) {
            if (!drops[d].open) continue;
            r = SettingsRowRect(m, drops[d].row);
            int comboX = SettingsComboX(m, r), comboY = SettingsComboY(m, r);
            int ly = SettingsComboListY(m, comboY, m.comboH, drops[d].count) + 2;
            for (int i = 0; i < drops[d].count; i++) {
                if (x >= comboX && x < comboX + m.comboW && y >= ly && y < ly + m.comboH)
                    return drops[d].firstHit + i;
                ly += m.comboH;
            }
        }
        if (g_dropOpacity && !g_supportsMaterial) {
            r = SettingsRowRect(m, 1);
            int comboX = SettingsComboX(m, r), comboY = SettingsComboY(m, r);
            int ly = SettingsComboListY(m, comboY, m.comboH, 6) + 2;
            for (int i = 0; i < 6; i++) {
                if (x >= comboX && x < comboX + m.comboW && y >= ly && y < ly + m.comboH) return S_HIT_OPACITY_OPT0 + i;
                ly += m.comboH;
            }
        }

        r = SettingsRowRect(m, 0);
        int comboX = SettingsComboX(m, r), comboY = SettingsComboY(m, r);
        if (x >= comboX && x < comboX + m.comboW && y >= comboY && y < comboY + m.comboH) return S_HIT_THEME_DROP;

        r = SettingsRowRect(m, 1);
        comboX = SettingsComboX(m, r); comboY = SettingsComboY(m, r);
        if (g_supportsMaterial) {
            if (x >= comboX && x < comboX + m.comboW && y >= comboY && y < comboY + m.comboH) return S_HIT_MATERIAL_DROP;
        } else {
            if (x >= comboX && x < comboX + m.comboW && y >= comboY && y < comboY + m.comboH) return S_HIT_OPACITY_DROP;
        }

        r = SettingsRowRect(m, 2);
        comboX = SettingsComboX(m, r); comboY = SettingsComboY(m, r);
        if (x >= comboX && x < comboX + m.comboW && y >= comboY && y < comboY + m.comboH) return S_HIT_HL_DROP;

        if (HlSel() == 2) {
            r = SettingsHighlightRect(m, TRUE);
            RECT input = SettingsHexRect(m, r);
            if (x >= input.left && x < input.right && y >= input.top && y < input.bottom) return S_HIT_HL_BOX;
            int palX, palY, palS, palGap;
            SettingsPaletteMetrics(m, r, &palX, &palY, &palS, &palGap);
            for (int i = 0; i < 5; i++) {
                int px = palX + i * (palS + palGap);
                if (x >= px && x < px + palS && y >= palY && y < palY + palS) return S_HIT_HL_PAL0 + i;
            }
            for (int i = 0; i < 3; i++) {
                RECT slider = SettingsColorSliderRect(m, r, i);
                int pad = (int)(8 * m.dpi);
                if (x >= slider.left && x < slider.right && y >= slider.top - pad && y < slider.bottom + pad)
                    return S_HIT_HL_HUE + i;
            }
        }
    } else {
        int uy = m.H - m.margin - (int)(20 * m.dpi);
        int x1, w1, x2, w2, sx, sw2;
        AboutLinkLayout(m.contentX, m.contentW, &x1, &w1, &sx, &sw2, &x2, &w2);
        if (x >= x1 && x < x1 + w1 && y >= uy && y < uy + (int)(20 * m.dpi)) return S_HIT_URL;
        if (x >= x2 && x < x2 + w2 && y >= uy && y < uy + (int)(20 * m.dpi)) return S_HIT_FEEDBACK;
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
        return;
    }
    IniSetInt(L"General", L"RememberClose", 0);
    IniSetInt(L"General", L"CloseToTray", 0);
    IniSetInt(L"General", L"ConfigVersion", 3);
    IniSetInt(L"Theme", L"Mode", 0);
    IniSetInt(L"Theme", L"Wallpaper", 0);
    IniSetInt(L"Theme", L"Material", 0);
    IniSetInt(L"Theme", L"Opacity", 100);
    IniSetInt(L"Keyboard", L"Layout", 0);
    IniSetInt(L"Keyboard", L"FKeys", 0);
    IniSetInt(L"Keyboard", L"FnWebLayout", 0);
    IniSetInt(L"General", L"ShiftSymbols", 1);
    IniSetInt(L"General", L"Language", 0);
    IniSetInt(L"General", L"HighlightMode", 0);
    IniSetInt(L"General", L"HighlightColor", 0xD47800);
    IniSetInt(L"General", L"AutoPopup", 1);
}

// 读取上次的窗口大小 / 主题 / 关闭行为
static void LoadConfig() {
    g_rememberClose = (IniGetInt(L"General", L"RememberClose", 0) != 0);
    if (g_rememberClose)
        g_closeToTray = (IniGetInt(L"General", L"CloseToTray", 0) != 0);
    // 窗口大小与位置按布局记忆恢复（见 WinMain / ApplyKeyboardLayout）
    int tm = IniGetInt(L"Theme", L"Mode", -1);
    if (tm >= 0 && tm <= 2) g_themeMode = tm;
    int material = IniGetInt(L"Theme", L"Material", 0);
    if (material < 0 || material > 2) material = 0;
    if (!g_isWin11 && material == 1) material = 2;   // 非 Win11 无 Mica，回退亚克力
    g_materialMode = (g_isWinPE || !g_supportsMaterial) ? 0 : material;
    g_mainOpacity = IniGetInt(L"Theme", L"Opacity", 100);
    if (g_mainOpacity < 50 || g_mainOpacity > 100) g_mainOpacity = 100;
    g_wallpaperAccent = (IniGetInt(L"Theme", L"Wallpaper", 0) != 0);
    g_layoutMode = IniGetInt(L"Keyboard", L"Layout", 0);
    if (g_layoutMode < 0 || g_layoutMode > 2) g_layoutMode = 0;
    g_showFKeys = (IniGetInt(L"Keyboard", L"FKeys", 0) != 0);
    g_fnWebLayout = (IniGetInt(L"Keyboard", L"FnWebLayout", 0) != 0);
    g_shiftSymbols = (IniGetInt(L"General", L"ShiftSymbols", 1) != 0);
    g_hideDelayMs = 1000;   // 自动隐藏延迟固定 1 秒
    g_lang = IniGetInt(L"General", L"Language", 0);
    if (g_lang < 0 || g_lang > 1) g_lang = 0;
    g_hlMode = IniGetInt(L"General", L"HighlightMode", 0);
    if (g_hlMode < 0 || g_hlMode > 1) g_hlMode = 0;
    g_hlColor = IniGetInt(L"General", L"HighlightColor", 0xD47800);
    g_af = (IniGetInt(L"General", L"AutoPopup", 1) != 0);
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
    IniSetInt(L"Theme", L"Material", g_materialMode);
}

// 持久化键盘布局设置
static void SaveLayoutConfig() {
    IniSetInt(L"Keyboard", L"Layout", g_layoutMode);
    IniSetInt(L"Keyboard", L"FKeys", g_showFKeys ? 1 : 0);
    IniSetInt(L"Keyboard", L"FnWebLayout", g_fnWebLayout ? 1 : 0);
}

// 应用键盘布局：保存设置、重建按键；resetSize=TRUE 时按布局与 DPI 重置窗口大小
// （仅布局模式切换调用；功能键行等只增减键行的开关保持当前窗口大小）
static void ApplyKeyboardLayout(BOOL resetSize) {
    SaveLayoutConfig();
    if (resetSize) {
        RECT saved;
        if (LoadLayoutWindowRect(&saved) && LayoutRectOnScreen(saved)) {
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

// HEX 颜色编辑：每次输入产生完整合法 #RRGGBB 时实时应用
static void TryApplyHexEdit(HWND hWnd) {
    DWORD bgr;
    if (!ParseHexToBgr(g_hlEditBuf, &bgr)) return;
    g_hlColor = (int)bgr;
    g_hlMode = 1;
    ApplyTheme();
    IniSetInt(L"General", L"HighlightMode", 1);
    IniSetInt(L"General", L"HighlightColor", (int)bgr);
    if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
    (void)hWnd;
}
static void CommitHexEdit(HWND hWnd) {
    TryApplyHexEdit(hWnd);   // 合法则应用，非法则保留原色
    g_hlEditFocus = FALSE;
    InvalidateRect(hWnd, NULL, TRUE);
}

static void UpdateHighlightSlider(HWND hWnd, int hit, int mouseX) {
    SettingsMetrics m = GetSettingsMetrics(hWnd);
    RECT row = SettingsHighlightRect(m, TRUE);
    int index = hit - S_HIT_HL_HUE;
    if (index < 0 || index > 2) return;
    RECT slider = SettingsColorSliderRect(m, row, index);
    double p = (double)(mouseX - slider.left) / (double)(slider.right - slider.left - 1);
    if (p < 0.0) p = 0.0; if (p > 1.0) p = 1.0;

    double hue, sat, val;
    BgrToHsv((DWORD)g_hlColor, &hue, &sat, &val);
    if (index == 0) {
        hue = p * 359.0;
        if (sat < 0.05) sat = 0.70;
        if (val < 0.20) val = 1.0;
    } else if (index == 1) {
        sat = p;
    } else {
        val = p;
    }

    g_hlColor = (int)HsvToBgr(hue, sat, val);
    g_hlMode = 1;
    g_wallpaperAccent = FALSE;
    g_hlEditFocus = FALSE;
    ApplyTheme();
    IniSetInt(L"General", L"HighlightMode", 1);
    IniSetInt(L"General", L"HighlightColor", g_hlColor);
    IniSetInt(L"Theme", L"Wallpaper", 0);
    if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
    RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
}

static void SettingsApplyHit(HWND hWnd, int hit) {
    BOOL themeChanged = FALSE;
    BOOL materialChanged = FALSE;
    BOOL layoutChanged = FALSE;        // 布局模式切换：按布局重置窗口大小
    BOOL keyRowsChanged = FALSE;       // 仅增减键行（功能键行）：保持窗口大小
    switch (hit) {
    case S_HIT_AUTO:
        BeginSwitchAnimation(hWnd, hit, g_af, !g_af);
        g_af = !g_af;
        IniSetInt(L"General", L"AutoPopup", g_af ? 1 : 0);
        if (g_af) UpdateAutoVisibility();
        break;
    case S_HIT_CLOSE_DROP:
        g_dropClose = !g_dropClose;
        if (g_dropClose) { g_dropTheme = FALSE; g_dropMaterial = FALSE; g_dropLayout = FALSE; g_dropLang = FALSE; g_dropHl = FALSE; g_dropCloseHov = -1; }
        break;
    case S_HIT_CLOSE_OPT0:
    case S_HIT_CLOSE_OPT1:
        g_closeToTray = (hit == S_HIT_CLOSE_OPT1);
        g_dropClose = FALSE;
        if (g_rememberClose) SaveCloseSettings();
        break;
    case S_HIT_REMEMBER:
        BeginSwitchAnimation(hWnd, hit, g_rememberClose, !g_rememberClose);
        g_rememberClose = !g_rememberClose;
        SaveCloseSettings();   // 持久化“记住我的选择”
        break;
    case S_HIT_LAYOUT_DROP:
        g_dropLayout = !g_dropLayout;
        if (g_dropLayout) { g_dropTheme = FALSE; g_dropMaterial = FALSE; g_dropLang = FALSE; g_dropHl = FALSE; g_dropClose = FALSE; g_dropLayoutHov = -1; }
        break;
    case S_HIT_LAYOUT_OPT0:
    case S_HIT_LAYOUT_OPT1:
    case S_HIT_LAYOUT_OPT2:
        g_layoutMode = hit - S_HIT_LAYOUT_OPT0;
        g_dropLayout = FALSE;
        layoutChanged = TRUE;
        break;
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
    case S_HIT_SHIFTSYM:
        BeginSwitchAnimation(hWnd, hit, g_shiftSymbols, !g_shiftSymbols);
        g_shiftSymbols = !g_shiftSymbols;
        IniSetInt(L"General", L"ShiftSymbols", g_shiftSymbols ? 1 : 0);
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
        break;
    case S_HIT_THEME_DROP:
        g_dropTheme = !g_dropTheme;
        if (g_dropTheme) { g_dropMaterial = FALSE; g_dropOpacity = FALSE; g_dropLayout = FALSE; g_dropLang = FALSE; g_dropHl = FALSE; g_dropClose = FALSE; g_dropThemeHov = -1; }
        break;
    case S_HIT_THEME_OPT0:
    case S_HIT_THEME_OPT1:
    case S_HIT_THEME_OPT2:
        if (g_themeMode != hit - S_HIT_THEME_OPT0) { g_themeMode = hit - S_HIT_THEME_OPT0; themeChanged = TRUE; }
        g_dropTheme = FALSE;
        break;
    case S_HIT_MATERIAL_DROP:
        g_dropMaterial = !g_dropMaterial;
        if (g_dropMaterial) { g_dropTheme = FALSE; g_dropOpacity = FALSE; g_dropLayout = FALSE; g_dropLang = FALSE; g_dropHl = FALSE; g_dropClose = FALSE; g_dropMaterialHov = -1; }
        break;
    case S_HIT_MATERIAL_OPT0:
    case S_HIT_MATERIAL_OPT1:
    case S_HIT_MATERIAL_OPT2:
    {
        int mode = MaterialModeOf(hit - S_HIT_MATERIAL_OPT0);
        if (hit - S_HIT_MATERIAL_OPT0 < MaterialOptionCount() && g_materialMode != mode) {
            g_materialMode = mode;
            materialChanged = TRUE;
        }
        g_dropMaterial = FALSE;
        break;
    }
    case S_HIT_OPACITY_DROP:
        g_dropOpacity = !g_dropOpacity;
        if (g_dropOpacity) { g_dropTheme = FALSE; g_dropMaterial = FALSE; g_dropLayout = FALSE; g_dropLang = FALSE; g_dropHl = FALSE; g_dropClose = FALSE; g_dropOpacityHov = -1; }
        break;
    case S_HIT_OPACITY_OPT0:
    case S_HIT_OPACITY_OPT0 + 1:
    case S_HIT_OPACITY_OPT0 + 2:
    case S_HIT_OPACITY_OPT0 + 3:
    case S_HIT_OPACITY_OPT0 + 4:
    case S_HIT_OPACITY_OPT0 + 5:
        g_mainOpacity = g_opacityValues[hit - S_HIT_OPACITY_OPT0];
        g_dropOpacity = FALSE;
        IniSetInt(L"Theme", L"Opacity", g_mainOpacity);
        ApplyAllWindowMaterials();   // 立即应用透明度
        break;
    case S_HIT_LANG_DROP:
        g_dropLang = !g_dropLang;
        if (g_dropLang) { g_dropTheme = FALSE; g_dropMaterial = FALSE; g_dropLayout = FALSE; g_dropHl = FALSE; g_dropClose = FALSE; g_dropLangHov = -1; }
        break;
    case S_HIT_LANG_OPT0:
    case S_HIT_LANG_OPT1:
        g_lang = hit - S_HIT_LANG_OPT0;
        g_dropLang = FALSE;
        IniSetInt(L"General", L"Language", g_lang);
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);   // 主键盘文本立即切换
        break;
    case S_HIT_HL_DROP:
        g_dropHl = !g_dropHl;
        if (g_dropHl) { g_dropTheme = FALSE; g_dropMaterial = FALSE; g_dropLayout = FALSE; g_dropLang = FALSE; g_dropClose = FALSE; g_dropHlHov = -1; }
        break;
    case S_HIT_HL_OPT0:
    case S_HIT_HL_OPT1:
    case S_HIT_HL_OPT2:
    {
        int sel = hit - S_HIT_HL_OPT0;
        if (sel == 1) { g_hlMode = 0; g_wallpaperAccent = TRUE; }
        else if (sel == 2) { g_hlMode = 1; g_wallpaperAccent = FALSE; }
        else { g_hlMode = 0; g_wallpaperAccent = FALSE; }
        g_dropHl = FALSE;
        if (g_hlMode == 0) g_hlEditFocus = FALSE;
        ApplyTheme();
        IniSetInt(L"General", L"HighlightMode", g_hlMode);
        IniSetInt(L"Theme", L"Wallpaper", g_wallpaperAccent ? 1 : 0);
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
        break;
    }
    case S_HIT_HL_BOX:
        if (HlSel() == 2) {
            if (g_hlEditFocus) {
                CommitHexEdit(hWnd);
            } else {
                g_hlEditFocus = TRUE;
                HexFromBgr(g_hlColor, g_hlEditBuf);
                SetFocus(hWnd);
            }
        }
        break;
    case S_HIT_HL_PAL0:
    case S_HIT_HL_PAL0 + 1:
    case S_HIT_HL_PAL0 + 2:
    case S_HIT_HL_PAL0 + 3:
    case S_HIT_HL_PAL0 + 4:
        g_hlColor = (int)RgbToBgr(g_paletteRgb[hit - S_HIT_HL_PAL0]);
        g_hlMode = 1;
        g_wallpaperAccent = FALSE;
        g_hlEditFocus = FALSE;
        ApplyTheme();
        IniSetInt(L"General", L"HighlightMode", 1);
        IniSetInt(L"General", L"HighlightColor", g_hlColor);
        IniSetInt(L"Theme", L"Wallpaper", 0);
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
        ApplyAllWindowMaterials();
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
    }
    if (materialChanged) {
        ApplyTheme();
        SaveThemeConfig();
        ApplyAllWindowMaterials();
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
    }
    if (layoutChanged) ApplyKeyboardLayout(TRUE);       // 应用布局并按布局重置窗口大小
    if (keyRowsChanged) ApplyKeyboardLayout(FALSE);     // 仅重建键行，保持当前窗口大小
    RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE); // 设置页立即刷新
}

static void CloseSettingsAnimated(HWND hWnd) {
    if (!hWnd || !IsWindow(hWnd) || g_settingsClosing) return;
    g_settingsClosing = TRUE;
    RECT rc = {0};
    GetWindowRect(hWnd, &rc);
    StartWindowMotion(&g_settingsMotion, hWnd, rc.left, rc.top, rc.top + (int)(32 * GetSystemDpiScale()), 170, MOTION_DESTROY);
}

static void SettingsOnClick(HWND hWnd, int x, int y) {
    int hit = SettingsHitTest(hWnd, x, y);
    if (g_hlEditFocus && hit != S_HIT_HL_BOX) CommitHexEdit(hWnd);   // 点击其它位置时提交 HEX 编辑
    if (hit >= S_HIT_HL_HUE && hit <= S_HIT_HL_VAL) {
        g_hlSliderDrag = hit;
        SetCapture(hWnd);
        UpdateHighlightSlider(hWnd, hit, x);
        return;
    }
    if (hit == S_HIT_CLOSE) { SendMessageW(hWnd, WM_CLOSE, 0, 0); return; }
    if (hit >= S_HIT_TAB0 && hit <= S_HIT_TAB2) {
        g_sTab = hit - S_HIT_TAB0;
        g_dropTheme = FALSE;
        g_dropMaterial = FALSE;
        g_dropLayout = FALSE;
        g_dropOpacity = FALSE;
        g_dropLang = FALSE;
        g_dropHl = FALSE;
        g_dropClose = FALSE;
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
        return;
    }
    if (hit != S_HIT_NONE) {
        SettingsApplyHit(hWnd, hit);
    } else if (g_dropTheme || g_dropMaterial || g_dropLayout || g_dropOpacity || g_dropLang || g_dropHl || g_dropClose) {
        // 点击空白处关闭下拉
        g_dropTheme = FALSE;
        g_dropMaterial = FALSE;
        g_dropLayout = FALSE;
        g_dropOpacity = FALSE;
        g_dropLang = FALSE;
        g_dropHl = FALSE;
        g_dropClose = FALSE;
        RedrawWindow(hWnd, NULL, NULL, RDW_INVALIDATE | RDW_ERASE);
    }
}

static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE:
        ApplyRoundedWindow(hWnd, 14);
        ApplyWindowMaterial(hWnd);
        return 0;
    case WM_REAPPLY_MATERIAL:
        if (IsWindowVisible(hWnd)) ApplyWindowMaterial(hWnd);
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
        DrawWindowMaterialTint(surface.dc, hWnd, rc.right, rc.bottom);
        SettingsDraw(surface.dc, hWnd);
        if (!surface.buffered)
            BitBlt(dc, 0, 0, rc.right, rc.bottom, surface.dc, 0, 0, SRCCOPY);
        EndWindowPaintSurface(&surface, TRUE);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SettingsOnClick(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        return 0;
    case WM_LBUTTONUP:
        if (g_hlSliderDrag != S_HIT_NONE) {
            UpdateHighlightSlider(hWnd, g_hlSliderDrag, GET_X_LPARAM(l));
            g_hlSliderDrag = S_HIT_NONE;
            if (GetCapture() == hWnd) ReleaseCapture();
            return 0;
        }
        break;
    case WM_MOUSEMOVE: {
        if (g_hlSliderDrag != S_HIT_NONE) {
            UpdateHighlightSlider(hWnd, g_hlSliderDrag, GET_X_LPARAM(l));
            return 0;
        }
        if (!g_sTracking) { TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hWnd, 0}; TrackMouseEvent(&tme); g_sTracking = TRUE; }
        int hov = SettingsHitTest(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (hov != g_sHov) { g_sHov = hov; InvalidateRect(hWnd, NULL, TRUE); }
        int thov = (hov >= S_HIT_THEME_OPT0 && hov <= S_HIT_THEME_OPT2) ? hov - S_HIT_THEME_OPT0 : -1;
        int mhov = (hov >= S_HIT_MATERIAL_OPT0 && hov <= S_HIT_MATERIAL_OPT2) ? hov - S_HIT_MATERIAL_OPT0 : -1;
        int lhov = (hov >= S_HIT_LAYOUT_OPT0 && hov <= S_HIT_LAYOUT_OPT2) ? hov - S_HIT_LAYOUT_OPT0 : -1;
        int langov = (hov >= S_HIT_LANG_OPT0 && hov <= S_HIT_LANG_OPT1) ? hov - S_HIT_LANG_OPT0 : -1;
        int hlov = (hov >= S_HIT_HL_OPT0 && hov <= S_HIT_HL_OPT0 + 2) ? hov - S_HIT_HL_OPT0 : -1;
        int clov = (hov >= S_HIT_CLOSE_OPT0 && hov <= S_HIT_CLOSE_OPT1) ? hov - S_HIT_CLOSE_OPT0 : -1;
        int ohov = (hov >= S_HIT_OPACITY_OPT0 && hov <= S_HIT_OPACITY_OPT0 + 5) ? hov - S_HIT_OPACITY_OPT0 : -1;
        if (thov != g_dropThemeHov || mhov != g_dropMaterialHov || lhov != g_dropLayoutHov ||
            langov != g_dropLangHov || hlov != g_dropHlHov || clov != g_dropCloseHov ||
            ohov != g_dropOpacityHov) {
            g_dropThemeHov = thov;
            g_dropMaterialHov = mhov;
            g_dropLayoutHov = lhov;
            g_dropLangHov = langov;
            g_dropHlHov = hlov;
            g_dropCloseHov = clov;
            g_dropOpacityHov = ohov;
            InvalidateRect(hWnd, NULL, TRUE);
        }
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
        g_settingsHwnd = NULL;
        g_settingsClosing = FALSE;
        g_settingsMoving = FALSE;
        g_sHov = -1;
        g_sTracking = FALSE;
        g_dropTheme = FALSE;
        g_dropMaterial = FALSE;
        g_dropLayout = FALSE;
        g_dropOpacity = FALSE;
        g_dropLang = FALSE;
        g_dropHl = FALSE;
        g_dropClose = FALSE;
        g_hlEditFocus = FALSE;
        g_hlSliderDrag = S_HIT_NONE;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, w, l);
}

static void OpenSettingsTab(int tab) {
    g_sTab = (tab >= 0 && tab <= 2) ? tab : 0;   // 0=常规 1=主题 2=关于
    if (g_settingsHwnd && IsWindow(g_settingsHwnd)) {
        if (!IsWindowVisible(g_settingsHwnd)) {
            RECT rc = {0};
            GetWindowRect(g_settingsHwnd, &rc);
            StartWindowMotion(&g_settingsMotion, g_settingsHwnd, rc.left, rc.top + (int)(32 * GetSystemDpiScale()), rc.top, 190, MOTION_NONE);
        } else {
            ApplyWindowMaterial(g_settingsHwnd);
        }
        SetForegroundWindow(g_settingsHwnd);
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
        StartWindowMotion(&g_settingsMotion, g_settingsHwnd, x, y + (int)(32 * dpi), y, 190, MOTION_NONE);
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
    if (!hWnd || !IsWindow(hWnd) || g_promptClosing) return;
    g_promptClosing = TRUE;
    RECT rc = {0};
    GetWindowRect(hWnd, &rc);
    StartWindowMotion(&g_promptMotion, hWnd, rc.left, rc.top, rc.top + (int)(24 * GetSystemDpiScale()), 150, MOTION_DESTROY);
}

static void PromptDraw(HDC dc, HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    int W = rc.right, H = rc.bottom;
    double dpi = GetSystemDpiScale();
    int hdr = (int)(36 * dpi);
    (void)hWnd;
    // 标题区不再铺独立底色，与窗口背景/材质一体化
    DrawTextL(dc, 14, 0, W - 90, hdr, T(L"关闭轻键", L"Close HKeyboard"), g_sf13, C_WHITE);
    int bw = (int)(26 * dpi), bh = hdr - (int)(12 * dpi);
    int bx = W - bw - 8, by = (hdr - bh) / 2;
    DrawRoundRect(dc, bx, by, bw, bh, (g_pHov == P_HIT_CLOSE) ? C_HOVER : C_KEY, C_KEY_BORDER, 6);
    int mx = bx + bw / 2, my = by + bh / 2, r = (int)(5 * dpi);
    DrawLineAA(dc, mx - r, my - r, mx + r, my + r, C_DIM, 2.0f);
    DrawLineAA(dc, mx + r, my - r, mx - r, my + r, C_DIM, 2.0f);

    int x0 = 20, y = hdr + 12, cw = W - 40;
    int rowH = (int)(24 * dpi);
    DrawTextL(dc, x0, y, cw, (int)(20 * dpi), T(L"请选择关闭方式：", L"Choose how to close:"), g_sf13, C_DIM); y += (int)(22 * dpi);
    DrawRadio(dc, x0 + (int)(8 * dpi), y + rowH / 2, (int)(7 * dpi), g_pChoice == 0, C_BG);
    DrawTextL(dc, x0 + (int)(26 * dpi), y, cw - (int)(26 * dpi), rowH, T(L"直接退出程序", L"Exit program directly"), g_sf13, C_WHITE);
    y += rowH;
    DrawRadio(dc, x0 + (int)(8 * dpi), y + rowH / 2, (int)(7 * dpi), g_pChoice == 1, C_BG);
    DrawTextL(dc, x0 + (int)(26 * dpi), y, cw - (int)(26 * dpi), rowH, T(L"隐藏到系统托盘", L"Hide to system tray"), g_sf13, C_WHITE);
    y += rowH + (int)(4 * dpi);
    int swW = (int)(40 * dpi), swH = (int)(20 * dpi);
    int swX = x0 + cw - swW;
    DrawSwitch(dc, swX, y + (rowH - swH) / 2, swW, swH, g_pRemember);
    DrawTextL(dc, x0, y, (swX - 12) - x0, rowH, T(L"记住我的选择", L"Remember my choice"), g_sf13, C_WHITE);
    y += rowH + (int)(8 * dpi);
    int bw2 = (int)(84 * dpi), bh2 = (int)(28 * dpi);
    int bxCancel = W - 20 - bw2;                    // 按钮右对齐
    int bxOk = bxCancel - (int)(12 * dpi) - bw2;
    DrawRoundRect(dc, bxOk, y, bw2, bh2, (g_pHov == P_HIT_OK) ? C_HOVER : C_HOT, C_KEY_BORDER, 6);
    DrawTextC(dc, bxOk, y, bw2, bh2, T(L"确定", L"OK"), g_sf13, IsLightColor(C_HOT) ? 0x1A1A1A : C_WHITE);
    DrawRoundRect(dc, bxCancel, y, bw2, bh2, (g_pHov == P_HIT_CANCEL) ? C_HOVER : C_KEY, C_KEY_BORDER, 6);
    DrawTextC(dc, bxCancel, y, bw2, bh2, T(L"取消", L"Cancel"), g_sf13, C_WHITE);
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
        ApplyWindowMaterial(hWnd);
        return 0;
    case WM_REAPPLY_MATERIAL:
        if (IsWindowVisible(hWnd)) ApplyWindowMaterial(hWnd);
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
        DrawWindowMaterialTint(surface.dc, hWnd, rc.right, rc.bottom);
        PromptDraw(surface.dc, hWnd);
        if (!surface.buffered)
            BitBlt(dc, 0, 0, rc.right, rc.bottom, surface.dc, 0, 0, SRCCOPY);
        EndWindowPaintSurface(&surface, TRUE);
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
        g_pHov = -1;
        g_pTracking = FALSE;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, w, l);
}

static void OpenClosePrompt() {
    if (g_closePromptHwnd && IsWindow(g_closePromptHwnd)) {
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
        StartWindowMotion(&g_promptMotion, g_closePromptHwnd, x, y + (int)(24 * dpi), y, 150, MOTION_NONE);
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

    // 主题切换子菜单
    HMENU themeMenu = CreatePopupMenu();
    AppendMenuW(themeMenu, MF_STRING | (g_themeMode == 0 ? MF_CHECKED : 0), ID_MENU_THEME + 1, T(L"\x8DDF\x968F\x7CFB\x7EDF", L"Follow System"));
    AppendMenuW(themeMenu, MF_STRING | (g_themeMode == 1 ? MF_CHECKED : 0), ID_MENU_THEME + 2, T(L"\x6DF1\x8272\x4E3B\x9898", L"Dark Theme"));
    AppendMenuW(themeMenu, MF_STRING | (g_themeMode == 2 ? MF_CHECKED : 0), ID_MENU_THEME + 3, T(L"\x6D45\x8272\x4E3B\x9898", L"Light Theme"));
    AppendMenuW(m, MF_POPUP, (UINT_PTR)themeMenu, T(L"\x4E3B\x9898", L"Theme"));
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
    } else if (id == ID_MENU_THEME + 1) {
        g_themeMode = 0; ApplyTheme(); SaveThemeConfig(); ApplyAllWindowMaterials(); InvalidateRect(hWnd, 0, TRUE);
    } else if (id == ID_MENU_THEME + 2) {
        g_themeMode = 1; ApplyTheme(); SaveThemeConfig(); ApplyAllWindowMaterials(); InvalidateRect(hWnd, 0, TRUE);
    } else if (id == ID_MENU_THEME + 3) {
        g_themeMode = 2; ApplyTheme(); SaveThemeConfig(); ApplyAllWindowMaterials(); InvalidateRect(hWnd, 0, TRUE);
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
        if (!g_manualHide && !g_vis) ShowKB(TRUE, FALSE);
        return;
    }

    HWND fg = GetForegroundWindow();
    if (fg == g_settingsHwnd || fg == g_closePromptHwnd) return;

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
        case HDR_MIN:
            // 最小化仅收起键盘，不做抑制：输入框再次获得焦点时正常自动呼出
            ShowKB(FALSE, FALSE);
            break;
        case HDR_CLOSE: HandleCloseAction(hWnd); break;
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
        ApplyWindowMaterial(hWnd);

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
        double dpiScale = GetSystemDpiScale();
        // 允许自由缩小（小键盘布局等），仅挡住过小尺寸
        mmi->ptMinTrackSize.x = (int)(300 * dpiScale);
        mmi->ptMinTrackSize.y = (int)(150 * dpiScale);
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
        WindowPaintSurfaceLocal surface = BeginWindowPaintSurface(dc, hWnd, rc);
        ClearWindowBackBuffer(surface.dc, hWnd, g_ww, g_wh);
        DrawWindowMaterialTint(surface.dc, hWnd, g_ww, g_wh);
        DrawHeader(surface.dc);
        DrawKeys(surface.dc);
        if (!surface.buffered)
            BitBlt(dc, 0, 0, g_ww, g_wh, surface.dc, 0, 0, SRCCOPY);
        EndWindowPaintSurface(&surface, TRUE);
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
    case WM_REAPPLY_MATERIAL:
        if (IsWindowVisible(hWnd)) ApplyWindowMaterial(hWnd);
        return 0;
    case WM_SHOW_KEYBOARD:
        if (w) {
            ApplyTheme();
            ApplyWindowMaterial(hWnd);
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
        case ID_MENU_THEME + 1: g_themeMode = 0; ApplyTheme(); SaveThemeConfig(); ApplyAllWindowMaterials(); InvalidateRect(hWnd, 0, TRUE); break;
        case ID_MENU_THEME + 2: g_themeMode = 1; ApplyTheme(); SaveThemeConfig(); ApplyAllWindowMaterials(); InvalidateRect(hWnd, 0, TRUE); break;
        case ID_MENU_THEME + 3: g_themeMode = 2; ApplyTheme(); SaveThemeConfig(); ApplyAllWindowMaterials(); InvalidateRect(hWnd, 0, TRUE); break;
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
        if (g_winHook) { UnhookWinEvent(g_winHook); g_winHook = 0; }
        if (g_fgHook) { UnhookWinEvent(g_fgHook); g_fgHook = 0; }
        if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = 0; }
        if (g_tray) {   // 显式删除托盘图标，避免程序退出后图标残留到鼠标悬停才消失
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            g_tray = FALSE;
        }
        DeleteObject(g_f12); DeleteObject(g_f13); DeleteObject(g_f13b); DeleteObject(g_f14);
        DeleteObject(g_f14b); DeleteObject(g_f16b); DeleteObject(g_f18b);
        DeleteObject(g_sf12); DeleteObject(g_sf13); DeleteObject(g_sf13b); DeleteObject(g_sf14b); DeleteObject(g_sf20b); DeleteObject(g_sfIcon);
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

    // 系统环境检测需在字体初始化前完成（图标字体按系统版本选择）
    g_isWinPE = IsWindowsPE();
    DetectWinVersion();

    InitGdiPlus();       // 初始化 GDI+ （抗锯齿圆形绘图）
    LoadEmbeddedFonts();   // 注册内嵌字体（MiSans 精简版），失败自动回退系统字体
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
    if (g_fontRegRegular) RemoveFontMemResourceEx(g_fontRegRegular);
    if (g_fontRegBold) RemoveFontMemResourceEx(g_fontRegBold);
    if (g_fontRegMdl2) RemoveFontMemResourceEx(g_fontRegMdl2);
    if (g_timePeriod.end) g_timePeriod.end(1);
    ShutdownGdiPlus();
    return (int)msg.wParam;
}
