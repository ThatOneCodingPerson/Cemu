# Feature research: what other Android emulators offer, and how to bring it here

Every claim here has a source and an access date. Re-verify anything older than about 3 months before building on it; the emulation scene moves fast.

**Owner priorities (2026-09-28):**
- All areas are in scope.
- Emphasis on:
  1. Controller-type detection with automatic keybinds.
  2. Save states.
  3. An Eden-style GPU driver manager (download Turnip and others, plus local install) and more graphics options.
- Dual screen for the AYN Thor and other devices.

Status: **in progress** (Phase 7). The sections below are filled as research lands; the backlog at the bottom feeds TODO.md.

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

## 6. Game and data management
(pending)

## 7. Input and controls (beyond auto-map)
(pending)

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
(pending)
