package info.cemu.cemu.common.settings

import kotlinx.serialization.Serializable

@Serializable
data class HotkeyCombo(
    val keys: Set<Int>
)

// stored by name in the settings, so only append (a removed name makes older settings files unreadable)
enum class HotkeyAction {
    QUIT,
    TOGGLE_MENU,
    SHOW_EMULATED_USB_DEVICES_DIALOG,
    TOGGLE_PAUSE,
    TAKE_SCREENSHOT,
    SWAP_SCREENS,
    TOGGLE_PAD,
    TOGGLE_INPUT_OVERLAY,
    SCAN_AMIIBO,
}