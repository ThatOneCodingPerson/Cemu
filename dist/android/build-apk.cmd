@echo off
rem Double-click or run: build-apk.cmd [-BuildType dev^|release^|debug] [-Install] [-Clean]
setlocal
where pwsh >nul 2>nul
if %ERRORLEVEL%==0 (
    pwsh -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-apk.ps1" %*
) else (
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0build-apk.ps1" %*
)
set RESULT=%ERRORLEVEL%
rem keep the window open when started by double-click
if "%~1"=="" pause
exit /b %RESULT%
