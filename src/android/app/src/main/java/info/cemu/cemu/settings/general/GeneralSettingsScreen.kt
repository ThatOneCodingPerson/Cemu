package info.cemu.cemu.settings.general

import android.os.PowerManager
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.dropUnlessResumed
import androidx.lifecycle.viewmodel.compose.viewModel
import info.cemu.cemu.common.settings.GamePadPosition
import info.cemu.cemu.common.settings.TV_SCREEN_PERCENT_RANGE
import info.cemu.cemu.common.ui.components.Slider
import info.cemu.cemu.common.ui.components.Button
import info.cemu.cemu.common.ui.components.ScreenContent
import info.cemu.cemu.common.ui.components.SingleSelection
import info.cemu.cemu.common.ui.components.Toggle
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeSettings

@Composable
fun GeneralSettingsScreen(
    navigateBack: () -> Unit,
    goToGamePathsSettings: () -> Unit,
    goToDataStorageSettings: () -> Unit,
    viewModel: GeneralSettingsViewModel = viewModel(),
) {
    val context = LocalContext.current

    val emulationSettings by viewModel.emulationSettings.collectAsState()
    val guiSettings by viewModel.guiSettings.collectAsState()

    ScreenContent(
        appBarText = tr("General settings"),
        navigateBack = navigateBack,
    ) {
        Button(
            label = tr("Add game path"),
            description = tr("Add the root directory of your game(s). It will scan all directories in it for games"),
            onClick = dropUnlessResumed { goToGamePathsSettings() },
        )
        Button(
            label = tr("Data storage"),
            description = tr("Choose where Cemu stores saves, MLC, settings and cache"),
            onClick = dropUnlessResumed { goToDataStorageSettings() },
        )
        SingleSelection(
            label = tr("Language"),
            choice = guiSettings.language,
            onChoiceChanged = { viewModel.setLanguage(language = it, context) },
            choiceToString = { viewModel.languageToDisplayNameMap[it] ?: it },
            choices = viewModel.languages,
        )
        SingleSelection(
            label = tr("Console language"),
            initialChoice = NativeSettings::getConsoleLanguage,
            onChoiceChanged = NativeSettings::setConsoleLanguage,
            choiceToString = { consoleLanguageToString(it) },
            choices = listOf(
                NativeSettings.ConsoleLanguage.JAPANESE,
                NativeSettings.ConsoleLanguage.ENGLISH,
                NativeSettings.ConsoleLanguage.FRENCH,
                NativeSettings.ConsoleLanguage.GERMAN,
                NativeSettings.ConsoleLanguage.ITALIAN,
                NativeSettings.ConsoleLanguage.SPANISH,
                NativeSettings.ConsoleLanguage.CHINESE,
                NativeSettings.ConsoleLanguage.KOREAN,
                NativeSettings.ConsoleLanguage.DUTCH,
                NativeSettings.ConsoleLanguage.PORTUGUESE,
                NativeSettings.ConsoleLanguage.RUSSIAN,
                NativeSettings.ConsoleLanguage.TAIWANESE,
            ),
        )

        SingleSelection(
            label = tr("GamePad position"),
            choice = emulationSettings.gamePadPosition,
            onChoiceChanged = { viewModel.setGamePadPosition(it) },
            choiceToString = { gamePadPositionToString(it) },
            choices = GamePadPosition.entries,
        )

        Slider(
            label = tr("TV screen size next to the GamePad"),
            value = emulationSettings.tvScreenPercent,
            valueFrom = TV_SCREEN_PERCENT_RANGE.first,
            valueTo = TV_SCREEN_PERCENT_RANGE.last,
            steps = (TV_SCREEN_PERCENT_RANGE.last - TV_SCREEN_PERCENT_RANGE.first) / 5 - 1,
            labelFormatter = { "$it%" },
            onValueChange = { viewModel.setTvScreenPercent(it) },
        )

        val isSustainedPerformanceModeSupported = remember {
            context.getSystemService(PowerManager::class.java)?.isSustainedPerformanceModeSupported == true
        }
        if (isSustainedPerformanceModeSupported) {
            Toggle(
                label = tr("Sustained performance mode"),
                description = tr("Keeps the clock speeds at a level the device can hold without overheating: fewer sudden slowdowns in long sessions, but a lower peak"),
                checked = emulationSettings.isSustainedPerformanceModeEnabled,
                onCheckedChanged = { viewModel.setSustainedPerformanceModeEnabled(it) },
            )
        }
    }
}

private fun gamePadPositionToString(position: GamePadPosition) = when (position) {
    GamePadPosition.ABOVE -> tr("Above")
    GamePadPosition.BELOW -> tr("Below")
    GamePadPosition.LEFT -> tr("Left")
    GamePadPosition.RIGHT -> tr("Right")
}

private fun consoleLanguageToString(channels: Int): String = when (channels) {
    NativeSettings.ConsoleLanguage.JAPANESE -> tr("Japanese")
    NativeSettings.ConsoleLanguage.ENGLISH -> tr("English")
    NativeSettings.ConsoleLanguage.FRENCH -> tr("French")
    NativeSettings.ConsoleLanguage.GERMAN -> tr("German")
    NativeSettings.ConsoleLanguage.ITALIAN -> tr("Italian")
    NativeSettings.ConsoleLanguage.SPANISH -> tr("Spanish")
    NativeSettings.ConsoleLanguage.CHINESE -> tr("Chinese")
    NativeSettings.ConsoleLanguage.KOREAN -> tr("Korean")
    NativeSettings.ConsoleLanguage.DUTCH -> tr("Dutch")
    NativeSettings.ConsoleLanguage.PORTUGUESE -> tr("Portuguese")
    NativeSettings.ConsoleLanguage.RUSSIAN -> tr("Russian")
    NativeSettings.ConsoleLanguage.TAIWANESE -> tr("Taiwanese")
    else -> throw IllegalArgumentException("Invalid console language: $channels")
}
