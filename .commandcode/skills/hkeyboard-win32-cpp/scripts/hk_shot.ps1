# ============================================================================
# hk_shot.ps1 -- HKeyboard acceptance-shot framework (hardened).
#
# Why not CopyFromScreen: grabbing the screen over the window rect silently
# returns the DESKTOP when the window is closed or covered, and it still reports
# "saved" -- so a manual close of the keyboard yields a valid-looking PNG of the
# taskbar. Verified on this window: PrintWindow flags=0 and flags=2 produce
# byte-identical output, i.e. the app implements WM_PRINTCLIENT, so PrintWindow
# is occlusion-proof, z-order independent and is the right primitive here.
#
# Hardening, each one earned by a real mis-read in practice:
#   * PrintWindow capture (+ flat-image detection to reject animation frames).
#   * Every shot asserts process alive + hwnd is a visible window, and THROWS
#     instead of saving a bogus image.
#   * Scenario-level retry (3 attempts): if the keyboard is closed mid-scenario
#     the whole scenario is replayed from its ini.
#   * Kill -> write ini -> start. Writing the ini first lets the dying instance's
#     remembered window size clobber the config we just wrote.
#   * NoModsDown gate: if the USER holds a real Shift/Ctrl/Alt while the probe
#     runs, the app faithfully renders uppercase letters and highlights both
#     Shift keys. Wait for release instead of blaming the app.
#   * GrabStable: two grabs 350ms apart must be byte-identical, otherwise a real
#     mouse move (hover tint) or a transient repaint slipped in.
#   * WS_EX_TOPMOST is dropped so the probe never parks a topmost window over
#     whatever the user is doing (PrintWindow does not need it).
#
# Usage:  .\hk_shot.ps1 -Exe <exe path> -Out <shot dir>
# Add scenarios at the bottom with Run-Scenario + Shot; helpers Get-Geom /
# FnCenter / WebShiftCenter / Np123Center already recompute the app geometry.
# ============================================================================
param(
    [Parameter(Mandatory=$true)][string]$Exe,
    [Parameter(Mandatory=$true)][string]$Out
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

Add-Type -TypeDefinition @'
using System;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;
public class HQ2 {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint f);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr hdc, uint flags);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vk);
  [DllImport("gdi32.dll")] public static extern IntPtr CreateDC(string drv, string dev, string port, IntPtr pd);
  [DllImport("gdi32.dll")] public static extern int GetDeviceCaps(IntPtr dc, int idx);
  [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr dc);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }

  public static readonly IntPtr HWND_NOTOPMOST = new IntPtr(-2);
  public const uint SWP_NOSIZE = 0x0001, SWP_NOMOVE = 0x0002, SWP_NOZORDER = 0x0004, SWP_NOACTIVATE = 0x0010;

  // GetDpiForWindow returns 0 inside the PowerShell host (different DPI-awareness
  // than the app), so keep the GetDeviceCaps(LOGPIXELSX) fallback.
  public static uint Dpi(IntPtr h) {
    uint d = GetDpiForWindow(h);
    if (d == 0) {
      IntPtr dc = CreateDC("DISPLAY", null, null, IntPtr.Zero);
      d = (uint)GetDeviceCaps(dc, 88);
      DeleteDC(dc);
    }
    return d == 0 ? 96u : d;
  }
  public static IntPtr Find(uint pid, string cls) {
    IntPtr found = IntPtr.Zero;
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == pid && IsWindowVisible(h)) {
        var c = new StringBuilder(64); GetClassNameW(h, c, 64);
        if (c.ToString() == cls) { found = h; return false; }
      }
      return true;
    }, IntPtr.Zero);
    return found;
  }
  public static int ClientW(IntPtr h) { RECT r; GetClientRect(h, out r); return r.R; }
  public static int ClientH(IntPtr h) { RECT r; GetClientRect(h, out r); return r.B; }
  public static string WinInfo(IntPtr h) {
    if (!IsWindow(h)) return "DESTROYED";
    RECT r; GetWindowRect(h, out r);
    return string.Format("win=({0},{1}) {2}x{3} vis={4}", r.L, r.T, r.R-r.L, r.B-r.T, IsWindowVisible(h));
  }
  public static void Click(IntPtr h, int x, int y) {
    IntPtr lp = (IntPtr)((y << 16) | (x & 0xFFFF));
    PostMessageW(h, 0x0201, (IntPtr)1, lp);
    System.Threading.Thread.Sleep(70);
    PostMessageW(h, 0x0202, IntPtr.Zero, lp);
  }
  public static void ClearHover(IntPtr h) { PostMessageW(h, 0x0200, IntPtr.Zero, (IntPtr)((20000 << 16) | 20000)); }

  // Returns "ok distinct=<n> WxH" or an error string. A window that renders a
  // single flat colour means we caught an animation frame (or a dead surface),
  // which must NOT be treated as a good shot.
  public static string PrintGrab(IntPtr h, string path) {
    if (!IsWindow(h)) return "window destroyed";
    RECT r; GetWindowRect(h, out r);
    int w = r.R-r.L, hh = r.B-r.T;
    if (w <= 0 || hh <= 0) return "bad rect";
    bool ok;
    int distinct = 0;
    using (var bmp = new Bitmap(w, hh))
    using (var g = Graphics.FromImage(bmp)) {
      IntPtr hdc = g.GetHdc();
      try { ok = PrintWindow(h, hdc, 0); } finally { g.ReleaseHdc(hdc); }
      var seen = new System.Collections.Generic.HashSet<int>();
      for (int y = 0; y < hh; y += 4)
        for (int x = 0; x < w; x += 4)
          seen.Add(bmp.GetPixel(x, y).ToArgb());
      distinct = seen.Count;
      bmp.Save(path, ImageFormat.Png);
    }
    if (!ok) return "PrintWindow FAILED";
    if (distinct <= 2) return "flat image (distinct=" + distinct + ")";
    return string.Format("ok distinct={0} {1}x{2}", distinct, w, hh);
  }

  // Real physical modifiers break the shot: if the user holds Shift while the probe
  // runs, the app faithfully renders uppercase letters and highlights both Shift
  // keys (g_physShift). Wait for them to be released instead of blaming the app.
  public static bool NoModsDown() {
    return (GetAsyncKeyState(0x10) & 0x8000) == 0     // VK_SHIFT
        && (GetAsyncKeyState(0x11) & 0x8000) == 0     // VK_CONTROL
        && (GetAsyncKeyState(0x12) & 0x8000) == 0;    // VK_MENU
  }
  // Two grabs 350ms apart must be byte-identical, otherwise a real mouse move or
  // a transient repaint slipped in and the frame is not deliverable.
  public static string GrabStable(IntPtr h, string path) {
    string a = path + ".a.png", b = path + ".b.png";
    string ra = PrintGrab(h, a), rb = PrintGrab(h, b);
    if (!ra.StartsWith("ok")) return ra;
    if (!rb.StartsWith("ok")) return rb;
    byte[] ba = System.IO.File.ReadAllBytes(a), bb = System.IO.File.ReadAllBytes(b);
    bool same = ba.Length == bb.Length;
    if (same) for (int i = 0; i < ba.Length; i++) if (ba[i] != bb[i]) { same = false; break; }
    if (!same) { System.IO.File.Delete(a); System.IO.File.Delete(b); return "unstable frame"; }
    System.IO.File.Delete(b);
    System.IO.File.Delete(path);
    System.IO.File.Move(a, path);
    return ra + " stable";
  }
}
'@ -ReferencedAssemblies System.Drawing

[HQ2]::SetProcessDPIAware() | Out-Null
New-Item -ItemType Directory -Force -Path $Out | Out-Null
$exeDir = Split-Path -Parent (Resolve-Path $Exe)

$script:App = $null

function Kill-App {
    foreach ($n in @('HKeyboard_x64','HKeyboard_x86','HKeyboard_arm64','HKeyboard')) {
        Get-Process $n -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Milliseconds 400
}

function Write-Ini($layout, $npStyle, $fkeys, $fnWeb, $keyIcon) {
    $p = Join-Path $exeDir 'HKeyboard.ini'
    $t = @"
[General]
Language=0
AutoPopup=0
AutoHide=0
ShiftSymbols=1
RememberClose=0
HighlightColor=13924352

[Theme]
Mode=2
Opacity=100
Wallpaper=0
Hue=250

[Keyboard]
Layout=$layout
ShowNumBtn=1
NpHidden=0
FKeys=$fkeys
FnWebLayout=$fnWeb
KeyIconStyle=$keyIcon
NumpadStyle=$npStyle
"@
    [System.IO.File]::WriteAllText($p, $t, (New-Object System.Text.UTF8Encoding($false)))
    Start-Sleep -Milliseconds 150
}

function Start-App {
    $proc = Start-Process -FilePath $Exe -ArgumentList '-noauto' -PassThru
    $h = [IntPtr]::Zero
    for ($i = 0; $i -lt 30; $i++) {
        Start-Sleep -Milliseconds 400
        $proc.Refresh()
        if ($proc.HasExited) { throw "exited during startup (single-instance mutex?)" }
        $h = [HQ2]::Find($proc.Id, 'HKeyboard')
        if ($h -ne [IntPtr]::Zero) { break }
    }
    if ($h -eq [IntPtr]::Zero) { throw "main window not found after 12s" }
    [HQ2]::ShowWindow($h, 5) | Out-Null
    # Probe does not need topmost: PrintWindow renders regardless of z-order, and
    # dropping it keeps the probe from covering the user's screen.
    [HQ2]::SetWindowPos($h, [HQ2]::HWND_NOTOPMOST, 0, 0, 0, 0,
        [HQ2]::SWP_NOMOVE -bor [HQ2]::SWP_NOSIZE -bor [HQ2]::SWP_NOACTIVATE) | Out-Null
    Start-Sleep -Milliseconds 1100
    $script:App = @{ Proc = $proc; Hwnd = $h }
    return $script:App
}

function Assert-Alive($h, $what) {
    if ($null -eq $script:App) { throw "$what : no app" }
    $p = $script:App.Proc
    $p.Refresh()
    if ($p.HasExited) { throw "$what : process exited (code $($p.ExitCode))" }
    if (-not [HQ2]::IsWindow($h)) { throw "$what : hwnd destroyed" }
    if (-not [HQ2]::IsWindowVisible($h)) { throw "$what : hwnd not visible" }
}

function Shot($h, $file) {
    Assert-Alive $h $file
    [HQ2]::ClearHover($h)
    Start-Sleep -Milliseconds 240
    Assert-Alive $h $file
    $r = [HQ2]::PrintGrab($h, (Join-Path $Out $file))
    if (-not $r.StartsWith('ok')) { throw "$file : $r" }
    Write-Output ("  saved {0,-26} {1}" -f $file, $r)
}

function Run-Scenario([string]$name, [scriptblock]$body) {
    for ($t = 1; $t -le 3; $t++) {
        try {
            & $body
            Write-Output ("  [OK] " + $name)
            return
        } catch {
            Write-Output ("  [RETRY {0}] {1} -> {2}" -f $t, $name, $_.Exception.Message)
            Kill-App
            Start-Sleep -Milliseconds 900
        }
    }
    Write-Output ("  [FAIL] " + $name)
}

# --- geometry recomputed from the same formulas as the app -------------------
function BaseWDip($layout) {
    if ($layout -eq 1) { return 430.0 }
    if ($layout -eq 2) { return 1280.0 }
    return 980.0
}

function Get-Geom($h, $layout, $npStyle, $fkeys, $webSurf) {
    $W = [HQ2]::ClientW($h); $H = [HQ2]::ClientH($h)
    $s = [HQ2]::Dpi($h) / 96.0
    $dpi = [int][Math]::Round($s * 96)
    if ($layout -eq 2) { $rows = 6 }
    elseif ($layout -eq 1) { if ($npStyle -eq 1) { $rows = 6 } else { $rows = 4 } }
    else { $rows = 5; if ($fkeys -and -not $webSurf) { $rows = 6 } }

    $scaleX = $W / (980.0 * $s)
    $gap = [Math]::Max(2, [int][Math]::Round(4.0 * $s * $scaleX))
    $kax = [Math]::Max(6, [int][Math]::Round(10.0 * $s))
    $padV = [int][Math]::Round(10.0 * $s)
    $hdrBot = [int][Math]::Round(38.0 * $s)
    $va = ($H - $padV) - ($hdrBot + $padV)
    if ($va -lt 20 * $rows) { $va = 20 * $rows }
    $va = $va - ($rows - 1) * $gap
    $KH = [int][Math]::Floor($va / $rows)
    if ($KH -lt 20) { $KH = 20 }
    $va = $va - $KH * $rows
    $vRem = 0; if ($va -gt 0) { $vRem = $va }
    $y0 = $hdrBot + $padV + [int][Math]::Floor($vRem / 2)

    $hg = [int][Math]::Round(8 * $s); $rm = [int][Math]::Round(8 * $s)
    $wMenu = [int][Math]::Round(78 * $s)
    $wClose = [int][Math]::Round(32 * $s); $wMin = $wClose
    $wNum = [int][Math]::Round(96 * $s)
    $xClose = $W - $rm - $wClose
    $xMin = $xClose - $hg - $wMin
    $xNum = $xMin - $hg - $wNum
    $xMenu = [int][Math]::Round(6 * $s)
    $cy = [int][Math]::Round(10 * $s) + [int][Math]::Round([int][Math]::Round(28 * $s) / 2)

    return @{
        W = $W; H = $H; dpi = $dpi; s = $s; scaleX = $scaleX
        gap = $gap; kax = $kax; KH = $KH; y0 = $y0; rows = $rows
        keyAreaW = $W - 2 * $kax
        hdr = @{ cy = $cy; menu = $xMenu + [int]($wMenu / 2); num = $xNum + [int]($wNum / 2)
                 min = $xMin + [int]($wMin / 2); close = $xClose + [int]($wClose / 2) }
    }
}

function Show-Geom($g, $tag) {
    Write-Output ("  [{0}] W={1} H={2} dpi={3} s={4} scaleX={5} gap={6} KH={7} y0={8} rows={9}" -f `
        $tag, $g.W, $g.H, $g.dpi, [Math]::Round($g.s,3), [Math]::Round($g.scaleX,4), $g.gap, $g.KH, $g.y0, $g.rows)
}

function FnCenter($g) {
    $wFn = [int][Math]::Round(46 * $g.s * $g.scaleX)
    return @(($g.kax + [int]($wFn / 2)), ($g.y0 + 4 * ($g.KH + $g.gap) + [int]($g.KH / 2)))
}
function Np123Center($g) {
    $colW = [int](($g.keyAreaW - 3 * $g.gap) / 4)
    return @(($g.kax + [int]($colW / 2)), ($g.y0 + 3 * ($g.KH + $g.gap) + [int]($g.KH / 2)))
}
function WebShiftCenter($g) {
    $wLSh = [int][Math]::Round(95 * $g.s * $g.scaleX)
    return @(($g.kax + [int]($wLSh / 2)), ($g.y0 + 3 * ($g.KH + $g.gap) + [int]($g.KH / 2)))
}

function Shot($h, $file) {
    Assert-Alive $h $file
    $ok = $false
    for ($k = 0; $k -lt 24; $k++) {
        if ([HQ2]::NoModsDown()) { $ok = $true; break }
        Start-Sleep -Milliseconds 250
    }
    if (-not $ok) { throw "$file : a modifier key stayed held for 6s" }
    [HQ2]::ClearHover($h)
    Start-Sleep -Milliseconds 280
    Assert-Alive $h $file
    $r = [HQ2]::GrabStable($h, (Join-Path $Out $file))
    if (-not $r.StartsWith('ok')) { throw "$file : $r" }
    Write-Output ("  saved {0,-26} {1}" -f $file, $r)
}

Write-Output "=== exe: $Exe"
Write-Output "=== out: $Out"

# ---- add scenarios here, e.g. ---------------------------------------------
# Run-Scenario 'default layout' {
#     Kill-App; Write-Ini 0 0 0 1 2          # layout fkeys fnWeb keyIcon
#     $a = Start-App
#     $g = Get-Geom $a.Hwnd 0 0 0 $false; Show-Geom $g 'L0'
#     Shot $a.Hwnd 'default.png'
# }
# Run-Scenario 'numpad' {
#     Kill-App; Write-Ini 1 0 0 0 2
#     $a = Start-App
#     $g = Get-Geom $a.Hwnd 1 1 0 $false; Show-Geom $g 'NP'
#     Shot $a.Hwnd 'numpad.png'
# }
# Kill-App
# Write-Output "done"
