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
- **Start by contacting issue #2062's author and diffing their code against PR #953.**
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
- **Shader cache import/export** (Cemu's transferable caches are portable between devices of the same driver family).
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

## Gap table
Only facts verified from the sources above are marked. `✓` = has it, `–` = verified absent, `?` = not verified yet, `partial` = see notes.

| Feature | Ours (0.5.x) | Eden | Azahar | Dolphin | PPSSPP | NetherSX2 | Vita3K |
|---|---|---|---|---|---|---|---|
| In-app GPU driver *download* | – | ✓ | ? | ? | ? | ? | ? |
| Custom driver install from zip | ✓ | ✓ | ? | ? | ? | ? | ✓ |
| Per-game driver | ✓ | ✓ (per-game settings) | ? | ? | ? | ? | ? |
| Per-game settings (broad) | partial (CPU mode, thread quantum, shader precision, shared libs, driver) | ✓ | ? | ✓ | ✓ | ✓ | partial |
| Curated per-game defaults | partial (`gameProfiles/` from `bin/`) | ✓ (eden-overrides repo) | ? | ✓ (GameINI) | ? | ? | ? |
| Controller auto-map | – | ? | ✓ ("press A" layout detect) | ? | ? | ? | ? |
| Save states | – | ? | ✓ | ✓ | ✓ | ✓ | ? |
| Secondary display (dual screen) | ✓ (basic) | n/a | ✓ (layouts + picker) | n/a | n/a | n/a | n/a |
| Performance overlay | ✓ (FPS, CPU, RAM, draw calls) | ✓ (FPS, frametime, CPU/GPU) | ? | ? | ? | ? | ? |
| Post-processing shaders | partial (upscale/downscale filter) | ✓ (2026-09 nightly) | ? | ? | ✓ | ? | ? |
| Resolution scaling | via graphic packs | ✓ | ✓ | ✓ | ✓ (up to 10×) | ✓ | ? |
| Fast-forward / speed | – | ? | ? | ? | ✓ | ✓ | ? |
| RetroAchievements | n/a (Wii U unsupported by RA) | – | – | ✓ (dev builds) | ✓ | ? | – |
| Cheats | via graphic-pack patches | ? | ? | ✓ (AR/Gecko) | ✓ | ✓ | ? |

**Sources:** the sections above. Also Dolphin (androidauthority.com 2503 update; retroachievements.org forum topic 33323), PPSSPP (ppsspp.org; 1.20 in March 2026 added native DualSense support and portrait mode), NetherSX2 (netherx2.org), and Vita3K (heldgames.com guide; it clears the shader cache when the driver changes). All read 2026-09-28.

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
