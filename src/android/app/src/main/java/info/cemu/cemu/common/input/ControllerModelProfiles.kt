package info.cemu.cemu.common.input

import android.view.InputDevice
import info.cemu.cemu.common.android.inputdevice.listGameControllers
import info.cemu.cemu.common.settings.AppSettingsStore
import info.cemu.cemu.common.settings.ControllerModelProfile
import info.cemu.cemu.nativeinterface.NativeInput
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * Controller model profiles: the mappings last used with a controller model (vendor:product) are remembered, so a
 * different unit of that model, or a model used before, works right away. Mappings are bound to one unit's
 * InputDevice.descriptor; applying a profile re-targets them to the connected unit. Main process only, it owns the
 * controller configuration.
 */
object ControllerModelProfiles {
    // outlives the settings screen that triggers recording
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    /** Remembers controller [controllerIndex]'s mappings for the model of every connected device it uses. */
    fun record(controllerIndex: Int) {
        val controllerType = NativeInput.getControllerType(controllerIndex)
        if (controllerType == NativeInput.EmulatedControllerType.DISABLED) {
            return
        }
        val connectedDevices = listGameControllers().associateBy { it.descriptor }
        val recorded = NativeInput.getControllerMappedDescriptors(controllerIndex).mapNotNull { descriptor ->
            val device = connectedDevices[descriptor] ?: return@mapNotNull null
            val model = device.modelKey() ?: return@mapNotNull null
            val buttons = NativeInput.getControllerMappingButtons(controllerIndex, descriptor).toButtonMap()
            if (buttons.isEmpty()) {
                return@mapNotNull null
            }
            RecordedProfile(descriptor, model, ControllerModelProfile(buttons, device.name))
        }
        if (recorded.isEmpty()) {
            return
        }
        scope.launch {
            AppSettingsStore.dataStore.updateData { settings ->
                settings.copy(
                    controllerModelProfiles = settings.controllerModelProfiles +
                        recorded.associate { profileKey(it.model, controllerType) to it.profile },
                    knownControllerModels = settings.knownControllerModels +
                        recorded.associate { it.descriptor to it.model },
                )
            }
        }
    }

    /** Replaces controller [controllerIndex]'s mappings with [profile], for [target]. */
    fun apply(controllerIndex: Int, target: ConnectedController, profile: ControllerModelProfile) {
        val pairs = profile.buttons.entries.flatMap { (mapping, button) -> listOf(mapping, button) }.toLongArray()
        NativeInput.applyControllerMappings(controllerIndex, target.descriptor, target.name, pairs)
    }

    private data class RecordedProfile(val descriptor: String, val model: String, val profile: ControllerModelProfile)
}

data class ConnectedController(val descriptor: String, val name: String, val modelKey: String?)

/** "vendor:product" in hex, null if the device doesn't report them (virtual or built-in devices may not). */
fun InputDevice.modelKey(): String? =
    if (vendorId == 0 && productId == 0) null else "%04x:%04x".format(vendorId, productId)

fun InputDevice.toConnectedController() = ConnectedController(descriptor, name, modelKey())

fun profileKey(modelKey: String, controllerType: Int) = "$modelKey:$controllerType"

private fun LongArray.toButtonMap(): Map<Long, Long> =
    (0 until size / 2).associate { this[it * 2] to this[it * 2 + 1] }

/**
 * The connected controller that controller 1 should switch to with its model's saved profile, or null to leave the
 * mappings alone. Switches only when none of the devices it is mapped to is connected, and only if each of those can
 * get its mappings back later (its model is known and has a profile for this controller type), so nothing is lost.
 * [connected] is in order of preference.
 */
fun chooseModelProfileTarget(
    mappedDescriptors: List<String>,
    connected: List<ConnectedController>,
    controllerType: Int,
    profiles: Map<String, ControllerModelProfile>,
    knownModels: Map<String, String>,
): ConnectedController? {
    if (connected.any { it.descriptor in mappedDescriptors }) {
        return null
    }
    val canRestoreMappedDevices = mappedDescriptors.all { descriptor ->
        val model = knownModels[descriptor]
        model != null && profileKey(model, controllerType) in profiles
    }
    if (!canRestoreMappedDevices) {
        return null
    }
    return connected.firstOrNull { it.modelKey != null && profileKey(it.modelKey, controllerType) in profiles }
}
