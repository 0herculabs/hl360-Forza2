@echo off
rem Developer run: the game straight from the build tree, against proj\assets,
rem logging to proj\run.log (tools\session.ps1 reads it). Players use
rem Forza2Launcher.exe instead. Extra arguments go to the exe
rem (e.g. --gamertag=Name). Set FORZA2_BUILD=relwithdebinfo for the build with
rem symbols (crash reports name the guest functions).
setlocal
if "%FORZA2_BUILD%"=="" set FORZA2_BUILD=release
"%~dp0out\build\win-amd64-%FORZA2_BUILD%\forza2.exe" --game_data_root="%~dp0assets" --log_file="%~dp0run.log" %*
