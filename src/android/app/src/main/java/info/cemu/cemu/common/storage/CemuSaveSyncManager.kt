package info.cemu.cemu.common.storage

import android.content.Context
import android.os.FileObserver
import info.cemu.cemu.common.settings.AppSettingsStore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File
import java.util.concurrent.ConcurrentHashMap

object CemuSaveSyncManager {
    private const val SYNC_DEBOUNCE_MS = 1500L
    private const val EVENT_MASK = FileObserver.CREATE or
        FileObserver.MODIFY or
        FileObserver.CLOSE_WRITE or
        FileObserver.DELETE or
        FileObserver.MOVED_FROM or
        FileObserver.MOVED_TO or
        FileObserver.DELETE_SELF or
        FileObserver.MOVE_SELF

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val pendingRelativePaths = linkedSetOf<String>()

    private var observer: RecursiveSaveObserver? = null
    private var flushJob: Job? = null

    /** Watches the mirror's saves while a game runs and exports changes (debounced). Not on the main thread. */
    suspend fun start(context: Context) {
        val settings = AppSettingsStore.dataStore.data.first().storageSettings
        synchronized(this) {
            val applicationContext = context.applicationContext
            observer?.stopWatching()

            if (settings.customRootUri.isNullOrBlank()) {
                observer = null
                return
            }

            val mirrorRoot = CemuDataStorage.getActiveRoot()
                ?: CemuDataStorage.resolveCurrentRoot(applicationContext, settings)
            val saveRoot = CemuDataStorage.saveRoot(mirrorRoot)
            saveRoot.mkdirs()
            observer = RecursiveSaveObserver(saveRoot) { relativePath ->
                enqueueChangedPath(applicationContext, relativePath)
            }.also { it.startWatching() }
        }
    }

    /** Stops watching; the caller then exports with SaveSyncCoordinator.flush. */
    fun stop() {
        synchronized(this) {
            observer?.stopWatching()
            observer = null
        }
    }

    /** Exports pending changes (or everything if the mirror is dirty). Call through SaveSyncCoordinator. */
    suspend fun flush(context: Context): Boolean {
        val (paths, keepDirtyAfterFlush) = synchronized(this) {
            flushJob?.cancel()
            flushJob = null
            val snapshot = pendingRelativePaths.toSet()
            pendingRelativePaths.clear()
            snapshot to (observer != null)
        }

        return withContext(Dispatchers.IO) {
            val storageSettings = AppSettingsStore.dataStore.data.first().storageSettings
            if (paths.isEmpty() && !storageSettings.isSaveMirrorDirty) {
                return@withContext true
            }
            if (paths.isNotEmpty()) {
                CemuDataStorage.markSavesDirty(context.applicationContext)
            }
            if (paths.isEmpty()) {
                CemuDataStorage.syncSavesToCustomRoot(
                    context = context.applicationContext,
                    force = true,
                    clearDirty = !keepDirtyAfterFlush,
                )
            } else {
                CemuDataStorage.exportSaveChangesToCustomRoot(
                    context = context.applicationContext,
                    changedSaveRelativePaths = paths,
                    clearDirty = !keepDirtyAfterFlush,
                )
            }
        }
    }

    private fun enqueueChangedPath(context: Context, relativePath: String) {
        if (relativePath.isBlank()) {
            return
        }
        synchronized(this) {
            pendingRelativePaths += relativePath
            flushJob?.cancel()
            flushJob = scope.launch {
                CemuDataStorage.markSavesDirty(context.applicationContext)
                delay(SYNC_DEBOUNCE_MS)
                val paths = synchronized(this@CemuSaveSyncManager) {
                    val snapshot = pendingRelativePaths.toSet()
                    pendingRelativePaths.clear()
                    flushJob = null
                    snapshot
                }
                if (paths.isNotEmpty()) {
                    SaveSyncCoordinator.runQuietly(context) {
                        CemuDataStorage.exportSaveChangesToCustomRoot(
                            context = it,
                            changedSaveRelativePaths = paths,
                            clearDirty = false,
                        )
                    }
                }
            }
        }
    }

    private class RecursiveSaveObserver(
        private val root: File,
        private val onChanged: (String) -> Unit,
    ) {
        // FileObserver events arrive on their own thread (watchDirectory on CREATE) while stopWatching runs on
        // the caller's thread
        private val observers = ConcurrentHashMap<String, FileObserver>()

        fun startWatching() {
            if (!root.exists()) {
                root.mkdirs()
            }
            root.walkTopDown()
                .filter { it.isDirectory }
                .forEach { watchDirectory(it) }
        }

        fun stopWatching() {
            observers.values.forEach { it.stopWatching() }
            observers.clear()
        }

        private fun watchDirectory(directory: File) {
            val canonicalPath = directory.absolutePath
            if (observers.containsKey(canonicalPath)) {
                return
            }

            val observer = object : FileObserver(directory, EVENT_MASK) {
                override fun onEvent(event: Int, path: String?) {
                    val changed = if (path.isNullOrBlank()) directory else directory.resolve(path)
                    if ((event and FileObserver.CREATE) != 0 && changed.isDirectory) {
                        watchDirectory(changed)
                    }
                    onChanged(changed.relativeToOrSelf(root).path.replace(File.separatorChar, '/'))
                }
            }
            if (observers.putIfAbsent(canonicalPath, observer) == null) {
                observer.startWatching()
            }
        }
    }
}
