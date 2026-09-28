<#
.SYNOPSIS
    One-shot Android APK build for Cemu (Windows host).

.DESCRIPTION
    Bootstraps everything the Android build needs (JDK 21, Android SDK cmdline-tools,
    platform, NDK, CMake), then runs Gradle and copies the APK to dist\android\output
    with an obvious name. Safe to re-run: every bootstrap step is skipped when already done.

    Toolchain versions are read from src/android/app/build.gradle.kts (ndkVersion, compileSdk,
    cmake version), so updating Gradle is enough to update this script.

.PARAMETER BuildType
    dev     (default) optimized build, app id info.cemu.cemu.dev, label "Cemu Dev". Installs next to a release build.
    release optimized build, app id info.cemu.cemu. Signed with ANDROID_STORE_FILE if set, else the debug key.
    debug   unoptimized native code (slow emulation), app id info.cemu.cemu.debug.

.PARAMETER Install
    After building, install the APK on every connected adb device (or only -Device).

.PARAMETER Device
    adb serial to install to (see: adb devices).

.PARAMETER Clean
    Delete Gradle and native (CMake/vcpkg) build outputs first. vcpkg's binary cache keeps this reasonably fast.

.PARAMETER NoBootstrap
    Don't download anything; fail if a tool is missing.

.EXAMPLE
    .\dist\android\build-apk.ps1
    .\dist\android\build-apk.ps1 -Install
    .\dist\android\build-apk.ps1 -BuildType release
#>
[CmdletBinding()]
param(
    [ValidateSet('dev', 'release', 'debug')]
    [string]$BuildType = 'dev',
    [switch]$Install,
    [string]$Device,
    [switch]$Clean,
    [switch]$NoBootstrap
)

$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
[Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12

# ---------------------------------------------------------------------------------------------
# Pinned download (update together; checksum from https://developer.android.com/studio#command-tools)
# ---------------------------------------------------------------------------------------------
$CmdlineToolsUrl = 'https://dl.google.com/android/repository/commandlinetools-win-15859902_latest.zip'
$CmdlineToolsSha256 = '90ae805d20434428bffcb699c290860f19bb5f66a67e6b330067e3de801fb04a'
$JdkMajor = 21 # same as CI (.github/workflows/deploy_release_android.yml)
# vcpkg builds a few host tools (pkgconf, compiler detection) for the *host* triplet. Without Visual
# Studio C++ we use x64-mingw-static with this portable MinGW-w64 GCC (https://winlibs.com, GitHub releases)
$MinGwUrl = 'https://github.com/brechtsanders/winlibs_mingw/releases/download/15.3.0posix-14.0.0-ucrt-r1/winlibs-x86_64-posix-seh-gcc-15.3.0-mingw-w64ucrt-14.0.0-r1.zip'
$MinGwSha256 = 'ae31de2d5c1da830422b2b65384e3ad17f59c4daa8f11d9234e67090c32734e2'

$ScriptDir = $PSScriptRoot
$RepoRoot = (Resolve-Path (Join-Path $ScriptDir '..\..')).Path
$OutputDir = Join-Path $ScriptDir 'output'
$LogDir = Join-Path $OutputDir 'logs'
$ToolsDir = Join-Path $env:LOCALAPPDATA 'CemuAndroidBuild'
$GradleFile = Join-Path $RepoRoot 'src\android\app\build.gradle.kts'
$StartTime = Get-Date

function Write-Step([string]$Message) { Write-Host "==> $Message" -ForegroundColor Cyan }
function Write-Note([string]$Message) { Write-Host "    $Message" -ForegroundColor DarkGray }
function Fail([string]$Message) { Write-Host "ERROR: $Message" -ForegroundColor Red; exit 1 }

function Get-GradleValue([string]$Pattern, [string]$Name) {
    $match = Select-String -Path $GradleFile -Pattern $Pattern | Select-Object -First 1
    if (-not $match) { Fail "Could not read $Name from $GradleFile" }
    return $match.Matches[0].Groups[1].Value
}

function Invoke-Download([string]$Url, [string]$Destination, [string]$Sha256) {
    Write-Note "downloading $Url"
    $tmp = "$Destination.part"
    if (Get-Command curl.exe -ErrorAction SilentlyContinue) {
        & curl.exe -L --fail --retry 3 -o $tmp $Url
        if ($LASTEXITCODE -ne 0) { Fail "Download failed: $Url" }
    }
    else {
        Invoke-WebRequest -Uri $Url -OutFile $tmp -UseBasicParsing
    }
    if ($Sha256) {
        $actual = (Get-FileHash -Algorithm SHA256 $tmp).Hash
        if ($actual -ine $Sha256) {
            Remove-Item $tmp -Force
            Fail "Checksum mismatch for $Url (expected $Sha256, got $actual)"
        }
    }
    Move-Item -Force $tmp $Destination
}

function Expand-Zip([string]$Zip, [string]$Destination) {
    New-Item -ItemType Directory -Force $Destination | Out-Null
    # tar.exe (bsdtar, built into Windows 10+) is much faster than Expand-Archive
    & tar.exe -xf $Zip -C $Destination
    if ($LASTEXITCODE -ne 0) { Expand-Archive -Force -Path $Zip -DestinationPath $Destination }
}

function Get-JavaMajor([string]$JavaHome) {
    $java = Join-Path $JavaHome 'bin\java.exe'
    if (-not (Test-Path $java)) { return 0 }
    $release = Join-Path $JavaHome 'release'
    if (Test-Path $release) {
        $line = Select-String -Path $release -Pattern 'JAVA_VERSION="(\d+)' | Select-Object -First 1
        if ($line) { return [int]$line.Matches[0].Groups[1].Value }
    }
    return 0
}

# ---------------------------------------------------------------------------------------------
# JDK: reuse JAVA_HOME if it is 17..21, else use/download a private Temurin 21
# ---------------------------------------------------------------------------------------------
function Get-Jdk {
    if ($env:JAVA_HOME) {
        $major = Get-JavaMajor $env:JAVA_HOME
        if ($major -ge 17 -and $major -le $JdkMajor) { return $env:JAVA_HOME }
    }
    $jdkRoot = Join-Path $ToolsDir "jdk-$JdkMajor"
    $existing = Get-ChildItem $jdkRoot -Directory -ErrorAction SilentlyContinue | Where-Object { Test-Path (Join-Path $_.FullName 'bin\java.exe') } | Select-Object -First 1
    if ($existing) { return $existing.FullName }
    if ($NoBootstrap) { Fail "No JDK $JdkMajor found (set JAVA_HOME or drop -NoBootstrap)" }

    Write-Step "Installing Temurin JDK $JdkMajor into $jdkRoot"
    $api = "https://api.adoptium.net/v3/assets/latest/$JdkMajor/hotspot?architecture=x64&image_type=jdk&os=windows&vendor=eclipse"
    $asset = (Invoke-RestMethod -Uri $api -UseBasicParsing) | Where-Object { $_.binary.package.name -like '*.zip' } | Select-Object -First 1
    if (-not $asset) { Fail "Could not find a JDK $JdkMajor zip via $api" }
    New-Item -ItemType Directory -Force $ToolsDir | Out-Null
    $zip = Join-Path $ToolsDir $asset.binary.package.name
    Invoke-Download $asset.binary.package.link $zip $asset.binary.package.checksum
    Expand-Zip $zip $jdkRoot
    Remove-Item $zip -Force
    $existing = Get-ChildItem $jdkRoot -Directory | Where-Object { Test-Path (Join-Path $_.FullName 'bin\java.exe') } | Select-Object -First 1
    if (-not $existing) { Fail "JDK extraction failed in $jdkRoot" }
    return $existing.FullName
}

# ---------------------------------------------------------------------------------------------
# Android SDK
# ---------------------------------------------------------------------------------------------
function Get-SdkRoot {
    foreach ($candidate in @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT)) {
        if ($candidate -and (Test-Path $candidate)) { return $candidate }
    }
    return (Join-Path $env:LOCALAPPDATA 'Android\Sdk')
}

function Get-SdkManager([string]$Sdk) {
    $sdkmanager = Join-Path $Sdk 'cmdline-tools\latest\bin\sdkmanager.bat'
    if (Test-Path $sdkmanager) { return $sdkmanager }
    if ($NoBootstrap) { Fail "Android cmdline-tools not found in $Sdk" }

    Write-Step "Installing Android command-line tools into $Sdk"
    New-Item -ItemType Directory -Force $Sdk, $ToolsDir | Out-Null
    $zip = Join-Path $ToolsDir 'cmdline-tools.zip'
    Invoke-Download $CmdlineToolsUrl $zip $CmdlineToolsSha256
    $staging = Join-Path $ToolsDir 'cmdline-tools-staging'
    if (Test-Path $staging) { Remove-Item -Recurse -Force $staging }
    Expand-Zip $zip $staging
    $target = Join-Path $Sdk 'cmdline-tools\latest'
    if (Test-Path $target) { Remove-Item -Recurse -Force $target }
    New-Item -ItemType Directory -Force (Split-Path $target) | Out-Null
    Move-Item (Join-Path $staging 'cmdline-tools') $target
    Remove-Item -Recurse -Force $staging, $zip
    return $sdkmanager
}

function Invoke-SdkManager([string]$SdkManager, [string[]]$Arguments) {
    # sdkmanager prints progress to stderr; don't let PowerShell treat that as an error
    $prev = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    $out = & $SdkManager @Arguments 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    $ErrorActionPreference = $prev
    if ($code -ne 0) { $out | Select-Object -Last 30 | ForEach-Object { Write-Host $_ }; Fail "sdkmanager $($Arguments -join ' ') failed ($code)" }
    return $out
}

function Select-SdkCMake([string]$SdkManager, [string]$Sdk, [version]$MinVersion) {
    # Prefer an installed CMake, newest 3.x first (CMake 4 dropped compatibility with
    # cmake_minimum_required < 3.5, which some vcpkg ports still declare).
    $installed = Get-ChildItem (Join-Path $Sdk 'cmake') -Directory -ErrorAction SilentlyContinue |
        Where-Object { Test-Path (Join-Path $_.FullName 'bin\cmake.exe') } |
        ForEach-Object { [pscustomobject]@{ Version = [version]$_.Name; Path = $_.FullName } } |
        Where-Object { $_.Version -ge $MinVersion }
    $pick = $installed | Where-Object { $_.Version.Major -eq 3 } | Sort-Object Version -Descending | Select-Object -First 1
    if (-not $pick) { $pick = $installed | Sort-Object Version -Descending | Select-Object -First 1 }
    if ($pick) { return $pick }
    if ($NoBootstrap) { Fail "No SDK CMake >= $MinVersion installed" }

    $list = Invoke-SdkManager $SdkManager @('--list', "--sdk_root=$Sdk")
    $available = $list | ForEach-Object { if ($_ -match '^\s*cmake;([\d\.]+)\s*\|') { [version]$matches[1] } } |
        Where-Object { $_ -ge $MinVersion } | Sort-Object -Unique -Descending
    $choice = $available | Where-Object { $_.Major -eq 3 } | Select-Object -First 1
    if (-not $choice) { $choice = $available | Select-Object -First 1 }
    if (-not $choice) { Fail "sdkmanager offers no CMake >= $MinVersion" }
    Write-Step "Installing SDK CMake $choice"
    Invoke-SdkManager $SdkManager @('--install', "cmake;$choice", "--sdk_root=$Sdk") | Out-Null
    return [pscustomobject]@{ Version = $choice; Path = (Join-Path $Sdk "cmake\$choice") }
}

# ---------------------------------------------------------------------------------------------
# vcpkg host toolchain: Visual Studio C++ if installed (vcpkg default x64-windows), else MinGW
# ---------------------------------------------------------------------------------------------
function Test-VisualStudioCpp {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) { return $false }
    $found = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    return [bool]$found
}

function Get-MinGwBin {
    $root = Join-Path $ToolsDir 'mingw'
    $bin = Join-Path $root 'mingw64\bin'
    if (Test-Path (Join-Path $bin 'x86_64-w64-mingw32-gcc.exe')) { return $bin }
    if ($NoBootstrap) { Fail 'No Visual Studio C++ and no MinGW found for vcpkg host tools' }
    Write-Step "Installing portable MinGW-w64 GCC into $root (for vcpkg host tools)"
    New-Item -ItemType Directory -Force $ToolsDir | Out-Null
    $zip = Join-Path $ToolsDir 'mingw.zip'
    Invoke-Download $MinGwUrl $zip $MinGwSha256
    if (Test-Path $root) { Remove-Item -Recurse -Force $root }
    Expand-Zip $zip $root
    Remove-Item $zip -Force
    if (-not (Test-Path (Join-Path $bin 'x86_64-w64-mingw32-gcc.exe'))) { Fail "MinGW extraction failed in $root" }
    return $bin
}

# ---------------------------------------------------------------------------------------------
# Space-free build root. The repo path may contain spaces (vcpkg's autotools-based ports such as
# openssl/libusb break on them) and is long (MAX_PATH). Map it to a stable drive letter with subst.
# ---------------------------------------------------------------------------------------------
function Get-BuildRoot([string]$Path) {
    if ($Path -notmatch '\s' -and $Path.Length -le 40) { return $Path }
    $mappings = @(subst) | ForEach-Object { if ($_ -match '^([A-Z]):\\: => (.+)$') { [pscustomobject]@{ Letter = $matches[1]; Target = $matches[2].TrimEnd('\') } } }
    foreach ($m in $mappings) { if ($m.Target -ieq $Path.TrimEnd('\')) { return "$($m.Letter):\" } }

    $driveFile = Join-Path $ScriptDir '.build-drive'
    $letters = @()
    if (Test-Path $driveFile) { $letters += (Get-Content $driveFile -Raw).Trim() }
    $letters += 'W', 'V', 'U', 'T', 'S', 'R', 'Q', 'P'
    foreach ($letter in $letters) {
        if (-not $letter) { continue }
        if (Test-Path "$($letter):\") { continue }
        & subst "$($letter):" $Path
        if ($LASTEXITCODE -eq 0) {
            Set-Content -Path $driveFile -Value $letter -NoNewline
            Write-Note "mapped $($letter): -> $Path (space-free build path)"
            return "$($letter):\"
        }
    }
    Write-Warning 'Could not map a drive letter; building from the original path (vcpkg may fail on spaces).'
    return $Path
}

function Get-AdbDevices([string]$Adb) {
    $lines = & $Adb devices
    return @($lines | Select-Object -Skip 1 | ForEach-Object { if ($_ -match '^(\S+)\s+device$') { $matches[1] } })
}

# =============================================================================================
New-Item -ItemType Directory -Force $OutputDir, $LogDir | Out-Null
$ndkVersion = Get-GradleValue 'ndkVersion\s*=\s*"([^"]+)"' 'ndkVersion'
$compileSdk = Get-GradleValue 'compileSdk\s*=\s*(\d+)' 'compileSdk'
$cmakeMin = [version](Get-GradleValue '^\s*version\s*=\s*"(\d+\.\d+\.\d+)\+?"' 'cmake version')

Write-Step "Cemu Android build: type=$BuildType, NDK $ndkVersion, compileSdk $compileSdk, CMake >= $cmakeMin"

$jdk = Get-Jdk
Write-Note "JDK: $jdk"
$env:JAVA_HOME = $jdk

$sdk = Get-SdkRoot
$sdkmanager = Get-SdkManager $sdk
Write-Note "SDK: $sdk"

$ndkDir = Join-Path $sdk "ndk\$ndkVersion"
$platformDir = Join-Path $sdk "platforms\android-$compileSdk"
$platformTools = Join-Path $sdk 'platform-tools\adb.exe'
$needed = @()
if (-not (Test-Path (Join-Path $ndkDir 'source.properties'))) { $needed += "ndk;$ndkVersion" }
if (-not (Test-Path $platformDir)) { $needed += "platforms;android-$compileSdk" }
if (-not (Test-Path $platformTools)) { $needed += 'platform-tools' }
if ($needed.Count -gt 0) {
    if ($NoBootstrap) { Fail "Missing SDK packages: $($needed -join ', ')" }
    Write-Step 'Accepting Android SDK licenses'
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    ('y' + [Environment]::NewLine) * 40 | & $sdkmanager --licenses "--sdk_root=$sdk" 2>&1 | Out-Null
    $ErrorActionPreference = $prev
    Write-Step "Installing SDK packages: $($needed -join ', ') (NDK is ~1-2 GB, be patient)"
    Invoke-SdkManager $sdkmanager (@('--install') + $needed + @("--sdk_root=$sdk")) | Out-Null
}
$cmake = Select-SdkCMake $sdkmanager $sdk $cmakeMin
Write-Note "NDK: $ndkDir"
Write-Note "CMake: $($cmake.Path) ($($cmake.Version))"

$env:ANDROID_HOME = $sdk
$env:ANDROID_SDK_ROOT = $sdk
$env:ANDROID_NDK_HOME = $ndkDir
$env:VCPKG_DISABLE_METRICS = '1'
$env:Path = "$jdk\bin;$(Join-Path $sdk 'platform-tools');$env:Path"
if ($cmake.Version.Major -ge 4) { $env:CMAKE_POLICY_VERSION_MINIMUM = '3.5' }
if (-not (Test-VisualStudioCpp)) {
    $mingwBin = Get-MinGwBin
    # appended (not prepended) so its make/cmake/python can't shadow the SDK tools
    $env:Path = "$env:Path;$mingwBin"
    $env:VCPKG_DEFAULT_HOST_TRIPLET = 'x64-mingw-static'
    Write-Note "vcpkg host triplet: x64-mingw-static ($mingwBin)"
}

# ---------------------------------------------------------------------------------------------
Write-Step 'Updating git submodules'
& git -C $RepoRoot submodule update --init --recursive
if ($LASTEXITCODE -ne 0) { Fail 'git submodule update failed' }

$buildRoot = Get-BuildRoot $RepoRoot
$androidDir = Join-Path $buildRoot 'src\android'

# local.properties (untracked): tells Gradle where the SDK and the chosen CMake are
$escape = { param($p) ($p -replace '\\', '\\' -replace ':', '\:') }
Set-Content -Path (Join-Path $androidDir 'local.properties') -Encoding ASCII -Value @(
    "sdk.dir=$(& $escape $sdk)",
    "cmake.dir=$(& $escape $cmake.Path)"
)

if ($Clean) {
    Write-Step 'Cleaning previous build outputs'
    foreach ($dir in @('app\build', 'app\.cxx', 'build')) {
        $full = Join-Path $androidDir $dir
        if (Test-Path $full) { Remove-Item -Recurse -Force $full }
    }
}

$task = ':app:assemble' + $BuildType.Substring(0, 1).ToUpper() + $BuildType.Substring(1)
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logFile = Join-Path $LogDir "build-$BuildType-$stamp.log"
Write-Step "Running Gradle $task (first build compiles all vcpkg deps; expect 30-90 min)"
Write-Note "log: $logFile"

Push-Location $androidDir
try {
    $prev = $ErrorActionPreference; $ErrorActionPreference = 'Continue'
    & .\gradlew.bat --console=plain $task 2>&1 | ForEach-Object { "$_" } | Tee-Object -FilePath $logFile
    $gradleExit = $LASTEXITCODE
    $ErrorActionPreference = $prev
}
finally {
    Pop-Location
}
if ($gradleExit -ne 0) { Fail "Gradle failed (exit $gradleExit). See $logFile" }

# ---------------------------------------------------------------------------------------------
$apkDir = Join-Path $RepoRoot "src\android\app\build\outputs\apk\$BuildType"
$apk = Get-ChildItem $apkDir -Filter '*.apk' -ErrorAction SilentlyContinue | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $apk) { Fail "No APK found in $apkDir" }

$versionName = 'unknown'
$metadata = Join-Path $apkDir 'output-metadata.json'
if (Test-Path $metadata) {
    $versionName = ((Get-Content $metadata -Raw | ConvertFrom-Json).elements | Select-Object -First 1).versionName
}
$baseVersion = $versionName -replace "-$BuildType$", ''
$gitHash = (& git -C $RepoRoot rev-parse --short HEAD).Trim()
& git -C $RepoRoot diff --quiet HEAD -- src
if ($LASTEXITCODE -ne 0) { $gitHash += '-dirty' }

$finalApk = Join-Path $OutputDir "Cemu-$baseVersion-$gitHash-$BuildType.apk"
$latestApk = Join-Path $OutputDir "Cemu-latest-$BuildType.apk"
Copy-Item -Force $apk.FullName $finalApk
Copy-Item -Force $apk.FullName $latestApk

$elapsed = (Get-Date) - $StartTime
Write-Host ''
Write-Host '============================================================' -ForegroundColor Green
Write-Host " APK READY ($BuildType, $([int]$elapsed.TotalMinutes) min)" -ForegroundColor Green
Write-Host " $finalApk" -ForegroundColor Green
Write-Host " (also copied to $latestApk)" -ForegroundColor Green
Write-Host '============================================================' -ForegroundColor Green

if ($Install) {
    $adb = Join-Path $sdk 'platform-tools\adb.exe'
    $targets = if ($Device) { @($Device) } else { Get-AdbDevices $adb }
    if ($targets.Count -eq 0) { Fail 'No adb device connected (enable USB debugging and accept the prompt on the device)' }
    foreach ($serial in $targets) {
        $model = (& $adb -s $serial shell getprop ro.product.model 2>$null)
        Write-Step "Installing on $serial ($model)"
        & $adb -s $serial install -r $finalApk
        if ($LASTEXITCODE -ne 0) { Fail "adb install failed on $serial" }
    }
}
