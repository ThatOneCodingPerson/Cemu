package info.cemu.cemu.tests

import info.cemu.cemu.common.input.ConnectedController
import info.cemu.cemu.common.input.chooseModelProfileTarget
import info.cemu.cemu.common.input.profileKey
import info.cemu.cemu.common.settings.ControllerModelProfile
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

private const val VPAD = 0
private const val XBOX = "045e:0b13"
private const val DUALSENSE = "054c:0ce6"

private val XBOX_UNIT_1 = ConnectedController("xbox-1", "Xbox Wireless Controller", XBOX)
private val XBOX_UNIT_2 = ConnectedController("xbox-2", "Xbox Wireless Controller", XBOX)
private val DUALSENSE_UNIT = ConnectedController("ds-1", "DualSense Wireless Controller", DUALSENSE)
private val NO_IDS = ConnectedController("builtin", "gpio-keys", null)

private val PROFILE = ControllerModelProfile(mapOf(1L to 96L), "x")

class ControllerModelProfilesTests {
    private val profiles = mapOf(profileKey(XBOX, VPAD) to PROFILE, profileKey(DUALSENSE, VPAD) to PROFILE)
    private val knownModels = mapOf("xbox-1" to XBOX, "ds-1" to DUALSENSE)

    private fun choose(mapped: List<String>, connected: List<ConnectedController>) =
        chooseModelProfileTarget(mapped, connected, VPAD, profiles, knownModels)

    @Test
    fun keepsTheMappingsWhileAMappedDeviceIsConnected() {
        assertNull(choose(listOf("xbox-1"), listOf(DUALSENSE_UNIT, XBOX_UNIT_1)))
    }

    @Test
    fun anotherUnitOfTheSameModelGetsItsLayout() {
        assertEquals(XBOX_UNIT_2, choose(listOf("xbox-1"), listOf(XBOX_UNIT_2)))
    }

    @Test
    fun switchesToAnotherKnownModel() {
        assertEquals(DUALSENSE_UNIT, choose(listOf("xbox-1"), listOf(DUALSENSE_UNIT)))
    }

    @Test
    fun takesTheFirstConnectedDeviceThatHasAProfile() {
        assertEquals(DUALSENSE_UNIT, choose(listOf("xbox-1"), listOf(NO_IDS, DUALSENSE_UNIT, XBOX_UNIT_2)))
    }

    @Test
    fun neverDropsMappingsThatCouldNotBeRestored() {
        // the mapped device's model was never recorded
        assertNull(choose(listOf("unknown-pad"), listOf(DUALSENSE_UNIT)))
        // its model has no profile for this controller type
        assertNull(chooseModelProfileTarget(listOf("xbox-1"), listOf(DUALSENSE_UNIT), 1, profiles, knownModels))
    }

    @Test
    fun devicesWithoutAProfileAreNotChosen() {
        assertNull(choose(listOf("xbox-1"), listOf(NO_IDS)))
        assertNull(choose(emptyList(), emptyList()))
    }

    @Test
    fun unmappedControllerGetsTheSavedLayout() {
        assertEquals(XBOX_UNIT_2, choose(emptyList(), listOf(XBOX_UNIT_2)))
    }
}
