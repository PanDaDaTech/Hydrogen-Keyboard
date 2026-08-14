// hkeyboard.cpp - HKeyboard 轻键 (Pure Win32 C++)
// SPDX-License-Identifier: MIT
#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0501
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <objbase.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "resource.h"

// 当前编译架构（关于页显示用）
#ifdef _M_ARM64
#define HK_ARCH L"arm64"
#elif defined(_M_X64)
#define HK_ARCH L"64位"
#else
#define HK_ARCH L"32位"
#endif

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

#define KEY_AREA_X   6
#define KEY_AREA_W   (g_ww - 12)

#define TIMER_FOCUS     8820
#define TIMER_EXIT      8822
#define TIMER_SLIDE     8823
#define TIMER_REPEAT    8826
// 自动呼出参数
#define AUTO_POP_COOLDOWN_MS 400    // 隐藏后再次自动弹出冷却（毫秒）
#define MANUAL_HIDE_GRACE_MS 3000   // 手动显示后自动隐藏宽限期（毫秒）
#define WM_TRAY         (WM_APP + 100)
#define WM_FOCUS_EVENT  (WM_APP + 101)

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

// 读取系统壁纸自动派生的强调色并转为 GDI COLORREF (BGR)。
// 优先读 DWM AccentColor（Windows“自动从壁纸取色”时即为壁纸派生的强调色），
// 不可用时回退 ColorizationColor（DWM 现用取色颜色）。
// 注册表值均为 ABGR (0xAABBGGRR)；alpha 位仅用于透明度提示，对强调色无意义，直接忽略。
static DWORD GetWallpaperAccentBgr() {
    HKEY hKey;
    LONG ok = ERROR_SUCCESS;
    DWORD val = 0, sz = sizeof(val);

    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\DWM",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        ok = RegQueryValueExW(hKey, L"AccentColor", NULL, NULL, (LPBYTE)&val, &sz);
        RegCloseKey(hKey);
    }
    if (ok == ERROR_SUCCESS && (val & 0xFFFFFF) != 0) {
        // ABGR -> COLORREF BGR
        return ((val & 0xFF) << 16) | (val & 0x00FF00) | ((val >> 16) & 0xFF);
    }

    // 备用：ColorizationColor
    ok = ERROR_SUCCESS; val = 0; sz = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
        L"Software\\Microsoft\\Windows\\DWM",
        0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        ok = RegQueryValueExW(hKey, L"ColorizationColor", NULL, NULL, (LPBYTE)&val, &sz);
        RegCloseKey(hKey);
    }
    if (ok == ERROR_SUCCESS && (val & 0xFFFFFF) != 0) {
        return ((val & 0xFF) << 16) | (val & 0x00FF00) | ((val >> 16) & 0xFF);
    }
    return 0;
}

static void ApplyTheme() {
    const ThemeColors* base;
    if (g_themeMode == 1) {
        base = &g_darkTheme;
    } else if (g_themeMode == 2) {
        base = &g_lightTheme;
    } else {
        base = IsSystemDarkMode() ? &g_darkTheme : &g_lightTheme;
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

enum KeyType {
    K_NORMAL, K_LETTER, K_MOD, K_CAPS,
    K_SPECIAL, K_ARROW, K_SPACE, K_HIDE, K_DOCK, K_MIN, K_CLOSE
};

struct KeyDef { int x, y, w, h; short vk; KeyType type; };

// C++ 函数前置声明
static void ShowKB(BOOL show, BOOL isManual = FALSE);
static void ToggleKB();
static void HandleCloseAction(HWND hWnd);
static void OpenClosePrompt();
static void RecreateFontsAndLayout();
static double GetSystemDpiScale();
static void InitWindowSizeForDpi();
static void SendKey(BYTE vk, BOOL sh, BOOL ct, BOOL al, BOOL win = FALSE);

// Global state
HINSTANCE   g_hInst = 0;
HWND        g_hWnd = 0;
HICON       g_hTrayIcon = 0;
BOOL        g_vis = FALSE;
BOOL        g_manualShow = FALSE;
DWORD       g_manualShowTick = 0;        // 手动显示时刻（自动隐藏宽限期用）
BOOL        g_manualHide = FALSE;      // 用户显式收起（×隐藏到托盘）后不自动弹出，直到手动重新显示
DWORD       g_hideUntil = 0;           // 最小化后禁止自动呼出的截止时刻（3 秒抑制）
int         g_hideDelayMs = 500;       // 自动隐藏延迟（毫秒）
DWORD       g_lastNonInput = 0;        // 最近一次离焦时刻（自动隐藏延迟用）
int         g_lang = 0;                // 语言：0=中文 1=English
int         g_hlMode = 0;              // 高亮颜色：0=默认 1=自定义
int         g_hlColor = 0xD47800;      // 自定义高亮颜色（BGR）

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
int         g_layoutMode = 0;          // 键盘布局：0=全尺寸 1=小键盘
BOOL        g_showFKeys = FALSE;       // 顶部显示 F1~F12 键
BOOL        g_shiftSymbols = TRUE;     // 按 Shift 时显示特殊符号（否则显示数字）
DWORD       g_lht = 0;
int         g_hk = -1, g_pk = -1;
int         g_repeatKeyIdx = -1;
BOOL        g_tracking = FALSE;
BOOL        g_tray = FALSE;
int         g_slideFrom = 0, g_slideTo = 0, g_slideStep = -1;
HWINEVENTHOOK g_winHook = 0;
HANDLE      g_mutex = 0;
#define SLIDE_STEPS 8
#define SLIDE_MS 8
HFONT       g_f12 = 0, g_f13 = 0, g_f13b = 0, g_f14 = 0, g_f14b = 0, g_f16b = 0, g_f18b = 0;
static HFONT g_sf12 = 0, g_sf13 = 0, g_sf13b = 0, g_sf14b = 0;   // 设置/关闭窗口固定字号字体
static HANDLE g_fontRegRegular = 0;    // AddFontMemResourceEx 句柄（内嵌字体）
static HANDLE g_fontRegBold = 0;
static BOOL   g_fontReady = FALSE;     // 内嵌字体注册成功（失败回退系统字体）
NOTIFYICONDATAW g_nid;

// Fn 功能键层：TRUE 时数字行显示为 F1~F12
BOOL        g_fnLayer = FALSE;

#define MAX_KEYS 120
KeyDef g_keys[MAX_KEYS];
int g_nk = 0;

static double GetSystemDpiScale() {
    HDC hdc = GetDC(NULL);
    int dpiY = GetDeviceCaps(hdc, LOGPIXELSY);
    ReleaseDC(NULL, hdc);
    if (dpiY < 96) dpiY = 96;
    return (double)dpiY / 96.0;
}

static void InitWindowSizeForDpi() {
    double dpiScale = GetSystemDpiScale();
    if (g_layoutMode == 1) {        // 小键盘默认更窄
        g_ww = (int)(430 * dpiScale);
        g_wh = (int)(320 * dpiScale);
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

// 常用布局（标准 87 键 TKL：无数字小键盘、无 Fn，可选 F1~F12 顶行）
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

    // Row 1: `, 1-0, -, =, Backspace (14 keys)
    {
        int wBksp = (int)(80 * dpiScale * scaleX);
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

    // Row 5: Ctrl, Win, Alt, Space, Alt, Menu, Ctrl, ←, ↓, → (10 keys)
    {
        int wCtl = (int)(56 * dpiScale * scaleX);
        int wWin = (int)(46 * dpiScale * scaleX);
        int wAlt = (int)(58 * dpiScale * scaleX);
        int wMenu = (int)(46 * dpiScale * scaleX);
        int wArw = (int)(52 * dpiScale * scaleX);
        int leftOfArrows = wCtl + wWin + wAlt + wAlt + wMenu + wCtl;
        int spaceW = KEY_AREA_W - leftOfArrows - wArw * 3 - 9 * g_keyGap;
        if (spaceW < 60) spaceW = 60;
        int w[10] = {wCtl, wWin, wAlt, spaceW, wAlt, wMenu, wCtl, wArw, wArw, wArw};
        short v[10] = {0x11, 0x5B, 0x12, 0x20, 0x12, 0x5D, 0x11, 0x25, 0x28, 0x27};
        KeyType t[10] = {K_MOD, K_SPECIAL, K_MOD, K_SPACE, K_MOD, K_NORMAL, K_MOD, K_ARROW, K_ARROW, K_ARROW};
        int x = KEY_AREA_X;
        for (int i = 0; i < 10; i++) { AddKey(x, y, w[i], g_keyHeight, v[i], t[i]); x += w[i] + g_keyGap; }
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

    // 行数：全尺寸 5 行 + 可选 F1~F12 顶行；小键盘 5 行
    int rows = 5 + (g_layoutMode != 1 && g_showFKeys ? 1 : 0);   // 全尺寸/常用可选 F1~F12 顶行
    g_keyHeight = (g_wh - g_headerH - 8 - (rows - 1) * g_keyGap) / rows;
    if (g_keyHeight < 20) g_keyHeight = 20;

    int y = g_headerH + g_keyGap + 2;

    if (g_layoutMode == 1) {   // 小键盘
        BuildNumpad(y);
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

// 注册内嵌字体（阿里巴巴普惠体精简版）到当前进程；失败则回退系统字体
static void LoadEmbeddedFonts() {
    struct { int id; HANDLE* slot; } fonts[2] = {
        { IDR_FONT_REGULAR, &g_fontRegRegular },
        { IDR_FONT_BOLD,    &g_fontRegBold },
    };
    for (int i = 0; i < 2; i++) {
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
            g_fontReady = TRUE;
        }
    }
}

static HFONT MakeFont(int size, BOOL bold) {
    HDC hdc = GetDC(0);
    int h = -MulDiv(size, 96, 72);
    ReleaseDC(0, hdc);
    return CreateFontW(h, 0, 0, 0, bold ? FW_BOLD : FW_NORMAL,
        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, g_fontReady ? L"Alibaba PuHuiTi 3.0 55 Regular" : L"Microsoft YaHei");
}

// 设置/关闭窗口使用固定字号字体（不随主键盘窗口缩放，仅随 DPI）
static void InitFixedFonts() {
    double dpi = GetSystemDpiScale();
    g_sf12  = MakeFont((int)(10 * dpi), 0);   // 统一 10 号：提示小字
    g_sf13  = MakeFont((int)(10 * dpi), 0);   // 统一 10 号：行文本
    g_sf13b = MakeFont((int)(10 * dpi), 1);   // 统一 10 号：Tab / 小节标题 / 按钮
    g_sf14b = MakeFont((int)(10 * dpi), 1);   // 统一 10 号：关于大标题
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
    RECT r = {x, y, x + w, y + h};
    HBRUSH br = CreateSolidBrush(c);
    FillRect(dc, &r, br);
    DeleteObject(br);
}

static void DrawRoundRect(HDC dc, int x, int y, int w, int h, DWORD fillC, DWORD borderC, int radius) {
    HPEN p = CreatePen(PS_SOLID, 1, borderC);
    HPEN op = (HPEN)SelectObject(dc, p);
    HBRUSH b = CreateSolidBrush(fillC);
    HBRUSH ob = (HBRUSH)SelectObject(dc, b);
    RoundRect(dc, x, y, x + w, y + h, radius, radius);
    SelectObject(dc, ob); DeleteObject(b);
    SelectObject(dc, op); DeleteObject(p);
}

static void DrawTextC(HDC dc, int x, int y, int w, int h, const wchar_t* s, HFONT f, DWORD c) {
    RECT r = {x, y, x + w, y + h};
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
    SelectObject(dc, fShift);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, shiftC);
    DrawTextW(dc, buf, -1, &rt, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

    // 主字符（键下半部）
    buf[0] = baseCh;
    RECT rb = {x, y + h / 2, x + w, y + h};
    SelectObject(dc, fBase);
    SetTextColor(dc, baseC);
    DrawTextW(dc, buf, -1, &rb, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
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

static const wchar_t* KeyText(const KeyDef* k) {
    static wchar_t buf[16];
    if (k->type == K_LETTER) {
        return LetterKeyText(k->vk);
    }
    if (k->type == K_NORMAL) {
        if (g_fnLayer) {
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
        if (g_fnLayer) {
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
        if (k->vk == 0) {  // Fn 键：切换 F1~F12 功能层
            g_fnLayer = !g_fnLayer;
            if (g_fnLayer) g_sh = FALSE;
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
            if (g_sh) {
                g_sh = FALSE;
                g_fnLayer = FALSE;
                ToggleImeLang();
            } else {
                g_sh = TRUE;
                g_fnLayer = FALSE;
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
    double scaleX = (double)g_ww / (980.0 * dpiScale);

    int rMargin = (int)(6 * dpiScale * scaleX);
    int gap     = (int)(6 * dpiScale * scaleX);
    int wClose = (int)(36 * dpiScale * scaleX);
    int wMin   = (int)(36 * dpiScale * scaleX);
    int wMenu  = (int)(48 * dpiScale * scaleX);

    int xClose = g_ww - rMargin - wClose;
    int xMin   = xClose - gap - wMin;
    int xMenu  = (int)(6 * dpiScale * scaleX);

    if (x >= xClose && x < g_ww) return HDR_CLOSE;
    if (x >= xMin && x < xClose) return HDR_MIN;
    if (x >= xMenu && x < xMenu + wMenu + gap) return HDR_DOCK;
    return -1;
}

static void DrawHeader(HDC dc) {
    Fill(dc, 0, 0, g_ww, g_headerH, C_HDR);

    double dpiScale = GetSystemDpiScale();
    double scaleX = (double)g_ww / (980.0 * dpiScale);
    double scaleY = (double)g_wh / (320.0 * dpiScale);

    int rMargin = (int)(6 * dpiScale * scaleX);
    int gap     = (int)(6 * dpiScale * scaleX);
    int btnH    = g_headerH - (int)(12 * dpiScale * scaleY);
    if (btnH < 22) btnH = 22;
    int btnY = (g_headerH - btnH) / 2;

    int wClose = (int)(36 * dpiScale * scaleX);
    int wMin   = (int)(36 * dpiScale * scaleX);
    int wMenu  = (int)(48 * dpiScale * scaleX);

    int xClose = g_ww - rMargin - wClose;
    int xMin   = xClose - gap - wMin;
    int xMenu  = (int)(6 * dpiScale * scaleX);

    DrawRoundRect(dc, xMenu, btnY, wMenu, btnH, C_KEY, C_KEY_BORDER, btnH / 2);
    DrawTextC(dc, xMenu, btnY, wMenu, btnH, T(L"\x8BBE\x7F6E", L"Settings"), g_f12, C_WHITE);   // 菜单按钮 → 打开设置页

    int xTitle = xMenu + wMenu + gap;
    int wTitle = xMin - xTitle - gap;
    if (wTitle > 40) {
        DrawTextC(dc, xTitle, 0, wTitle, g_headerH, L"", g_f12, C_DIM);
    }

    // 最小化图标：直接绘制居中小横条（避免字体缺少 U+229F 字形时显示为 "-"）
    {
        int barW = (int)(14 * dpiScale * scaleX);
        int barH = (int)(2 * dpiScale * scaleY); if (barH < 2) barH = 2;
        int barX = xMin + (wMin - barW) / 2;
        int barY = btnY + btnH / 2 - barH / 2;
        DrawRoundRect(dc, barX, barY, barW, barH, C_DIM, C_DIM, barH / 2);
    }
    // 关闭图标：与最小化一样直接绘制（X），避免字体缺少字形时显示异常
    {
        int cx = xClose + wClose / 2;
        int cy = g_headerH / 2;
        int r  = (int)(7 * dpiScale * scaleX); if (r < 5) r = 5;
        HPEN pen = CreatePen(PS_SOLID, 2, C_DIM);
        HPEN op = (HPEN)SelectObject(dc, pen);
        HGDIOBJ ob = SelectObject(dc, GetStockObject(NULL_BRUSH));
        MoveToEx(dc, cx - r, cy - r, NULL);
        LineTo(dc, cx + r, cy + r);
        MoveToEx(dc, cx + r, cy - r, NULL);
        LineTo(dc, cx - r, cy + r);
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(pen);
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
        else {
            int dt[] = {K_SPECIAL, K_CAPS, K_MOD, K_ARROW, K_HIDE};
            for (size_t j = 0; j < sizeof(dt)/sizeof(dt[0]); j++) {
                if (k->type == dt[j]) { bg = C_DARK; break; }
            }
        }

        DrawRoundRect(dc, k->x, k->y, k->w, k->h, bg, C_KEY_BORDER, 8);

        const wchar_t* txt = KeyText(k);
        HFONT f = g_f14b;
        if (k->type == K_HIDE || k->type == K_ARROW) f = g_f14b;
        if (k->vk == 0x08) f = g_f18b;
        if (k->vk == 0x0D) f = g_f13b;
        if (k->vk == 0x20 || k->type == K_SPACE) f = g_f14b;
        DWORD textC = (active || pressed) && IsLightColor(bg) ? 0x1A1A1A : C_WHITE;

        // 双符号键（数字行/标点）：同时显示主字符与副符号，副符号随 Shift 灰/白；
        // Fn 层时仅数字行/-/= 键改为显示 F1~F12（不显示双符号），其余标点键双符号显示不变。
        wchar_t baseCh = 0, shiftCh = 0;
        if (k->type == K_NORMAL && !(g_fnLayer && FnMap(k->vk) != 0)) {
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

static void ShowKB(BOOL show, BOOL isManual) {
    if (!g_hWnd) return;
    KillTimer(g_hWnd, TIMER_SLIDE);
    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int sx = work.left + ((work.right - work.left) - g_ww) / 2;
    int targetY = work.bottom - g_wh - 6;

    if (show) {
        if (isManual) { g_manualShow = TRUE; g_manualShowTick = GetTickCount(); g_manualHide = FALSE; g_hideUntil = 0; }
        if (g_vis) {
            SetWindowPos(g_hWnd, HWND_TOPMOST, sx, targetY, g_ww, g_wh, SWP_NOACTIVATE | SWP_SHOWWINDOW);
            return;
        }
        g_vis = TRUE;
        g_slideFrom = work.bottom;
        g_slideTo = targetY;
        g_slideStep = 0;
        SetWindowPos(g_hWnd, HWND_TOPMOST, sx, g_slideFrom, g_ww, g_wh, SWP_SHOWWINDOW | SWP_NOACTIVATE);
        SetTimer(g_hWnd, TIMER_SLIDE, SLIDE_MS, 0);
    } else {
        if (!g_vis) return;
        g_manualShow = FALSE;
        g_lht = GetTickCount();
        RECT rc; GetWindowRect(g_hWnd, &rc);
        g_slideFrom = rc.top;
        g_slideTo = work.bottom;
        g_slideStep = 0;
        SetTimer(g_hWnd, TIMER_SLIDE, SLIDE_MS, 0);
    }
}

static void ToggleKB() { ShowKB(!g_vis, TRUE); }

// × 关闭：已记住选择则直接按所记方式执行，否则弹出关闭方式提示窗口
static void HandleCloseAction(HWND hWnd) {
    (void)hWnd;
    if (g_rememberClose) {
        if (g_closeToTray) {
            g_manualHide = TRUE;      // 显式隐藏到托盘后不再自动弹出
            ShowKB(FALSE, FALSE);
        } else if (g_hWnd && IsWindow(g_hWnd)) {
            DestroyWindow(g_hWnd);
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
#define S_HIT_CLOSE_DIRECT   11
#define S_HIT_CLOSE_TRAY     12
#define S_HIT_REMEMBER       13
#define S_HIT_LAYOUT_DROP    14
#define S_HIT_LAYOUT_OPT0    15
#define S_HIT_LAYOUT_OPT1    16
#define S_HIT_LAYOUT_OPT2    19
#define S_HIT_FKEYS          17
#define S_HIT_SHIFTSYM       18
#define S_HIT_THEME_DROP     20
#define S_HIT_THEME_OPT0     21
#define S_HIT_THEME_OPT1     22
#define S_HIT_THEME_OPT2     23
#define S_HIT_WALLPAPER      24
#define S_HIT_URL            30
#define S_HIT_HIDEDELAY_DROP  40
#define S_HIT_HIDEDELAY_OPT0  41
#define S_HIT_LANG_DROP       50
#define S_HIT_LANG_OPT0       51
#define S_HIT_HL_DROP         60
#define S_HIT_HL_OPT0         61
#define S_HIT_HL_BOX          63

static HWND g_settingsHwnd = 0;
static int  g_sTab = 0;        // 0=常规 1=主题 2=关于
static int  g_sHov = -1;       // 悬停元素，-1=无
static BOOL g_sTracking = FALSE;
static BOOL g_dropTheme = FALSE;    // 主题下拉是否展开
static BOOL g_dropLayout = FALSE;   // 布局下拉是否展开
static int  g_dropThemeHov = -1;
static int  g_dropLayoutHov = -1;
static BOOL g_dropHideDelay = FALSE;   // 自动隐藏延迟下拉
static int  g_dropHideDelayHov = -1;
static BOOL g_dropLang = FALSE;        // 语言下拉
static int  g_dropLangHov = -1;
static BOOL g_dropHl = FALSE;          // 高亮颜色下拉
static int  g_dropHlHov = -1;
static BOOL g_hlEditFocus = FALSE;     // HEX 输入框是否处于编辑态
static wchar_t g_hlEditBuf[8] = {0};   // 编辑中的 HEX 文本（#RRGGBB）
static const int g_hideDelayValues[6] = {0, 300, 500, 1000, 2000, 5000};
static const wchar_t* g_hideDelayNames[6] = { L"立即隐藏", L"0.3 秒", L"0.5 秒", L"1 秒", L"2 秒", L"5 秒" };
static const wchar_t* g_hideDelayNamesEn[6] = { L"Immediately", L"0.3 s", L"0.5 s", L"1 s", L"2 s", L"5 s" };
static const wchar_t* g_langNames[2] = { L"简体中文", L"English" };
static const wchar_t* g_langNamesEn[2] = { L"Simplified Chinese", L"English" };
static const wchar_t* g_hlModeNames[2] = { L"默认", L"自定义" };
static const wchar_t* g_hlModeNamesEn[2] = { L"Default", L"Custom" };
static const wchar_t* g_themeNames[3] = { L"跟随系统", L"深色主题", L"浅色主题" };
static const wchar_t* g_themeNamesEn[3] = { L"Follow System", L"Dark Theme", L"Light Theme" };
static const wchar_t* g_layoutNames[3] = { L"全尺寸", L"小键盘", L"常用" };
static const wchar_t* g_layoutNamesEn[3] = { L"Full", L"Numpad", L"Common" };

static void DrawTextL(HDC dc, int x, int y, int w, int h, const wchar_t* s, HFONT f, DWORD c) {
    RECT r = {x, y, x + w, y + h};
    SelectObject(dc, f);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, c);
    DrawTextW(dc, s, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
}

static void DrawRadio(HDC dc, int x, int cy, int r, BOOL on) {
    HPEN p = CreatePen(PS_SOLID, 1, C_DIM);
    HPEN op = (HPEN)SelectObject(dc, p);
    HBRUSH b = CreateSolidBrush(C_BG);
    HBRUSH ob = (HBRUSH)SelectObject(dc, b);
    Ellipse(dc, x - r, cy - r, x + r, cy + r);
    SelectObject(dc, ob); DeleteObject(b);
    SelectObject(dc, op); DeleteObject(p);
    if (on) {
        HBRUSH bb = CreateSolidBrush(C_HOT);
        HBRUSH obb = (HBRUSH)SelectObject(dc, bb);
        Ellipse(dc, x - r + 3, cy - r + 3, x + r - 3, cy + r - 3);
        SelectObject(dc, obb); DeleteObject(bb);
    }
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
    DWORD bg = active ? C_HOT : (hover ? C_HOVER : C_KEY);
    DrawRoundRect(dc, x, y, w, h, bg, C_KEY_BORDER, 6);
    DrawTextC(dc, x, y, w, h, label, g_sf13b, IsLightColor(bg) ? 0x1A1A1A : C_WHITE);
}

// 圆角方框面板（设置项容器）
static void DrawPanel(HDC dc, int x, int y, int w, int h) {
    DrawRoundRect(dc, x, y, w, h, C_KEY, C_KEY_BORDER, 8);
}

// 下拉框
static void DrawCombo(HDC dc, int x, int y, int w, int h, const wchar_t* text, BOOL open, BOOL hover) {
    DrawRoundRect(dc, x, y, w, h, (open || hover) ? C_HOVER : C_DARK, C_DIM, 6);
    DrawTextL(dc, x + 10, y, w - 30, h, text, g_sf13, C_WHITE);
    int ax = x + w - 14, ay = y + h / 2;
    HPEN pen = CreatePen(PS_SOLID, 1, C_DIM);
    HPEN op = (HPEN)SelectObject(dc, pen);
    HBRUSH br = CreateSolidBrush(C_DIM);
    HBRUSH ob = (HBRUSH)SelectObject(dc, br);
    POINT tri[3] = { {ax - 5, ay - 3}, {ax + 5, ay - 3}, {ax, ay + 3} };
    Polygon(dc, tri, 3);
    SelectObject(dc, ob); DeleteObject(br);
    SelectObject(dc, op); DeleteObject(pen);
}

// 下拉列表（选中项带勾，浅/深色模式一致）
static void DrawComboList(HDC dc, int x, int y, int w, int itemH, const wchar_t** items, int count, int sel, int hov) {
    DrawRoundRect(dc, x, y, w, itemH * count + 4, C_DARK, C_DIM, 8);
    for (int i = 0; i < count; i++) {
        int iy = y + 2 + i * itemH;
        if (i == hov) Fill(dc, x + 2, iy, w - 4, itemH, C_HOVER);
        if (i == sel) {
            HPEN pen = CreatePen(PS_SOLID, 2, C_HOT);
            HPEN op = (HPEN)SelectObject(dc, pen);
            MoveToEx(dc, x + 10, iy + itemH / 2, NULL);
            LineTo(dc, x + 15, iy + itemH / 2 + 4);
            LineTo(dc, x + 22, iy + itemH / 2 - 4);
            SelectObject(dc, op); DeleteObject(pen);
            DrawTextL(dc, x + 28, iy, w - 34, itemH, items[i], g_sf13, C_HOT);
        } else {
            DrawTextL(dc, x + 28, iy, w - 34, itemH, items[i], g_sf13, C_WHITE);
        }
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
static int HideDelayIndex() {
    for (int i = 0; i < 6; i++) if (g_hideDelayValues[i] == g_hideDelayMs) return i;
    return 1;
}
static const wchar_t* HideDelayName() {
    int idx = HideDelayIndex();
    return g_lang ? g_hideDelayNamesEn[idx] : g_hideDelayNames[idx];
}

static void SettingsDraw(HDC dc, HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    int W = rc.right, H = rc.bottom;
    double dpi = GetSystemDpiScale();
    int hdr = (int)(40 * dpi);

    Fill(dc, 0, 0, W, hdr, C_HDR);
    DrawTextL(dc, 14, 0, W - 90, hdr, T(L"设置", L"Settings"), g_sf13, C_WHITE);
    int bw = (int)(26 * dpi), bh = hdr - (int)(12 * dpi);
    int bx = W - bw - 8, by = (hdr - bh) / 2;
    DrawRoundRect(dc, bx, by, bw, bh, (g_sHov == S_HIT_CLOSE) ? C_HOVER : C_KEY, C_KEY_BORDER, 6);
    int mx = bx + bw / 2, my = by + bh / 2, r = (int)(5 * dpi);
    HPEN pen = CreatePen(PS_SOLID, 2, C_DIM); HPEN op = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, mx - r, my - r, NULL); LineTo(dc, mx + r, my + r);
    MoveToEx(dc, mx + r, my - r, NULL); LineTo(dc, mx - r, my + r);
    SelectObject(dc, op); DeleteObject(pen);

    // 左侧 Tab（纵向），关于在最底
    int tabX = 12, tabY = hdr + 12;
    int tabW = (int)(110 * dpi), tabH = (int)(38 * dpi), tabGap = (int)(8 * dpi);
    SettingsTab(dc, tabX, tabY, tabW, tabH, T(L"常规", L"General"), g_sTab == 0, g_sHov == S_HIT_TAB0); tabY += tabH + tabGap;
    SettingsTab(dc, tabX, tabY, tabW, tabH, T(L"主题", L"Theme"), g_sTab == 1, g_sHov == S_HIT_TAB1); tabY += tabH + tabGap;
    SettingsTab(dc, tabX, tabY, tabW, tabH, T(L"关于", L"About"), g_sTab == 2, g_sHov == S_HIT_TAB2);

    // 右侧内容
    int x0 = tabX + tabW + 16, y = hdr + 16, cw = W - x0 - 16;
    int rowH = (int)(26 * dpi);
    int panelPad = 12, comboW = (int)(150 * dpi), comboH = (int)(24 * dpi);
    if (g_sTab == 0) {
        // ===== 常规：三个圆角面板 ======
        int py = y;
        // 面板 1：自动呼出
        int p1h = 8 + 20 + 4 + rowH + 8;
        DrawPanel(dc, x0, py, cw, p1h);
        {
            int ix = x0 + panelPad, iy = py + 8;
            DrawTextL(dc, ix, iy, cw - 24, (int)(20 * dpi), T(L"自动呼出", L"Auto Pop-up"), g_sf13b, C_DIM);
            iy += (int)(20 * dpi) + 4;
            DrawCheck(dc, ix, iy + (int)(2 * dpi), (int)(16 * dpi), g_af);
            DrawTextL(dc, ix + (int)(26 * dpi), iy, cw - 24 - (int)(26 * dpi), rowH, T(L"点击输入框时自动弹出键盘", L"Auto show when clicking an input"), g_sf13, C_WHITE);
        }
        py += p1h + 10;
        // 面板 2：关闭按钮
        int p2h = 8 + 20 + 4 + rowH * 3 + 8;
        DrawPanel(dc, x0, py, cw, p2h);
        {
            int ix = x0 + panelPad, iy = py + 8;
            DrawTextL(dc, ix, iy, cw - 24, (int)(20 * dpi), T(L"关闭按钮 (×)", L"Close Button (×)"), g_sf13b, C_DIM);
            iy += (int)(20 * dpi) + 4;
            DrawRadio(dc, ix + (int)(8 * dpi), iy + rowH / 2, (int)(7 * dpi), !g_closeToTray);
            DrawTextL(dc, ix + (int)(26 * dpi), iy, cw - 24 - (int)(26 * dpi), rowH, T(L"直接退出程序", L"Exit program directly"), g_sf13, C_WHITE);
            iy += rowH;
            DrawRadio(dc, ix + (int)(8 * dpi), iy + rowH / 2, (int)(7 * dpi), g_closeToTray);
            DrawTextL(dc, ix + (int)(26 * dpi), iy, cw - 24 - (int)(26 * dpi), rowH, T(L"隐藏到系统托盘", L"Hide to system tray"), g_sf13, C_WHITE);
            iy += rowH;
            DrawCheck(dc, ix, iy + (int)(2 * dpi), (int)(16 * dpi), g_rememberClose);
            DrawTextL(dc, ix + (int)(26 * dpi), iy, cw - 24 - (int)(26 * dpi), rowH, T(L"记住我的选择", L"Remember my choice"), g_sf13, C_WHITE);
        }
        py += p2h + 10;
        // 面板 3：键盘布局
        int p3h = 8 + 20 + 4 + comboH + 6 + rowH + 4 + rowH + 8;
        DrawPanel(dc, x0, py, cw, p3h);
        {
            int ix = x0 + panelPad, iy = py + 8;
            DrawTextL(dc, ix, iy, cw - 24, (int)(20 * dpi), T(L"键盘布局", L"Keyboard Layout"), g_sf13b, C_DIM);
            iy += (int)(20 * dpi) + 4;
            int comboY = iy;
            DrawCombo(dc, ix, comboY, comboW, comboH, g_lang ? g_layoutNamesEn[g_layoutMode] : g_layoutNames[g_layoutMode], g_dropLayout, g_sHov == S_HIT_LAYOUT_DROP);
            iy += comboH + 6;
            DrawCheck(dc, ix, iy + (int)(2 * dpi), (int)(16 * dpi), g_showFKeys);
            DrawTextL(dc, ix + (int)(26 * dpi), iy, cw - 24 - (int)(26 * dpi), rowH, T(L"顶部显示 F1~F12 键", L"Show F1~F12 row"), g_sf13, C_WHITE);
            iy += rowH + (int)(4 * dpi);
            DrawCheck(dc, ix, iy + (int)(2 * dpi), (int)(16 * dpi), g_shiftSymbols);
            DrawTextL(dc, ix + (int)(26 * dpi), iy, cw - 24 - (int)(26 * dpi), rowH, T(L"按 Shift 时显示特殊符号（否则显示数字）", L"Show symbols on Shift (else numbers)"), g_sf13, C_WHITE);
            if (g_dropLayout) {
                const wchar_t* names[3];
                for (int i = 0; i < 3; i++) names[i] = g_lang ? g_layoutNamesEn[i] : g_layoutNames[i];
                DrawComboList(dc, ix, comboY + comboH + 2, comboW, comboH, names, 3, g_layoutMode, g_dropLayoutHov);
            }
        }
        py += p3h + 10;
        // 面板 4：自动隐藏延迟
        int p4h = 8 + 20 + 4 + comboH + 8;
        DrawPanel(dc, x0, py, cw, p4h);
        {
            int ix = x0 + panelPad, iy = py + 8;
            DrawTextL(dc, ix, iy, cw - 24, (int)(20 * dpi), T(L"自动隐藏延迟", L"Auto-hide Delay"), g_sf13b, C_DIM);
            iy += (int)(20 * dpi) + 4;
            int comboY = iy;
            DrawCombo(dc, ix, comboY, comboW, comboH, HideDelayName(), g_dropHideDelay, g_sHov == S_HIT_HIDEDELAY_DROP);
            if (g_dropHideDelay) {
                const wchar_t* names[6];
                for (int i = 0; i < 6; i++) names[i] = g_lang ? g_hideDelayNamesEn[i] : g_hideDelayNames[i];
                DrawComboList(dc, ix, comboY + comboH + 2, comboW, comboH, names, 6, HideDelayIndex(), g_dropHideDelayHov);
            }
        }
        py += p4h + 10;
        // 面板 5：语言
        int p5h = 8 + 20 + 4 + comboH + 8;
        DrawPanel(dc, x0, py, cw, p5h);
        {
            int ix = x0 + panelPad, iy = py + 8;
            DrawTextL(dc, ix, iy, cw - 24, (int)(20 * dpi), T(L"语言", L"Language"), g_sf13b, C_DIM);
            iy += (int)(20 * dpi) + 4;
            int comboY = iy;
            DrawCombo(dc, ix, comboY, comboW, comboH, g_lang ? g_langNamesEn[g_lang] : g_langNames[g_lang], g_dropLang, g_sHov == S_HIT_LANG_DROP);
            if (g_dropLang) {
                const wchar_t* names[2];
                for (int i = 0; i < 2; i++) names[i] = g_lang ? g_langNamesEn[i] : g_langNames[i];
                DrawComboList(dc, ix, comboY + comboH + 2, comboW, comboH, names, 2, g_lang, g_dropLangHov);
            }
        }
        py += p5h;
    } else if (g_sTab == 1) {
        // ===== 主题：圆角面板 ======
        int py = y;
        int ph = 8 + 20 + 4 + comboH + 6 + rowH + 8;
        DrawPanel(dc, x0, py, cw, ph);
        {
            int ix = x0 + panelPad, iy = py + 8;
            DrawTextL(dc, ix, iy, cw - 24, (int)(20 * dpi), T(L"主题模式", L"Theme Mode"), g_sf13b, C_DIM);
            iy += (int)(20 * dpi) + 4;
            int comboY = iy;
            DrawCombo(dc, ix, comboY, comboW, comboH, g_lang ? g_themeNamesEn[g_themeMode] : g_themeNames[g_themeMode], g_dropTheme, g_sHov == S_HIT_THEME_DROP);
            iy += comboH + 6;
            DrawCheck(dc, ix, iy + (int)(2 * dpi), (int)(16 * dpi), g_wallpaperAccent);
            DrawTextL(dc, ix + (int)(26 * dpi), iy, cw - 24 - (int)(26 * dpi), rowH, T(L"高亮按钮跟随壁纸强调色", L"Highlight follows wallpaper accent"), g_sf13, C_WHITE);
            if (g_dropTheme) {
                const wchar_t* names[3];
                for (int i = 0; i < 3; i++) names[i] = g_lang ? g_themeNamesEn[i] : g_themeNames[i];
                DrawComboList(dc, ix, comboY + comboH + 2, comboW, comboH, names, 3, g_themeMode, g_dropThemeHov);
            }
        }
        py += ph + 10;
        // 面板 2：高亮颜色（默认 / 自定义 + #HEX 输入）
        int p2h = 8 + 20 + 4 + comboH + 6 + (int)(26 * dpi) + 8;
        DrawPanel(dc, x0, py, cw, p2h);
        {
            int ix = x0 + panelPad, iy = py + 8;
            DrawTextL(dc, ix, iy, cw - 24, (int)(20 * dpi), T(L"高亮颜色", L"Highlight Color"), g_sf13b, C_DIM);
            iy += (int)(20 * dpi) + 4;
            int comboY = iy;
            DrawCombo(dc, ix, comboY, comboW, comboH, g_lang ? g_hlModeNamesEn[g_hlMode] : g_hlModeNames[g_hlMode], g_dropHl, g_sHov == S_HIT_HL_DROP);
            iy += comboH + 6;
            if (g_hlMode == 1) {
                int inputH = (int)(24 * dpi), hexW = (int)(96 * dpi), sw = (int)(20 * dpi);
                DrawRoundRect(dc, ix, iy, hexW, inputH, g_hlEditFocus ? C_HOVER : C_DARK, C_DIM, 6);
                wchar_t hexbuf[8];
                if (g_hlEditFocus) wcscpy(hexbuf, g_hlEditBuf);
                else HexFromBgr(g_hlColor, hexbuf);
                DrawTextL(dc, ix + 8, iy, hexW - 16, inputH, hexbuf, g_sf13, C_WHITE);
                DrawRoundRect(dc, ix + hexW + (int)(8 * dpi), iy + (inputH - sw) / 2, sw, sw, (DWORD)g_hlColor, C_KEY_BORDER, 4);
            }
            if (g_dropHl) {
                const wchar_t* names[2];
                for (int i = 0; i < 2; i++) names[i] = g_lang ? g_hlModeNamesEn[i] : g_hlModeNames[i];
                DrawComboList(dc, ix, comboY + comboH + 2, comboW, comboH, names, 2, g_hlMode, g_dropHlHov);
            }
        }
    } else {
        // ===== 关于：Logo + 名称 + 版本(架构) + 底部项目地址 ======
        int logo = (int)(72 * dpi);
        HICON hIcon = LoadMainIcon(logo);
        if (hIcon) {
            DrawIconEx(dc, x0 + (cw - logo) / 2, y, hIcon, logo, logo, 0, NULL, DI_NORMAL);
            DestroyIcon(hIcon);
        }
        y += logo + (int)(22 * dpi);
        // 项目名称（居中）
        DrawTextC(dc, x0, y, cw, (int)(26 * dpi), T(L"HKeyboard 轻键", L"HKeyboard"), g_sf14b, C_WHITE);
        y += (int)(34 * dpi);
        // 版本号（架构，居中，小一号）
        wchar_t ver[64];
        swprintf(ver, 64, T(L"版本：v%hs (%ls)", L"Version: v%hs (%ls)"), VER_FILEVERSION_STR, ArchName());
        DrawTextC(dc, x0, y, cw, (int)(18 * dpi), ver, g_sf12, C_WHITE);
        // 底部项目地址（超链接，单独一行避免超宽裁剪）
        const wchar_t* urlText = L"https://github.com/PanDaDaTech/Hydrogen-Keyboard";
        int uy = H - (int)(56 * dpi);
        DrawTextL(dc, x0, uy, cw, (int)(18 * dpi), T(L"项目地址", L"Project URL"), g_sf12, C_DIM);
        DrawTextL(dc, x0, uy + (int)(20 * dpi), cw, (int)(18 * dpi), urlText, g_sf12, C_HOT);
        if (g_sHov == S_HIT_URL) {
            SIZE sz;
            HFONT of = (HFONT)SelectObject(dc, g_sf12);
            GetTextExtentPoint32W(dc, urlText, (int)wcslen(urlText), &sz);
            SelectObject(dc, of);
            HPEN pen2 = CreatePen(PS_SOLID, 1, C_HOT);
            HPEN op2 = (HPEN)SelectObject(dc, pen2);
            MoveToEx(dc, x0, uy + (int)(34 * dpi), NULL);
            LineTo(dc, x0 + sz.cx, uy + (int)(34 * dpi));
            SelectObject(dc, op2); DeleteObject(pen2);
        }
    }
}

static int SettingsHitTest(HWND hWnd, int x, int y) {
    RECT rc; GetClientRect(hWnd, &rc);
    int W = rc.right, H = rc.bottom;
    double dpi = GetSystemDpiScale();
    int hdr = (int)(40 * dpi);
    int bw = (int)(26 * dpi), bh = hdr - (int)(12 * dpi);
    int bx = W - bw - 8, by = (hdr - bh) / 2;
    if (x >= bx && x < bx + bw && y >= by && y < by + bh) return S_HIT_CLOSE;
    // 左侧 Tab（纵向），关于在最底
    int tabX = 12, tabY = hdr + 12, tabW = (int)(110 * dpi), tabH = (int)(38 * dpi), tabGap = (int)(8 * dpi);
    for (int i = 0; i < 3; i++) {
        if (x >= tabX && x < tabX + tabW && y >= tabY && y < tabY + tabH) return S_HIT_TAB0 + i;
        tabY += tabH + tabGap;
    }
    int x0 = tabX + tabW + 16, yy = hdr + 16, cw = W - x0 - 16;
    int rowH = (int)(26 * dpi);
    int panelPad = 12, comboW = (int)(150 * dpi), comboH = (int)(24 * dpi);
    if (g_sTab == 0) {
        int py = yy;
        int p1h = 8 + 20 + 4 + rowH + 8;
        {
            int ix = x0 + panelPad, iy = py + 8 + 20 + 4;
            if (x >= ix && x < ix + cw - 24 && y >= iy && y < iy + rowH) return S_HIT_AUTO;
        }
        py += p1h + 10;
        int p2h = 8 + 20 + 4 + rowH * 3 + 8;
        {
            int ix = x0 + panelPad, iy = py + 8 + 20 + 4;
            if (x >= ix && x < ix + cw - 24 && y >= iy && y < iy + rowH) return S_HIT_CLOSE_DIRECT;
            iy += rowH;
            if (x >= ix && x < ix + cw - 24 && y >= iy && y < iy + rowH) return S_HIT_CLOSE_TRAY;
            iy += rowH;
            if (x >= ix && x < ix + cw - 24 && y >= iy && y < iy + rowH) return S_HIT_REMEMBER;
        }
        py += p2h + 10;
        int p3h = 8 + 20 + 4 + comboH + 6 + rowH + 4 + rowH + 8;
        {
            int ix = x0 + panelPad, iy = py + 8 + 20 + 4;
            int comboY = iy;
            if (g_dropLayout) {
                int ly = comboY + comboH + 2;
                for (int i = 0; i < 3; i++) {
                    if (x >= ix && x < ix + comboW && y >= ly && y < ly + comboH) return S_HIT_LAYOUT_OPT0 + i;
                    ly += comboH;
                }
            }
            if (x >= ix && x < ix + comboW && y >= comboY && y < comboY + comboH) return S_HIT_LAYOUT_DROP;
            iy += comboH + 6;
            if (x >= ix && x < ix + cw - 24 && y >= iy && y < iy + rowH) return S_HIT_FKEYS;
            iy += rowH + (int)(4 * dpi);
            if (x >= ix && x < ix + cw - 24 && y >= iy && y < iy + rowH) return S_HIT_SHIFTSYM;
        }
        py += p3h + 10;
        int p4h = 8 + 20 + 4 + comboH + 8;
        {
            int ix = x0 + panelPad, iy = py + 8 + 20 + 4;
            int comboY = iy;
            if (g_dropHideDelay) {
                int ly = comboY + comboH + 2;
                for (int i = 0; i < 6; i++) {
                    if (x >= ix && x < ix + comboW && y >= ly && y < ly + comboH) return S_HIT_HIDEDELAY_OPT0 + i;
                    ly += comboH;
                }
            }
            if (x >= ix && x < ix + comboW && y >= comboY && y < comboY + comboH) return S_HIT_HIDEDELAY_DROP;
        }
        py += p4h + 10;
        int p5h = 8 + 20 + 4 + comboH + 8;
        {
            int ix = x0 + panelPad, iy = py + 8 + 20 + 4;
            int comboY = iy;
            if (g_dropLang) {
                int ly = comboY + comboH + 2;
                for (int i = 0; i < 2; i++) {
                    if (x >= ix && x < ix + comboW && y >= ly && y < ly + comboH) return S_HIT_LANG_OPT0 + i;
                    ly += comboH;
                }
            }
            if (x >= ix && x < ix + comboW && y >= comboY && y < comboY + comboH) return S_HIT_LANG_DROP;
        }
    } else if (g_sTab == 1) {
        int py = yy;
        int ph = 8 + 20 + 4 + comboH + 6 + rowH + 8;
        {
            int ix = x0 + panelPad, iy = py + 8 + 20 + 4;
            int comboY = iy;
            if (g_dropTheme) {
                int ly = comboY + comboH + 2;
                for (int i = 0; i < 3; i++) {
                    if (x >= ix && x < ix + comboW && y >= ly && y < ly + comboH) return S_HIT_THEME_OPT0 + i;
                    ly += comboH;
                }
            }
            if (x >= ix && x < ix + comboW && y >= comboY && y < comboY + comboH) return S_HIT_THEME_DROP;
            iy += comboH + 6;
            if (x >= ix && x < ix + cw - 24 && y >= iy && y < iy + rowH) return S_HIT_WALLPAPER;
        }
        py += ph + 10;
        int p2h = 8 + 20 + 4 + comboH + 6 + (int)(26 * dpi) + 8;
        {
            int ix = x0 + panelPad, iy = py + 8 + 20 + 4;
            int comboY = iy;
            if (g_dropHl) {
                int ly = comboY + comboH + 2;
                for (int i = 0; i < 2; i++) {
                    if (x >= ix && x < ix + comboW && y >= ly && y < ly + comboH) return S_HIT_HL_OPT0 + i;
                    ly += comboH;
                }
            }
            if (x >= ix && x < ix + comboW && y >= comboY && y < comboY + comboH) return S_HIT_HL_DROP;
            if (g_hlMode == 1) {
                iy += comboH + 6;
                int inputH = (int)(24 * dpi), hexW = (int)(96 * dpi);
                if (x >= ix && x < ix + hexW && y >= iy && y < iy + inputH) return S_HIT_HL_BOX;
            }
        }
    } else if (g_sTab == 2) {
        int uy = H - (int)(56 * dpi);
        if (x >= x0 && x < x0 + cw && y >= uy + (int)(20 * dpi) && y < uy + (int)(38 * dpi)) return S_HIT_URL;
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

// 首次启动自动生成 HKeyboard.ini（含默认值），之后按需写入
static void EnsureConfigFile() {
    wchar_t path[MAX_PATH];
    GetConfigPath(path, MAX_PATH);
    if (GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES) return;   // 已存在
    IniSetInt(L"General", L"RememberClose", 0);
    IniSetInt(L"General", L"CloseToTray", 0);
    IniSetInt(L"Window", L"Width", g_ww);
    IniSetInt(L"Window", L"Height", g_wh);
    IniSetInt(L"Theme", L"Mode", 0);
    IniSetInt(L"Theme", L"Wallpaper", 0);
    IniSetInt(L"Keyboard", L"Layout", 0);
    IniSetInt(L"Keyboard", L"FKeys", 0);
    IniSetInt(L"General", L"ShiftSymbols", 1);
    IniSetInt(L"General", L"HideDelay", 500);
    IniSetInt(L"General", L"Language", 0);
    IniSetInt(L"General", L"HighlightMode", 0);
    IniSetInt(L"General", L"HighlightColor", 0xD47800);
}

// 读取上次的窗口大小 / 主题 / 关闭行为
static void LoadConfig() {
    g_rememberClose = (IniGetInt(L"General", L"RememberClose", 0) != 0);
    if (g_rememberClose)
        g_closeToTray = (IniGetInt(L"General", L"CloseToTray", 0) != 0);
    int w = IniGetInt(L"Window", L"Width", 0);
    int h = IniGetInt(L"Window", L"Height", 0);
    if (w >= 300 && h >= 150) { g_ww = w; g_wh = h; }   // 上次调整过的窗口大小
    int tm = IniGetInt(L"Theme", L"Mode", -1);
    if (tm >= 0 && tm <= 2) g_themeMode = tm;
    g_wallpaperAccent = (IniGetInt(L"Theme", L"Wallpaper", 0) != 0);
    g_layoutMode = IniGetInt(L"Keyboard", L"Layout", 0);
    if (g_layoutMode < 0 || g_layoutMode > 2) g_layoutMode = 0;
    g_showFKeys = (IniGetInt(L"Keyboard", L"FKeys", 0) != 0);
    g_shiftSymbols = (IniGetInt(L"General", L"ShiftSymbols", 1) != 0);
    g_hideDelayMs = IniGetInt(L"General", L"HideDelay", 500);
    if (g_hideDelayMs < 0 || g_hideDelayMs > 5000) g_hideDelayMs = 500;
    g_lang = IniGetInt(L"General", L"Language", 0);
    if (g_lang < 0 || g_lang > 1) g_lang = 0;
    g_hlMode = IniGetInt(L"General", L"HighlightMode", 0);
    if (g_hlMode < 0 || g_hlMode > 1) g_hlMode = 0;
    g_hlColor = IniGetInt(L"General", L"HighlightColor", 0xD47800);
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
}

// 持久化键盘布局设置
static void SaveLayoutConfig() {
    IniSetInt(L"Keyboard", L"Layout", g_layoutMode);
    IniSetInt(L"Keyboard", L"FKeys", g_showFKeys ? 1 : 0);
}

// 应用键盘布局：保存设置、重建按键并调整窗口默认大小
static void ApplyKeyboardLayout() {
    SaveLayoutConfig();
    InitWindowSizeForDpi();
    if (g_hWnd && IsWindow(g_hWnd)) {
        RecreateFontsAndLayout();
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

static void SettingsApplyHit(HWND hWnd, int hit) {
    BOOL themeChanged = FALSE;
    BOOL layoutChanged = FALSE;
    switch (hit) {
    case S_HIT_AUTO: g_af = !g_af; break;
    case S_HIT_CLOSE_DIRECT: g_closeToTray = FALSE; if (g_rememberClose) SaveCloseSettings(); break;
    case S_HIT_CLOSE_TRAY:   g_closeToTray = TRUE;  if (g_rememberClose) SaveCloseSettings(); break;
    case S_HIT_REMEMBER:     g_rememberClose = !g_rememberClose; SaveCloseSettings(); break;
    case S_HIT_LAYOUT_DROP:
        g_dropLayout = !g_dropLayout;
        if (g_dropLayout) { g_dropTheme = FALSE; g_dropHideDelay = FALSE; g_dropLang = FALSE; g_dropHl = FALSE; g_dropLayoutHov = -1; }
        break;
    case S_HIT_LAYOUT_OPT0:
    case S_HIT_LAYOUT_OPT1:
    case S_HIT_LAYOUT_OPT2:
        g_layoutMode = hit - S_HIT_LAYOUT_OPT0;
        g_dropLayout = FALSE;
        layoutChanged = TRUE;
        break;
    case S_HIT_FKEYS: g_showFKeys = !g_showFKeys; layoutChanged = TRUE; break;
    case S_HIT_SHIFTSYM:
        g_shiftSymbols = !g_shiftSymbols;
        IniSetInt(L"General", L"ShiftSymbols", g_shiftSymbols ? 1 : 0);
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
        break;
    case S_HIT_THEME_DROP:
        g_dropTheme = !g_dropTheme;
        if (g_dropTheme) { g_dropLayout = FALSE; g_dropHideDelay = FALSE; g_dropLang = FALSE; g_dropHl = FALSE; g_dropThemeHov = -1; }
        break;
    case S_HIT_THEME_OPT0:
    case S_HIT_THEME_OPT1:
    case S_HIT_THEME_OPT2:
        if (g_themeMode != hit - S_HIT_THEME_OPT0) { g_themeMode = hit - S_HIT_THEME_OPT0; themeChanged = TRUE; }
        g_dropTheme = FALSE;
        break;
    case S_HIT_WALLPAPER: g_wallpaperAccent = !g_wallpaperAccent; themeChanged = TRUE; break;
    case S_HIT_HIDEDELAY_DROP:
        g_dropHideDelay = !g_dropHideDelay;
        if (g_dropHideDelay) { g_dropTheme = FALSE; g_dropLayout = FALSE; g_dropLang = FALSE; g_dropHl = FALSE; g_dropHideDelayHov = -1; }
        break;
    case S_HIT_HIDEDELAY_OPT0:
    case S_HIT_HIDEDELAY_OPT0 + 1:
    case S_HIT_HIDEDELAY_OPT0 + 2:
    case S_HIT_HIDEDELAY_OPT0 + 3:
    case S_HIT_HIDEDELAY_OPT0 + 4:
    case S_HIT_HIDEDELAY_OPT0 + 5:
        g_hideDelayMs = g_hideDelayValues[hit - S_HIT_HIDEDELAY_OPT0];
        g_dropHideDelay = FALSE;
        IniSetInt(L"General", L"HideDelay", g_hideDelayMs);
        break;
    case S_HIT_LANG_DROP:
        g_dropLang = !g_dropLang;
        if (g_dropLang) { g_dropTheme = FALSE; g_dropLayout = FALSE; g_dropHideDelay = FALSE; g_dropHl = FALSE; g_dropLangHov = -1; }
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
        if (g_dropHl) { g_dropTheme = FALSE; g_dropLayout = FALSE; g_dropHideDelay = FALSE; g_dropLang = FALSE; g_dropHlHov = -1; }
        break;
    case S_HIT_HL_OPT0:
    case S_HIT_HL_OPT1:
        g_hlMode = hit - S_HIT_HL_OPT0;
        g_dropHl = FALSE;
        if (g_hlMode == 0) g_hlEditFocus = FALSE;
        ApplyTheme();
        IniSetInt(L"General", L"HighlightMode", g_hlMode);
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
        break;
    case S_HIT_HL_BOX:
        if (g_hlMode == 1) {
            if (g_hlEditFocus) {
                CommitHexEdit(hWnd);
            } else {
                g_hlEditFocus = TRUE;
                HexFromBgr(g_hlColor, g_hlEditBuf);
                SetFocus(hWnd);
            }
        }
        break;
    case S_HIT_URL:
        ShellExecuteW(NULL, L"open", L"https://github.com/PanDaDaTech/Hydrogen-Keyboard", NULL, NULL, SW_SHOWNORMAL);
        break;
    default: return;
    }
    if (themeChanged) {
        ApplyTheme();                                   // 立即换肤
        SaveThemeConfig();                              // 持久化主题选择
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
    }
    if (layoutChanged) ApplyKeyboardLayout();           // 应用并保存布局
    InvalidateRect(hWnd, NULL, TRUE);                   // 设置页立即刷新
}

static void SettingsOnClick(HWND hWnd, int x, int y) {
    int hit = SettingsHitTest(hWnd, x, y);
    if (g_hlEditFocus && hit != S_HIT_HL_BOX) CommitHexEdit(hWnd);   // 点击其它位置时提交 HEX 编辑
    if (hit == S_HIT_CLOSE) { DestroyWindow(hWnd); return; }
    if (hit >= S_HIT_TAB0 && hit <= S_HIT_TAB2) {
        g_sTab = hit - S_HIT_TAB0;
        g_dropTheme = FALSE;
        g_dropLayout = FALSE;
        g_dropHideDelay = FALSE;
        g_dropLang = FALSE;
        g_dropHl = FALSE;
        InvalidateRect(hWnd, NULL, TRUE);
        return;
    }
    if (hit != S_HIT_NONE) {
        SettingsApplyHit(hWnd, hit);
    } else if (g_dropTheme || g_dropLayout || g_dropHideDelay || g_dropLang || g_dropHl) {
        // 点击空白处关闭下拉
        g_dropTheme = FALSE;
        g_dropLayout = FALSE;
        g_dropHideDelay = FALSE;
        g_dropLang = FALSE;
        g_dropHl = FALSE;
        InvalidateRect(hWnd, NULL, TRUE);
    }
}

static LRESULT CALLBACK SettingsWndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
        Fill(mem, 0, 0, rc.right, rc.bottom, C_BG);
        SettingsDraw(mem, hWnd);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hWnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    case WM_LBUTTONDBLCLK:
        SettingsOnClick(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        return 0;
    case WM_MOUSEMOVE: {
        if (!g_sTracking) { TRACKMOUSEEVENT tme = {sizeof(tme), TME_LEAVE, hWnd, 0}; TrackMouseEvent(&tme); g_sTracking = TRUE; }
        int hov = SettingsHitTest(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (hov != g_sHov) { g_sHov = hov; InvalidateRect(hWnd, NULL, TRUE); }
        int thov = (hov >= S_HIT_THEME_OPT0 && hov <= S_HIT_THEME_OPT2) ? hov - S_HIT_THEME_OPT0 : -1;
        int lhov = (hov >= S_HIT_LAYOUT_OPT0 && hov <= S_HIT_LAYOUT_OPT2) ? hov - S_HIT_LAYOUT_OPT0 : -1;
        int hhov = (hov >= S_HIT_HIDEDELAY_OPT0 && hov <= S_HIT_HIDEDELAY_OPT0 + 5) ? hov - S_HIT_HIDEDELAY_OPT0 : -1;
        int langov = (hov >= S_HIT_LANG_OPT0 && hov <= S_HIT_LANG_OPT1) ? hov - S_HIT_LANG_OPT0 : -1;
        int hlov = (hov >= S_HIT_HL_OPT0 && hov <= S_HIT_HL_OPT1) ? hov - S_HIT_HL_OPT0 : -1;
        if (thov != g_dropThemeHov || lhov != g_dropLayoutHov || hhov != g_dropHideDelayHov ||
            langov != g_dropLangHov || hlov != g_dropHlHov) {
            g_dropThemeHov = thov;
            g_dropLayoutHov = lhov;
            g_dropHideDelayHov = hhov;
            g_dropLangHov = langov;
            g_dropHlHov = hlov;
            InvalidateRect(hWnd, NULL, TRUE);
        }
        return 0;
    }
    case WM_SETCURSOR: {
        POINT pt;
        GetCursorPos(&pt);
        ScreenToClient(hWnd, &pt);
        if (g_sTab == 2 && SettingsHitTest(hWnd, pt.x, pt.y) == S_HIT_URL) {
            SetCursor(LoadCursor(NULL, IDC_HAND));
            return TRUE;
        }
        break;
    }
    case WM_MOUSELEAVE:
        g_sTracking = FALSE;
        if (g_sHov != -1) { g_sHov = -1; InvalidateRect(hWnd, NULL, TRUE); }
        return 0;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            if (g_hlEditFocus) { g_hlEditFocus = FALSE; InvalidateRect(hWnd, NULL, TRUE); }
            else DestroyWindow(hWnd);
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
        int hdr = (int)(40 * GetSystemDpiScale());
        if (pt.y >= 0 && pt.y < hdr) {
            if (SettingsHitTest(hWnd, pt.x, pt.y) != S_HIT_CLOSE) return HTCAPTION;
        }
        return HTCLIENT;
    }
    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY:
        g_settingsHwnd = NULL;
        g_sHov = -1;
        g_sTracking = FALSE;
        g_dropTheme = FALSE;
        g_dropLayout = FALSE;
        g_dropHideDelay = FALSE;
        g_dropLang = FALSE;
        g_dropHl = FALSE;
        g_hlEditFocus = FALSE;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, w, l);
}

static void OpenSettingsTab(int tab) {
    g_sTab = (tab >= 0 && tab <= 2) ? tab : 0;   // 0=常规 1=主题 2=关于
    if (g_settingsHwnd && IsWindow(g_settingsHwnd)) {
        ShowWindow(g_settingsHwnd, SW_SHOW);
        SetForegroundWindow(g_settingsHwnd);
        InvalidateRect(g_settingsHwnd, NULL, TRUE);   // 切到指定 Tab 后刷新
        return;
    }
    double dpi = GetSystemDpiScale();
    int w = (int)(520 * dpi), h = (int)(560 * dpi);
    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    int x = work.left + ((work.right - work.left) - w) / 2;
    int y = work.top + ((work.bottom - work.top) - h) / 2;
    g_settingsHwnd = CreateWindowExW(WS_EX_TOPMOST, L"HKeyboardSettings", T(L"设置", L"Settings"), WS_POPUP,
        x, y, w, h, NULL, NULL, g_hInst, NULL);
    if (g_settingsHwnd) {
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

static HWND g_closePromptHwnd = 0;
static int  g_pChoice = 0;       // 0=直接退出 1=隐藏到托盘
static BOOL g_pRemember = FALSE;
static int  g_pHov = -1;
static BOOL g_pTracking = FALSE;

static void PromptDraw(HDC dc, HWND hWnd) {
    RECT rc; GetClientRect(hWnd, &rc);
    int W = rc.right, H = rc.bottom;
    double dpi = GetSystemDpiScale();
    int hdr = (int)(36 * dpi);
    Fill(dc, 0, 0, W, hdr, C_HDR);
    DrawTextL(dc, 14, 0, W - 90, hdr, T(L"关闭轻键", L"Close HKeyboard"), g_sf13, C_WHITE);
    int bw = (int)(26 * dpi), bh = hdr - (int)(12 * dpi);
    int bx = W - bw - 8, by = (hdr - bh) / 2;
    DrawRoundRect(dc, bx, by, bw, bh, (g_pHov == P_HIT_CLOSE) ? C_HOVER : C_KEY, C_KEY_BORDER, 6);
    int mx = bx + bw / 2, my = by + bh / 2, r = (int)(5 * dpi);
    HPEN pen = CreatePen(PS_SOLID, 2, C_DIM); HPEN op = (HPEN)SelectObject(dc, pen);
    MoveToEx(dc, mx - r, my - r, NULL); LineTo(dc, mx + r, my + r);
    MoveToEx(dc, mx + r, my - r, NULL); LineTo(dc, mx - r, my + r);
    SelectObject(dc, op); DeleteObject(pen);

    int x0 = 20, y = hdr + 12, cw = W - 40;
    int rowH = (int)(24 * dpi);
    DrawTextL(dc, x0, y, cw, (int)(20 * dpi), T(L"请选择关闭方式：", L"Choose how to close:"), g_sf13b, C_DIM); y += (int)(22 * dpi);
    DrawRadio(dc, x0 + (int)(8 * dpi), y + rowH / 2, (int)(7 * dpi), g_pChoice == 0);
    DrawTextL(dc, x0 + (int)(26 * dpi), y, cw - (int)(26 * dpi), rowH, T(L"直接退出程序", L"Exit program directly"), g_sf13, C_WHITE);
    y += rowH;
    DrawRadio(dc, x0 + (int)(8 * dpi), y + rowH / 2, (int)(7 * dpi), g_pChoice == 1);
    DrawTextL(dc, x0 + (int)(26 * dpi), y, cw - (int)(26 * dpi), rowH, T(L"隐藏到系统托盘", L"Hide to system tray"), g_sf13, C_WHITE);
    y += rowH + (int)(4 * dpi);
    DrawCheck(dc, x0, y + (int)(2 * dpi), (int)(16 * dpi), g_pRemember);
    DrawTextL(dc, x0 + (int)(26 * dpi), y, cw - (int)(26 * dpi), rowH, T(L"记住我的选择", L"Remember my choice"), g_sf13, C_WHITE);
    y += rowH + (int)(8 * dpi);
    int bw2 = (int)(84 * dpi), bh2 = (int)(28 * dpi);
    int bxCancel = W - 20 - bw2;                    // 按钮右对齐
    int bxOk = bxCancel - (int)(12 * dpi) - bw2;
    DrawRoundRect(dc, bxOk, y, bw2, bh2, (g_pHov == P_HIT_OK) ? C_HOVER : C_HOT, C_KEY_BORDER, 8);
    DrawTextC(dc, bxOk, y, bw2, bh2, T(L"确定", L"OK"), g_sf13b, IsLightColor(C_HOT) ? 0x1A1A1A : C_WHITE);
    DrawRoundRect(dc, bxCancel, y, bw2, bh2, (g_pHov == P_HIT_CANCEL) ? C_HOVER : C_KEY, C_KEY_BORDER, 8);
    DrawTextC(dc, bxCancel, y, bw2, bh2, T(L"取消", L"Cancel"), g_sf13b, C_WHITE);
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
        DestroyWindow(hWnd);
        return;
    case P_HIT_DIRECT:   g_pChoice = 0; break;
    case P_HIT_TRAY:     g_pChoice = 1; break;
    case P_HIT_REMEMBER: g_pRemember = !g_pRemember; break;
    case P_HIT_OK: {
        g_closeToTray = (g_pChoice == 1);
        g_rememberClose = g_pRemember;
        SaveCloseSettings();          // 持久化选择与“记住我的选择”标志
        DestroyWindow(hWnd);
        if (g_closeToTray) {
            g_manualHide = TRUE;      // 显式隐藏到托盘后不再自动弹出
            ShowKB(FALSE, FALSE);
        } else if (g_hWnd && IsWindow(g_hWnd)) {
            DestroyWindow(g_hWnd);
        }
        return;
    }
    default: return;
    }
    InvalidateRect(hWnd, NULL, TRUE);
}

static LRESULT CALLBACK PromptWndProc(HWND hWnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, rc.right, rc.bottom);
        HBITMAP old = (HBITMAP)SelectObject(mem, bmp);
        Fill(mem, 0, 0, rc.right, rc.bottom, C_BG);
        PromptDraw(mem, hWnd);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
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
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) { DestroyWindow(hWnd); return 0; }
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
    case WM_CLOSE: DestroyWindow(hWnd); return 0;
    case WM_DESTROY:
        g_closePromptHwnd = NULL;
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
        ShowWindow(g_closePromptHwnd, SW_SHOW);
        SetForegroundWindow(g_closePromptHwnd);
    }
}

static void ShowMenu(HWND hWnd) {
    POINT pt; GetCursorPos(&pt);
    HMENU m = CreatePopupMenu();
    // 隐藏/收起交给标题栏“最小化”按钮，菜单只保留隐藏状态下的“显示轻键”
    if (!g_vis) {
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
        InvalidateRect(hWnd, 0, TRUE);
    } else if (id == ID_MENU_THEME + 1) {
        g_themeMode = 0; ApplyTheme(); SaveThemeConfig(); InvalidateRect(hWnd, 0, TRUE);
    } else if (id == ID_MENU_THEME + 2) {
        g_themeMode = 1; ApplyTheme(); SaveThemeConfig(); InvalidateRect(hWnd, 0, TRUE);
    } else if (id == ID_MENU_THEME + 3) {
        g_themeMode = 2; ApplyTheme(); SaveThemeConfig(); InvalidateRect(hWnd, 0, TRUE);
    } else if (id == ID_MENU_SETTINGS) {
        OpenSettings();
    } else if (id == ID_MENU_ABOUT) {
        OpenSettingsTab(2);   // 跳转到设置“关于”Tab
    } else if (id == ID_MENU_EXIT) {
        DestroyWindow(hWnd);
    }
}

static BOOL IsInputControl(HWND hw) {
    if (!hw || !IsWindow(hw)) return FALSE;
    char buf[128] = {0};
    GetClassNameA(hw, buf, 128);

    if (strstr(buf, "Shell_") || strstr(buf, "Progman") || strstr(buf, "WorkerW") ||
        strstr(buf, "Taskbar") || strstr(buf, "TrayNotify") || strstr(buf, "MSTaskSwWClass"))
        return FALSE;

    if (strstr(buf, "Edit") || strstr(buf, "Rich") || strstr(buf, "Scintilla") ||
        strstr(buf, "TextBox") || strstr(buf, "Console") || strstr(buf, "Omnibox") ||
        strstr(buf, "Search") || strstr(buf, "InputSite") || strstr(buf, "TXGuiFoundation") ||
        strstr(buf, "Chrome_") || strstr(buf, "Qt5") || strstr(buf, "Afx"))
        return TRUE;

    return FALSE;
}

static void CALLBACK WinEventProc(HWINEVENTHOOK hook, DWORD event, HWND hwnd, LONG idObject, LONG idChild, DWORD dwEventThread, DWORD dwmsEventTime) {
    if (!g_af || !g_hWnd) return;
    if (!hwnd || hwnd == g_hWnd) return;

    if (event == EVENT_OBJECT_FOCUS || event == EVENT_SYSTEM_FOREGROUND || (event == EVENT_OBJECT_SHOW && idObject == -8)) {
        HWND fg = GetForegroundWindow();
        if (!fg || fg == g_hWnd) return;

        DWORD tid = GetWindowThreadProcessId(fg, NULL);
        GUITHREADINFO gi = {sizeof(gi)};
        BOOL isText = FALSE;

        if (GetGUIThreadInfo(tid, &gi)) {
            HWND fh = gi.hwndFocus ? gi.hwndFocus : fg;
            if ((gi.flags & GUI_CARETBLINKING) != 0 || gi.hwndCaret != NULL || IsInputControl(fh) || IsInputControl(fg)) {
                isText = TRUE;
            }
        }

        if (isText) {
            g_lastNonInput = 0;   // 在输入框上，重置自动隐藏计时
            if (!g_vis) {
                if (GetTickCount() - g_lht >= AUTO_POP_COOLDOWN_MS) {
                    PostMessage(g_hWnd, WM_FOCUS_EVENT, TRUE, 0);
                }
            }
        } else if (!isText && g_vis && !g_manualShow) {
            g_lastNonInput = GetTickCount();   // 记录离焦时刻（自动隐藏延迟）
            if (g_hideDelayMs <= 50) PostMessage(g_hWnd, WM_FOCUS_EVENT, FALSE, 0);
        }
    }
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
        case HDR_MIN: g_hideUntil = GetTickCount() + 3000; ShowKB(FALSE, FALSE); break;
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
    int nk = HitKey(x, y);
    if (nk != g_hk) {
        g_hk = nk;
        InvalidateRect(hWnd, 0, TRUE);
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
    switch (msg) {
    case WM_CREATE: {
        g_hWnd = hWnd;
        InitWindowSizeForDpi();
        RecreateFontsAndLayout();
        SetWindowLong(hWnd, GWL_EXSTYLE, GetWindowLong(hWnd, GWL_EXSTYLE) | WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST);
        SetLayeredWindowAttributes(hWnd, 0, 255, LWA_ALPHA);

        g_winHook = SetWinEventHook(EVENT_OBJECT_FOCUS, EVENT_OBJECT_SHOW, 0, WinEventProc, 0, 0, WINEVENT_OUTOFCONTEXT);
        // 实体键盘状态监控：安装低级键盘钩子（只监控 Win/Shift/Caps，Fn 预留接口）
        g_kbHook = SetWindowsHookExW(WH_KEYBOARD_LL, PhysKeyHookProc, g_hInst, 0);
        g_cp = (GetKeyState(VK_CAPITAL) & 1) != 0;  // 启动时同步 CapsLock 状态
        SetTimer(hWnd, TIMER_FOCUS, 200, 0);
        return 0;
    }
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_ERASEBKGND: return 1;
    case WM_GETMINMAXINFO: {
        LPMINMAXINFO mmi = (LPMINMAXINFO)l;
        double dpiScale = GetSystemDpiScale();
        mmi->ptMinTrackSize.x = (int)(500 * dpiScale);
        mmi->ptMinTrackSize.y = (int)(200 * dpiScale);
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
            InvalidateRect(hWnd, NULL, TRUE);
        }
        return 0;
    }
    case WM_EXITSIZEMOVE:
        // 记录上次调整过的窗口大小
        IniSetInt(L"Window", L"Width", g_ww);
        IniSetInt(L"Window", L"Height", g_wh);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hWnd, &ps);
        HDC mem = CreateCompatibleDC(dc);
        HBITMAP bmp = CreateCompatibleBitmap(dc, g_ww, g_wh);
        HBITMAP old = (HBITMAP)SelectObject(mem, bmp);

        Fill(mem, 0, 0, g_ww, g_wh, C_BG);
        DrawHeader(mem);
        DrawKeys(mem);

        BitBlt(dc, 0, 0, g_ww, g_wh, mem, 0, 0, SRCCOPY);
        SelectObject(mem, old); DeleteObject(bmp); DeleteDC(mem);
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
        KillTimer(hWnd, TIMER_REPEAT);
        g_repeatKeyIdx = -1;
        InvalidateRect(hWnd, 0, TRUE);
        return 0;
    case WM_MOUSEMOVE: OnMMove(hWnd, GET_X_LPARAM(l), GET_Y_LPARAM(l)); return 0;
    case WM_FOCUS_EVENT:
        if (w) {
            if (g_af && !g_manualHide && GetTickCount() >= g_hideUntil && !g_vis && (GetTickCount() - g_lht >= AUTO_POP_COOLDOWN_MS)) {
                ShowKB(TRUE, FALSE);
            }
        } else {
            HWND fg = GetForegroundWindow();
            if (fg == g_settingsHwnd || fg == g_closePromptHwnd) return 0;   // 设置/提示窗口在前台不收起
            if (g_vis && !g_manualShow && !g_manualHide) {
                POINT pt; GetCursorPos(&pt);
                if (WindowFromPoint(pt) != g_hWnd) ShowKB(FALSE, FALSE);
            }
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
        if (w == TIMER_REPEAT) {
            SetTimer(hWnd, TIMER_REPEAT, 40, NULL);
            if (g_pk >= 0 && g_pk == g_repeatKeyIdx) {
                const KeyDef* k = &g_keys[g_pk];
                DoKeyAction(k);
            } else {
                KillTimer(hWnd, TIMER_REPEAT);
            }
        } else if (w == TIMER_SLIDE) {
            g_slideStep++;
            int ny = g_slideFrom + (g_slideTo - g_slideFrom) * g_slideStep / SLIDE_STEPS;
            RECT work = {0};
            SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
            int sx = work.left + ((work.right - work.left) - g_ww) / 2;
            SetWindowPos(hWnd, HWND_TOPMOST, sx, ny, g_ww, g_wh, SWP_NOACTIVATE);
            if (g_slideStep >= SLIDE_STEPS) {
                KillTimer(hWnd, TIMER_SLIDE);
                g_slideStep = -1;
                SetWindowPos(hWnd, HWND_TOPMOST, sx, g_slideTo, g_ww, g_wh, SWP_NOACTIVATE);
                if (g_slideTo >= work.bottom) {
                    g_vis = FALSE;
                    ShowWindow(hWnd, SW_HIDE);
                }
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

            if (!g_af || GetTickCount() - g_lht < AUTO_POP_COOLDOWN_MS) return 0;

            HWND fg = GetForegroundWindow();
            if (!fg || fg == g_hWnd) return 0;
            if ((g_settingsHwnd && fg == g_settingsHwnd) || (g_closePromptHwnd && fg == g_closePromptHwnd)) return 0;   // 设置页/关闭提示在前台时不自动收起键盘

            DWORD tid = GetWindowThreadProcessId(fg, NULL);
            GUITHREADINFO gi = {sizeof(gi)};
            BOOL hasFocusInput = FALSE;

            if (GetGUIThreadInfo(tid, &gi)) {
                HWND fh = gi.hwndFocus ? gi.hwndFocus : fg;
                if (IsInputControl(fh) || IsInputControl(fg) || gi.hwndCaret != NULL || (gi.flags & GUI_CARETBLINKING) != 0) {
                    hasFocusInput = TRUE;
                }
            }

            if (hasFocusInput) {
                g_lastNonInput = 0;
                if (!g_vis && !g_manualHide && GetTickCount() >= g_hideUntil) {
                    ShowKB(TRUE, FALSE);
                }
            } else {
                if (g_lastNonInput == 0) g_lastNonInput = GetTickCount();
                if (g_vis && (GetTickCount() - g_lastNonInput >= (DWORD)g_hideDelayMs) &&
                    (!g_manualShow || GetTickCount() - g_manualShowTick > MANUAL_HIDE_GRACE_MS)) {
                    POINT pt; GetCursorPos(&pt);
                    if (WindowFromPoint(pt) != g_hWnd) {
                        ShowKB(FALSE, FALSE);
                    }
                }
            }
        }
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case ID_MENU_TOGGLE: ToggleKB(); break;
        case ID_MENU_AUTO: g_af = !g_af; InvalidateRect(hWnd, 0, TRUE); break;
        case ID_MENU_THEME + 1: g_themeMode = 0; ApplyTheme(); SaveThemeConfig(); InvalidateRect(hWnd, 0, TRUE); break;
        case ID_MENU_THEME + 2: g_themeMode = 1; ApplyTheme(); SaveThemeConfig(); InvalidateRect(hWnd, 0, TRUE); break;
        case ID_MENU_THEME + 3: g_themeMode = 2; ApplyTheme(); SaveThemeConfig(); InvalidateRect(hWnd, 0, TRUE); break;
        case ID_MENU_SETTINGS: OpenSettings(); break;
        case ID_MENU_ABOUT: OpenSettingsTab(2); break;
        case ID_MENU_EXIT: DestroyWindow(hWnd); break;
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
        KillTimer(hWnd, TIMER_FOCUS);
        KillTimer(hWnd, TIMER_SLIDE);
        KillTimer(hWnd, TIMER_REPEAT);
        if (g_winHook) { UnhookWinEvent(g_winHook); g_winHook = 0; }
        if (g_kbHook) { UnhookWindowsHookEx(g_kbHook); g_kbHook = 0; }
        if (g_tray) {   // 显式删除托盘图标，避免程序退出后图标残留到鼠标悬停才消失
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            g_tray = FALSE;
        }
        DeleteObject(g_f12); DeleteObject(g_f13); DeleteObject(g_f13b); DeleteObject(g_f14);
        DeleteObject(g_f14b); DeleteObject(g_f16b); DeleteObject(g_f18b);
        DeleteObject(g_sf12); DeleteObject(g_sf13); DeleteObject(g_sf13b); DeleteObject(g_sf14b);
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

int WINAPI WinMain(HINSTANCE hI, HINSTANCE, LPSTR cmd, int) {
    g_hInst = hI;

    HMODULE hUser32 = GetModuleHandleA("user32.dll");
    if (hUser32) {
        typedef BOOL (WINAPI *SetDpiAwareProc)();
        SetDpiAwareProc pSetDPIAware = (SetDpiAwareProc)GetProcAddress(hUser32, "SetProcessDPIAware");
        if (pSetDPIAware) pSetDPIAware();
    }

    LoadEmbeddedFonts();   // 注册内嵌字体（阿里巴巴普惠体精简版），失败自动回退系统字体
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

    if (fNoAuto) g_af = FALSE;
    else if (fAuto) g_af = TRUE;

    g_mutex = CreateMutexW(0, FALSE, L"HKeyboard_Mutex");
    if (g_mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
        CloseHandle(g_mutex);
        HWND ew = FindWindowW(L"HKeyboard", 0);
        if (ew) {
            if (!fHide) ShowWindow(ew, SW_SHOW);
            SetForegroundWindow(ew);
        }
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

    // 配置：首次启动自动生成 HKeyboard.ini；恢复上次的布局 / 窗口大小 / 主题 / 关闭行为
    EnsureConfigFile();
    LoadConfig();
    if (fThemeCli) g_themeMode = fDark ? 1 : (fLight ? 2 : 0);   // 命令行主题优先
    if (fWall) g_wallpaperAccent = TRUE;
    InitWindowSizeForDpi();   // 布局决定默认窗口大小（在 LoadConfig 之后）
    ApplyTheme();

    RECT work = {0};
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
    HWND hWnd = CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TOPMOST,
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
    return (int)msg.wParam;
}
