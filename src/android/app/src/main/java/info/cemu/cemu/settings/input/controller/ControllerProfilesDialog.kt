package info.cemu.cemu.settings.input.controller

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.HorizontalDivider
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import info.cemu.cemu.R
import info.cemu.cemu.common.ui.localization.tr

/**
 * Saves the controller's configuration under a name, or loads a saved one into it. Game profiles can use these
 * names to switch configurations per game.
 */
@Composable
fun ControllerProfilesDialog(
    profiles: List<String>,
    isValidName: (String) -> Boolean,
    onSave: (String) -> Unit,
    onLoad: (String) -> Unit,
    onDelete: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    var newName by remember { mutableStateOf("") }
    val trimmedName = newName.trim()
    val isNameValid = trimmedName.isNotEmpty() && isValidName(trimmedName)

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(tr("Controller profiles")) },
        text = {
            Column(
                modifier = Modifier
                    .heightIn(max = 420.dp)
                    .verticalScroll(rememberScrollState())
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    OutlinedTextField(
                        value = newName,
                        onValueChange = { newName = it },
                        label = { Text(tr("Profile name")) },
                        singleLine = true,
                        isError = trimmedName.isNotEmpty() && !isNameValid,
                        modifier = Modifier.weight(1f),
                    )
                    TextButton(enabled = isNameValid, onClick = { onSave(trimmedName) }) {
                        Text(tr("Save"))
                    }
                }
                Text(
                    text = if (trimmedName in profiles) tr("Saving replaces the existing profile")
                    else tr("Saves the current configuration of this controller"),
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                    modifier = Modifier.padding(vertical = 4.dp),
                )
                if (profiles.isNotEmpty()) {
                    HorizontalDivider(modifier = Modifier.padding(vertical = 8.dp))
                }
                profiles.forEach { profile ->
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(text = profile, modifier = Modifier.weight(1f))
                        TextButton(onClick = { onLoad(profile) }) { Text(tr("Load")) }
                        IconButton(onClick = { onDelete(profile) }) {
                            Icon(painter = painterResource(R.drawable.ic_delete), contentDescription = tr("Delete"))
                        }
                    }
                }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(tr("Close")) } },
    )
}
