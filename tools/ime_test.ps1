# Dev helper: real IME test - toggles Hangul mode, types keys, screenshots mid-composition, restores mode.
param([string]$Keys = "gks", [string]$Rest = "rmf", [string]$Out1, [string]$Out2)
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class I {
  [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
}
"@
& "$PSScriptRoot\hotkey.ps1" -Chord "Ctrl+F" | Out-Null
Start-Sleep -Milliseconds 200
function Tap([int]$vk) { [I]::keybd_event([byte]$vk, 0, 0, [UIntPtr]::Zero); Start-Sleep -Milliseconds 40; [I]::keybd_event([byte]$vk, 0, 2, [UIntPtr]::Zero); Start-Sleep -Milliseconds 80 }
Tap 0x15   # VK_HANGUL -> Korean input mode
foreach ($c in $Keys.ToCharArray()) { Tap ([int][char]::ToUpper($c)) }
Start-Sleep -Milliseconds 300
& "$PSScriptRoot\shot.ps1" -Out $Out1 | Out-Null
foreach ($c in $Rest.ToCharArray()) { Tap ([int][char]::ToUpper($c)) }
Start-Sleep -Milliseconds 300
& "$PSScriptRoot\shot.ps1" -Out $Out2 | Out-Null
Tap 0x15   # back to English mode
