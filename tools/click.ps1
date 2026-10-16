# Dev helper: left-click at client coordinates of the TwinDeck window via posted mouse messages.
param([int]$X, [int]$Y)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class M { [DllImport("user32.dll")] public static extern IntPtr PostMessageW(IntPtr h, uint m, IntPtr w, IntPtr l); }
"@
$h = (Get-Process TwinDeck | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle
$lp = [IntPtr](($Y -shl 16) -bor $X)
[M]::PostMessageW($h, 0x200, [IntPtr]0, $lp) | Out-Null; Start-Sleep -Milliseconds 100
[M]::PostMessageW($h, 0x201, [IntPtr]1, $lp) | Out-Null; Start-Sleep -Milliseconds 80
[M]::PostMessageW($h, 0x202, [IntPtr]0, $lp) | Out-Null
