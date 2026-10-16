# Dev helper: real mouse click at coordinates relative to the TwinDeck window rect (same space as shot.ps1 images).
param([int]$X, [int]$Y)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class R {
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
  [DllImport("user32.dll")] public static extern void mouse_event(uint f, int dx, int dy, uint d, UIntPtr e);
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  public struct RECT { public int L, T, Rr, B; }
}
"@
[R]::SetProcessDPIAware() | Out-Null
$h = (Get-Process TwinDeck | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle
$r = New-Object R+RECT
[R]::GetWindowRect($h, [ref]$r) | Out-Null
[R]::SetCursorPos($r.L + $X, $r.T + $Y) | Out-Null
Start-Sleep -Milliseconds 120
[R]::mouse_event(2, 0, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 70
[R]::mouse_event(4, 0, 0, 0, [UIntPtr]::Zero)
