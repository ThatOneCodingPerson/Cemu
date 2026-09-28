package info.cemu.cemu.emulation.input

import android.content.Context
import android.hardware.input.InputManager
import android.os.VibrationEffect
import android.view.InputDevice
import info.cemu.cemu.common.android.inputdevice.listGameControllers
import info.cemu.cemu.common.android.inputdevice.tryUseVibrator
import info.cemu.cemu.common.input.InputDeviceListener
import info.cemu.cemu.nativeinterface.NativeInput

class ControllerCallbacks(private val context: Context) : NativeInput.ControllerCallbacks {
    private val inputManager
        get() = context.getSystemService(Context.INPUT_SERVICE) as InputManager?

    // replaced (never mutated) on the main thread, read by the native rumble callbacks on another thread.
    // (synchronized(inputs) didn't work: it locked on the object the field is being replaced with)
    @Volatile
    private var inputs = mapOf<String, InputDevice>()

    private fun refreshControllers() {
        inputs = listGameControllers().associateBy { it.descriptor }
    }

    private val inputDeviceListener = object : InputDeviceListener {
        override fun onInputDeviceChanged() = refreshControllers()
    }

    fun register() {
        inputManager?.registerInputDeviceListener(inputDeviceListener, null)
        refreshControllers()
        NativeInput.setControllerCallbacks(this)
    }

    fun unregister() {
        inputManager?.unregisterInputDeviceListener(inputDeviceListener)
        NativeInput.setControllerCallbacks(null)
    }


    override fun vibrateController(
        descriptor: String, milliseconds: Long, amplitude: Int
    ) {
        inputs[descriptor]?.tryUseVibrator {
            vibrate(
                VibrationEffect.createOneShot(
                    milliseconds, amplitude.coerceIn(1, 255)
                )
            )
        }
    }

    override fun cancelControllerVibration(descriptor: String) {
        inputs[descriptor]?.tryUseVibrator { cancel() }
    }
}