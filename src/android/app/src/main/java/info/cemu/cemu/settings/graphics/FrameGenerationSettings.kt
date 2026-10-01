package info.cemu.cemu.settings.graphics

import android.content.Context
import android.net.Uri
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import info.cemu.cemu.common.ui.components.Button
import info.cemu.cemu.common.ui.components.Header
import info.cemu.cemu.common.ui.components.SingleSelection
import info.cemu.cemu.common.ui.components.Toggle
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeSettings
import info.cemu.cemu.nativeinterface.NativeSettings.LosslessDllStatus
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.io.IOException
import kotlin.math.abs

// modes up to this value are multipliers, higher ones are target frame rates
private const val MAX_MULTIPLIER = 4

private val FrameGenerationModes = listOf(2, 3, 4, 60, 90, 120)

private val FlowScaleChoices = listOf(NativeSettings.FRAME_GENERATION_FLOW_SCALE_AUTO, 100, 75, 50, 25)

/**
 * Frame generation with the shaders of the user's own Lossless Scaling: adding its Lossless.dll, turning it on and
 * the mode. The renderer reads the settings every frame.
 */
@Composable
fun FrameGenerationSettings() {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    // null while the file is checked
    var dllStatus by remember { mutableStateOf<Int?>(null) }
    var importResult by remember { mutableStateOf<Int?>(null) }
    var isImporting by remember { mutableStateOf(false) }
    var isEnabled by remember { mutableStateOf(NativeSettings.isFrameGenerationEnabled()) }

    LaunchedEffect(Unit) {
        dllStatus = withContext(Dispatchers.IO) { NativeSettings.getLosslessDllStatus() }
    }

    val pickDll = rememberLauncherForActivityResult(ActivityResultContracts.OpenDocument()) { uri ->
        if (uri == null) return@rememberLauncherForActivityResult
        isImporting = true
        scope.launch {
            importResult = withContext(Dispatchers.IO) { importLosslessDll(context, uri) }
            dllStatus = withContext(Dispatchers.IO) { NativeSettings.getLosslessDllStatus() }
            isImporting = false
        }
    }
    val isInstalled = dllStatus == LosslessDllStatus.OK

    Header(tr("Frame generation"))
    Button(
        label = if (isInstalled) tr("Replace Lossless.dll") else tr("Add Lossless.dll"),
        description = losslessDllDescription(dllStatus, importResult),
        enabled = !isImporting && dllStatus != null,
        // DLLs have no reliable MIME type
        onClick = { pickDll.launch(arrayOf("*/*")) },
    )
    if (isInstalled) {
        Button(
            label = tr("Remove Lossless.dll"),
            enabled = !isImporting,
            onClick = {
                NativeSettings.setFrameGenerationEnabled(false)
                isEnabled = false
                importResult = null
                isImporting = true
                scope.launch {
                    dllStatus = withContext(Dispatchers.IO) {
                        NativeSettings.removeLosslessDll()
                        NativeSettings.getLosslessDllStatus()
                    }
                    isImporting = false
                }
            },
        )
    }
    Toggle(
        label = tr("Frame generation"),
        description = tr("Shows generated frames between the ones the game renders, so motion looks smoother. Adds a little input delay, and fast motion can show artifacts. VSync is on while it runs. The in-game menu can switch it for the game you are playing."),
        checked = isEnabled && isInstalled,
        enabled = isInstalled,
        onCheckedChanged = {
            isEnabled = it
            NativeSettings.setFrameGenerationEnabled(it)
        },
    )
    SingleSelection(
        label = tr("Frame generation mode"),
        initialChoice = ::currentFrameGenerationMode,
        choices = FrameGenerationModes,
        choiceToString = ::frameGenerationModeToString,
        enabled = isInstalled,
        onChoiceChanged = { mode ->
            if (mode > MAX_MULTIPLIER) {
                NativeSettings.setFrameGenerationTargetRate(mode)
            } else {
                NativeSettings.setFrameGenerationMultiplier(mode)
                NativeSettings.setFrameGenerationTargetRate(0)
            }
        },
    )
    SingleSelection(
        label = tr("Motion detail"),
        initialChoice = NativeSettings::getFrameGenerationFlowScale,
        choices = FlowScaleChoices,
        choiceToString = ::flowScaleToString,
        enabled = isInstalled,
        onChoiceChanged = NativeSettings::setFrameGenerationFlowScale,
    )
}

/** Copies the picked file into the cache, the native side checks it and moves it into Cemu's data folder. */
private fun importLosslessDll(context: Context, uri: Uri): Int {
    val file = File(context.cacheDir, "Lossless.dll.import")
    return try {
        val input = context.contentResolver.openInputStream(uri) ?: return LosslessDllStatus.UNREADABLE_FILE
        input.use { file.outputStream().use { output -> it.copyTo(output) } }
        NativeSettings.installLosslessDll(file.absolutePath)
    } catch (e: IOException) {
        LosslessDllStatus.UNREADABLE_FILE
    } catch (e: SecurityException) {
        LosslessDllStatus.UNREADABLE_FILE
    } finally {
        file.delete()
    }
}

private fun currentFrameGenerationMode(): Int {
    val targetRate = NativeSettings.getFrameGenerationTargetRate()
    if (targetRate == 0) return NativeSettings.getFrameGenerationMultiplier()
    return FrameGenerationModes.filter { it > MAX_MULTIPLIER }.minBy { abs(it - targetRate) }
}

private fun losslessDllDescription(status: Int?, importResult: Int?): String {
    when (importResult) {
        null, LosslessDllStatus.OK -> {}
        LosslessDllStatus.NOT_PORTABLE_EXECUTABLE ->
            return tr("That file is not a Windows DLL. Pick Lossless.dll from the Lossless Scaling folder.")

        LosslessDllStatus.MISSING_SHADERS ->
            return tr("That DLL does not have the frame generation shaders. Pick Lossless.dll from an up to date Lossless Scaling.")

        else -> return tr("The file could not be read.")
    }
    return when (status) {
        null -> tr("Checking…")
        LosslessDllStatus.OK ->
            if (importResult == LosslessDllStatus.OK) tr("Lossless.dll added. Turn frame generation on below.")
            else tr("Frame generation uses the shaders from this Lossless.dll.")

        LosslessDllStatus.NOT_INSTALLED ->
            tr("Frame generation uses the shaders of Lossless Scaling, which do not come with Cemu. Pick Lossless.dll from your own copy (in steamapps/common/Lossless Scaling on your PC).")

        else -> tr("The added Lossless.dll can not be used. Pick it again.")
    }
}

private fun frameGenerationModeToString(mode: Int) = when (mode) {
    2 -> tr("2x (30 FPS becomes 60)")
    3 -> tr("3x (30 FPS becomes 90)")
    4 -> tr("4x (30 FPS becomes 120)")
    60 -> tr("Adaptive, up to 60 FPS")
    90 -> tr("Adaptive, up to 90 FPS")
    120 -> tr("Adaptive, up to 120 FPS")
    else -> throw IllegalArgumentException("Invalid frame generation mode: $mode")
}

private fun flowScaleToString(percent: Int) = when (percent) {
    NativeSettings.FRAME_GENERATION_FLOW_SCALE_AUTO -> tr("Automatic (from the game's resolution)")
    100 -> tr("100% (best, slowest)")
    75 -> tr("75%")
    50 -> tr("50%")
    25 -> tr("25% (fastest)")
    else -> throw IllegalArgumentException("Invalid frame generation flow scale: $percent")
}
