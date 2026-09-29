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
- Phases 1–6 are built and awaiting device tests (latest APK `dist/android/output/Cemu-latest-dev.apk`). Phase 7 research first pass is done.
- Next:
  - Owner device tests and logs.
  - D1/D2 decision (SAF save sync design).
  - Then the feature backlog, starting with the GPU driver fetcher.
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

## Phase 2: surface/swapchain lifecycle (design: PHASE2_DESIGN.md). Built in f333fe7a, awaiting device test
- [x] Research (2026-09-28):
  - NDK `native_window_jni.h` confirms `ANativeWindow_fromSurface` returns an acquired reference.
  - Azahar PR #2425 (merged 2026) uses the same lessons: ignore duplicate `surfaceChanged` for the same window (NATIVE_WINDOW_IN_USE), release the old window, and serialize with renderer creation.
- [?] WindowSystem `AndroidCanvasInfo` (window + generation + ack)
- [?] JNI `setSurface` (dedupe, one reference) / `clearSurface` (500 ms bounded wait); `initializeSurface` and `clearPadSurface` removed; `TestSurface` reference fixed (A7)
- [?] SwapchainInfoVk owns its window reference; `RecreateSurface` destroys first (N6) and works for the pad (A6)
- [?] `VulkanRenderer::SyncCanvasWindow` on the GPU thread (acquire and idle); retry after 250 ms (N4); ImGui fallback init and re-init; `ImguiBegin` acquires first (N8)
- [?] `StopUsingPadAndWait` removed on Android (A4, A5)
- [?] Pause stops presenting and audio (P5); the GPU thread sleeps while paused instead of busy-spinning; `sTitlePaused` / `sSystemRunning` atomic (N15)
- [?] Swap screens applied on the GPU thread (D5, N9)
- [?] ViewModel: callbacks only publish; pause and resume follow the main surface; a title launched in the background gets paused (K3)
- [?] Presentation keyed on display id (D4); `InvalidDisplayException` caught (D6)
- [ ] A15 leftovers: `initialized` flag semantics and error codes (Phase 3)
- **Device test:** run the matrix in PHASE2_DESIGN.md. The most important cases:
  1. Rotate (if unlocked), press Home and return, 20 times in a row, mid-game: no crash, no freeze, and the game resumes with sound.
  2. While a game loads, press Home, wait 10 s, then return: the game should not be running in the background (no audio). It continues when you're back.
  3. Toggle "Show pad" 10 times quickly: no hang.
  4. Thor: toggle "External PAD screen" on and off, "Swap screens", and "Rotate external screen left". The pad shows on the bottom screen and touch works.
  5. Thor: turn the bottom screen off and on with the dual-screen button while playing: no crash.
  6. Background the app for several minutes while paused: the battery shouldn't drain (the GPU thread no longer spins).

## Phase 3: JNI, storage, lifecycle hardening. Built in 91f156d3 and eae0b51f, awaiting device test
- [?] JNI `CheckAndClearException` after every native→Kotlin call; NativeFiles never throws; `isFile` fixed (A10)
- [?] Persistent JNI worker pool replaces per-call threads in `FiberSafeJNICall` (A11)
- [?] UTF-8 conversion via UTF-16 (emoji-safe), null-safe (N13)
- [ ] SAF path percent-decoding with a round-trip design (names containing `:`, spaces, unicode). Needs research and tests on the device.
- [?] Manifest `configChanges` (A12); missing launch path handled (A15, partial); relaunch while running attaches to the running title (K2)
- [?] Pipeline cache: atomic write, flush on quit (2 s max), save on pause (A13)
- [?] Data files: hash written last, cross-process file lock (A14)
- [!] **D1/D2 need an owner decision:** SAF save sync runs synchronously on the main thread at startup (every process, including each game launch) and on quit/background. The fix needs a design choice:
  - (a) keep the sync but run it on a background thread, and gate game launch and the save UI on its completion (a "Syncing saves…" screen), or
  - (b) only sync in the main process and before launch, with a progress dialog.
  - Don't just move it off the main thread: a game could read or write saves mid-import and the export could overwrite newer data.
- [?] D3 observer map thread-safe
- [?] Kotlin crash fixes K1, K4, K5, K6, K7, K8, K9, K10, K11
- [?] Native races N10, N11, N12
- [ ] D7 cross-process "is emulation running" state (main process can't see the emulation process)
- [ ] K13 title install: foreground service / WorkManager; K14 WUA compression uses the filtered list; K15 account deletion
- **Device test:**
  1. Revoke the games folder permission (Android settings → app → storage), open the game list: no crash, just no games.
  2. A game folder or file name with emoji or non-Latin characters shows correctly and launches.
  3. Connect or disconnect a Bluetooth controller and toggle dark mode mid-game: the game keeps running (no restart).
  4. Play a new area, quit from the menu, relaunch: less shader or pipeline stutter in that area than before.
  5. Launch a game, press Home, tap the game's home-screen shortcut: it returns to the running game (no crash). Tap a *different* game's shortcut: a "quit first" message.
  6. Games list with many games, including some with odd or missing names: no crash when sorting.

## Phase 4: input. Built in ea67dfae, awaiting device test
- [?] Overlay `ACTION_CANCEL`, release on rebuild or hide, multi-pointer guard (I2)
- [?] The overlay owns gestures and forwards non-button touches to the game screen (I3)
- [x] CanvasOnTouchListener re-verified by review (I1, fixed on the dual branch)
- [?] Drawer open and activity pause release held controller keys and axes; hotkey state reset (K22)
- [?] Gyro follows 180° flips (K23); `SensorEvent` copy (K20); binding UI event copies (K21); rumble callbacks lock (K19); null input device
- [ ] K16 motion toggle after activity recreation (rare now that config changes don't recreate)
- **Device test:**
  1. Touch the game screen with one finger, then press A with another: A registers.
  2. Hold a button, swipe down the notification shade: the button releases.
  3. Hold a controller button, open the in-game menu, close it: the button isn't stuck.
  4. Phone gyro game: flip the device 180°; tilt direction stays correct.
  5. Touch the GamePad screen area while the overlay is visible: touch still works.

## Phase 5: dual screen and multi-device
- [x] Research: Azahar's secondary-display PRs #617, #1371 and #1385; the owner's own Azahar PR #1341; melonDS; Thor specs (FEATURE_RESEARCH.md §4)
- [ ] `adb shell dumpsys display` on the Thor, to confirm how the bottom panel is exposed (needs USB debugging)
- [?] Pad swapchain presents non-blocking (MAILBOX/IMMEDIATE) and never drives host vsync (P8), 76ac98f1
- [?] Per-display pad DPI: `setPadDPI` from `PadPresentation`, reset when the pad returns to the main display (P10), fe594253
- [?] Swap screens persisted; a one-time "Second screen detected: show GamePad there" snackbar, fe594253
- [?] Single-screen devices: "External PAD screen" and "Rotate external screen left" are hidden without a second display, fe594253
- [ ] Split ratio setting for the inline pad (instead of a fixed 50/50); display picker when there are 2+ candidates
- **Device test:**
  - Thor: 120 Hz main screen not throttled when the pad is on the bottom screen (overlay FPS).
  - Pad touch maps correctly; the pad notifications are sized right on the bottom screen.
  - Swap screens is remembered after quitting.
  - Thor, first launch after install: the "Second screen detected" snackbar appears once.
  - Phone: no dual-screen options in the menu.

## Phase 6: performance
- [?] PPCTimer via `cntfrq_el0` (no 3 s wait at launch); `_addcarry_u64` fix (P3), 76ac98f1
- [?] ADPF performance hint session for the PPC and GPU threads, frame reports vs 16.67 ms (P2), 76ac98f1. Later: adaptive target for 30 fps titles; sustained-performance option
- [ ] Vulkan pre-rotation (P1). Engine work; measure before and after on the Thor
- [?] BotW fork path guarded (P6), 76ac98f1. P7 is wontfix (per-core CPU only, not exposed on Android)
- [ ] Review pipeline compile thread count and priority (P9)
- **Device test:**
  - Game launch is about 3 s faster (time from tap to first frame).
  - Logcat shows `ADPF performance hints: available` on the Thor.
  - Overlay FPS before and after in the same scene.

## Phase 7: feature research → FEATURE_RESEARCH.md
- [x] Survey with sources: Eden (driver fetcher, ADPF, overrides), Azahar (auto-map, secondary display, save states, surface fixes), Dolphin, PPSSPP, NetherSX2, Vita3K, other Cemu Android forks (2026-09-28)
- [x] Deep dives the owner asked for: controller auto-map (Azahar PR #1769), save states (cemu PR #953, issue #2062), Eden driver fetcher (repos, API, recommendation table)
- [x] Gap table, implementation notes, prioritized backlog (below)
- [ ] Fill the remaining `?` cells of the gap table (web search was rate-limited on 2026-09-28)

## Feature backlog (from FEATURE_RESEARCH.md; each item becomes its own phase)
1. [ ] GPU driver fetcher (Eden-style): repo list, releases, download, install, GPU model plus recommendation
2. [ ] Controller auto-map with a "press A" layout prompt; auto-apply a default profile per vendor:product
3. [ ] More hotkeys: swap screens, toggle pad, pause, screenshot
4. [ ] Dual-screen polish: display picker, mirror the TV, overlay on the second screen, split ratio
5. [ ] Per-game controller profiles and overlay layouts
6. [ ] Save backup/restore per title; single-file WUA/WUP install picker
7. [ ] Overlay frametime graph, battery and temperature; sustained-performance toggle
8. [ ] Amiibo picker (`nn_nfp`)
9. [ ] Save states: experimental, fork-only; start from Cemu issue #2062 and PR #953
10. [ ] Vulkan pre-rotation (P1) and an adaptive ADPF target

## Housekeeping
- [x] Deleted the stray `%TEMP%\k.txt` left by a planning agent (2026-09-28)
- [x] B4 `vcpkg.json` duplicate hidapi (95337964)
- [x] B6 APK only packages arm64-v8a (fe594253)
- [ ] B5 dead `ENABLE_NSYSHID_LIBUSB` flag
