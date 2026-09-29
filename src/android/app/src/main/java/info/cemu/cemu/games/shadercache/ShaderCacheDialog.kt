package info.cemu.cemu.games.shadercache

import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.unit.dp
import info.cemu.cemu.common.emulation.EmulationSessionState
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeGameTitles.Game
import info.cemu.cemu.nativeinterface.NativeShaderCache
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.util.Locale

/**
 * A game's shader cache: what's there, importing/merging caches from other devices or other people, exporting it,
 * and compiling it for the current GPU driver without playing.
 */
@Composable
fun ShaderCacheDialog(
    game: Game,
    onCompile: () -> Unit,
    onMessage: (String) -> Unit,
    onDismiss: () -> Unit,
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var info by remember { mutableStateOf<NativeShaderCache.CacheInfo?>(null) }
    var isWorking by remember { mutableStateOf(false) }
    var reloadCount by remember { mutableIntStateOf(0) }

    LaunchedEffect(game.titleId, reloadCount) {
        info = withContext(Dispatchers.IO) { NativeShaderCache.getInfo(game.titleId) }
    }

    fun isGameRunning(): Boolean {
        if (!EmulationSessionState.isEmulationRunning(context))
            return false
        onMessage(tr("Quit the running game first"))
        return true
    }

    val importLauncher = rememberLauncherForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
        if (uris.isEmpty()) return@rememberLauncherForActivityResult
        isWorking = true
        scope.launch {
            val summary = ShaderCacheTransfer.import(context, game.titleId, uris)
            isWorking = false
            reloadCount++
            onMessage(importSummaryMessage(summary))
        }
    }

    val exportLauncher = rememberLauncherForActivityResult(ActivityResultContracts.CreateDocument("application/zip")) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        isWorking = true
        scope.launch {
            val exported = ShaderCacheTransfer.export(context, game.titleId, uri)
            isWorking = false
            onMessage(if (exported) tr("Shader cache exported") else tr("Failed to export the shader cache"))
        }
    }

    val currentInfo = info
    val hasList = currentInfo != null && (currentInfo.shaderCount > 0 || currentInfo.pipelineCount > 0)

    AlertDialog(
        onDismissRequest = onDismiss,
        title = { Text(tr("Shader cache")) },
        text = {
            Column(
                modifier = Modifier.verticalScroll(rememberScrollState()),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text(text = game.name ?: "", style = MaterialTheme.typography.titleSmall)
                if (currentInfo != null) {
                    Text(
                        tr(
                            "Shader list: {0} shaders, {1} pipelines",
                            currentInfo.shaderCount.coerceAtLeast(0),
                            currentInfo.pipelineCount.coerceAtLeast(0),
                        )
                    )
                    // the driver ignores a cache of another driver (version), so this can't tell "compiled for
                    // the current one"
                    Text(
                        if (currentInfo.driverCacheBytes > 0)
                            tr("Compiled pipelines: {0}, from the last compile or play", megabytes(currentInfo.driverCacheBytes))
                        else
                            tr("Compiled pipelines: none yet")
                    )
                }
                Text(
                    text = tr("The shader list grows while you play and works on any device, so lists from other devices or other players can be imported and are merged with yours. Compiling turns the list into shaders for your GPU driver, which the game would otherwise do on its loading screen; do it again after switching drivers."),
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )
                if (isWorking) {
                    LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                }
                OutlinedButton(
                    modifier = Modifier.fillMaxWidth(),
                    enabled = !isWorking && hasList,
                    onClick = {
                        if (!isGameRunning()) {
                            onDismiss()
                            onCompile()
                        }
                    },
                ) { Text(tr("Compile now")) }
                OutlinedButton(
                    modifier = Modifier.fillMaxWidth(),
                    enabled = !isWorking,
                    onClick = { if (!isGameRunning()) importLauncher.launch(arrayOf("*/*")) },
                ) { Text(tr("Import cache files (.bin or .zip)")) }
                OutlinedButton(
                    modifier = Modifier.fillMaxWidth(),
                    enabled = !isWorking && hasList,
                    onClick = {
                        exportLauncher.launch("${game.name ?: "game"} [${"%016X".format(game.titleId)}] shader cache.zip")
                    },
                ) { Text(tr("Export shader list")) }
            }
        },
        confirmButton = { TextButton(onClick = onDismiss) { Text(tr("Close")) } },
    )
}

private fun megabytes(bytes: Long) = String.format(Locale.ROOT, "%.1f MB", bytes / (1024.0 * 1024.0))

private fun importSummaryMessage(summary: ShaderCacheImportSummary): String {
    val parts = mutableListOf(tr("Added {0} shaders and {1} pipelines", summary.addedShaders, summary.addedPipelines))
    if (summary.filesOfOtherGames > 0)
        parts += tr("{0} files belong to another game or are not shader caches", summary.filesOfOtherGames)
    if (summary.failedFiles > 0)
        parts += tr("{0} files could not be read", summary.failedFiles)
    return parts.joinToString(". ")
}
