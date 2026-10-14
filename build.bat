@echo off
rem Build forza2 + Forza2Launcher with clang inside the MSVC x64 environment.
rem   build.bat [release|relwithdebinfo|debug]   (default: release)
rem Needs Visual Studio 2022 (or Build Tools) with the C++ workload, and LLVM
rem (clang). Fetches the ReXGlue SDK into _sdk\ on first run.
setlocal
set PRESET=win-amd64-%~1
if "%~1"=="" set PRESET=win-amd64-release
cd /d "%~dp0"

if not exist "_sdk\win-amd64\bin\rexglue.exe" (
  powershell -NoProfile -ExecutionPolicy Bypass -File scripts\get-sdk.ps1 || exit /b 1
)

set VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSDIR=%%i
if "%VSDIR%"=="" (
  echo Visual Studio 2022 / Build Tools with the C++ workload not found.
  exit /b 1
)
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

rem clang from LLVM; CMake and Ninja from PATH, else the copies VS ships.
set PATH=%ProgramFiles%\LLVM\bin;%PATH%;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja
where clang++ >nul 2>nul || (echo clang++ not found: install LLVM ^(winget install LLVM.LLVM^). & exit /b 1)

cmake --preset %PRESET% -DCMAKE_PREFIX_PATH="%~dp0_sdk\win-amd64" || exit /b 1
cmake --build --preset %PRESET% || exit /b 1
