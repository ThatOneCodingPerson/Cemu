package info.cemu.cemu.emulation

import android.content.BroadcastReceiver
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.os.BatteryManager
import android.os.PowerManager
import androidx.core.content.ContextCompat
import info.cemu.cemu.nativeinterface.NativeEmulation

/**
 * Reports the battery level/temperature and the thermal status (throttling) to the performance overlay. Registered
 * while the emulation activity is resumed.
 */
class DeviceStatusMonitor(private val context: Context) {
    private val powerManager = context.getSystemService(PowerManager::class.java)

    private var batteryPercent = -1
    private var isCharging = false
    private var batteryTemperatureTenths = Int.MIN_VALUE
    private var thermalStatus = -1

    private val batteryReceiver = object : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) = onBatteryChanged(intent)
    }

    private val thermalListener = PowerManager.OnThermalStatusChangedListener { status ->
        thermalStatus = status
        publish()
    }

    fun register() {
        // sticky: returns the current state right away
        ContextCompat.registerReceiver(
            context,
            batteryReceiver,
            IntentFilter(Intent.ACTION_BATTERY_CHANGED),
            ContextCompat.RECEIVER_NOT_EXPORTED,
        )?.let { onBatteryChanged(it) }
        powerManager?.let {
            thermalStatus = it.currentThermalStatus
            it.addThermalStatusListener(ContextCompat.getMainExecutor(context), thermalListener)
        }
        publish()
    }

    fun unregister() {
        context.unregisterReceiver(batteryReceiver)
        powerManager?.removeThermalStatusListener(thermalListener)
    }

    private fun onBatteryChanged(intent: Intent) {
        val level = intent.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
        val scale = intent.getIntExtra(BatteryManager.EXTRA_SCALE, -1)
        batteryPercent = if (level >= 0 && scale > 0) level * 100 / scale else -1
        val status = intent.getIntExtra(BatteryManager.EXTRA_STATUS, -1)
        isCharging = status == BatteryManager.BATTERY_STATUS_CHARGING || status == BatteryManager.BATTERY_STATUS_FULL
        // tenths of a degree Celsius
        batteryTemperatureTenths = intent.getIntExtra(BatteryManager.EXTRA_TEMPERATURE, Int.MIN_VALUE)
        publish()
    }

    private fun publish() {
        NativeEmulation.setDeviceStatus(batteryPercent, isCharging, batteryTemperatureTenths, thermalStatus)
    }
}
