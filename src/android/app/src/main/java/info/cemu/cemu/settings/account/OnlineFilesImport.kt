package info.cemu.cemu.settings.account

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import info.cemu.cemu.nativeinterface.NativeActiveSettings
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import java.io.File

/** Console files needed for online play, recognized by their size like the core checks them (iosu_crypto.cpp). */
enum class OnlineFileKind(val fileName: String, val size: Int) {
    OTP("otp.bin", 1024),
    SEEPROM("seeprom.bin", 512),
}

internal fun onlineFileKindForSize(size: Int): OnlineFileKind? = OnlineFileKind.entries.firstOrNull { it.size == size }

sealed interface OnlineFileImportResult {
    data class Imported(val kind: OnlineFileKind) : OnlineFileImportResult
    data class Unrecognized(val name: String) : OnlineFileImportResult
    data object Error : OnlineFileImportResult
}

private const val MAX_ONLINE_FILE_SIZE = 4096

/**
 * Copies a picked otp.bin or seeprom.bin into the user data folder, which a file manager often can't reach
 * (Android/data). The file is recognized by its size, so its name doesn't matter.
 */
suspend fun importOnlineFile(context: Context, uri: Uri): OnlineFileImportResult = withContext(Dispatchers.IO) {
    try {
        val bytes = context.contentResolver.openInputStream(uri)?.use { input ->
            val output = ByteArrayOutputStream()
            val chunk = ByteArray(4096)
            while (true) {
                val count = input.read(chunk)
                if (count < 0)
                    break
                if (output.size() + count > MAX_ONLINE_FILE_SIZE)
                    return@withContext OnlineFileImportResult.Unrecognized(displayName(context, uri) ?: "?")
                output.write(chunk, 0, count)
            }
            output.toByteArray()
        } ?: return@withContext OnlineFileImportResult.Error

        val kind = onlineFileKindForSize(bytes.size)
            ?: return@withContext OnlineFileImportResult.Unrecognized(displayName(context, uri) ?: "?")

        val target = File(NativeActiveSettings.getUserDataPath(), kind.fileName)
        val tempFile = File(target.parentFile, "${kind.fileName}.tmp")
        tempFile.writeBytes(bytes)
        if (!tempFile.renameTo(target)) {
            tempFile.delete()
            return@withContext OnlineFileImportResult.Error
        }
        OnlineFileImportResult.Imported(kind)
    } catch (_: Exception) {
        OnlineFileImportResult.Error
    }
}

private fun displayName(context: Context, uri: Uri): String? = try {
    context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
        ?.use { cursor -> if (cursor.moveToFirst()) cursor.getString(0) else null }
} catch (_: Exception) {
    null
}
