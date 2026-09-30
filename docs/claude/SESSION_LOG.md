# Session log (append-only, newest last)

Format: date · what was done · what's next · open questions. Keep each entry short; details belong in TODO.md and BUGS.md.

---

## 2026-09-28: Session 1 (planning + Phase 0)
**Context from the owner:**
- Asked for five things:
  1. A future-self prompt.
  2. Fix the crashes.
  3. Research other emulators' features for a roadmap.
  4. A persistent notes and TODO system with real check-offs, plus online fact-checking before coding.
  5. A maintained one-shot APK build script under `dist/`.
- Supplied a prior claude.ai audit of `android-port` (bugs A1–A15, I1–I3, perf notes, Thor dual-screen notes).

**Findings:**
- The owner's released 0.5.2 lives on `origin/android-port-dual`, 5 commits ahead of `android-port`. It adds `PadPresentation`, screen swap, and custom data storage with save sync.
- `upstream` (SSimco) android-port is identical to the local `android-port`, so there's nothing to pull.
- Three explorer agents re-verified the audit and added about 50 findings (BUGS.md: N#, K#, D#, B#).
- A design agent produced `PHASE2_DESIGN.md`.

**Decisions (owner):**
- Work on a new branch from `-dual`.
- A `dev` build type (`info.cemu.cemu.dev`, "Cemu Dev") that installs side by side with the release.
- Docs committed in the repo.
- The Thor is on USB; after each phase, install the build and pull logs myself.
- Must work on non-Thor devices too.
- Feature research covers all areas, with emphasis on controller auto-mapping, save states, and an Eden-style driver manager.

**Done:**
- Branch `android-stability` (upstream unset).
- `CLAUDE.md`, `docs/claude/*`.
- `dist/android/build-apk.ps1` + `.cmd`.
- Gradle `dev` build type.
- B1 (data hash path), B2 (pkg-config on a Windows host), B3 (`.gitattributes` LF for shell scripts).

**Build bring-up notes (Windows host, no Visual Studio, no admin):**
- The script bootstraps JDK 21 (Adoptium API, checksum-verified), cmdline-tools 15859902, NDK 29.0.14206865, platform 36, and SDK CMake 3.31.6.
- The repo path has a space, so the build runs from `subst W:`.
- AGP builds the `dev` type's native code as `RelWithDebInfo` (optimized). Confirmed from the CMake command line.
- vcpkg's default host triplet `x64-windows` needs Visual Studio. The fix is `VCPKG_DEFAULT_HOST_TRIPLET=x64-mingw-static` plus portable WinLibs GCC 15.3.0 (pinned sha256), appended to PATH.

**Build bring-up results:**
- The first APK was built at 2026-09-28 19:19. vcpkg took about 12 min; Cemu's native build took under 5 min.
- The vcpkg `hidapi:arm64-android` failure (pkg-config missing inside vcpkg's sanitized env) was fixed by removing the duplicate unconditional hidapi dependency (B4).
- WinLibs ships `pkgconf.exe`, which CMake's FindPkgConfig finds for Cemu's own libusb lookup.

**Commits:**
- `95337964`: build tooling and the `dev` build type.
- `3f3939f1`: EmulationSessionState moved to `common/`. ArchUnit had failed on the dual branch's `settings` → `emulation` dependency, which also exposed D7: cross-process state is always false in the main process.
- `92ecc623`: Phase 1 crash visibility (A1, A2, A3, A8, A9, N1, N2, N3, N5, N7, N14, plus the logcat mirror).

**Artifact:** `dist/android/output/Cemu-0.5.1-92ecc623-dev.apk`. `:app:testDebugUnitTest` passes. AGP 9 only has unit-test tasks for debug, so there is no `testDevUnitTest`.

**Lessons for next time:**
- Edit CRLF files with the Edit tool or the `$TEMP/crlfedit.py` helper; a backslash-n escape inside Python heredocs turns into a real newline, so use the Edit tool for any string containing escapes.
- Don't edit native sources while a build is compiling.

**Next (superseded, see below):** Phase 2 onwards.

**Later the same session (Phases 2–7):**

*Commits:*
- `f333fe7a`: Phase 2, surface lifecycle.
- `91f156d3` and `eae0b51f`: Phase 3, JNI, storage and crash fixes.
- `ea67dfae`: Phase 4, input.
- `76ac98f1`: pad present mode, `cntfrq` timer, ADPF, BotW guard.
- `fe594253`: dual-screen UX, single-screen cleanup, arm64-only APK.
- Docs: `654c7ef6`, `71c540da`.

*Research findings:*
- Azahar PR #2425 confirmed the surface design lessons: dedupe `surfaceChanged`, one VkSurface per window.
- NDK headers confirmed: `fromSurface` returns +1; ADPF core API is 33, `setThreads` 34.
- Eden `adpf.cpp` was read for design only (GPL-3.0, no code copied).
- Eden `DriverFetcherFragment` gave the repo list and recommendation table.
- Azahar PR #1769 covers auto-map; Azahar #1371/#1385 cover the secondary display.

*Status and blockers:*
- About 60 BUGS entries are fixed. Everything is `[?]` until the owner tests on a device.
- **Blocked on the owner:**
  1. USB debugging on the Thor (still MTP only).
  2. The D1/D2 design decision (SAF save sync on the main thread; options are in TODO Phase 3).
- **Tooling notes:**
  - WebSearch hit a session limit late in the session; GitHub raw/API via curl still worked.
  - The auto-mode command classifier failed transiently several times; file edits via the Edit tool were unaffected.

*Next:*
- Owner device tests: pull logs with `dist/android/pull-logs.ps1`, fix regressions first.
- D1/D2 once decided.
- SAF path encoding (Phase 3).
- Then the feature backlog, starting with the GPU driver fetcher (FEATURE_RESEARCH.md §1).

## 2026-09-28, session 2 (bugs, driver fetcher, hotkeys, auto-map)
**Device:** still none. `adb devices` was empty all session (the Thor enumerates as MTP only).

**Commits (all built as dev APKs; `testDebugUnitTest` passes; 6 tests: 3 ArchUnit + 3 driver tests):**
- **`aaab5241`, `c27e208b`, `4392ec37`, `08ddc021`: open bugs.**
  - A15: specific error codes for an invalid RPX and an unmountable title.
  - K14/K18: title list races and hidden DLC in compression.
  - K15: deleting the active account.
  - K25: `navigateBackSafely`.
  - K26: backup rules.
  - B5: dead CMake argument.
  - K29: no pad/second-screen offer after a failed launch.
  - K24 is blocked: the core swkbd has no cancel.
- **`e8efeb45`: GPU driver fetcher.**
  - Five GitHub repos. `whitebelyash/freedreno_turnip-CI` moved to `whitebelyash/AdrenoToolsDrivers`.
  - GPU probe via a separate system-loader VkInstance, plus an Adreno-based suggestion.
  - Streamed download, the shared `installDriverZip`, and a `minApi` check. KIMCHI's Qualcomm 840 package needs API 35.
- **`1c18255f`: hotkeys and menu entries.**
  - Pause (user pause and surface-loss pause combined).
  - Screenshot: the core capture, handed to Kotlin, saved as PNG via MediaStore to Pictures/Cemu.
  - Swap screens, toggle PAD, toggle overlay.
- **`8e9a5094`: controller auto-configuration.**
  - K28: a fresh install had no emulated controller at all.
  - Auto-map while controller 1 is unmapped.
  - "Press A" prompt in Map all inputs.
  - `InputMapper` moved to `common/input`.

**Research this session:**
- **GitHub releases API:** re-checked with curl. One asset from each repo was downloaded and its `meta.json` inspected.
- **GitHub docs:** rate limit and User-Agent rules.
- **Android docs:** MediaStore needs no permission for owned files on Android 10+ (the `IS_PENDING` protocol).
- **Azahar `AutoMapDialogFragment.kt`:** read for the idea only (GPL).
- **Ktor 3.4.2:** `ByteReadChannel.readAvailable` signature verified with javap on the jar.

**Lessons:**
- Again: backslash escapes inside `<<'EOF'` Python heredocs end up literally in files (`\"`). Use the Edit tool for such strings.
- Swallowing `CancellationException` in `catch (Exception)` makes cancelled coroutines overwrite newer state. Rethrow it (done in the driver code).
- Compose `Dialog` windows don't pass gamepad keys to the activity; use `Popup` for key capture.

**Next:** see "Next session" in TODO.md. Every session-2 item is `[?]` and needs the owner's device test first.

## 2026-09-28/29, session 3 (bugs, owner requests, backlog features)
**Owner:** "so far it's doing pretty great" from their own testing, and asked to keep going. Mid-session they asked for a keys file import. Still no adb device (MTP only).

**Commits** (every one built as a dev APK; 17 JVM tests pass: 3 ArchUnit + driver 3 + SAF 7 + keys 4):
- **Bugs:**
  - 9e51a7b8 SAF paths (K30).
  - 76d76442 K16.
  - ef6fc725 K17/K27.
  - a6d42c74 P9 compile thread priority.
  - 6d877933 K13 (ForegroundTaskService plus install recovery).
  - adf22428 TV/GamePad split and K31.
- **Features:**
  - 61d1481e amiibo.
  - f03ccff9 battery/thermal overlay.
  - 30c39093 save export/import.
  - 49c662ca keys file import (owner request).
  - a23cad60 otp/seeprom import.
  - f1faa289 sustained performance mode.
  - 89469694 controller profiles (named and per-game).
  - 78299e2e frame time graph.

**Research this session:**
- AOSP `DocumentsContract` (GitHub mirror, main): `getDocumentId` decodes, `buildDocumentUriUsingTree` encodes.
- setpriority(2) man page (the nice value is per-thread on Linux/NPTL), and AOSP libutils `androidSetThreadPriority` uses `setpriority(PRIO_PROCESS, tid)`.
- Foreground services: developer.android.com FGS types and the Android 15 behavior changes (dataSync: 6 h/24 h, `onTimeout`, needs `FOREGROUND_SERVICE_DATA_SYNC`).
- `android.jar` (API 36) via javap: `PowerManager` thermal constants and listener, `BatteryManager` extras, sustained performance mode (not deprecated).
- Core code: `nfc::TouchTagFromFile` (amiibo keys built in), `KeyCache`, `iosu_crypto` (otp 1024 / seeprom 512 bytes), `InputManager` profiles, and `GameProfile` `[Controller]`.

**Lessons:**
- `<<'EOF'` heredocs break in this harness when the text contains `$` or unbalanced quotes. Write edit scripts with the Write tool into the scratchpad and run them with python.
- The auto-mode safety check failed transiently several times (WebFetch/Bash/PowerShell). Edit/Write kept working; retry later.
- An anonymous C++ struct can't hold a `static constexpr` member (build error); use a namespace-scope constant.

**Next:** see "Current focus" in TODO.md. D1/D2/D7 still need the owner's decision.

## 2026-09-29, session 4 (save sync, shader caches, graphics options, per-game/per-model settings)
**Owner:**
- Chose option (a) for D1/D2 at the start.
- Asked for per-game shader compilation.
- Then: "proceed with what's contained in the claude.md file… not to worry about trying to connect through adb. I will be the one managing the testing of the actual apk". Builds are now handed over without `-Install`, and memory, CLAUDE.md and TODO say so.
- **Answers to the scope questions:**
  - Single-file WUA install: dropped.
  - Save states: a feasibility write-up only, but at high priority.
  - Second screen: only the display picker (with a Settings entry), at the lowest priority.
  - Graphics/performance options ("vulkan/openGL") became a new priority item.

**Commits** (every phase built as a dev APK; 31 JVM tests pass: ArchUnit 3, driver 3, SAF 7, keys 4, overlay layouts 7, controller model profiles 7):
- **Earlier in the session:**
  - 08a9c4da save sync off the main thread (D1, D2, D7).
  - 2db34da4/58e76ba0/0cdc1d12 notes.
- 706e1979 shader cache per game: compile-only launch, import/merge, export, flush on quit.
- dbf60105 ADPF target follows the title's swap interval.
- 3cfdb190 save states feasibility (FEATURE_RESEARCH §10).
- bc8d00b4 graphics options: Vulkan pre-rotation finished (P1), gamma UI, anisotropic filtering override, AMD FSR 1 EASU upscale filter.
- 463bc6ab "Graphic packs…" for one game.
- 7c177945 separate input overlay layout per game (#5).
- a068f91b controller layouts per model (vendor:product) (#2).
- 2aac0ca9 display picker (#4).
- 2fb686f5 build-apk.ps1 exits 0 on success (it returned `git diff --quiet`'s 1 for a dirty tree).
- **Notes:** 539cbe5d, 9a0df255, e8e248b2 and this entry.

**Artifact:** `dist/android/output/Cemu-0.5.1-2aac0ca9-dev.apk` (= `Cemu-latest-dev.apk`). Device test lists are at each `[?]` item in TODO.md.

**Research this session (all 2026-09-29):**
- **Save states:** cemu-project/Cemu PR #953 and issue #2062 (GitHub API). The Matt-Wood-23/Cemu `savestates` branch: its compare, docs and `src/Cafe/SaveState`, MPL-2.0, verified with MH3U on x64. Drift against our core is small (FEATURE_RESEARCH §10).
- **Emulator settings:** Eden `IntSetting.kt`/`BooleanSetting.kt`/`arrays.xml` (GPL, ideas only) and Dolphin's settings enums, for the graphics options.
- **FSR 1:** AMD FidelityFX-FSR `ffx_fsr1.h` v1.20210629 and `ffx_a.h` (MIT). Approximation constants verified: `0x7ef07ebb` and `0x5f347d74`.
- **AOSP `libvulkan/swapchain.cpp`** (googlesource main; the GitHub aosp-mirror is gone):
  - `currentExtent` is in the window's orientation.
  - Present returns `VK_SUBOPTIMAL_KHR` only for a transform mismatch.

**Lessons:**
- In PowerShell, `git commit -F -` with a here-string doesn't read stdin. Write the message to a scratchpad file and pass the path.
- The bootstrapped JDK is one level deeper (`CemuAndroidBuild\jdk-21\jdk-21.0.x+y`). The scratchpad `run-tests.ps1` finds it; for a new session: `$env:JAVA_HOME` = that folder, then `gradlew :app:testDebugUnitTest` from the subst drive.
- Changing `CemuConfig.h` or `EmulatedController.h` rebuilt ~150 objects in about 2 minutes, not the whole tree.
- The auto-mode safety check again failed transiently for Bash. Edits and reads kept working; retrying later worked.

**Open questions for the owner:**
- **Save states:** port the Matt-Wood-23 branch as an experimental feature (yes/no), and which 2–3 games to test with.
- **Pre-rotation:** does the Thor report 0° (landscape panels)? The log line `pre-rotation N degrees` tells.

**Next:**
- Regressions from the owner's device tests come first.
- Save-state port step A if approved.
- Leftovers:
  - deadzones in controller model profiles;
  - switching controller 1 when a better pad connects while built-in controls are mapped;
  - FXAA/RCAS;
  - the gap table's Azahar/PPSSPP/NetherSX2/Vita3K cells.

## 2026-09-29/30, session 4 addendum (README, the regular Cemu APK, repository cleanup)
**Owner requests:**
1. A community-facing README (4b190378).
2. A script that builds the regular "Cemu" app (`info.cemu.cemu`), so frontends such as Cocoon recognize it. The owner chose to build `android-stability`, signed with the debug key.
3. "fix everything where I have all my proper code", after branches got mixed up.

**What had happened:**
- In GitHub Desktop, the owner created `NewCemu` from `android-port` and committed there.
- Because `android-port`'s `.gitignore` doesn't exclude `dist/android/output`, that commit held only the 38 dev APKs (~1.3 GB), build logs and `.build-drive`. It had no code, and the README in it was still the old one.
- `NewCemu` and `android-stability` were both pushed to GitHub.
- A stash `!!GitHub_Desktop<NewCemu>` held an accidental revert of upstream's figure fix 8759323e (stash commit fb4b72be, dropped).
- My git worktree (`Cemu-android-stability`) added to the confusion. It also broke the build: submodule `.git` files use relative paths that climb above the `subst` drive root.

**Cleanup (2026-09-30):**
- Removed the worktree and the extra `S:` mapping.
- The main folder is back on `android-stability`.
- The old dev APKs were restored as ignored files in `dist/android/output`.
- The stash was dropped, and the local `NewCemu` was deleted.
- Left for the owner: whether to delete `origin/NewCemu` and push `android-stability`.

**Added:** `dist/android/build-cemu-apk.ps1`/`.cmd`. It builds the `release` type (`info.cemu.cemu`, label "Cemu"), checks the app id with aapt2, prints the signer with apksigner, and takes `-Keystore/-KeyAlias/-Version/-Install`. The release key's password is asked for, never stored.

**Checked:**
- Cocoon (cocoon-shell.com) is an Android frontend for dual-screen handhelds with a Wii U platform (read 2026-09-30).
- Frontends start games through `info.cemu.cemu/.emulation.EmulationActivity` with an ACTION_VIEW content URI. This fork keeps that.
