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
- **Testing (owner, 2026-09-29):** the owner installs and tests the APKs themselves. No adb from Claude: build without `-Install`, hand over `dist/android/output/Cemu-latest-dev.apk` plus the device test list, and mark items `[?]`.
- **Session 4 (2026-09-29), working order** (the owner reprioritized: save states and graphics options first, the second screen last):
  1. [x] Commit the pending work: shader compile per game (706e1979), adaptive ADPF target (dbf60105).
  2. [x] Save states: feasibility write-up (#9, FEATURE_RESEARCH §10) (2026-09-29). **Owner decision needed:** port the Matt-Wood-23 branch as an experimental feature (yes/no, which games).
  3. [ ] Graphics options (new, see "Graphics options" below): research, pre-rotation (P1), gamma, anisotropic filtering, a sharpening upscale filter, graphic packs per game.
  4. [ ] Per-game overlay layouts (#5).
  5. [ ] Per vendor:product controller profiles (#2).
  6. [ ] Display picker, with a Settings entry (#4), at the lowest priority.
- **Owner decisions (2026-09-29):**
  - #6 single-file WUA/WUP install: dropped. Cemu runs .wua/.wud/.wux and WUP folders from a games folder, and adding that folder as a game path covers it.
  - #4: only the display picker. "Mirror the main screen" and "input overlay on the second screen" are not planned. The performance overlay already draws on both screens.
  - OpenGL isn't available on Android: the build uses `-DENABLE_OPENGL=OFF`, and Cemu's GL backend needs desktop GL 4.5. Graphics options are Vulkan only.
- Latest APK: `dist/android/output/Cemu-latest-dev.apk`. Phases 1–6 and sessions 2–4 are built; everything is `[?]` until device-tested.
- **Owner feedback (session 3 start, 2026-09-28):** "so far it's doing pretty great". Testing on their own devices went well, and no specific regressions were reported. Items stay `[?]` until confirmed individually. The owner asked to keep working through the list.
- (D1/D2 decided 2026-09-29: option (a); built in 08a9c4da, awaiting device test.)
- **Session 2 (2026-09-28):**
  - [?] Open bugs: K14, K15, K18, K25, K26 (c27e208b), A15 (aaab5241, 08ddc021), B5 (4392ec37).
  - [?] Driver fetcher (e8efeb45), hotkeys (1c18255f), auto-map (8e9a5094).
- **Session 3 (2026-09-28/29), bugs** (device test steps below):
  - [?] SAF path encoding (9e51a7b8). [?] K16 (76d76442), K17 + K27 (ef6fc725).
     - **Device test:**
       - Enable motion, trigger an activity recreation (e.g. multi-window/resize if possible): motion still works.
       - Overlay edit > resize: follows the finger smoothly and still resizes at the screen edge.
       - A layout saved on one device/screen size shows every button on screen on another.
  - [?] P9 pipeline compile threads at background priority (a6d42c74). [?] K13 install/compression kept alive by a foreground service + interrupted-install recovery (6d877933).
     - **Device test:**
       - Install a title from the title manager, press Home during the copy:
         - A "Installing title" notification with progress shows, if notifications are allowed for Cemu Dev; otherwise it's in the task manager.
         - The install finishes in the background.
       - Force-stop Cemu Dev mid-install, reopen:
         - No broken title in the list.
         - Updating an installed title and killing it midway brings the old version back.
       - The same for WUA compression (notification plus completion).
       - In a shader-heavy area, compare stutter and FPS with the previous APK (P9).
- **Session 3 features** (device test steps at each item in "Owner requests" and "Feature backlog"):
  - [?] Keys file import (49c662ca) and otp.bin/seeprom.bin import (a23cad60). These are owner requests.
  - [?] TV/GamePad split + K31 (adf22428)
  - [?] Amiibo (61d1481e)
  - [?] Battery/thermal (f03ccff9), frame time graph (78299e2e) and sustained performance (f1faa289) in the overlay/settings
  - [?] Save export/import (30c39093)
  - [?] Named and per-game controller profiles (89469694)
- **Next:** the session 4 working order above. Regressions the owner reports from device tests come first.

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
- [?] 0.6 Install on the Thor next to 0.5.2 and boot to the game list. The owner does this (they install and test the APKs themselves since 2026-09-29).
- [?] 0.7 Confirm B1 on the owner's 0.5.2 install: does `Android/data/info.cemu.cemu/files/data/gameProfiles` exist (file manager or adb)? Owner check.
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
- [x] A15 leftovers: error codes (aaab5241) and the `initialized` flag semantics (08ddc021) (2026-09-28)
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
- [?] SAF path percent-decoding with a round-trip design (names containing `:`, spaces, unicode) (2026-09-28, 9e51a7b8, K30)
  - **Device test:**
    - A games folder containing a game folder named e.g. `Zelda: BotW #1` (extracted code/content/meta) and a `.wua` with a space and non-Latin characters in its name. Both show in the list and launch.
    - A game that was already listed before updating still launches, and so do its old home screen shortcut and title manager deletion (old paths are still read).
- [?] Manifest `configChanges` (A12); missing launch path handled (A15, partial); relaunch while running attaches to the running title (K2)
- [?] Pipeline cache: atomic write, flush on quit (2 s max), save on pause (A13)
- [?] Data files: hash written last, cross-process file lock (A14)
- [?] **D1/D2/D7: the owner chose option (a)** (2026-09-29, "let's continue working on that" in reply to the recommendation). Built in 08a9c4da.
  - **Device test** (only with a custom data folder set in Settings > Data storage):
    - Cold start with many saves: the game list shows "Syncing saves…" briefly, no ANR, then it's usable.
    - Launch a game from the list: no second sync, and it starts quickly.
    - Play, save in-game, Quit: "Saving…" shows briefly, and the custom folder has the new save.
    - Home-screen shortcut with the app closed: "Syncing saves…" first, then the game.
    - Press Home mid-game, then open Settings > Data storage in the game list: it knows a game runs.
    - Force-stop mid-game, restart: the save is still there (the dirty mirror is exported, not overwritten). SAF save sync runs synchronously on the main thread at startup (every process, including each game launch) and on quit/background. The options were:
  - (a) keep the sync but run it on a background thread, and gate game launch and the save UI on its completion (a "Syncing saves…" screen), or
  - (b) only sync in the main process and before launch, with a progress dialog.
  - Don't just move it off the main thread: a game could read or write saves mid-import and the export could overwrite newer data.
- [?] D3 observer map thread-safe
- [?] Kotlin crash fixes K1, K4, K5, K6, K7, K8, K9, K10, K11
- [?] Native races N10, N11, N12
- [?] D7 cross-process "is emulation running" state (08a9c4da, `emulation-session.lock`)
- [?] K13 title install foreground service + recovery (6d877933); K14 WUA compression uses the filtered list, K15 account deletion (c27e208b)
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
- [?] K16 motion toggle after activity recreation (76d76442)
- **Device test:**
  1. Touch the game screen with one finger, then press A with another: A registers.
  2. Hold a button, swipe down the notification shade: the button releases.
  3. Hold a controller button, open the in-game menu, close it: the button isn't stuck.
  4. Phone gyro game: flip the device 180°; tilt direction stays correct.
  5. Touch the GamePad screen area while the overlay is visible: touch still works.

## Phase 5: dual screen and multi-device
- [x] Research: Azahar's secondary-display PRs #617, #1371 and #1385; the owner's own Azahar PR #1341; melonDS; Thor specs (FEATURE_RESEARCH.md §4)
- [?] How the Thor exposes the bottom panel (`adb shell dumpsys display`): an owner check, if they want to run it. Not needed for the current code.
- [?] Pad swapchain presents non-blocking (MAILBOX/IMMEDIATE) and never drives host vsync (P8), 76ac98f1
- [?] Per-display pad DPI: `setPadDPI` from `PadPresentation`, reset when the pad returns to the main display (P10), fe594253
- [?] Swap screens persisted; a one-time "Second screen detected: show GamePad there" snackbar, fe594253
- [?] Single-screen devices: "External PAD screen" and "Rotate external screen left" are hidden without a second display, fe594253
- [?] Split ratio setting for the inline pad (adf22428)
- [ ] Display picker when there are 2+ candidates, with a Settings entry (backlog #4, lowest priority in session 4)
- **Device test:**
  - Thor: 120 Hz main screen not throttled when the pad is on the bottom screen (overlay FPS).
  - Pad touch maps correctly; the pad notifications are sized right on the bottom screen.
  - Swap screens is remembered after quitting.
  - Thor, first launch after install: the "Second screen detected" snackbar appears once.
  - Phone: no dual-screen options in the menu.

## Phase 6: performance
- [?] PPCTimer via `cntfrq_el0` (no 3 s wait at launch); `_addcarry_u64` fix (P3), 76ac98f1
- [?] ADPF performance hint session for the PPC and GPU threads, frame reports vs 16.67 ms (P2), 76ac98f1. Sustained-performance option: f1faa289.
- [?] Adaptive ADPF target: target = 16.67 ms × the title's GX2SetSwapInterval (FEATURE_RESEARCH §9) (2026-09-29, dbf60105)
  - **Device test:** a 30 fps game (e.g. BotW, Xenoblade X) logs `ADPF: target frame time 33.3 ms` once in log.txt; a 60 fps game logs nothing extra. FPS the same as the previous APK.
- [~] Vulkan pre-rotation (P1), session 4. Source: developer.android.com/games/optimize/vulkan-prerotation (fetched 2026-09-29).
  - The swapchain's `preTransform` takes `currentTransform`, and the image is in the display's natural orientation.
  - The output quad rotates through specialization constant 0 in the output vertex shader, and the viewport is mapped (`SwapchainInfoVk::ToImageViewport`).
  - ImGui draw data is mapped on the CPU in `ImguiEnd`.
  - `VK_SUBOPTIMAL_KHR` means look at the transform again, about once a second.
  - Off by default: Settings > Graphics > "Pre-rotate the picture (experimental)", Android only.
- [?] BotW fork path guarded (P6), 76ac98f1. P7 is wontfix (per-core CPU only, not exposed on Android)
- [?] Pipeline compile thread priority (P9, a6d42c74)
- **Device test:**
  - Game launch is about 3 s faster (time from tap to first frame).
  - Logcat shows `ADPF performance hints: available` on the Thor.
  - Overlay FPS before and after in the same scene.

## Phase 7: feature research → FEATURE_RESEARCH.md
- [x] Survey with sources: Eden (driver fetcher, ADPF, overrides), Azahar (auto-map, secondary display, save states, surface fixes), Dolphin, PPSSPP, NetherSX2, Vita3K, other Cemu Android forks (2026-09-28)
- [x] Deep dives the owner asked for: controller auto-map (Azahar PR #1769), save states (cemu PR #953, issue #2062), Eden driver fetcher (repos, API, recommendation table)
- [x] Gap table, implementation notes, prioritized backlog (below)
- [ ] Fill the remaining `?` cells of the gap table (web search was rate-limited on 2026-09-28)

## Owner requests
- [?] **Shader compilation per game** (requested 2026-09-29; 706e1979): "compile shaders for game X" so the full shader list is ready ahead of play. See FEATURE_RESEARCH.md §8 for how the caches work. A game's full list can't be read out of its files; it grows while playing, or comes from other players' caches.
  - Game list long-press > "Shader cache…" dialog:
    - Shader and pipeline counts plus the compiled (driver) cache size.
    - **Compile now:** a compile-only launch. The GPU thread compiles the caches with the usual progress screen, then stops before the game starts (`Latte_SetPrecompileOnly`). A "Shaders compiled" dialog follows. No save sync, but the session lock is held.
    - **Import cache files:** .bin files or a .zip. They are merged into the game's list: only new entries are added, and the title check is the one the core uses. Old title-independent files are accepted only when the file name contains the title id. Metal caches are skipped.
    - **Export shader list:** a zip of both transferable files.
  - Also fixed: shaders and pipelines found since the last async write were lost at quit (`_exit`). `quitProcess` now waits up to 2 s for `FileCache` async writes.
  - **Device test:**
    - Long-press a game you have played > "Shader cache…": the dialog shows counts above 0.
    - **Compile now:**
      - The game screen shows "Compiling shaders only, the game will not start" and the usual shader loading progress.
      - Then "Shaders compiled" with counts; Done returns to the list.
      - The game's audio/intro never starts.
      - Launch the game normally afterwards: the loading screen is shorter.
    - **Export shader list:** a zip with `<titleid>_shaders.bin` and `<titleid>_vkpipeline.bin`.
    - **Import:**
      - Import that zip on the other device (or a cache from the internet named after the title id): "Added N shaders and M pipelines".
      - Import it again: "Added 0 shaders and 0 pipelines".
      - Import a cache of a different game: "1 files belong to another game…".
    - While a game runs (from a shortcut), Compile/Import say "Quit the running game first".
    - Play, find new areas, quit from the menu, and reopen the dialog: the counts grew. This checks the quit flush.
- [?] **Keys file import** (requested 2026-09-29; 49c662ca): Settings > "Import keys file". Picks a `keys.txt` from anywhere (Android/data is locked on newer Android) and merges its keys into Cemu's `keys.txt` (new keys only, below an "imported from" comment). The keys are reloaded and the titles rescanned, so no restart is needed.
  - **Device test:**
    - With a WUD/WUX in the games folder that doesn't show (missing key), import a keys.txt that has its key from Downloads: "Added N keys" and the game appears in the list without a restart. It launches.
    - Import the same file again: "All keys of this file were already added".
    - Pick a random text file: "No keys found".
- [?] **otp.bin / seeprom.bin import** (same problem as keys.txt; a23cad60): Account settings > "Import otp.bin / seeprom.bin" (multi-select, recognized by size 1024/512). The online status refreshes without a restart.
  - **Device test:**
    - Select both files from Downloads: "otp.bin imported / seeprom.bin imported".
    - The status line updates. The Network service choice unlocks once the certificates in the MLC are present too.
    - A wrong file shows the size hint.

## Graphics options (owner request, 2026-09-29)
The owner asked for "graphical settings that can help improvements for games either more visually or for more performance". This is Vulkan only (see "Owner decisions" above). Research is in FEATURE_RESEARCH.md §11.
- [ ] Research: what Eden, Dolphin and Azahar offer; which of Cemu's own options the Android UI doesn't expose; sources for a sharpening filter
- [~] Vulkan pre-rotation (P1): finish the renderer part; a Graphics toggle
- [ ] Gamma: override the game's gamma, display gamma / sRGB (core options without an Android UI)
- [ ] Anisotropic filtering override
- [ ] Sharpening upscale filter
- [ ] "Graphic packs…" per game (resolution/FPS packs of that game)

## Feature backlog (from FEATURE_RESEARCH.md; each item becomes its own phase)
1. [?] GPU driver fetcher (Eden-style): repo list, releases, download, install, GPU model plus recommendation (2026-09-28, e8efeb45)
   - **Device test:** Settings > Graphics > Custom drivers > download icon.
     - On the Thor: GPU card shows "Adreno (TM) 740", a system driver version, and "Suggested: Mr. Purple Turnip (T23)".
     - Switch between repos. Download the T23 zip: progress bar, then "Driver installed successfully" + "Use it".
     - Back: the driver is listed and selected. Launch a game (log.txt shows the custom driver being loaded).
     - Download KIMCHI "Qualcomm Driver v840" on Android 13: "needs a newer Android version".
     - Airplane mode: error card + Retry. Leaving the screen mid-download leaves no `driver-download.zip` in the cache dir.
     - A non-Qualcomm phone has no Custom drivers entry at all (unchanged).
2. [~] Controller auto-map with a "press A" layout prompt; auto-apply a default profile per vendor:product
   - [?] "Press A" prompt in Map all inputs (A = button names, B = Wii U positions) + first-run auto-configure + auto-map while controller 1 is unmapped, with a setting (2026-09-28, 8e9a5094)
   - [ ] Per vendor:product saved profiles (auto-apply when a *different* known controller connects). Joy-Con quirks (Azahar special-cases their partial A/B swap).
     - **Design note (2026-09-29):** a saved profile binds its mappings to one unit's `InputDevice.descriptor` (the native controller `uuid`). Another unit of the same model has a different descriptor, so applying "this model's profile" needs a native re-target step: replace the Android controller's uuid/name, keep the mappings. `NativeInput.cpp` `GetOrCreateController` / `getMotionEnabledControllerDescriptors` show the pattern.
   - **Device test:**
     - Fresh install of Cemu Dev on the Thor:
       - Game list shows the toast `"<Thor controller>" was mapped to controller 1…`.
       - Settings > Input > Controller 1 is GamePad with mappings. A game responds to the built-in controls, and the input overlay works too.
     - Phone without a controller, fresh install: controller 1 is GamePad, overlay works, no toast.
     - Connect a Bluetooth pad while controller 1 has no mappings: toast plus mappings. With mappings already there: nothing happens.
     - Controller 1 > Map all inputs > pick the controller > the prompt appears:
       - The Xbox pad's "A" (bottom) keeps names.
       - Its right button ("B") swaps (Wii U A = right button).
       - "Use button names" and Cancel buttons work.
     - Toggle "Map controllers automatically" off: no auto-mapping.
3. [?] More hotkeys: swap screens, toggle pad, pause, screenshot, toggle input overlay (2026-09-28, 1c18255f)
   - **Device test:**
     - Map each new action in Settings > Input > Hotkeys (e.g. Select+L, Select+R …) and use it in-game.
     - Pause from the menu and by hotkey:
       - The "Paused" badge shows, game and audio stop.
       - Home, then return: the game is still paused. Resume: it continues.
     - Screenshot (menu and hotkey):
       - "Screenshot saved to Pictures/Cemu" appears, and the image is in the gallery.
       - While paused it says "Resume the game to take a screenshot".
       - Repeated presses don't crash.
     - Swap screens / toggle PAD / toggle overlay hotkeys behave like the menu checkboxes (Thor: with the PAD on the bottom screen too).
   - **Not done:** fast-forward/speed toggle (the core has no speed-up path) and default hotkey combos (part of auto-map, #2).
4. [~] Dual-screen polish: display picker, split ratio. (Mirror and overlay on the second screen: not planned, owner 2026-09-29.)
   - [?] Split ratio for single-screen devices: Settings > General > "TV screen size next to the GamePad" 50–80 %, plus the fix for GamePad position Left/Above having had no effect (K31) (2026-09-28, adf22428)
     - **Device test:** phone, Show PAD in the game menu:
       - The slider changes the TV/GamePad split.
       - All four GamePad positions put the pad on that side.
       - Touch on both surfaces still hits the right spots.
   - [ ] Display picker + per-display memory, with a Settings entry (only matters with 2+ candidate displays, e.g. Thor + HDMI). Lowest priority.
   - Not planned (owner, 2026-09-29): mirror the main screen, input overlay on the second screen. The performance overlay already draws on both screens (`LatteOverlay_render(isPadView)`).
5. [~] Per-game controller profiles and overlay layouts
   - [?] Named controller profiles (Controller N > Profiles: save/load/delete) + "Controller N profile" in Edit game profile, using the core's `[Controller]` game profile support (2026-09-29, 89469694)
     - **Device test:**
       - Save profile "A", change mappings, save "B".
       - Load "A": the old mappings (and type) are back. Mapping a button right after loading works (the cached controller is reloaded).
       - Pick "B" as Controller 1 profile in a game's profile: that game uses B, other games use the normal configuration.
   - [~] Per-game overlay layouts (session 4)
6. [?] Save backup/restore per title. (Single-file WUA/WUP install: dropped by the owner 2026-09-29; a games folder already runs these files.)
   - [?] Title manager > save entry menu > Export save (zip) / Import save (confirm, staged, rollback; marks the save mirror dirty with a custom root) (2026-09-29, 30c39093)
     - **Device test:**
       - Export a save and check that the zip holds `user/` and `meta/`.
       - Play, then import the export: the old progress is back.
       - Import a zip of the save folder itself (one level deeper): works.
       - Import a random zip: "isn't a save backup".
       - With a custom data root: restart after importing, and the imported save is still there (not overwritten by the sync).
7. [?] Overlay frametime graph, battery and temperature; sustained-performance toggle
   - [?] Battery (level, charging "+", battery temperature) and thermal status lines in the overlay; Settings > Overlay toggles (2026-09-28, f03ccff9)
     - **Device test:** overlay position set, both toggles on:
       - The lines show and update while charging/unplugging.
       - The thermal status changes after a long heavy session.
       - The overlay with only these two lines enabled still shows.
   - [?] Sustained performance mode toggle in Settings > General, only shown if the device supports it (2026-09-29, f1faa289)
     - **Device test:** check whether the Thor shows the toggle. If it does, enable it, play 20+ min and compare the FPS stability with it off.
   - [?] Frame time graph (last 120 frames, avg/max, 0–50 ms scale) in the overlay; Settings > Overlay toggle (2026-09-29, 78299e2e)
     - **Device test:** enable it with the overlay position set. The graph shows, and a shader-compile stutter shows as a spike.
8. [?] Amiibo picker (`nn_nfp`) (2026-09-28, 61d1481e): game menu "Scan amiibo" + hotkey, files copied to `<user data>/amiibo` (games write back to them)
   - **Device test:** in a game that asks for an amiibo (e.g. BotW rune, Smash, Splatoon, MK8):
     - Menu > Scan amiibo > Import file > pick a .bin: "Scanned <name>" and the game reacts.
     - Scanning again from the list works; importing the same file again doesn't create a duplicate.
     - Delete works. A non-amiibo file gives "Not a valid amiibo or NFC file".
9. [!] Save states: experimental, fork-only. Feasibility done (FEATURE_RESEARCH §10, 2026-09-29): feasible by porting github.com/Matt-Wood-23/Cemu/tree/savestates (MPL-2.0; MH3U verified on x64). Plan: A port (1 session), B Android UI (1 session), C device iteration (1–3 sessions). Blocked on the owner's go-ahead and choice of test games.
10. [~] Vulkan pre-rotation (P1) and an adaptive ADPF target (session 4; see Phase 6 and "Graphics options"). ADPF target: dbf60105.

## Housekeeping
- [x] Deleted the stray `%TEMP%\k.txt` left by a planning agent (2026-09-28)
- [x] B4 `vcpkg.json` duplicate hidapi (95337964)
- [x] B6 APK only packages arm64-v8a (fe594253)
- [x] B5 dead `ENABLE_NSYSHID_LIBUSB` flag (2026-09-28, 4392ec37)
