package info.cemu.cemu.emulation.input

import android.view.KeyEvent
import android.view.MotionEvent
import info.cemu.cemu.common.android.inputevent.isFromPhysicalController
import info.cemu.cemu.nativeinterface.NativeInput

object InputHandler {
    // held keys and deflected axes per device descriptor, so they can be released when events stop being
    // forwarded (e.g. the in-game menu opens while a button is held, which would otherwise stay pressed)
    private val pressedKeys = mutableMapOf<String, MutableSet<Int>>()
    private val activeAxes = mutableMapOf<String, MutableSet<Int>>()

    fun onKeyEvent(event: KeyEvent): Boolean {
        if (!event.isFromPhysicalController()) {
            return false
        }

        // null when the device was disconnected meanwhile
        val device = event.device ?: return false
        val isPressed = event.action == KeyEvent.ACTION_DOWN

        val deviceKeys = pressedKeys.getOrPut(device.descriptor) { mutableSetOf() }
        if (isPressed) deviceKeys.add(event.keyCode) else deviceKeys.remove(event.keyCode)

        NativeInput.onControllerKey(device.descriptor, event.keyCode, isPressed)

        return true
    }

    fun onMotionEvent(event: MotionEvent): Boolean {
        if (!event.isFromPhysicalController()) {
            return false
        }

        val device = event.device ?: return false
        val deviceAxes = activeAxes.getOrPut(device.descriptor) { mutableSetOf() }
        val actionPointerIndex = event.actionIndex
        for (motionRange in device.motionRanges) {
            val axisValue = event.getAxisValue(motionRange.axis, actionPointerIndex)
            val axis = motionRange.axis
            if (axisValue != 0f) deviceAxes.add(axis) else deviceAxes.remove(axis)
            NativeInput.onControllerAxis(device.descriptor, axis, axisValue)
        }

        return true
    }

    /** Releases all held controller buttons and centers all axes. */
    fun releaseAll() {
        for ((descriptor, keys) in pressedKeys) {
            for (key in keys) {
                NativeInput.onControllerKey(descriptor, key, false)
            }
        }
        pressedKeys.clear()
        for ((descriptor, axes) in activeAxes) {
            for (axis in axes) {
                NativeInput.onControllerAxis(descriptor, axis, 0f)
            }
        }
        activeAxes.clear()
    }
}
