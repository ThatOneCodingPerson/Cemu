package info.cemu.cemu.common.emulation

import android.content.Context
import info.cemu.cemu.common.storage.CemuDataStorage
import info.cemu.cemu.common.storage.CemuSaveSyncManager
import info.cemu.cemu.common.storage.SaveSyncCoordinator
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import java.nio.channels.FileChannel
import java.nio.channels.FileLock
import java.nio.channels.OverlappingFileLockException
import java.util.concurrent.atomic.AtomicInteger

/**
 * Game sessions and their save sync. Nothing here blocks the main thread (D2); exports run through
 * SaveSyncCoordinator.
 */
object EmulationSessionState {
    private val activeSessions = AtomicInteger(0)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    // held by the process running a game, so the other process can see it (D7)
    private var sessionLockChannel: FileChannel? = null
    private var sessionLock: FileLock? = null

    private fun sessionLockFile(context: Context) = File(context.noBackupFilesDir, "emulation-session.lock")

    /** True while a game runs in this process or in the emulation process. */
    fun isEmulationRunning(context: Context): Boolean {
        if (activeSessions.get() > 0)
            return true
        return try {
            RandomAccessFile(sessionLockFile(context), "rw").channel.use { channel ->
                val lock = channel.tryLock() ?: return true
                lock.release()
                false
            }
        } catch (_: OverlappingFileLockException) {
            true
        } catch (_: IOException) {
            false
        }
    }

    /** [syncSaves] false: no game code runs (shader compiling), only the other process has to know about it. */
    fun onSessionStarted(context: Context, syncSaves: Boolean = true) {
        val applicationContext = context.applicationContext
        if (activeSessions.incrementAndGet() == 1)
            acquireSessionLock(applicationContext)
        if (!syncSaves)
            return
        scope.launch {
            // after this process's startup sync: a dirty mirror would make it export instead of import
            SaveSyncCoordinator.awaitStartupSync()
            CemuDataStorage.markSavesDirty(applicationContext)
            CemuSaveSyncManager.start(applicationContext)
        }
    }

    /** Exports the session's save changes and waits for it; for quitting with a progress dialog. */
    suspend fun finishSession(context: Context): Boolean {
        CemuSaveSyncManager.stop()
        return SaveSyncCoordinator.flush(context)
    }

    /** The activity went away without quitting: export in the background (kept alive by a foreground service). */
    fun onSessionStopped(context: Context, syncSaves: Boolean = true) {
        val remainingSessions = activeSessions.updateAndGet { count -> (count - 1).coerceAtLeast(0) }
        if (remainingSessions == 0) {
            releaseSessionLock()
            if (!syncSaves)
                return
            CemuSaveSyncManager.stop()
            SaveSyncCoordinator.flushInBackground(context)
        }
    }

    // a small local file, fast enough for the main thread
    private fun acquireSessionLock(context: Context) {
        try {
            val channel = RandomAccessFile(sessionLockFile(context), "rw").channel
            sessionLock = channel.tryLock()
            sessionLockChannel = channel
        } catch (_: Exception) {
            // only used to inform the other process
        }
    }

    private fun releaseSessionLock() {
        try {
            sessionLock?.release()
            sessionLockChannel?.close()
        } catch (_: IOException) {
        }
        sessionLock = null
        sessionLockChannel = null
    }
}
