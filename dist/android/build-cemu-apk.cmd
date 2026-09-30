@echo off
rem Double-click or run: build-cemu-apk.cmd [-Keystore path -KeyAlias alias] [-Version 0.6] [-Install] [-Clean]
rem Builds the regular "Cemu" APK (info.cemu.cemu), the one frontends recognize. See build-cemu-apk.ps1.
setlocal
where pwsh >nul 2>nul
if %ERRORLEVEL%==0 (
    pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-cemu-apk.ps1" %*
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-cemu-apk.ps1" %*
)
set RESULT=%ERRORLEVEL%
rem keep the window open when started by double-click
if "%~1"=="" pause
exit /b %RESULT%
