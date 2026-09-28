package info.cemu.cemu.emulation

import android.view.SurfaceHolder
import androidx.datastore.core.DataStore
import androidx.lifecycle.ViewModel
import androidx.lifecycle.ViewModelProvider
import androidx.lifecycle.viewModelScope
import androidx.lifecycle.viewmodel.CreationExtras
import androidx.lifecycle.viewmodel.initializer
import androidx.lifecycle.viewmodel.viewModelFactory
import info.cemu.cemu.common.either.Either
import info.cemu.cemu.common.either.Error
import info.cemu.cemu.common.either.Success
import info.cemu.cemu.common.either.attemptWithContext
import info.cemu.cemu.common.either.bind
import info.cemu.cemu.common.either.mapError
import info.cemu.cemu.common.settings.AppSettings
import info.cemu.cemu.common.settings.AppSettingsStore
import info.cemu.cemu.common.settings.InputOverlayRect
import info.cemu.cemu.common.settings.InputOverlaySettings
import info.cemu.cemu.common.settings.OverlayInputConfig
import info.cemu.cemu.nativeinterface.NativeEmulation
import info.cemu.cemu.nativeinterface.NativeEmulation.PrepareTitleResult
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch

data class SideMenuState(
    val isMotionEnabled: Boolean = false,
    val isTVReplacedWithPad: Boolean = false,
    val isPadVisible: Boolean = false,
    val isPadOnExternalDisplay: Boolean = false,
    val areScreensSwapped: Boolean = false,
    val isExternalScreenRotatedLeft: Boolean = false,
    val isInputOverlayVisible: Boolean = false,
)

sealed interface NativeError {
    data class SurfaceCreationError(val message: String) : NativeError
    data class RendererInitializationError(val message: String) : NativeError

    object GameFilesNotFoundError : NativeError
    object NoDiscKeysError : NativeError
    object NoTitleTikError : NativeError
    data class UnknownTilePrepareError(val launchPath: String) : NativeError
    data class SystemInitializationError(val message: String) : NativeError

    object LaunchingTitleError : NativeError
}

data class SurfaceDimensions(val width: Int = 1, val height: Int = 1)

class EmulationViewModel(
    private val launchPath: String,
    private val dataStore: DataStore<AppSettings> = AppSettingsStore.dataStore
) : ViewModel() {
    private val _emulationError = MutableStateFlow<NativeError?>(null)
    val emulationError = _emulationError.asStateFlow()

    private val _sideMenuState = MutableStateFlow(SideMenuState())
    val sideMenuState = _sideMenuState.asStateFlow()

    private val _mainSurfaceDimensions = MutableStateFlow(SurfaceDimensions())
    val mainSurfaceDimensions = _mainSurfaceDimensions.asStateFlow()

    private val _padSurfaceDimensions = MutableStateFlow(SurfaceDimensions())
    val padSurfaceDimensions = _padSurfaceDimensions.asStateFlow()

    val isInputOverlayVisible =
        sideMenuState.map { it.isInputOverlayVisible }
            .stateIn(
                viewModelScope,
                SharingStarted.WhileSubscribed(5000),
                false,
            )

    init {
        viewModelScope.launch {
            val settings = dataStore.data.first()
            _sideMenuState.update {
                it.copy(
                    isPadVisible = settings.emulationSettings.isPadVisible,
                    isPadOnExternalDisplay = settings.emulationSettings.isPadOnExternalDisplay,
                    isExternalScreenRotatedLeft = settings.emulationSettings.isExternalScreenRotatedLeft,
                    isInputOverlayVisible = settings.inputOverlaySettings.isOverlayEnabled,
                )
            }
        }
    }

    val inputOverlaySettings = dataStore.data.map { it.inputOverlaySettings }.stateIn(
        viewModelScope,
        SharingStarted.WhileSubscribed(5000),
        InputOverlaySettings(),
    )

    fun saveInputOverlayRectangles(inputOverlayRectMap: Map<OverlayInputConfig, InputOverlayRect>) {
        viewModelScope.launch {
            dataStore.updateData {
                val overlaySettings =
                    it.inputOverlaySettings.copy(inputOverlayRectMap = inputOverlayRectMap)

                it.copy(inputOverlaySettings = overlaySettings)
            }
        }
    }

    fun resetInputOverlayLayout() {
        viewModelScope.launch {
            dataStore.updateData {
                val overlaySettings =
                    it.inputOverlaySettings.copy(inputOverlayRectMap = emptyMap())

                it.copy(inputOverlaySettings = overlaySettings)
            }
        }
    }

    fun updateSideMenuState(sideMenuState: SideMenuState) {
        val oldState = _sideMenuState.value
        _sideMenuState.value = sideMenuState

        if (oldState.isPadVisible != sideMenuState.isPadVisible ||
            oldState.isPadOnExternalDisplay != sideMenuState.isPadOnExternalDisplay ||
            oldState.isExternalScreenRotatedLeft != sideMenuState.isExternalScreenRotatedLeft
        ) {
            viewModelScope.launch {
                dataStore.updateData {
                    it.copy(
                        emulationSettings = it.emulationSettings.copy(
                            isPadVisible = sideMenuState.isPadVisible,
                            isPadOnExternalDisplay = sideMenuState.isPadOnExternalDisplay,
                            isExternalScreenRotatedLeft = sideMenuState.isExternalScreenRotatedLeft,
                        )
                    )
                }
            }
        }
    }

    val gamePadPosition = dataStore.data.map { it.emulationSettings.gamePadPosition }
        .stateIn(
            viewModelScope,
            SharingStarted.WhileSubscribed(5000),
            null,
        )

    // Surface state, only touched on the main thread (SurfaceHolder callbacks and viewModelScope).
    // The native side creates the swapchains on the GPU thread from the published surfaces.
    private var isMainSurfaceAvailable = false
    private var isTitleLaunched = false
    private var isPausedBySurfaceLoss = false

    private fun pauseForSurfaceLoss() {
        if (isTitleLaunched && !isPausedBySurfaceLoss) {
            NativeEmulation.pauseTitle()
            isPausedBySurfaceLoss = true
        }
    }

    private fun resumeAfterSurfaceLoss() {
        if (isPausedBySurfaceLoss) {
            isPausedBySurfaceLoss = false
            NativeEmulation.resumeTitle()
        }
    }

    private fun updateSurfaceDimensions(isMainCanvas: Boolean, width: Int, height: Int) {
        val newDimensions = SurfaceDimensions(
            width = width.coerceAtLeast(1),
            height = height.coerceAtLeast(1),
        )
        if (isMainCanvas) {
            _mainSurfaceDimensions.value = newDimensions
        } else {
            _padSurfaceDimensions.value = newDimensions
        }
    }

    private inner class CanvasSurfaceHolderCallback(val isMainCanvas: Boolean) :
        SurfaceHolder.Callback {

        override fun surfaceCreated(surfaceHolder: SurfaceHolder) {}

        override fun surfaceChanged(
            surfaceHolder: SurfaceHolder,
            format: Int,
            width: Int,
            height: Int,
        ) {
            NativeEmulation.setSurfaceSize(width, height, isMainCanvas)
            updateSurfaceDimensions(isMainCanvas, width, height)
            // also called for size changes of the same surface, the native side ignores those
            NativeEmulation.setSurface(surfaceHolder.surface, isMainCanvas)

            if (isMainCanvas) {
                isMainSurfaceAvailable = true
                resumeAfterSurfaceLoss()
            }
        }

        override fun surfaceDestroyed(surfaceHolder: SurfaceHolder) {
            // false if this surface was already replaced (e.g. the pad moved between inline view and Presentation)
            val wasCurrentSurface = NativeEmulation.clearSurface(surfaceHolder.surface, isMainCanvas)
            if (isMainCanvas && wasCurrentSurface) {
                isMainSurfaceAvailable = false
                // the app went to the background (or the view is recreated): stop emulating until it's back
                pauseForSurfaceLoss()
            }
        }
    }

    val mainHolderCallback: SurfaceHolder.Callback = CanvasSurfaceHolderCallback(true)
    val padHolderCallback: SurfaceHolder.Callback = CanvasSurfaceHolderCallback(false)

    private suspend fun initializeSystems() = attemptWithContext(Dispatchers.IO) {
        NativeEmulation.initializeSystems()
    }.mapError { NativeError.SystemInitializationError(it) }

    private suspend fun initializeRenderer() = attemptWithContext(Dispatchers.IO) {
        NativeEmulation.initializeRenderer()
    }.mapError { NativeError.RendererInitializationError(it) }

    private suspend fun prepareTitle(): Either<Unit, NativeError> =
        attemptWithContext(Dispatchers.IO) { NativeEmulation.prepareTitle(launchPath) }
            .fold(
                onSuccess = { result ->
                    when (result) {
                        PrepareTitleResult.SUCCESSFUL -> Success(Unit)
                        PrepareTitleResult.ERROR_GAME_BASE_FILES_NOT_FOUND -> Error(NativeError.GameFilesNotFoundError)
                        PrepareTitleResult.ERROR_NO_DISC_KEY -> Error(NativeError.NoDiscKeysError)
                        PrepareTitleResult.ERROR_NO_TITLE_TIK -> Error(NativeError.NoTitleTikError)
                        else -> Error(NativeError.UnknownTilePrepareError(launchPath))
                    }
                },
                onError = { Error(NativeError.UnknownTilePrepareError(launchPath)) }
            )

    private suspend fun launchTitle() =
        attemptWithContext(Dispatchers.IO) { NativeEmulation.launchTitle() }
            .mapError { NativeError.LaunchingTitleError }

    private val _isEmulationInitialized = MutableStateFlow(false)
    val isEmulationInitialized = _isEmulationInitialized.asStateFlow()
    private var emulationInitializationJob: Job? = null
    fun initializeEmulation() {
        if (_isEmulationInitialized.value || emulationInitializationJob != null) {
            return
        }

        emulationInitializationJob = viewModelScope.launch {
            prepareTitle()
                .bind { initializeSystems() }
                .bind { initializeRenderer() }
                .bind { launchTitle() }
                .fold(
                    onSuccess = {
                        isTitleLaunched = true
                        // the app may have been sent to the background while loading
                        if (!isMainSurfaceAvailable) {
                            pauseForSurfaceLoss()
                        }
                    },
                    onError = { _emulationError.value = it },
                )

            _isEmulationInitialized.value = true
        }
    }

    companion object {
        val LAUNCH_PATH_KEY = object : CreationExtras.Key<String> {}
        val Factory: ViewModelProvider.Factory = viewModelFactory {
            initializer {
                EmulationViewModel(
                    this[LAUNCH_PATH_KEY] as String
                )
            }
        }
    }
}
