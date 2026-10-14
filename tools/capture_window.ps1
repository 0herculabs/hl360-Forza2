# Capture only one window (default: forza2) (PrintWindow, works even when it is behind
# other windows). usage: capture_window.ps1 OUT.png [-Process name]
param([string]$Out, [string]$Process = "forza2")
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class W {
  [StructLayout(LayoutKind.Sequential)] public struct R { public int L, T, Ri, B; }
  [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h, out R r);
  [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint f);
}
"@
$p = Get-Process $Process -ErrorAction SilentlyContinue | Where-Object { -not $env:FORZA2_TARGET -or $_.Path -like "$env:FORZA2_TARGET*" } | Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p -or $p.MainWindowHandle -eq 0) { "no $Process window"; exit 1 }
$r = New-Object W+R; [void][W]::GetClientRect($p.MainWindowHandle, [ref]$r)
$bmp = New-Object System.Drawing.Bitmap ([math]::Max($r.Ri,1)), ([math]::Max($r.B,1))
$g = [System.Drawing.Graphics]::FromImage($bmp); $dc = $g.GetHdc()
[void][W]::PrintWindow($p.MainWindowHandle, $dc, 3)  # PW_CLIENTONLY | PW_RENDERFULLCONTENT
$g.ReleaseHdc($dc); $bmp.Save($Out); "saved $($r.Ri)x$($r.B)"
