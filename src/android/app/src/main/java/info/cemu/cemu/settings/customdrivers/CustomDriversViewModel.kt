package info.cemu.cemu.settings.customdrivers

import android.content.Context
import android.net.Uri
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import info.cemu.cemu.common.customdrivers.DriverMetadata
import info.cemu.cemu.common.customdrivers.parseInstalledDrivers
import info.cemu.cemu.nativeinterface.NativeSettings
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.launch
import kotlin.io.path.Path

data class Driver(
    val path: String,
    val metadata: DriverMetadata,
    val selected: Boolean = false,
)

class CustomDriversViewModel : ViewModel() {
    private val selectedDriverPath = MutableStateFlow(NativeSettings.getCustomDriverPath())
    val isSystemDriverSelected = selectedDriverPath.map { it == null }.stateIn(
        viewModelScope,
        SharingStarted.WhileSubscribed(5000),
        false
    )

    private val _installedDrivers = MutableStateFlow<List<Driver>>(emptyList())
    val installedDrivers = _installedDrivers.asStateFlow()

    /**
     * Reads the installed drivers and the selection. Called whenever the screen is shown, the download screen may
     * have installed or selected a driver.
     */
    fun refresh() {
        viewModelScope.launch {
            selectedDriverPath.value = NativeSettings.getCustomDriverPath()
            val selectedDriver = selectedDriverPath.value

            _installedDrivers.value = parseInstalledDrivers().map {
                Driver(
                    it.path,
                    it.metadata,
                    selected = selectedDriver == it.path,
                )
            }
        }
    }

    private val _isDriverInstallInProgress = MutableStateFlow(false)
    val isDriverInstallInProgress = _isDriverInstallInProgress.asStateFlow()

    fun installDriver(
        context: Context,
        driverZipUri: Uri,
        onInstallFinished: (DriverInstallStatus) -> Unit,
    ) {
        _isDriverInstallInProgress.value = true
        viewModelScope.launch {
            try {
                val result = installDriverZip { context.contentResolver.openInputStream(driverZipUri) }
                if (result.status == DriverInstallStatus.Installed) {
                    refresh()
                }
                onInstallFinished(result.status)
            } finally {
                _isDriverInstallInProgress.value = false
            }
        }
    }

    fun deleteDriver(driver: Driver) {
        if (!_installedDrivers.value.any { it == driver })
            return

        _installedDrivers.value -= driver
        if (selectedDriverPath.value == driver.path) {
            selectedDriverPath.value = null
            NativeSettings.setCustomDriverPath(null)
        }

        viewModelScope.launch(Dispatchers.IO) {
            Path(driver.path).toFile().deleteRecursively()
        }
    }

    fun setSystemDriverSelected() {
        if (selectedDriverPath.value == null)
            return

        val installedDrivers = _installedDrivers.value.toMutableList()
        val oldSelectedDriverIndex = installedDrivers.indexOfFirst { it.selected }
        if (oldSelectedDriverIndex != -1) {
            installedDrivers[oldSelectedDriverIndex] =
                installedDrivers[oldSelectedDriverIndex].copy(selected = false)
            _installedDrivers.value = installedDrivers
        }

        selectedDriverPath.value = null
        NativeSettings.setCustomDriverPath(null)
    }

    fun setDriverSelected(driver: Driver) {
        if (selectedDriverPath.value == driver.path)
            return

        val installedDrivers = _installedDrivers.value.toMutableList()

        val oldSelectedDriverIndex = installedDrivers.indexOfFirst { it.selected }
        if (oldSelectedDriverIndex != -1)
            installedDrivers[oldSelectedDriverIndex] =
                installedDrivers[oldSelectedDriverIndex].copy(selected = false)

        val newSelectedDriverIndex = installedDrivers.indexOf(driver)
        if (newSelectedDriverIndex == -1)
            return
        installedDrivers[newSelectedDriverIndex] = driver.copy(selected = true)

        _installedDrivers.value = installedDrivers

        NativeSettings.setCustomDriverPath(driver.path)
        selectedDriverPath.value = driver.path
    }
}