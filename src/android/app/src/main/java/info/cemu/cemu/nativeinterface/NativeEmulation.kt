package info.cemu.cemu.nativeinterface

import android.view.Surface
import androidx.annotation.Keep
import kotlinx.coroutines.channels.BufferOverflow
import kotlinx.coroutines.flow.MutableSharedFlow
import kotlinx.coroutines.flow.asSharedFlow

object NativeEmulation {
    @JvmStatic
    external fun initializeEmulation()

    @JvmStatic
    external fun setDPI(dpi: Float)

    /** Overrides the GamePad DPI when it is shown on another display (setDPI sets both). */
    @JvmStatic
    external fun setPadDPI(dpi: Float)


    /**
     * Publishes the surface of a canvas; the GPU thread creates the swapchain for it. Call from
     * SurfaceHolder.Callback.surfaceChanged (repeated calls for the same surface are ignored).
     */
    @JvmStatic
    external fun setSurface(surface: Surface?, isMainCanvas: Boolean)

    /**
     * Call from SurfaceHolder.Callback.surfaceDestroyed. Waits briefly until the GPU thread stopped
     * using the surface. Returns false if it wasn't the canvas' current surface.
     */
    @JvmStatic
    external fun clearSurface(surface: Surface?, isMainCanvas: Boolean): Boolean

    @JvmStatic
    external fun setSurfaceSize(width: Int, height: Int, isMainCanvas: Boolean)

    @JvmStatic
    external fun setExternalScreenRotatedLeft(rotated: Boolean)

    @JvmStatic
    external fun initializeRenderer()

    object PrepareTitleResult {
        const val SUCCESSFUL: Int = 0
        const val ERROR_GAME_BASE_FILES_NOT_FOUND: Int = 1
        const val ERROR_NO_DISC_KEY: Int = 2
        const val ERROR_NO_TITLE_TIK: Int = 3
        const val ERROR_UNKNOWN: Int = 4
        const val ERROR_INVALID_EXECUTABLE: Int = 5
        const val ERROR_UNABLE_TO_MOUNT: Int = 6
    }

    @JvmStatic
    external fun prepareTitle(launchPath: String?): Int

    @JvmStatic
    external fun launchTitle()

    @JvmStatic
    external fun pauseTitle()

    @JvmStatic
    external fun resumeTitle()

    @JvmStatic
    external fun initializeSystems()

    @JvmStatic
    external fun setReplaceTVWithPadView(swapped: Boolean)

    @JvmStatic
    external fun setSwapScreens(swapped: Boolean)

    @JvmStatic
    external fun supportsLoadingCustomDriver(): Boolean

    /**
     * Probes the system Vulkan driver: {device name, driver version, Vulkan version}, or null. Takes tens of
     * milliseconds, call off the main thread.
     */
    @JvmStatic
    external fun getSystemGpuInfo(): Array<String>?

    /**
     * Asks the renderer to capture the next presented frame, delivered to [screenshots]. Returns false if no game
     * runs or a capture is still pending.
     */
    @JvmStatic
    external fun requestScreenshot(): Boolean

    /** [rgba] holds width * height pixels, 4 bytes each. */
    class Screenshot(val rgba: ByteArray, val width: Int, val height: Int)

    private val _screenshots = MutableSharedFlow<Screenshot>(
        extraBufferCapacity = 1,
        onBufferOverflow = BufferOverflow.DROP_OLDEST,
    )
    val screenshots = _screenshots.asSharedFlow()

    @Keep
    @JvmStatic
    @Suppress("unused")
    private fun onScreenshotCaptured(rgba: ByteArray, width: Int, height: Int) {
        _screenshots.tryEmit(Screenshot(rgba, width, height))
    }

    /** Terminates the emulation process after flushing logs. Never returns. */
    @JvmStatic
    external fun quitProcess()
}
