# Dev helper: simulate a click on the custom maximize button (non-client HTMAXBUTTON messages).
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class N { [DllImport("user32.dll")] public static extern IntPtr SendMessageW(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern bool IsZoomed(IntPtr h); }
"@
$h = (Get-Process TwinDeck | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1).MainWindowHandle
[N]::SendMessageW($h, 0xA1, [IntPtr]9, [IntPtr]0) | Out-Null
[N]::SendMessageW($h, 0xA2, [IntPtr]9, [IntPtr]0) | Out-Null
Start-Sleep -Milliseconds 500
"zoomed=" + [N]::IsZoomed($h)
