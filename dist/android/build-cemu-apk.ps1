<#
.SYNOPSIS
    Builds the regular "Cemu" APK (app id info.cemu.cemu), the one frontends and launchers recognize.

.DESCRIPTION
    Same app id, name and game-launch activity as the Cemu Android releases, so frontends (ES-DE, Daijisho,
    Cocoon and others) find it and can start games with it: info.cemu.cemu/.emulation.EmulationActivity with an
    ACTION_VIEW intent and the game's content:// URI. The dev build (build-apk.ps1's default, "Cemu Dev",
    info.cemu.cemu.dev) is for testing next to it and isn't recognized by frontends.

    This wraps build-apk.ps1 -BuildType release (same automatic toolchain setup), then checks the APK's app id and
    shows which key signed it.

    The signing key decides whether it installs over a Cemu that is already on the device:
    - Without -Keystore it is signed with this PC's debug key (%USERPROFILE%\.android\debug.keystore; back it up,
      later builds from this PC then update each other). It installs on a device without Cemu. Android refuses it
      as an update of a Cemu signed with another key, and uninstalling that Cemu first deletes its data in
      Android/data/info.cemu.cemu (saves, keys, shader caches) unless a custom data folder is set. Back up first.
    - With -Keystore it is signed with your release key (the one your GitHub releases use), so it updates that
      Cemu and keeps its data.

.PARAMETER Keystore
    Release keystore (.jks/.keystore). The password is read from ANDROID_KEY_STORE_PASSWORD or asked for; it is
    used for the store and the key (as in the release CI workflow).

.PARAMETER KeyAlias
    Key alias in the keystore. Default: ANDROID_KEY_ALIAS.

.PARAMETER Version
    Version shown in the app, "major.minor" (e.g. 0.6). Default: the one in build.gradle.kts. Changing it
    rebuilds the native code.

.PARAMETER Install
    Also install on the connected adb devices (or only -Device). This never uninstalls anything: a Cemu signed with
    another key makes the install fail instead.

.PARAMETER Device
    adb serial for -Install.

.PARAMETER Clean
    Rebuild from scratch (see build-apk.ps1).

.EXAMPLE
    .\dist\android\build-cemu-apk.ps1
    .\dist\android\build-cemu-apk.ps1 -Keystore D:\keys\cemu-release.jks -KeyAlias cemu
    .\dist\android\build-cemu-apk.ps1 -Version 0.6
#>
[CmdletBinding()]
param(
    [string]$Keystore,
    [string]$KeyAlias,
    [ValidatePattern('^\d+\.\d+$')]
    [string]$Version,
    [switch]$Install,
    [string]$Device,
    [switch]$Clean
)

$ErrorActionPreference = 'Stop'

function Write-Step([string]$Message) { Write-Host "==> $Message" -ForegroundColor Cyan }
function Write-Note([string]$Message) { Write-Host "    $Message" -ForegroundColor DarkGray }
function Fail([string]$Message) { Write-Host "ERROR: $Message" -ForegroundColor Red; exit 1 }

$AppId = 'info.cemu.cemu'
$LaunchActivity = "$AppId/$AppId.emulation.EmulationActivity"
$OutputDir = Join-Path $PSScriptRoot 'output'

# environment for the Gradle run only; restored afterwards so nothing (like the password) stays in this session
$envNames = 'ANDROID_STORE_FILE', 'ANDROID_KEY_ALIAS', 'ANDROID_KEY_STORE_PASSWORD', 'EMULATOR_VERSION_MAJOR', 'EMULATOR_VERSION_MINOR'
$savedEnv = @{}
foreach ($name in $envNames) { $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }

try {
    if ($Keystore) {
        if (-not (Test-Path $Keystore -PathType Leaf)) { Fail "Keystore not found: $Keystore" }
        $alias = if ($KeyAlias) { $KeyAlias } else { $env:ANDROID_KEY_ALIAS }
        if (-not $alias) { Fail 'Give the key alias with -KeyAlias (or set ANDROID_KEY_ALIAS).' }
        if (-not $env:ANDROID_KEY_STORE_PASSWORD) {
            $secure = Read-Host -AsSecureString "Password for $(Split-Path $Keystore -Leaf)"
            $env:ANDROID_KEY_STORE_PASSWORD = [Runtime.InteropServices.Marshal]::PtrToStringBSTR(
                [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secure))
        }
        $env:ANDROID_STORE_FILE = (Resolve-Path $Keystore).Path
        $env:ANDROID_KEY_ALIAS = $alias
        Write-Step "Signing with your release key ($alias in $(Split-Path $Keystore -Leaf))"
    }
    else {
        # build.gradle.kts signs release builds with ANDROID_STORE_FILE whenever it is set
        Remove-Item Env:ANDROID_STORE_FILE -ErrorAction SilentlyContinue
        Write-Step 'Signing with the debug key of this PC (no -Keystore given)'
    }

    if ($Version) {
        $parts = $Version.Split('.')
        $env:EMULATOR_VERSION_MAJOR = $parts[0]
        $env:EMULATOR_VERSION_MINOR = $parts[1]
        Write-Note "version $Version"
    }

    $buildArgs = @{ BuildType = 'release' }
    if ($Install) { $buildArgs.Install = $true }
    if ($Device) { $buildArgs.Device = $Device }
    if ($Clean) { $buildArgs.Clean = $true }
    & (Join-Path $PSScriptRoot 'build-apk.ps1') @buildArgs
    if ($LASTEXITCODE -ne 0) { Fail "build-apk.ps1 failed (exit $LASTEXITCODE); see the messages above and dist\android\output\logs" }
}
finally {
    foreach ($name in $envNames) { [Environment]::SetEnvironmentVariable($name, $savedEnv[$name], 'Process') }
}

# ---------------------------------------------------------------------------------------------
# Check the result: app id, name, signer
# ---------------------------------------------------------------------------------------------
$apk = Get-ChildItem $OutputDir -Filter 'Cemu-*-release.apk' | Where-Object { $_.Name -notlike 'Cemu-latest-*' } |
    Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $apk) { Fail "No release APK in $OutputDir" }

$sdk = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else { Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
$buildTools = Get-ChildItem (Join-Path $sdk 'build-tools') -Directory -ErrorAction SilentlyContinue |
    Sort-Object { [version]($_.Name -replace '-.*$', '') } -Descending | Select-Object -First 1
$isDebugSigned = $null
if ($buildTools) {
    $badging = & (Join-Path $buildTools.FullName 'aapt2.exe') dump badging $apk.FullName 2>$null
    $packageLine = $badging | Where-Object { $_ -like 'package:*' } | Select-Object -First 1
    $labelLine = $badging | Where-Object { $_ -like 'application-label:*' } | Select-Object -First 1
    if ($packageLine -notmatch "name='$([regex]::Escape($AppId))'") { Fail "Unexpected app id in the APK: $packageLine" }
    Write-Note $packageLine
    if ($labelLine) { Write-Note $labelLine }

    $certs = & (Join-Path $buildTools.FullName 'apksigner.bat') verify --print-certs $apk.FullName 2>$null
    $subject = $certs | Where-Object { $_ -match 'certificate DN:' } | Select-Object -First 1
    $digest = $certs | Where-Object { $_ -match 'certificate SHA-256 digest:' } | Select-Object -First 1
    if ($subject) { Write-Note $subject.Trim() }
    if ($digest) { Write-Note $digest.Trim() }
    $isDebugSigned = [bool]($subject -match 'CN=Android Debug')
}
else {
    Write-Warning 'Android build-tools not found; the APK was not checked.'
}

Write-Host ''
Write-Host '============================================================' -ForegroundColor Green
Write-Host " Cemu APK ($AppId)" -ForegroundColor Green
Write-Host " $($apk.FullName)" -ForegroundColor Green
Write-Host " Frontends launch: $LaunchActivity" -ForegroundColor Green
Write-Host '============================================================' -ForegroundColor Green
if ($isDebugSigned) {
    Write-Host ' Signed with the debug key of this PC. It updates only a Cemu built on this PC.' -ForegroundColor Yellow
    Write-Host ' A Cemu signed with another key must be uninstalled first, which deletes its data in' -ForegroundColor Yellow
    Write-Host ' Android/data/info.cemu.cemu (saves, keys, shader caches): back it up first.' -ForegroundColor Yellow
}
exit 0
