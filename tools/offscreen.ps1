# Move the dev-build forza2 window off the visible desktop (it keeps
# rendering; capture_window.ps1 still works) so long scripted sessions do not
# cover the screen. usage: offscreen.ps1 [-Back]
param([switch]$Back)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class O {
  [DllImport("user32.dll")] public static extern bool SetWindowPos(IntPtr h, IntPtr after, int x, int y, int cx, int cy, uint flags);
}
"@
$p = Get-Process forza2 -ErrorAction SilentlyContinue |
  Where-Object { -not $env:FORZA2_TARGET -or $_.Path -like "$env:FORZA2_TARGET*" } |
  Where-Object { $_.MainWindowHandle -ne 0 } | Select-Object -First 1
if (-not $p) { "no forza2 window"; exit 1 }
$x = if ($Back) { 100 } else { -6000 }
# SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE
[void][O]::SetWindowPos($p.MainWindowHandle, [IntPtr]::Zero, $x, 100, 0, 0, 0x0001 -bor 0x0004 -bor 0x0010)
"moved to x=$x"
