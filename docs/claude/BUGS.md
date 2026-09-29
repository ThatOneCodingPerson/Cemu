# Bug registry

Every entry was verified against the code on 2026-09-28. Line numbers are from `android-port-dual` 564bfdad (or `android-port` 8759323e for files the dual branch doesn't touch), so re-check them before editing.

**Severity:**
- **C** = crash, hang or data loss
- **H** = frequent user-visible failure
- **M** = latent or occasional
- **L** = minor

**Status:** `open` · `fixed <commit>` (built, not device-tested) · `verified <commit>` (owner confirmed on device) · `wontfix` (reason given).

When you fix an entry, update its status here and tick the matching item in TODO.md.

## A: original audit (Android layer)
| ID | Sev | Where | Problem | Status |
|---|---|---|---|---|
| A1 | C | `gui/androidgui/AndroidWindowSystem.cpp:17` | `ShowErrorDialog` is empty. `MMU.cpp:95→99`, `MMU.cpp:136→137` (4 GB reserve), `CafeSystem.cpp:176→179` (damaged RPX) then `exit()`, so the app vanishes silently. `KeyCache.cpp:78,112` and `GraphicPack2Patches.cpp:66` errors are also silent | fixed 92ecc623 |
| A2 | C | `Common/ExceptionHandler/ExceptionHandler_posix.cpp:69-178` | Doesn't save or chain the previous handlers, and has no SA_ONSTACK. Ends in `_Exit(1)`, so there's no tombstone or logcat backtrace. Runs boost::stacktrace, fmt and the async log inside the handler, and never flushes it, so the crash text is lost. Also catches SIGQUIT (ART's ANR dump) and SIGTRAP (`cemu_assert`) | fixed 92ecc623 |
| A3 | C | `Latte/Core/LatteThread.cpp:115-213`, throws at `SwapchainInfoVk.cpp:287,297-302`, `VulkanRenderer.cpp:2238,2941,2966,3136` | Exceptions on the GPU thread lead to `std::terminate` | fixed 92ecc623 |
| A4 | C | `VulkanRenderer.cpp:929-944` | `InitializeSurface(pad)` replaces the `unique_ptr` from the UI thread while the Latte thread uses it (use-after-free) | fixed f333fe7a |
| A5 | C | `VulkanRenderer.cpp:956-960` vs `2975-2985`; `SwapchainInfoVk.cpp:39-41` | `StopUsingPadAndWait` waits forever if the pad chain is invalid, so the UI thread hits an ANR. The GPU thread also blocks in an untimed `surface.wait` | fixed f333fe7a |
| A6 | H | `SwapchainInfoVk.cpp:36-37` | `RecreateSurface` returns early for the pad, so a lost pad surface is never recovered | fixed f333fe7a |
| A7 | M | `NativeEmulation.cpp` setSurface ~289-302, TestSurface ~178-191 | ANativeWindow leaks one reference per call. Detecting a new window by pointer comparison has an ABA risk once the leak is fixed | fixed f333fe7a |
| A8 | C | `NativeEmulation.cpp:~244`, `VulkanAPI.cpp:257-260` | The return value of `InitializeGlobalVulkan()` is ignored, which leads to null function pointers and a SIGSEGV. The loader is also re-dlopened on every call | fixed 92ecc623 |
| A9 | M | `VulkanAPI.cpp:168-173,213-214` | Custom-driver `meta.json` parse isn't checked, `libraryName` isn't sanitized, and a copy error is ignored, so a stale `custom_vulkan.so` gets loaded | fixed 92ecc623 |
| A10 | C | `cpp/AndroidFilesystemCallbacks.cpp:30-32`; `NativeFiles.kt:69-73,99-110` | There are zero `ExceptionCheck` calls. `listFiles` throws outside its try, then native calls `GetArrayLength(null)`, which aborts. `isFile = !isDirectory` | fixed 91f156d3 |
| A11 | H | `cpp/JNIUtils.h:210-215` | `FiberSafeJNICall` creates, attaches and joins a new thread per call. 9 call sites, including rumble, which holds controller mutexes | fixed 91f156d3 |
| A12 | H | `AndroidManifest.xml:~35` | EmulationActivity `configChanges` only has `orientation\|screenSize`, so a controller connecting, uiMode or display change recreates the activity mid-game | fixed 91f156d3 |
| A13 | H | `EmulationActivity.kt` onQuit; `VulkanRenderer.cpp:2338-2366,815` | `exitProcess` runs without a flush. The pipeline cache is written in place (no temp + rename) with a 15 s delay, the size check misses same-size changes, and there's no final save | fixed eae0b51f |
| A14 | M | `CemuApplication.kt` | `hash.txt` is written before the asset copy, so a kill mid-copy leaves the data permanently incomplete. All native init runs on the main thread. Two processes can race the copy | fixed eae0b51f |
| A15 | L | `EmulationActivity.kt:~108`, `EmulationViewModel.kt` init, `NativeEmulation.cpp:~351` | Throws when there's no launch path (and the activity is exported). Sets `initialized=true` on failure. Every prepare error becomes UNKNOWN | fixed 91f156d3 (missing path) + aaab5241 (INVALID_RPX/UNABLE_TO_MOUNT codes, JNI exception guard). The flag still means "initialization finished" (it hides the loading dialog); the pad and second-screen offer now also require no error (08ddc021) |

## I: input (original audit)
| ID | Sev | Where | Problem | Status |
|---|---|---|---|---|
| I1 | H | `emulation/CanvasOnTouchListener.kt` | Pointer id tracking was broken on `android-port`. The dual branch rewrote it (pointer id, CANCEL, MOVE index); **re-verify** | fixed on dual branch (re-verified by review 2026-09-28) |
| I2 | H | `emulation/inputoverlay/*` (Button, DPad, Joystick, InputOverlaySurfaceView) | No `ACTION_CANCEL` handling anywhere, so buttons stick. `resetInput()` is never called on `setInputs` or when hidden | fixed ea67dfae |
| I3 | H | `InputOverlaySurfaceView.kt:~540-560` | Returns false when the first finger misses, so a second finger's button press is lost. Button has no multi-pointer guard | fixed ea67dfae |

## D: introduced by `android-port-dual`
| ID | Sev | Where | Problem | Status |
|---|---|---|---|---|
| D1 | C | `CemuApplication.initializeCemu` → `CemuDataStorage.prepareActiveRoot` | `runBlocking` full SAF save import/export on the main thread in `Application.onCreate`, which ANRs at startup | open |
| D2 | C | `EmulationSessionState.kt`, `CemuSaveSyncManager.kt` | `runBlocking` DataStore reads and SAF save sync on the main thread in onCreate, onDestroy, onQuit and onActivityStopped. ANR, or the copy is killed midway | open |
| D3 | M | `CemuSaveSyncManager.RecursiveSaveObserver` | `observers` map is mutated from FileObserver threads while the main thread iterates it (ConcurrentModificationException) | fixed eae0b51f |
| D4 | C | `EmulationScreen.rememberPadDisplay` | The `Display` object is state and changes on every `onDisplayChanged`, so the Presentation is torn down and rebuilt repeatedly. That triggers A4/A5 | fixed f333fe7a |
| D5 | M | `NativeEmulation.setSwapScreens` | Writes `LatteGPUState.isDRCPrimary` from the UI thread (data race) | fixed f333fe7a |
| D6 | H | `PadPresentation.kt`, `EmulationScreen.kt` | `show()` can throw `InvalidDisplayException`, and display removal isn't handled | fixed f333fe7a |
| D7 | M | `common/emulation/EmulationSessionState.kt` (moved from `emulation/` for ArchUnit), used by `CemuApplication` and `settings/storage/DataStorageSettingsViewModel` | In release/dev builds, emulation runs in `:EmulationProcess`, so in the main process `isEmulationRunning` is always false. The settings screen and the main process's `onActivityStopped` flush can't tell whether emulation is running. Needs cross-process state (e.g. a file lock, a ContentProvider query, or `ActivityManager.getRunningAppProcesses`) | open |

## N: new native findings
| ID | Sev | Where | Problem | Status |
|---|---|---|---|---|
| N1 | C | `EmulationActivity.onQuit` → `exitProcess` → `exit()` | Static destructors run: `~std::thread` on the unjoined `sLatteThread` (`LatteThread.cpp:215`) calls `std::terminate`, and `~VulkanRenderer` (global `g_renderer`) runs while the GPU thread is live. **Crash on every quit** (currently hidden by A2's `_Exit`) | fixed 92ecc623 |
| N2 | C | `cpp/NativeLocalization.cpp:5,35-38` | `unordered_map<string_view,string>` keys point into a loop-local string, so every `_tr()` is UB. `clear()` also races readers | fixed 92ecc623 |
| N3 | C | `NativeInput.cpp:247,256` (throw), `NativeGraphicPacks.cpp:129,146,155,163` and `AndroidInputHelpers.cpp:22` (`.at()`), `NativeEmulation.cpp:~228` initializeEmulation (`fs::create_directories`) | C++ exceptions escape JNI, leading to `std::terminate` | fixed 92ecc623 |
| N4 | C | `VulkanRenderer.cpp:3003-3065`, `SwapchainInfoVk.cpp:309-315` | A failed recreate (surface lost) leaves a null chain and ImGui shut down, and nothing retries: permanent black screen, or pad hang via A5 | fixed f333fe7a |
| N5 | C | `common/settings/Settings.kt` `AppSettingsSerializer` | Decode has no `ignoreUnknownKeys` and a catch-all falls back to defaults. Any schema drift (for example dual vs non-dual builds) silently resets all settings, including the custom storage root, so saves "vanish" | fixed 92ecc623 |
| N6 | H | `SwapchainInfoVk.cpp:43-44` | Creates the new VkSurface before destroying the old one on the same window, which gives `VK_ERROR_NATIVE_WINDOW_IN_USE_KHR` | fixed f333fe7a |
| N7 | C | global `g_renderer` (`Renderer.cpp:13`) | Destroyed on `exit()` while the Latte thread still runs (part of N1) | fixed 92ecc623 |
| N8 | M | `VulkanRenderer.cpp:2050` `ImguiBegin` | Calls `GetChainInfo` before acquire, so it dereferences a null chain | fixed f333fe7a |
| N9 | M | `CafeSystem.cpp:414` | Overwrites `isDRCPrimary` at launch, undoing a swap-screens setting made earlier | fixed f333fe7a |
| N10 | M | `GameTitleLoader.cpp:79` vs `106-138` | The loader thread mutates maps without a lock while the JNI thread clears them, and reads the callback pointer without the mutex (UAF) | fixed eae0b51f |
| N11 | M | `NativeGameTitles.cpp:277` | `metaInfo` isn't null-checked | fixed eae0b51f |
| N12 | M | `NativeSwkbd.cpp:9,45,83-89` | `s_currentInputText` is shared between the PPC and UI threads without a lock | fixed eae0b51f |
| N13 | L | `JNIUtils.h:13-16`, `JNIUtils.cpp:115-121` | A null from `GetStringUTFChars` becomes `std::string(nullptr)`. Modified UTF-8 mangles emoji in paths. `GetEnv` ignores attach failure | fixed 91f156d3 |
| N14 | M | `ExceptionHandler_posix.cpp:81`, `ExceptionHandler.cpp:9-15` | A second crashing thread returns from the handler and re-faults forever. `crashLogCreated` is a plain bool | fixed 92ecc623 |
| N15 | M | `CafeSystem.cpp:465,468` | `sSystemRunning` / `sTitlePaused` are plain bools read across threads | fixed f333fe7a |
| N16 | L | `NativeEmulation.cpp` surface JNI | `VulkanRenderer::GetInstance()` isn't null-checked, and `ANativeWindow_acquire(nullptr)` is possible | fixed f333fe7a |

## K: new Kotlin findings (android-port audit; recheck on the dual branch)
| ID | Sev | Where | Problem | Status |
|---|---|---|---|---|
| K1 | C | `titlemanager/usecases/CompressTitleUseCase.kt:62` | `openFileDescriptor("rw")` on the main thread with no try. Crashes on providers without "rw" support (Drive). Also leaks the fd if the native converter is null (:108) | fixed eae0b51f |
| K2 | C | `EmulationActivity.kt` + manifest `singleTop` | No `onNewIntent`. A shortcut relaunch inside the live `:EmulationProcess` runs prepare/launch on a live emulator | fixed 91f156d3 |
| K3 | C | `EmulationViewModel.kt` initializeRenderer | Renderer and surface init on IO race with the main-thread surface callbacks (Phase 2 design fixes this) | fixed f333fe7a |
| K4 | C | `settings/account/AccountsViewModel.kt:66` | `accounts.first()` on an empty list | fixed eae0b51f |
| K5 | C | `nativeinterface/NativeGameTitles.kt:112-126` | `compareTo` isn't symmetric with null names, so TimSort throws "Comparison method violates…" (32+ games) | fixed eae0b51f |
| K6 | C | `games/details/GameDetailsScreen.kt:122-126` | `LocalDate.of` is called with a native month or day of 0, which throws | fixed eae0b51f |
| K7 | H | `provider/DocumentsProvider.kt:263-266,322-333,61-66` | `resolveWithoutConflict` resolves *inside* the file. Path traversal through `..` plus a prefix-only `isChildDocument` | fixed eae0b51f |
| K8 | C | `nativeinterface/NativeGraphicPacks.kt:82` | A `require` in the setter, called from the UI thread, throws after a preset list refresh | fixed eae0b51f |
| K9 | M | `settings/input/hotkeys/HotkeySettingsScreen.kt:121,138` | A mutable `SnapshotStateSet` is stored in DataStore, which requires immutable data | fixed eae0b51f |
| K10 | H | `common/io/ZipUtils.kt:31-36` | Zip-slip; parent dirs aren't created. Used for driver zips and graphic packs | fixed eae0b51f |
| K11 | M | `graphicpacks/GraphicPacksDownloader.kt:45,93-101` | The HttpClient is never closed, and the whole zip is held in a ByteArray (OOM risk) | fixed eae0b51f (client closed; zip still downloaded into memory, ~20–30 MB, acceptable) |
| K12 | H | main-thread I/O | `MainActivity.kt:70-78` saveSettings on pause; `TitleListViewModel.kt:186,226`; `GraphicPacksViewModel` init; `GamesListScreen.kt:360` calls `titleHasShaderCacheFiles` during composition | deferred (reviewed: small, bounded I/O; revisit if ANRs show up) |
| K13 | M | `titlemanager/usecases/InstallTitleUseCase.kt:95-97,178-193` | Backup and restore run on `viewModelScope` with no foreground service. If the process dies mid-install, the title is left partial or orphaned | fixed 6d877933: `ForegroundTaskService` (dataSync) keeps install/compression alive in the background; the `.installing` marker + `.backup` order makes killed installs recoverable, repaired at app start |
| K14 | M | `titlemanager/TitleListViewModel.kt:229-232` | Compression uses the *filtered* title list, so updates and DLC hidden by the filter are silently dropped from the WUA | fixed c27e208b |
| K15 | M | `settings/account/AccountsViewModel.kt:92-100`, `AccountsScreen.kt:84-88` | Deleting the active account leaves the config pointing at it. Edits are only saved in `onDispose` | fixed c27e208b (next account becomes active; network service refreshed). Edits saved only in onDispose: still open, low risk |
| K16 | M | `EmulationActivity.kt:118` | After recreation, a new `InputDelegateManager` starts with motion off while the UI shows it on | fixed 76d76442 (motion follows the side menu state) |
| K17 | M | `common/settings/Settings.kt` + `InputOverlaySurfaceView.kt:309-322` | Overlay rects are absolute pixels and never clamped, so they break across screen sizes and displays | fixed ef6fc725 (saved rects fitted into the view, default if they don't fit; rebuilt on every size change). Relative coordinates would need the original screen size, which was never stored |
| K18 | M | `GamesListViewModel.kt:41-46`, `TitleListViewModel.kt:99-139` | `_x.value += …` from native threads loses updates | fixed c27e208b |
| K19 | M | `emulation/input/ControllerCallbacks.kt:19-56` | `synchronized` on a field that is reassigned under the lock; the field isn't volatile | fixed ea67dfae |
| K20 | M | `emulation/input/ControllerMotionHandler.kt:81,84` | Keeps a reference to the reused `SensorEvent.values` array | fixed ea67dfae |
| K21 | M | `common/input/GamepadInput.kt:17-33` | Emits recycled `MotionEvent`/`KeyEvent` objects to a later collector, which can bind the wrong key. `hasKeySubscribers` checks the wrong flow | fixed ea67dfae |
| K22 | M | `EmulationScreen.kt:125-127`, `HotkeyManager.kt:13` | Opening the drawer drops KEY_UP, so held buttons stick. `pressedKeys` is never cleared | fixed ea67dfae |
| K23 | M | `emulation/input/DeviceMotionHandler.kt:24` | Rotation is only read in onResume, so a 180° flip inverts the gyro | fixed ea67dfae |
| K24 | L | `emulation/EmulationTextInputDialog.kt:40,73` | The software keyboard dialog has no cancel path | blocked: the core swkbd HLE (`src/Cafe/OS/libs/swkbd/swkbd.cpp` `keyInput`) only handles BACKSPACE and RETURN, so there is no cancel result to send. Needs core work |
| K25 | L | navigation `popBackStack()` everywhere | A double back-press can pop the start destination, leaving a blank screen | fixed c27e208b (`navigateBackSafely`) |
| K26 | M | `AndroidManifest.xml:15-18` | `allowBackup=true` with empty rules; keys, otp and seeprom can be backed up | fixed c27e208b (cloud backup = DataStore + shared prefs only) |
| K27 | L | `InputOverlaySurfaceView.kt:520-534,438-442` | Resize barely responds; `requestFocus()` is called on every layout | fixed ef6fc725 (resize tracks movement across events, per-axis clamp; focus only on layout change) |
| K28 | H | first run, `InputManager::load` + `EmulatedControllerManager` | Fresh install: no controller profile, so emulated controller 1 doesn't exist. Neither a gamepad nor the input overlay does anything until controller 1 is set up by hand | fixed 8e9a5094 (`ControllerAutoMapper`: controller 1 becomes a GamePad when none is configured; a connected gamepad is mapped). Found by reading code, not reproduced on a device |
| K31 | L | `emulation/EmulationScreen.kt` `LinearLayout` | GamePad position Left/Above had no effect: both surfaces fill the row by weight, so the arrangement can't move them, and the TV was always composed first | fixed adf22428 |
| K30 | M | `nativeinterface/NativeFiles.kt` `toNativePath`/`fromNativePath` | SAF native paths decoded `%2F` only after the last `%3A`: games got percent-encoded names from directory listings; a `:` in a name broke `filename()`/`parent_path()`; a `#`/`?` in a name appended by native code became a URI fragment or query | fixed 9e51a7b8 (plain document id after `/document/`, lenient decoder accepts old paths; `SafNativePathTests`) |
| K29 | L | `emulation/EmulationScreen.kt` | The second-screen snackbar and the pad surfaces could appear behind the error dialog after a failed launch (`isEmulationInitialized` also means "finished with error") | fixed 08ddc021 |

## B: build and packaging
| ID | Sev | Where | Problem | Status |
|---|---|---|---|---|
| B1 | H? | `app/build.gradle.kts` `computeCemuDataFilesHash` | Used `File("../../../bin")`, resolved against the Gradle daemon's cwd rather than `app/`, so the hash is likely always "invalid" and the app skips copying `gameProfiles/`. **Confirm on a 0.5.2 install**: does `files/data/gameProfiles` exist? Now uses `project.file()` | fixed 95337964 |
| B2 | L | `src/Cafe/CMakeLists.txt:~660` | pkg-config path was only set for a WIN32 *target*, so building Android from a Windows host fails. Now uses `CMAKE_HOST_WIN32` | fixed 95337964 |
| B3 | L | `.gitattributes` | `text=auto eol=lf` had no pattern, so `gradlew` and `*.sh` were CRLF. Targeted rules added | fixed 95337964 |
| B4 | L | `vcpkg.json` | `hidapi` is listed unconditionally as well as `!android`, so it's built for Android anyway | fixed 95337964 |
| B5 | L | Gradle `-DENABLE_NSYSHID_LIBUSB=OFF` | Referenced by no CMake file (libusb is still required) | fixed 4392ec37 |
| B6 | L | APK packaging | androidx dependencies ship `armeabi-v7a`/`x86` native libs, so the APK looks installable on 32-bit/x86 devices where `libCemuAndroid.so` is missing. Add `ndk { abiFilters += "arm64-v8a" }` to defaultConfig | fixed fe594253 |

## P: performance (not crashes)
| ID | Where | Item | Status |
|---|---|---|---|
| P1 | `SwapchainInfoVk.cpp:437-441` | `preTransform` is forced to IDENTITY, so the compositor rotates every frame. Honor `currentTransform` and rotate in the final blit (developer.android.com/games/optimize/vulkan-prerotation) | open |
| P2 | whole codebase | No ADPF, affinity or priority. Thor runs API 33, so `APerformanceHint` is available; load it at runtime (minSdk 30). TIDs are in `g_schedulerThreadIds` | fixed 76ac98f1 |
| P3 | `Espresso/PPCTimer.cpp:34-83` | 3 s frequency estimate that launch blocks on; use `cntfrq_el0`. `_rdtscFrequency` is a data race. The `_addcarry_u64` shim (`precompiled.h:431`) drops the carry | fixed 76ac98f1 |
| P4 | `BackendAArch64.cpp:1595-1608` | Functions with unsupported IML fall back to the interpreter. Profile per game (upstream work) | open |
| P5 | `snd_core/ax_out.cpp`, `CafeSystem::PauseTitle` | Audio keeps running while paused; no audio focus handling | fixed f333fe7a |
| P6 | `VulkanRenderer.cpp:348-476,538-544` | BotW/RADV `fork()`+`execl` compiled in via `BOOST_OS_LINUX`; unreachable on Adreno/Mali. Guard with `!BOOST_PLAT_ANDROID` | fixed 76ac98f1 |
| P7 | `util/SystemInfo/SystemInfoLinux.cpp:27` | `/proc/stat` is blocked by SELinux, so the overlay's CPU% reads 0 | wontfix: only the per-core breakdown reads /proc/stat; overall CPU% uses times() and works; per-core isn't exposed in the Android UI |
| P8 | `VulkanRenderer.cpp:3037-3039` | One vsync setting drives both swapchains. A FIFO pad chain at 60 Hz can throttle the 120 Hz main chain on Thor | fixed 76ac98f1 |
| P9 | `VulkanPipelineCompiler.cpp:1147-1160` | min(8, cores−1) compile threads at normal priority compete with the PPC and GPU threads | fixed a6d42c74 (all but thread 0 at nice 10, like Windows' BELOW_NORMAL) |
| P10 | `NativeEmulation.cpp:255-259` | `setDPI` gives both canvases the main display's density | fixed fe594253 |
