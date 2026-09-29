package info.cemu.cemu.emulation

import android.content.Context
import android.content.Intent
import android.hardware.display.DisplayManager
import android.os.Bundle
import android.os.PowerManager
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import info.cemu.cemu.BuildConfig
import info.cemu.cemu.common.android.display.DisplayUtils
import info.cemu.cemu.common.android.inputevent.isFromPhysicalController
import info.cemu.cemu.common.emulation.EmulationSessionState
import info.cemu.cemu.common.settings.AppSettingsStore
import info.cemu.cemu.common.storage.SaveSyncCoordinator
import info.cemu.cemu.common.ui.components.ActivityContent
import info.cemu.cemu.common.ui.components.SaveSyncOverlay
import info.cemu.cemu.common.ui.localization.TranslatableContent
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.emulation.input.ControllerCallbacks
import info.cemu.cemu.emulation.input.ControllerMotionHandler
import info.cemu.cemu.emulation.input.DeviceControllerCallbacks
import info.cemu.cemu.emulation.input.DeviceMotionHandler
import info.cemu.cemu.emulation.input.HotkeyManager
import info.cemu.cemu.emulation.input.InputHandler
import info.cemu.cemu.emulation.input.NativeInputDeviceListener
import info.cemu.cemu.nativeinterface.NativeEmulation
import kotlinx.coroutines.flow.distinctUntilChanged
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.launch
import kotlinx.coroutines.withTimeoutOrNull

private class InputDelegateManager(context: Context) {
    private val nativeInputDeviceListener = NativeInputDeviceListener(context)
    private val controllerCallbacks = ControllerCallbacks(context)
    private val controllerMotionHandler = ControllerMotionHandler(context)
    private val deviceControllerCallbacks = DeviceControllerCallbacks(context)
    private val deviceMotionHandler = DeviceMotionHandler(context)

    fun setDeviceMotionEnabled(isListening: Boolean) =
        deviceMotionHandler.setIsListening(isListening)

    fun registerAll() {
        nativeInputDeviceListener.register()
        controllerCallbacks.register()
        controllerMotionHandler.register()
        deviceControllerCallbacks.register()
    }

    fun unregisterAll() {
        nativeInputDeviceListener.unregister()
        controllerCallbacks.unregister()
        controllerMotionHandler.unregister()
        deviceControllerCallbacks.unregister()
    }

    fun onResume(rotation: Int) {
        registerAll()
        deviceMotionHandler.setDeviceRotation(rotation)
        deviceMotionHandler.resumeListening()
    }

    fun onRotationChanged(rotation: Int) {
        deviceMotionHandler.setDeviceRotation(rotation)
    }

    fun onPause() {
        unregisterAll()
        deviceMotionHandler.pauseListening()
    }
}

class EmulationActivity : AppCompatActivity() {
    private lateinit var inputManager: InputDelegateManager
    private lateinit var deviceStatusMonitor: DeviceStatusMonitor
    private var processInputEvents = true

    override fun onGenericMotionEvent(event: MotionEvent): Boolean {
        if (processInputEvents && InputHandler.onMotionEvent(event)) {
            return true
        }

        return super.onGenericMotionEvent(event)
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        HotkeyManager.onKeyEvent(event)

        if (processInputEvents && InputHandler.onKeyEvent(event)) {
            return true
        }

        if (event.keyCode == KeyEvent.KEYCODE_BUTTON_MODE && event.isFromPhysicalController()) {
            return true
        }

        return super.dispatchKeyEvent(event)
    }

    private fun getGamePath(intent: Intent): String? {
        return intent.extras?.getString(EXTRA_LAUNCH_PATH) ?: intent.data?.toString()
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        // the activity is exported (VIEW intents), so a launch without a game is possible
        val requestedGamePath = getGamePath(intent)
        if (requestedGamePath == null) {
            Toast.makeText(this, tr("No game to launch"), Toast.LENGTH_LONG).show()
            finish()
            return
        }
        // a title may still run in this process (see EmulationProcessState); only one title per process
        val runningGamePath = EmulationProcessState.runningGamePath
        if (runningGamePath != null && runningGamePath != requestedGamePath) {
            showGameAlreadyRunningMessage()
        }
        val gamePath = runningGamePath ?: requestedGamePath

        // started from a home screen shortcut without the main process: sync the saves before the game runs.
        // From the game list the main process already did (EXTRA_SAVES_SYNCED)
        val needsStartupSync = !intent.getBooleanExtra(EXTRA_SAVES_SYNCED, false) && runningGamePath == null
        if (needsStartupSync) {
            SaveSyncCoordinator.startStartupSync(this)
        }
        EmulationSessionState.onSessionStarted(this)
        DisplayUtils.init(this)
        inputManager = InputDelegateManager(this)
        deviceStatusMonitor = DeviceStatusMonitor(this)

        setupHotkeys()
        setupSustainedPerformanceMode()

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        setFullscreen()

        setContent {
            TranslatableContent {
                ActivityContent {
                    var isStartupSyncDone by remember { mutableStateOf(!needsStartupSync) }
                    LaunchedEffect(Unit) {
                        SaveSyncCoordinator.awaitStartupSync()
                        isStartupSyncDone = true
                    }
                    Box {
                        if (isStartupSyncDone) {
                            EmulationScreen(
                                gamePath = gamePath,
                                setMotionSensorEnabled = inputManager::setDeviceMotionEnabled,
                                onQuit = ::onQuit,
                                setInputListeningEnabled = { enabled ->
                                    processInputEvents = enabled
                                    if (!enabled) {
                                        // key-ups aren't forwarded anymore, don't leave buttons held
                                        InputHandler.releaseAll()
                                    }
                                },
                            )
                        }
                        // the startup sync and the export when quitting
                        SaveSyncOverlay()
                    }
                }
            }
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        // singleTop: a launch while this activity is on top. The running title can't be replaced in-process
        val newGamePath = getGamePath(intent)
        if (newGamePath != null && newGamePath != EmulationProcessState.runningGamePath) {
            showGameAlreadyRunningMessage()
        }
    }

    private fun showGameAlreadyRunningMessage() {
        Toast.makeText(
            this,
            tr("Another game is already running. Quit it from the menu first."),
            Toast.LENGTH_LONG
        ).show()
    }

    override fun onPause() {
        super.onPause()

        inputManager.onPause()
        deviceStatusMonitor.unregister()
        getSystemService(DisplayManager::class.java).unregisterDisplayListener(rotationListener)
        // key-up events of buttons held now may go elsewhere
        InputHandler.releaseAll()
        HotkeyManager.reset()
    }

    override fun onResume() {
        super.onResume()

        inputManager.onResume(display.rotation)
        deviceStatusMonitor.register()
        getSystemService(DisplayManager::class.java).registerDisplayListener(rotationListener, null)
    }

    // flipping between landscape and reverse landscape doesn't cause a configuration change, but the motion
    // sensor axes have to follow the rotation
    private val rotationListener = object : DisplayManager.DisplayListener {
        override fun onDisplayChanged(displayId: Int) {
            val display = display ?: return
            if (displayId == display.displayId) {
                inputManager.onRotationChanged(display.rotation)
            }
        }

        override fun onDisplayAdded(displayId: Int) {}
        override fun onDisplayRemoved(displayId: Int) {}
    }

    override fun onDestroy() {
        if (::inputManager.isInitialized) {
            EmulationSessionState.onSessionStopped(this)
        }
        super.onDestroy()
    }

    private fun setupHotkeys() {
        lifecycleScope.launch {
            repeatOnLifecycle(Lifecycle.State.STARTED) {
                AppSettingsStore.dataStore.data.map { it.hotkeySettings }
                    .distinctUntilChanged()
                    .collect { HotkeyManager.setHotkeyMappings(it) }
            }
        }
    }

    private fun setupSustainedPerformanceMode() {
        val isSupported =
            getSystemService(PowerManager::class.java)?.isSustainedPerformanceModeSupported == true
        if (!isSupported) {
            return
        }
        lifecycleScope.launch {
            AppSettingsStore.dataStore.data.map { it.emulationSettings.isSustainedPerformanceModeEnabled }
                .distinctUntilChanged()
                .collect { window.setSustainedPerformanceMode(it) }
        }
    }

    private fun setFullscreen() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val controller = WindowInsetsControllerCompat(window, window.decorView)
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        controller.hide(WindowInsetsCompat.Type.systemBars())
    }

    private var isQuitting = false

    private fun onQuit() {
        if (isQuitting) {
            return
        }
        isQuitting = true
        // no more save writes during the export
        NativeEmulation.pauseTitle()
        lifecycleScope.launch {
            // off the main thread with the "Saving…" overlay (D2). If it takes too long the mirror stays dirty and
            // the next start exports it, nothing is lost
            withTimeoutOrNull(QUIT_SAVE_SYNC_TIMEOUT_MS) {
                EmulationSessionState.finishSession(this@EmulationActivity)
            }
            finish()
            // not exitProcess(): exit() runs native static destructors while emulation threads still run (crash)
            NativeEmulation.quitProcess()
        }
    }

    companion object {
        const val EXTRA_LAUNCH_PATH: String = BuildConfig.APPLICATION_ID + ".LaunchPath"
        const val EXTRA_SAVES_SYNCED: String = BuildConfig.APPLICATION_ID + ".SavesSynced"
        private const val QUIT_SAVE_SYNC_TIMEOUT_MS = 60_000L
    }
}
