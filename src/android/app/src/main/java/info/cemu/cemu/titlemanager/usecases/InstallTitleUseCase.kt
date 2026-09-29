package info.cemu.cemu.titlemanager.usecases

import android.content.ContentResolver
import android.content.Context
import android.net.Uri
import androidx.documentfile.provider.DocumentFile
import info.cemu.cemu.common.android.contentresolver.DocumentEntry
import info.cemu.cemu.common.android.contentresolver.walkDocumentTree
import info.cemu.cemu.common.android.service.ForegroundTasks
import info.cemu.cemu.common.io.copyInputStreamToFile
import info.cemu.cemu.common.string.urlDecode
import info.cemu.cemu.common.ui.localization.tr
import info.cemu.cemu.nativeinterface.NativeGameTitles
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.NonCancellable
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.coroutines.yield
import java.io.IOException
import java.nio.file.Path
import java.util.LinkedList
import kotlin.coroutines.cancellation.CancellationException
import kotlin.io.path.Path
import kotlin.io.path.createDirectories
import kotlin.math.max

enum class InstallResult{
    ERROR,
    FINISHED,
    NOT_ENOUGH_SPACE,
}

private sealed class DirEntry {
    data class File(val uri: Uri, val destinationPath: Path, val size: Long) : DirEntry()
    data class Dir(val destinationPath: Path) : DirEntry()
}

class InstallTitleUseCase(
    private val scope: CoroutineScope,
    private val mlcPath: Path
) {
    private val _inProgress = MutableStateFlow(false)
    val inProgress: StateFlow<Boolean> = _inProgress

    private val _progress = MutableStateFlow<Pair<Long, Long>?>(null)
    val progress: StateFlow<Pair<Long, Long>?> = _progress

    private var installJob: Job? = null

    fun cancel() {
        installJob?.cancel()
        installJob = null
    }

    fun install(
        context: Context,
        titleUri: Uri,
        targetLocation: String,
        callback: (InstallResult) -> Unit
    ) {
        if (_inProgress.value) return

        val installPath = Path(targetLocation)
        val installFile = installPath.toFile()
        val backupFile = installPath.getBackupFile()

        _progress.value = null
        _inProgress.value = true

        val oldInstallJob = installJob
        installJob = scope.launch(Dispatchers.IO) {
            var installStarted = false
            // keeps the process alive when the app goes to the background during the copy (K13)
            val foregroundTask = ForegroundTasks.begin(context, tr("Installing title"))

            try {
                oldInstallJob?.join()
                installMutex.withLock {
                    val contentResolver = context.contentResolver
                    val buffer = ByteArray(8192)

                    val (totalSize, entries) = listFilesInSourceDirs(
                        contentResolver = contentResolver,
                        titleDir = DocumentFile.fromTreeUri(context, titleUri)!!,
                        titleUri = titleUri,
                        targetLocation = targetLocation,
                    )

                    if (totalSize > mlcPath.toFile().freeSpace) {
                        callback(InstallResult.NOT_ENOUGH_SPACE)
                        return@launch
                    }

                    // Recoverable order, see recoverInterruptedInstall: existing title to .backup, the .installing
                    // marker, copy, remove the marker, remove the backup
                    recoverInterruptedInstall(installPath)
                    backupFile.deleteRecursively()
                    if (installFile.exists() && !installFile.renameTo(backupFile))
                        throw IOException("Can't move the installed title aside")
                    installStarted = true
                    if (!installPath.getInstallingMarkerFile().createNewFile())
                        throw IOException("Can't create the install marker")

                    _progress.value = 0L to totalSize
                    var bytesWritten = 0L

                    for (file in entries) {
                        yield()
                        when (file) {
                            is DirEntry.Dir -> file.destinationPath.createDirectories()
                            is DirEntry.File -> contentResolver.openInputStream(
                                file.uri
                            )?.use {
                                copyInputStreamToFile(it, file.destinationPath, buffer)
                                bytesWritten += file.size
                                _progress.value = bytesWritten to totalSize
                                foregroundTask.setProgress(bytesWritten.toFloat() / totalSize)
                            }
                        }
                    }

                    installPath.getInstallingMarkerFile().delete()
                    if (backupFile.exists())
                        backupFile.deleteRecursively()
                }

                NativeGameTitles.addTitleFromPath(targetLocation)

                callback(InstallResult.FINISHED)
            } catch (e: Exception) {
                // also when cancelled (e.g. the screen was closed), so it can't be a new job of the same scope
                if (installStarted)
                    withContext(NonCancellable) { installMutex.withLock { recoverInterruptedInstall(installPath) } }

                if (e !is CancellationException) callback(InstallResult.ERROR)
            } finally {
                foregroundTask.end()
                _inProgress.value = false
            }
        }
    }

    private suspend fun listFilesInSourceDirs(
        contentResolver: ContentResolver,
        titleDir: DocumentFile,
        titleUri: Uri,
        targetLocation: String,
    ): Pair<Long, LinkedList<DirEntry>> {
        val entries = LinkedList<DirEntry>()
        var totalSize = 0L

        for (sourceDir in SOURCE_DIRS) {
            val parentUri = titleDir.findFile(sourceDir)!!.uri
            val parentUriLength = titleUri.toString().length
            val uriToTargetPath: (Uri) -> Path = {
                val relativePath = it.toString().substring(parentUriLength).urlDecode()
                Path(targetLocation, relativePath)
            }

            entries += DirEntry.Dir(Path(targetLocation, sourceDir))
            contentResolver.walkDocumentTree(
                dirUri = parentUri,
                onEntry = {
                    when (it) {
                        is DocumentEntry.Directory -> {
                            entries += DirEntry.Dir(uriToTargetPath(it.uri))
                        }

                        is DocumentEntry.File -> {
                            totalSize += it.size
                            entries += DirEntry.File(
                                it.uri,
                                uriToTargetPath(it.uri),
                                it.size
                            )
                        }
                    }
                },
            )
        }

        totalSize = max(totalSize, 1L)

        return Pair(totalSize, entries)
    }

    companion object {
        private val SOURCE_DIRS = arrayOf("content", "code", "meta")
        private fun Path.getBackupFile() = resolveSibling("$fileName.backup").toFile()
        private fun Path.getInstallingMarkerFile() = resolveSibling("$fileName.installing").toFile()

        // one install or recovery at a time in the process (a new screen can start while an old install unwinds)
        private val installMutex = Mutex()

        /**
         * Brings a title folder back to a consistent state after an install that didn't finish (error,
         * cancellation, or the process was killed):
         * - marker present: the copy didn't finish. Remove the partial title, restore the backup if there is one.
         * - no marker, backup and title present: it finished, only the backup is left. Remove it.
         * - no marker, only the backup: killed right after moving the old title aside. Restore it.
         */
        private fun recoverInterruptedInstall(installPath: Path): Boolean {
            val installFile = installPath.toFile()
            val backupFile = installPath.getBackupFile()
            val markerFile = installPath.getInstallingMarkerFile()
            if (markerFile.exists()) {
                installFile.deleteRecursively()
                if (backupFile.exists())
                    backupFile.renameTo(installFile)
                markerFile.delete()
                return true
            }
            if (!backupFile.exists())
                return false
            if (installFile.exists())
                backupFile.deleteRecursively()
            else
                backupFile.renameTo(installFile)
            return true
        }

        /**
         * Repairs installs interrupted in an earlier session, see [recoverInterruptedInstall]. Does disk I/O.
         * Returns whether anything was repaired.
         */
        suspend fun recoverInterruptedInstalls(mlcPath: Path): Boolean = installMutex.withLock {
            var recovered = false
            for (titleRoot in listOf("usr/title", "sys/title")) {
                val titleTypeDirs = mlcPath.resolve(titleRoot).toFile().listFiles() ?: continue
                for (titleTypeDir in titleTypeDirs) {
                    val leftovers = titleTypeDir.listFiles { file ->
                        file.name.endsWith(".installing") || file.name.endsWith(".backup")
                    } ?: continue
                    leftovers
                        .map { it.name.substringBeforeLast('.') }
                        .distinct()
                        .forEach { recovered = recoverInterruptedInstall(titleTypeDir.toPath().resolve(it)) || recovered }
                }
            }
            recovered
        }
    }
}
