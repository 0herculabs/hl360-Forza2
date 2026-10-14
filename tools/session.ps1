# Scripted play session: launch, send keys to the window, capture frames.
# usage: session.ps1 -OutDir DIR -Steps "wait:22","key:13","shot:menu",...
#   wait:<sec>   sleep
#   key:<vk>[:<holdms>]  post a key press (13 Return/Start, 32 Space/A,
#                8 Backspace/B, 0x44 D/right, 0x41 A/left, 0x57 W, 0x53 S, 0x45 E throttle)
#   shot:<name>  capture the game window to DIR\<name>.png
# Leaves the game running if -Keep is set; otherwise kills it at the end.
param([string]$OutDir, [string[]]$Steps, [string]$Gamertag = "TestDriver", [string[]]$ExtraArgs = @(), [switch]$Keep)
$T = $PSScriptRoot
# Only ever drive the dev build this script launched, never a player instance.
$env:FORZA2_TARGET = Join-Path (Split-Path $T) "out\build"
function Get-DevGame { Get-Process forza2 -ErrorAction SilentlyContinue | Where-Object { $_.Path -like "$env:FORZA2_TARGET*" } }
$log = Join-Path (Split-Path $T) "run.log"
Remove-Item $log -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $OutDir | Out-Null
Start-Process -FilePath cmd.exe -ArgumentList (@("/c", (Join-Path (Split-Path $T) "run.bat"), "--gamertag=$Gamertag") + $ExtraArgs) -WindowStyle Hidden | Out-Null
foreach ($s in $Steps) {
  if (-not (Get-DevGame) -and $s -notlike "wait:*") { "game exited before step $s"; break }
  $f = $s.Split(":")
  switch ($f[0]) {
    "wait" { Start-Sleep -Seconds ([double]$f[1]) }
    "key" { $hold = if ($f.Count -gt 2) { [int]$f[2] } else { 120 }; & powershell -NoProfile -File "$T\send_key.ps1" ([int]$f[1]) $hold | Out-Null; Start-Sleep -Milliseconds 600 }
    "shot" { & powershell -NoProfile -File "$T\capture_window.ps1" (Join-Path $OutDir "$($f[1]).png") | Out-Null; "shot $($f[1])" }
  }
}
$alive = [bool](Get-DevGame)
if ($alive -and -not $Keep) { Get-DevGame | Stop-Process -Force }
"alive at end: $alive"
Select-String -Path $log -Pattern "crash:|access violation|\[critical\]" -ErrorAction SilentlyContinue |
  ForEach-Object { $_.Line.Substring([math]::Min(27, $_.Line.Length)) } | Select-Object -First 40
