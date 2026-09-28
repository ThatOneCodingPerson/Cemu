package info.cemu.cemu.emulation

import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.WindowManager
import android.widget.Toast
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
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
import info.cemu.cemu.common.ui.components.ActivityContent
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

    fun onPause() {
        unregisterAll()
        deviceMotionHandler.pauseListening()
    }
}

class EmulationActivity : AppCompatActivity() {
    private lateinit var inputManager: InputDelegateManager
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

        EmulationSessionState.onSessionStarted(this)
        DisplayUtils.init(this)
        inputManager = InputDelegateManager(this)

        setupHotkeys()

        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)

        setFullscreen()

        setContent {
            TranslatableContent {
                ActivityContent {
                    EmulationScreen(
                        gamePath = gamePath,
                        setMotionSensorEnabled = inputManager::setDeviceMotionEnabled,
                        onQuit = ::onQuit,
                        setInputListeningEnabled = { processInputEvents = it },
                    )
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
    }

    override fun onResume() {
        super.onResume()

        inputManager.onResume(display.rotation)
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

    private fun setFullscreen() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        val controller = WindowInsetsControllerCompat(window, window.decorView)
        controller.systemBarsBehavior =
            WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        controller.hide(WindowInsetsCompat.Type.systemBars())
    }

    private fun onQuit() {
        EmulationSessionState.syncSavesToCustomRoot(this)
        finish()
        // not exitProcess(): exit() runs native static destructors while emulation threads still run (crash)
        NativeEmulation.quitProcess()
    }

    companion object {
        const val EXTRA_LAUNCH_PATH: String = BuildConfig.APPLICATION_ID + ".LaunchPath"
    }
}
