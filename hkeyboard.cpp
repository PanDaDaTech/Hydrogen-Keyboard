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
// ⚠ 只用到纯 getter（CoCreateInstance / GetFocusedElement / get_CurrentControlType）
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
// -----------------------------------------------------------------------
// ⚠ **正式发布版一律不定义这个宏** —— 上面这些参数与代码一行都不会

// ⚠ 只有日期、不带时分 —— 这是**正式版**该有的样子：用户只需要知道
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
#define TIMER_CARET     8834   // 设置页 HEX 输入框的光标闪烁
// ⚠ 前向声明是必需的：WM_TIMER 的处理在主 WndProc 里（文件靠前）
BOOL g_envTest = FALSE;
int  g_envTestStage = 0;
static void EnvTestInject();

// ⚠ 前向声明的理由同 EnvTestInject：调用点在 RecreateFontsAndLayout（文件中部）
BOOL g_fontDiag = FALSE;
static void FontDiagDump();

// ---- 排查开关：二分「点击按键」时究竟是哪个动作破坏了 IME 组字 -
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
#define ID_MENU_DIAG   10011

// 布局 Tab 命中码（S_HIT_TAB0=常规 TAB1=主题 TAB2=关于，视觉顺序：常规/布局/主题/关于）
#define S_HIT_TABL 5
#define S_HIT_AUTOHIDE 110   // 注意：97~102 已被透明度下拉占用

// ========== Ethereal 主题令牌（单一色相驱动） ==========

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

// ⚠ 「经典」不等于「去掉颜色」：v1.1 就是灰键面 + 强调色实底（见 DrawKeys 的高亮分支）。
static int  g_themeStyle = 0;

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

    if (g_themeStyle == 1) {
        if (!dark) {
            out->pageBg             = RGB(243, 243, 243);
            out->cardBg             = RGB(255, 255, 255);
            out->btnRegularBg       = RGB(232, 232, 232);
            out->btnRegularBgHover  = RGB(224, 224, 224);
            out->btnRegularBgActive = RGB(216, 216, 216);
            out->btnCardBgHover     = RGB(247, 247, 247);
            out->btnPlainBgHover    = RGB(240, 240, 240);
            out->keyOutline         = RGB(214, 214, 214);
            out->keyOutlineHover    = RGB(190, 190, 190);
            out->text               = RGB(26, 26, 26);
            out->textMuted          = RGB(102, 102, 102);
            out->btnContent         = RGB(26, 26, 26);
            out->lineDivider        = RGB(230, 230, 230);
            out->metaDivider        = RGB(214, 214, 214);
        } else {
            out->pageBg             = RGB(31, 31, 31);
            out->cardBg             = RGB(44, 44, 44);
            out->btnRegularBg       = RGB(36, 36, 36);
            out->btnRegularBgHover  = RGB(56, 56, 56);
            out->btnRegularBgActive = RGB(56, 56, 56);
            out->btnCardBgHover     = RGB(51, 51, 51);
            out->btnPlainBgHover    = RGB(42, 42, 42);
            out->keyOutline         = RGB(58, 58, 58);
            out->keyOutlineHover    = RGB(78, 78, 78);
            out->text               = RGB(245, 245, 245);
            out->textMuted          = RGB(160, 160, 160);
            out->btnContent         = RGB(245, 245, 245);
            out->lineDivider        = RGB(51, 51, 51);
            out->metaDivider        = RGB(58, 58, 58);
            out->controlBg          = RGB(58, 58, 58);
            out->floatPanelBg       = RGB(40, 40, 40);
        }
    }
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

static DWORD AbgrToBgr(DWORD val) {
    return (((val >> 16) & 0xFF) << 16) | (val & 0xFF00) | (val & 0xFF);
}

// 旧 DWM（Win7/8 与 Win10+ 的 ColorizationColor）为 ARGB (0xAARRGGBB) 布局
static DWORD ArgbToBgr(DWORD val) {
    return ((val & 0xFF) << 16) | (val & 0xFF00) | ((val >> 16) & 0xFF);
}

static DWORD GetWallpaperAccentBgr() {
    // 读**壁纸本身**求主色调：缩到 16×16 取平均。
    // 原来只读注册表（DWM\AccentColor 等），Win11 上那是"系统强调色"，
    // 换壁纸不变，所以"跟随壁纸"永远取不到壁纸的颜色。
    wchar_t path[MAX_PATH] = {0};
    if (SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, path, 0) && path[0]) {
        Gdiplus::Bitmap src(path, FALSE);
        if (src.GetLastStatus() == Gdiplus::Ok && src.GetWidth() > 0) {
            Gdiplus::Bitmap small(16, 16, PixelFormat32bppARGB);
            {
                Gdiplus::Graphics g(&small);
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBilinear);
                g.DrawImage(&src, 0, 0, 16, 16);
            }
            double r = 0, gg = 0, b = 0, n = 0;
            for (int y = 0; y < 16; y++) {
                for (int x = 0; x < 16; x++) {
                    Gdiplus::Color c;
                    if (small.GetPixel(x, y, &c) != Gdiplus::Ok) continue;
                    if (c.GetA() < 128) continue;
                    r += c.GetR(); gg += c.GetG(); b += c.GetB(); n += 1;
                }
            }
            if (n > 0) {
                int R = (int)(r / n), G = (int)(gg / n), B = (int)(b / n);
                if ((R | G | B) != 0) return RGB(R, G, B);
            }
        }
    }
    // 兜底：纯色壁纸（SPI 返回空）或图片读不了时，退回注册表里的系统强调色。
    HKEY hKey;
    DWORD val = 0, sz = sizeof(val);
    if (RegOpenKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM",
                      0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        RegQueryValueExW(hKey, L"AccentColor", NULL, NULL, (LPBYTE)&val, &sz);
        RegCloseKey(hKey);
    }
    if ((val & 0xFFFFFF) != 0) return AbgrToBgr(val);
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
    if (memcmp(&before, &g_themeBuf, sizeof(HkbTokens)) == 0) return;
    // ⚠ 之前只失效了传进来的那个窗口（调用方传的是设置窗），**主键盘根本没被失效** ——
    //   它只能等下一次别的重绘（鼠标划过、敲键）把新配色带出来，表现就是「切风格要等一会儿才变」。
    //   现在三处一起失效并立即上屏（UPDATENOW），切完就是新的。
    HWND wins[3] = { g_hWnd, g_settingsHwnd, g_closePromptHwnd };
    for (int i = 0; i < 3; i++) {
        if (wins[i] && IsWindow(wins[i]))
            RedrawWindow(wins[i], NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW);
    }
    (void)hWnd;
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
    K_SYM
};

enum KeyAlign { KA_CENTER = 0, KA_LEFT = 1, KA_RIGHT = 2 };

// ⚠ 2026-10-04 新增：键面标签的字号要**按区块统一**，需要知道每个键属于哪一块。
struct KeyDef { int x, y, w, h; short vk; KeyType type; unsigned char symShift;
                unsigned char align; unsigned char block; };

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
static void InvalidateFitTier(void);      // 作废「区块统一字号」的缓存（见 FitUnifiedTier）
static double GetSystemDpiScale();
static void InitWindowSizeForDpi();
static void SendKey(BYTE vk, BOOL sh, BOOL ct, BOOL al, BOOL win = FALSE);
// 只失效某个 vk 对应的那一格键（定义在绘制函数区，OnKeyDown 里点 Num 键要用）
static void InvalidateKeyByVk(HWND hWnd, short vk);
static HWND GetFocusedInputControl();
static void UpdateAutoVisibility();
static BOOL IsTouchDevice();          // 定义在窗口过程附近；自动呼出要按它决定是否启用触屏兜底
// 用户通过托盘 / 热键**主动**把键盘叫出来时，作废"精判收起"备忘（见其定义处）。
// 前向声明在这里是因为 ShowKB 定义在那套 UIA 状态之前。
static void ForgetVerdictHide();
// 导出诊断日志（实现在那套 UIA 状态之后）：托盘右键与设置页「关于」Tab 都调它
static void ExportDiagLog();
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

// ⚠ 用户实测：「刚打开的时候，有时候在没输入状态会触发自动呼出的逻辑
#define AUTOSHOW_WARMUP_MS 1200
static DWORD g_appStartTick = 0;
static WindowMotion g_settingsMotion = {};
static WindowMotion g_promptMotion = {};

// 高精度毫秒时钟：动画进度改用 QueryPerformanceCounter 计算
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
BOOL        g_detectedByChromiumClass = FALSE;
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
#define AUTOSHOW_INPUT_WINDOW_MS 1500
#define AUTOSHOW_INPUT_GRACE_MS 500
static DWORD g_lastClickTick = 0;      // 最后一次真实鼠标左键点击
static POINT g_lastClickPt = {0, 0};   // 它的屏幕坐标
static DWORD g_lastKeyTick = 0;        // 最后一次真实键盘按键
// 最后一次**按下屏幕键盘上的键**的时刻（实体键钩子看不到注入事件，所以单独记）。
// 用途见 UpdateAutoVisibility 的 Chromium 精判：正在输入时不收起。
static DWORD g_lastOwnKeyTick = 0;

// ⚠ 用户要的是「点输入框 / 键盘聚焦 / **网页自动聚焦搜索框**」都弹
static HWND g_lastFg = NULL;
static BOOL g_fgAwaitUserInput = FALSE;
// ⚠ 不要等到 UpdateAutoVisibility 里再 WindowFromPoint：那时窗口可能已经变了
static HWND  g_lastClickTopHwnd = NULL;
static int   g_noInputStreak = 0;
// 失焦开始的时刻（首次越过"连续确认"门槛时记录，输入焦点恢复时清零）。
// 与 AUTOSHOW_INPUT_GRACE_MS 配合，滤掉爆发式抖动 —— 见其定义处说明。
static DWORD g_noInputSinceTick = 0;
BOOL        g_physShift = FALSE;      // 实体 Shift 是否按住（左右任一；仅显示同步，不影响虚拟键逻辑）
// ⚠ 2026-10-10：左右 Shift 必须**分开**记。
BOOL        g_physLShift = FALSE;     // 实体左 Shift 按住
BOOL        g_physRShift = FALSE;     // 实体右 Shift 按住
BOOL        g_physWin = FALSE;        // 实体 Win 是否按住（仅显示同步）
// 实体 NumLock 锁定态（与 g_npLock 同源，都来自 ReadSystemNumLock）
// 一起进帧缓存签名 —— 它俩任一变化都要触发重绘。
BOOL        g_physNum = FALSE;
//   ⚠ 2026-10-10 改：以前它还决定「小键盘数字键发数字键还是发光标键」
static BOOL ReadSystemNumLock() {
    return (GetKeyState(VK_NUMLOCK) & 1) != 0;
}

static BOOL g_npLock = TRUE;            // TRUE = 数字（小键盘）模式
// （原 g_npLockInit「只跟随一次」已删：现在 TIMER 每个 tick 都读回
//   高亮始终等于真实锁定态，不需要再区分「首帧」与「之后」）
BOOL        g_physFn = FALSE;         // 预留接口：Fn 实体键状态（多数键盘不产生按键事件，后续按需扩展）
BOOL        g_af = TRUE;
BOOL        g_afChromiumPrecise = TRUE;
// ⚠ 2026-10-04 语义变更：这个开关原来控制的是"收起后在同一输入框内不自动回弹"
BOOL        g_afAutoHide = TRUE;
static BOOL g_userHidInInput = FALSE;  // 用户刚在输入状态下手动收起（自动收起开启时不回弹）
static ULONG_PTR g_hiddenInputToken = 0; // 手动收起时所在的输入控件标识
static DWORD     g_userHidInTick = 0;    // 手动收起的时刻（防回弹的超时兜底用）

// ===== 临时诊断（-afdiag，定位自动呼出问题用）=====
//   ⚠ 只有带 `-afdiag` 参数启动时才真正工作（此时 g_afLogPath 非空）
static wchar_t g_afLogPath[MAX_PATH] = {0};
static int     g_afLogLines = 0;      // 已写行数（够多就停，别把磁盘写爆）
#define AFLOG_MAX_LINES 4000

// ===== 常驻诊断环形缓冲（供托盘菜单「导出诊断日志」用）=====
//   ⚠ 与 -afdiag **无关**：这段始终在跑（默认每 ~200ms 一条 eval + 所有事件）
#define HKRING_N   1000
#define HKRING_LEN 576
static char g_ring[HKRING_N][HKRING_LEN];
static int  g_ringHead = 0;           // 下一个写入位置
static int  g_ringCount = 0;          // 已累积条数（满后恒为 HKRING_N）
//   ⚠ 存在的唯一理由：本机没编译器，只能靠 CI 出包再回来验
static BOOL g_dbgAcceptInjected = FALSE;
static void DiagRingPush(const char* line) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    _snprintf_s(g_ring[g_ringHead], HKRING_LEN, _TRUNCATE, "%02d:%02d:%02d.%03d %s",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, line);
    g_ringHead = (g_ringHead + 1) % HKRING_N;
    if (g_ringCount < HKRING_N) g_ringCount++;
}

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
static int g_dbgResp = -1;
static int g_dbgAcc = 0;
static int g_dbgAccFound = -1;
static int      g_dbgExtraMs = -1;
static int      g_dbgExtraOk = -1;
static BOOL     g_dbgExtraDue = FALSE;
static DWORD    g_dbgExtraLastTick = 0;
// ⚠ 决定"QQ / 浏览器能不能用 a11y 精判"的关键数据
static int      g_dbgFocusVt = -1;
static LONG     g_dbgFocusRole = -1;
// ⚠ 要验证的假设：「点输入框」会触发 FOCUS 事件、「点空白处」不触发
static DWORD    g_lastFocusEvTick = 0;
static HWND     g_lastFocusEvHwnd = NULL;   // ⚠ 必须是 HWND（x64 下 64 位，DWORD 会截断）
// ⚠ 这是用来验证「程序以为显示着、用户却看不到」这个猜想的
static int g_dbgVisWnd = -1;
static int g_dbgRc[4] = {0, 0, 0, 0};
// ⚠ 用户日志里 SHOW/HIDE 成对交替、且都是 isManual=TRUE —— 最像
static const char* g_dbgShowFrom = "(startup)";
static int g_dbgUiaCt = -1;
//   ⚠ 这一栏是"自动呼出为什么没弹"的唯一直接答案 —— 之前只有一堆输入量
static const char* g_dbgWhy = "-";
// 诊断：两个触屏证据源的即时值（见 TouchFocusEvidence / SysTipJustAppeared）
static int g_dbgEvFoc = 0;
static int g_dbgEvTip = 0;

// ⚠⚠ 用 Win32 API（CreateFileW / WriteFile）写日志，**不用 CRT 流**。
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
    // ⚠ 环形缓冲**始终在跑**（托盘菜单能随时导出）；抽稀到每 4 拍一条（≈200ms）
    static int ringTick = 0;
    BOOL wantRing = ((++ringTick & 3) == 0);
    BOOL wantFile = (g_afLogPath[0] && g_afLogLines < AFLOG_MAX_LINES);
    if (!wantRing && !wantFile) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    DWORD fevNow = g_lastFocusEvTick ? (GetTickCount() - g_lastFocusEvTick) : 0xFFFFFFFFu;
    // feh：最近一次 FOCUS 事件的来源窗口是否**就是当前前台顶层窗口**
    // （避免把"其它应用弹窗抢焦点"的噪音误当成"用户聚焦了输入框"）
    int fehMatch = (g_lastFocusEvHwnd && fgTop && g_lastFocusEvHwnd == fgTop) ? 1 : 0;
    char buf[512];
    // ⚠⚠ 字段顺序是**按重要性排的**：decision（含 uiact/pt/hid/why）紧跟 tag。
    _snprintf_s(buf, sizeof(buf), _TRUNCATE,
                "%02d:%02d:%02d.%03d %-7s => %s | rc=%d cif=%d key=%d await=%d vis=%d mot=%d | "
                "input=%p fg=%-20s fgTop=%p click=%p | "
                "gui=%d focusCls=%-20s isInput=%d caret=%p "
                "resp=%d hr=0x%X found=%d | xms=%d xok=%d | fvt=%d frole=0x%X fev=%u feh=%d | "
                "visWnd=%d rect=%d,%d %dx%d\n",
                st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, tag, decision,
                (int)recentClick, (int)clickInFg,
                (int)byKey, (int)await, (int)vis, (int)motion,
                (void*)input, AfClsName(fg), (void*)fgTop, (void*)g_lastClickTopHwnd,
                g_dbgHaveGui, g_dbgFocusCls, g_dbgIsInput,
                (void*)g_dbgCaret, g_dbgResp, (unsigned)g_dbgAcc, g_dbgAccFound,
                g_dbgExtraMs, g_dbgExtraOk,
                g_dbgFocusVt, (unsigned)g_dbgFocusRole, (unsigned)fevNow, fehMatch,
                g_dbgVisWnd, g_dbgRc[0], g_dbgRc[1], g_dbgRc[2], g_dbgRc[3]);
    if (wantRing) DiagRingPush(buf);
    if (wantFile) AfWriteRaw(buf);
}

// 直接写一行（不带那些字段，用于阶段标记）
static void AfNote(const char* text) {
    {
        char rb[256];
        _snprintf_s(rb, sizeof(rb), _TRUNCATE, "--- %s", text);
        DiagRingPush(rb);
    }
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
// 窗口大小档位：0=默认 1=2/3 2=一半 3=最小。按各布局自己的基准等比缩放。
static int  g_windowSize = 0;
BOOL        g_shiftSymbols = TRUE;     // 按 Shift 时数字键只显示特殊符号（原义，常规 Tab）
BOOL        g_dualSymbols = TRUE;      // 键面是否**上下并列**显示主/副两个符号（布局 Tab，默认开）

// ===== Shift 双击锁定（2026-10-04 新增）=====
BOOL        g_shiftLock = FALSE;        // 双击锁定态（界面高亮据此）
BOOL        g_shiftOnce = FALSE;        // 单击的一次性待用态
static DWORD g_shiftLastTap = 0;        // 上次点击 Shift 的时刻（双击窗口判定）
static const DWORD SHIFT_DOUBLE_MS = 350;   // 双击间隔上限
// ⚠ 默认**关闭**（2026-10-04 用户要求）。双击锁定改变了 Shift 的既有语义
BOOL        g_shiftDoubleTap = FALSE;   // 设置项：启用 Shift 双击锁定（ini: General/ShiftDoubleTap）

// ⚠⚠ 2026-10-10 修：这里原先两个分支都无条件 `g_sh = FALSE`，而 g_shiftLock 没人清
static void ReleaseShiftOnce(void) {
    if (g_shiftDoubleTap && g_shiftLock) return;   // 锁定态：Shift 要一直带下去
    if (g_shiftDoubleTap && g_shiftOnce) g_shiftOnce = FALSE;
    g_sh = FALSE;
}

int         g_keyIconStyle = 2;        // 键面始终「图标+文字」；「仅文字」模式已按实机反馈下线
DWORD       g_lht = 0;
int         g_hk = -1, g_pk = -1;
static int   g_pkFlash = -1;          // 刚松开、还在闪光期的那一格
static DWORD g_pkFlashUntil = 0;
#define HK_PK_FLASH_MS 130
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

// ⚠ 2026-10-04 第 25 轮：导航区曾经的专用字体（g_nf14…g_nf7 = 常规档 × 0.85）

int         g_bkspTextW = 0;                   // 「Backspace」在 g_f12 档下的实测容纳宽（退格标签判据）
static HFONT g_sfBig = 0, g_sfRow = 0, g_sfCtrl = 0, g_sfBase = 0, g_sfMeta = 0;   // 设置/关闭窗口固定字号字体
static HFONT g_sfSec = 0;                                                          // 14 档大字号（关于页产品名）
static Gdiplus::PrivateFontCollection* g_gdipFonts = NULL;   // 内嵌字体的 GDI+ 视角（GDI 注册的字 GDI+ 看不见）
static HANDLE g_fontReg = 0;
static BOOL   g_fontReady = FALSE;     // 内嵌字体注册成功（失败回退系统字体）
NOTIFYICONDATAW g_nid;
// 托盘图标点击去抖（见 WM_TRAY 的说明）：一次"双击"会送来三条消息
// 不去抖就会连切三次 —— 净结果纯属奇偶侥幸，某些外壳只送两条时直接变成"没反应"。
static DWORD g_lastTrayToggleTick = 0;

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
    } else {                        // 默认布局
        g_ww = (int)(980 * dpiScale);
        g_wh = (int)((320 + (g_showFKeys ? 53 : 0)) * dpiScale);
    }
    // 窗口大小档位：按各布局自己的基准缩放（小键盘 430×320、全尺寸 1280×404 各算各的）
    if (g_windowSize > 0) {
        double f = (g_windowSize == 1) ? (2.0 / 3.0) : (g_windowSize == 2) ? 0.5 : 0.35;
        g_ww = (int)(g_ww * f);
        g_wh = (int)(g_wh * f);
        if (g_ww < 300) g_ww = 300;
        if (g_wh < 120) g_wh = 120;
    }
}

static int AddKey(int x, int y, int w, int h, short vk, KeyType type, BOOL symShift = FALSE,
                  int align = KA_CENTER, int block = 0) {
    if (g_nk >= MAX_KEYS) return g_nk;
    KeyDef* k = &g_keys[g_nk++];
    k->x = x; k->y = y; k->w = w; k->h = h; k->vk = vk; k->type = type;
    k->symShift = symShift ? 1 : 0;
    k->align = (unsigned char)align;
    k->block = (unsigned char)block;
    return g_nk;
}

// ============================================================================
// -------------------------------------------------------------------------
// ============================================================================

// 小键盘：4 列 × 5 行数字盘（含 + 与 .）+ 底行 Tab / Shift / 空格
static void BuildNumpadPlus(int y) {
    const int colW  = (KEY_AREA_W - 3 * g_keyGap) / 4;
    const int pitch = g_keyHeight + g_keyGap;
    int yy = y;

    // Row 0: 回退, /, *
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

// 数字区当前是否隐藏：「用户手动按 Tab / 标题栏按钮」与「窗口过窄自动隐藏」是两件事
// 合成一个生效值再往外用 —— 只在 G 处改一份，绘制 / 命中 / 帧缓存签名才不会互相打架。
static BOOL NumpadHidden() { return (g_npHidden || g_npHiddenAuto); }

// ⚠ 这个阈值同时是「取消隐藏」时必须把窗口拉到的宽度 —— 见 SetFullNumpadHidden
#define FULL_NUMPAD_MIN_W_DIP 1090
static void UpdateNumpadAuto(int ww, double dpiScale) {
    if (g_layoutMode != 2) { g_npHiddenAuto = FALSE; return; }
    g_npHiddenAuto = (ww < (int)(FULL_NUMPAD_MIN_W_DIP * dpiScale));
}

// ============================================================================
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

// ⚠⚠ 2026-10-04 修：宽度**不能**用 `FullColX(c1) - FullColX(c0) - gap`。
static void FullPut(float base, const KbKeySpec& s, int yy, int kh, double u, int gap,
                    const float* bc, int nb, int block = 0) {
    float c0 = base + s.col, c1 = c0 + s.w;
    int xa = FullColX(c0, u, gap, bc, nb);
    int w  = (int)floor(c1 * u + 0.5) - (int)floor(c0 * u + 0.5) - gap;
    if (w < 1) w = 1;
    AddKey(xa, yy, w, kh, s.vk, s.type, FALSE, KA_CENTER, block);
}

// ---- 主区 6 行（列位严格按 ANSI 104，每行合计 15u）-----------------
static const KbKeySpec kFullMain0[] = {      // F 行：Esc + F1~F12（三组），无 Del
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

// ---- 导航区 3u：行位与主区对齐（PrtSc 在 F 行、Ins 在数字行、Del 在 Tab 行
//      ↑ 在 Shift 行、←↓→ 在 Ctrl 行）—— 这一点现状就是对的，保持不动 -
static const KbKeySpec kFullNav0[] = {
    // ⚠ ScrLk 必须是 VK_SCROLL(0x91)：一直写成 0x46，而 0x46 是字母 F
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

// ---- 数字区 4u：5 行，对齐主区的「数字行 ↔ Ctrl 行」（= 真实 104）-----
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
                FullPut(FULL_U_MAIN, nav[i].k[j], yy, KH, u, gap, bc, nb, 1);  // 1=导航区
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
                FullPut(FULL_U_MAIN + FULL_U_NAV, s, yy, s.vrow == 2 ? h2 : KH, u, gap, bc, nb, 2);  // 2=数字区
            }
        }
    }
}

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

// ---- F 行（可选，g_showFKeys）：Esc + F1~F12 + Del = 15.5u -
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

// ---- Row0：Esc, `, 1~0, -, =, Backspace = 15.5u -
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

// ---- Row1：Tab, q~p, [, ], \, Del = 15.5u -
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

// ---- Row2：Caps, a~l, ;, ', Enter = 15.5u -
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

// ---- Row3：LShift, z~m, ,, ., /, ↑, RShift = 15.5u -
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

// ---- Row4：底行（参考图键序）-
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
        // ⚠ `/` 的虚拟键码是 VK_OEM_2 = 0xBF，**不是** 0x2F —— 0x2F 是 ASCII 的 '/'
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

    DefPutRow(y, kDefRow4, kDefRow4N, u);
}

#define HDR_BTN_TOP_DIP 10
#define HDR_BTN_H_DIP   28

static void BuildKeys() {
    g_nk = 0;
    InvalidateFitTier();          // 键位几何变了，缓存的「区块统一档位」必须重算

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

// ⚠ 顺序：needW 用闭式解，绝不能在改 g_ww 之前去读 g_keyGap —— 它由 g_ww 反算
static void SetFullNumpadHidden(HWND hWnd, BOOL hidden) {
    g_npHidden = hidden;
    IniSetInt(L"Keyboard", L"NpHidden", hidden ? 1 : 0);
    if (!hidden) {
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

// ⚠ 子集是按「源码全部 UI 字符」生成的（tools/subset_font.py，从 build/font-backup 的
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

    BuildKeys();

    const double kFontRefKeyH  = 48.0;   // 默认布局（5 行 / 320 DIP）的键高，字号基准
    const double kFontGrowCapH = 90.0;   // 字号随键高增长的上限（DIP）
    double keyHDip = (double)g_keyHeight / dpiScale;
    if (keyHDip > kFontGrowCapH) keyHDip = kFontGrowCapH;

    // ⚠ 基准只能取键高，**不要再掺键宽**：试过 min(键高, 1u 键宽)，窗口拖窄时整盘字都跟着缩
    double finalFontScale = dpiScale * (keyHDip / kFontRefKeyH);
    if (finalFontScale < 0.4 * dpiScale) finalFontScale = 0.4 * dpiScale;

    // 单字重：g_f14 是唯一主档，修饰键与普通键的区分交给底色 + 文字色（与设置页 Tab 同一套逻辑）。
    g_f12 = MakeFont((int)(12 * finalFontScale + 0.5));   // 四舍五入，别让非整数缩放累积偏差
    g_f13 = MakeFont((int)(13 * finalFontScale + 0.5));
    g_f14 = MakeFont((int)(14 * finalFontScale + 0.5));   // 主档
    g_f10 = MakeFont((int)(10 * finalFontScale + 0.5));
    g_f9  = MakeFont((int)( 9 * finalFontScale + 0.5));
    g_f8 = MakeFont((int)(8 * finalFontScale + 0.5));
    g_f7 = MakeFont((int)(7 * finalFontScale + 0.5));
    g_f6 = MakeFont((int)(6 * finalFontScale + 0.5));

    // ⚠⚠ 2026-10-04 第 25 轮：**导航区专用字体（g_nf* / NAV_FONT_SCALE）整套删除**。

    {
        HDC dc = GetDC(0);
        g_bkspTextW = MeasureTextW(dc, L"Backspace", g_f12);
        ReleaseDC(0, dc);
    }

#ifdef HK_DIAG
    if (g_fontDiag) FontDiagDump();
#endif
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

static int TextInkShiftPx(float emPx, const wchar_t* s) {
    if (emPx <= 0.0f) return 0;
    float v = emPx * (TextHasDescender(s) ? -0.025f : 0.085f);
    return (int)(v >= 0.0f ? v + 0.5f : v - 0.5f);
}

// GDI+ 单行水平对齐模式。贴边标签必须用 Near/Far 而不是 Center
// 见 DrawTextKey 里关于「矩形边 vs 墨迹边」的说明。
enum TextAlignMode { TAL_Near = 0, TAL_Center = 1, TAL_Far = 2 };

static BOOL DrawTextGp(HDC dc, int x, int y, int w, int h, const wchar_t* s,
                       float emPx, DWORD color, int align, BOOL wrap) {
    if (!s || !s[0] || w <= 0 || h <= 0 || emPx <= 0.0f) return FALSE;
    if (!g_gdipFonts) return FALSE;          // GDI+ 私有字体未就绪 → 调用方回退 GDI 直绘

    Gdiplus::Graphics g(dc);
    // 高 DPI 用无 gridfit 的 AntiAlias（笔画线性、层级可预测）
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

// ⚠ 贴边锚点只有一个：DrawKeyLabel（图标+文字那一路）的 edge = 6 DIP。
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

// ⚠ 唯一调用点在 DrawKeys 里，副符号固定传 C_DIM —— 用户 2026-10-10 明确
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
static const HkIconDef& HkIcon(int id) { return k_hkIcons[id]; }

// ⚠ 文字宽度有**两个不能混用**的用途
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

// ⚠ 这里曾经「刻意只有三档」（地板 12），理由是「掉到 9pt 会让同一排导航键冒出 4 种字号」。
static HFONT FitKeyFont(HDC dc, const wchar_t* s, int maxW) {
    if (!s || !s[0] || maxW <= 0) return g_f14;
    // 8 档降档阶梯：正常尺寸命中前几档，越靠后只在「键窄而窗口极高」时才用得上。
    // 地板给到 6pt 是为了兑现「标签永远完整」（见 RecreateFontsAndLayout 里的说明）。
    HFONT ladder[8] = { g_f14, g_f13, g_f12, g_f10, g_f9, g_f8, g_f7, g_f6 };
    for (int i = 0; i < 8; i++)
        if (ladder[i] && MeasureTextW(dc, s, ladder[i]) <= maxW) return ladder[i];
    return g_f6;
}

// ===== 同宽键的字号必须一致（2026-10-04）=====
// ⚠ FitUnifiedTier 要遍历全盘键读它们的标签与「有没有图形」，这两个函数定义在
static const wchar_t* KeyText(const KeyDef* k);
static BOOL KeyHasGlyph(const KeyDef* k);
// 缓冲区版（写进调用方缓冲）—— 定义在更后面，同样要前置声明。
#define KEY_TEXT_MAX 16
static const wchar_t* KeyTextInto(const KeyDef* k, wchar_t* out, size_t n);
// 区块统一档计算（定义在下面一点），BlockTierNow 与 FontDiagDump 都要用。
static int FitUnifiedTier(HDC dc, unsigned char block);

// ⚠ 第 29 轮：**主区与导航区各自成组、各算一档**（见 FitUnifiedTier）。
static int  g_blockTier[3] = { 0, 0, 0 };
static BOOL g_blockTierValid = FALSE;

void InvalidateFitTier(void) { g_blockTierValid = FALSE; }

// 取指定区块当前帧的档位（没有就现算一次）。
static int BlockTierNow(HDC dc, unsigned char blk) {
    if (blk > 2) blk = 0;
    if (!g_blockTierValid) {
        g_blockTier[0] = FitUnifiedTier(dc, 0);
        g_blockTier[1] = FitUnifiedTier(dc, 1);
        g_blockTier[2] = FitUnifiedTier(dc, 2);
        g_blockTierValid = TRUE;
    }
    return g_blockTier[blk];
}

// ⚠⚠ 2026-10-04 第 25 轮：**原来这里有两套阶梯**（g_f* 常规 / g_nf* 导航区
struct FontLadder {
    HFONT f[8];
};

static FontLadder MainLadder() {
    FontLadder L;
    L.f[0]=g_f14; L.f[1]=g_f13; L.f[2]=g_f12; L.f[3]=g_f10;
    L.f[4]=g_f9;  L.f[5]=g_f8;  L.f[6]=g_f7;  L.f[7]=g_f6;
    return L;
}

// 按给定阶梯选档：第一个量得下的档，全放不下则地板档。
static int FitTierInLadder(HDC dc, const wchar_t* s, int maxW, const FontLadder& L) {
    for (int i = 0; i < 8; i++)
        if (L.f[i] && MeasureTextW(dc, s, L.f[i]) <= maxW) return i;
    return 7;                                   // 地板档
}

// ⚠ 统一字号的**分组判据**（第 29 轮）：**区块各自成组**。
static BOOL InUnifiedGroup(unsigned char blk, unsigned char want) {
    return blk == want;
}

// 该区块的统一档：遍历**本区块**所有纯文字键，取「每一键在它自己的可用宽下都不超宽」
// 的最大档位（即最严档）。宽键（1.25u 的 Ctrl / Alt、2u 的退格…）按自己宽度量。
static int FitUnifiedTier(HDC dc, unsigned char block) {
    const FontLadder L = MainLadder();
    int worst = 0;
    BOOL any = FALSE;
    for (int i = 0; i < g_nk; i++) {
        const KeyDef* k = &g_keys[i];
        if (!InUnifiedGroup(k->block, block)) continue;
        // 图标键（Tab / Caps / Shift / Enter / 退格 / 方向键 / Win / Menu）不参与
        // 它们走 DrawKeyLabel 自己那套「图标 + 名字」阶梯，可用宽里还要留图标位。
        if (KeyHasGlyph(k)) continue;
        wchar_t s[KEY_TEXT_MAX];
        KeyTextInto(k, s, KEY_TEXT_MAX);
        if (!s[0]) continue;
        int avail = k->w - (int)(6 * GetSystemDpiScale());
        if (avail < 1) continue;
        int t = FitTierInLadder(dc, s, avail, L);
        if (!any || t > worst) worst = t;        // 取最严（档位序号越大字号越小）
        any = TRUE;
    }
    return any ? worst : 0;
}

// ⚠ 2026-10-05 第 29 轮：**tier 由调用方传入**，本函数不再自己量档。
static HFONT FitKeyFontUnified(HDC dc, int idx, const KeyDef* self, const wchar_t* s, int maxW, int tier) {
    (void)idx; (void)self; (void)maxW;
    // ⚠ 2026-10-04 第 25 轮：全键盘只有一套阶梯（导航区专用档已删）。
    const FontLadder L = MainLadder();
    if (!s || !s[0]) return L.f[0] ? L.f[0] : g_f14;
    if (tier < 0) tier = 0;
    if (tier > 7) tier = 7;
    if (L.f[tier]) return L.f[tier];
    return L.f[7] ? L.f[7] : g_f6;               // 地板档
}

#ifdef HK_DIAG
// ---- -fontdiag：把「每个键的标签 / 键宽 / 最终档位」dump 成表 -
static void FontDiagDump() {
    if (!g_fontDiag) return;

    wchar_t self[MAX_PATH] = {0};
    GetModuleFileNameW(NULL, self, MAX_PATH);
    wchar_t* slash = wcsrchr(self, L'\\');
    if (slash) *(slash + 1) = 0;
    wchar_t path[MAX_PATH * 2] = {0};
    _snwprintf_s(path, MAX_PATH * 2, _TRUNCATE, L"%lsfontdiag.txt", self);

    FILE* f = NULL;
    if (_wfopen_s(&f, path, L"w, ccs=UTF-8") != 0 || !f) return;

    HDC dc = GetDC(0);
    fwprintf(f, L"# layoutMode=%d  g_ww=%d g_wh=%d keyHeight=%d keyGap=%d keyAreaX=%d dpi=%.3f\n",
             g_layoutMode, g_ww, g_wh, g_keyHeight, g_keyGap, g_keyAreaX, GetSystemDpiScale());
    fwprintf(f, L"# cols: idx block w maxW glyph t_own t_block label\n");

    // 逐键打印。FitUnifiedTier 每次都会遍历本区块 —— 这里刻意**不走缓存**
    // 直接算才是"当前几何的真值"。
    for (int i = 0; i < g_nk; i++) {
        const KeyDef* k = &g_keys[i];
        // ⚠ 局部缓冲：FitUnifiedTier 会调 KeyTextInto 覆盖 static 包装的 buf
        wchar_t s[KEY_TEXT_MAX];
        KeyTextInto(k, s, KEY_TEXT_MAX);
        if (!s[0]) continue;

        const int maxW = k->w - (int)(6 * GetSystemDpiScale());
        // t_own = 本键自己按可用宽能到的最大档；t_block = 本区块统一档（实际绘制用的）。
        int tOwn = k->w > 0 ? FitTierInLadder(dc, s, maxW, MainLadder()) : 0;
        int tBlk = FitUnifiedTier(dc, k->block);
        fwprintf(f, L"%3d  b%d  w=%-4d mW=%-4d glyph=%d t_own=%d t_block=%d  %ls\n",
                 i, (int)k->block, k->w, maxW, (int)KeyHasGlyph(k), tOwn, tBlk, s);
    }
    ReleaseDC(0, dc);
    fclose(f);
}
#endif  // HK_DIAG

// 图标定义在网格坐标里，描边宽度也是网格单位：缩放后必须把 pen 宽度乘回 k
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

// 这个键有没有可画的图形。Win 键是特例：Windows 徽标字体里没有对应字形
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
        // ⚠ 外框要比 size 收一圈：Menu 的三条杠在 24 网格里只占 2..22（≈83%）
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

static BOOL DrawKeyLabel(HDC dc, const KeyDef* k, HFONT f, const wchar_t* text, DWORD color) {
    if (!KeyHasGlyph(k)) return FALSE;

    int iconInline = k->h * 36 / 100;
    int iconOnly   = k->h * 52 / 100;
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

// ⚠⚠⚠ 2026-10-05 血案（第 27 轮）：`KeyText` 曾经返回**指向静态缓冲区**的指针。
static void LetterKeyTextInto(short vk, wchar_t* out, size_t n) {
    if (!out || n < 2) return;
    BOOL upper = g_cp;
    if (g_sh || g_physShift) upper = !upper;
    out[0] = (vk >= 0x41 && vk <= 0x5A) ? (upper ? vk : (wchar_t)(vk + 32)) : (wchar_t)vk;
    out[1] = 0;
}

// 网页布局的网址后缀键（vk 0x200 起为索引哨兵）
static const wchar_t* g_domainTexts[6] = { L"www.", L".com", L".cn", L".org", L".cc", L".net" };

// ⚠ 判定点有两处，必须同源，否则会出现「大部分键面不动、个别键（如 `~）还在跟着变」
static BOOL IsWebFnLayer() { return (g_fnLayer && g_fnWebLayout); }

// 把键面文本写进调用方缓冲区（⚠ 不要再改成"返回 static 指针"，理由见上方血案注释）。
// n 是宽字符容量，内部按 KEY_TEXT_MAX 处理；返回 out（方便链式使用）。
static const wchar_t* KeyTextInto(const KeyDef* k, wchar_t* out, size_t n) {
    if (!out || n < 2) return L"";
    out[0] = 0;
    if (k->vk >= 0x200 && k->vk <= 0x205) {
        const wchar_t* d = g_domainTexts[k->vk - 0x200];
        wcsncpy_s(out, n, d, _TRUNCATE);
        return out;
    }
    // 网页层符号键：键面固定画一个符号，与 Shift、g_shiftSymbols 都无关
    if (k->type == K_SYM) {
        wchar_t ch = GetSymForKey(k->vk, k->symShift ? TRUE : FALSE);
        if (ch) { out[0] = ch; out[1] = 0; return out; }
    }
    if (k->type == K_LETTER) return LetterKeyTextInto(k->vk, out, n), out;
    if (k->type == K_NORMAL) {
        if (g_fnLayer && !g_fnWebLayout) {
            int fn = FnMap(k->vk);
            if (fn) { swprintf(out, n, L"F%d", fn); return out; }
        }
        wchar_t ch = GetSymForKey(k->vk, (g_sh && g_shiftSymbols && !IsWebFnLayer()) ? TRUE : FALSE);
        if (ch) { out[0] = ch; out[1] = 0; return out; }
    }
    // F1~F12 顶行 / 小键盘数字
    if (k->vk >= 0x70 && k->vk <= 0x7B) { swprintf(out, n, L"F%d", k->vk - 0x6F); return out; }
    if (k->vk >= 0x60 && k->vk <= 0x69) {
        out[0] = (wchar_t)(L'0' + (k->vk - 0x60)); out[1] = 0; return out;
    }
    const wchar_t* lit = NULL;
    switch (k->vk) {
        case 0x6A: lit = L"*"; break;
        case 0x6B: lit = L"+"; break;
        case 0x6D: lit = L"-"; break;
        case 0x6E: lit = L"."; break;
        case 0x6F: lit = L"/"; break;
        case 0x90: lit = L"Num"; break;
        case 0x1B: lit = L"Esc"; break;
        case 0x2E: lit = L"Del"; break;
        case 0x09: lit = L"Tab"; break;
        case 0x0D: lit = L"Enter"; break;
        case 0x14: lit = L"Caps"; break;
        case 0x10: case 0xA0: case 0xA1: lit = L"Shift"; break;
        case 0x11: lit = L"Ctrl"; break;
        case 0x12: lit = L"Alt"; break;
        // Win / Menu：键面只留图形，不再带名字（实机反馈：这两个键的文字描述去掉）。
        // 键面本身就是公认图形（四格徽标 / 三条杠），名字是冗余信息，去掉后与方向键一样干净。
        case 0x5B: case 0x5D: lit = L""; break;
        // 独立小键盘是 4 列网格，纯空白的一格看着像画错了（参考图那里写着「空格」）
        // 默认布局 / 全尺寸的空格键很宽，沿用屏幕键盘的惯例不写字。
        case 0x20: lit = (g_layoutMode == 1) ? T(L"\x7A7A\x683C", L"Space") : L""; break;
        case 0x25: lit = L"\x2190"; break;
        case 0x26: lit = L"\x2191"; break;
        case 0x27: lit = L"\x2192"; break;
        case 0x28: lit = L"\x2193"; break;
        case 0x2C: lit = L"PrtSc"; break;
        // ⚠ 2026-10-05：这三个**保持全称**，导航区因此停在 9pt。
        case 0x91: lit = L"ScrLk"; break;   // VK_SCROLL（原来是 0x46 = 字母 F，已修）
        case 0x13: lit = L"Pause"; break;
        case 0x08: {
            if (g_layoutMode == 0) { lit = L""; break; }
            int avail = k->w - (int)(6 * GetSystemDpiScale());
            if (g_bkspTextW > 0) { lit = (avail >= g_bkspTextW) ? L"Backspace" : L""; break; }
            lit = (k->w >= (int)(1.9 * k->h)) ? L"Backspace" : L"";
            break;
        }
        case 0x2D: lit = L"Ins"; break;
        case 0x24: lit = L"Home"; break;   // 与 Pause / ScrLk 同理：保持全称，接受导航区 9pt
        case 0x21: lit = L"PgUp"; break;
        case 0x23: lit = L"End"; break;
        case 0x22: lit = L"PgDn"; break;
        case 0xC0: lit = L"\x60"; break;
        default: lit = NULL; break;
    }
    if (lit) { wcsncpy_s(out, n, lit, _TRUNCATE); return out; }

    if (k->type == K_HIDE) { wcsncpy_s(out, n, T(L"\x6536\x8D77", L"Hide"), _TRUNCATE); return out; }
    if (k->type == K_SPACE) return out;                       // 空串
    // vk==0 是 Fn 键（Fn 网页层 / F1~F12 层的入口）
    if (k->type == K_SPECIAL && k->vk == 0) { wcsncpy_s(out, n, L"Fn", _TRUNCATE); return out; }
    return out;
}

// 便捷包装：**仅用于「拿到就用、中间不再调 KeyText」的场景**。
// ⚠ 需要跨调用持有文本时，必须自己 `wchar_t buf[KEY_TEXT_MAX]; KeyTextInto(k, buf, ...)`。
static const wchar_t* KeyText(const KeyDef* k) {
    static wchar_t buf[KEY_TEXT_MAX];
    return KeyTextInto(k, buf, KEY_TEXT_MAX);
}

static BOOL IsActive(const KeyDef* k) {
    if (k->vk == 0x14 && g_cp) return TRUE;
    // ⚠ 不要再改成「跟实体灯走」或「自己取反」：第 17~20 轮在这上面反复翻车
    if (k->vk == 0x90 && g_npLock) return TRUE;
    if (k->vk == VK_LSHIFT) return (g_sh || g_physLShift);
    if (k->vk == VK_RSHIFT) return (g_sh || g_physRShift);
    if (k->vk == VK_SHIFT && (g_sh || g_physShift)) return TRUE;   // 通用 VK_SHIFT：现有布局未用到，留着防以后加键
    if (k->vk == 0x11 && g_ct) return TRUE;
    if (k->vk == VK_LWIN && (g_winKey || g_physWin)) return TRUE;   // 锁定(等 Win+快捷键)或实体 Win 按下时高亮
    if (k->vk == 0x12 && g_al) return TRUE;
    if (k->type == K_SPECIAL && k->vk == 0 && g_fnLayer) return TRUE;
    return FALSE;
}

// ========== IME-Compatible Input Injection ==========
// ⚠ 必须用 SendInput（keybd_event 已废弃且拦不到高权限窗口），扫描码走
#define KEY_INJECT_GAP_MS 1

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

// ⚠ 2026-10-10：**当前没有被调用** —— 屏幕小键盘的数字键已改为"一律发数字"
static BYTE NumpadNavKey(BYTE vk) {
    switch (vk) {
    case 0x67: return VK_HOME;        // 7
    case 0x68: return VK_UP;          // 8
    case 0x69: return VK_PRIOR;       // 9 PgUp
    case 0x64: return VK_LEFT;        // 4
    case 0x66: return VK_RIGHT;       // 6
    case 0x61: return VK_END;         // 1
    case 0x62: return VK_DOWN;        // 2
    case 0x63: return VK_NEXT;        // 3 PgDn
    case 0x60: return VK_INSERT;      // 0 Ins
    default:   return 0;               // 5：未锁定时不发键
    }
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

    // ---- 第 1 段：修饰键按下（一次 SendInput，顺序 ct → al → sh → win）-
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

    // ---- 第 2 段：目标键 down -
    // ⚠⚠⚠ **必须带 KEYEVENTF_SCANCODE**，否则 `wScan` 是死字段（issue #3 根因）
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

    // ---- 第 3 段：目标键 up -
    {
        INPUT up = {};
        up.type = INPUT_KEYBOARD;
        up.ki.wVk = vk;
        up.ki.wScan = (WORD)sc;
        up.ki.dwFlags = extFlag | KEYEVENTF_SCANCODE | KEYEVENTF_KEYUP;
        SendInput(1, &up, sizeof(INPUT));
    }

    // ---- 第 4 段：修饰键抬起（逆序win → sh → al → ct，与按下相反）-
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

// 发送一次 Win 键（开/关开始菜单）：按下与抬起分两次注入并留出间隔
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

// ⚠ 别再用「注入 Esc 关开始菜单」那套：曾这么做过，但注入的 Win 键在部分环境下
static void ClearWinLock() {
    g_winKey = FALSE;
    g_winCount = 0;
}

// ========== 开始菜单可见性检测（IAppVisibility，Win8+ 官方 API） ==========
static const GUID CLSID_AppVisibility =
    {0x7E5FE3D9, 0x985F, 0x4908, {0x91, 0xF9, 0xEE, 0x19, 0xF9, 0xFD, 0x15, 0x14}};
static const GUID IID_IAppVisibility =
    {0x2246EA2D, 0xCAEA, 0x4444, {0xA3, 0xC4, 0x6D, 0xE8, 0x27, 0xE4, 0x43, 0x13}};

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
        // 网址后缀是「一次性把整串打完」，不会进中文组字 —— 走 gap=0 的快速路径
        // 否则每个字符 2 次 Sleep(1)，20 字符的域名就是 40ms 的卡顿。
        SendKeyGap((BYTE)vk, sh, FALSE, FALSE, FALSE, 0);
    }
}

#ifdef HK_DIAG
// ---- -diag：最小诊断，记录"注入前后"的关键系统状态 -
// ⚠ 重点看 LBTN
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
static BOOL g_inWaitLButton = FALSE;
// ⚠ 连发时用户**一直按着**鼠标，等下去只会每次都耗满 250ms 超时
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
            // ⚠ WM_QUIT 必须自己再投回去：PeekMessage 会把它取走
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
    // ⚠ 所有按键都在此统一等待 —— 空格尤其重要：它负责把候选框里的中文上屏
    //   若在左键按下期间注入，中文同样上不去（这正是 issue #3 的主诉之一）。
    WaitForLeftButtonUp();
    switch (k->type) {
    case K_LETTER:
#ifdef HK_DIAG
        DiagSnap(L"[before]");
#endif
        if (g_ct || g_al || g_winKey) {
            SendKey(k->vk, g_sh, g_ct, g_al, g_winKey);
            ReleaseShiftOnce(); g_ct = FALSE; g_al = FALSE; ClearWinLock();
        } else {
            // ⚠⚠ 2026-10-17 修（用户报「双击 Shift 锁定后按 Caps，键面变小写但打出来还是大写」）
            BOOL wantUpper = (g_cp != g_sh);
            BOOL us = wantUpper ? !(GetKeyState(VK_CAPITAL) & 1) : ((GetKeyState(VK_CAPITAL) & 1) != 0);
            SendKey(k->vk, us, FALSE, FALSE);
            ReleaseShiftOnce();
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
                ReleaseShiftOnce(); g_ct = FALSE; g_al = FALSE; ClearWinLock();
                InvalidateRect(g_hWnd, 0, TRUE);
                break;
            }
        }
        // ---- 小键盘数字键：**始终发数字键**（2026-10-10 改）-
        // ⚠⚠ 为什么不看 NumLock 了（用户报「小键盘没作用了 / 小键盘布局也输入不了数字」）
        if (k->vk >= 0x60 && k->vk <= 0x69) {
            SendKey((g_layoutMode == 2 && !g_npLock) ? NumpadNavKey(k->vk) : k->vk,
                    g_sh, g_ct, g_al, g_winKey);
            ReleaseShiftOnce(); g_ct = FALSE; g_al = FALSE; ClearWinLock();
            break;
        }
        SendKey(k->vk, g_sh, g_ct, g_al, g_winKey);
        ReleaseShiftOnce(); g_ct = FALSE; g_al = FALSE; ClearWinLock();
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
        // --- 以下是第 20~24 轮留下的结论，仍然成立，保留备查
        // ⚠⚠ 2026-10-10 大改（用户报「Num Lock 又不能点击了 / 小键盘输入不了数字」）。
        if (k->vk == 0x90) {
            INPUT pair[2] = {};
            pair[0].type = INPUT_KEYBOARD;
            pair[0].ki.wVk = VK_NUMLOCK;                 // wScan 留 0
            pair[0].ki.dwFlags = KEYEVENTF_EXTENDEDKEY;  // ⚠ 刻意无 SCANCODE
            pair[1] = pair[0];
            pair[1].ki.dwFlags |= KEYEVENTF_KEYUP;
            SendInput(2, pair, sizeof(INPUT));
            // 不写 g_npLock / g_physNum：真值由 TIMER_FOCUS 下一拍读回并驱动高亮。
            g_sh = FALSE; g_ct = FALSE; g_al = FALSE; ClearWinLock();
            break;
        }
        SendKey(k->vk, g_sh, g_ct, g_al, g_winKey);
        ReleaseShiftOnce(); g_ct = FALSE; g_al = FALSE; ClearWinLock();
        break;
    case K_SYM:
        // 网页层符号键：键面画什么就发什么。symShift=1 的格子（! @ # …）内部自动带 Shift
        // 所以不需要用户先点 Shift，点击结果与键面永远一致。
        SendKey(k->vk, k->symShift ? TRUE : FALSE, g_ct, g_al, g_winKey);
        ReleaseShiftOnce(); g_ct = FALSE; g_al = FALSE; ClearWinLock();
        break;
    case K_MOD:
        if (k->vk == VK_RSHIFT || k->vk == VK_SHIFT || k->vk == VK_LSHIFT) {
            // ⚠ 网页布局层里 Shift 不再兼作「退出 Fn 层」
            BOOL keepFn = (g_fnLayer && g_fnWebLayout);
            BOOL wasFn = g_fnLayer;

            if (g_shiftDoubleTap) {
                // ---- 新模式：单击 = 一次性，双击 = 锁定 -
                DWORD now = GetTickCount();
                BOOL isDouble = (g_shiftLastTap && (now - g_shiftLastTap) <= SHIFT_DOUBLE_MS);
                g_shiftLastTap = now;

                if (g_shiftLock) {
                    // 已在锁定态 → 再点一次解除
                    g_shiftLock = FALSE;
                    g_shiftOnce = FALSE;
                    g_sh = FALSE;
                    if (!keepFn) g_fnLayer = FALSE;
                } else if (isDouble) {
                    // 350ms 内第二下 → 进入锁定
                    g_shiftLock = TRUE;
                    g_shiftOnce = FALSE;
                    g_sh = TRUE;
                    if (!keepFn) g_fnLayer = FALSE;
                } else {
                    // 单击 → 一次性（下一个键用完自动释放）
                    g_shiftOnce = TRUE;
                    g_sh = TRUE;
                    if (!keepFn) g_fnLayer = FALSE;
                }
                InvalidateRect(g_hWnd, 0, TRUE);
            } else if (g_sh) {
            // ---- 旧模式：单击即锁定，再点切输入法（保持原行为）-
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
        } else {
            SendKey(k->vk, g_sh, g_ct, g_al, g_winKey);
            ReleaseShiftOnce(); g_ct = FALSE; g_al = FALSE; ClearWinLock();
        }
        break;
    case K_CAPS:
        SendKey(0x14, FALSE, FALSE, FALSE, g_winKey);
        ClearWinLock();
        g_cp = (GetKeyState(VK_CAPITAL) & 1) != 0;
        break;
    case K_ARROW:
        SendKey(k->vk, g_sh, g_ct, g_al, g_winKey);
        ReleaseShiftOnce();
        g_ct = FALSE; g_al = FALSE; ClearWinLock();
        break;
    case K_SPACE:
        SendKey(0x20, g_sh, g_ct, g_al, g_winKey);
        ReleaseShiftOnce();
        g_ct = FALSE; g_al = FALSE; ClearWinLock();
        break;
    case K_HIDE: UserHideKeyboard(); break;   // 收起键：临时挡路，遇到输入框仍会回来
    default: break;
    }
}
// ========== Header Layout & Dynamic DPI Positioning ==========
#define HDR_DOCK  1000
#define HDR_MIN   1003
#define HDR_CLOSE 1004
#define HDR_LAYOUT 1007  // 标题栏布局胶囊：打开/收起布局菜单（A1）
#define HDR_HIDE  1006   // ⏑ 按钮：永久隐藏到托盘（单击托盘图标恢复）

struct HeaderMetrics {
    int btnY, btnH;
    int wMenu, wMin, wHide, wNum, wClose;
    int xMenu, xMin, xHide, xNum, xClose;
    int xTitle, wTitle;
    BOOL numBtnVisible;
};

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
    // 按钮上留白固定 10 DIP（原来是 (header-btnH)/2 居中，页头 36 时只剩 4 DIP，贴顶）
    // 页头被压得很矮时退回居中，免得按钮溢出页头。
    hm.btnY = (int)(HDR_BTN_TOP_DIP * dpi);
    if (hm.btnY + hm.btnH > g_headerH) {
        hm.btnY = (g_headerH - hm.btnH) / 2;
        if (hm.btnY < 0) hm.btnY = 0;
    }

    int gap     = (int)(8 * dpi);
    int rMargin = (int)(8 * dpi);

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
    if (hm.numBtnVisible && x >= hm.xNum && x < hm.xNum + hm.wNum) return HDR_LAYOUT;
    if (x >= hm.xMenu && x < hm.xMenu + hm.wMenu) return HDR_DOCK;
    return -1;
}

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

static void DrawHeaderPill(HDC dc, const HeaderMetrics& hm, int x, int w, BOOL hov,
                           const HkIconDef& icon, int iconId,
                           const wchar_t* label, BOOL active, BOOL caret) {
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
    // 下箭头：可见约 9 DIP 的小三角。间距 6 DIP —— 再小就和文字粘在一起
    // 再大整组就往左偏（组是整体居中的）。
    int cGap    = caret ? (int)(6 * dpi * HeaderFontRatio()) : 0;
    int caretSz = caret ? (int)(9 * dpi * HeaderFontRatio()) : 0;
    int groupW  = iconSz + gap + adv + (caret ? cGap + caretSz : 0);
    int cx = x + (w - groupW) / 2;
    DrawHkIcon(dc, (float)cx, (float)(hm.btnY + (hm.btnH - iconSz) / 2), (float)iconSz,
               icon, fg, fg);
    // DrawTextC 是把墨迹居中在矩形里的，所以矩形要再左移「(矩形宽 - 文字宽)/2」
    // 墨迹才会正好落在间距之后（否则整段文字右移半个余量）。
    DrawTextC(dc, cx + iconSz + gap + adv / 2 - (tw + 4) / 2, hm.btnY, tw + 4, hm.btnH,
              label, g_f12, fg);
    if (caret) {
        int ccx = cx + iconSz + gap + adv + cGap;
        // HKICON_CHEVRONDOWN 是 24 网格、可见区 6..18 ⇒ 外框 = 可见 × 24/12
        int box = (int)(caretSz * 24.0 / 12.0 + 0.5);
        DrawHkIcon(dc, (float)ccx, (float)(hm.btnY + (hm.btnH - box) / 2), (float)box,
                   HkIcon(HKICON_CHEVRONDOWN), fg, fg);
    }
}

static void DrawHeaderMenuButton(HDC dc, const HeaderMetrics& hm) {
    DrawHeaderPill(dc, hm, hm.xMenu, hm.wMenu, (g_hdrHov == HDR_DOCK),
                   HkIcon(HKICON_GEAR), HKICON_GEAR,
                   T(L"\x8BBE\x7F6E", L"Settings"), FALSE, FALSE);
}

// ===== 布局菜单（A1：窗口内弹层，见 HKeyboard-UI方案评估 §三）=====
// ⚠ 三件事必须一起做，少一件就是 bug
static BOOL g_lmOpen = FALSE;
static int  g_lmHover = -1;
// 打开菜单时所在的前台窗口。换到别的程序后菜单就不该留着（见 UpdateAutoVisibility 的短路）。
static HWND g_lmOpenFg = NULL;

static void ApplyKeyboardLayout(BOOL resetSize);

static void SetLayoutMode(int mode) {
    if (mode < 0 || mode > 2 || mode == g_layoutMode) return;
    if (mode == 1) g_prevLayout = g_layoutMode;   // 记下「上次布局」，小键盘可回退
    g_layoutMode = mode;
    ApplyKeyboardLayout(TRUE);                    // 统一入口：落盘 + 按布局重置窗口尺寸
}

static int LayoutMenuItemCount() { return (g_layoutMode == 2) ? 4 : 3; }

static RECT LayoutMenuRect() {
    double dpi = GetSystemDpiScale();
    HeaderMetrics hm = GetHeaderMetrics();
    int n = LayoutMenuItemCount();
    int itemH = (int)(44 * dpi);
    int pad = (int)(6 * dpi);
    // 184 而不是 168：开关行右侧要留出开关（36）与间距，168 时英文 "Number pad"
    // 会被裁掉。中文标签在 168 下是够的，但这份界面有英文版，取两者中较宽的那个。
    int w = (int)(184 * dpi);
    if (w < hm.wNum) w = hm.wNum;
    int hh = n * itemH + pad * 2;
    int right = hm.xNum + hm.wNum;
    int top = hm.btnY + hm.btnH + (int)(8 * dpi);
    if (top + hh > g_wh) top = hm.btnY - (int)(8 * dpi) - hh;   // 超出底边就上翻
    if (top < 0) top = 0;
    RECT r = { right - w, top, right, top + hh };
    return r;
}

static int LayoutMenuHitIndex(int x, int y) {
    RECT p = LayoutMenuRect();
    if (x < p.left || x >= p.right || y < p.top || y >= p.bottom) return -1;
    double dpi = GetSystemDpiScale();
    int pad = (int)(6 * dpi), itemH = (int)(44 * dpi);
    int idx = (y - (p.top + pad)) / itemH;
    if (idx < 0 || idx >= LayoutMenuItemCount()) return -1;
    return idx;
}

static void LayoutMenuApply(HWND hWnd, int idx) {
    g_lmOpen = FALSE;
    g_lmHover = -1;
    if (idx == 0)      SetLayoutMode(0);
    else if (idx == 1) SetLayoutMode(1);
    else if (idx == 2) SetLayoutMode(2);
    else if (idx == 3 && g_layoutMode == 2) SetFullNumpadHidden(hWnd, !NumpadHidden());
    if (hWnd && IsWindow(hWnd)) InvalidateRect(hWnd, NULL, TRUE);
}

// 勾用两条线画，不依赖字体里有没有 ✓ 这个字形
static void DrawMenuCheck(HDC dc, int cx, int cy, int size, DWORD color) {
    HPEN pen = CreatePen(PS_SOLID, (size >= 20 ? 2 : 1), color);
    HGDIOBJ old = SelectObject(dc, pen);
    MoveToEx(dc, cx - size / 4, cy, NULL);
    LineTo(dc, cx - size / 16, cy + size / 4);
    LineTo(dc, cx + size / 3, cy - size / 4);
    SelectObject(dc, old);
    DeleteObject(pen);
}

// ⚠ 为什么不用勾：全尺寸下菜单里有**两个** ✓（全尺寸 + 数字区），而且两行都涂着同一层
static void DrawMenuToggle(HDC dc, int x, int y, int w, int h, BOOL on) {
    double dpi = GetSystemDpiScale();
    // 关：面板底与键面之间的一档灰；开：主色。两种主题下都成立（都是与 C_KEY 混出来的）。
    DWORD track = on ? C_HOT : BlendColor(C_KEY, C_WHITE, 0.14);
    DrawRoundRect(dc, x, y, w, h, track, track, h / 2);
    int inset = (int)(2 * dpi);
    int d = h - inset * 2;
    int kx = on ? (x + w - inset - d) : (x + inset);
    // 滑块固定「接近白」：浅色下是白钮，深色下是浅灰钮，都不需要按主题分叉。
    DWORD knob = BlendColor(C_KEY, C_WHITE, 0.92);
    DrawRoundRect(dc, kx, y + inset, d, d, knob, knob, d / 2);
}

static void DrawLayoutMenu(HDC dc) {
    if (!g_lmOpen) return;
    double dpi = GetSystemDpiScale();
    RECT p = LayoutMenuRect();
    int pad = (int)(6 * dpi), itemH = (int)(44 * dpi);
    DrawRoundRect(dc, p.left, p.top, p.right - p.left, p.bottom - p.top,
                  C_KEY, C_KEY, (int)(12 * dpi));
    const wchar_t* labels[4] = { T(L"默认布局", L"Default"), T(L"小键盘", L"Numpad"),
                                 T(L"全尺寸", L"Full size"), T(L"数字区", L"Number pad") };
    int n = LayoutMenuItemCount();
    int iw = p.right - p.left;
    int toggleW = (int)(36 * dpi), toggleH = (int)(20 * dpi);
    for (int i = 0; i < n; i++) {
        int top = p.top + pad + i * itemH;
        RECT ir = { p.left + pad, top, p.right - pad, top + itemH };
        // 数字区（i==3）是开关行：勾/淡底都不给它，只给右侧的小开关
        BOOL isToggle = (i == 3);
        BOOL cur = isToggle ? !NumpadHidden() : (i == g_layoutMode);
        if (i == g_lmHover)
            DrawRoundRect(dc, ir.left, ir.top, ir.right - ir.left, ir.bottom - ir.top,
                          C_REGULAR_HOV, C_REGULAR_HOV, (int)(8 * dpi));
        else if (cur && !isToggle)
            DrawRoundRect(dc, ir.left, ir.top, ir.right - ir.left, ir.bottom - ir.top,
                          BlendColor(C_KEY, C_HOT, 0.08), BlendColor(C_KEY, C_HOT, 0.08), (int)(8 * dpi));
        if (cur && !isToggle)
            DrawMenuCheck(dc, ir.left + (int)(16 * dpi), ir.top + itemH / 2, (int)(16 * dpi), C_HOT);
        int textX = ir.left + (int)(30 * dpi);
        int textR = ir.right - (int)(12 * dpi);
        if (isToggle) textR = ir.right - toggleW - (int)(26 * dpi);
        DrawTextL(dc, textX, ir.top, textR - textX, itemH, labels[i], g_sfCtrl, C_WHITE);
        if (isToggle) {
            // 分隔线：画在开关行上沿，把「三个选项 / 一个开关」分开
            Fill(dc, p.left + (int)(12 * dpi), top - (int)(0.5 * dpi),
                 iw - (int)(24 * dpi), 1, C_META);
            DrawMenuToggle(dc, ir.right - (int)(12 * dpi) - toggleW,
                           ir.top + (itemH - toggleH) / 2, toggleW, toggleH, cur);
        }
    }
}

static void DrawHeader(HDC dc) {
    // 标题栏与主界面一体化：不单独铺底色，统一由 ClearWindowBackBuffer 的面板底色呈现。
    double dpiScale = GetSystemDpiScale();
    HeaderMetrics hm = GetHeaderMetrics();

    DrawHeaderMenuButton(dc, hm);

    if (hm.wTitle > 40) {
        DrawTextC(dc, hm.xTitle, 0, hm.wTitle, g_headerH, L"", g_f12, C_DIM);
    }

    // 布局胶囊：三种布局下都常驻（全尺寸下菜单里还管着数字区显隐）。
    // 激活态 = **菜单正开着**，不是「小键盘开着」——理由见 DrawHeaderPill 的说明。
    if (hm.numBtnVisible) {
        DrawHeaderPill(dc, hm, hm.xNum, hm.wNum, (g_hdrHov == HDR_LAYOUT),
                       HkIcon(HKICON_NUMPAD), HKICON_NUMPAD,
                       T(L"\x5E03\x5C40", L"Layout"), g_lmOpen, TRUE);
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
    // 各区块统一档位每帧重算一次（几何 / 字体 / 标签都可能变）。
    g_blockTierValid = FALSE;
    for (int i = 0; i < g_nk; i++) {
        const KeyDef* k = &g_keys[i];
        BOOL active = IsActive(k);
        BOOL pressed = (i == g_pk || i == g_pkFlash);   // g_pkFlash = 松手后的短暂反馈
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

        DWORD bg, outline, textC;
        if (active || pressed) {
            if (g_themeStyle == 1) {
                // ⚠ 上一版这里走 btnRegularBgActive，而经典风格把它覆盖成了 #D8D8D8
                bg = C_HOT; outline = C_KEY_BORDER; textC = C_ON_PRIMARY;
            } else {
                bg = C_REGULAR_ACT; outline = C_BORDER_HOVER; textC = C_WHITE;
            }
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

        DrawRoundRect(dc, k->x, k->y, k->w, k->h, bg, outline, g_themeStyle == 1 ? 4 : 8);

        // ---- 按**本键所在区块**取统一档 -
        int tier = BlockTierNow(dc, k->block);

        // ⚠ 文本必须存在**本作用域的局部缓冲**里（不能拿 static 包装的指针）
        wchar_t txtBuf[KEY_TEXT_MAX];
        const wchar_t* txt = KeyTextInto(k, txtBuf, KEY_TEXT_MAX);
        HFONT f = FitKeyFontUnified(dc, i, k, txt, k->w - (int)(6 * GetSystemDpiScale()), tier);

        // ⚠ 网页布局层（IsWebFnLayer）整个不做「随 Shift 换面」
        const BOOL webLayer = IsWebFnLayer();
        wchar_t baseCh = 0, shiftCh = 0;
        if (k->type == K_NORMAL && !webLayer && !(g_fnLayer && FnMap(k->vk) != 0)) {
            baseCh = GetSymForKey(k->vk, FALSE);
            shiftCh = GetSymForKey(k->vk, TRUE);
        }
        // 未按 Shift：双符号显示（数字 + 顶部特殊符号，副符号置灰）
        // 按 Shift：开启“仅显示特殊符号”时只显示顶部符号（不显示数字），关闭时仍显示数字。
        BOOL shiftOn = (g_sh || g_physShift);
        BOOL dual = (baseCh && shiftCh && shiftCh != baseCh);

        if (dual) {
            // ⚠ 符号永远是灰色，别再改回「按 Shift 把符号提成大黑字」。
            if (shiftOn && g_shiftSymbols) {
                wchar_t single[2] = { shiftCh, 0 };
                DrawTextC(dc, k->x, k->y, k->w, k->h, single, f, C_DIM);
            } else if (g_dualSymbols) {
                DrawKeyDual(dc, k->x, k->y, k->w, k->h, baseCh, shiftCh, f, g_f12, textC, C_DIM);
            } else {
                wchar_t single[2] = { baseCh, 0 };
                DrawTextC(dc, k->x, k->y, k->w, k->h, single, f, textC);
            }
        } else if (!DrawKeyLabel(dc, k, f, txt, textC)) {
            DrawTextKey(dc, k, txt, f, textC);
        }
    }
    // 统一档位只对**当前几何与字体**有效：按下 Shift / Fn、改变窗口大小、改配色
    // 都会让键面标签或可用宽变化 ⇒ 下一帧进入 DrawKeys 时重算（见函数开头）。
}

static int HitKey(int x, int y) {
    for (int i = 0; i < g_nk; i++) {
        const KeyDef* k = &g_keys[i];
        if (x >= k->x && x < k->x + k->w && y >= k->y && y < k->y + k->h)
            return i;
    }
    return -1;
}

// ⚠ 帧缓存的重建由 `EnsureKbFrameCache` 的签名比对决定，**与本函数的
static void InvalidateKeyByVk(HWND hWnd, short vk) {
    short alt = 0;
    if (vk == VK_LSHIFT) alt = VK_RSHIFT;
    else if (vk == VK_RSHIFT) alt = VK_LSHIFT;
    else if (vk == VK_LWIN) alt = VK_RWIN;
    else if (vk == VK_RWIN) alt = VK_LWIN;

    for (int i = 0; i < g_nk; i++) {
        const KeyDef* k = &g_keys[i];
        if (k->vk != vk && (alt == 0 || k->vk != alt)) continue;
        // 外扩 2px：圆角/描边/按下态的发光都在键矩形外沿一点点
        // 不留余量会切出一条没重画的边。
        RECT r = { k->x - 2, k->y - 2, k->x + k->w + 2, k->y + k->h + 2 };
        InvalidateRect(hWnd, &r, FALSE);
    }
}

// ========== 主窗口帧缓存（首帧预热 + 动画/重绘零开销呈现） ==========
static HBITMAP   g_kbCacheBmp = 0;
static HDC       g_kbCacheDc = 0;
static RGBQUAD*  g_kbCacheBits = 0;
static int       g_kbCacheRow = 0;
static int       g_kbCacheW = 0, g_kbCacheH = 0;

struct KbFrameSig {
    int w, h, hk, pk, pkFlash, hdrHov, layoutMode, nk, hue, keyIconStyle;
    DWORD themeBg;
    float dpi;
    BOOL sh, ct, al, cp, winKey, physShift, physWin, physNum, fnLayer, showFKeys,
         fnWebLayout, shiftSymbols, lang;
    // ⚠ 左右 Shift 分开进签名（2026-10-10）：physShift 只是两者的「或」
    //   从「按住左」变成「按住右」时或值不变 —— 不分开就会漏重绘。
    BOOL physLShift, physRShift;
    // 布局菜单：打开/悬停变化必须触发重绘（整帧缓存，漏了就「点了没反应」）
    BOOL lmOpen;
    int  lmHover;
    // 数字区显隐（生效值）：之前没进签名，靠「17 个键消失 → nk 变」侥幸触发重绘
    // 窄屏自动收起与手动收起是两条路径，显式纳入才不依赖这个巧合。
    BOOL npHidden;
    // 标题栏「布局」胶囊的显隐：设置页里一拨开关，签名不变就不会重绘
    // 表现为「要拿鼠标碰一下标题栏才刷新」（用户实机反馈）。
    BOOL showNumBtn;
    int  themeStyle;
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
    DrawLayoutMenu(mem);

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
    // 必须零初始化：签名是 memcmp 全字节比较，结构体里有填充字节
    // 留着不确定值会让缓存永远打不中（每帧都全量重绘）。
    KbFrameSig sig = {};
    sig.w = g_ww; sig.h = g_wh;
    sig.hk = g_hk; sig.pk = g_pk; sig.pkFlash = g_pkFlash; sig.hdrHov = g_hdrHov;
    sig.layoutMode = g_layoutMode; sig.nk = g_nk;
    sig.themeBg = g_themeBuf.pageBg;
    sig.hue = g_hue;
    sig.lmOpen = g_lmOpen; sig.lmHover = g_lmHover;
    sig.keyIconStyle = g_keyIconStyle;   // 漏掉这一项 → 切换图标样式后主键盘不刷新（看似"没生效"）
    sig.dpi = (float)GetSystemDpiScale();
    sig.sh = g_sh; sig.ct = g_ct; sig.al = g_al; sig.cp = g_cp;
    sig.winKey = g_winKey; sig.physShift = g_physShift; sig.physWin = g_physWin;
    sig.physLShift = g_physLShift; sig.physRShift = g_physRShift;   // 左右分开（见 KbFrameSig 的说明）
    sig.physNum = g_physNum;             // 实体 NumLock 灯变 → 数字区 Num 键高亮要跟上
    sig.fnLayer = g_fnLayer; sig.showFKeys = g_showFKeys; sig.fnWebLayout = g_fnWebLayout;
    sig.shiftSymbols = g_shiftSymbols; sig.lang = g_lang;
    sig.npHidden = NumpadHidden();
    sig.showNumBtn = g_showNumBtn;
    sig.themeStyle = g_themeStyle;
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

    // 显示前先在隐藏状态下同步完成首帧绘制：DWM 首次合成时窗口内容已就绪
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
            ForgetVerdictHide();        // 用户明确要键盘 → 撤掉上一轮的精判收起备忘
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

// ⚠ 这里**故意不置 g_manualHide** —— 自动呼出是这个键盘的核心功能
static void UserHideKeyboard() {
    // ⚠ 防回弹记录**无条件生效**（原来受 g_afAutoHide 控制，但那个开关已改为
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
#define S_HIT_SHIFTSYM       19   // 常规 Tab：Shift 符号
#define S_HIT_DUAL           27   // 布局 Tab：双符号（键面是否并列显示两个）
#define S_HIT_SHDLOCK       29   // Shift 双击锁定（**布局 Tab** 子项，紧跟「Shift 符号」，2026-10-11 从常规搬来）
#define S_HIT_THEME_DROP     20
#define S_HIT_URL            30
#define S_HIT_LICENSE        32   // 关于 Tab：开源许可行的「查看」按钮
#define S_HIT_DIAG           33   // 关于 Tab：诊断日志行的「导出」按钮
#define S_HIT_CLOSE_DROP     70
#define S_HIT_LANG_DROP      50
#define S_HIT_HL_DROP        60
#define S_HIT_HL_BOX         64
#define S_HIT_HL_HUE         65
#define S_HIT_HL_PAL0        80
#define S_HIT_REMEMBER       95
#define S_HIT_OPACITY_DROP   96
#define S_HIT_STYLE          97   // 主题 Tab：界面风格（现代 / 经典）

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

static void DrawSwitch(HDC dc, int x, int y, int w, int h, BOOL on) {
    DrawRoundRect(dc, x, y, w, h, on ? C_HOT : C_DARK, C_KEY_BORDER, h / 2);
    int knob = h - 6;
    int kx = on ? x + w - knob - 3 : x + 3;
    DrawRoundRect(dc, kx, y + 3, knob, knob, on ? C_KEY : C_DIM, on ? C_KEY : C_DIM, knob / 2);
}

// ========== 设置页度量与布局 ==========
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
    // 顶部留白比左右边距略小即可：原来只有 10 DIP，页头会顶在窗口上沿
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

// ⚠⚠ 布局 Tab 的 index 是**按绘制顺序从 0 数**的，别把注释当行号
static BOOL SettingsRowHidden(int tab, int index) {
    BOOL onlyDefault = (g_layoutMode != 0);
    if (tab == 0) {
        int closeRow = g_af ? 2 : 1;
        // 功能键行：全尺寸也藏（全尺寸常驻 F 行，开关没有意义）
        if (index == closeRow + 2) return onlyDefault;
        // Shift 符号 / Shift 双击锁定：留在常规（2026-10-11 回退搬迁），全尺寸也显示
        return FALSE;
    }
    // 布局 Tab：0=键盘布局 1=Fn 网页布局 2=小键盘按钮 3=双符号
    if (tab == 3) {
        if (index == 1) return (onlyDefault || g_showFKeys);
        // 双符号：小键盘布局没有双符号键 ⇒ 藏
        if (index == 3) return (g_layoutMode == 1);
        return FALSE;
    }
    return FALSE;
}

// 每行的「控件高」，单位 DIP（不乘 dpi）：开关 26 / 下拉与分段 40；无控件行给 0
static int SettingsRowCtrlDip(int tab, int index) {
    if (tab == 0) {                                 // 常规
        int closeRow = g_af ? 2 : 1;
        if (index == closeRow) return 40;           // 关闭按钮下拉
        if (index == closeRow + 5) return 40;       // 界面语言下拉
        return 26;
    }
    if (tab == 3) {                                 // 布局
        if (index == 0) return 40;                  // 键盘布局分段
        return 26;
    }
    if (tab == 1) return 40;                        // 主题：模式 / 透明度 / 色相 都是下拉行
    return 0;
}

// 描述**可能**需要两行的行（文案本身长，窗口窄时一行放不下会被硬截断）
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

// ⚠ 全长度必须同单位再乘 dpi：这里曾经写成 `m.rowPadY * 2 + content`
static int SettingsRowHeight(const SettingsMetrics& m, int index) {
    if (SettingsRowHidden(g_sTab, index)) return 0;      // 见 SettingsRowHidden：后续行随之上移
    if (g_sTab == 1 && index == 2 && !g_wallpaperAccent) return (int)(210 * m.dpi);
    int contentDip = SettingsRowCtrlDip(g_sTab, index);
    if (contentDip < 30) contentDip = 30;   // 图标 tile
    // 文字块：标题 18 + 间距 6 + 描述 16 = 40。间距不能省 —— 标题的下缘和描述的上缘
    // 会顶在一起（实机反馈「文本和描述的间距对吗」）；文字蒙版本身还上下各留了 4px。
    if (contentDip < 40) contentDip = 40;
    // 描述真会折成两行时内容高 40 → 58（多 18 DIP）。原来这里是「可能折行就留」
    // 于是中文单行的「小键盘按钮」行高 80 DIP、内容只占 52，描述下方空 30 DIP。
    if (SettingsRowDescTwoLines(m, g_sTab, index) && contentDip < 58)
        contentDip = 58;                    // 标题 20 + 间距 6 + 描述两行 32（与 DrawSettingRowContent 的块高一致）
    return (int)((12 + contentDip + 12) * m.dpi);
}

static int SettingsRowCount(int tab) {
    // ⚠⚠ 2026-10-04 修：原来返回 `g_af ? 7 : 6`，**漏算了「Shift 双击锁定」那一行**。
    if (tab == 0) return g_af ? 8 : 7;
    if (tab == 3) return 4;   // 布局 Tab：键盘布局 / Fn 网页布局 / 小键盘按钮
    if (tab == 1) return 4;
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

// ⚠ 折叠行必须用**整行高**，不能写死 `rowPadY*2 + 40`（= 64 DIP）。
static int SettingsRowHeadH(const SettingsMetrics& m, const RECT& row) {
    int headH = m.rowPadY * 2 + (int)(40 * m.dpi);
    // 色相可展开行：标题区固定 64 DIP，展开区坐标是相对 row.top 的硬编码（见上方说明）。
    if (g_sTab == 1) return headH;
    // ⚠⚠ 折叠行返回**整行高 rowH**，不要写成 `headH > rowH ? headH : rowH` 之类的
    return row.bottom - row.top;
}

static int SettingsComboY(const SettingsMetrics& m, const RECT& row) {
    return row.top + (SettingsRowHeadH(m, row) - m.comboH) / 2;
}

// 与 SettingsComboY 等价：头部高 = rowPadY*2 + comboH，在头部里居中后的偏移正好是 rowPadY。
static int SettingsComboYTop(const SettingsMetrics& m, const RECT& row) {
    return row.top + m.rowPadY;
}

// 开关行的文字块右界：给「开/关」文字（34）+ 开关（46）让位
// 否则长描述会压到开关上（标签宽度按控件自适应，不写死）
static int SettingsSwitchTextRight(const SettingsMetrics& m, const RECT& row) {
    return row.right - (int)(20 * m.dpi) - m.switchW - (int)(42 * m.dpi);
}

// 行悬停底色：铺满整行、左右直接贴到卡片边缘（不再内缩，否则会切到图标 tile）
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
    // ⚠ 不能只靠 iconId<0 表示：DrawIconTile 在 iconId<0 且无文字时仍会画一个
    if (row.bottom <= row.top) return;

    if (hover) DrawSettingsRowHover(dc, m, row);

    int headH = SettingsRowHeadH(m, row);
    int ty = row.top + (headH - m.tileSize) / 2;
    if (!noTile)
        DrawIconTile(dc, SettingsRowTileX(m), ty, m.tileSize, (int)(17 * m.dpi), iconId, glyph);

    int tx = SettingsRowTextX(m);
    int rightLimit = (ctrlLeft > 0) ? ctrlLeft - (int)(12 * m.dpi) : row.right - (int)(20 * m.dpi);
    int tw = rightLimit - tx;
    if (tw < (int)(60 * m.dpi)) tw = (int)(60 * m.dpi);

    int needTitle = MeasureTextW(dc, title, g_sfRow);
    if (ctrlLeft > 0 && tw < needTitle) {
        int hard = ctrlLeft - tx;
        if (hard > tw) tw = hard;
    }

    int blockBottom = descWrap ? ((int)(26 * m.dpi) + (int)(32 * m.dpi))
                               : ((int)(26 * m.dpi) + (int)(16 * m.dpi));
    int textMid = blockBottom / 2;
    int tyText = ty + m.tileSize / 2 - textMid;

    DrawTextL(dc, tx, tyText, tw, (int)(20 * m.dpi), title, g_sfRow, C_WHITE);
    if (desc && desc[0]) {
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
static int StyleSegItems(const wchar_t** out) {
    static const wchar_t* zh[2] = { L"\u73B0\u4EE3", L"\u7ECF\u5178" };   // 现代 / 经典
    static const wchar_t* en[2] = { L"Modern", L"Classic" };
    for (int i = 0; i < 2; i++) out[i] = g_lang ? en[i] : zh[i];
    return 2;
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

// 「按键图标样式」行已下线（实机反馈：只保留「图标+文字」）；g_keyIconStyle 固定为 2
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
    int rowIndex;
    if (hit == S_HIT_AUTO) rowIndex = 0;
    else if (hit == S_HIT_AUTOHIDE) rowIndex = 1;
    else if (hit == S_HIT_FNWEB) rowIndex = 1;
    else if (hit == S_HIT_NPBTN) rowIndex = 2;
    else if (hit == S_HIT_REMEMBER) rowIndex = g_af ? 3 : 2;
    else if (hit == S_HIT_FKEYS) rowIndex = g_af ? 4 : 3;
    // 2026-10-11：Shift 符号 / Shift 双击锁定 从常规 Tab 搬到**布局 Tab**（行号 3 / 4）。
    // 原来的 2026-10-04 那条注释（"+4 落到 else"）随搬迁作废，别再按老行号推。
    else if (hit == S_HIT_SHIFTSYM) rowIndex = g_af ? 5 : 4;
    else if (hit == S_HIT_SHDLOCK)  rowIndex = g_af ? 6 : 5;
    else if (hit == S_HIT_DUAL)     rowIndex = 3;
    else if (hit == S_HIT_LANG_DROP) rowIndex = g_af ? 7 : 6;
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
// ⚠ 上下内边距是 **14 不是 10**：制作工具的许可卡高 78 DIP、许可行 46，反推出 padY = 16
static int AboutPadY(const SettingsMetrics& m) { return (int)(14 * m.dpi); }      // .card-pad 上下
static int AboutPadX(const SettingsMetrics& m) { return (int)(20 * m.dpi); }      // .card-pad 左右
static int AboutGap(const SettingsMetrics& m) { return (int)(6 * m.dpi); }        // 卡与卡之间的间距

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

// 许可 / 诊断行：上 12 + 按钮 32 + 下 2。
// 卡高 = 上下 padY + **两行** + 中间 1px 分隔线（第 2 行是「诊断日志 / 导出」）。
static int AboutLicenceRowH(const SettingsMetrics& m) {
    return m.rowPadY + (int)(32 * m.dpi) + (int)(2 * m.dpi);
}
static int AboutLicenceCardH(const SettingsMetrics& m) {
    return AboutPadY(m) * 2 + AboutLicenceRowH(m) * 2 + 1;
}

// index：0 = 开源许可，1 = 诊断日志
static RECT AboutLicenceRowRect(const SettingsMetrics& m, const RECT& card, int index) {
    int top = card.top + AboutPadY(m) + (index == 0 ? 0 : AboutLicenceRowH(m) + 1);
    RECT r = {card.left, top, card.right, top + AboutLicenceRowH(m)};
    return r;
}

static int AboutBtnW(const wchar_t* label, double dpi) {
    HDC dc = GetDC(0);
    int adv = MeasureTextAdvW(dc, label, g_sfCtrl);
    if (adv <= 0) adv = MeasureTextW(dc, label, g_sfCtrl);   // GDI+ 未就绪时的回退
    ReleaseDC(0, dc);
    return adv + (int)(28 * dpi);
}

static RECT AboutLicenceBtnRect(const SettingsMetrics& m, int rowTop, const wchar_t* label) {
    int w = AboutBtnW(label, m.dpi);
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

// ⚠ 高亮**只作用在圆钮上，不铺整行**（实测制作工具：链接卡整卡纯白 (255,255,255)
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

    int tx = row.left + AboutPadX(m);
    int tw = bx - (int)(12 * m.dpi) - tx;
    int blockH = (int)(36 * m.dpi);
    int tyText = row.top + ((row.bottom - row.top) - blockH) / 2;
    DrawTextL(dc, tx, tyText, tw, (int)(15 * m.dpi), title, g_sfCtrl, C_WHITE);
    if (desc && desc[0])
        DrawTextL(dc, tx, tyText + (int)(23 * m.dpi), tw, (int)(13 * m.dpi), desc, g_sfBase, C_DIM);
}

static void DrawAboutActionRow(HDC dc, const SettingsMetrics& m, const RECT& row, BOOL hover,
                               const wchar_t* label, const wchar_t* value, const wchar_t* btnLabel) {
    int labelW = (int)(110 * m.dpi);
    int lx = row.left + AboutPadX(m);
    RECT btn = AboutLicenceBtnRect(m, row.top, btnLabel);
    int bw = btn.right - btn.left, bh = btn.bottom - btn.top;

    DrawRoundRect(dc, btn.left, btn.top, bw, bh, hover ? C_REGULAR_HOV : C_REGULAR,
                  hover ? C_REGULAR_HOV : C_REGULAR, (int)(8 * m.dpi));
    DrawTextC(dc, btn.left, btn.top, bw, bh, btnLabel, g_sfCtrl, C_BTN_CONTENT);

    DrawTextL(dc, lx, row.top, labelW, row.bottom - row.top,
              label, g_sfCtrl, C_WHITE);
    int vx = lx + labelW + (int)(18 * m.dpi);
    DrawTextL(dc, vx, row.top, btn.left - (int)(12 * m.dpi) - vx, row.bottom - row.top,
              value, g_sfCtrl, C_HOT);
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
        // 行序（自动收起行仅在自动呼出开启时存在，隐藏时后续行上移一行）
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
        // ⚠ 守卫**只包住功能键行**。Shift 两行必须留在守卫外面 —— 它们在全尺寸布局下照常显示
        if (!SettingsRowHidden(0, closeRow + 2)) {
        r = SettingsRowRect(m, closeRow + 2);
        DrawSettingRowContent(dc, m, r, -1, L"F",   // 功能键行保留手绘 F
                              T(L"功能键行", L"Function Key Row"),
                              T(L"在键盘顶部显示 F1~F12 和 Del", L"Show F1~F12 and Del above the keyboard"),
                              g_sHov == S_HIT_FKEYS, SettingsSwitchTextRight(m, r));
        DrawSettingSwitch(dc, m, r, g_showFKeys, S_HIT_FKEYS);
        }

        r = SettingsRowRect(m, closeRow + 3);
        DrawSettingRowContent(dc, m, r, HKICON_SHIFT, NULL,
                              T(L"Shift 符号", L"Shift Symbols"),
                              T(L"按下 Shift 后数字键仅显示特殊符号", L"Show only symbols while Shift is held"),
                              g_sHov == S_HIT_SHIFTSYM, SettingsSwitchTextRight(m, r));
        DrawSettingSwitch(dc, m, r, g_shiftSymbols, S_HIT_SHIFTSYM);

        r = SettingsRowRect(m, closeRow + 4);
        // 子项样式（不画图标 tile）：与「Shift 符号」从属，标题左对齐
        DrawSettingRowContent(dc, m, r, -1, NULL,
                              T(L"Shift 双击锁定", L"Shift Double-Tap Lock"),
                              T(L"连按两下 Shift 锁定，再点解除", L"Tap twice to lock, tap again to release"),
                              g_sHov == S_HIT_SHDLOCK, SettingsSwitchTextRight(m, r), FALSE, TRUE);
        DrawSettingSwitch(dc, m, r, g_shiftDoubleTap, S_HIT_SHDLOCK);

        r = SettingsRowRect(m, closeRow + 5);
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

        if (!SettingsRowHidden(3, 3)) {                  // 小键盘下不存在
        r = SettingsRowRect(m, 3);
        DrawSettingRowContent(dc, m, r, HKICON_SHIFT, NULL,
                              T(L"双符号", L"Both Symbols"),
                              T(L"键面上下并列显示主符号与副符号", L"Stack both symbols on the key face"),
                              g_sHov == S_HIT_DUAL, SettingsSwitchTextRight(m, r));
        DrawSettingSwitch(dc, m, r, g_dualSymbols, S_HIT_DUAL);
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
            // 输入光标：自绘输入框没有系统 caret，得自己画（530ms 亮灭，跟系统一致）
            if (g_hlEditFocus && ((GetTickCount() / 530) & 1) == 0) {
                int cx = input.left + (int)(12 * m.dpi) + MeasureTextAdvW(dc, shown, g_sfBase);
                int ch = inputH / 2;
                if (cx > input.right - (int)(8 * m.dpi)) cx = input.right - (int)(8 * m.dpi);
                Fill(dc, cx, input.top + (inputH - ch) / 2, 1, ch, C_HOT);
            }
        }
        // 界面风格（B1）：现代 / 经典
        r = SettingsRowRect(m, 3);
        { const wchar_t* st[2]; int stn = StyleSegItems(st);
          RECT stR = RowSegRect(m, r, st, stn);
          DrawSettingRowContent(dc, m, r, HKICON_CONTRAST, NULL,
                                T(L"界面风格", L"Interface Style"),
                                T(L"灰色键面 + 4px 圆角；按下与锁定仍用主题色相高亮",
                                  L"Grey keys, 4px corners; presses and locks still use the accent hue"),
                                g_sHov == S_HIT_STYLE, stR.left);
          DrawSegmented(dc, m, stR, st, stn, g_themeStyle); }

    } else {
        AboutLayout al = GetAboutLayout(m);
        DrawRoundRect(dc, al.card1.left, al.card1.top,
                      al.card1.right - al.card1.left, al.card1.bottom - al.card1.top,
                      C_KEY, C_KEY, (int)(16 * m.dpi));

        // ⚠ hole 色必须与 fg 不同（这里传 C_ON_PRIMARY = primaryFg 浅色下的深色正文色）
        int my = al.card1.top + (al.card1.bottom - al.card1.top - al.mark) / 2;
        // ⚠ 文字起点用 **13.3 DIP**（原 18）：al.mark 是 56 DIP 的**绘制网格**，而
        DrawHkIcon(dc, (float)(al.card1.left + AboutPadX(m)), (float)my, (float)al.mark,
                   HkIcon(HKICON_KEYBOARDMARK), C_HOT, C_ON_PRIMARY);

        // ⚠ 文字块高度是 **49 DIP**（标题盒 28 + 间隙 3 + 描述盒 18），不是 53。
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
        DrawAboutActionRow(dc, m, AboutLicenceRowRect(m, al.card3, 0), g_sHov == S_HIT_LICENSE,
                           T(L"开源许可", L"Licence"), L"MIT", T(L"查看", L"View"));
        // 诊断行：把「导出诊断日志」放到设置页里（托盘右键也有一份）。
        // 自动呼出出问题时用它导出最近约 3 分钟的运行记录 + 环境/UIA 快照。
        DrawAboutActionRow(dc, m, AboutLicenceRowRect(m, al.card3, 1), g_sHov == S_HIT_DIAG,
                           T(L"诊断日志", L"Diagnostics"),
                           T(L"自动呼出与 UIA 状态排查", L"Auto pop-up / UIA troubleshooting"),
                           T(L"导出", L"Export"));

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

        if (!SettingsRowHidden(0, closeRow + 2)) {       // 全尺寸 / 小键盘下这行不存在
            r = SettingsRowRect(m, closeRow + 2);
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_FKEYS;
        }

        r = SettingsRowRect(m, closeRow + 3);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_SHIFTSYM;
        r = SettingsRowRect(m, closeRow + 4);
        if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_SHDLOCK;

        r = SettingsRowRect(m, closeRow + 3);
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

        if (!SettingsRowHidden(3, 3)) {
            r = SettingsRowRect(m, 3);
            if (x >= r.left && x < r.right && y >= r.top && y < r.bottom) return S_HIT_DUAL;
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
        r = SettingsRowRect(m, 3);
        { const wchar_t* st[2]; int stn = StyleSegItems(st);
          RECT sr = RowSegRect(m, r, st, stn);
          if (x >= sr.left && x < sr.right && y >= sr.top && y < sr.bottom) return S_HIT_STYLE; }

    } else {
        // 关于 tab：两个链接行整行可点（卡片位置与绘制同源）
        // 许可 / 诊断行的热区只有右侧按钮本身 —— 行内的文字是读数，不是动作
        AboutLayout al = GetAboutLayout(m);
        RECT r0 = AboutRowRect(m, al.card2, 0);
        if (x >= r0.left && x < r0.right && y >= r0.top && y < r0.bottom) return S_HIT_URL;
        RECT rb = AboutLicenceBtnRect(m, AboutLicenceRowRect(m, al.card3, 0).top,
                                      T(L"查看", L"View"));
        if (x >= rb.left && x < rb.right && y >= rb.top && y < rb.bottom) return S_HIT_LICENSE;
        RECT rd = AboutLicenceBtnRect(m, AboutLicenceRowRect(m, al.card3, 1).top,
                                      T(L"导出", L"Export"));
        if (x >= rd.left && x < rd.right && y >= rd.top && y < rd.bottom) return S_HIT_DIAG;
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
    IniSetInt(L"General", L"DualSymbols", 1);
    IniSetInt(L"General", L"Language", 0);
    IniSetInt(L"General", L"AutoPopup", 1);
    IniSetInt(L"General", L"AutoHide", 1);
    IniSetInt(L"General", L"ChromiumPrecise", 1);   // Chromium 系精判收起（新增，见该开关的说明）
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
    g_windowSize = IniGetInt(L"Keyboard", L"WindowSize", 0);
    if (g_windowSize < 0) g_windowSize = 0;
    if (g_windowSize > 3) g_windowSize = 3;
    g_fnWebLayout = (IniGetInt(L"Keyboard", L"FnWebLayout", 0) != 0);
    // 键面固定「图标+文字」：不再提供「仅文字」模式（实机反馈），设置页也没有对应行了。
    // 这里仍然读一次 ini 只为了让老配置不残留（值一律归一到 2）。
    g_keyIconStyle = 2;                          // 老配置里残留的 0 / 1 一并归一
    g_shiftSymbols = (IniGetInt(L"General", L"ShiftSymbols", 1) != 0);
    g_dualSymbols = (IniGetInt(L"General", L"DualSymbols", 1) != 0);
    g_themeStyle = IniGetInt(L"Theme", L"Style", 0);   // 0=现代 1=经典（B1）   // 默认开：键面并列显示两个符号
    g_shiftDoubleTap = (IniGetInt(L"General", L"ShiftDoubleTap", 0) != 0);
    g_hideDelayMs = 300;    // 失焦后的自动隐藏延迟（见定义处说明）
    g_lang = IniGetInt(L"General", L"Language", 0);
    if (g_lang < 0 || g_lang > 1) g_lang = 0;
    g_af = (IniGetInt(L"General", L"AutoPopup", 1) != 0);
    g_afAutoHide = (IniGetInt(L"General", L"AutoHide", 1) != 0);
    // Chromium 系「精判收起」开关（默认开）。关掉就回到"Chromium 一在前台就不收"的旧行为。
    // ⚠ 只影响收起，不影响呼出 —— 见 g_afChromiumPrecise 的说明。
    g_afChromiumPrecise = (IniGetInt(L"General", L"ChromiumPrecise", 1) != 0);
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
    KillTimer(hWnd, TIMER_CARET);
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
        IniSetInt(L"Keyboard", L"FKeys", g_showFKeys ? 1 : 0);
        layoutChanged = TRUE;   // 多/少一行 ⇒ 窗口高度要跟着变（见 InitWindowSizeForDpi）
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
    case S_HIT_DUAL:
        // 布局 Tab：键面是否上下并列显示两个符号（主键盘立即重绘）
        BeginSwitchAnimation(hWnd, hit, g_dualSymbols, !g_dualSymbols);
        g_dualSymbols = !g_dualSymbols;
        IniSetInt(L"General", L"DualSymbols", g_dualSymbols ? 1 : 0);
        if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, TRUE);
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
    case S_HIT_SHDLOCK:
        BeginSwitchAnimation(hWnd, hit, g_shiftDoubleTap, !g_shiftDoubleTap);
        g_shiftDoubleTap = !g_shiftDoubleTap;
        // 关掉时清掉锁定/一次性态，否则会残留一个"看不见的锁定"
        if (!g_shiftDoubleTap) { g_shiftLock = FALSE; g_shiftOnce = FALSE; g_sh = FALSE; }
        IniSetInt(L"General", L"ShiftDoubleTap", g_shiftDoubleTap ? 1 : 0);
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
                SetTimer(hWnd, TIMER_CARET, 530, NULL);   // 光标闪烁
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
    case S_HIT_DIAG: {
        // 关于 Tab / 托盘右键都有这个入口：导出最近约 3 分钟的运行记录
        // 外加环境快照与 UIA 自检结果（见 ExportDiagLog）。
        ExportDiagLog();
        break;
    }
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
    if (hit == S_HIT_STYLE) {
        SettingsMetrics sm = GetSettingsMetrics(hWnd);
        RECT r3 = SettingsRowRect(sm, 3);
        const wchar_t* st[2]; int stn = StyleSegItems(st);
        RECT sr = RowSegRect(sm, r3, st, stn);
        int idx = SegmentedHitIndex(sm, sr, st, stn, x);
        if (idx >= 0 && idx != g_themeStyle) {
            g_themeStyle = idx;
            IniSetInt(L"Theme", L"Style", g_themeStyle);
            RefreshThemeAndRepaint(hWnd);   // 主键盘 / 设置页 / 关闭提示窗一起刷
        }
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
        {
            RECT cr; GetClientRect(hWnd, &cr);
            int want = SettingsDesiredHeight(hWnd);
            if (want > cr.bottom + 2) {
                RECT wr; GetWindowRect(hWnd, &wr);
                SetWindowPos(hWnd, NULL, 0, 0, wr.right - wr.left, want,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
                ValidateRect(hWnd, NULL);
                return 0;
            }
        }
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        WindowPaintSurfaceLocal surface = BeginWindowPaintSurface(dc, hWnd, rc);
        ClearWindowBackBuffer(surface.dc, hWnd, rc.right, rc.bottom);
        SettingsDraw(surface.dc, hWnd);
        BitBlt(dc, 0, 0, rc.right, rc.bottom, surface.dc, 0, 0, SRCCOPY);
        // 注：内容高于客户区时的自动长高在 WM_PAINT 开头处理（见 SettingsFitHeight 的说明）
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
            if (hv == S_HIT_URL || hv == S_HIT_LICENSE || hv == S_HIT_DIAG) {
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

// ⚠ 卡片高**必须含上下 rowPadY**。原来漏成 rowH*2+1 = 109，让行 2 底超出
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

    // 窗口底：**pageBg**（淡紫），与设置页页面底同一层
    // 白卡片浮在它上面构成层级。原来的纯白底把卡片「吸」进去了，看着像一张大表格。
    DrawRoundRect(dc, 0, 0, W, H, C_BG, C_BG, (int)(14 * dpi));

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

    // ---- 行 1：选择关闭方式（描述已按要求去掉，标题在行盒内垂直居中）-
    {
        int ry = L.segRowY + (L.rowH - L.tile) / 2;
        DrawRoundRect(dc, L.cardX + (int)(20 * dpi), ry, L.tile, L.tile,
                      C_REGULAR, C_REGULAR, (int)(8 * dpi));
        int isz = (int)(15 * dpi);
        DrawHkIcon(dc, (float)(L.cardX + (int)(20 * dpi) + (L.tile - isz) / 2),
                   (float)(ry + (L.tile - isz) / 2), (float)isz,
                   HkIcon(HKICON_CLOSE), C_BTN_CONTENT, C_BTN_CONTENT);
        // ⚠ 没有描述了，标题必须按**整个行盒**居中（不是 titleH2 居中后再偏上）
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

    // ---- 行 2：记住我的选择（描述已按要求去掉）-
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
    // ⚠ 与 PromptDraw 读**同一个** PromptComputeLayout —— 原来两边各算一遍
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
    // ⚠ 宽度按**实机字宽**（MiSans 11pt）反推。描述去掉后约束变成了分段控件
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
    // “显示轻键”仅在自动呼出关闭且键盘隐藏时提供
    // 自动呼出开启时键盘由呼出逻辑自动管理，无需手动呼出入口
    if (!g_af && !g_vis) {
        AppendMenuW(m, MF_STRING, ID_MENU_TOGGLE, T(L"\x663E\x793A\x8F7B\x952E", L"Show Keyboard"));
    }

    // 自动呼出：菜单勾选项（主界面不再显示开关按钮）
    AppendMenuW(m, MF_STRING | (g_af ? MF_CHECKED : 0), ID_MENU_AUTO, T(L"\x81EA\x52A8\x547C\x51FA", L"Auto Pop-up"));

    AppendMenuW(m, MF_STRING, ID_MENU_SETTINGS, T(L"\x8BBE\x7F6E", L"Settings"));

    AppendMenuW(m, MF_SEPARATOR, 0, NULL);
    AppendMenuW(m, MF_STRING, ID_MENU_DIAG, T(L"\x5BFC\x51FA\x8BCA\x65AD\x65E5\x5FD7", L"Export Diagnostics"));
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
    } else if (id == ID_MENU_DIAG) {
        ExportDiagLog();
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

static BOOL AccessibleHasEditableFocus(IAccessible* acc, int depth, ULONG_PTR* token) {
    // ⚠ 深度上限 3 → 6（2026-10-04）：Chrome 系的 a11y 树很深
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
        if (focus.lVal == 0) return FALSE;
        VARIANT varId;
        ZeroMemory(&varId, sizeof(varId));
        varId.vt = VT_I4;
        varId.lVal = focus.lVal;              // 要取的元素 id
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

// ⚠⚠ 存在的理由（用户实测的「自动呼出卡死」）
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

static BOOL IsShellSurfaceClass(const char* cls) {
    if (!cls) return FALSE;
    return strstr(cls, "Shell_") || strstr(cls, "Progman") || strstr(cls, "WorkerW") ||
           strstr(cls, "Taskbar") || strstr(cls, "TrayNotify") || strstr(cls, "MSTaskSwWClass") ||
           strstr(cls, "NotifyIconOverflowWindow") || strstr(cls, "XamlExplorerHost") ||
           strstr(cls, "ShellExperienceHost") ||
           strstr(cls, "SearchHost") || strstr(cls, "StartMenu") ||
           strstr(cls, "TopLevelWindowForOverflowXamlIsland");
}

// ===== Windows 自带触摸键盘（Win10 与 Win11 形态不同）=====
// ⚠ 只做判定上的忽略，**不主动关掉系统键盘**（用户明确要求不动它）。
typedef BOOL (WINAPI *QueryFullProcessImageNameWProc)(HANDLE, DWORD, LPWSTR, PDWORD);
static BOOL WindowProcessName(HWND h, wchar_t* out, int cch) {
    if (out) out[0] = 0;
    if (!h || !IsWindow(h) || !out || cch < 2) return FALSE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (!pid) return FALSE;
#ifndef PROCESS_QUERY_LIMITED_INFORMATION
#define PROCESS_QUERY_LIMITED_INFORMATION 0x1000
#endif
    HANDLE p = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!p) return FALSE;
    static QueryFullProcessImageNameWProc proc = NULL;
    static BOOL tried = FALSE;
    if (!tried) {
        tried = TRUE;
        proc = (QueryFullProcessImageNameWProc)GetProcAddress(
                   GetModuleHandleW(L"kernel32.dll"), "QueryFullProcessImageNameW");
    }
    BOOL ok = FALSE;
    if (proc) { DWORD n = (DWORD)cch; ok = proc(p, 0, out, &n); }
    CloseHandle(p);
    if (!ok) { out[0] = 0; return FALSE; }
    wchar_t* sl = wcsrchr(out, L'\\');
    if (sl) memmove(out, sl + 1, (wcslen(sl + 1) + 1) * sizeof(wchar_t));
    return TRUE;
}

static BOOL IsSystemTouchKeyboard(HWND h) {
    if (!h || !IsWindow(h)) return FALSE;
    char cls[128] = {0};
    GetClassNameA(h, cls, 128);
    if (strstr(cls, "IPTip_Main_Window")) return TRUE;          // Win10
    if (strstr(cls, "Windows.UI.Core.CoreWindow") ||
        strstr(cls, "InputSite") || strstr(cls, "TextInputHost")) {
        wchar_t exe[64] = {0};
        if (WindowProcessName(h, exe, 64)) {
            if (_wcsicmp(exe, L"TextInputHost.exe") == 0) return TRUE;   // Win11
            if (_wcsicmp(exe, L"TabTip.exe") == 0) return TRUE;
        }
    }
    return FALSE;
}

// 上一个「真正的」前台窗口（跳过系统触摸键盘）。
// 触摸键盘抢前台时不更新它，于是自动呼出的判定继续对着底下那个应用。
static HWND g_lastRealFg = NULL;
static char g_dbgLastFgCls[64] = {0};
static HWND RealForegroundWindow() {
    HWND fg = GetForegroundWindow();
    if (!fg) return NULL;

    if (g_afLogPath[0]) {
        char cls[64] = {0};
        GetClassNameA(fg, cls, 64);
        if (strcmp(cls, g_dbgLastFgCls) != 0) {
            strncpy_s(g_dbgLastFgCls, sizeof(g_dbgLastFgCls), cls, _TRUNCATE);
            char buf[112];
            _snprintf_s(buf, 112, _TRUNCATE, "fg class -> %s", cls);
            AfNote(buf);
        }
    }

    if (IsSystemTouchKeyboard(fg))
        return (g_lastRealFg && IsWindow(g_lastRealFg)) ? g_lastRealFg : fg;
    g_lastRealFg = fg;
    return fg;
}

// ===== 会话级「用户操作过」判定（触屏必需，2026-10-10）=====
// ⚠ 判据是「**刚刚**有过一次输入」，不绑定"这一次前台变化"。
static DWORD g_touchTapTick = 0;         // 最近一次"触控式点击"（有输入且光标未动）
static DWORD g_touchInputTick = 0;       // 上一次采样到的 GetLastInputInfo.dwTime
static POINT g_touchCursor = {0, 0};
static BOOL  g_touchCursorInit = FALSE;

// 每 tick 调一次。检测到"触控式点击"就刷时间戳；byUserInFg 用它回答"用户刚动过没有"。
static void TouchInputTick() {
    if (!IsTouchDevice()) return;
    LASTINPUTINFO li = {sizeof(li)};
    if (!GetLastInputInfo(&li)) return;
    POINT p = {0, 0};
    if (!GetCursorPos(&p)) return;
    if (g_touchCursorInit) {
        BOOL newInput  = (li.dwTime != g_touchInputTick);   // 又有输入了
        BOOL cursorStill = (p.x == g_touchCursor.x && p.y == g_touchCursor.y);
        if (newInput && cursorStill) g_touchTapTick = GetTickCount();
    }
    g_touchCursorInit = TRUE;
    g_touchInputTick = li.dwTime;
    g_touchCursor = p;
}

// 「用户刚在这台机器上点了一下（触控/笔）」—— 只对触屏设备可能为真。
//   1500ms 的窗口：够人手从"点一下"到"看键盘有没有出来"。
static BOOL TouchTappedRecently() {
    if (!IsTouchDevice() || !g_touchTapTick) return FALSE;
    return (DWORD)(GetTickCount() - g_touchTapTick) <= 1500;
}

//   ⚠ 为什么需要它：触屏拿不到落点（没有鼠标消息），触屏的两条强证据
static DWORD g_touchTapConsumed = 0;
static BOOL TouchTapEdgeConsume() {
    if (!g_touchTapTick || g_touchTapTick == g_touchTapConsumed) return FALSE;
    g_touchTapConsumed = g_touchTapTick;
    return TRUE;
}

// ===== Chromium 系「光标到底在不在输入框里」的精判（UI Automation）=====
// ⚠⚠ 与历史结论的关系（必读，别再重复回退）
#define HK_UIA_CT_EDIT 50004    // UIA_EditControlTypeId
#define HK_UIA_CT_DOC  50030    // UIA_DocumentControlTypeId（诊断用）

static IUIAutomation* g_uia = NULL;

// ⚠ 必须的护栏：contenteditable 型输入框报不出 Edit —— 实测 QQ NT 主窗口里
static HWND g_chromiumTrustFg = NULL;

//   ⚠ 它只是一句备忘："当前这个窗口正处于精判收起态"。
static BOOL g_verdictHide = FALSE;
static HWND g_verdictHidFg = NULL;
static int  g_dbgUiaPt = -1;             // 诊断：最近一次落点查询结果
static int  g_dbgUiaPtCt = -1;           // 诊断：落点元素的 ControlType

static BOOL EnsureUia() {
    if (g_uia) return TRUE;
    // 复用已有的 COM 初始化（oleacc 那条路也是它）—— 重复 CoInitializeEx 会拿到
    // RPC_E_CHANGED_MODE，虽然无害，但没必要。
    if (!EnsureAccessibilityCom()) return FALSE;
    // ⚠ CLSID_CUIAutomation / IID_IUIAutomation 走 __uuidof —— 只取 GUID 常量
    //   不需要 import lib（不要改成 GetCurrentPropertyValue，那会拉进 oleaut32）。
    HRESULT hr = CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER,
                                  __uuidof(IUIAutomation), (void**)&g_uia);
    return SUCCEEDED(hr) && g_uia != NULL;
}

// ⚠⚠ **必须校验"这个元素属于哪个顶层窗口"** —— `GetFocusedElement` 是全局的
static int UiaFocusEditable(HWND fgExpect) {
    g_dbgUiaCt = -1;
    if (!EnsureUia()) return -1;
    //   ⚠ 本函数在 UI 线程上跑，没有这道闸，目标进程一卡死
    if (!IsWindowResponsive(GetForegroundWindow(), 50)) return -1;
    IUIAutomationElement* el = NULL;
    HRESULT hr = g_uia->GetFocusedElement(&el);
    if (FAILED(hr) || !el) return -1;
    // ⚠ 它的出参类型是 `UIA_HWND*`（= `void*`），**不是 `HWND*`** —— 直接传 HWND*
    UIA_HWND elHwndRaw = NULL;
    el->get_CurrentNativeWindowHandle(&elHwndRaw);
    HWND elHwnd = (HWND)elHwndRaw;
    CONTROLTYPEID ct = 0;
    hr = el->get_CurrentControlType(&ct);
    el->Release();          // 只管当场取值，不留跨帧的 COM 指针（会过期）
    if (FAILED(hr)) return -1;
    if (fgExpect && elHwnd) {
        HWND elTop = GetAncestor(elHwnd, GA_ROOT);
        HWND fgTop = GetAncestor(fgExpect, GA_ROOT);
        if (elTop && fgTop && elTop != fgTop) return -1;   // 这不是本窗口的焦点
    }
    g_dbgUiaCt = (int)ct;
    return (ct == HK_UIA_CT_EDIT) ? 1 : 0;
}

//   ⚠ 缓存的是**判定结果**，不是元素指针。
static HWND  g_uiaCacheFg = NULL;
static int   g_uiaCacheVal = -1;
static DWORD g_uiaCacheTick = 0;
static int UiaFocusEditableCached(HWND fg) {
    DWORD now = GetTickCount();
    if (g_uiaCacheFg == fg && g_uiaCacheTick && now - g_uiaCacheTick < 250)
        return g_uiaCacheVal;
    g_uiaCacheFg = fg;
    g_uiaCacheVal = UiaFocusEditable(fg);
    g_uiaCacheTick = now;
    return g_uiaCacheVal;
}

//   ⚠ 不加这个，点进输入框之后的前 250ms 还会拿"点在空白"时的旧结论去收起
static void InvalidateUiaCache() { g_uiaCacheTick = 0; }

// ⚠ 同样**按窗口分组**（理由同上）。
static HWND  g_uiaHoldFg = NULL;
static DWORD g_uiaHoldTick = 0;
static int UiaFocusEditableHeld(HWND fg) {
    DWORD now = GetTickCount();
    int v = UiaFocusEditableCached(fg);
    if (v == 1) { g_uiaHoldFg = fg; g_uiaHoldTick = now; }
    if (g_uiaHoldFg == fg && g_uiaHoldTick && now - g_uiaHoldTick < 400) return 1;
    return v;               // ⚠ 可能是 -1（未知）—— 调用方必须当成"不做决定"
}

// ===== 落点查询：这个屏幕点上是可编辑输入框吗？（UIA ElementFromPoint）=====
// ⚠⚠ 为什么"要再弹"这件事用**落点**而不是**焦点**
static int UiaPointEditable(POINT pt) {
    g_dbgUiaPtCt = -1;
    if (!EnsureUia()) return -1;
    IUIAutomationElement* el = NULL;
    if (FAILED(g_uia->ElementFromPoint(pt, &el)) || !el) return -1;
    CONTROLTYPEID ct = 0;
    HRESULT hr = el->get_CurrentControlType(&ct);
    el->Release();
    if (FAILED(hr)) return -1;
    g_dbgUiaPtCt = (int)ct;
    return (ct == HK_UIA_CT_EDIT) ? 1 : 0;
}

// dwmapi 的 DwmGetWindowAttribute —— 与上面 DwmSetWindowAttribute 一样**动态取**
//   不新增链接依赖（本机没有编译器，arm64 job 上链新的 lib 有翻车史）。
typedef HRESULT (WINAPI *DwmGetWindowAttributeProc)(HWND, DWORD, PVOID, DWORD);
static DwmGetWindowAttributeProc GetDwmGetWindowAttribute() {
    static DwmGetWindowAttributeProc proc = NULL;
    static BOOL tried = FALSE;
    if (!tried) {
        tried = TRUE;
        HMODULE dwm = LoadLibraryW(L"dwmapi.dll");
        if (dwm) proc = (DwmGetWindowAttributeProc)GetProcAddress(dwm, "DwmGetWindowAttribute");
    }
    return proc;
}

//     ⚠⚠ 实测（本机 Win11）：**这个宿主窗口常年 IsWindowVisible=TRUE**
struct TipEnumCtx { BOOL found; };
static BOOL CALLBACK TipEnumProc(HWND h, LPARAM lp) {
    TipEnumCtx* c = (TipEnumCtx*)lp;
    if (!IsWindowVisible(h)) return TRUE;
    char cls[128] = {0};
    GetClassNameA(h, cls, 128);
    if (!strstr(cls, "Windows.UI.Core.CoreWindow") &&
        !strstr(cls, "InputSite") && !strstr(cls, "TextInputHost"))
        return TRUE;
    wchar_t exe[64] = {0};
    if (!WindowProcessName(h, exe, 64)) return TRUE;
    if (_wcsicmp(exe, L"TextInputHost.exe") != 0 && _wcsicmp(exe, L"TabTip.exe") != 0)
        return TRUE;
    DWORD cloaked = 0;
    DwmGetWindowAttributeProc getAttr = GetDwmGetWindowAttribute();
    if (getAttr) {
        const DWORD DWMWA_CLOAKED_VALUE = 14;
        if (SUCCEEDED(getAttr(h, DWMWA_CLOAKED_VALUE, &cloaked, sizeof(cloaked))) && cloaked)
            return TRUE;        // 被 DWM 遮住 ⇒ 没在显示
    }
    c->found = TRUE;
    return FALSE;               // 找到了，停
}

static BOOL g_tipVisible = FALSE;
static DWORD g_tipCheckTick = 0;
static BOOL SystemTouchKeyboardVisible() {
    DWORD now = GetTickCount();
    if (g_tipCheckTick && now - g_tipCheckTick < 100) return g_tipVisible;
    g_tipCheckTick = now;
    BOOL vis = FALSE;
    HWND t = FindWindowW(L"IPTip_Main_Window", NULL);
    if (t && IsWindowVisible(t)) vis = TRUE;
    if (!vis) {
        TipEnumCtx ctx = { FALSE };
        EnumWindows(TipEnumProc, (LPARAM)&ctx);
        vis = ctx.found;
    }
    g_tipVisible = vis;
    return vis;
}

// ⚠ 只对触屏启用：桌面机上点空白同样产生焦点事件，那会退化成"点哪儿都弹"。
static BOOL TouchFocusEvidence() {
    if (!IsTouchDevice()) return FALSE;
    if (!g_lastFocusEvTick) return FALSE;
    if (GetTickCount() - g_lastFocusEvTick > 1200) return FALSE;
    HWND fg = RealForegroundWindow();
    HWND fgTop = fg ? GetAncestor(fg, GA_ROOT) : NULL;
    if (!g_lastFocusEvHwnd || !fgTop) return FALSE;
    HWND evTop = GetAncestor(g_lastFocusEvHwnd, GA_ROOT);
    if (evTop && evTop == fgTop) return TRUE;
    // ⚠ 放宽一档：**同进程**也算。
    DWORD pidEv = 0, pidFg = 0;
    GetWindowThreadProcessId(g_lastFocusEvHwnd, &pidEv);
    GetWindowThreadProcessId(fg, &pidFg);
    return (pidEv != 0 && pidEv == pidFg);
}

static void ForgetVerdictHide() {
    g_verdictHide = FALSE;
    g_verdictHidFg = NULL;
    g_uiaCacheTick = 0;
}

// ===== 导出诊断日志（托盘右键菜单）=====
// ⚠ 不需要带 -afdiag 启动、也不需要"重现前先做准备工作" —— 缓冲一直在跑
static void DiagWrite(HANDLE h, const char* s) {
    DWORD wr = 0;
    WriteFile(h, s, (DWORD)strlen(s), &wr, NULL);
}

// ===== 导出内容之二：环境 + UIA 自检（换机 / 虚拟机排查用）=====
// ⚠ 这一段是给"我拿不到现场"用的：把「钩子装没装上」「UIA 能不能用」
static BOOL IsInputControl(HWND hw);      // 定义在后面（此处需要用）

// 列一个触摸键盘候选窗口（classe 匹配 + 进程名 + 可见性 + DWM cloak + 矩形）
static BOOL CALLBACK DiagTipEnumProc(HWND h, LPARAM lp) {
    HANDLE f = (HANDLE)lp;
    if (!IsWindowVisible(h)) return TRUE;
    char cls[128] = {0};
    GetClassNameA(h, cls, 128);
    if (!strstr(cls, "Windows.UI.Core.CoreWindow") &&
        !strstr(cls, "InputSite") && !strstr(cls, "TextInputHost") &&
        !strstr(cls, "IPTip")) return TRUE;
    wchar_t exe[64] = {0};
    WindowProcessName(h, exe, 64);
    char exeA[64] = {0};
    WideCharToMultiByte(CP_ACP, 0, exe, -1, exeA, sizeof(exeA), NULL, NULL);
    DWORD cloaked = 0;
    DwmGetWindowAttributeProc getAttr = GetDwmGetWindowAttribute();
    if (getAttr) {
        const DWORD DWMWA_CLOAKED_VALUE = 14;
        getAttr(h, DWMWA_CLOAKED_VALUE, &cloaked, sizeof(cloaked));
    }
    RECT r = {0, 0, 0, 0};
    GetWindowRect(h, &r);
    char line[320];
    _snprintf_s(line, sizeof(line), _TRUNCATE,
                "  %-20s cls=[%s] vis=1 cloaked=%lu rect=%ld,%ld %ldx%ld\r\n",
                exeA, cls, (unsigned long)cloaked,
                (long)r.left, (long)r.top, (long)(r.right - r.left), (long)(r.bottom - r.top));
    DiagWrite(f, line);
    return TRUE;
}

static void DiagAppendProbe(HANDLE h) {
    char line[640];
    HWND fg = GetForegroundWindow();

    // ── 钩子是否装上：装不上 = 自动呼出必然失效（权限 / 杀软常见）
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    BOOL elevated = FALSE;
    {
        HANDLE tok = NULL;
        if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) {
            // TokenElevation = 20（Vista+）。本项目目标是 XP，不引它的枚举名/结构名
            // 直接按 DWORD 读（TOKEN_ELEVATION 就一个 DWORD）。
            DWORD elev = 0, cb = 0;
            if (GetTokenInformation(tok, (TOKEN_INFORMATION_CLASS)20, &elev, sizeof(elev), &cb))
                elevated = (elev != 0) ? TRUE : FALSE;
            CloseHandle(tok);
        }
    }
    _snprintf_s(line, sizeof(line), _TRUNCATE,
        "--- hooks / process ---\r\n"
        "mouseHook=%p kbHook=%p winHook=%p fgEvHook=%p   (0/NULL = 装不上)\r\n"
        "elevated=%d  pid=%lu  iniPathSee:exe dir\r\n",
        (void*)g_mouseHook, (void*)g_kbHook, (void*)g_winHook, (void*)g_fgHook,
        (int)elevated, (unsigned long)GetCurrentProcessId());
    DiagWrite(h, line);

    // ── UIA 可用性 + 自检
    HRESULT hrCreate = E_FAIL;
    if (g_uia) {
        hrCreate = S_OK;
    } else if (EnsureAccessibilityCom()) {
        hrCreate = CoCreateInstance(__uuidof(CUIAutomation), NULL, CLSCTX_INPROC_SERVER,
                                    __uuidof(IUIAutomation), (void**)&g_uia);
        if (FAILED(hrCreate)) g_uia = NULL;
    }
    HMODULE uiaDll = LoadLibraryW(L"uiautomationcore.dll");
    _snprintf_s(line, sizeof(line), _TRUNCATE,
        "--- UIA ---\r\n"
        "uiautomationcore.dll = %s\r\n"
        "CoCreateInstance(CUIAutomation) hr=0x%08X  -> %s\r\n"
        "foreground responsive = %d  (0 = 目标进程无响应，判据一律不做决定)\r\n",
        uiaDll ? "found" : "MISSING",
        (unsigned)hrCreate, g_uia ? "OK" : "UIA 不可用 —— 自动收起功能将整体降级为旧行为",
        (int)IsWindowResponsive(fg, 200));
    if (uiaDll) FreeLibrary(uiaDll);
    DiagWrite(h, line);

    if (g_uia) {
        IUIAutomationElement* el = NULL;
        HRESULT hr = g_uia->GetFocusedElement(&el);
        if (SUCCEEDED(hr) && el) {
            UIA_HWND raw = NULL;
            el->get_CurrentNativeWindowHandle(&raw);
            CONTROLTYPEID ct = 0;
            el->get_CurrentControlType(&ct);
            BOOL kf = FALSE, hf = FALSE, off = FALSE;
            el->get_CurrentIsKeyboardFocusable(&kf);
            el->get_CurrentHasKeyboardFocus(&hf);
            el->get_CurrentIsOffscreen(&off);
            // 模式探测：TextPattern(10014) / ValuePattern(10002) / TextPattern2(10024)
            // ⚠ GetCurrentPattern 出参是 IUnknown*，**不碰 VARIANT**（不拉 oleaut32）
            BOOL hasT1 = FALSE, hasV = FALSE, hasT2 = FALSE;
            IUnknown* pat = NULL;
            if (SUCCEEDED(el->GetCurrentPattern(10014, &pat)) && pat) { hasT1 = TRUE; pat->Release(); pat = NULL; }
            if (SUCCEEDED(el->GetCurrentPattern(10002, &pat)) && pat) { hasV = TRUE; pat->Release(); pat = NULL; }
            if (SUCCEEDED(el->GetCurrentPattern(10024, &pat)) && pat) { hasT2 = TRUE; pat->Release(); pat = NULL; }
            HWND elHwnd = (HWND)raw;
            HWND elTop = elHwnd ? GetAncestor(elHwnd, GA_ROOT) : NULL;
            HWND fgTop = fg ? GetAncestor(fg, GA_ROOT) : NULL;
            _snprintf_s(line, sizeof(line), _TRUNCATE,
                "GetFocusedElement hr=0x%08X ct=%lu  (50004=Edit / 50030=Document / 50026=Group)\r\n"
                "  nativeHwnd=%p root=%p  fgRoot=%p  belongsToForeground=%d\r\n"
                "  kbdFocusable=%d hasKbdFocus=%d offscreen=%d  TextPat=%d ValuePat=%d TextPat2=%d\r\n",
                (unsigned)hr, (unsigned long)ct, (void*)elHwnd, (void*)elTop, (void*)fgTop,
                (int)(elTop && fgTop && elTop == fgTop),
                (int)kf, (int)hf, (int)off, (int)hasT1, (int)hasV, (int)hasT2);
            el->Release();
        } else {
            _snprintf_s(line, sizeof(line), _TRUNCATE,
                        "GetFocusedElement hr=0x%08X el=%p  -> 取不到焦点元素\r\n",
                        (unsigned)hr, (void*)el);
        }
        DiagWrite(h, line);

        POINT cp = {0, 0};
        GetCursorPos(&cp);
        int pv = UiaPointEditable(cp);
        _snprintf_s(line, sizeof(line), _TRUNCATE,
            "ElementFromPoint(光标 %ld,%ld) = %d  ct=%d  (1=Edit 0=非Edit -1=查不出)\r\n",
            (long)cp.x, (long)cp.y, pv, g_dbgUiaPtCt);
        DiagWrite(h, line);
    }

    // ── 前台窗口粗判（与 IsInputControl 逐项一致）
    {
        char cls[128] = {0};
        if (fg) GetClassNameA(fg, cls, 128);
        wchar_t exe[64] = {0};
        WindowProcessName(fg, exe, 64);
        char exeA[64] = {0};
        WideCharToMultiByte(CP_ACP, 0, exe, -1, exeA, sizeof(exeA), NULL, NULL);
        DWORD tid = fg ? GetWindowThreadProcessId(fg, NULL) : 0;
        GUITHREADINFO gi = {sizeof(gi)};
        BOOL haveGi = fg ? GetGUIThreadInfo(tid, &gi) : FALSE;
        _snprintf_s(line, sizeof(line), _TRUNCATE,
            "--- foreground ---\r\n"
            "class=[%s] process=[%s]\r\n"
            "shellSurface=%d systemTouchKeyboard=%d\r\n"
            "giOk=%d hwndFocus=%p caret=%p flags=0x%X\r\n"
            "IsInputControl(fg)=%d   coarseChromiumClass=%d\r\n",
            cls, exeA,
            (int)IsShellSurfaceClass(cls), (int)IsSystemTouchKeyboard(fg),
            (int)haveGi, (void*)(haveGi ? gi.hwndFocus : NULL),
            (void*)(haveGi ? gi.hwndCaret : NULL),
            (unsigned)(haveGi ? gi.flags : 0),
            (int)IsInputControl(fg), (int)g_detectedByChromiumClass);
        DiagWrite(h, line);
    }

    // ── 系统触摸键盘（Win10 = TabTip.exe / Win11 = TextInputHost.exe）
    {
        HWND t = FindWindowW(L"IPTip_Main_Window", NULL);
        _snprintf_s(line, sizeof(line), _TRUNCATE,
            "--- touch keyboard ---\r\n"
            "IPTip_Main_Window hwnd=%p visible=%d\r\n"
            "SystemTouchKeyboardVisible()=%d\r\n"
            "候选窗口（可见的）:\r\n",
            (void*)t, (int)(t && IsWindowVisible(t)), (int)SystemTouchKeyboardVisible());
        DiagWrite(h, line);
        EnumWindows(DiagTipEnumProc, (LPARAM)h);
    }

    DiagWrite(h, "------------------------------------------------------------\r\n");
}

// ⚠ 本项目目标是 XP（_WIN32_WINNT=0x0501），**不能用 `RegGetValueA`**（Vista+ 才声明）
static void DiagOsVersion(char* out, int cch) {
    out[0] = 0;
    char product[96] = "?", dv[32] = "?", build[32] = "?", ubr[32] = "";
    HKEY k = NULL;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
                      L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",
                      0, KEY_READ, &k) == ERROR_SUCCESS) {
        struct { const wchar_t* name; char* dst; DWORD cch; } items[] = {
            { L"ProductName",    product, sizeof(product) },
            { L"DisplayVersion", dv,      sizeof(dv) },
            { L"CurrentBuild",   build,   sizeof(build) },
        };
        for (int i = 0; i < 3; i++) {
            wchar_t wv[128] = {0};
            DWORD cb = sizeof(wv), type = 0;
            if (RegQueryValueExW(k, items[i].name, NULL, &type,
                                 (LPBYTE)wv, &cb) == ERROR_SUCCESS &&
                (type == REG_SZ || type == REG_EXPAND_SZ)) {
                WideCharToMultiByte(CP_ACP, 0, wv, -1, items[i].dst,
                                    (int)items[i].cch, NULL, NULL);
            }
        }
        DWORD ubrVal = 0, cb2 = sizeof(ubrVal), type2 = 0;
        if (RegQueryValueExW(k, L"UBR", NULL, &type2, (LPBYTE)&ubrVal,
                             &cb2) == ERROR_SUCCESS && type2 == REG_DWORD)
            _snprintf_s(ubr, sizeof(ubr), _TRUNCATE, "%lu", (unsigned long)ubrVal);
        RegCloseKey(k);
    }
    _snprintf_s(out, cch, _TRUNCATE, "%s %s build %s.%s", product, dv, build, ubr);
}

static void ExportDiagLog() {
    wchar_t self[MAX_PATH * 2] = {0};
    GetModuleFileNameW(NULL, self, MAX_PATH * 2);
    wchar_t* sl = wcsrchr(self, L'\\');
    if (sl) *(sl + 1) = 0;

    SYSTEMTIME st;
    GetLocalTime(&st);
    wchar_t path[MAX_PATH * 2] = {0};
    _snwprintf_s(path, MAX_PATH * 2, _TRUNCATE,
                 L"%sHKeyboard_diag_%04d%02d%02d_%02d%02d%02d.txt",
                 self, st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        MessageBoxW(g_hWnd, L"无法写入日志文件（目录不可写）。", L"导出诊断日志",
                    MB_OK | MB_ICONWARNING);
        return;
    }

    char osv[192] = {0};
    DiagOsVersion(osv, sizeof(osv));
    char selfA[MAX_PATH * 2] = {0};
    WideCharToMultiByte(CP_ACP, 0, self, -1, selfA, sizeof(selfA), NULL, NULL);

    char head[2048];
    _snprintf_s(head, sizeof(head), _TRUNCATE,
        "HKeyboard diagnostic dump\r\n"
        "build          : %s %s\r\n"
        "exported at    : %04d-%02d-%02d %02d:%02d:%02d\r\n"
        "exe dir        : %s\r\n"
        "pid            : %lu\r\n"
        "OS             : %s\r\n"
        "DPI scale      : %.3f\r\n"
        // ⚠ 这三个 SM_* 常量在 _WIN32_WINNT=0x0501 下没有声明（Vista+ 才进 winuser.h）
        //   所以按数值写 —— 与 IsTouchDevice 里的 94/95 同一处理。
        "touch device   : %d (maxTouches=%d, digitizer=0x%X, tabletPC=%d)\r\n"
        "ini AutoPopup  : %d\r\n"
        "ini AutoHide   : %d\r\n"
        "ini ChromPrec  : %d\r\n"
        "layout mode    : %d\r\n"
        "now state      : vis=%d visWnd=%d manualHide=%d manualShow=%d await=%d\r\n"
        "                 verdictHide=%d verdictFg=%p trustFg=%p tabStopVisible=%d\r\n"
        "                 touchTapAgo=%ldms (触屏\"刚点过一下\"距今；-1=本次运行还没点过)\r\n"
        "ring entries   : %d (oldest -> newest)\r\n"
        "------------------------------------------------------------\r\n",
        __DATE__, __TIME__,
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
        selfA, (unsigned long)GetCurrentProcessId(), osv,
        GetSystemDpiScale(),
        (int)IsTouchDevice(), (int)GetSystemMetrics(95),
        (unsigned)GetSystemMetrics(94), (int)GetSystemMetrics(86),
        (int)g_af, (int)g_afAutoHide, (int)g_afChromiumPrecise,
        g_layoutMode,
        (int)g_vis, (int)g_dbgVisWnd, (int)g_manualHide, (int)g_manualShow,
        (int)g_fgAwaitUserInput,
        (int)g_verdictHide, (void*)g_verdictHidFg, (void*)g_chromiumTrustFg,
        (int)SystemTouchKeyboardVisible(),
        g_touchTapTick ? (long)(GetTickCount() - g_touchTapTick) : -1L,
        g_ringCount);
    DiagWrite(h, head);

    // 环境 + UIA 自检（换机 / 虚拟机排查用；见 DiagAppendProbe）
    DiagAppendProbe(h);

    //   ⚠ 兜底补换行：即使某条被 _TRUNCATE 截掉了结尾的 '\n'，也要保证一行一条
    int start = (g_ringCount < HKRING_N) ? 0 : g_ringHead;
    for (int i = 0; i < g_ringCount; i++) {
        const char* line = g_ring[(start + i) % HKRING_N];
        DWORD wr = 0;
        size_t n = strlen(line);
        WriteFile(h, line, (DWORD)n, &wr, NULL);
        if (n == 0 || line[n - 1] != '\n') WriteFile(h, "\r\n", 2, &wr, NULL);
    }
    CloseHandle(h);

    // 打开资源管理器并选中该文件
    wchar_t arg[MAX_PATH * 2 + 32] = {0};
    _snwprintf_s(arg, MAX_PATH * 2 + 32, _TRUNCATE, L"/select,\"%s\"", path);
    ShellExecuteW(NULL, L"open", L"explorer.exe", arg, NULL, SW_SHOWNORMAL);

    wchar_t msg[MAX_PATH * 3] = {0};
    _snwprintf_s(msg, MAX_PATH * 3, _TRUNCATE,
                 L"诊断日志已导出：\n%s\n\n请把该文件发送给开发者。", path);
    MessageBoxW(g_hWnd, msg, L"导出诊断日志", MB_OK | MB_ICONINFORMATION);
}

// ⚠ 取**上升沿**：TabTip 一旦出现会持续可见（电平），直接用会变成
//   "清标记 → 弹出 → 精判又收起 → 还在可见 → 再清 → …" 的振荡。
static BOOL g_sysTipPrev = FALSE;
static BOOL SysTipJustAppeared() {
    BOOL tip = SystemTouchKeyboardVisible();
    BOOL edge = (tip && !g_sysTipPrev);
    g_sysTipPrev = tip;
    return edge;
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

    // ⚠⚠ 2026-10-04 第十一轮：这里曾改成"由 a11y 精判说话"，**连续两次回归**
    if (strstr(buf, "Chrome_RenderWidgetHostHWND") || strstr(buf, "Chrome_WidgetWin")) {
        // ⚠ 这是**粗判**：只说明"前台是 Chromium 窗口"，不含"光标在输入框里"这层信息。
        g_detectedByChromiumClass = TRUE;
        return TRUE;
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
    g_detectedByChromiumClass = FALSE;   // 由 IsInputControl 的 Chromium 分支置位（见那里）
    // ⚠ 用 RealForegroundWindow 而不是 GetForegroundWindow：Windows 自带触摸键盘
    //   弹出时会成为前台窗口，直接拿它去探测必然判不出输入框（见 RealForegroundWindow）。
    HWND fg = RealForegroundWindow();
    if (!fg || IsOwnForegroundWindow(fg)) return NULL;

    // 系统外壳界面（桌面/任务栏/开始菜单/托盘弹窗等）直接跳过
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
        // ⚠ 2026-10-04 第十一轮：Chrome / Electron 系的 a11y 精判**整体移除**。
        if (IsInputControl(focus)) {
            g_detectedInputToken = (ULONG_PTR)focus;
            // ⚠ 纯验证用，**不影响返回值**。仅在诊断模式 + 用户操作窗口内
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
    ULONG_PTR token = 0;
    if (IsAccessibleInputWindow(focus, &token) || (focus != fg && IsAccessibleInputWindow(fg, &token))) {
        g_detectedInputToken = token ? token : (ULONG_PTR)focus;
        return focus;
    }
    return NULL;
}

static void UpdateAutoVisibility() {
    if (!g_af || !g_hWnd) return;
    // why：本拍为什么这么判（进日志）。每拍重设，任何 return 之前都要写。
    g_dbgWhy = "?";
    g_dbgEvFoc = -1;    // -1 = 本拍没评估
    g_dbgEvTip = -1;
    // 启动预热期内不评估（见 g_appStartTick 的说明）：此刻前台状态还没稳定
    // 判断不可靠，容易在没输入框的地方误弹键盘并连带卡住状态。
    if (g_appStartTick && GetTickCount() - g_appStartTick < AUTOSHOW_WARMUP_MS) { g_dbgWhy = "warmup"; return; }

    // ⚠ 设置窗（或关闭提示窗）开着时**挂起自动显隐**：用户此刻在配置，不是在打字。
    if ((g_settingsHwnd && IsWindow(g_settingsHwnd) && IsWindowVisible(g_settingsHwnd)) ||
        (g_closePromptHwnd && IsWindow(g_closePromptHwnd) && IsWindowVisible(g_closePromptHwnd))) {
        g_dbgWhy = "settings-open";
        return;
    }
    // ⚠ 但如果用户已经切到别的程序了，菜单留着就没意义 —— 收掉再走正常判定
    if (g_lmOpen) {
        if (g_lmOpenFg && RealForegroundWindow() != g_lmOpenFg) {
            g_lmOpen = FALSE;
            g_lmHover = -1;
            if (g_hWnd && IsWindow(g_hWnd)) InvalidateRect(g_hWnd, NULL, FALSE);
        } else {
            g_dbgWhy = "layout-menu";
            return;
        }
    }
    // 窗口滑动动画期间不做焦点评估：焦点探测可能触发跨进程 COM 调用
    // 在启动动画中执行会造成可感知的卡顿；动画结束后下个轮询周期再评估。
    if (g_mainMotion.active) {
        // ⚠⚠ 兜底：动画靠 WM_TIMER（15ms）推进，而 WM_TIMER 在消息队列里
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
        g_dbgWhy = "anim";
        return;
    }

    // 前台窗口变了 -> 进入"等待用户操作"状态（见 g_fgAwaitUserInput 的说明）
    {
        HWND fgNow = RealForegroundWindow();   // 跳过系统触摸键盘：它弹出来不算"用户切了窗口"
        if (fgNow != g_lastFg) {
            g_lastFg = fgNow;
            g_fgAwaitUserInput = TRUE;
        }
    }

    DWORD nowTick = GetTickCount();

    // ⚠⚠⚠ 全部判断都不再依赖 `input`（GetFocusedInputControl 的返回值
    HWND fgNow = RealForegroundWindow();   // 跳过系统触摸键盘（见 RealForegroundWindow）
    HWND fgTop = fgNow ? GetAncestor(fgNow, GA_ROOT) : NULL;

    // 前台窗口变了 -> 进入"等待用户操作"状态
    if (fgNow != g_lastFg) {
        g_lastFg = fgNow;
        g_fgAwaitUserInput = TRUE;
    }

    // 触屏："用户刚点了一下"的边沿检测（每 tick 采一次，见 TouchInputTick）
    TouchInputTick();

    BOOL recentClick = (g_lastClickTick && nowTick - g_lastClickTick <= AUTOSHOW_INPUT_WINDOW_MS);
    BOOL byKey       = (g_lastKeyTick && nowTick - g_lastKeyTick <= AUTOSHOW_INPUT_WINDOW_MS);
    // 点击落点是否在当前前台窗口内（点在任务栏 / 桌面 / 别的应用上都不算）
    BOOL clickInFg   = (recentClick && g_lastClickTopHwnd && fgTop &&
                        g_lastClickTopHwnd == fgTop);

    // ⚠⚠ byUserInFg 是**触屏专用兜底**（见 TouchInputTick 的完整论证）
    BOOL byUserInFg = TouchTappedRecently();
    if (byKey || clickInFg || byUserInFg) g_fgAwaitUserInput = FALSE;

    // 诊断：本 tick 默认无补查；用户操作窗口内才安排（须在 GetFocusedInputControl
    // 之前清/置，否则会把补查刚写入的结果清掉）
    g_dbgExtraMs = -1;
    g_dbgExtraOk = -1;
    g_dbgExtraDue = (g_afLogPath[0] && (recentClick || byKey)) ? TRUE : FALSE;
    // ⚠ 这一句必须放在 `if (input)` 判定段**之前**：那段里有 `goto done`，而 done 在
    HWND fg = RealForegroundWindow();   // 别让系统触摸键盘被当成"别的程序"而误收键盘
    HWND input = GetFocusedInputControl();

    //   ⚠⚠ 前几版反复翻车的根因只有一条：**"该不该弹"和"该不该收"用了两个
    HWND inputForLog = input;       // 判定会把 input 置空（精判收起），日志要看判定前的值
    // ⚠ g_vis 也要留判定前的值：日志现在打在函数出口（判定/动作之后）
    //   直接读 g_vis 会记成"动作之后"的值，跟 ShowKB 那行的语义就对不上了。
    BOOL visForLog = g_vis;
    int chromiumVerdict = -1;       // 焦点判据：1=可编辑 0=明确不是 -1=未知
    if (input && g_afChromiumPrecise && g_detectedByChromiumClass) {
        if (g_vis) {
            // ⚠ 只在**已显示**时用焦点判据：隐藏期间它给过无效值
            BOOL typing = byKey || (g_lastOwnKeyTick && (nowTick - g_lastOwnKeyTick) <= 1200);
            if (!typing) chromiumVerdict = UiaFocusEditableHeld(fgNow);
        } else {
            g_dbgUiaCt = -1;        // 本 tick 没查 —— 别在日志里留上一次的残值
        }
    }
    // 信任门：**只有真正见到可编辑元素才设**。-1（未知）绝不算。
    if (chromiumVerdict == 1) g_chromiumTrustFg = fgNow;
    if (input && chromiumVerdict == 0 &&
        g_chromiumTrustFg && IsWindow(g_chromiumTrustFg) && g_chromiumTrustFg == fgNow) {
        // 精判说"光标不在可编辑元素里" ⇒ 收起，并记下"这一次是精判收的"。
        g_verdictHide = TRUE;
        g_verdictHidFg = fgNow;
        input = NULL;
        g_dbgWhy = "verdict-hide";
    }
    // 换了前台窗口 ⇒ 这条备忘作废（新窗口要重新攒证据）。
    if (g_verdictHide && g_verdictHidFg != fgNow) g_verdictHide = FALSE;

    if (input) {
        // ⚠ 不能用"点了窗口内任何地方"当证据 —— 那样点空白也会弹一下再收（闪一下）
        if (g_verdictHide) {
            if (g_verdictHidFg != fgNow || byKey) {
                g_verdictHide = FALSE;      // 换了前台窗口 / 敲了实体键 → 重新攒证据
                g_dbgWhy = "vhid-reset";
            } else if (recentClick) {
                // 落点查询结果在诊断块里已经算好（g_dbgUiaPt），一拍只查一次
                if (g_dbgUiaPt == 0 && g_chromiumTrustFg == fgNow) {
                    g_dbgWhy = "vhid-blank";    // 明确点过空白 → 保持收起，不要弹一下再收
                    goto done;
                }
                g_verdictHide = FALSE;      // 点在可编辑元素上 / 判据不可用 → 放行
                g_dbgWhy = "vhid-click-ok";
            } else {
                // ⚠ SysTipJustAppeared 有副作用（更新上升沿），每拍**只能调一次**
                BOOL evTip = SysTipJustAppeared();
                BOOL evFoc = evTip ? FALSE : TouchFocusEvidence();
                BOOL evTap = (evTip || evFoc) ? FALSE : TouchTapEdgeConsume();
                g_dbgEvTip = (int)evTip;
                g_dbgEvFoc = (int)evFoc;
                if (evTip || evFoc || evTap) {
                    g_verdictHide = FALSE;
                    g_dbgWhy = evTap ? "vhid-tap-ok" : "vhid-touch-ok";
                } else {
                    // ⚠ 这里**刻意不用 byUserInFg** —— 它是电平式的（一次触控后一直为真
                    g_dbgWhy = "vhid-hold";     // ← 触屏上"点了输入框也不弹"时最常停在这里
                    goto done;
                }
            }
        }
        g_lastNonInput = 0;
        g_noInputStreak = 0;
        g_noInputSinceTick = 0;   // 输入焦点恢复，失焦计时清零（见失焦分支防抖说明）
        g_manualShow = FALSE;

        // ⚠ token 对不上也先按住，直到超时才放行（原来无条件清标记
        if (g_userHidInInput) {
            BOOL sameInput = (g_hiddenInputToken && g_detectedInputToken == g_hiddenInputToken);
            // cif=1 = 用户点了当前前台窗口内（对 QQ/Edge/浏览器
            // 顶层窗口内点击就意味着点到了地址栏 / 输入框 / 页面）
            if (sameInput && !byKey && !clickInFg && !byUserInFg) { g_dbgWhy = "userHid"; goto done; }
            g_userHidInInput = FALSE;
        }

        //   ⚠ 原来接受"点击落点 != 前台窗口"作为"点到别处"的依据，但那个条件
        if (g_afAutoHide && g_vis && recentClick && g_lastClickTopHwnd) {
            char hitCls[128] = {0};
            GetClassNameA(g_lastClickTopHwnd, hitCls, 128);
            if (IsShellSurfaceClass(hitCls)) {
                ShowKB(FALSE, FALSE);
                g_dbgWhy = "shell-hide";
                goto done;
            }
        }

        if (g_fgAwaitUserInput) { g_dbgWhy = "await"; goto done; }
        if (!g_manualHide && !g_vis) { ShowKB(TRUE, FALSE); g_dbgWhy = "show"; goto done; }
        g_dbgWhy = g_vis ? "shown" : "manualHide";
        goto done;
    }

    if (fg == g_settingsHwnd || fg == g_closePromptHwnd) { g_dbgWhy = "own-window"; goto done; }

    // ⚠ 连续确认：焦点探测会间歇性返回 NULL（hwndCaret 是瞬态量）
    g_noInputStreak++;
    if (g_noInputStreak < 4) { g_dbgWhy = "noInput-streak"; goto done; }
    if (g_noInputSinceTick == 0) g_noInputSinceTick = nowTick;

    // ⚠⚠ 防抖（2026-10-04 用户日志实测，修的是「手动收起后过几秒键盘自己弹回来」）
    if (nowTick - g_noInputSinceTick >= AUTOSHOW_INPUT_GRACE_MS)
        g_userHidInInput = FALSE;
    if (g_lastNonInput == 0) g_lastNonInput = GetTickCount();
    if (g_lastNonInput == 0) g_lastNonInput = GetTickCount();
    if (!g_vis) {
        // 隐藏滑动被打断（如拖动标题栏）后窗口可能仍残留可见：直接收尾藏到任务栏底部
        if (!(g_mainMotion.active && g_mainMotion.finish == MOTION_HIDE) &&
            IsWindowVisible(g_hWnd))
            ShowWindow(g_hWnd, SW_HIDE);
        g_dbgWhy = "hidden-nop";
        goto done;
    }
    if (g_manualHide)     { g_dbgWhy = "manualHide-nop"; goto done; }
    if (GetTickCount() - g_lastNonInput < (DWORD)g_hideDelayMs) { g_dbgWhy = "noInput-delay"; goto done; }
    if (g_manualShow)     { g_dbgWhy = "manualShow-nop"; goto done; }

    ShowKB(FALSE, FALSE);
    g_dbgWhy = "auto-hide";

done:
    // 诊断（-afdiag / 日志导出）：记录这次评估的全部输入量
    {
        // 落点查询（只在"精判刚收过 + 这一拍有新点击"时查）
        // 结果既进日志，也供下面的 show 分支决定要不要放行 —— 一拍只查一次。
        g_dbgUiaPt = -1;
        g_dbgUiaPtCt = -1;
        if (g_verdictHide && g_verdictHidFg == fgNow && recentClick &&
            g_afChromiumPrecise && g_detectedByChromiumClass)
            g_dbgUiaPt = UiaPointEditable(g_lastClickPt);
        char st[160];
        _snprintf_s(st, sizeof(st), _TRUNCATE,
                    "why=%s uiact=%d pt=%d ptc=%d inFg=%d tip=%d foc=%d mh=%d uhi=%d ns=%d hid=%d",
                    g_dbgWhy, g_dbgUiaCt, g_dbgUiaPt, g_dbgUiaPtCt, (int)byUserInFg,
                    g_dbgEvTip, g_dbgEvFoc, (int)g_manualHide, (int)g_userHidInInput,
                    g_noInputStreak, (int)g_verdictHide);
        g_dbgVisWnd = (g_hWnd && IsWindowVisible(g_hWnd)) ? 1 : 0;
        RECT wr = {0, 0, 0, 0};
        if (g_hWnd) GetWindowRect(g_hWnd, &wr);
        g_dbgRc[0] = (int)wr.left;  g_dbgRc[1] = (int)wr.top;
        g_dbgRc[2] = (int)(wr.right - wr.left);
        g_dbgRc[3] = (int)(wr.bottom - wr.top);
        AfLog("eval", fgNow, fgTop, inputForLog, recentClick, clickInFg, byKey,
              g_fgAwaitUserInput, visForLog, g_mainMotion.active, st);
    }
    return;
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
static LRESULT CALLBACK PhysMouseHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        const MSLLHOOKSTRUCT* m = (const MSLLHOOKSTRUCT*)lParam;
        if ((!(m->flags & LLMHF_INJECTED) || g_dbgAcceptInjected) && wParam == WM_LBUTTONDOWN) {
            g_lastClickTick = GetTickCount();
            g_lastClickPt = m->pt;
            // 当场记下落点的顶层窗口（供 UpdateAutoVisibility 判断"点的是不是当前前台窗口"）
            HWND h = WindowFromPoint(m->pt);
            g_lastClickTopHwnd = h ? GetAncestor(h, GA_ROOT) : NULL;
            // 点击之后焦点必然变了 ⇒ 作废 UIA 判定缓存
            // 否则点进输入框后的前 250ms 还在用"点在空白"时的旧结论去收起（会闪）。
            InvalidateUiaCache();
        }
    }
    return CallNextHookEx(NULL, nCode, wParam, lParam);
}

// ===== 实体键盘状态监控（WH_KEYBOARD_LL） =====
static LRESULT CALLBACK PhysKeyHookProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        const KBDLLHOOKSTRUCT* p = (const KBDLLHOOKSTRUCT*)lParam;
        // 忽略本程序 SendInput 注入的事件，避免与虚拟键逻辑互相干扰
        if (!(p->flags & LLKHF_INJECTED) || g_dbgAcceptInjected) {
            // 真实按键 = 用户主动操作，记一笔供自动呼出判断（见 g_lastKeyTick）
            g_lastKeyTick = GetTickCount();
            InvalidateUiaCache();   // 按键后焦点可能已变（Tab / 方向键）
            BOOL down = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);
            BOOL up   = (wParam == WM_KEYUP   || wParam == WM_SYSKEYUP);
            if (down || up) {
                BOOL changed = FALSE;
                switch (p->vkCode) {
                // ⚠ 实体 Shift 按下时顺手解除屏幕键盘上的 Shift 锁定（g_sh / g_shiftLock /
                case VK_LSHIFT: case VK_RSHIFT: {
                    BOOL& side = (p->vkCode == VK_LSHIFT) ? g_physLShift : g_physRShift;
                    if (side != down) { side = down; changed = TRUE; }
                    g_physShift = (g_physLShift || g_physRShift);
                    if (down && (g_sh || g_shiftLock || g_shiftOnce)) {
                        g_sh = FALSE; g_shiftLock = FALSE; g_shiftOnce = FALSE;
                        changed = TRUE;
                    }
                    break;
                }
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
                // NumLock 与 Caps 同为锁存键：keydown 时系统还没翻状态（钩子在系统处理之前）
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
    // 布局菜单优先级最高：点项=应用；点外面=只收起，这一下不传给底下的键
    if (g_lmOpen) {
        int idx = LayoutMenuHitIndex(x, y);
        if (idx >= 0) { LayoutMenuApply(hWnd, idx); return; }
        g_lmOpen = FALSE; g_lmHover = -1;
        InvalidateRect(hWnd, NULL, TRUE);
        return;
    }
    int hh = HitHeader(x, y);
    if (hh >= 0) {
        switch (hh) {
        case HDR_DOCK: OpenSettings(); break;
        // 最小化 = 临时收起，自动呼出仍然有效
        case HDR_MIN: UserHideKeyboard(); break;
        // ⏑ = 永久隐藏到托盘，只有单击托盘图标才回来
        case HDR_HIDE: HideToTray(); break;
        case HDR_CLOSE: HandleCloseAction(hWnd); break;
        case HDR_LAYOUT:
            // 打开/收起布局菜单（原来的三态语义已收进菜单，见 LayoutMenuApply）
            g_lmOpen = !g_lmOpen;
            g_lmHover = -1;
            if (g_lmOpen) g_lmOpenFg = RealForegroundWindow();   // 换程序时据此收起
            g_lastOwnKeyTick = GetTickCount();   // 算作「用户正在用键盘」
            InvalidateRect(hWnd, NULL, TRUE);
            break;
        }
        return;
    }

    int ki = HitKey(x, y);
    if (ki < 0) return;
    g_pk = ki;
    g_pkFlash = -1;                 // 真的按下了，撤掉上一次松手留下的闪光
    // ⚠⚠ 「按下去」这一帧必须**在 DoKeyAction 之前**上屏
#ifdef HK_DIAG
    if (!g_noClickRepaint) {
#endif
        InvalidateRect(hWnd, NULL, FALSE);
        UpdateWindow(hWnd);         // 同步把按下态画出来（不擦背景，见 WM_ERASEBKGND）
#ifdef HK_DIAG
    }
#endif
    // ⚠ 正式版里这两句恒为真（`#ifdef` 整块不编译），保持原行为不变
    //   诊断版可以用 -noscapture / -norepaint 临时跳过它们做二分。
#ifdef HK_DIAG
    if (!g_noSetCapture)
#endif
    SetCapture(hWnd);   // 捕获鼠标，防止开始菜单等出现时抢走鼠标抬起消息导致键一直高亮
    const KeyDef* k = &g_keys[ki];
    g_lastOwnKeyTick = GetTickCount();   // 用户在用屏幕键盘 —— 供"正在输入不收起"用
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
        // 松手后让这一格再亮一小会儿：短按也能看清"确实按到了"。
        // 由 TIMER_FOCUS 到期清除（见那里），所以不需要额外定时器。
        g_pkFlash = g_pk;
        g_pkFlashUntil = GetTickCount() + HK_PK_FLASH_MS;
        g_pk = -1;
        InvalidateRect(hWnd, NULL, FALSE);
    }
}

static void OnMMove(HWND hWnd, int x, int y) {
    int hh = HitHeader(x, y);
    if (hh != g_hdrHov) {
        g_hdrHov = hh;
        InvalidateRect(hWnd, 0, TRUE);
    }
    // ⚠ 布局菜单开着时：只在菜单内做悬停，**不点亮底下的键**。
    int nk = g_lmOpen ? -1 : HitKey(x, y);
    int li = g_lmOpen ? LayoutMenuHitIndex(x, y) : -1;
    if (nk != g_hk || li != g_lmHover) {
        g_hk = nk;
        g_lmHover = li;
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
        // ⚠⚠ 2026-10-04 第 24 轮（卡顿）：确保缓存**在 Blit 之前**就绪。
        EnsureKbFrameCache(hWnd);
        BOOL useKbCache = g_kbCacheBmp && g_kbCacheDc && g_kbCacheBits &&
                          g_kbCacheW == g_ww && g_kbCacheH == g_wh;
        // ⚠ 只贴**脏区**（ps.rcPaint），不再无条件 BitBlt 整个窗口。
        RECT dirty = ps.rcPaint;
        if (dirty.right <= dirty.left || dirty.bottom <= dirty.top) dirty = rc;
        int dx = dirty.left, dy = dirty.top;
        int dw = dirty.right - dirty.left;
        int dh = dirty.bottom - dirty.top;
        if (useKbCache) {
            BitBlt(dc, dx, dy, dw, dh, g_kbCacheDc, dx, dy, SRCCOPY);
        } else {
            // 缓存创建失败时回退到逐帧全量绘制
            WindowPaintSurfaceLocal surface = BeginWindowPaintSurface(dc, hWnd, rc);
            ClearWindowBackBuffer(surface.dc, hWnd, g_ww, g_wh);
            DrawHeader(surface.dc);
            DrawKeys(surface.dc);
            BitBlt(dc, dx, dy, dw, dh, surface.dc, dx, dy, SRCCOPY);
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
        if (g_hdrHov != -1 || g_hk != -1 || g_lmHover != -1) {
            g_hdrHov = -1;
            g_hk = -1;
            g_lmHover = -1;
            InvalidateRect(hWnd, 0, TRUE);
        }
        return 0;
    case WM_FOCUS_EVENT:
        UpdateAutoVisibility();
        return 0;
    case WM_SHOW_KEYBOARD:
        // 诊断：这个分支来自单实例消息转发 —— 每次有第二个 HKeyboard 启动
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
        // 跟随系统主题自动切换（themeMode==0 或开启壁纸强调色时生效）
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
                // ⚠ 标记为"连发中"，让 WaitForLeftButtonUp 直接返回
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
            // 「按下去」的反馈闪光到期 → 收尾重绘一次（见 g_pkFlash 的说明）。
            if (g_pkFlash >= 0 && (LONG)(GetTickCount() - g_pkFlashUntil) >= 0) {
                g_pkFlash = -1;
                InvalidateRect(hWnd, NULL, FALSE);
            }
            if (g_lmOpen && !g_vis) { g_lmOpen = FALSE; g_lmHover = -1; }
            // Win 锁定/高亮状态与开始菜单状态同步
            // 开始菜单（无论由本键盘还是任务栏打开）一旦显示，即清除 Win 锁定，避免高亮残留。
            if (g_winKey && IsStartMenuOpen()) {
                ClearWinLock();
                InvalidateRect(hWnd, 0, TRUE);
            }

            // Win 状态超时自动复位：锁定/开始菜单已开未操作 8 秒即回到空闲
            // 防止一直高亮，以及残留状态导致“第一次点击就弹开始菜单”。
            if (g_winCount != 0 && (GetTickCount() - g_lastWinTick) > 8000) {
                ClearWinLock();
                InvalidateRect(hWnd, 0, TRUE);
            }

            // 实体键状态自校正：钩子偶尔漏掉 keyup/keydown 时，按物理键实际状态修正显示，避免高亮残留
            {
                // ⚠ 左右 Shift 分开读（2026-10-10）：与 g_physLShift / g_physRShift 一一对应。
                //   之前只读一个或值，自校正时会把「另一边还按着」的状态覆盖掉。
                BOOL pLShift = (GetAsyncKeyState(VK_LSHIFT) & 0x8000) != 0;
                BOOL pRShift = (GetAsyncKeyState(VK_RSHIFT) & 0x8000) != 0;
                BOOL pWin   = ((GetAsyncKeyState(VK_LWIN) & 0x8000) != 0) ||
                              ((GetAsyncKeyState(VK_RWIN) & 0x8000) != 0);
                // ⚠⚠ 2026-10-04 第 23 轮：NumLock 改成**每个 tick 都读回真值**。
                BOOL pNum   = ReadSystemNumLock();
                BOOL pCaps  = (GetKeyState(VK_CAPITAL) & 1) != 0;
                BOOL dLock  = (g_npLock != pNum);        // 高亮依据变了
                BOOL dLShift = (g_physLShift != pLShift);
                BOOL dRShift = (g_physRShift != pRShift);
                BOOL dShift  = (dLShift || dRShift);
                BOOL dWin   = (g_physWin != pWin);
                BOOL dNum   = (g_physNum != pNum);
                BOOL dCaps  = (g_cp != pCaps);
                if (dLock) g_npLock = pNum;              // 与真实锁定态对齐
                if (dShift || dWin || dNum || dCaps || dLock) {
                    g_physLShift = pLShift;
                    g_physRShift = pRShift;
                    g_physShift = (pLShift || pRShift);
                    g_physWin = pWin;
                    g_physNum = pNum;
                    g_cp = pCaps;
                    // 按「哪几个变了」逐个失效，别再整窗重画。
                    if (dShift) { InvalidateKeyByVk(hWnd, VK_LSHIFT); }
                    if (dWin)   { InvalidateKeyByVk(hWnd, VK_LWIN); }
                    if (dNum || dLock) { InvalidateKeyByVk(hWnd, VK_NUMLOCK); }
                    if (dCaps)  { InvalidateKeyByVk(hWnd, VK_CAPITAL); }
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
        // ⚠ 托盘图标的一次**双击**会送来三条消息：UP / DBLCLK / UP
        if (l == WM_LBUTTONUP || l == WM_LBUTTONDBLCLK) {
            DWORD tn = GetTickCount();
            if (tn - g_lastTrayToggleTick >= 400) {
                g_lastTrayToggleTick = tn;
                g_dbgShowFrom = "tray-click"; ToggleKB();
            }
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
// ⚠⚠⚠ 为什么需要这个入口（2026-10 加）
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

// ---- -envtest：在 HKeyboard 的**完整环境**里只做纯注入 -
// ⚠ 用定时器触发，**不在 WinMain 里 Sleep**。真实按键也是"短暂阻塞 UI 线程
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

            // ---- 键表诊断（关键！）-
            // ⚠ 上一轮"点 N 键"那版，用户反馈两段都是「你好」
            fwprintf(f, L"---- 键表诊断（排查用）----\n");
            fwprintf(f, L"g_nk=%d  g_ww=%d  g_wh=%d  g_keyAreaX=%d\n",
                     g_nk, g_ww, g_wh, g_keyAreaX);
            for (int i = 0; i < g_nk && i < 6; i++) {
                // ⚠ KeyDef.x/y/w/h 是 **int**（见其定义），这里必须用 %d。
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

    // ---- 阶段 1：纯注入（基线）-
    // ⚠ 这里的 DiagSnap 是关键对照：它记录的是**定时器触发注入**时的系统状态
    DiagSnap(L"[env-bef]");
    SendNihao();
    DiagSnap(L"[env-aft]");
    SendTestKey(VK_RETURN, 1);
    SendTestKey(VK_RETURN, 1);
    Sleep(700);

    // ---- 阶段 2：**完全用真实点击**打一遍 nihao + 空格 -
    // ⚠⚠ 上一版这里只点了一个 N 键，然后接纯注入 —— **那个设计有歧义**
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

// ⚠ 刻意不自己算"格单位 → dpiScale → scaleX"那套换算：那一串乘除
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
    // ---- 以下全是排查参数，正式版不编译（见文件顶部"诊断开关"说明）-

    // ⚠⚠ 必须在 `CreateMutexW` **之前**返回。否则若托盘里已有实例在跑
    if (HasArg(cmd, "-sendtest")) {
        RunSendTest();
        return 0;
    }

    // ⚠ 不能在这里 return —— 要让它正常启动到消息循环
    g_envTest = HasArg(cmd, "-envtest") ? TRUE : FALSE;

    // 排查开关：二分「点击按键」时哪个动作破坏了 IME 组字（见其定义处说明）
    g_noSetCapture    = HasArg(cmd, "-noscapture") ? TRUE : FALSE;
    g_noClickRepaint  = HasArg(cmd, "-norepaint")  ? TRUE : FALSE;
    // -diag：记录每次字母键注入前后的系统状态到 diag.txt（见 DiagSnap）
    g_diag            = HasArg(cmd, "-diag")       ? TRUE : FALSE;
    // -fontdiag：键面字号诊断表（见 FontDiagDump）。字体建好后自动 dump 一次。
    g_fontDiag        = HasArg(cmd, "-fontdiag")   ? TRUE : FALSE;
    // -envtest 一律开启记录：它的两次注入就是"手动点击"的对照组
    // 少了这个没法比较（见 DiagSnap 的说明）。
    if (g_envTest) g_diag = TRUE;
#endif  // HK_DIAG

    // ⚠ -afdiag 必须放在 #endif **外面** —— 它要能在正式版里工作。
    g_dbgAcceptInjected = HasArg(cmd, "-dginj") ? TRUE : FALSE;
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
        // 诊断：上次 afdiag.txt 只有 BOM、一行都没有，最可能就是走的这条路
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
