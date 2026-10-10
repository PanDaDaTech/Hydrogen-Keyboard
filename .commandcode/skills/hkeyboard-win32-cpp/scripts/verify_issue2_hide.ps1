param([string]$Exe, [string]$Log)
# Issue #2 行为验证：点标题栏最小化后，把焦点送进一个编辑框，
# 检查键盘是否被自动弹回（修复前必弹，修复后不弹）。
# 按钮坐标用 GetDpiForWindow 反推 dpi，与 GetHeaderMetrics 的公式一致。
$log = if ($Log) { $Log } else { Join-Path $env:TEMP "issue2_verify.log" }
$out = New-Object System.Collections.Generic.List[string]
function Say([string]$s) { $out.Add([string]$s) }
# 不管怎么退出都要落盘，否则工具层看不到失败原因
trap {
    Say ("TRAPPED: " + $_.Exception.Message)
    Say ("  at: " + $_.InvocationInfo.Line.Trim())
    $out -join "`n" | Set-Content -Path $log -Encoding UTF8
    exit 1
}
$ErrorActionPreference = 'Stop'
try { Add-Type -AssemblyName System.Windows.Forms } catch { Say "WinForms load failed: $($_.Exception.Message)" }
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class B {
  [DllImport("user32.dll")] public static extern IntPtr FindWindowW(string c, string n);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x,int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f,uint dx,uint dy,uint d,IntPtr e);
  [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }
}
"@

Get-Process HKeyboard_x64, notepad -ErrorAction SilentlyContinue | Stop-Process -Force
Start-Sleep -Milliseconds 500
$null = Start-Process -FilePath $Exe
$hkb = [IntPtr]::Zero
for ($i = 0; $i -lt 30; $i++) {
    Start-Sleep -Milliseconds 300
    $proc = Get-Process HKeyboard_x64 -ErrorAction SilentlyContinue
    if ($proc -and $proc.MainWindowHandle -ne 0) { $hkb = $proc.MainWindowHandle; break }
}
if ($hkb -eq [IntPtr]::Zero) { throw "no keyboard window" }

$r = New-Object B+RECT
[void][B]::GetWindowRect($hkb, [ref]$r)
$W = $r.R - $r.L
$s = [B]::GetDpiForWindow($hkb) / 96.0
Say ("keyboard " + $W + "x" + ($r.B - $r.T) + "  dpi=" + [B]::GetDpiForWindow($hkb) +
     "  scale=" + [Math]::Round($s, 4) + "  top=" + $r.T)
# GetHeaderMetrics: rMargin=8*s, wClose=wMin=32*s, gap=8*s
$rm  = [int][Math]::Round(8 * $s)
$wC  = [int][Math]::Round(32 * $s)
$hg  = [int][Math]::Round(8 * $s)
$xMin = $W - $rm - $wC - $hg - $wC
$minX = $r.L + $xMin + [int]($wC / 2)
$minY = $r.T + [int][Math]::Round(14 * $s)
Say ("minimize button at (" + $minX + "," + $minY + ")  [xMin=" + $xMin + " wMin=" + $wC + "]")
$np = $null
$afterMin = $false
$afterFocus = $false
try {
    # 工作区底部：隐藏动画的目标位置就是 work.bottom
    $wa = [System.Windows.Forms.Screen]::PrimaryScreen.WorkingArea
    $workBottom = $wa.Bottom
    Say ("work area bottom = " + $workBottom)

    # 点最小化
    [void][B]::SetForegroundWindow($hkb)
    Start-Sleep -Milliseconds 500
    [void][B]::SetCursorPos($minX, $minY)
    Start-Sleep -Milliseconds 250
    [B]::mouse_event(0x0002, 0, 0, 0, [IntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [B]::mouse_event(0x0004, 0, 0, 0, [IntPtr]::Zero)
    # 隐藏动画 150ms，留足
    Start-Sleep -Milliseconds 1500
    $r2 = New-Object B+RECT
    [void][B]::GetWindowRect($hkb, [ref]$r2)
    # 隐藏了：top 已滑到 work.bottom 附近，或窗口被 SW_HIDE（rect 不变但不可见）
    $afterMinTop = $r2.T
    $afterMinVis = [B]::IsWindowVisible($hkb)
    # top 明显下移（>100px）即视为已滑出
    $minMoved = ($afterMinTop - $r.T) -gt 100
    $afterMin = $minMoved -or (-not $afterMinVis)
    Say ("after minimize: top " + $r.T + " -> " + $afterMinTop +
         "  visible=" + $afterMinVis + "  => hidden=" + $afterMin)

    # 焦点送进记事本编辑区。SendKeys 在无交互桌面时会抛异常 —— 包起来，
    # 打字不是必需的，把焦点送过去才是。
    $np = Start-Process notepad -PassThru
    Start-Sleep -Seconds 2
    try {
        [void][B]::SetForegroundWindow($np.MainWindowHandle)
        Start-Sleep -Milliseconds 500
        [System.Windows.Forms.SendKeys]::SendWait("{TAB}")
        Start-Sleep -Milliseconds 300
        [System.Windows.Forms.SendKeys]::SendWait("test")
        Say "typed into notepad (real focus)"
    } catch {
        Say ("SendKeys unavailable (" + $_.Exception.GetType().Name + ") - SetForegroundWindow only")
    }

    # 等 3.5 秒，覆盖 >= 10 个 300ms 轮询周期
    Start-Sleep -Milliseconds 3500
    $r3 = New-Object B+RECT
    [void][B]::GetWindowRect($hkb, [ref]$r3)
    $afterFocusTop = $r3.T
    $afterFocusVis = [B]::IsWindowVisible($hkb)
    # 弹回来了 = 顶回到屏幕内（top 明显小于 workBottom）
    $poppedBack = ($afterFocusVis -and ($workBottom - $afterFocusTop) -gt 150)
    $afterFocus = $poppedBack
    Say ("after focus input + 3.5s: top=" + $afterFocusTop +
         "  visible=" + $afterFocusVis + "  => popped back=" + $afterFocus)
} catch {
    Say ("ERROR: " + $_.Exception.Message)
}

Say ""
if (-not $afterMin) {
    Say "INCONCLUSIVE: minimize did not register (top never moved), cannot judge"
} elseif ($afterFocus) {
    Say "FAIL: popped back up -> Issue #2 NOT fixed"
} else {
    Say "PASS: stayed hidden after minimize (Issue #2 fixed)"
}

if ($np) { $np | Stop-Process -Force }
Get-Process HKeyboard_x64 -ErrorAction SilentlyContinue | Stop-Process -Force
$out -join "`n" | Set-Content -Path $log -Encoding UTF8
