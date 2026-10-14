# Build the player-facing release zip from a release build. Contains the
# launcher, the recompiled game, the runtime DLLs, port config, the file
# manifest imports are checked against, and the VC++ runtime (app-local, as
# Microsoft's redistribution terms allow). No game data of any kind.
#
# usage: tools\package_release.ps1 [-Version 0.1.0] [-Preset win-amd64-release]
param([string]$Version = "0.1.0", [string]$Preset = "win-amd64-release")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$build = Join-Path $root "out\build\$Preset"
$name = "Forza2-Native-$Version-windows-x64"
$dist = Join-Path $root "dist"
$stage = Join-Path $dist $name

$files = "Forza2Launcher.exe", "forza2.exe", "rexruntime.dll", "rexgpu-xenos.dll", "forza2.toml", "game_files.sha256"
foreach ($f in $files) {
  if (-not (Test-Path (Join-Path $build $f))) { throw "missing $f in $build - build first (build.bat release)" }
}

Remove-Item $stage -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force $stage | Out-Null
foreach ($f in $files) { Copy-Item (Join-Path $build $f) $stage }
# Box art, only when a local launcher\data\cover.png was built in (not in the repository).
if (Test-Path (Join-Path $build "cover.png")) { Copy-Item (Join-Path $build "cover.png") $stage }

# VC++ 2015-2022 runtime, app-local.
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -products * -latest -property installationPath
$crt = Get-ChildItem "$vs\VC\Redist\MSVC\*\x64\Microsoft.VC14*.CRT" -Directory | Sort-Object FullName | Select-Object -Last 1
if (-not $crt) { throw "VC++ redistributable folder not found under $vs" }
Copy-Item "$crt\*.dll" $stage

Copy-Item (Join-Path $root "docs\PLAYERS.md") (Join-Path $stage "LEEME.md") -ErrorAction SilentlyContinue
# Licences of bundled third-party code (BSD/MIT/zlib/LGPL require them with binaries).
Copy-Item (Join-Path $root "docs\THIRD_PARTY_NOTICES.txt") $stage

# Guard: nothing from the game may end up in the package.
$leak = Get-ChildItem $stage -Recurse -File | Where-Object { $_.Extension -in ".xex", ".zip", ".iso", ".xds", ".fsb", ".bik" }
if ($leak) { throw "game data in package: $($leak.Name -join ', ')" }

$zip = Join-Path $dist "$name.zip"
Remove-Item $zip -ErrorAction SilentlyContinue
Compress-Archive -Path "$stage\*" -DestinationPath $zip
$hash = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$hash  $name.zip" | Set-Content -Encoding ascii "$zip.sha256"
"package: $zip"
"sha256:  $hash"
Get-ChildItem $stage | Select-Object Name, Length | Format-Table -AutoSize | Out-String
