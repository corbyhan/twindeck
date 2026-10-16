# Dev helper: press a key chord with real input (keybd_event) on the TwinDeck window. e.g. -Chord "Ctrl+Shift+P"
# Injected input is system-wide: modifiers are always released in `finally`, keys carry real scan codes, and
# navigation keys get KEYEVENTF_EXTENDEDKEY (without it Windows treats them as numpad keys and synthesizes
# extra Shift up/down events, which can leave Shift logically stuck).
param([string]$Chord)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class K {
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
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint type);
  [DllImport("user32.dll")] public static extern short GetAsyncKeyState(int vk);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
}
"@
$proc = Get-Process TwinDeck | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $proc) { Write-Error "TwinDeck window not found"; exit 1 }
[K]::Focus($proc.MainWindowHandle) | Out-Null
Start-Sleep -Milliseconds 150
if ([K]::GetForegroundWindow() -ne $proc.MainWindowHandle) { Write-Warning "TwinDeck is not foreground; not sending keys"; exit 1 }
# left-hand modifiers by their specific virtual keys
$map = @{ Ctrl = 0xA2; Shift = 0xA0; Alt = 0xA4; Enter = 0x0D; Esc = 0x1B; Tab = 0x09; Space = 0x20; Back = 0x08; Del = 0x2E;
          Up = 0x26; Down = 0x28; Left = 0x25; Right = 0x27; Home = 0x24; End = 0x23; PageUp = 0x21; PageDown = 0x22;
          F2 = 0x71; F5 = 0x74; Backslash = 0xDC; Comma = 0xBC }
$extended = @(0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2D, 0x2E)
$keys = @($Chord.Split('+') | ForEach-Object {
  if ($map.ContainsKey($_)) { $map[$_] }
  elseif ($_ -match '^F(\d+)$') { 0x6F + [int]$Matches[1] }
  else { [int][char]$_.ToUpper() } })
function Send([int]$vk, [bool]$up) {
  $flags = 0
  if ($extended -contains $vk) { $flags = $flags -bor 1 }  # KEYEVENTF_EXTENDEDKEY
  if ($up) { $flags = $flags -bor 2 }                       # KEYEVENTF_KEYUP
  [K]::keybd_event([byte]$vk, [byte][K]::MapVirtualKey($vk, 0), $flags, [UIntPtr]::Zero)
}
$pressed = New-Object System.Collections.ArrayList
try {
  foreach ($k in $keys) { Send $k $false; [void]$pressed.Add($k); Start-Sleep -Milliseconds 30 }
} finally {
  for ($i = $pressed.Count - 1; $i -ge 0; $i--) { Send $pressed[$i] $true; Start-Sleep -Milliseconds 30 }
  # never leave a modifier down, whatever happened above
  foreach ($m in 0xA0, 0xA1, 0xA2, 0xA3, 0xA4, 0xA5) { if ([K]::GetAsyncKeyState($m) -band 0x8000) { Send $m $true } }
}
