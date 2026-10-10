# HKeyboard 实机截图探针：把 CI 产物拉下来在本机真跑真截图。
#
# 用法（PowerShell 工具里跑，不要从 bash 调 powershell）：
#   .\hk_probe.ps1 <exe 路径> <输出目录>
# 产物：
#   v3_kb.png           键盘主窗口
#   v3_kb_numpad.png    「小键盘」布局（点了标题栏那颗 pill）
#   v3_tab_general.png  设置窗口「常规」Tab
#   v3_tab_layout.png / v3_tab_theme.png / v3_tab_about.png
#
# 为什么要有它：本机没有编译器，改完只能等 CI 出包。想确认「改完到底长什么样」，
# 唯一可靠的办法就是 gh run download 拉产物 + 本机跑 + 截图 + 量像素。
# 靠几何推演验证过 5 处改动，结果实机跑量出来才发现模型还有偏差 —— 别再纯推演。
#
# 三个必须记住的坑：
#   1) **单实例互斥**：HKeyboard 用 `HKeyboard_Mutex` 判重，已有实例在跑时新 exe 会
#      `exit 0` 直接退出、只把旧窗口唤到前台。所以本脚本开头先 Stop-Process。
#      用户报「测了最新 CI 还是老样子」，十有八九就是这台机器上还挂着旧实例。
#   2) **Add-Type 必须 -ReferencedAssemblies System.Drawing**，否则报「命名空间
#      System.Drawing 中不存在类型 Imaging」。
#   3) **不要靠 SetCursorPos + mouse_event 点击**（窗口可能不在前台、坐标会被 DPI 虚拟化）。
#      统一用 PostMessage 投递 WM_LBUTTONDOWN/UP 到**客户端坐标**，不依赖焦点。
#      主窗口开设置直接用 WM_COMMAND + ID_MENU_SETTINGS(10010)，比模拟点击稳。
$ErrorActionPreference = 'Continue'
Add-Type -AssemblyName System.Drawing

Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Text;
using System.Drawing;
using System.Drawing.Imaging;
using System.Runtime.InteropServices;

public class HkW {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc p, IntPtr l);
  public delegate bool EnumProc(IntPtr h, IntPtr l);
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
  [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowTextW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr h, StringBuilder s, int n);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int c);
  [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L,T,R,B; }

  public static List<IntPtr> TopLevel(uint pid, int minW, int minH) {
    var res = new List<IntPtr>();
    EnumWindows(delegate(IntPtr h, IntPtr l) {
      uint p; GetWindowThreadProcessId(h, out p);
      if (p == pid && IsWindowVisible(h)) {
        RECT r; GetWindowRect(h, out r);
        if ((r.R-r.L) >= minW && (r.B-r.T) >= minH) res.Add(h);
      }
      return true;
    }, IntPtr.Zero);
    return res;
  }
  public static string Cls(IntPtr h) { var s = new StringBuilder(256); GetClassNameW(h, s, 256); return s.ToString(); }
  public static string Title(IntPtr h) { var s = new StringBuilder(256); GetWindowTextW(h, s, 256); return s.ToString(); }
  public static RECT Rect(IntPtr h) { RECT r; GetWindowRect(h, out r); return r; }
  public static string Grab(IntPtr h, string path) {
    RECT r; GetWindowRect(h, out r);
    int w = r.R-r.L, hh = r.B-r.T;
    if (w <= 0 || hh <= 0) return "bad rect";
    using (var bmp = new Bitmap(w, hh))
    using (var g = Graphics.FromImage(bmp)) {
      g.CopyFromScreen(r.L, r.T, 0, 0, new Size(w, hh));
      bmp.Save(path, ImageFormat.Png);
    }
    return string.Format("saved {0}  {1}x{2}", path, w, hh);
  }
  // 客户端坐标点击：PostMessage 直接投递，不依赖前台/焦点
  public static void Click(IntPtr h, int x, int y) {
    IntPtr lp = (IntPtr)((y << 16) | (x & 0xFFFF));
    PostMessageW(h, 0x0201, (IntPtr)1, lp);   // WM_LBUTTONDOWN, MK_LBUTTON
    System.Threading.Thread.Sleep(40);
    PostMessageW(h, 0x0202, IntPtr.Zero, lp); // WM_LBUTTONUP
  }
  // 清掉 hover：往窗口投一个「远离窗口」的 WM_MOUSEMOVE，SettingsHitTest 会返回 -1。
  // 不做这一步，截图里会残留鼠标所在那一行的整行高亮与白色 tile —— 两张图逐像素比对时
  // 这一整行都是差异，很容易被误读成「这里有 bug」。（Windows 在窗口激活/移动时还会用
  // 真实鼠标位置补发一次 WM_MOUSEMOVE，所以只靠「脚本没动鼠标」是不够的。）
  public static void ClearHover(IntPtr h) {
    PostMessageW(h, 0x0200, IntPtr.Zero, (IntPtr)((20000 << 16) | 20000));
  }
}
'@ -ReferencedAssemblies System.Drawing

# 截图统一走 Shot：先清 hover 再抓，避免把鼠标所在行的整行高亮拍进去。
function Shot($h, $path) {
  [HkW]::ClearHover($h)
  Start-Sleep -Milliseconds 220
  Write-Output ([HkW]::Grab($h, $path))
}

$exe = $args[0]
$out = $args[1]
[HkW]::SetProcessDPIAware() | Out-Null
Get-Process HKeyboard_x64 -ErrorAction SilentlyContinue | Stop-Process -Force -ErrorAction SilentlyContinue
Start-Sleep -Milliseconds 400

$proc = Start-Process -FilePath $exe -PassThru
Start-Sleep -Milliseconds 2600
$proc.Refresh()

$wins = [HkW]::TopLevel($proc.Id, 300, 200)
$main = $null
foreach ($w in $wins) { if ([HkW]::Cls($w) -eq 'HKeyboard') { $main = $w } }
if (-not $main) { Write-Output "NO MAIN"; exit 1 }
[HkW]::ShowWindow($main, 5) | Out-Null
[HkW]::SetForegroundWindow($main) | Out-Null
Start-Sleep -Milliseconds 700
Shot $main "$out\v3_kb.png"

# 键盘标题栏「小键盘」pill：xNum 起 ~1443，宽 147
[HkW]::Click($main, 1516, 41)
Start-Sleep -Milliseconds 900
Shot $main "$out\v3_kb_numpad.png"
[HkW]::Click($main, 1516, 41)      # 切回
Start-Sleep -Milliseconds 800

# 打开设置
[HkW]::PostMessageW($main, 0x0111, [IntPtr]10010, [IntPtr]::Zero) | Out-Null
Start-Sleep -Milliseconds 1600
$wins2 = [HkW]::TopLevel($proc.Id, 300, 200)
$set = $null
foreach ($w in $wins2) { if ([HkW]::Cls($w) -eq 'HKeyboardSettings') { $set = $w } }
if (-not $set) { Write-Output "NO SETTINGS"; exit 1 }
[HkW]::SetForegroundWindow($set) | Out-Null
Start-Sleep -Milliseconds 700

$tabX = @(86, 199, 311, 427)
$tabN = @('general', 'layout', 'theme', 'about')
for ($i = 0; $i -lt 4; $i++) {
  [HkW]::Click($set, $tabX[$i], 144)
  Start-Sleep -Milliseconds 900
  Shot $set "$out\v3_tab_$($tabN[$i]).png"
}
Write-Output "PID=$($proc.Id)"
