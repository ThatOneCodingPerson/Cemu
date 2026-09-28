# Phase 2 design: surface/swapchain lifecycle, error dialog, crash handler

Design review written 2026-09-28 against `origin/android-port-dual` (564bfdad). The line numbers are from that commit, so re-check them before editing. BUGS.md tracks what this fixes (A3–A7, D4–D6, N1, N4, N6–N9).

**Core rule:** the UI thread only *publishes* window state. Only the Latte (GPU) thread creates or destroys `SwapchainInfoVk`, `VkSurfaceKHR` and swapchains.

## 0. Defects confirmed while designing (new IDs are in BUGS.md)
- Per VK_KHR_android_surface, a VkSurface holds its own reference to the window. `setSurface` therefore leaks one reference per call rather than causing a use-after-free, and `m_currentWindow` is a non-owning pointer used only for comparison.
- **N6:** `RecreateSurface` (SwapchainInfoVk.cpp:43-44) creates the new VkSurface before destroying the old one. Android allows only one VkSurface per ANativeWindow, so this returns `VK_ERROR_NATIVE_WINDOW_IN_USE_KHR`.
- **N7:** `g_renderer` is a global (Renderer.cpp:13). `exitProcess(0)` runs `~VulkanRenderer` on the main thread while the Latte thread is still live. Today the crash handler's `_Exit` hides this, so it must be fixed before the crash handler starts re-raising signals.
- **N8:** `ImguiBegin` calls `GetChainInfo` (VulkanRenderer.cpp:2050) before `AcquireNextSwapchainImage` (2052), which dereferences a null chain.
- **N9:** CafeSystem.cpp:414 overwrites `isDRCPrimary` at launch, which undoes a swap-screens setting made before launch.

## 1. Shared state (`src/gui/interface/WindowSystem.h`, under `#if BOOST_PLAT_ANDROID`)
```cpp
struct AndroidCanvasInfo
{
	std::mutex mutex;
	std::condition_variable cv;
	void* window = nullptr;              // ANativeWindow*, holds ONE ref; guarded by mutex
	std::atomic_uint64_t generation = 0; // ++ under mutex on every change
	uint64 appliedGeneration = 0;        // guarded; written by Latte thread
	bool consumerActive = false;         // guarded; Latte thread running & applying
	bool consumerUsesWindow = false;     // guarded; Latte holds ref/VkSurface for this canvas
};
```
`WindowInfo` gets `AndroidCanvasInfo android_canvas_main, android_canvas_pad;`.

## 2. UI thread (`NativeEmulation.cpp`)
**`setSurface(surface, isMain)`:**
1. `w = ANativeWindow_fromSurface` (already +1, so don't call `ANativeWindow_acquire`).
2. Lock. If `w == window`, unlock, release `w`, and return (duplicate callback).
3. Otherwise set `old = window; window = w; ++generation`. Unlock and release `old`.
4. For the pad canvas, set `pad_open = true`. This call never waits.

**`jboolean clearSurface(surface, isMain)`** (replaces `clearPadSurface`):
1. `w = fromSurface`, then lock. Set `wasCurrent = (w == window)`.
2. If `wasCurrent`: `old = window; window = nullptr; ++generation`, and for the pad set `pad_open = false`.
3. Wait on the cv for up to **500 ms** until `!consumerActive || !consumerUsesWindow || appliedGeneration >= target`. Log if it times out.
4. Unlock, release `old` and `w`, and return `wasCurrent`.

**Deleted:** `initializeSurface` JNI, the extra acquire in `TestSurface`, and the `isDRCPrimary` write in `setSwapScreens`.

**Added:** a `quitProcess` JNI that runs `cemuLog_waitForFlush(); fflush(nullptr); _exit(0);`.

**No deadlock is possible:**
- The Latte thread never waits on the UI thread.
- If the UI wait times out, the Latte thread's own references keep the window memory valid, and presenting into an abandoned window returns OUT_OF_DATE or SURFACE_LOST, which is already handled.
- If the Latte thread isn't running, `consumerActive` is false and the UI thread doesn't wait.

## 3. Latte thread
**SwapchainInfoVk:**
- Android constructor `SwapchainInfoVk(bool mainWindow, Vector2i size, ANativeWindow* window)`: create the VkSurface, then acquire our own reference into `m_window`.
- Destructor: release `m_window` after `vkDestroySurfaceKHR`.
- `RecreateSurface`: no `!mainWindow` early-out and no `surface.wait`. Destroy the old surface first, then recreate from `m_window`.

**VulkanRenderer:**
- New members:
  - `SyncCanvasWindow(bool mainWindow, bool allowAbandonFrame)`
  - `SetCanvasConsumerActive(bool)`
  - `uint64 m_canvasAppliedGeneration[2]`, `bool m_canvasHasWindow[2]`, `HRTick m_canvasRetryTick[2]`
- `InitializeSurface`, `StopUsingPadAndWait` and `m_destroyPadSwapchainNextAcquire` move under `#if !BOOST_PLAT_ANDROID`.

**`SyncCanvasWindow` (Latte thread only):**
1. Decide whether there's work.
   - There is work if the generation changed, or if a window exists, the chain is invalid and the retry tick has passed. Otherwise return.
   - If `!allowAbandonFrame` and an image is already acquired, return.
2. Under the lock, take `w` and the generation. If `w` is set, acquire a ref and set `consumerUsesWindow = true`.
3. `draw_endRenderPass(); SubmitCommandBuffer(); WaitDeviceIdle(); chain.reset();`
4. If `w` is set, build and `Create()` a new chain inside try/catch.
   - On failure, log, reset the chain, and set the retry tick to now + 250 ms.
   - For the main canvas, run `ImGui_ImplVulkan_Shutdown(); ImguiInit();`.
   - Release the temporary ref.
5. Record the applied generation and `hasWindow`. Under the lock, set `appliedGeneration` and `consumerUsesWindow = (chain != nullptr)`, then `notify_all`.

Only the latest state is applied, so rapid toggles collapse into one rebuild.

**Call sites** (points where no `chainInfo&` is held):
- Start of `AcquireNextSwapchainImage`: `SyncCanvasWindow(mainWindow, false)`, then `if (CafeSystem::IsTitlePaused()) return false;`.
- `NotifyLatteCommandProcessorIdle`: both canvases with `allowAbandonFrame = true`. The Latte thread spins in this loop while paused (LatteCommandProcessor.cpp:152).
- `Initialize` (after `ImguiInit`): activate the consumer and sync both canvases. The first main chain is now created on the Latte thread, and the IO thread's `initializeSurface(true)` is removed.
- `Shutdown`: deactivate the consumer, clear `consumerUsesWindow`, notify.
- Not covered: the GX2Init wait loop (LatteThread.cpp:202) and WAIT_MEM stalls. There, a destroy falls back to the 500 ms timeout.

**ImGui:**
- In `ImguiInit`, fall back to `VK_FORMAT_R8G8B8A8_UNORM` with 2 images when there's no main chain, so the backend is always initialized.
- `RecreateSwapchain`: catch around `Create()`; for main, `ImguiInit()` and then rethrow.
- `ImguiBegin`: acquire first, then `GetChainInfo`.
- `UpdateSwapchainProperties`: ignore a 0×0 size on Android.

## 4. Pause and audio
- `CafeSystem`: make `sTitlePaused` a `std::atomic_bool`, and add `IsTitlePaused()`.
- `PauseTitle`: after suspending threads, call `snd_core::AXOut_SetPaused(true)`.
- `ResumeTitle`: call `AXOut_SetPaused(false)` first, then resume.
- `ax_out.cpp` gets an `std::atomic_bool s_outputPaused`:
  - `AXOut_SetPaused` takes a unique lock on `g_audioMutex`, sets the flag and stops the TV, pad and portal devices.
  - `AXOut_updateDevicePlayState(true)` returns early while paused (checked under the shared lock).
  - `AXOut_update` returns early while paused. It keeps running from the core idle loop while PPC threads are suspended (coreinit_Thread.cpp:1276).

## 5. Swap screens (D5)
In `LatteRenderTarget.cpp` (~1005), before `showDRC` is computed, on the Latte thread:
`static bool s_swapScreensLast; if (swapScreens != s_swapScreensLast) { LatteGPUState.isDRCPrimary = swapScreens; s_swapScreensLast = swapScreens; }`

## 6. Kotlin
**EmulationViewModel:** remove the ConditionFlags logic.
- `surfaceChanged`: `setSurfaceSize`, then `setSurface`. If this is main and we paused because the surface was lost, `resumeTitle`.
- `surfaceDestroyed`: if `clearSurface(...)` returns true for main, `pauseTitle` and remember that we paused.
- `initializeEmulation` waits for a `mainSurfaceReady` flow before `launchTitle`.

**EmulationScreen:** `rememberPadDisplay` tracks a display id (`Int?`) and updates only when the id changes. Key the `DisposableEffect` on that id, not on the `Display` object (D4).

**EmulationActivity.onQuit:** call `quitProcess()`, not `exitProcess(0)`.

## A. Error dialog (A1)
**Native `ShowErrorDialog`:**
- Log, write `last_error.txt` (inside try/catch), and flush the log.
- Call the Kotlin `NativeEmulation.showErrorDialog(title, msg, id)`. Cache the `Scopedjclass` and method id on a Java thread during `initializeEmulation`, because FindClass fails on native threads.

**Kotlin side:**
- Post to the main looper and show an AlertDialog on the resumed activity, tracked via `ActivityLifecycleCallbacks`.
- If there's no resumed activity, show a Toast and ack immediately.
- Dismissing the dialog calls `onErrorDialogClosed(id)`.

**Blocking:** off the main thread (`gettid() != getpid()`), wait up to 60 s for the ack. On the main thread, never wait.

## B. Crash handler (A2)
**Signals handled on Android:** SEGV, BUS, FPE, ILL, ABRT only. Install with `SA_SIGINFO|SA_ONSTACK` and a full mask, and save the previous `sigaction` per signal.
- Don't install SIGQUIT, because ART uses it for ANR traces.
- Leave SIGINT, SIGTERM and SIGTRAP alone.

**Handler body:**
1. An `atomic_flag` guards against re-entry.
2. `open`/`write`/`close` a crash file whose path was precomputed at init. Write the signal, code, fault address and tid with hand-rolled integer formatting.
3. Restore the previous `sigaction`.
4. Re-queue the signal with `syscall(__NR_rt_tgsigqueueinfo, getpid(), gettid(), sig, info)` and return, so debuggerd writes a tombstone.

On Android there is no `boost::stacktrace` and no `_Exit`. Bionic already gives every pthread an alternate signal stack, so SA_ONSTACK is enough. **This depends on `quitProcess` (N7) landing first.**

## C. Latte thread guard (A3)
On Android, start the thread with `Latte_ThreadEntryGuarded`:
`try { Latte_ThreadEntry(); } catch (const std::exception& ex) { log; ShowErrorDialog; cemuLog_waitForFlush(); _exit(1); }`
Catch only `std::exception`, so the forced unwind from `pthread_exit` isn't swallowed.

## Edge cases
- **Rotation:** same window, new size. `UpdateSwapchainProperties` rebuilds the swapchain; `setSurface` ignores the duplicate callback.
- **Backgrounded during loading:** launch waits for the main surface. After launch, the shader-cache loop's `BeginFrame` applies the change.
- **Pad toggled rapidly:** the toggles collapse into one rebuild.
- **Display removed:** the Presentation auto-dismisses, and the pad is cleared normally.
- **Inline pad ↔ Presentation switch:** the `wasCurrent` check handles either callback order.
- **Quit while paused:** `_exit` avoids global destructors.
- **Still open:** pausing before the PPC threads exist doesn't suspend them (audio and presents still stop). `ResumeActiveThreads` resumes threads it never suspended.

## Manual test matrix (phone, then Thor)
| Scenario | Expected |
|---|---|
| Cold boot, FPS overlay on | Renders; ImGui visible |
| Home during loading/shader cache, then back | No background audio/CPU; renders on return |
| Home in-game, then back | Audio stops ~100 ms; no timeout in log; resumes |
| Split-screen resize; 180° flip | No black screen; no rebuild loop |
| Inline pad toggled 20× fast | No ANR/crash |
| Thor: external pad on/off fast, rotate-left, swap screens | Bottom screen correct; overlays OK |
| Thor bottom screen off/on; phone unplug DP | Pad disappears or falls back inline; no crash |
| Home with pad on other display | Presentation stays; frames stop; resume works |
| Exit in-game / after resume | No tombstone |
| Damaged RPX / bad graphic pack | Dialog blocks until dismissed; `last_error.txt` written |
| Debug `raise(SIGSEGV)` on Latte/PPC thread | Tombstone + crash file, no hang |
| `kill -3 <pid>` | Survives (ANR dump) |
| Debug throw on Latte thread | Dialog, then exit |
| Desktop builds | Compile unchanged |
