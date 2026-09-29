package info.cemu.cemu.common.android.service

import android.app.Notification
import android.app.PendingIntent
import android.app.Service
import android.content.Context
import android.content.Intent
import android.content.pm.ServiceInfo
import android.os.Build
import android.os.IBinder
import android.util.Log
import androidx.core.app.NotificationChannelCompat
import androidx.core.app.NotificationCompat
import androidx.core.app.NotificationManagerCompat
import androidx.core.app.ServiceCompat
import androidx.core.content.ContextCompat
import info.cemu.cemu.R
import info.cemu.cemu.common.ui.localization.tr
import kotlinx.coroutines.Job
import kotlinx.coroutines.MainScope
import kotlinx.coroutines.cancel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

/**
 * Long tasks the user started (title install, WUA compression) that run in a view model coroutine. While any is
 * running, [ForegroundTaskService] keeps the process in the foreground, so Android doesn't kill it mid-copy when the
 * app is in the background.
 */
object ForegroundTasks {
    /** [progress] in 0..1, null if unknown. */
    data class Summary(val title: String, val progress: Float?, val taskCount: Int)

    class Task internal constructor(internal val id: Int, internal val title: String) {
        internal var percent: Int? = null

        /** [fraction] in 0..1, null if unknown. Cheap to call often, the notification only changes per percent. */
        fun setProgress(fraction: Float?) = updateProgress(this, fraction)

        /** Idempotent. */
        fun end() = endTask(this)
    }

    private val lock = Any()
    private val tasks = LinkedHashMap<Int, Task>()
    private var nextId = 0

    private val _summary = MutableStateFlow<Summary?>(null)
    val summary: StateFlow<Summary?> = _summary.asStateFlow()

    fun begin(context: Context, title: String): Task {
        val task = synchronized(lock) {
            Task(nextId++, title).also {
                tasks[it.id] = it
                publish()
            }
        }
        try {
            ContextCompat.startForegroundService(
                context.applicationContext,
                Intent(context.applicationContext, ForegroundTaskService::class.java),
            )
        } catch (exception: Exception) {
            // e.g. not allowed from the background: the task still runs, just without the protection
            Log.w("ForegroundTasks", "Can't start the foreground service: ${exception.message}")
        }
        return task
    }

    private fun updateProgress(task: Task, fraction: Float?) {
        val percent = fraction?.let { (it.coerceIn(0f, 1f) * 100).toInt() }
        synchronized(lock) {
            if (task.percent == percent || !tasks.containsKey(task.id))
                return
            task.percent = percent
            publish()
        }
    }

    private fun endTask(task: Task) = synchronized(lock) {
        if (tasks.remove(task.id) != null)
            publish()
    }

    // the newest task is shown
    private fun publish() {
        val task = tasks.values.lastOrNull()
        _summary.value = task?.let { Summary(it.title, it.percent?.let { percent -> percent / 100f }, tasks.size) }
    }
}

/**
 * Foreground service without work of its own: see [ForegroundTasks]. Type dataSync; apps targeting Android 15 get
 * 6 hours per 24 hours of it, then [onTimeout] must stop it within seconds.
 */
class ForegroundTaskService : Service() {
    private val scope = MainScope()
    private var summaryJob: Job? = null
    private var lastStartId = 0

    override fun onBind(intent: Intent?): IBinder? = null

    override fun onCreate() {
        super.onCreate()
        NotificationManagerCompat.from(this).createNotificationChannel(
            NotificationChannelCompat.Builder(CHANNEL_ID, NotificationManagerCompat.IMPORTANCE_LOW)
                .setName(tr("Long tasks"))
                .build()
        )
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        lastStartId = startId
        // required soon after startForegroundService, even if the task already ended
        showNotification(buildNotification(ForegroundTasks.summary.value))
        if (summaryJob == null) {
            summaryJob = scope.launch {
                ForegroundTasks.summary.collect { summary ->
                    if (summary == null) {
                        stop()
                    } else {
                        showNotification(buildNotification(summary))
                    }
                }
            }
        }
        return START_NOT_STICKY
    }

    override fun onTimeout(startId: Int, fgsType: Int) {
        // the tasks go on, just without the protection
        stop()
    }

    override fun onDestroy() {
        scope.cancel()
        super.onDestroy()
    }

    private fun stop() {
        ServiceCompat.stopForeground(this, ServiceCompat.STOP_FOREGROUND_REMOVE)
        // a newer start (a task begun meanwhile) keeps the service running
        stopSelfResult(lastStartId)
    }

    private fun showNotification(notification: Notification) {
        val type = if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q)
            ServiceInfo.FOREGROUND_SERVICE_TYPE_DATA_SYNC
        else 0
        try {
            ServiceCompat.startForeground(this, NOTIFICATION_ID, notification, type)
        } catch (exception: Exception) {
            Log.w("ForegroundTaskService", "Can't enter the foreground: ${exception.message}")
            stopSelfResult(lastStartId)
        }
    }

    private fun buildNotification(summary: ForegroundTasks.Summary?): Notification {
        val openApp = packageManager.getLaunchIntentForPackage(packageName)?.let {
            PendingIntent.getActivity(this, 0, it, PendingIntent.FLAG_IMMUTABLE)
        }
        val title = when {
            summary == null -> tr("Finishing")
            summary.taskCount > 1 -> tr("{0} (+{1} more)", summary.title, summary.taskCount - 1)
            else -> summary.title
        }
        val progress = summary?.progress
        return NotificationCompat.Builder(this, CHANNEL_ID)
            .setSmallIcon(R.drawable.ic_package_2)
            .setContentTitle(title)
            .setContentIntent(openApp)
            .setOngoing(true)
            .setOnlyAlertOnce(true)
            .setSilent(true)
            .setCategory(NotificationCompat.CATEGORY_PROGRESS)
            .setProgress(100, ((progress ?: 0f) * 100).toInt(), progress == null)
            .build()
    }

    companion object {
        private const val CHANNEL_ID = "long_tasks"
        private const val NOTIFICATION_ID = 1
    }
}
