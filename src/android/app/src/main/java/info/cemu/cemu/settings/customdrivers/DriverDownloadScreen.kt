@file:OptIn(ExperimentalLayoutApi::class)

package info.cemu.cemu.settings.customdrivers

import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.items
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.SnackbarDuration
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.SnackbarResult
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.lifecycle.viewmodel.compose.viewModel
import info.cemu.cemu.R
import info.cemu.cemu.common.ui.components.ScreenContentLazy
import info.cemu.cemu.common.ui.localization.tr
import kotlinx.coroutines.launch
import java.time.Instant
import java.time.ZoneId
import java.time.format.DateTimeFormatter
import java.time.format.FormatStyle
import java.util.Locale

@Composable
fun DriverDownloadScreen(
    navigateBack: () -> Unit,
    viewModel: DriverDownloadViewModel = viewModel(),
) {
    val coroutineScope = rememberCoroutineScope()
    val snackbarHostState = remember { SnackbarHostState() }
    val gpuInfo by viewModel.gpuInfo.collectAsState()
    val suggestion by viewModel.suggestion.collectAsState()
    val selectedRepository by viewModel.selectedRepository.collectAsState()
    val releasesState by viewModel.releasesState.collectAsState()
    val download by viewModel.download.collectAsState()

    fun onInstallFinished(result: DriverInstallResult) {
        coroutineScope.launch {
            snackbarHostState.currentSnackbarData?.dismiss()
            val driverPath = result.driverPath
            if (driverPath == null) {
                snackbarHostState.showSnackbar(driverInstallStatusMessage(result.status))
                return@launch
            }
            // a downloaded driver is best picked per game (game profile); the action makes it the driver of every game
            val snackbarResult = snackbarHostState.showSnackbar(
                message = tr("{0}. Pick it for a game in its game profile, or use it for all games.", driverInstallStatusMessage(result.status)),
                actionLabel = tr("Use for all games"),
                duration = SnackbarDuration.Long,
            )
            if (snackbarResult == SnackbarResult.ActionPerformed) {
                viewModel.selectDriver(driverPath)
                snackbarHostState.showSnackbar(tr("Driver selected for all games, it is used the next time a game starts"))
            }
        }
    }

    ScreenContentLazy(
        snackbarHost = { SnackbarHost(hostState = snackbarHostState) },
        appBarText = tr("Download drivers"),
        navigateBack = navigateBack,
    ) {
        item {
            DefaultDriverNote()
        }
        item {
            GpuInfoCard(gpuInfo, suggestion)
        }
        item {
            RepositorySelection(
                selectedRepository = selectedRepository,
                suggestedRepository = suggestion?.repository,
                onSelect = viewModel::selectRepository,
            )
        }
        when (val state = releasesState) {
            ReleasesState.Loading -> item {
                Row(modifier = Modifier.fillMaxWidth().padding(16.dp), horizontalArrangement = Arrangement.Center) {
                    CircularProgressIndicator()
                }
            }

            ReleasesState.Error -> item {
                ReleasesMessage(
                    message = tr("Could not load the releases. Check the internet connection."),
                    onRetry = viewModel::loadReleases,
                )
            }

            is ReleasesState.RateLimited -> item {
                val resetTime = state.resetEpochSeconds?.let {
                    Instant.ofEpochSecond(it).atZone(ZoneId.systemDefault()).toLocalTime()
                        .format(DateTimeFormatter.ofLocalizedTime(FormatStyle.SHORT))
                }
                ReleasesMessage(
                    message = if (resetTime != null)
                        tr("GitHub limits how often releases can be listed. Try again after {0}.", resetTime)
                    else
                        tr("GitHub limits how often releases can be listed. Try again later."),
                    onRetry = viewModel::loadReleases,
                )
            }

            is ReleasesState.Loaded -> {
                if (state.releases.isEmpty()) {
                    item { ReleasesMessage(message = tr("No driver releases found"), onRetry = null) }
                }
                val newestRelease = state.releases.firstOrNull { !it.prerelease }
                items(state.releases, key = { it.tagName }) { release ->
                    ReleaseCard(
                        release = release,
                        isSuggested = suggestion?.matches(selectedRepository, release, newestRelease) == true,
                        download = download,
                        onDownload = { asset -> viewModel.downloadAndInstall(asset, ::onInstallFinished) },
                    )
                }
            }
        }
    }
}

@Composable
private fun GpuInfoCard(gpuInfo: SystemGpuInfo?, suggestion: DriverSuggestion?) {
    Card(modifier = Modifier.fillMaxWidth().padding(8.dp)) {
        Column(modifier = Modifier.padding(12.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(
                text = gpuInfo?.deviceName ?: tr("Unknown GPU"),
                fontSize = 18.sp,
                fontWeight = FontWeight.Bold,
            )
            if (gpuInfo != null) {
                Text(tr("System driver {0}, Vulkan {1}", gpuInfo.driverVersion, gpuInfo.vulkanVersion))
            }
            val suggestionText = when {
                suggestion == null -> tr("No suggestion for this GPU")
                suggestion.releaseTagPart != null ->
                    tr("Suggested: {0} ({1})", suggestion.repository.displayName, suggestion.releaseTagPart)

                else -> tr("Suggested: {0} (newest release)", suggestion.repository.displayName)
            }
            Text(text = suggestionText, fontWeight = FontWeight.Medium)
            Text(
                text = tr("Suggestions come from other emulators and are a starting point for games that have problems with the system driver."),
                fontSize = 13.sp,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
    }
}

@Composable
private fun RepositorySelection(
    selectedRepository: DriverRepository,
    suggestedRepository: DriverRepository?,
    onSelect: (DriverRepository) -> Unit,
) {
    Column(modifier = Modifier.padding(horizontal = 8.dp)) {
        FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            DriverRepository.entries.forEach { repository ->
                FilterChip(
                    selected = repository == selectedRepository,
                    onClick = { onSelect(repository) },
                    label = {
                        Text(if (repository == suggestedRepository) "★ ${repository.displayName}" else repository.displayName)
                    },
                )
            }
        }
        Text(
            text = "${tr(selectedRepository.description)} · github.com/${selectedRepository.repo}",
            fontSize = 13.sp,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun ReleasesMessage(message: String, onRetry: (() -> Unit)?) {
    Column(
        modifier = Modifier.fillMaxWidth().padding(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text(message)
        if (onRetry != null) {
            Button(onClick = onRetry) { Text(tr("Retry")) }
        }
    }
}

@Composable
private fun ReleaseCard(
    release: GitHubRelease,
    isSuggested: Boolean,
    download: DriverDownload?,
    onDownload: (GitHubReleaseAsset) -> Unit,
) {
    Card(modifier = Modifier.fillMaxWidth().padding(8.dp)) {
        Column(modifier = Modifier.padding(12.dp)) {
            Text(text = release.title, fontSize = 17.sp, fontWeight = FontWeight.Bold)
            val labels = listOfNotNull(
                release.publishedAt?.take(10),
                if (isSuggested) tr("Suggested") else null,
                if (release.prerelease) tr("Pre-release") else null,
            )
            Text(
                text = labels.joinToString(" · "),
                fontSize = 13.sp,
                color = if (isSuggested) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
            )
            release.zipAssets.forEach { asset ->
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Column(modifier = Modifier.weight(1f)) {
                        Text(text = asset.name, fontSize = 14.sp)
                        Text(
                            text = String.format(Locale.ROOT, "%.1f MB", asset.size / (1024.0 * 1024.0)),
                            fontSize = 12.sp,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                        )
                    }
                    IconButton(onClick = { onDownload(asset) }, enabled = download == null) {
                        Icon(
                            painter = painterResource(R.drawable.ic_download),
                            contentDescription = tr("Download and install"),
                        )
                    }
                }
                if (download != null && download.asset == asset) {
                    val progress = download.progress
                    if (progress != null)
                        LinearProgressIndicator(progress = { progress }, modifier = Modifier.fillMaxWidth())
                    else
                        LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                }
            }
        }
    }
}
