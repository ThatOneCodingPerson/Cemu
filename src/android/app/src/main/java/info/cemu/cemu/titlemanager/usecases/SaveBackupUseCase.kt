package info.cemu.cemu.titlemanager.usecases

import android.content.Context
import android.net.Uri
import info.cemu.cemu.common.io.unzip
import info.cemu.cemu.common.storage.CemuDataStorage
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

enum class SaveImportResult {
    IMPORTED,
    NOT_A_SAVE_BACKUP,
    ERROR,
}

/**
 * Exports a title's save folder (mlc01/usr/save/<high>/<low>, containing "user" and "meta") to a zip and imports
 * such a zip back. The game must not be running meanwhile.
 */
object SaveBackupUseCase {
    suspend fun export(context: Context, savePath: String, targetUri: Uri): Boolean = withContext(Dispatchers.IO) {
        val saveDir = File(savePath)
        if (!saveDir.isDirectory)
            return@withContext false
        try {
            context.contentResolver.openOutputStream(targetUri, "wt")?.use { output ->
                ZipOutputStream(output.buffered()).use { zip ->
                    saveDir.walkTopDown().filter { it != saveDir }.forEach { file ->
                        val relativePath = file.relativeTo(saveDir).invariantSeparatorsPath
                        if (file.isDirectory) {
                            zip.putNextEntry(ZipEntry("$relativePath/"))
                            zip.closeEntry()
                        } else {
                            zip.putNextEntry(ZipEntry(relativePath).apply { time = file.lastModified() })
                            file.inputStream().use { it.copyTo(zip) }
                            zip.closeEntry()
                        }
                    }
                }
                true
            } ?: false
        } catch (_: Exception) {
            false
        }
    }

    /**
     * Replaces the save folder with the zip's content. The current save is kept until the new one is in place, and
     * is restored if that fails.
     */
    suspend fun import(context: Context, savePath: String, sourceUri: Uri): SaveImportResult =
        withContext(Dispatchers.IO) {
            val saveDir = File(savePath)
            val stagingDir = File(saveDir.parentFile, "${saveDir.name}.import")
            val backupDir = File(saveDir.parentFile, "${saveDir.name}.import-backup")
            try {
                stagingDir.deleteRecursively()
                stagingDir.mkdirs()
                context.contentResolver.openInputStream(sourceUri)?.use { unzip(it, stagingDir.toPath()) }
                    ?: return@withContext SaveImportResult.ERROR

                val importedRoot = findSaveRoot(stagingDir)
                    ?: return@withContext SaveImportResult.NOT_A_SAVE_BACKUP

                backupDir.deleteRecursively()
                if (saveDir.exists() && !saveDir.renameTo(backupDir))
                    return@withContext SaveImportResult.ERROR
                if (!importedRoot.renameTo(saveDir)) {
                    backupDir.renameTo(saveDir)
                    return@withContext SaveImportResult.ERROR
                }
                backupDir.deleteRecursively()
                // with a custom data root the next sync must export this save instead of importing the old one
                CemuDataStorage.markSavesDirty(context)
                SaveImportResult.IMPORTED
            } catch (_: Exception) {
                if (!saveDir.exists() && backupDir.exists())
                    backupDir.renameTo(saveDir)
                SaveImportResult.ERROR
            } finally {
                stagingDir.deleteRecursively()
            }
        }

    // the zip holds "user"/"meta" at its root, or inside one folder (e.g. the title's save folder zipped by hand)
    private fun findSaveRoot(dir: File): File? {
        fun isSaveRoot(candidate: File) =
            File(candidate, "user").isDirectory || File(candidate, "meta").isDirectory
        if (isSaveRoot(dir))
            return dir
        val children = dir.listFiles() ?: return null
        return children.singleOrNull()?.takeIf { it.isDirectory && isSaveRoot(it) }
    }
}
