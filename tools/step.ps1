# Drive an already running forza2: send keys, wait, capture one frame.
# usage: step.ps1 -Out shot.png [-Keys 32,0x44] [-Wait 3] [-HoldMs 120]
param([string]$Out, [int[]]$Keys = @(), [double]$Wait = 3, [int]$HoldMs = 120)
$T = $PSScriptRoot
foreach ($k in $Keys) { & powershell -NoProfile -File "$T\send_key.ps1" $k $HoldMs | Out-Null; Start-Sleep -Milliseconds 700 }
Start-Sleep -Seconds $Wait
if (-not (Get-Process forza2 -ErrorAction SilentlyContinue)) { "game is not running"; exit 1 }
& powershell -NoProfile -File "$T\capture_window.ps1" $Out
