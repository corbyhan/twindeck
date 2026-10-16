# Captures the TwinDeck window (dev helper) without bringing it to the front: PrintWindow with
# PW_RENDERFULLCONTENT renders DirectComposition content even when other windows cover it.
param([string]$Out = "$PSScriptRoot\..\build\shot.png", [string]$Exe = "")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class W {
  [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
  [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
  public struct RECT { public int L, T, R, B; }
}
"@
[W]::SetProcessDPIAware() | Out-Null
# by default only windows of executables in this repo's build folder (never an installed copy in use)
$build = [IO.Path]::GetFullPath("$PSScriptRoot\..\build")
$proc = Get-Process TwinDeck, TwinDeck-Setup -ErrorAction SilentlyContinue |
  Where-Object { ($_.MainWindowHandle -ne 0) -and $_.Path -and (($Exe -eq "" -and $_.Path.StartsWith($build, 'OrdinalIgnoreCase')) -or ($Exe -ne "" -and $_.Path -eq $Exe)) } |
  Select-Object -First 1
if (-not $proc) { Write-Error "window not found"; exit 1 }
$h = $proc.MainWindowHandle
$r = New-Object W+RECT
[W]::GetWindowRect($h, [ref]$r) | Out-Null
$w = $r.R - $r.L; $hh = $r.B - $r.T
$bmp = New-Object System.Drawing.Bitmap $w, $hh
$g = [System.Drawing.Graphics]::FromImage($bmp)
$dc = $g.GetHdc()
[W]::PrintWindow($h, $dc, 2) | Out-Null  # PW_RENDERFULLCONTENT
$g.ReleaseHdc($dc)
$bmp.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
Write-Output "saved $Out ($w x $hh)"
