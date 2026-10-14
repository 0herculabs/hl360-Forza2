# Download the ReXGlue SDK release this port is built against into _sdk\.
# usage: scripts\get-sdk.ps1 [-Version 0.10.0]
param([string]$Version = "0.10.0")
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot
$dest = Join-Path $root "_sdk"
if (Test-Path (Join-Path $dest "win-amd64\bin\rexglue.exe")) { "SDK already in $dest"; exit 0 }
$url = "https://github.com/rexglue/rexglue-sdk/releases/download/v$Version/rexglue-sdk-$Version-win-amd64.zip"
$zip = Join-Path $env:TEMP "rexglue-sdk-$Version-win-amd64.zip"
"Downloading $url"
Invoke-WebRequest -Uri $url -OutFile $zip -UseBasicParsing
New-Item -ItemType Directory -Force $dest | Out-Null
Expand-Archive -Path $zip -DestinationPath $dest -Force
Remove-Item $zip
"SDK ready in $dest"
