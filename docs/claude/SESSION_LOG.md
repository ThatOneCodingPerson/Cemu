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
