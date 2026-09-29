package info.cemu.cemu.settings.customdrivers

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import info.cemu.cemu.nativeinterface.NativeEmulation
import info.cemu.cemu.nativeinterface.NativeSettings
import io.ktor.client.HttpClient
import io.ktor.client.request.prepareGet
import io.ktor.client.statement.bodyAsChannel
import io.ktor.http.contentLength
import io.ktor.http.isSuccess
import io.ktor.utils.io.readAvailable
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

data class SystemGpuInfo(val deviceName: String, val driverVersion: String, val vulkanVersion: String) {
    val adrenoModel = parseAdrenoModel(deviceName)
}

sealed interface ReleasesState {
    data object Loading : ReleasesState
    data class Loaded(val releases: List<GitHubRelease>) : ReleasesState
    data class RateLimited(val resetEpochSeconds: Long?) : ReleasesState
    data object Error : ReleasesState
}

/** [progress] is null while the size is unknown. */
data class DriverDownload(val asset: GitHubReleaseAsset, val progress: Float?)

// driver zips are 2-20 MB, anything far bigger isn't a driver package
private const val MAX_DRIVER_ZIP_SIZE = 256L * 1024 * 1024

class DriverDownloadViewModel(application: Application) : AndroidViewModel(application) {
    private val client = HttpClient()

    private val _gpuInfo = MutableStateFlow<SystemGpuInfo?>(null)
    val gpuInfo = _gpuInfo.asStateFlow()

    private val _suggestion = MutableStateFlow<DriverSuggestion?>(null)
    val suggestion = _suggestion.asStateFlow()

    private val _selectedRepository = MutableStateFlow(DriverRepository.entries.first())
    val selectedRepository = _selectedRepository.asStateFlow()

    private val _releasesState = MutableStateFlow<ReleasesState>(ReleasesState.Loading)
    val releasesState = _releasesState.asStateFlow()

    private val _download = MutableStateFlow<DriverDownload?>(null)
    val download = _download.asStateFlow()

    private var releasesJob: Job? = null
    private var downloadJob: Job? = null

    init {
        viewModelScope.launch {
            val gpuInfo = withContext(Dispatchers.IO) {
                try {
                    NativeEmulation.getSystemGpuInfo()
                        ?.takeIf { it.size == 3 }
                        ?.let { SystemGpuInfo(it[0], it[1], it[2]) }
                } catch (_: Throwable) {
                    null
                }
            }
            _gpuInfo.value = gpuInfo
            val suggestion = suggestDriver(gpuInfo?.adrenoModel)
            _suggestion.value = suggestion
            selectRepository(suggestion?.repository ?: DriverRepository.entries.first())
        }
    }

    fun selectRepository(repository: DriverRepository) {
        _selectedRepository.value = repository
        loadReleases()
    }

    fun loadReleases() {
        val repository = _selectedRepository.value
        releasesJob?.cancel()
        _releasesState.value = ReleasesState.Loading
        releasesJob = viewModelScope.launch {
            _releasesState.value = when (val result = fetchDriverReleases(client, repository)) {
                is DriverReleasesResult.Success -> ReleasesState.Loaded(result.releases)
                is DriverReleasesResult.RateLimited -> ReleasesState.RateLimited(result.resetEpochSeconds)
                DriverReleasesResult.Error -> ReleasesState.Error
            }
        }
    }

    fun downloadAndInstall(asset: GitHubReleaseAsset, onFinished: (DriverInstallResult) -> Unit) {
        if (downloadJob?.isActive == true)
            return
        _download.value = DriverDownload(asset, null)
        downloadJob = viewModelScope.launch {
            val zipFile = File(getApplication<Application>().cacheDir, "driver-download.zip")
            try {
                val downloaded = withContext(Dispatchers.IO) { downloadToFile(asset, zipFile) }
                val result = if (downloaded) {
                    installDriverZip { zipFile.inputStream() }
                } else {
                    DriverInstallResult(DriverInstallStatus.DownloadFailed)
                }
                onFinished(result)
            } finally {
                withContext(NonCancellable + Dispatchers.IO) { zipFile.delete() }
                _download.value = null
            }
        }
    }

    private suspend fun downloadToFile(asset: GitHubReleaseAsset, file: File): Boolean = try {
        client.prepareGet(asset.downloadUrl).execute { response ->
            if (!response.status.isSuccess())
                return@execute false
            val totalBytes = response.contentLength()?.takeIf { it > 0 } ?: asset.size.takeIf { it > 0 }
            if (totalBytes != null && totalBytes > MAX_DRIVER_ZIP_SIZE)
                return@execute false
            val channel = response.bodyAsChannel()
            val buffer = ByteArray(64 * 1024)
            var bytesWritten = 0L
            file.outputStream().use { output ->
                while (true) {
                    val bytesRead = channel.readAvailable(buffer, 0, buffer.size)
                    if (bytesRead < 0)
                        break
                    output.write(buffer, 0, bytesRead)
                    bytesWritten += bytesRead
                    if (bytesWritten > MAX_DRIVER_ZIP_SIZE)
                        return@execute false
                    if (totalBytes != null)
                        _download.value = DriverDownload(asset, (bytesWritten.toFloat() / totalBytes).coerceIn(0f, 1f))
                }
            }
            true
        }
    } catch (exception: CancellationException) {
        throw exception
    } catch (_: Exception) {
        false
    }

    fun selectDriver(driverPath: String) {
        NativeSettings.setCustomDriverPath(driverPath)
    }

    override fun onCleared() {
        client.close()
    }
}
