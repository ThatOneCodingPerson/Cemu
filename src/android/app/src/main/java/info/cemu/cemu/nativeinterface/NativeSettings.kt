package info.cemu.cemu.nativeinterface

object NativeSettings {
    @JvmStatic
    external fun saveSettings()

    @JvmStatic
    external fun addGamesPath(uri: String?)

    @JvmStatic
    external fun removeGamesPath(uri: String?)

    @JvmStatic
    external fun getGamesPaths(): Array<String>

    @JvmStatic
    external fun getAsyncShaderCompile(): Boolean

    @JvmStatic
    external fun setAsyncShaderCompile(value: Boolean)

    object VSyncMode {
        const val OFF: Int = 0
        const val DOUBLE_BUFFERING: Int = 1
        const val TRIPLE_BUFFERING: Int = 2
    }

    @JvmStatic
    external fun getVsyncMode(): Int

    @JvmStatic
    external fun setVsyncMode(value: Int)

    object FullscreenScaling {
        const val KEEP_ASPECT_RATIO: Int = 0
        const val STRETCH: Int = 1
    }

    @JvmStatic
    external fun getFullscreenScaling(): Int

    @JvmStatic
    external fun setFullscreenScaling(value: Int)

    object ScalingFilter {
        const val BILINEAR_FILTER: Int = 0
        const val BICUBIC_FILTER: Int = 1
        const val BICUBIC_HERMITE_FILTER: Int = 2
        const val NEAREST_NEIGHBOR_FILTER: Int = 3

        /** AMD FidelityFX FSR 1 (EASU). Upscaling only. */
        const val FSR_EASU_FILTER: Int = 4
    }

    @JvmStatic
    external fun getUpscalingFilter(): Int

    @JvmStatic
    external fun setUpscalingFilter(value: Int)

    @JvmStatic
    external fun getDownscalingFilter(): Int

    @JvmStatic
    external fun setDownscalingFilter(value: Int)

    @JvmStatic
    external fun getAccurateBarriers(): Boolean

    @JvmStatic
    external fun setAccurateBarriers(value: Boolean)

    /** Vulkan pre-rotation: the swapchain takes the display's rotation, so the compositor doesn't rotate each frame. */
    @JvmStatic
    external fun getVulkanPreRotation(): Boolean

    @JvmStatic
    external fun setVulkanPreRotation(value: Boolean)

    /** Minimum anisotropic filtering for textures the game filters linearly. */
    object AnisotropicFilter {
        const val GAME_DEFAULT: Int = 0
        const val X2: Int = 1
        const val X4: Int = 2
        const val X8: Int = 3
        const val X16: Int = 4
    }

    @JvmStatic
    external fun getAnisotropicFilter(): Int

    @JvmStatic
    external fun setAnisotropicFilter(value: Int)

    /**
     * Frame generation with the shaders of the user's own Lossless Scaling ([installLosslessDll]).
     * Also applies to a running game.
     */
    @JvmStatic
    external fun isFrameGenerationEnabled(): Boolean

    @JvmStatic
    external fun setFrameGenerationEnabled(value: Boolean)

    /** Shown frames per rendered frame, 2 to 4. Unused while a target rate is set. */
    @JvmStatic
    external fun getFrameGenerationMultiplier(): Int

    @JvmStatic
    external fun setFrameGenerationMultiplier(value: Int)

    /** Frames per second to reach by generating as many frames as needed, 0 to use the multiplier. */
    @JvmStatic
    external fun getFrameGenerationTargetRate(): Int

    @JvmStatic
    external fun setFrameGenerationTargetRate(value: Int)

    const val FRAME_GENERATION_FLOW_SCALE_AUTO: Int = 0

    /**
     * Resolution of the motion estimation in percent, 25 to 100.
     * [FRAME_GENERATION_FLOW_SCALE_AUTO] picks it from the game's resolution.
     */
    @JvmStatic
    external fun getFrameGenerationFlowScale(): Int

    @JvmStatic
    external fun setFrameGenerationFlowScale(value: Int)

    /** Results of [getLosslessDllStatus] and [installLosslessDll] (FrameGen::LosslessStatus). */
    object LosslessDllStatus {
        const val OK: Int = 0
        const val NOT_INSTALLED: Int = 1
        const val UNREADABLE_FILE: Int = 2
        const val NOT_PORTABLE_EXECUTABLE: Int = 3
        const val MISSING_SHADERS: Int = 4
    }

    /** Reads and checks the whole file, [isLosslessDllInstalled] only looks whether it's there. */
    @JvmStatic
    external fun getLosslessDllStatus(): Int

    @JvmStatic
    external fun isLosslessDllInstalled(): Boolean

    /** Checks the Lossless.dll at [path] and copies it into Cemu's data folder. */
    @JvmStatic
    external fun installLosslessDll(path: String): Int

    @JvmStatic
    external fun removeLosslessDll(): Boolean

    /** Ignore the gamma adjustment a game requests (GX2SetTVGamma). */
    @JvmStatic
    external fun isOverrideGameGammaEnabled(): Boolean

    @JvmStatic
    external fun setOverrideGameGammaEnabled(value: Boolean)

    const val GAMMA_MIN: Float = 1.0f
    const val GAMMA_MAX: Float = 4.0f
    const val DEFAULT_GAMMA: Float = 2.2f

    /** The TV gamma to reproduce. Higher is darker. */
    @JvmStatic
    external fun getTargetGamma(): Float

    @JvmStatic
    external fun setTargetGamma(value: Float)

    /** The gamma of this device's screen; [DISPLAY_GAMMA_SRGB] for the piecewise sRGB curve. */
    @JvmStatic
    external fun getDisplayGamma(): Float

    @JvmStatic
    external fun setDisplayGamma(value: Float)

    const val DISPLAY_GAMMA_SRGB: Float = 0.0f

    @JvmStatic
    external fun getAudioDeviceEnabled(tv: Boolean): Boolean

    @JvmStatic
    external fun setAudioDeviceEnabled(enabled: Boolean, tv: Boolean)

    object AudioChannels {
        const val MONO: Int = 0
        const val STEREO: Int = 1
        const val SURROUND: Int = 2
    }

    @JvmStatic
    external fun setAudioDeviceChannels(channels: Int, tv: Boolean)

    @JvmStatic
    external fun getAudioDeviceChannels(tv: Boolean): Int

    const val AUDIO_MIN_VOLUME: Int = 0
    const val AUDIO_MAX_VOLUME: Int = 100

    @JvmStatic
    external fun setAudioDeviceVolume(volume: Int, tv: Boolean)

    @JvmStatic
    external fun getAudioDeviceVolume(tv: Boolean): Int

    const val AUDIO_LATENCY_MS_MAX: Int = 276

    @JvmStatic
    external fun getAudioLatency(): Int

    @JvmStatic
    external fun setAudioLatency(value: Int)

    object OverlayScreenPosition {
        const val DISABLED: Int = 0
        const val TOP_LEFT: Int = 1
        const val TOP_CENTER: Int = 2
        const val TOP_RIGHT: Int = 3
        const val BOTTOM_LEFT: Int = 4
        const val BOTTOM_CENTER: Int = 5
        const val BOTTOM_RIGHT: Int = 6
    }

    @JvmStatic
    external fun getOverlayPosition(): Int

    @JvmStatic
    external fun setOverlayPosition(value: Int)

    const val OVERLAY_TEXT_SCALE_MIN: Int = 50
    const val OVERLAY_TEXT_SCALE_MAX: Int = 300

    @JvmStatic
    external fun getOverlayTextScalePercentage(): Int

    @JvmStatic
    external fun setOverlayTextScalePercentage(value: Int)

    @JvmStatic
    external fun isOverlayFPSEnabled(): Boolean

    @JvmStatic
    external fun setOverlayFPSEnabled(value: Boolean)

    @JvmStatic
    external fun isOverlayDrawCallsPerFrameEnabled(): Boolean

    @JvmStatic
    external fun setOverlayDrawCallsPerFrameEnabled(value: Boolean)

    @JvmStatic
    external fun isOverlayCPUUsageEnabled(): Boolean

    @JvmStatic
    external fun setOverlayCPUUsageEnabled(value: Boolean)

    @JvmStatic
    external fun isOverlayRAMUsageEnabled(): Boolean

    @JvmStatic
    external fun setOverlayRAMUsageEnabled(value: Boolean)

    @JvmStatic
    external fun isOverlayDebugEnabled(): Boolean

    @JvmStatic
    external fun setOverlayDebugEnabled(value: Boolean)

    @JvmStatic
    external fun isOverlayBatteryEnabled(): Boolean

    @JvmStatic
    external fun setOverlayBatteryEnabled(value: Boolean)

    @JvmStatic
    external fun isOverlayThermalEnabled(): Boolean

    @JvmStatic
    external fun setOverlayThermalEnabled(value: Boolean)

    @JvmStatic
    external fun isOverlayFrameTimeEnabled(): Boolean

    @JvmStatic
    external fun setOverlayFrameTimeEnabled(value: Boolean)

    @JvmStatic
    external fun getNotificationsPosition(): Int

    @JvmStatic
    external fun setNotificationsPosition(value: Int)

    @JvmStatic
    external fun getNotificationsTextScalePercentage(): Int

    @JvmStatic
    external fun setNotificationsTextScalePercentage(value: Int)

    @JvmStatic
    external fun isNotificationControllerProfilesEnabled(): Boolean

    @JvmStatic
    external fun setNotificationControllerProfilesEnabled(value: Boolean)

    @JvmStatic
    external fun isNotificationShaderCompilerEnabled(): Boolean

    @JvmStatic
    external fun setNotificationShaderCompilerEnabled(value: Boolean)

    @JvmStatic
    external fun isNotificationFriendListEnabled(): Boolean

    @JvmStatic
    external fun setNotificationFriendListEnabled(value: Boolean)

    object ConsoleLanguage {
        const val JAPANESE: Int = 0
        const val ENGLISH: Int = 1
        const val FRENCH: Int = 2
        const val GERMAN: Int = 3
        const val ITALIAN: Int = 4
        const val SPANISH: Int = 5
        const val CHINESE: Int = 6
        const val KOREAN: Int = 7
        const val DUTCH: Int = 8
        const val PORTUGUESE: Int = 9
        const val RUSSIAN: Int = 10
        const val TAIWANESE: Int = 11
    }

    @JvmStatic
    external fun getConsoleLanguage(): Int

    @JvmStatic
    external fun setConsoleLanguage(value: Int)

    /**
     * @return the selected driver directory path. If it's null, then the default system driver will be used.
     */
    @JvmStatic
    external fun getCustomDriverPath(): String?

    /**
     * Sets the selected driver directory [path]. To use the default system driver, pass a null [path].
     */
    @JvmStatic
    external fun setCustomDriverPath(path: String?)

    object NetworkService {
        const val OFFLINE = 0
        const val NINTENDO = 1
        const val PRETENDO = 2
        const val CUSTOM = 3
    }

    @JvmStatic
    external fun getAccountNetworkService(persistentId: Int): Int


    @JvmStatic
    external fun setAccountNetworkService(persistentId: Int, networkService: Int)


    @JvmStatic
    external fun getAccountPersistentId(): Int

    @JvmStatic
    external fun setAccountPersistentId(persistentId: Int)

    @JvmStatic
    external fun isEmulateSkylanderPortalEnabled(): Boolean

    @JvmStatic
    external fun setEmulateSkylanderPortalEnabled(enabled: Boolean)

    @JvmStatic
    external fun isEmulateInfinityBaseEnabled(): Boolean

    @JvmStatic
    external fun setEmulateInfinityBaseEnabled(enabled: Boolean)

    @JvmStatic
    external fun isEmulateDimensionsToypadEnabled(): Boolean

    @JvmStatic
    external fun setEmulateDimensionsToypadEnabled(enabled: Boolean)

    @JvmStatic
    external fun hasCustomNetworkConfiguration(): Boolean
}
