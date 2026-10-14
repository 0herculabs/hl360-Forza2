# Post a key press to the forza2 window without focusing it.
# usage: send_key.ps1 <virtual-key-code> [hold-ms]
param([int]$Vk, [int]$HoldMs = 120)
Add-Type @"
using System; using System.Runtime.InteropServices;
public static class K {
  [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr h, uint m, IntPtr w, IntPtr l);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint c, uint t);
}
"@
$p = Get-Process forza2 -ErrorAction SilentlyContinue | Where-Object { -not $env:FORZA2_TARGET -or $_.Path -like "$env:FORZA2_TARGET*" } | Select-Object -First 1
if (-not $p -or $p.MainWindowHandle -eq 0) { "no forza2 window"; exit 1 }
$scan = [K]::MapVirtualKey($Vk, 0)
[void][K]::PostMessage($p.MainWindowHandle, 0x100, [IntPtr]$Vk, [IntPtr](1 -bor ($scan -shl 16)))
Start-Sleep -Milliseconds $HoldMs
[void][K]::PostMessage($p.MainWindowHandle, 0x101, [IntPtr]$Vk, [IntPtr](1 -bor ($scan -shl 16) -bor 0xC0000000))
