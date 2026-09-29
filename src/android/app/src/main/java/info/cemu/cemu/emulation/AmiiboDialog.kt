package info.cemu.cemu.emulation

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.unit.dp
import info.cemu.cemu.R
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeActiveSettings
import info.cemu.cemu.nativeinterface.NativeEmulation
import info.cemu.cemu.nativeinterface.NativeEmulation.NfcTouchResult
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import java.io.File

/**
 * Amiibo (NFC tag) files imported into the app, in <user data>/amiibo. Games may write amiibo data back to the file,
 * which SAF documents don't allow, so picked files are copied here and scanned from here.
 */
private object AmiiboLibrary {
    // an amiibo dump is 540 bytes (NTAG215), other NFC dumps are small too; this only guards against wrong picks
    private const val MAX_FILE_SIZE = 64 * 1024

    private fun directory() = File(NativeActiveSettings.getUserDataPath(), "amiibo")

    suspend fun list(): List<File> = withContext(Dispatchers.IO) {
        directory().listFiles { file -> file.isFile }?.sortedBy { it.name.lowercase() } ?: emptyList()
    }

    /** Copies the picked file into the library. An identical file already there is reused (keeps its game data). */
    suspend fun import(context: Context, uri: Uri): File? = withContext(Dispatchers.IO) {
        try {
            val bytes = context.contentResolver.openInputStream(uri)?.use { input ->
                val output = ByteArrayOutputStream()
                val chunk = ByteArray(8192)
                while (true) {
                    val count = input.read(chunk)
                    if (count < 0)
                        break
                    if (output.size() + count > MAX_FILE_SIZE)
                        return@withContext null
                    output.write(chunk, 0, count)
                }
                output.toByteArray()
            } ?: return@withContext null
            if (bytes.isEmpty())
                return@withContext null

            val directory = directory().apply { mkdirs() }
            val name = displayName(context, uri)
                ?.replace(Regex("[/\\\\:*?\"<>|\\p{Cntrl}]"), "_")
                ?.takeIf { it.isNotBlank() && it != "." && it != ".." }
                ?: "amiibo.bin"
            val baseName = name.substringBeforeLast('.')
            val extension = name.substringAfterLast('.', "").let { if (it.isEmpty()) "" else ".$it" }
            var target = File(directory, name)
            var index = 2
            while (target.exists()) {
                if (target.readBytes().contentEquals(bytes))
                    return@withContext target
                target = File(directory, "$baseName ($index)$extension")
                index++
            }
            target.writeBytes(bytes)
            target
        } catch (_: Exception) {
            null
        }
    }

    suspend fun delete(file: File) = withContext(Dispatchers.IO) { file.delete() }

    private fun displayName(context: Context, uri: Uri): String? = try {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor -> if (cursor.moveToFirst()) cursor.getString(0) else null }
    } catch (_: Exception) {
        null
    }
}

private fun nfcTouchResultMessage(result: Int, name: String) = when (result) {
    NfcTouchResult.SUCCESS -> tr("Scanned {0}", name)
    NfcTouchResult.NO_ACCESS -> tr("Cannot open the file")
    NfcTouchResult.INVALID_FILE_FORMAT -> tr("Not a valid amiibo or NFC file")
    else -> tr("The game isn't running yet")
}

/** Lists the imported amiibo; picking one touches it to the emulated NFC reader (the game has to be waiting for it). */
@Composable
fun AmiiboDialog(onMessage: (String) -> Unit, onDismiss: () -> Unit) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var files by remember { mutableStateOf<List<File>?>(null) }

    fun reload() {
        scope.launch { files = AmiiboLibrary.list() }
    }

    fun scan(file: File) {
        val name = file.nameWithoutExtension
        val result = NativeEmulation.touchNfcTagFromFile(file.path)
        onMessage(nfcTouchResultMessage(result, name))
        if (result == NfcTouchResult.SUCCESS)
            onDismiss()
    }

    LaunchedEffect(Unit) { reload() }

    val importLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        scope.launch {
            val file = AmiiboLibrary.import(context, uri)
            if (file == null) {
                onMessage(tr("Cannot open the file"))
                return@launch
            }
            reload()
            scan(file)
        }
    }

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(tr("Amiibo")) },
        text = {
            Column(
                modifier = Modifier
                    .heightIn(max = 360.dp)
                    .verticalScroll(rememberScrollState())
            ) {
                val currentFiles = files
                if (currentFiles != null && currentFiles.isEmpty()) {
                    Text(
                        text = tr("Import an amiibo file (.bin). It is kept in Cemu, so it can be scanned again later and the game can save data to it."),
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                    )
                }
                currentFiles?.forEach { file ->
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        Text(
                            text = file.nameWithoutExtension,
                            modifier = Modifier
                                .weight(1f)
                                .clickable { scan(file) }
                                .padding(vertical = 12.dp),
                        )
                        IconButton(onClick = { scope.launch { AmiiboLibrary.delete(file); reload() } }) {
                            Icon(painter = painterResource(R.drawable.ic_delete), contentDescription = tr("Delete"))
                        }
                    }
                }
            }
        },
        confirmButton = {
            TextButton(onClick = { importLauncher.launch(arrayOf("*/*")) }) { Text(tr("Import file")) }
        },
        dismissButton = {
            TextButton(onClick = onDismiss) { Text(tr("Close")) }
        },
    )
}
