# Dev helper: send text (WM_CHAR) or keys (WM_KEYDOWN) to the TwinDeck window.
param([string]$Text = "", [string[]]$Keys = @(), [int]$DelayMs = 60)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class S {
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, IntPtr pid);
  [DllImport("kernel32.dll")] public static extern uint GetCurrentThreadId();
  [DllImport("user32.dll")] public static extern bool AttachThreadInput(uint a, uint b, bool attach);
  [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr h);
  public static void Focus(IntPtr h) {
    uint fg = GetWindowThreadProcessId(GetForegroundWindow(), IntPtr.Zero);
    uint me = GetCurrentThreadId();
    AttachThreadInput(me, fg, true);
    BringWindowToTop(h);
    SetForegroundWindow(h);
    AttachThreadInput(me, fg, false);
  }
  [DllImport("user32.dll")] public static extern IntPtr PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@
$proc = Get-Process TwinDeck | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
$h = $proc.MainWindowHandle
[S]::Focus($h) | Out-Null
foreach ($k in $Keys) {
  $vk = [int]$k
  [S]::PostMessageW($h, 0x100, [IntPtr]$vk, [IntPtr]1) | Out-Null
  Start-Sleep -Milliseconds $DelayMs
}
foreach ($c in $Text.ToCharArray()) {
  [S]::PostMessageW($h, 0x102, [IntPtr][int]$c, [IntPtr]1) | Out-Null
  Start-Sleep -Milliseconds $DelayMs
}
