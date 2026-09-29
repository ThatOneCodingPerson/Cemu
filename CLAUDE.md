# CLAUDE.md: Cemu Android fork (read this first, every session)

## What this is
- Cemu (Wii U emulator, C++20) with an **Android port** (Kotlin/Compose UI + JNI + Vulkan). This is the owner's fork.
- **Remotes:** `origin` = github.com/SapphireRhodonite/Cemu (the fork), `upstream` = github.com/SSimco/Cemu (the original Android port). The desktop project is cemu-project/Cemu.
- **Branches:**
  - `android-port`: mirrors upstream.
  - `android-port-dual`: released 0.5.x, which adds dual screen (pad on a second display), screen swap, and custom data storage with save sync.
  - `android-stability`: current work, branched from `android-port-dual`. It has **no upstream**, on purpose.
- **Target devices:** AYN Thor (dual screen; usually connected over adb) and ordinary single-screen phones. Everything must work single-screen; dual screen is an added capability.
- **Current phase:** see `docs/claude/TODO.md` (top section).

## Session protocol (mandatory)
1. **Start:** read `docs/claude/TODO.md` and the last entries of `docs/claude/SESSION_LOG.md`. If the task touches a bug, check `docs/claude/BUGS.md`.
2. **Fact-check before coding.** Your training data may be stale for Android, Vulkan, AGP/NDK, vcpkg and the emulator scene. Verify APIs, versions and "what other emulators do" online (WebSearch/WebFetch) or in primary sources such as NDK headers and upstream code. Record the source URL and date in the notes.
3. **Track progress.** Edit TODO.md as you go, never in a batch at the end. Status markers:
   - `[ ]` todo
   - `[~]` in progress
   - `[x]` done, with date + short commit hash
   - `[?]` built, awaiting the owner's device test
   - `[!]` blocked, with the reason

   Update the matching BUGS.md entry (status + commit).
4. **Every phase ends with an APK.**
   - Build with `dist/android/build-apk.ps1 -Install`, which puts "Cemu Dev" on the connected Thor.
   - Give the owner the APK path plus a short device test list (Thor dual screen + a single-screen phone).
   - Mark items `[?]` until the owner confirms they work on a device.
   - After the owner tests, pull logs with `dist/android/pull-logs.ps1`.
5. **End of session:** append a SESSION_LOG.md entry (done / next / open questions). Update this file if the map or invariants changed.
6. **Git:**
   - Commit locally, one logical fix group per commit.
   - Never push, tag, release or merge into `android-port-dual` without asking.
   - Never uninstall or touch the owner's release install (`info.cemu.cemu`) or its data on devices.
7. Keep diffs minimal and in the local style. Changes to core emulation (JIT, GPU accuracy) are upstream material; note them, don't sprawl.

## Build and debug
- **Build:** `dist\android\build-apk.ps1 [-BuildType dev|release|debug] [-Install] [-Device <serial>] [-Clean]`, or double-click `build-apk.cmd`. Linux/WSL: `dist/android/build-apk.sh`.
  - Output goes to `dist/android/output/Cemu-<ver>-<git>-<type>.apk` plus `Cemu-latest-<type>.apk`. Logs are in `dist/android/output/logs/`.
- **Build types:** `dev` = optimized, `info.cemu.cemu.dev`, "Cemu Dev", debug-signed, installs side by side with release. `release` = what CI ships. `debug` = unoptimized native (slow).
- **Toolchain:** everything is bootstrapped into `%LOCALAPPDATA%\Android\Sdk` and `%LOCALAPPDATA%\CemuAndroidBuild` (JDK 21).
  - Versions come from `src/android/app/build.gradle.kts`: AGP 9.1.1, Gradle 9.3.1, Kotlin 2.3.20, compileSdk 36, minSdk 30, targetSdk 35, NDK 29.0.14206865, arm64-v8a only.
  - Native deps come from vcpkg (submodule `dependencies/vcpkg`, manifest `vcpkg.json`, triplet arm64-android via `cmake/vcpkg_android.cmake`).
- **Gotchas:**
  - The repo path has a space, so the script builds from a `subst` drive (letter saved in `dist/android/.build-drive`).
  - The first build compiles all vcpkg ports (long); later builds reuse vcpkg's binary cache.
  - `.gitattributes` forces LF for `*.sh` and `gradlew`.
- **Tests:** `gradlew :app:testDevUnitTest` (JVM). This includes ArchUnit rules in `src/android/app/src/test/.../ArchitectureTests.kt`:
  - Feature packages must not depend on each other.
  - `nativeinterface/` depends on no other app package.

  There are no C++ unit tests. CI (`.github/workflows/*android*`) only assembles.
- **Logs:**
  - Native `cemuLog_log` goes to logcat tag `Cemu` (after Phase 1) and to `log.txt` in `/sdcard/Android/data/<appId>/files/`.
  - Java crashes go to the `AndroidRuntime` tag. Native crashes produce tombstones (`adb bugreport`, or `adb shell dumpsys dropbox --print SYSTEM_TOMBSTONE`).
  - `dist/android/pull-logs.ps1` collects all of these.

## Where is what
| Area | Location |
|---|---|
| Kotlin app | `src/android/app/src/main/java/info/cemu/cemu/`: `CemuApplication.kt` (loads `libCemuAndroid`, native init), `MainActivity.kt` (NavHost: games, settings, title manager, graphic packs, about), `emulation/` (EmulationActivity/Screen/ViewModel, `PadPresentation.kt`, touch, input overlay, hotkeys), `common/` (settings DataStore, storage, display utils, UI components), `settings/`, `games/`, `titlemanager/`, `graphicpacks/`, `provider/DocumentsProvider.kt` |
| Kotlin → native bridge | `nativeinterface/Native*.kt`. They are `external` funcs bound by name to `src/android/app/src/main/cpp/Native*.cpp`; there is no RegisterNatives. Native → Kotlin calls use cached `jclass` + static methods (see `NativeSwkbd.cpp`) |
| JNI glue | `src/android/app/src/main/cpp/`: `JNIUtils.h/.cpp` (GetEnv, strings, `HandleNativeException`, `FiberSafeJNICall`), `NativeEmulation.cpp` (init, surfaces, launch, pause), `NativeInput.cpp`, `AndroidFilesystemCallbacks.cpp` (SAF → `NativeFiles.kt`), `GameTitleLoader.cpp`, `NativeSettings.cpp` |
| Android "window system" | `src/gui/androidgui/AndroidWindowSystem.cpp` (global `WindowInfo`), interface in `src/gui/interface/WindowSystem.h` |
| Emulated system | `src/Cafe/`: `CafeSystem.cpp` (init/launch/pause/shutdown), `OS/libs/*` (HLE Cafe OS), `IOSU/`, `Filesystem/` (incl. `fscDeviceAndroidSAF.cpp`), `HW/Espresso/` (PPC interpreter + recompiler; AArch64 backend `Recompiler/BackendAArch64/`), `HW/Latte/` (GPU: `Core/LatteThread.cpp`, `Renderer/Vulkan/`) |
| Vulkan on Android | `Renderer/Vulkan/VulkanRenderer.cpp` (swapchains, present, pipeline cache), `SwapchainInfoVk.cpp`, `VulkanAPI.cpp` (loader + adrenotools custom drivers) |
| Common / config | `src/Common/` (`precompiled.h` aarch64 intrinsic shims, `ExceptionHandler/`, `android/` SAF file streams), `src/config/` (`CemuConfig` = `settings.xml`, `ActiveSettings` paths), `src/Cemu/Logging/` |
| Input / audio | `src/input/api/Android/`, `src/input/api/Device/` (phone sensors). Audio goes through `src/audio/CubebAPI.cpp` (AAudio) |
| Build | root `CMakeLists.txt` (vcpkg, Android bits), `src/CMakeLists.txt` (`CemuBin` static + JNI subdir), `src/android/app/build.gradle.kts` |

**Boot sequence:**
1. `CemuApplication.onCreate` → `initializeCemu` → JNI `initializeEmulation` → `CemuCommonInit` (`src/main.cpp`).
2. `EmulationViewModel.initializeEmulation` (IO thread): `prepareTitle` → `initializeSystems` → `initializeRenderer` (Vulkan loader, `new VulkanRenderer`) → `launchTitle` → `CafeSystem::LaunchForegroundTitle`. The GPU thread starts via `Latte_Start`.

**Processes (release/dev):**
- `EmulationActivity` runs in `:EmulationProcess` (`src/release|dev/AndroidManifest.xml`). The debug build uses a single process.
- The DataStore is multi-process (`common/settings/Settings.kt`).
- `MainActivity.startGame` saves `settings.xml` before launching.

**Threads:**
- UI/main: SurfaceHolder callbacks, JNI calls from Compose.
- IO coroutines: title preparation and launch.
- Latte GPU thread: all Vulkan rendering and presentation.
- PPC threads: `OSSched[core=N]`, with TIDs in `g_schedulerThreadIds`.
- cubeb audio.
- Pipeline compile threads.
- Game-list loader.

**Data:**
- User, config and cache all live in the external files dir, or in a custom root chosen via `CemuDataStorage.kt` (dual branch).
- `settings.xml` is native config. Kotlin UI prefs are in DataStore `appSettings.json`.
- `shaderCache/`, `graphicPacks/`, `mlc01/`, `log.txt`.
- Assets from repo `bin/` are copied to `<root>/data` when `hash.txt` changes.

## Invariants and hazards (keep this list current)
- **Surfaces:**
  - Implemented in f333fe7a (`docs/claude/PHASE2_DESIGN.md`): only the Latte thread creates or destroys swapchains and VkSurfaces, via `VulkanRenderer::SyncCanvasWindow`. JNI `setSurface`/`clearSurface` publish `{window, generation}` in `WindowSystem::AndroidCanvasInfo`, and the UI thread waits at most 500 ms for the ack. Don't reintroduce UI-thread swapchain access.
  - The GamePad swapchain presents non-blocking (MAILBOX/IMMEDIATE) so a 60 Hz second screen can't throttle the TV.
  - One VkSurface per ANativeWindow at a time.
  - `ANativeWindow_fromSurface` returns +1; release exactly once.
- **JNI:**
  - Check `ExceptionCheck` after every call into Kotlin.
  - Never let C++ exceptions escape a JNI function; wrap in `JNIUtils::HandleNativeException`.
  - `FindClass` only works on Java threads, so cache classes during init.
  - Native-called Kotlin methods need `@Keep` (R8 shrinks release/dev).
  - Calls from native code (especially PPC fiber threads) go through `JNIUtils::FiberSafeJNICall`, which runs on a pool of 4 persistent attached workers. Always follow them with `JNIUtils::CheckAndClearException`.
  - Strings convert via UTF-16 (`FromJString`/`ToJString`); never `NewStringUTF`/`GetStringUTFChars`, which use modified UTF-8.
- **Main thread:** no disk, SAF or network I/O, and no `runBlocking` on I/O. It must never block on the GPU thread without a timeout.
- **Quit:** `exit()` runs static destructors while the GPU thread lives, which crashes. Use the native quit path (`_exit` after flushing).
- **Dual screen:**
  - Key the Presentation on display *id*, not the `Display` object.
  - Handle display removal (the Presentation auto-dismisses) and `InvalidDisplayException` from `show()`.
  - Test single-screen devices too.
- **Settings JSON:** must decode with `ignoreUnknownKeys`. Schema drift otherwise silently resets everything, including the custom storage root.
  - Enums stored by name (e.g. `HotkeyAction` map keys) are append-only. An unknown enum *map key* fails decoding even with `coerceInputValues`.
- **Processes:** the main process owns the native settings and controller profiles (`saveSettings`, `saveInputs`). The emulation process (`:EmulationProcess`) loads them at game start. Don't write profiles from the emulation process; it would race the main process's in-memory copy.
- **Navigation:** back buttons call `navController.navigateBackSafely()` (`common/ui/extensions`), never a bare `popBackStack()`. A double tap otherwise pops the start destination and leaves a blank screen.
- **Key capture UI:** use a `Popup`, not a `Dialog`, when listening to `GamepadInputSource`. A Dialog window takes the key events away from the activity.
- **Driver fetcher:** the repo list lives in `settings/customdrivers/DriverReleases.kt`. Re-check that the repos still exist when touching it (one moved in 2026-09).
- **SAF paths:** native code names SAF documents `content://<auth>/tree/<encoded tree id>/document/<plain document id>` (`nativeinterface/SafNativePath.kt`, unit tested). Only `NativeFiles` converts to and from URIs; the parser also accepts older half-encoded paths stored in settings, the title cache and shortcuts.
- **Long tasks** (install, compression): wrap them in `ForegroundTasks.begin(...)`/`end()` (a dataSync foreground service), and make them resumable after process death (see the `.installing`/`.backup` order in `InstallTitleUseCase`).
- **Key cache:** `g_keyCache` is append-only (a deque) because `KeyCache_Reload` adds keys while the title scan reads them; never clear it outside `KeyCache_Prepare`.
- **Controller profiles:** after `InputManager::load` replaces an emulated controller, call `EmulatedControllerManager::GetController(i).Reload()`, or later edits go to the discarded object.
- **Android-only core additions** (overlay battery/thermal/frame time, KeyCache_Reload) sit behind `#if BOOST_PLAT_ANDROID` so desktop builds are unchanged.

## Conventions
- **C++** (`CODING_STYLE.md`, `.clang-format`):
  - Members `m_`, statics `s_`, camelCase locals, PascalCase functions and types, Allman braces.
  - Cemu int types (`uint32`, `sint32`…), `fmt::format`, `cemuLog_log(LogType::…)`.
  - Android-only code goes under `#if BOOST_PLAT_ANDROID`. Note that `BOOST_OS_LINUX` is also true on Android.
  - Don't reformat whole files.
- **Kotlin:** official style; Compose; feature packages stay independent (ArchUnit).
- **Commits:** imperative subject, body explains why, and end with the attribution trailer given by the harness.

## Docs index (`docs/claude/`)
- `TODO.md`: phased checklist plus the feature backlog. **The source of truth for progress.**
- `BUGS.md`: bug registry (IDs A#, I#, D#, N#) with location, severity, status and fix commit.
- `SESSION_LOG.md`: dated log of what each session did.
- `PHASE2_DESIGN.md`: the surface/swapchain/crash-handler design.
- `FEATURE_RESEARCH.md`: what other emulators offer, with sources, the gap analysis, and implementation notes.
