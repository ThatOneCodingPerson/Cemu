# Feature research: what other Android emulators offer, and how to bring it here

Every claim here has a source and an access date. Re-verify anything older than about 3 months before building on it; the emulation scene moves fast.

**Owner priorities (2026-09-28):**
- All areas are in scope.
- Emphasis on:
  1. Controller-type detection with automatic keybinds.
  2. Save states.
  3. An Eden-style GPU driver manager (download Turnip and others, plus local install) and more graphics options.
- Dual screen for the AYN Thor and other devices.

Status: **first pass done** (2026-09-28). Every section is filled; the `?` cells in the gap table still need checking. The backlog at the bottom feeds TODO.md.

---

## 1. GPU driver manager (Eden-style)
**What Eden does** (source: `DriverFetcherFragment.kt` in github.com/eden-emulator/mirror, master, read 2026-09-28):
- **Repos queried.** Lists releases from five GitHub repos with `GET https://api.github.com/repos/<owner>/<repo>/releases`:

  | Eden label | Repo | Release sort |
  |---|---|---|
  | Mr. Purple Turnip | `MrPurple666/purple-turnip` | default |
  | GameHub Adreno 8xx | `crueter/GameHub-8Elite-Drivers` | default |
  | KIMCHI Turnip | `K11MCH1/AdrenoToolsDrivers` | publish time; uses the tag name |
  | Weab-Chan Freedreno | `Weab-chan/freedreno_turnip-CI` | default |
  | Whitebelyash Turnip | `whitebelyash/freedreno_turnip-CI` | publish time |
- **Recommendation badge by Adreno model.** It parses `Adreno (TM) NNN` from the Vulkan device name:

  | Adreno | Recommendation |
  |---|---|
  | 600–639 | "Mr. Purple EOL-24.3.4" |
  | 640–699 | "Mr. Purple T19" |
  | 700–710 | "KIMCHI 25.2.0_r5" |
  | 711–799 | "Mr. Purple T23" |
  | 800–899 | "GameHub Adreno 8xx" |
  | Axx (e.g. AYANEO Pocket S2) | "KIMCHI Latest" |
  | Anything else | "Unsupported" |
- **Driver management.** A separate `DriverManagerFragment` covers install from a local zip, the list, delete and select. The GPU selector shows the active driver.
- **Per-game override.** Eden has per-game settings (long-press a game). It also pulls curated per-game setting overrides from github.com/eden-emulator/eden-overrides.

**What we already have:**
- adrenotools custom-driver loading (`src/Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.cpp`), with the same `meta.json` + `libraryName` zip format that Eden, Skyline and Yuzu drivers use.
- A zip install / select / delete UI (`settings/customdrivers/`).
- A per-game driver override (`games/profile`, `GameProfile` `[AndroidDriver]`).

**What's missing:**
- The in-app fetcher (repo list, release list, download, verify, install).
- A GPU model readout and recommendation.
- A per-game "curated defaults" source.

**Implementation notes:**
- Reuse `CustomDriversViewModel` install code. It needs the K10 ZipUtils zip-slip fix first.
- Use the ktor client already in the app (`graphicpacks/GraphicPacksDownloader.kt` uses ktor-okhttp) and stream downloads to a file (K11).
- For the GPU name, add a small JNI call that runs `vkGetPhysicalDeviceProperties` via the existing loader. `VulkanRenderer` already enumerates devices. The adreno model could also come from `ro.hardware.egl` / the GLES renderer string.
- **Hazard:** GitHub API rate limit is 60 requests/hour unauthenticated. Cache responses and show a friendly error on 403.

**Re-check before implementing (2026-09-28, GitHub API + downloaded zips):**
- **Repo moved:** `whitebelyash/freedreno_turnip-CI` now answers 301 and is `whitebelyash/AdrenoToolsDrivers` (releases `tu_vNN` "Mainline Turnip", `stu_vN` "Stable Turnip"). The other four names still resolve.
- **Formats:** one asset from each repo was downloaded (purple T30, GameHub 842.8, KIMCHI Turnip R8 + Qualcomm 840, Weab-chan Oct-11-2025, whitebelyash V32). All are zips with `meta.json` at the root and every `DriverMetadata` field present with no extra keys, so the existing installer accepts them.
- **`minApi` varies (27–35).** KIMCHI's `Qualcomm_840_adpkg.zip` needs API 35, so the Thor (API 33) can't use it. The installer now reports this separately.
- **Order:** the API's default order isn't by date (K11MCH1 lists v819.2 above newer rc builds), so sort by `published_at`. Some releases have no assets (purple `vTurnip_23.0.0_R2`).
- **GitHub docs** (docs.github.com, rate-limits and getting-started pages): 60 requests/h unauthenticated. When exceeded, the response is 403 or 429 with `x-ratelimit-remaining: 0` and `x-ratelimit-reset` in epoch seconds. A `User-Agent` is required; send `Accept: application/vnd.github+json` and `X-GitHub-Api-Version: 2022-11-28`.

**Implemented** e8efeb45 (`settings/customdrivers/Driver{Releases,DownloadViewModel,DownloadScreen,Installer}.kt`, JNI `getSystemGpuInfo`).
- **GPU probe:** a separate VkInstance from the system `libvulkan.so`, never `dlclose`d.
- **Suggestions:** keyed by Adreno model. The table follows Eden's picks (facts only, no Eden code).
- **Caching:** results cached per process for 15 min.
- **Not done yet:**
  - A per-game "curated defaults" source.
  - ETag conditional requests (GitHub's docs page doesn't say whether a 304 counts against the limit).
  - Showing which listed release is already installed (the zip must be downloaded to read its `meta.json`).

## 2. Controller auto-mapping ("sense controller type, assume keybinds")
**What Azahar does** (azahar-emu/azahar PR #1769, merged 2026-02-27 in 2125.0; read 2026-09-28):
- **Trigger.** An "Auto-Map Controller" button; long-press clears all bindings.
- **Layout detection.** It prompts "press A" once to detect the layout: `KEYCODE_BUTTON_B` on the east button means Xbox layout, `KEYCODE_BUTTON_A` on the east button means Nintendo layout.
- **Mapping.** Standard Android gamepad keycodes go to console buttons: ABXY, L1/R1/L2/R2, stick clicks and start/select.
- **D-pad.** It detects whether the d-pad is axis-based (`motionRanges` has HAT_X/HAT_Y) or key-based (`hasKeys` DPAD_*).
- **Quirks.** Joy-Cons get special handling (scan-code d-pad, partial button swap).

**Other references:**
- Azahar issue #1920 proposes the same "press A" approach on desktop.
- RetroArch matches autoconfig profiles by device name + vendor id + product id (docs.libretro.com/guides/controller-autoconfiguration, read 2026-09-28).
- Eden's `InputHandler.kt` builds a device GUID from `productId` + `vendorId` (eden-emulator/mirror).

**What we already have:**
- Per-controller mapping UI with "map all" (`settings/input/`).
- Controller-type detection helpers (`common/android/inputevent`).
- Native mapping storage (`NativeInput.setControllerMapping`, `controllerProfiles/`).

**What's missing:**
- One-tap auto-map.
- Auto-applying a default profile when a new controller connects.
- A per-controller-type default profile keyed by vendor/product.

**Implementation notes:**
- Wii U GamePad button positions follow Nintendo's convention (A is east).
- Offer the "press A" detection, then build the mapping from Android standard keycodes. Map analog triggers (AXIS_LTRIGGER/RTRIGGER/BRAKE/GAS) to ZL/ZR when present, otherwise L2/R2 keys.
- Save a profile per vendor:product and auto-apply it on connect (`NativeInputDeviceListener`).
- Test with Xbox, DualSense, a Switch Pro controller, and the Thor's built-in controls.

**Implemented** 8e9a5094 (2026-09-28; Azahar's `AutoMapDialogFragment.kt` re-read on master, GPL, so ideas only):
- **Prompt:** "press the button that should be A". `BUTTON_A` keeps names; `BUTTON_B` swaps A/B + X/Y. Unlike Azahar we let the user choose labels vs positions on Xbox pads instead of forcing positions.
- **Popup:** a `Popup`, because Dialog windows swallow the key events.
- **Fresh install:** found that no emulated controller exists, so controller 1 becomes a GamePad (BUGS K28).
- **Auto-map:** while controller 1 is unmapped, the first gamepad (SOURCE_GAMEPAD, sticks preferred) is mapped by button names, in the main process only.
- **Not done:** saved per-vendor:product profiles, the Joy-Con quirks, auto-map mid-game from the emulation process.

## 3. Save states
**Prior art:**
- **cemu-project/Cemu PR #953 (Spegs21)** (read 2026-09-28):
  - Status: draft, opened 2023-07, activity in 2025-04.
  - Serializes PPC thread contexts, RAM, parts of coreinit and IOSU (fsa), VRAM textures, and the vertex/uniform caches.
  - The PR itself says every CafeOS module and IOSU driver still needs its own load/store.
- **cemu-project/Cemu issue #2062 (Matt-Wood-23, 2026-09-15)**:
  - "Code that works", tested only with Monster Hunter 3 Ultimate, offered as a PR or reference.
  - Upstream closed it as "not planned": save states count as an avoided large-scale feature.
- **Comparison:** Azahar supports save states on Android, and its 2126.1 release fixed surface crashes during save-state loading (release notes, read 2026-09-28).

**Assessment:**
- Feasible only as a fork feature.
- **Minimum state:** all guest RAM regions, PPC thread contexts plus the scheduler, the HLE module state, IOSU state including open file handles, GPU state, audio state, and timers.
- **GPU state options:** either flush and rebuild the GPU caches on load, or serialize texture and buffer caches.
- **Superseded by §10 (2026-09-29):** the full feasibility study, the prior art and a plan.
- **Risks:**
  - Save files corrupted by a state restored mid-write.
  - State files that are invalid across builds (version them).

## 4. Dual screen / handheld UX
**Where things stand in the scene:**
- The Thor's secondary display is used as a Presentation display by Azahar ("Secondary Display Screen Layout") and by melonDS forks.
- Our fork (0.5.x) has "External PAD screen", "Swap screens" and "Rotate external screen left" (retrogamecorps.com dual-screen guide, droix.net; read 2026-09-28).
- The owner (as SapphireRhodonite) authored Azahar PR #1341: "Dual screen fixes for handhelds like Ayaneo Pocket DS", which added touch handling on the secondary surface and relayed it to native code.

**Azahar's secondary-display work** (azahar-emu/azahar; read 2026-09-28):
- **#617:** first secondary display support (physical display or Chromecast/Miracast).
- **#1371, merged 2025-10-03:** a `DisplayManager.DisplayListener` so the layout updates automatically when a display is added or removed.
- **#1385, merged 2026-06-14:**
  - Adds layout options for the secondary display: "Reverse Primary" (shows whichever screen isn't primary, honoring swap), Hybrid, Large Screen, and Original/Stacked.
  - Adds a quick menu, shown only when a secondary display exists, with a display picker when there are two or more displays.
  - By default it excludes displays flagged "built-in" to prefer external or dual-screen panels.
- **#1437 (open issue):** with an external monitor attached, the monitor is chosen instead of the built-in bottom screen. The lesson is to let the user pick the target display, and remember it per display name.
- **#1978 (open issue):** users want the input overlay and performance overlay on the secondary display.

**Ideas for us (Phase 5 plus backlog):**
- A display picker when there are two or more candidate displays.
- An "auto-use the second screen when present" default.
- A per-display remembered choice.
- Optionally show the TV image on the second screen too (mirror / "reverse primary").
- Input overlay on either screen.
- A performance overlay on the pad screen.
- Split-ratio and position options for single-screen devices.
- A hotkey for swap screens.

**Pitfall we already hit:** D4 in BUGS.md (the Presentation is recreated on every `onDisplayChanged`). Key it on the display id.

## 5. Performance and graphics options
**What exists in Cemu today:**
- Async shaders, vsync, accurate barriers, scaling filters, and the native ImGui overlay (FPS, CPU, RAM, draw calls).
- Resolution, FPS and mods come through graphic packs (downloadable in-app).

**What others add:**
- In-app driver download (Eden).
- Per-game presets pulled from a curated repo (Eden).
- Post-processing shader selection.
- A frametime graph.
- Thermal and battery readout (common on handheld-focused forks).
- A sustained-performance toggle.

**Our planned engine-side work** (Phase 6, see BUGS.md P1–P10): ADPF hint sessions, Vulkan pre-rotation, an instant timer, per-swapchain present modes, and pipeline-cache robustness.

**Candidate features:**
1. A "Graphics" quick menu in-game: resolution preset from the game's graphic pack, FPS++ preset, and a vsync toggle.
2. A frametime graph in the overlay.
3. A battery and temperature readout.
4. Shader cache import/export (for sharing caches between devices).
- **Eden (2026):**
  - Android "Vulkan workers" count, a legacy rescale pass, post-processing shaders on Android (nightly, 2026-09-10), frame pacing, and a performance overlay with FPS, frametime, CPU and GPU readouts (pocket-lint, androidauthority, edenemulator.co; read 2026-09-28).
- **Azahar 2126:**
  - "Skip presenting duplicate frames" (default on), and an optional GPU-timing simulation (GitHub releases 2126.0/2126.1).

**ADPF prior art:** Eden's `src/common/adpf.cpp` (eden-emulator/mirror, read 2026-09-28; GPL-3.0, so ideas only, no code copied into this MPL-2.0 project).
- It loads `APerformanceHint_*` from `libandroid.so` at runtime.
- It keeps a "Render" session with a 16.67 ms target and a "Background" session that prefers power efficiency (API 35+).
- It reports the frame interval clamped to 4× the target.
- It uses `setThreads` when available (API 34+), otherwise recreates the session.

Ours (76ac98f1) follows the same approach with one session.
- **Idea for later:** adapt the target to the title's real frame rate (30 fps titles), and put the pipeline compiler threads into a power-efficient session.

## 6. Game and data management
**What we have** (Kotlin audit inventory, 2026-09-28):
- **Game list:** search, favorites, per-game profile, remove shader caches, home-screen shortcuts.
- **Title manager:**
  - Filters by type, format and location.
  - Installs from a folder, with progress and cancel.
  - Deletes titles.
  - Converts to WUA.
- **Custom data root with SAF save mirroring** (dual branch, `CemuDataStorage.kt`).

**Missing, which others commonly have:**
- Cover art or boxart (Dolphin, Eden and Azahar show game art in the grid; ours shows the title icon only).
- **Save export/import per title**: backup and restore of a single game's save as a zip. Most useful for moving between devices.
- **Installing single-file WUA/WUP through a file picker** (today: install from a folder only).
- A long-running install/convert as a **foreground service**, so it survives backgrounding (K13).
- **Shader cache import/export**. Cemu's transferable caches are portable between any devices and drivers; only the compiled caches are per driver (§8). Built 2026-09-29, see TODO "Shader compilation per game".
- Play-time and last-played display exists, but there are no sorting options (recent, most played).

## 7. Input and controls (beyond auto-map)
**What we have:**
- 8 controller slots with type selection (GamePad, Pro, Classic, Wiimote) and per-button mapping.
- Controller and phone rumble.
- Motion from controller sensors (Android 12+) or the phone.
- An input overlay with an editor (move and resize, opacity, per-button visibility).
- 3 hotkey actions (quit, menu, USB devices).
- Touch on both screens.

**Missing, or weaker than others:**
- **Auto-map** (§2).
- Per-game controller profiles.
- Profile import/export.
- More hotkeys: swap screens, toggle pad, screenshot, pause, fast-forward (Eden, Azahar and Dolphin have larger hotkey sets).
- Per-screen overlay layouts (portrait/landscape, dual screen), plus an overlay on the second screen (Azahar issue #1978).
- **Amiibo:** Cemu supports NFC via `nn_nfp` loading amiibo `.bin` files. Android has no UI to pick one, and it would need a file picker plus a JNI call (check `Cafe/OS/libs/nn_nfp`).
- Mouse and keyboard for the pointer or touch (Winlator-style) is out of scope.

---

## 8. Shader caches: "compile shaders for a game" (owner request, 2026-09-29)
**How Cemu's caches work** (sources, read 2026-09-29):
- Primary source, our core:
  - `LatteShaderCache.cpp`: `LatteShaderCache_Load` runs at Latte thread start (`LatteThread.cpp:203`), before the game's GX2 init.
  - `VulkanPipelineStableCache.cpp`, `FileCache.h`.
- Cemu 1.25.0 changelog (cemu.info/changelog/cemu_1_25_0.txt):
  - The Vulkan pipeline cache is "independent of hardware and drivers and can therefore be transferred between different PCs".
  - "Pipelines are directly tied to your shader cache. You need both caches to restore pipelines."
- Community caches for "Cemu 1.25, 1.26 and Cemu 2.x" exist (chriztr.github.io/cemu_shader_and_pipeline_caches/). Our core is 2.x.

**The layers (Android only uses Vulkan):**

| File (under the cache path) | What it is | Portable? |
|---|---|---|
| `shaderCache/transferable/<titleid>_shaders.bin` | Every GX2 shader the game has used so far (the "shader list") | Yes, between devices, GPUs, drivers and desktop ↔ Android |
| `shaderCache/transferable/<titleid>_vkpipeline.bin` | Every Vulkan pipeline state seen (needs the shaders file) | Yes |
| `shaderCache/precompiled/<titleid>_spirv.bin` | The shaders translated to SPIR-V | Renderer-specific, GPU-independent |
| `shaderCache/driver/vk/<titleid>.bin` | `VkPipelineCache` blob from the driver | **Only for this GPU and driver**; switching custom drivers (Turnip ↔ Qualcomm) means compiling again |

- **At every boot,** `LatteShaderCache_Load` compiles every transferable entry for the current driver behind the "shader progress" screen, then the pipelines. That is the "compile all shaders" step.
- **The "full list":** it can't be extracted from the game files. It grows while the game runs, so the most complete list comes from merging caches (your own plus shared ones).
- **File version:** the `FileCache` header's "extra version" encodes the title id:
  - Shaders: `((hi) + (lo)*3) + 1 + 0xe97af1ad`, plus a legacy value of 2 for 1.25.0–1.25.1b files.
  - Pipelines: the same without the constant.

  A file from another game fails to open, so an import can be validated by opening it the way the core does. Entries are keyed by hash (`name1`/`name2`), so two caches for the same title can be merged by adding the missing entries.

**Plan:**
1. **Import and merge** per game: pick `.bin` files (or a zip of them). Each is validated against the game's title id, its type is found by trying both versions, and missing entries are merged into the existing cache.
2. **Export** both transferable files as a zip, for other devices or desktop.
3. **"Compile shaders now":** launch the game in a compile-only mode. The Latte thread runs `LatteShaderCache_Load`; then the app saves the driver pipeline cache and exits before gameplay, reporting the counts.
4. **Show counts** (shaders, pipelines, whether the driver cache exists).

**Caveat:** don't import while the same game is running in the emulation process. The emulation session lock (D7) now makes it visible, and compile-only mode holds that lock too.

**Built (session 4):** all four steps, see TODO "Shader compilation per game". Metal caches (`_mtlshaders.bin`) have the same header and are skipped by name. Custom graphic-pack output shaders that read `gl_FragCoord` would see rotated coordinates with pre-rotation (§9); none of the built-in ones do.

## 9. Vulkan pre-rotation and the ADPF target (session 4, 2026-09-29)
**Pre-rotation.** Source: developer.android.com/games/optimize/vulkan-prerotation (fetched 2026-09-29).
- **The rule:** set `preTransform = currentTransform` and keep `imageExtent` in the identity orientation (swap `currentExtent` for 90/270). Rotate the output in clip space and remap viewport and scissor.
- **Detecting rotation:** Android 10+ reports a transform mismatch as `VK_SUBOPTIMAL_KHR` from `vkQueuePresentKHR`. The guide's viewport formulas:
  - 90°: `{W - h - y, x, h, w}`
  - 180°: `{W - w - x, H - h - y, w, h}`
  - 270°: `{y, H - w - x, h, w}`
- **Why we needed it:** our Android build forced IDENTITY and ignored the per-frame SUBOPTIMAL. On devices whose natural orientation is portrait (most phones), the compositor rotates every landscape frame.
- **How we did it:**
  - Only the final output is rotated: the output quad through a specialization constant, plus ImGui's draw data on the CPU. Game rendering is unaffected.
  - The extent swap compares `currentExtent` with the window instead of trusting the orientation it reports.
  - A mismatch (SUBOPTIMAL) triggers a surface query about once a second.
- **To verify on devices:**
  - Whether the Thor's panels are natively landscape; then the transform is IDENTITY and nothing changes.
- **AOSP check** (android.googlesource.com `frameworks/native` main, `vulkan/libvulkan/swapchain.cpp`, fetched 2026-09-29; the old GitHub `aosp-mirror` repo now returns 404):
  - `GetPhysicalDeviceSurfaceCapabilitiesKHR` sets `currentExtent` from `NATIVE_WINDOW_DEFAULT_WIDTH/HEIGHT`, which is the window's current orientation. The guide's swap for 90/270 is therefore right. Our code compares against the window instead, which gives the same result.
  - `currentTransform` comes from `NATIVE_WINDOW_TRANSFORM_HINT`.
  - `vkQueuePresentKHR` returns `VK_SUBOPTIMAL_KHR` only when the swapchain's `pre_transform` differs from the window's transform hint; extent changes don't cause it. With pre-rotation off, a rotated display therefore gets SUBOPTIMAL on every frame, which is why SUBOPTIMAL only triggers a cheap recheck here.

**ADPF target.** NDK 29 `android/performance_hint.h`: `APerformanceHint_updateTargetWorkDuration(session, int64_t)` (API 33).
- **The problem:** we report frame intervals, so a 30 fps title measured against 16.7 ms always looked late and kept clocks high.
- **The fix:** the target is now `16.67 ms × GX2SetSwapInterval` (1–4; 0 = vsync off counts as 1), and reports are capped at 4× the target.
- **Limitation:** a title that paces itself to 30 fps but leaves the swap interval at 1 still gets the 60 fps target.

## 10. Save states: feasibility (session 4, 2026-09-29)
The owner asked for a write-up first; no code. This section replaces the "start by contacting issue #2062's author" note in §3.

**Sources** (GitHub API and raw files, read 2026-09-29):
- cemu-project/Cemu PR #953 (Spegs21): `pulls/953` and `pulls/953/files`.
- Issue #2062 and its comments.
- The branch it links, github.com/Matt-Wood-23/Cemu/tree/savestates, including:
  - The compare against Cemu main: 8 commits, 47 files, ~4.6k added lines.
  - Its five docs under `docs/save-states-*.md`.
  - `src/Cafe/SaveState/*`.
- Our core, for the drift numbers below.

### Prior art
| | PR #953 (Spegs21) | Matt-Wood-23 `savestates` (issue #2062) |
|---|---|---|
| State | Draft, opened 2023-08, last update 2025-04 | Branch last pushed 2026-09-15, based on Cemu main `3310f3b8` (2026-09-10) |
| Size | 99 files, +2015 lines; a `save(writer)`/`restore(reader)` pair added to almost every module | 47 files, ~4.6k lines, of which ~2.5k are core C++ (the rest is docs, Python probes and the wx menu) |
| Approach | Suspend threads, dump RAM and a few module tables; no GPU or fiber handling | Quiesce at the scheduler boundary; RAM dump plus rebuilt host state; GPU cache drop; details below |
| Verified | Nothing reported working | Monster Hunter 3 Ultimate on Windows x64: save, walk to another area, load; the restored world keeps running and makes further area transitions, with audio, no crash log |
| License | MPL-2.0 (Cemu) | MPL-2.0 (fork of Cemu, the repo's license field says so) |
| Upstream | Stalled | Closed "not planned" within 20 minutes: Cemu doesn't accept AI-written contributions (CONTRIBUTING.md). A technical rejection was not the reason |

**PR #953 is superseded.** It serializes host tables without a world-stop and leaves the GPU and fibers to chance.

The Matt-Wood-23 design is the one to build on. It was found by measuring, and its docs are frank about what failed.

### How the Matt-Wood-23 design works
1. **Quiesce (`SaveState/Quiesce.cpp`):** each scheduler core parks in its idle loop right after `__OSStoreThread()`, so every guest thread's registers are already in guest RAM.
   - Busy cores are routed through the idle loop while a request is pending.
   - The Latte thread also parks at a command-packet boundary (`LatteCP_readU32Deprc`'s idle path).
   - There is a 2 s timeout: the save fails rather than deadlocking.
2. **Chunked stream (`StateStream`, symmetric read/write, a marker per chunk):**
   - `MEMR`: every mapped MMU range, about 286 MiB for MH3U.
   - `CPUS`: per-core state, plus the new `hleEntryStackPointer` per thread.
   - `TIME`: the PPCTimer counters.
   - `ALRM`: host alarms, via a callback-ID registry.
   - `SNDV`: AX voice lists.
   - `GX2S`/`LATT`: GPU registers and cursors.
   - `FSAH`: open IOSU file handles, reopened at the same slot and check value. Create/truncate flags are stripped; directory iterators are skipped.
   - The container is zstd-compressed, with an uncompressed header (thumbnail, fingerprint), 10 slots plus undo.
3. **Load:**
   - The state is read, decompressed and fingerprint-checked before the world stops.
   - Then RAM is replaced, and every guest-address-keyed GPU cache is dropped on the Latte thread.
   - Host fibers are rebuilt from the restored thread list.
   - Threads blocked inside an HLE call are restarted at their HLE entry stack pointer. That needs a hook in the interpreter, `BackendX64` and `BackendAArch64`. The recompilers bypass `PPCInterpreter_virtualHLE`.
4. **Fingerprint:** build version, title id and version, multicore mode. Their next step: hash the HLE call table (names in registration order), because dev builds share a git hash.

**The lesson that applies to us too:** in an HLE emulator a RAM snapshot is necessary but not sufficient. Every failure they hit was host state describing guest objects:
- fibers
- the Latte thread reading guest memory mid-restore
- AX voice vectors (an endless mixer loop)
- the FSA handle table (streamed music stopped)

### Still open in that branch (their §8 "known-unfixed")
1. `FSClient` allows 1 command in flight, so a load taken with a command in flight can wedge file I/O. Seen in source, not yet triggered.
2. IOSU host threads (`FSAIoThread`, nn service threads) are not parked by the quiesce, so they can write guest memory during the restore.
3. `__depr__IOS_Ioctlv` (MCP, act, acp, nim) blocks by self-suspend, which the restart helper doesn't see.
4. `OSWaitCond`/`OSFastCond_Wait` keep `prevLockCount` on the host stack.
5. `nlibcurl.curl_easy_perform` runs a detached host worker that holds fiber-stack pointers, so a load can cause a use-after-free.

Also: a chunk-layout mismatch is found only after `MEMR` has overwritten memory. Chunk compatibility should be validated from the header first. And only one title was tested.

### Fit with our fork
- **Drift is small.** Our core last merged Cemu main on 2026-04-19; their base is 2026-09-10. For the files the branch touches, the differences between our HEAD and their base:

  | File | Lines that differ |
  |---|---|
  | `coreinit_Alarm`, `ax_voice`, `ax_ist`, `iosu_fsa`, `PPCScheduler`, `PPCInterpreterHLE`, `PPCState.h` | 0 |
  | `TCL.cpp` | 3 |
  | `BackendAArch64.cpp` | 11 |
  | `PPCTimer.cpp` | 12 |
  | `coreinit_Thread.cpp` | 47 |
  | `LatteThread.cpp` | 49 |
  | `ax_mix.cpp` | 88 |
  | `LatteCommandProcessor.cpp` | 269 |

  Most of those differences are our own Android changes (cntfrq timer, ADPF thread registration, the precompile mode, the Latte guard). A port is a merge exercise, not a rewrite.
- **Platform:** the core part is platform-neutral. zstd is already a dependency (`vcpkg.json`, linked by CemuCafe). Only their wx menu and probe scripts are Windows-specific.
- **AArch64:** they already hook `BackendAArch64.cpp`. Android runs the AArch64 recompiler, so the HLE-entry stack-pointer capture works, but it has never run on ARM.
- **Android-specific residue to check when porting:**
  - Our `PauseTitle` (threads suspended, AX output paused), so saving while paused must still quiesce.
  - `SyncCanvasWindow` runs in the same idle path as their GPU hook.
  - SAF-backed files (`fscDeviceAndroidSAF`): `FSAH` reopens by FSC path, which goes through the SAF device again.
  - Amiibo/NFC state.
  - `NativeSwkbd` text.
- **Memory:** the capture holds `MEMR` in RAM (~290 MiB for MH3U, up to ~1 GiB in theory), and their undo keeps another copy in RAM. On 6–8 GB phones that invites the low-memory killer. On Android, undo should go to a file, and compression should stream.
- **Storage:** 100–400 MB per state after zstd. Store under `<data root>/savestates/<titleid>/`. Never mirror states through the SAF save sync (too big, and not real saves).
- **Build lock:** every APK update invalidates states until the fingerprint hashes the HLE table. Users must be told, or a state should be refused with a clear message (it already is, by fingerprint).
- **Upstream:** Cemu won't take AI-written code, and this fork is AI-assisted too. Save states stay fork-only whatever we do. Keep them behind an "experimental" setting.

### Plan if the owner wants it built
| Step | Contents | Estimate |
|---|---|---|
| A. Port | Bring `src/Cafe/SaveState/*` and the hooks (scheduler, alarms, AX, FSA, TCL, Latte, PPCTimer, the three HLE dispatch paths) onto our tree. Resolve the coreinit_Thread/Latte hunks against our Android changes. Header-first chunk validation; fingerprint with an HLE-table hash; undo to a file | 1 session |
| B. Android UI | Setting "Save states (experimental)", off by default. In-game menu: save/load slot 1–3 with thumbnail and time, undo last load. Hotkeys (append-only `HotkeyAction` values). JNI calls run on a worker thread, never the UI thread (the quiesce blocks up to 2 s) | 1 session |
| C. Device iteration | The owner tests MH3U (the known-good title), then e.g. Mario Kart 8, BotW, Splatoon (offline). Fix what the log shows; start with known-unfixed items 1, 2 and 5 | 1–3 sessions, driven by the owner's logs |

**Success criteria for an experimental release:** save, then load in the same session, and play on for 5 minutes, on 3 titles; no crash; states refused (not corrupted) after an APK update.

### Recommendation
Feasible as an experimental, fork-only feature by porting the Matt-Wood-23 branch (MPL-2.0, keep its file headers and credit it in the commit message), not PR #953.

The hard parts are already solved: the quiesce, fiber rebuild, HLE restart, AX and FSA state. What's left is breadth: more titles, the five known-unfixed hazards, and the GPU render targets. Those only show up by testing on devices.

**Decision needed from the owner:** start step A next session (yes/no). If yes, the owner should also name 2–3 games they want it for, and confirm that the known limits are acceptable:
- states are tied to one APK build;
- loading an old state after an in-game save can confuse the game;
- online sessions drop.

## 11. Graphics options for looks and performance (owner request, session 4, 2026-09-29)
The owner asked for "vulkan/openGL options and other graphical settings that can help improvements for games either more visually or for more performance".

**OpenGL is not an option on Android.** `app/build.gradle.kts` passes `-DENABLE_OPENGL=OFF`, and Cemu's OpenGL backend targets desktop GL 4.5; Android has GLES only. Everything below is Vulkan.

**What others expose** (read 2026-09-29):
- **Eden**, from eden-emulator/mirror master, `IntSetting.kt`, `BooleanSetting.kt` and `res/values/arrays.xml` (GPL-3.0, so ideas only). Its Android settings include:
  - resolution scale (`resolution_setup`) and `max_anisotropy`
  - `scaling_filter`: nearest, bilinear, bicubic, gaussian, lanczos, ScaleForce, FSR (with an `fsr_sharpening_slider`), area, MMPX, and more, up to SGSR
  - `anti_aliasing` (FXAA/SMAA), `pipeline_worker_count`, `frame_pacing_mode`, `async_presentation`, and frame generation
- **Dolphin**, from dolphin-emu master, `IntSetting.kt` and `BooleanSetting.kt`:
  - `GFX_EFB_SCALE` (internal resolution), `GFX_MSAA`
  - `GFX_ENHANCE_MAX_ANISOTROPY`, `GFX_ENHANCE_FORCE_TEXTURE_FILTERING`
  - colour correction (`GFX_CC_CORRECT_GAMMA`, colour space)
  - `GFX_ENHANCE_DISABLE_COPY_FILTER`, `GFX_WIDESCREEN_HACK`

**What Cemu can already do, and the Android gaps** (our core, 2026-09-29):
- Resolution, FPS and most visual mods come through graphic packs; Cemu has no global render scale. The Android graphic packs screen was only a big tree with no way to jump to a game's packs.
- The gamma options (`overrideAppGammaPreference`, `overrideGammaValue`, `userDisplayGamma`) exist in `CemuConfig` and are used by the output shader (`RendererOuputShader.cpp` FillUniformBlockBuffer, `ActiveSettings::GetTVGamma`), but only desktop has a UI for them.
  - Target gamma is the TV gamma to reproduce; games can add an offset with GX2SetTVGamma, which "override" ignores.
  - Display gamma 0 means the piecewise sRGB curve.
- Anisotropy comes from the game's sampler registers, or from graphic-pack texture rules (`overwriteInfo.anisotropicLevel`). There is no global setting.
- The upscale filter can be bilinear, bicubic, Hermite or nearest. Graphic packs can provide output shaders.
- `gx2drawdone_sync` isn't worth exposing: Vulkan always forces the full sync (`GX2_Event.cpp` GX2DrawDone).

**Built in session 4** (Android-only core additions behind `BOOST_PLAT_ANDROID`):
1. Pre-rotation toggle (§9), now a native config value (`vkPreRotation`).
2. Gamma section in Graphics settings: target gamma, ignore the game's gamma, display gamma or sRGB.
3. Anisotropic filtering "at least 2x–16x" (`AnisotropicFilter`).
   - It applies only to samplers that filter linearly with mipmaps and don't compare depth. Nearest-filtered textures (UI, pixel art) and shadow maps are left alone.
   - It is clamped to `maxSamplerAnisotropy`, and graphic-pack rules still win.
4. Upscale filter "AMD FSR 1": the EASU pass of FidelityFX FSR 1.
   - Source: github.com/GPUOpen-Effects/FidelityFX-FSR `ffx_fsr1.h` v1.20210629 and the `ffx_a.h` helpers; MIT, with the notice kept in `RendererOuputShader.cpp`.
   - It runs in the existing single output pass, using `textureGather` and `passUV`, never `gl_FragCoord`, so it works with pre-rotation.
   - RCAS sharpening would need a second pass and is left out.
   - Downscaling, or a shader that fails to compile, falls back to bilinear.
5. "Graphic packs…" in a game's long-press menu opens only that game's packs (resolution, FPS, mods).

**Candidates for later:**
- FXAA as a post pass. It needs an intermediate target, or a combined FXAA-and-scale output shader.
- A pipeline compile thread count (Eden's "Vulkan workers"); ours is min(8, cores−1) at background priority (P9).
- RCAS sharpening with a strength slider.
- Letting graphic-pack presets change while a game runs. Cemu needs a restart for most packs.

## 12. Frame generation, "Lossless Scaling" (owner request, session 5, 2026-09-30)
The owner asked for "an in-game Lossless scaling system", with Eden's nightly builds (git.eden-emu.dev/eden-ci/nightly/releases) as the reference.

**Decisions (owner, 2026-09-30):**
- Port Eden's code (GPL-3.0-or-later) with its headers and credit. The app as a whole is distributed under the GPL-3.0 from now on: `README.md` License section, `LICENSE.GPL-3.0.txt` (the gnu.org text, SHA-256 3972dc97…6986). Cemu's MPL-2.0 files stay MPL; MPL 2.0 §3.3 allows a GPL "Larger Work".
- The owner owns Lossless Scaling and tests with their own `Lossless.dll`. Cemu ships no shaders.

**Sources** (read 2026-09-30, Eden master):
- `src/video_core/frame_gen/lossless_dll.*`, `lsfg_translate.*`
- `src/video_core/renderer_vulkan/present/`: `lsfg_common`, `lsfg_shaders`, `lsfg_mipmaps`, `lsfg_alpha`, `lsfg_beta`, `lsfg_gamma`, `lsfg_delta`, `lsfg_generate`, `lsfg_chain`, `frame_gen`, `frame_gen_pacer`, `util.cpp`
- Presentation: `renderer_vulkan.cpp` Composite, `vk_present_manager.cpp`
- Settings: `common/settings.h` (`frame_gen*`)
- Android UI: `LosslessManagerFragment.kt`, `LosslessScalingHelper.kt`
- Eden's lsfg files also credit lsfg-vk (GPL-3.0-or-later in their headers). lsfg-vk v2 itself is CC BY-NC-ND and is not used.

**How Eden's frame generation works:**
- `Lossless.dll` is parsed as a PE file. Its RCDATA resources hold SPIR-V compute shaders. Eden uses the half-precision variants (resource id + 49) of ids 255 (mipmaps), 256 (generate) and 280–302. The descriptor bindings are renumbered 0…n in (set, binding) order, and each pass binds one descriptor per binding.
- Passes shared by all generated frames, per rendered frame:
  - mipmaps: luminance at 7 levels, at the flow scale
  - alpha: 4 stages per level, with a 3-frame history
  - beta: 5 stages, 6 outputs
- Per generated frame: gamma (5 stages per level, coarse to fine), delta (10 stages, from level 4) and generate, which warps the previous and current frames along the motion.
- The constants of each (generation count, generation) pair are fixed in small uniform buffers; the timestamp is (g+1)/(n+1).
- Some descriptors are null, so it needs robustness2 `nullDescriptor`, plus `shaderFloat16` and `vulkanMemoryModel`.
- Presentation: `[generated…, real]` back to back, FIFO spaces them out. The pacer either uses the fixed multiplier, or with a target rate raises the generation count while that brings the output closer to the target, and lowers it if the emulation slows down.
- Automatic flow scale: the game's rendered width over its width on screen, in 5% steps, 25–100%.

**Cemu port (`src/Cafe/HW/Latte/Renderer/Vulkan/FrameGen/`, Android only):**
- Eden's `vk::` wrappers are replaced with plain handles, and memory comes from `VKRMemoryManager`. Vulkan errors throw, and `FrameGenerator` turns frame generation off with an overlay notification.
- `FrameGenDevice` (new): reads the modules' SPIR-V version, `OpCapability` and `OpExtension` before the device is created, and enables exactly those features, plus robustness2 `nullDescriptor`. Without `Lossless.dll` the device is created as before. If `vkCreateDevice` fails with them, it retries without.
- Cemu draws the TV picture (and ImGui) straight into the swapchain image, so `VulkanRenderer::PresentGeneratedFrames` works on that image:
  1. Copy the finished frame into the passes' input. A blit converts BGRA; Android swapchains are usually RGBA8, which only needs a copy.
  2. The acquired image goes out with generated frame 0. Each further generated frame gets a newly acquired image.
  3. The finished frame is copied back into a last acquired image and presented as usual.
  - That way only one swapchain image is ever acquired, as Cemu assumes elsewhere.
- The TV swapchain gets `TRANSFER_SRC`, FIFO and one spare image while frame generation is on. Switching it on or off recreates the swapchain, which is also when the passes are freed.
- Settings are Android-only config values, and the renderer reads them every frame:
  - `FrameGeneration`, `FrameGenMultiplier` (2–4), `FrameGenTargetRate` (0 = multiplier), `FrameGenFlowScale` (0 = automatic)
- UI:
  - Settings > Graphics > Frame generation: add, replace or remove `Lossless.dll` (checked natively, copied to `<user data>/lossless/`), the toggle, the mode (2x/3x/4x/adaptive up to 60, 90 or 120 FPS) and motion detail.
  - In-game menu: a "Frame generation" checkbox, shown once a DLL is added. It only switches the running game: the emulation process never saves `settings.xml`, which belongs to the main process.

**Limits and things to watch on the device:**
- **Pacing:** FIFO alone shows each generated frame for one refresh. That's even when the output rate matches the screen (2x of 30 FPS at 60 Hz). On the Thor's 120 Hz screen, 2x would show the generated frame for 1 refresh and the real one for 3.
  - Where the device has `VK_GOOGLE_display_timing` (enabled with the other frame generation features), presents get desired times. Generated frame 0 goes out right away; frame k at start + k × (frame time / (n+1)); the rendered frame gets the last slot.
  - AOSP's swapchain hands the time to `native_window_set_buffers_timestamp` (frameworks/native `vulkan/libvulkan/swapchain.cpp` SetSwapchainFrameTimestamp, read 2026-09-30), and SurfaceFlinger holds the buffer until then.
  - `Surface` keeps that timestamp for every later frame: `mTimestamp` only changes in `setBuffersTimestamp` (`libs/gui/Surface.cpp`, not even on disconnect). So the first present without pacing passes `INT64_MIN`, which is `NATIVE_WINDOW_TIMESTAMP_AUTO` after the swapchain's int64 cast, to go back to automatic timestamps.
  - The GPU time spent generating frame 0 still shortens its slot a little.
- **Overlay:** "Frame generation on" when it starts. With the FPS overlay, a "Shown: N FPS (frame generation)" line counts presented frames, generated ones included.
- It adds about one frame of latency, since the real frame waits for the generated ones. The GPU work runs on the emulation's queue, so a GPU-bound game gets slower.
- The overlay and notifications are part of the captured frame and get interpolated too.
- Untested on a device so far (owner test, TODO session 5 item 2).

## Gap table
Only facts verified from the sources above are marked. `✓` = has it, `–` = verified absent, `?` = not verified yet, `partial` = see notes.

| Feature | Ours (0.5.x) | Eden | Azahar | Dolphin | PPSSPP | NetherSX2 | Vita3K |
|---|---|---|---|---|---|---|---|
| In-app GPU driver *download* | ✓ (e8efeb45) | ✓ | ? | ? | ? | ? | ? |
| Custom driver install from zip | ✓ | ✓ | ? | ? | ? | ? | ✓ |
| Per-game driver | ✓ | ✓ (per-game settings) | ? | ? | ? | ? | ? |
| Per-game settings (broad) | partial (CPU mode, thread quantum, shader precision, shared libs, driver) | ✓ | ? | ✓ | ✓ | ✓ | partial |
| Curated per-game defaults | partial (`gameProfiles/` from `bin/`) | ✓ (eden-overrides repo) | ? | ✓ (GameINI) | ? | ? | ? |
| Controller auto-map | – | ? | ✓ ("press A" layout detect) | ? | ? | ? | ? |
| Save states | – | ? | ✓ | ✓ | ✓ | ✓ | ? |
| Secondary display (dual screen) | ✓ (basic) | n/a | ✓ (layouts + picker) | n/a | n/a | n/a | n/a |
| Performance overlay | ✓ (FPS, CPU, RAM, draw calls, battery/thermal, frame time graph) | ✓ (FPS, frametime, CPU/GPU) | ? | ✓ (FPS, speed, frame times, graphs) | ? | ? | ? |
| Post-processing shaders | partial (upscale filters incl. AMD FSR 1, graphic-pack output shaders) | ✓ (2026-09 nightly; FSR, ScaleForce, Lanczos, SGSR…) | ? | ? | ✓ | ? | ? |
| Anisotropic filtering override | ✓ (session 4) | ✓ (`max_anisotropy`) | ? | ✓ (`GFX_ENHANCE_MAX_ANISOTROPY`) | ? | ? | ? |
| Gamma / colour correction | ✓ (session 4: target/display gamma) | ? | ? | ✓ (`GFX_CC_*`) | ? | ? | ? |
| Resolution scaling | via graphic packs | ✓ | ✓ | ✓ | ✓ (up to 10×) | ✓ | ? |
| Fast-forward / speed | – | partial (`use_speed_limit` off) | ? | ? | ✓ | ✓ | ? |
| RetroAchievements | n/a (Wii U unsupported by RA) | – | – | ✓ (dev builds) | ✓ | ? | – |
| Cheats | via graphic-pack patches | ? | ? | ✓ (AR/Gecko, `MAIN_ENABLE_CHEATS`) | ✓ | ✓ | ? |

**Sources:** the sections above; the Eden and Dolphin settings files of §11 (read 2026-09-29) for the rows added in session 4. Also Dolphin (androidauthority.com 2503 update; retroachievements.org forum topic 33323), PPSSPP (ppsspp.org; 1.20 in March 2026 added native DualSense support and portrait mode), NetherSX2 (netherx2.org), and Vita3K (heldgames.com guide; it clears the shader cache when the driver changes). All read 2026-09-28.

## Prioritized backlog → TODO.md
Ordered by value to the owner divided by cost. Each item becomes its own phase with the usual build → device test → tick loop, and each needs a short re-check of the sources above before starting.

1. **GPU driver fetcher (Eden-style).**
   - **Why:** the owner asked for it explicitly, it has a large performance/compatibility impact on Adreno, and it mostly reuses existing install code.
   - **Scope:**
     - Repo list and release list from the GitHub API.
     - Download, then verify it's a zip with `meta.json`.
     - Install via the existing `CustomDriversViewModel` path.
     - GPU name via JNI (`vkGetPhysicalDeviceProperties`), plus the recommendation table.
   - **Risks:**
     - The GitHub API limit is 60 requests/hour unauthenticated, so cache results.
     - The repo list goes stale; keep it in one Kotlin file and re-check the repos when touching it.
2. **Controller auto-map with a layout prompt (Azahar-style)**, plus a default profile auto-applied when a known controller connects.
3. **More hotkeys:** swap screens, toggle pad, pause, screenshot. Cheap, and it helps dual-screen users.
4. **Dual-screen polish:**
   - A display picker and per-display memory.
   - The option to mirror the TV on the second screen.
   - The overlay on the second screen.
   - Split ratio for single-screen devices.
5. **Per-game controller profiles and overlay layouts.**
6. **Save backup/restore per title (zip)**, plus a single-file WUA/WUP install picker.
7. **Frametime graph and battery/temperature readout** in the overlay; sustained-performance toggle.
8. **Amiibo picker** (`nn_nfp`).
9. **Save states.** Large; fork-only. Start from Cemu issue #2062 and PR #953, and prototype on one game. Clarify with the owner how much instability is acceptable (experimental flag).
10. **Vulkan pre-rotation (P1)** and **adaptive ADPF target**: engine work, measured with before/after FPS on the Thor.
