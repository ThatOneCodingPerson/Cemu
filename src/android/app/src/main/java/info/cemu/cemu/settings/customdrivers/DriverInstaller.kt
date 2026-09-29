@file:OptIn(ExperimentalPathApi::class, ExperimentalUuidApi::class)

package info.cemu.cemu.settings.customdrivers

import android.os.Build
import info.cemu.cemu.common.customdrivers.DriverMetadata
import info.cemu.cemu.common.customdrivers.META_FILE_NAME
import info.cemu.cemu.common.customdrivers.SUPPORTED_SCHEMA_VERSION
import info.cemu.cemu.common.customdrivers.getCustomDriversDir
import info.cemu.cemu.common.customdrivers.parseInstalledDrivers
import info.cemu.cemu.common.io.decodeJsonFromFile
import info.cemu.cemu.common.io.unzip
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeActiveSettings
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.InputStream
import kotlin.io.path.ExperimentalPathApi
import kotlin.io.path.Path
import kotlin.io.path.createDirectories
import kotlin.io.path.deleteRecursively
import kotlin.io.path.exists
import kotlin.io.path.moveTo
import kotlin.uuid.ExperimentalUuidApi
import kotlin.uuid.Uuid

enum class DriverInstallStatus {
    Installed,
    AlreadyInstalled,
    ErrorInstalling,
    RequiresNewerAndroid,
    DownloadFailed,
}

/** [driverPath] is set when the driver was installed. */
data class DriverInstallResult(val status: DriverInstallStatus, val driverPath: String? = null)

/**
 * Installs an adrenotools driver zip (meta.json + library) into the custom drivers folder. Used for zips picked by
 * the user and for downloaded ones.
 */
suspend fun installDriverZip(openZip: () -> InputStream?): DriverInstallResult = withContext(Dispatchers.IO) {
    val tempDir = Path(NativeActiveSettings.getUserDataPath()).resolve(Uuid.random().toString())

    fun fail(status: DriverInstallStatus): DriverInstallResult {
        tempDir.deleteRecursively()
        return DriverInstallResult(status)
    }

    try {
        tempDir.createDirectories()

        openZip()?.use {
            unzip(it, tempDir)
        }

        val metadata =
            decodeJsonFromFile<DriverMetadata>(tempDir.resolve(META_FILE_NAME).toFile())
        if (metadata == null
            || metadata.schemaVersion != SUPPORTED_SCHEMA_VERSION
            || !tempDir.resolve(metadata.libraryName).exists()
        ) {
            return@withContext fail(DriverInstallStatus.ErrorInstalling)
        }

        if (metadata.minApi > Build.VERSION.SDK_INT) {
            return@withContext fail(DriverInstallStatus.RequiresNewerAndroid)
        }

        if (parseInstalledDrivers().any { it.metadata == metadata }) {
            return@withContext fail(DriverInstallStatus.AlreadyInstalled)
        }

        val customDriversDir = getCustomDriversDir()
        customDriversDir.createDirectories()
        val driverPath = tempDir.moveTo(customDriversDir.resolve(tempDir.fileName))

        DriverInstallResult(DriverInstallStatus.Installed, driverPath.toString())
    } catch (exception: CancellationException) {
        tempDir.deleteRecursively()
        throw exception
    } catch (_: Exception) {
        fail(DriverInstallStatus.ErrorInstalling)
    }
}

fun driverInstallStatusMessage(status: DriverInstallStatus): String = when (status) {
    DriverInstallStatus.AlreadyInstalled -> tr("Driver already installed")
    DriverInstallStatus.ErrorInstalling -> tr("Failed to install driver")
    DriverInstallStatus.RequiresNewerAndroid -> tr("This driver needs a newer Android version")
    DriverInstallStatus.DownloadFailed -> tr("Download failed. Check the internet connection.")
    DriverInstallStatus.Installed -> tr("Driver installed successfully")
}
