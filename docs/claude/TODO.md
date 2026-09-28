# TODO: source of truth for progress

**Legend:**
- `[ ]` todo
- `[~]` in progress
- `[x]` done (date, commit)
- `[?]` built, awaiting the owner's device test
- `[!]` blocked (reason)

Bug IDs refer to `BUGS.md`. The full plan and rationale are in the "Phases" section below and in `PHASE2_DESIGN.md`.

**Working branch:** `android-stability` (from `android-port-dual` 564bfdad). It has no upstream. Ask before any push or merge.

## Current focus
- **Phase 1 is built and awaiting the owner's device test.** APK: `dist/android/output/Cemu-0.5.1-92ecc623-dev.apk`.
- Next: Phase 2 (surface lifecycle), per `PHASE2_DESIGN.md`.
- **Blocker for the adb workflow:** the Thor has no USB debugging yet. As of 2026-09-28 it enumerates as MTP only (USB PID 4EE1) and `adb devices` is empty. The owner was asked to enable it.

---

## Phase 0: foundation
- [x] 0.1 Branch `android-stability` from `origin/android-port-dual`, with upstream unset (2026-09-28)
- [x] 0.2 `CLAUDE.md` future-self prompt (2026-09-28)
- [x] 0.3 `docs/claude/`: TODO, BUGS, SESSION_LOG, PHASE2_DESIGN, FEATURE_RESEARCH (2026-09-28)
- [x] 0.4 Build tooling in `dist/android/` (2026-09-28, 95337964)
  - [x] `build-apk.ps1` + `.cmd`: bootstraps JDK 21, cmdline-tools, NDK, platform, CMake 3.31.6 and WinLibs MinGW 15.3; builds from a subst drive; copies the APK
  - [x] vcpkg host tools without Visual Studio: `VCPKG_DEFAULT_HOST_TRIPLET=x64-mingw-static`
  - [x] `build-apk.sh` (Linux/WSL, mirrors CI including msgunfmt). Not yet run on Linux.
  - [x] `pull-logs.ps1`. Not yet run against a device.
  - [x] `dist/android/README.md` and the BUILD.md Android section
  - [x] Gradle `dev` build type (`info.cemu.cemu.dev`, "Cemu Dev", release manifest overlay, native RelWithDebInfo)
  - [x] Fixes: B1 hash path, B2 pkg-config on a Windows host, B3 `.gitattributes`, B4 hidapi on Android
- [x] 0.5 First successful `dev` APK (2026-09-28)
  - First full build: about 12 min for vcpkg plus about 5 min for Cemu, on this PC.
  - Incremental rebuild: about 1 min.
- [!] 0.6 Install on the Thor next to 0.5.2 and boot to the game list. Blocked: USB debugging is off.
- [!] 0.7 Confirm B1 on the owner's 0.5.2 install (`adb shell ls /sdcard/Android/data/info.cemu.cemu/files/data`). Same blocker.
- [x] 0.8 Commit Phase 0 (95337964; architecture fix 3f3939f1)
- [x] 0.9 Memory pointer saved (2026-09-28)

## Phase 1: crash visibility. Built in 92ecc623, awaiting device test
- [?] Logcat mirror for `cemuLog_log` (tag `Cemu`)
- [?] N5 settings JSON: `ignoreUnknownKeys` + `coerceInputValues`; unreadable file backed up to `appSettings.json.bad`
- [?] N1/N7 native `quitProcess` (flush, then `_exit`) + atexit fast-exit hook for core `exit()` paths
- [?] A1 `ShowErrorDialog` → `NativeErrors` bridge + `NativeErrorDialogHost` (in `ActivityContent`); blocks off the main thread for up to 2 min; `last_error.txt`, shown on the next MainActivity resume
- [?] A2/N14 crash handler: signal-safe note to logcat + `crash.txt`, previous handler restored, re-queued (tombstone); no SIGQUIT; re-entry guard
- [?] A3 Latte thread guard (catch `std::exception` → dialog → `_exit`)
- [?] A8 Vulkan loader result checked (renderer init error); loaded once
- [?] A9 driver `meta.json` validation, `libraryName` sanitized, copy error falls back to the system driver with a dialog
- [?] N2 NativeLocalization owns its keys; `shared_mutex`; local refs freed
- [?] N3 VPAD toggle, graphic packs (stale ids) and `initializeEmulation` no longer throw through JNI
- [x] Kotlin uncaught handler already chains to the default (verified, no change needed)
- [x] Build, commit (92ecc623); architecture tests pass
- **Device test (Thor and a single-screen phone):**
  1. Install `Cemu-latest-dev.apk` next to the release build. It should appear as "Cemu Dev" and boot to the game list.
  2. Launch a game, play 1 minute, then Quit from the drawer. It should return to the game list, with no "Cemu Dev keeps stopping" and no crash dialog.
  3. `adb logcat -s Cemu` should show emulator log lines.
  4. Optional: select a custom driver whose folder is broken or empty. You should get a dialog, and the game should still boot on the system driver.
  5. Report any crash; then I run `pull-logs.ps1`, since the tombstone and `crash.txt` should now exist.

## Phase 2: surface/swapchain lifecycle (design: PHASE2_DESIGN.md)
- [ ] Research: Azahar PR #2425 and #2399 diffs, NDK `native_window_jni.h` ownership note, VK_KHR_android_surface one-surface rule
- [ ] WindowSystem `AndroidCanvasInfo` (window + generation + ack)
- [ ] JNI `setSurface` / `clearSurface` (bounded wait); remove `initializeSurface` and `clearPadSurface`; fix `TestSurface` ref (A7)
- [ ] SwapchainInfoVk owns its window ref; `RecreateSurface` destroys the old surface first (N6) and handles the pad (A6)
- [ ] VulkanRenderer `SyncCanvasWindow` on the Latte thread at acquire and idle; ImGui fallback init; `ImguiBegin` order (N8); retryable recreate (N4)
- [ ] Remove `StopUsingPadAndWait` on Android (A4, A5)
- [ ] Pause and resume stop presents and audio (P5); `sTitlePaused` / `sSystemRunning` atomics (N15)
- [ ] Swap screens applied on the Latte thread (D5, N9)
- [ ] Kotlin: EmulationViewModel surface callbacks; launch waits for the main surface (K3); `initialized` only on success (A15)
- [ ] Kotlin: Presentation keyed on display id (D4); `InvalidDisplayException` / `onDisplayRemoved` (D6)
- [ ] Build, install, commit
- **Device test:** run the whole matrix in PHASE2_DESIGN.md.

## Phase 3: JNI, storage, lifecycle hardening
- [ ] JNI `ExceptionCheck` everywhere; NativeFiles try blocks; `isFile` fix (A10)
- [ ] Persistent JNI worker thread replaces `FiberSafeJNICall` (A11)
- [ ] UTF-8 via byte arrays (N13); SAF path percent-decoding with a round-trip design (names containing `:`, spaces, unicode)
- [ ] Manifest `configChanges` (A12); missing launch path handled (A15); `onNewIntent` (K2)
- [ ] Quit flow: flush the pipeline cache atomically, DataStore, settings, save sync off the main thread (A13, D2)
- [ ] Startup: hash written after the copy (A14); SAF sync off the main thread (D1); observer map thread-safe (D3)
- [ ] Kotlin crash fixes K1, K4, K5, K6, K7, K8, K9, K10, K11
- [ ] Main-thread I/O K12; native races N10, N11, N12, N16
- [ ] Build, install, commit
- **Device test:**
  - A game in a folder whose name contains `:`.
  - Connect or disconnect a BT controller mid-game.
  - Quit, relaunch: no pipeline re-compile stutter.
  - Custom SAF root: startup doesn't ANR.

## Phase 4: input
- [ ] Overlay `ACTION_CANCEL`, `resetInput` on rebuild and hide, DOWN ownership, Button multi-pointer (I2, I3)
- [ ] Re-verify CanvasOnTouchListener (I1)
- [ ] Drawer releases held keys; hotkey state cleared (K22); motion rotation (K23); `SensorEvent` copy (K20)
- [ ] Binding UI event copies (K21); ControllerCallbacks lock (K19); motion state after recreation (K16)
- [ ] Build, install, commit
- **Device test:**
  - Two-finger overlay presses.
  - A system gesture while holding a button.
  - Open the drawer while holding a controller button.

## Phase 5: dual screen and multi-device
- [ ] Research: `adb shell dumpsys display` on the Thor; Azahar and melonDS display selection; other dual-screen devices
- [ ] Per-swapchain present mode or pacing (P8); per-canvas DPI (P10)
- [ ] Auto-offer the pad on a second screen; persist swap screens; split ratio setting
- [ ] Single-screen devices: dual options hidden, no behavior change
- [ ] Build, install, commit
- **Device test:**
  - Thor: 120 Hz main screen not throttled; pad touch maps correctly.
  - Phone: no dual options.

## Phase 6: performance
- [ ] PPCTimer via `cntfrq_el0`; `_addcarry_u64` fix (P3)
- [ ] ADPF performance hint sessions (runtime-loaded, API 33+), falling back to affinity; sustained-performance option (P2)
- [ ] Vulkan pre-rotation (P1)
- [ ] Guard the BotW fork path (P6); fix CPU% on Android (P7); review compile threads (P9)
- [ ] Build, install, commit
- **Device test:** boot time; overlay FPS before and after in the same scene.

## Phase 7: feature research → FEATURE_RESEARCH.md
- [ ] Survey (with sources): Eden/Citron/Sudachi, Azahar, melonDS, Dolphin, NetherSX2, PPSSPP, Vita3K, DuckStation, RetroArch, Winlator/GameHub, other Cemu Android forks
- [ ] Deep dives the owner asked for: controller auto-mapping, save states (cemu PR #953, issue #2062), Eden-style driver manager, graphics options
- [ ] Gap table + implementation notes (with code locations) + prioritized backlog below

## Feature backlog (filled by Phase 7; each item becomes its own phase)
- (empty)

## Housekeeping
- [x] Deleted the stray `%TEMP%\k.txt` left by a planning agent (2026-09-28)
- [ ] B4 `vcpkg.json` duplicate hidapi; B5 dead `ENABLE_NSYSHID_LIBUSB` flag
