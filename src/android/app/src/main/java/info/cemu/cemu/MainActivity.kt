package info.cemu.cemu

import android.app.PendingIntent
import android.content.Context
import android.content.Intent
import android.content.pm.ShortcutInfo
import android.content.pm.ShortcutManager
import android.graphics.drawable.Icon
import android.hardware.input.InputManager
import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import android.widget.Toast
import androidx.activity.compose.setContent
import androidx.appcompat.app.AppCompatActivity
import androidx.compose.animation.EnterTransition
import androidx.compose.animation.ExitTransition
import androidx.compose.foundation.layout.Box
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.lifecycleScope
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.rememberNavController
import info.cemu.cemu.about.AboutCemuRoute
import info.cemu.cemu.about.aboutCemuNavigation
import info.cemu.cemu.common.input.ControllerAutoMapper
import info.cemu.cemu.common.input.GamepadInputSource
import info.cemu.cemu.common.input.InputDeviceListener
import info.cemu.cemu.common.settings.AppSettingsStore
import info.cemu.cemu.common.storage.SaveSyncCoordinator
import info.cemu.cemu.common.ui.components.SaveSyncOverlay
import info.cemu.cemu.common.ui.components.ActivityContent
import info.cemu.cemu.common.ui.localization.TranslatableContent
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.emulation.EmulationActivity
import info.cemu.cemu.games.GameListRoute
import info.cemu.cemu.games.gamesNavigation
import info.cemu.cemu.graphicpacks.GraphicPacksForTitleRoute
import info.cemu.cemu.graphicpacks.GraphicPacksRoute
import info.cemu.cemu.graphicpacks.graphicPacksNavigation
import info.cemu.cemu.nativeinterface.NativeGameTitles.Game
import info.cemu.cemu.nativeinterface.NativeActiveSettings
import info.cemu.cemu.nativeinterface.NativeErrors
import info.cemu.cemu.nativeinterface.NativeGameTitles
import info.cemu.cemu.nativeinterface.NativeSettings
import info.cemu.cemu.settings.SettingsRoute
import info.cemu.cemu.settings.settingsNavigation
import info.cemu.cemu.titlemanager.TitleManagerRoute
import info.cemu.cemu.titlemanager.titleManagerNavigation
import info.cemu.cemu.titlemanager.usecases.InstallTitleUseCase
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlin.io.path.Path

class MainActivity : AppCompatActivity() {
    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        GamepadInputSource.emitMotion(event)

        if (GamepadInputSource.hasMotionSubscribers) {
            return true
        }

        return super.dispatchGenericMotionEvent(event)
    }

    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        GamepadInputSource.emitKey(event)

        if (GamepadInputSource.hasKeySubscribers) {
            return true
        }

        return super.dispatchKeyEvent(event)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        // once per process; SaveSyncOverlay holds the UI (and game launches) back while it runs
        SaveSyncCoordinator.startStartupSync(this)
        // a title install killed with the process leaves a partial title (and the old one as backup)
        lifecycleScope.launch(Dispatchers.IO) {
            if (InstallTitleUseCase.recoverInterruptedInstalls(Path(NativeActiveSettings.getMLCPath())))
                NativeGameTitles.refreshCafeTitleList()
        }
        setContent {
            TranslatableContent {
                ActivityContent {
                    Box {
                        MainNav()
                        SaveSyncOverlay()
                    }
                }
            }
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        NativeSettings.saveSettings()
    }

    override fun onPause() {
        super.onPause()
        NativeSettings.saveSettings()
        inputManager?.unregisterInputDeviceListener(controllerListener)
    }

    override fun onResume() {
        super.onResume()
        // the emulator may have terminated because of an error while no dialog could be shown
        NativeErrors.takeLastSessionError()?.let { error ->
            NativeErrors.show(tr("Cemu stopped because of an error"), error)
        }
        inputManager?.registerInputDeviceListener(controllerListener, null)
        autoConfigureControllers()
    }

    private val inputManager
        get() = getSystemService(INPUT_SERVICE) as InputManager?

    private val controllerListener = object : InputDeviceListener {
        override fun onInputDeviceChanged() = autoConfigureControllers()
    }

    // a connected controller works without setup, see ControllerAutoMapper
    private fun autoConfigureControllers() {
        lifecycleScope.launch {
            val isAutoMapEnabled = AppSettingsStore.dataStore.data.first().isControllerAutoMapEnabled
            val mappedControllerName = ControllerAutoMapper.autoConfigure(isAutoMapEnabled) ?: return@launch
            Toast.makeText(
                this@MainActivity,
                tr("\"{0}\" was mapped to controller 1. Change it in the input settings.", mappedControllerName),
                Toast.LENGTH_LONG,
            ).show()
        }
    }
}

@Composable
private fun MainNav() {
    val navController = rememberNavController()
    val context = LocalContext.current

    NavHost(
        navController = navController,
        startDestination = GameListRoute,
        enterTransition = { EnterTransition.None },
        exitTransition = { ExitTransition.None }) {
        gamesNavigation(
            navController = navController,
            startGame = { startGame(context, it) },
            tryCreateShortcut = { tryCreateShortcutForGame(context, it) },
            compileShaders = { startGame(context, it, precompileShadersOnly = true) },
            goToSettings = { navController.navigate(SettingsRoute) },
            goToTitleManager = { navController.navigate(TitleManagerRoute) },
            goToGraphicPacks = { navController.navigate(GraphicPacksRoute) },
            goToGraphicPacksForGame = {
                navController.navigate(GraphicPacksForTitleRoute(it.titleId, it.name ?: ""))
            },
            goToAboutCemu = { navController.navigate(AboutCemuRoute) },
        )
        settingsNavigation(navController)
        titleManagerNavigation(navController)
        graphicPacksNavigation(navController)
        aboutCemuNavigation(navController)
    }
}

private fun createIntentForGame(context: Context, game: Game): Intent {
    val intent = Intent(
        context, EmulationActivity::class.java
    )
    intent.action = Intent.ACTION_VIEW
    intent.putExtra(EmulationActivity.EXTRA_LAUNCH_PATH, game.path)

    return intent
}

/** [precompileShadersOnly]: only compile the game's shader cache for the current driver, see NativeEmulation. */
private fun startGame(context: Context, game: Game, precompileShadersOnly: Boolean = false) {
    if (SaveSyncCoordinator.isSyncing) {
        Toast.makeText(context, tr("Wait until the saves are synced"), Toast.LENGTH_SHORT).show()
        return
    }
    NativeSettings.saveSettings()

    val intent = createIntentForGame(context, game)
    // this process synced the saves at start; the emulation process doesn't have to again
    intent.putExtra(EmulationActivity.EXTRA_SAVES_SYNCED, true)
    intent.putExtra(EmulationActivity.EXTRA_PRECOMPILE_SHADERS_ONLY, precompileShadersOnly)
    context.startActivity(intent)
}

private fun tryCreateShortcutForGame(
    context: Context,
    game: Game,
): Boolean {
    try {
        val shortcutManager = context.getSystemService(
            ShortcutManager::class.java
        )
        if (!shortcutManager.isRequestPinShortcutSupported) {
            return false
        }

        val icon = game.icon?.asAndroidBitmap().let {
            if (it != null) Icon.createWithBitmap(it)
            else Icon.createWithResource(context, R.mipmap.ic_launcher)
        }

        val intent = createIntentForGame(context, game)

        val pinShortcutInfo =
            ShortcutInfo.Builder(context, game.titleId.toString()).setShortLabel(game.name!!)
                .setIntent(intent).setIcon(icon).build()

        val pinnedShortcutCallbackIntent =
            shortcutManager.createShortcutResultIntent(pinShortcutInfo)

        val successCallback = PendingIntent.getBroadcast(
            context, 0, pinnedShortcutCallbackIntent, PendingIntent.FLAG_IMMUTABLE
        )

        shortcutManager.requestPinShortcut(pinShortcutInfo, successCallback.intentSender)
        return true
    } catch (_: Exception) {
        return false
    }
}
