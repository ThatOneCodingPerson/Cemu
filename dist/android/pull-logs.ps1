<#
.SYNOPSIS
    Collect Cemu logs from a connected Android device into dist\android\output\logs\<timestamp>-<serial>\

.DESCRIPTION
    Pulls logcat, crash/ANR/tombstone summaries from dropbox (readable without root), device info,
    and Cemu's own log.txt / last_error.txt / crash file. Run with -Clear before a test session to empty the
    logcat buffer so the next pull only contains that session.

.PARAMETER Package
    App id to pull files for. Default info.cemu.cemu.dev (the dev build); release is info.cemu.cemu.

.PARAMETER Bugreport
    Also run `adb bugreport` (slow, large; includes full tombstones).

.EXAMPLE
    .\dist\android\pull-logs.ps1 -Clear        # before testing
    .\dist\android\pull-logs.ps1               # after testing
#>
[CmdletBinding()]
param(
    [string]$Device,
    [string]$Package = 'info.cemu.cemu.dev',
    [switch]$Clear,
    [switch]$Bugreport
)
$ErrorActionPreference = 'Stop'

$sdk = @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT, (Join-Path $env:LOCALAPPDATA 'Android\Sdk')) | Where-Object { $_ -and (Test-Path $_) } | Select-Object -First 1
$adb = Join-Path $sdk 'platform-tools\adb.exe'
if (-not (Test-Path $adb)) { Write-Host "adb not found; run build-apk.ps1 once to bootstrap the SDK" -ForegroundColor Red; exit 1 }

$serials = if ($Device) { @($Device) } else { @(& $adb devices | Select-Object -Skip 1 | ForEach-Object { if ($_ -match '^(\S+)\s+device$') { $matches[1] } }) }
if ($serials.Count -eq 0) { Write-Host 'No adb device connected (enable USB debugging and accept the prompt)' -ForegroundColor Red; exit 1 }

foreach ($serial in $serials) {
    if ($Clear) {
        & $adb -s $serial logcat -c
        Write-Host "Cleared logcat on $serial"
        continue
    }
    $model = (& $adb -s $serial shell getprop ro.product.model).Trim() -replace '[^\w\-]', '_'
    $dir = Join-Path $PSScriptRoot ("output\logs\{0}-{1}" -f (Get-Date -Format 'yyyyMMdd-HHmmss'), $model)
    New-Item -ItemType Directory -Force $dir | Out-Null
    Write-Host "==> $serial ($model) -> $dir" -ForegroundColor Cyan

    & $adb -s $serial logcat -d -b main,system,crash -v threadtime | Out-File -Encoding utf8 (Join-Path $dir 'logcat.txt')
    & $adb -s $serial logcat -d -s Cemu:* AndroidRuntime:* DEBUG:* libc:* ActivityManager:* -v threadtime | Out-File -Encoding utf8 (Join-Path $dir 'logcat-cemu.txt')
    foreach ($tag in 'SYSTEM_TOMBSTONE', 'data_app_native_crash', 'data_app_crash', 'data_app_anr') {
        & $adb -s $serial shell dumpsys dropbox --print $tag | Out-File -Encoding utf8 (Join-Path $dir "dropbox-$tag.txt")
    }
    & $adb -s $serial shell 'getprop ro.product.manufacturer; getprop ro.product.model; getprop ro.build.fingerprint; getprop ro.build.version.sdk; getprop ro.hardware.vulkan; dumpsys display | grep -E "mDisplayId|DisplayDeviceInfo"' | Out-File -Encoding utf8 (Join-Path $dir 'device.txt')
    & $adb -s $serial shell dumpsys package $Package | Select-String -Pattern 'versionName|versionCode|firstInstallTime|lastUpdateTime' | Out-File -Encoding utf8 (Join-Path $dir 'package.txt')

    $files = "/sdcard/Android/data/$Package/files"
    foreach ($name in 'log.txt', 'last_error.txt', 'crash.txt') {
        $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
        & $adb -s $serial pull "$files/$name" (Join-Path $dir $name) 2>&1 | Out-Null
        $ErrorActionPreference = $prev
    }
    if ($Bugreport) {
        Write-Host '    running adb bugreport (can take a few minutes)...'
        & $adb -s $serial bugreport (Join-Path $dir 'bugreport.zip')
    }
    Get-ChildItem $dir | Where-Object { $_.Length -eq 0 } | Remove-Item
    Get-ChildItem $dir | Format-Table Name, Length -AutoSize | Out-String | Write-Host
}
