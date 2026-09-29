package info.cemu.cemu.common.input

import android.view.InputDevice
import info.cemu.cemu.common.android.inputdevice.listGameControllers
import info.cemu.cemu.nativeinterface.NativeInput

/**
 * Makes a connected controller work without setup. Runs in the main process, which owns the controller profiles;
 * the emulation process loads them when a game starts.
 */
object ControllerAutoMapper {
    private const val CONTROLLER_INDEX = 0

    /**
     * The controller to map: a gamepad, one with sticks first. D-pad-only devices (some keyboards and remotes) are
     * left to manual mapping.
     */
    fun pickPrimaryGameController(): InputDevice? = listGameControllers()
        .filter { it.sources and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD }
        .maxByOrNull { if (it.sources and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK) 1 else 0 }

    /**
     * Without any configured controller (fresh install) controller 1 becomes a GamePad, which the input overlay
     * needs too. If [mapControllers] and controller 1 has no mappings, a connected controller is mapped to it by
     * button name. Returns the name of the mapped controller, null if none was mapped.
     */
    fun autoConfigure(mapControllers: Boolean): String? {
        var changed = false
        if ((0..<NativeInput.MAX_CONTROLLERS).all { NativeInput.isControllerDisabled(it) }) {
            NativeInput.setControllerType(CONTROLLER_INDEX, NativeInput.EmulatedControllerType.VPAD)
            changed = true
        }

        var mappedDeviceName: String? = null
        if (mapControllers
            && !NativeInput.isControllerDisabled(CONTROLLER_INDEX)
            && NativeInput.getControllerMappings(CONTROLLER_INDEX).isEmpty()
        ) {
            val device = pickPrimaryGameController()
            if (device != null && InputMapper.mapAllInputs(device.id, CONTROLLER_INDEX) > 0) {
                mappedDeviceName = device.name
                changed = true
            }
        }

        if (changed) {
            NativeInput.saveInputs()
        }
        return mappedDeviceName
    }
}
