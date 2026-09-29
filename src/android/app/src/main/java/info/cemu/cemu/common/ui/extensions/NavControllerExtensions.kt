package info.cemu.cemu.common.ui.extensions

import androidx.navigation.NavController

/**
 * Navigates back unless the current destination is the first one. A quick second tap on a back button while the
 * previous pop is still animating would otherwise pop the start destination and leave a blank screen.
 */
fun NavController.navigateBackSafely() {
    if (previousBackStackEntry != null) {
        popBackStack()
    }
}
