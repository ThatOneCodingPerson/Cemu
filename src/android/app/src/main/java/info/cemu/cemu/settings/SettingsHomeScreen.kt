package info.cemu.cemu.settings

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.dropUnlessResumed
import info.cemu.cemu.common.ui.components.Button
import info.cemu.cemu.common.ui.components.ScreenContent
import info.cemu.cemu.common.ui.extensions.showMessage
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.settings.keys.KeysImportResult
import info.cemu.cemu.settings.keys.importKeysFile
import kotlinx.coroutines.launch

@Composable
fun SettingsHomeScreen(
    goToGeneralSettings: () -> Unit,
    goToInputSettings: () -> Unit,
    goToGraphicsSettings: () -> Unit,
    goToEmulatedUSBDevicesSettings: () -> Unit,
    goToAudioSettings: () -> Unit,
    goToAccountSettings: () -> Unit,
    goToOverlaySettings: () -> Unit,
    navigateBack: () -> Unit
) {
    val context = LocalContext.current
    val coroutineScope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }

    val keysFileLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        coroutineScope.launch {
            val message = when (val result = importKeysFile(context, uri)) {
                is KeysImportResult.Imported -> when {
                    result.addedCount == 0 -> tr("All keys of this file were already added")
                    result.invalidLineCount > 0 ->
                        tr("Added {0} keys, skipped {1} lines that are not keys", result.addedCount, result.invalidLineCount)

                    else -> tr("Added {0} keys", result.addedCount)
                }

                KeysImportResult.NoKeysFound -> tr("No keys found in this file")
                KeysImportResult.Error -> tr("Failed to import the keys file")
            }
            snackbarHostState.showMessage(coroutineScope, message)
        }
    }

    ScreenContent(
        appBarText = tr("Settings"),
        snackbarHost = { SnackbarHost(hostState = snackbarHostState) },
        navigateBack = navigateBack,
    ) {
        Button(
            label = tr("General settings"),
            onClick = dropUnlessResumed(block = goToGeneralSettings)
        )
        Button(
            label = tr("Input settings"),
            onClick = dropUnlessResumed(block = goToInputSettings)
        )
        Button(
            label = tr("Graphics settings"),
            onClick = dropUnlessResumed(block = goToGraphicsSettings)
        )
        Button(
            label = tr("Audio settings"),
            onClick = dropUnlessResumed(block = goToAudioSettings)
        )
        Button(
            label = tr("Overlay settings"),
            onClick = dropUnlessResumed(block = goToOverlaySettings)
        )
        Button(
            label = tr("Emulated USB Devices"),
            onClick = dropUnlessResumed(block = goToEmulatedUSBDevicesSettings)
        )
        Button(
            label = tr("Account settings"),
            onClick = dropUnlessResumed(block = goToAccountSettings)
        )
        Button(
            label = tr("Import keys file"),
            description = tr("Adds the disc keys of a keys.txt (needed for WUD/WUX images) from any folder"),
            onClick = { keysFileLauncher.launch(arrayOf("text/plain", "application/octet-stream", "*/*")) }
        )
    }
}
