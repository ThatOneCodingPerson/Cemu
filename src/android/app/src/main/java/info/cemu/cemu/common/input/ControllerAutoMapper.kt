package info.cemu.cemu.common.input

import android.view.InputDevice
import info.cemu.cemu.common.android.inputdevice.listGameControllers
import info.cemu.cemu.common.settings.AppSettings
import info.cemu.cemu.nativeinterface.NativeInput

/**
 * Makes a connected controller work without setup. Runs in the main process, which owns the controller profiles;
 * the emulation process loads them when a game starts.
 */
object ControllerAutoMapper {
    private const val CONTROLLER_INDEX = 0

    sealed interface Result {
        val deviceName: String

        /** Mapped by button names. */
        data class Mapped(override val deviceName: String) : Result

        /** The layout last used with this controller model was applied (ControllerModelProfiles). */
        data class ProfileApplied(override val deviceName: String) : Result
    }

    /** Gamepads, ones with sticks first. D-pad-only devices (some keyboards and remotes) are left to manual mapping. */
    private fun listGamepadsByPreference(): List<InputDevice> = listGameControllers()
        .filter { it.sources and InputDevice.SOURCE_GAMEPAD == InputDevice.SOURCE_GAMEPAD }
        .sortedByDescending { if (it.sources and InputDevice.SOURCE_JOYSTICK == InputDevice.SOURCE_JOYSTICK) 1 else 0 }

    /**
     * Without any configured controller (fresh install) controller 1 becomes a GamePad, which the input overlay
     * needs too. If [mapControllers]:
     * - when none of the devices controller 1 is mapped to is connected, a connected controller whose model has a
     *   saved layout gets it (see [chooseModelProfileTarget]);
     * - otherwise, while controller 1 has no mappings, a connected controller is mapped to it by button name.
     * Returns what was done to controller 1, null if nothing.
     */
    fun autoConfigure(mapControllers: Boolean, settings: AppSettings): Result? {
        var changed = false
        if ((0..<NativeInput.MAX_CONTROLLERS).all { NativeInput.isControllerDisabled(it) }) {
            NativeInput.setControllerType(CONTROLLER_INDEX, NativeInput.EmulatedControllerType.VPAD)
            changed = true
        }

        var result: Result? = null
        if (mapControllers && !NativeInput.isControllerDisabled(CONTROLLER_INDEX)) {
            val controllerType = NativeInput.getControllerType(CONTROLLER_INDEX)
            val mappedDescriptors = NativeInput.getControllerMappedDescriptors(CONTROLLER_INDEX).toList()
            val hasMappings = NativeInput.getControllerMappings(CONTROLLER_INDEX).isNotEmpty()
            val gamepads = listGamepadsByPreference()
            // mappings to something other than an Android input device are never replaced
            val target = if (mappedDescriptors.isNotEmpty() || !hasMappings) {
                chooseModelProfileTarget(
                    mappedDescriptors = mappedDescriptors,
                    connected = gamepads.map { it.toConnectedController() },
                    controllerType = controllerType,
                    profiles = settings.controllerModelProfiles,
                    knownModels = settings.knownControllerModels,
                )
            } else {
                null
            }
            val profile = target?.modelKey?.let { settings.controllerModelProfiles[profileKey(it, controllerType)] }
            if (target != null && profile != null) {
                ControllerModelProfiles.apply(CONTROLLER_INDEX, target, profile)
                result = Result.ProfileApplied(target.name)
            } else if (!hasMappings) {
                val device = gamepads.firstOrNull()
                if (device != null && InputMapper.mapAllInputs(device.id, CONTROLLER_INDEX) > 0) {
                    result = Result.Mapped(device.name)
                }
            }
            if (result != null) {
                changed = true
            }
        }

        if (changed) {
            NativeInput.saveInputs()
        }
        if (result != null) {
            // this device's model now has (or keeps) a profile, and the device is known
            ControllerModelProfiles.record(CONTROLLER_INDEX)
        }
        return result
    }
}
