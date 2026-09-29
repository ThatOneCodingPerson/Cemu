package info.cemu.cemu.settings.graphics

import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import info.cemu.cemu.common.ui.components.Button
import info.cemu.cemu.common.ui.components.Header
import info.cemu.cemu.common.ui.components.ScreenContent
import info.cemu.cemu.common.ui.components.SingleSelection
import info.cemu.cemu.common.ui.components.Slider
import info.cemu.cemu.common.ui.components.Toggle
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeEmulation
import info.cemu.cemu.nativeinterface.NativeSettings
import kotlin.math.roundToInt

private val UpscalingFilterChoices = listOf(
    NativeSettings.ScalingFilter.BILINEAR_FILTER,
    NativeSettings.ScalingFilter.BICUBIC_FILTER,
    NativeSettings.ScalingFilter.BICUBIC_HERMITE_FILTER,
    NativeSettings.ScalingFilter.NEAREST_NEIGHBOR_FILTER,
    NativeSettings.ScalingFilter.FSR_EASU_FILTER,
)

// FSR only upscales
private val DownscalingFilterChoices = listOf(
    NativeSettings.ScalingFilter.BILINEAR_FILTER,
    NativeSettings.ScalingFilter.BICUBIC_FILTER,
    NativeSettings.ScalingFilter.BICUBIC_HERMITE_FILTER,
    NativeSettings.ScalingFilter.NEAREST_NEIGHBOR_FILTER,
)

private val AnisotropicFilterChoices = listOf(
    NativeSettings.AnisotropicFilter.GAME_DEFAULT,
    NativeSettings.AnisotropicFilter.X2,
    NativeSettings.AnisotropicFilter.X4,
    NativeSettings.AnisotropicFilter.X8,
    NativeSettings.AnisotropicFilter.X16,
)

// gamma sliders work in tenths
private val GAMMA_SLIDER_RANGE =
    (NativeSettings.GAMMA_MIN * 10).roundToInt()..(NativeSettings.GAMMA_MAX * 10).roundToInt()

@Composable
fun GraphicsSettingsScreen(navigateBack: () -> Unit, goToCustomDriversSettings: () -> Unit) {
    val supportsLoadingCustomDrivers = remember { NativeEmulation.supportsLoadingCustomDriver() }

    ScreenContent(
        appBarText = tr("Graphics settings"),
        navigateBack = navigateBack,
    ) {
        if (supportsLoadingCustomDrivers) {
            Button(
                label = tr("Custom drivers"),
                onClick = goToCustomDriversSettings
            )
        }
        Toggle(
            label = tr("Async shader compile"),
            description = tr("Enables async shader and pipeline compilation. Reduces stutter at the cost of objects not rendering for a short time.\nVulkan only"),
            initialCheckedState = NativeSettings::getAsyncShaderCompile,
            onCheckedChanged = NativeSettings::setAsyncShaderCompile,
        )
        SingleSelection(
            label = tr("VSync"),
            initialChoice = NativeSettings::getVsyncMode,
            onChoiceChanged = NativeSettings::setVsyncMode,
            choiceToString = { vsyncModeToString(it) },
            choices = listOf(
                NativeSettings.VSyncMode.OFF,
                NativeSettings.VSyncMode.DOUBLE_BUFFERING,
                NativeSettings.VSyncMode.TRIPLE_BUFFERING
            ),
        )
        Toggle(
            label = tr("Accurate barriers"),
            description = tr("Disabling the accurate barriers option will lead to flickering graphics but may improve performance. It is highly recommended to leave it turned on"),
            initialCheckedState = NativeSettings::getAccurateBarriers,
            onCheckedChanged = NativeSettings::setAccurateBarriers,
        )
        Toggle(
            label = tr("Pre-rotate the picture (experimental)"),
            description = tr("Rotates the picture before it is shown, so Android does not have to rotate every frame. Saves some GPU work on devices whose screen is naturally portrait, like most phones. Applies from the next game start."),
            initialCheckedState = NativeSettings::getVulkanPreRotation,
            onCheckedChanged = NativeSettings::setVulkanPreRotation,
        )
        SingleSelection(
            label = tr("Anisotropic filtering"),
            initialChoice = NativeSettings::getAnisotropicFilter,
            onChoiceChanged = NativeSettings::setAnisotropicFilter,
            choiceToString = { anisotropicFilterToString(it) },
            choices = AnisotropicFilterChoices,
        )
        SingleSelection(
            label = tr("Fullscreen scaling"),
            initialChoice = NativeSettings::getFullscreenScaling,
            onChoiceChanged = NativeSettings::setFullscreenScaling,
            choiceToString = { fullscreenScalingModeToString(it) },
            choices = listOf(
                NativeSettings.FullscreenScaling.KEEP_ASPECT_RATIO,
                NativeSettings.FullscreenScaling.STRETCH
            ),
        )
        SingleSelection(
            label = tr("Upscale filter"),
            initialChoice = NativeSettings::getUpscalingFilter,
            onChoiceChanged = NativeSettings::setUpscalingFilter,
            choiceToString = { scalingFilterToString(it) },
            choices = UpscalingFilterChoices,
        )
        SingleSelection(
            label = tr("Downscale filter"),
            initialChoice = NativeSettings::getDownscalingFilter,
            onChoiceChanged = NativeSettings::setDownscalingFilter,
            choiceToString = { scalingFilterToString(it) },
            choices = DownscalingFilterChoices,
        )
        GammaSettings()
    }
}

@Composable
private fun GammaSettings() {
    var isGameGammaOverridden by rememberSaveable { mutableStateOf(NativeSettings.isOverrideGameGammaEnabled()) }
    var isDisplaySrgb by rememberSaveable {
        mutableStateOf(NativeSettings.getDisplayGamma() == NativeSettings.DISPLAY_GAMMA_SRGB)
    }

    Header(tr("Gamma"))
    Slider(
        label = tr("Target gamma"),
        initialValue = { (NativeSettings.getTargetGamma() * 10).roundToInt() },
        valueFrom = GAMMA_SLIDER_RANGE.first,
        valueTo = GAMMA_SLIDER_RANGE.last,
        labelFormatter = { gammaToString(it) },
        onValueChange = { NativeSettings.setTargetGamma(it / 10f) },
    )
    Toggle(
        label = tr("Ignore the gamma of the game"),
        description = tr("Games can adjust the target gamma. Turn this on to always use the value above."),
        checked = isGameGammaOverridden,
        onCheckedChanged = {
            isGameGammaOverridden = it
            NativeSettings.setOverrideGameGammaEnabled(it)
        },
    )
    Toggle(
        label = tr("The screen uses the sRGB curve"),
        description = tr("Leave this off unless colors in dark scenes look wrong. When it is off, the display gamma below is used."),
        checked = isDisplaySrgb,
        onCheckedChanged = {
            isDisplaySrgb = it
            NativeSettings.setDisplayGamma(if (it) NativeSettings.DISPLAY_GAMMA_SRGB else NativeSettings.DEFAULT_GAMMA)
        },
    )
    Slider(
        label = tr("Display gamma"),
        initialValue = {
            val displayGamma = NativeSettings.getDisplayGamma()
            val gamma = if (displayGamma == NativeSettings.DISPLAY_GAMMA_SRGB) NativeSettings.DEFAULT_GAMMA else displayGamma
            (gamma * 10).roundToInt()
        },
        valueFrom = GAMMA_SLIDER_RANGE.first,
        valueTo = GAMMA_SLIDER_RANGE.last,
        enabled = !isDisplaySrgb,
        labelFormatter = { gammaToString(it) },
        onValueChange = { NativeSettings.setDisplayGamma(it / 10f) },
    )
}

private fun gammaToString(tenths: Int): String {
    val text = "%d.%d".format(tenths / 10, tenths % 10)
    return if (tenths == (NativeSettings.DEFAULT_GAMMA * 10).roundToInt()) tr("{0} (default)", text) else text
}

private fun anisotropicFilterToString(level: Int) = when (level) {
    NativeSettings.AnisotropicFilter.GAME_DEFAULT -> tr("As the game requests")
    NativeSettings.AnisotropicFilter.X2 -> tr("At least 2x")
    NativeSettings.AnisotropicFilter.X4 -> tr("At least 4x")
    NativeSettings.AnisotropicFilter.X8 -> tr("At least 8x")
    NativeSettings.AnisotropicFilter.X16 -> tr("At least 16x")
    else -> throw IllegalArgumentException("Invalid anisotropic filter level: $level")
}

private fun scalingFilterToString(scalingFilter: Int) = when (scalingFilter) {
    NativeSettings.ScalingFilter.BILINEAR_FILTER -> tr("Bilinear")
    NativeSettings.ScalingFilter.BICUBIC_FILTER -> tr("Bicubic")
    NativeSettings.ScalingFilter.BICUBIC_HERMITE_FILTER -> tr("Hermite")
    NativeSettings.ScalingFilter.NEAREST_NEIGHBOR_FILTER -> tr("Nearest neighbor")
    NativeSettings.ScalingFilter.FSR_EASU_FILTER -> tr("AMD FSR 1 (sharp edges)")
    else -> throw IllegalArgumentException("Invalid scaling filter:  $scalingFilter")
}

private fun vsyncModeToString(vsyncMode: Int) = when (vsyncMode) {
    NativeSettings.VSyncMode.OFF -> tr("Off")
    NativeSettings.VSyncMode.DOUBLE_BUFFERING -> tr("Double buffering")
    NativeSettings.VSyncMode.TRIPLE_BUFFERING -> tr("Triple buffering")
    else -> throw IllegalArgumentException("Invalid vsync mode: $vsyncMode")
}

private fun fullscreenScalingModeToString(fullscreenScaling: Int) = when (fullscreenScaling) {
    NativeSettings.FullscreenScaling.KEEP_ASPECT_RATIO -> tr("Keep aspect ratio")
    NativeSettings.FullscreenScaling.STRETCH -> tr("Stretch")
    else -> throw IllegalArgumentException("Invalid fullscreen scaling mode:  $fullscreenScaling")
}
