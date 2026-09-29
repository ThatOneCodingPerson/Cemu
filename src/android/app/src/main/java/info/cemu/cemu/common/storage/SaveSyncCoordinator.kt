package info.cemu.cemu.common.storage

import android.content.Context
import info.cemu.cemu.common.android.service.ForegroundTasks
import info.cemu.cemu.common.settings.AppSettingsStore
import info.cemu.cemu.common.ui.localization.tr
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.io.File
import java.io.RandomAccessFile

/**
 * Runs every save sync between the local mirror and the custom root (SAF) off the main thread (D1/D2), one at a time
 * across both processes, and publishes whether one runs so the UI can hold back game launches meanwhile.
 *
 * Why an interrupted sync is safe: while the mirror is marked dirty it is the source of truth, and the next startup
 * sync exports a dirty mirror instead of importing over it.
 */
object SaveSyncCoordinator {
    sealed interface State {
        data object Idle : State
        data class Syncing(val message: String) : State
    }

    private val _state = MutableStateFlow<State>(State.Idle)
    val state: StateFlow<State> = _state.asStateFlow()

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val processMutex = Mutex()
    private var startupSyncJob: Job? = null

    /**
     * Once per process, before any game runs: exports a dirty mirror or imports the custom root. The main process
     * runs it at start; the emulation process only when it was started without the main process (shortcut).
     */
    fun startStartupSync(context: Context) {
        val applicationContext = context.applicationContext
        synchronized(this) {
            if (startupSyncJob != null)
                return
            startupSyncJob = scope.launch {
                runSync(applicationContext, tr("Syncing saves…")) { CemuDataStorage.syncAtStartup(it) }
            }
        }
    }

    /** Returns when the startup sync of this process finished, right away if there is none. */
    suspend fun awaitStartupSync() {
        synchronized(this) { startupSyncJob }?.join()
    }

    val isSyncing: Boolean
        get() = _state.value is State.Syncing

    /** Exports pending save changes and waits for it. False if it failed (the mirror stays dirty then). */
    suspend fun flush(context: Context): Boolean =
        runSync(context.applicationContext, tr("Saving…")) { CemuSaveSyncManager.flush(it) } ?: true

    /** Like [flush], in the background; a foreground service keeps the process alive until it's done. */
    fun flushInBackground(context: Context) {
        val applicationContext = context.applicationContext
        scope.launch {
            if (!hasCustomRoot())
                return@launch
            val task = ForegroundTasks.begin(applicationContext, tr("Syncing saves"))
            try {
                flush(applicationContext)
            } finally {
                task.end()
            }
        }
    }

    /** For syncs started elsewhere (the save observer's debounced export): serialized like the others. */
    suspend fun <T> runQuietly(context: Context, block: suspend (Context) -> T): T? =
        runSync(context.applicationContext, message = null, block)

    private suspend fun hasCustomRoot() =
        CemuDataStorage.hasCustomRoot(AppSettingsStore.dataStore.data.first().storageSettings)

    // null if there is no custom root (nothing to sync, no UI)
    private suspend fun <T> runSync(context: Context, message: String?, block: suspend (Context) -> T): T? {
        if (!hasCustomRoot())
            return null
        return processMutex.withLock {
            if (message != null)
                _state.value = State.Syncing(message)
            try {
                withCrossProcessLock(context) { block(context) }
            } finally {
                if (message != null)
                    _state.value = State.Idle
            }
        }
    }

    // Both processes sync the same folders. The lock is released by the system if a process dies; within this
    // process processMutex keeps a second lock attempt away (that would throw OverlappingFileLockException).
    private suspend fun <T> withCrossProcessLock(context: Context, block: suspend () -> T): T =
        withContext(Dispatchers.IO) {
            RandomAccessFile(File(context.noBackupFilesDir, "save-sync.lock"), "rw").channel.use { channel ->
                channel.lock().use { block() }
            }
        }
}
