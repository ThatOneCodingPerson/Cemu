# Android build scripts

## Windows (recommended)
Double-click `build-apk.cmd`, or run it from a terminal:

```powershell
.\dist\android\build-apk.ps1                     # dev build (default)
.\dist\android\build-apk.ps1 -Install            # build + install on all adb devices
.\dist\android\build-apk.ps1 -BuildType release  # release build
.\dist\android\build-apk.ps1 -Clean              # rebuild native code from scratch
```

The APK ends up in `dist/android/output/` as `Cemu-<version>-<commit>-<type>.apk`, with a copy at `Cemu-latest-<type>.apk`. Build logs go to `dist/android/output/logs/`.

The first run downloads and installs everything it needs, so nothing has to be installed beforehand:

| Tool | Location |
|---|---|
| JDK 21 | `%LOCALAPPDATA%\CemuAndroidBuild` |
| Android SDK command-line tools, NDK, platform, CMake | `%LOCALAPPDATA%\Android\Sdk` (Android Studio uses the same location) |
| Portable MinGW GCC (only when Visual Studio C++ isn't installed; vcpkg needs a compiler for a few host tools) | `%LOCALAPPDATA%\CemuAndroidBuild` |

That's roughly 6–8 GB. The first build also compiles all native dependencies with vcpkg, which takes a long time. Later builds reuse vcpkg's binary cache and are much faster.

## Build types
| Type | App id | Name on device | Notes |
|---|---|---|---|
| `dev` | `info.cemu.cemu.dev` | Cemu Dev | Optimized. Installs next to a release build without touching its data. Use it for testing. |
| `release` | `info.cemu.cemu` | Cemu | Signed with `ANDROID_STORE_FILE` / `ANDROID_KEY_ALIAS` / `ANDROID_KEY_STORE_PASSWORD` if set, otherwise with the debug key. |
| `debug` | `info.cemu.cemu.debug` | Cemu debug | Unoptimized native code, so emulation is very slow. Single process. |

## The regular "Cemu" app (for frontends)
Frontends and launchers (ES-DE, Daijishō, Cocoon…) look for the app id `info.cemu.cemu` and start games through `info.cemu.cemu/.emulation.EmulationActivity`. "Cemu Dev" has a different id, so they don't see it. To build the regular app, double-click `build-cemu-apk.cmd` or run:

```powershell
.\dist\android\build-cemu-apk.ps1                                          # signed with this PC's debug key
.\dist\android\build-cemu-apk.ps1 -Keystore D:\keys\release.jks -KeyAlias cemu   # signed with your release key
.\dist\android\build-cemu-apk.ps1 -Version 0.6                             # version shown in the app
```

It builds the `release` type, checks that the APK's app id is `info.cemu.cemu` and prints which key signed it. The APK is `dist/android/output/Cemu-<version>-<commit>-release.apk`.

**The signing key matters.** Android only installs an APK as an update of an installed Cemu when both are signed with the same key:
- With your release keystore (the one behind your GitHub releases), it updates that Cemu and keeps its data.
- With the debug key, it installs on a device without Cemu, and it updates other debug-key builds from the same PC (keep `%USERPROFILE%\.android\debug.keystore`). A Cemu signed with another key has to be uninstalled first, and that deletes its data in `Android/data/info.cemu.cemu` (saves, keys, shader caches) unless a custom data folder is set. Back up first.

The keystore password comes from `ANDROID_KEY_STORE_PASSWORD` or is asked for; it is used for both the store and the key, like the release CI workflow does. `-Install` never uninstalls anything: with a key mismatch the install just fails.

## Collecting logs from a device
```powershell
.\dist\android\pull-logs.ps1 -Clear   # before a test session
.\dist\android\pull-logs.ps1          # afterwards: logcat, crash/ANR reports, Cemu's log.txt/crash.txt
```

## Linux / WSL / macOS
`dist/android/build-apk.sh [dev|release|debug] [--install] [--clean]`

This needs `JAVA_HOME` (JDK 17–21) and `ANDROID_HOME` with cmdline-tools already set up. It mirrors the CI release workflow, including the translation step when `msgunfmt` from gettext is installed.

## Notes
- The repository path may contain spaces. The Windows script maps the repo to a drive letter with `subst` (remembered in `dist/android/.build-drive`), because vcpkg's autotools-based ports break on spaces.
- Toolchain versions (NDK, compileSdk, CMake) are read from `src/android/app/build.gradle.kts`. Update them there. The JDK, cmdline-tools and MinGW downloads are pinned at the top of `build-apk.ps1`, together with their checksums.
