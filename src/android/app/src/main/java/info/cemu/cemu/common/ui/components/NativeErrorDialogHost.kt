package info.cemu.cemu.common.ui.components

import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.window.DialogProperties
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeErrors

/** Shows errors reported by the emulator core one at a time; see [NativeErrors]. */
@Composable
fun NativeErrorDialogHost() {
    val dialogs by NativeErrors.dialogs.collectAsState()
    val dialog = dialogs.firstOrNull() ?: return

    AlertDialog(
        // require an explicit OK: the emulator waits for it before it may exit
        onDismissRequest = {},
        properties = DialogProperties(dismissOnBackPress = false, dismissOnClickOutside = false),
        title = { Text(dialog.title.ifEmpty { tr("Error") }) },
        text = {
            Text(
                text = dialog.message,
                modifier = Modifier.verticalScroll(rememberScrollState()),
            )
        },
        confirmButton = {
            TextButton(onClick = { NativeErrors.dismiss(dialog) }) {
                Text(tr("OK"))
            }
        },
    )
}
