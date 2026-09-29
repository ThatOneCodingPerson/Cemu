package info.cemu.cemu.common.android.display

import android.app.Activity
import android.content.Context
import android.hardware.display.DisplayManager
import android.view.Display

object DisplayUtils {
    private var launchDisplayId: Int? = null

    fun init(activity: Activity) {
        if (launchDisplayId != null) {
            return
        }
        val displayId = activity.display?.displayId ?: Display.DEFAULT_DISPLAY
        launchDisplayId = displayId
    }

    fun getInternalDisplay(context: Context): Display? {
        val displayManager = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
        return displayManager.getDisplay(Display.DEFAULT_DISPLAY)
    }

    /** Displays that can show the GamePad (second screens, external monitors), in the system's order. */
    fun getExternalDisplays(context: Context): List<Display> {
        val displayManager = context.getSystemService(Context.DISPLAY_SERVICE) as DisplayManager
        val internalDisplay = getInternalDisplay(context)
        val internalId = internalDisplay?.displayId ?: launchDisplayId
        return displayManager.getDisplays(DisplayManager.DISPLAY_CATEGORY_PRESENTATION)
            .filter { display ->
                display.displayId != internalId && display.isUsableExternalDisplay(internalDisplay)
            }
    }

    /**
     * The display for the GamePad: the one named [preferredName] (Settings > General > GamePad display) if it is
     * connected, otherwise the first one.
     */
    fun getExternalDisplay(context: Context, preferredName: String? = null): Display? {
        val displays = getExternalDisplays(context)
        return displays.firstOrNull { preferredName != null && it.name == preferredName } ?: displays.firstOrNull()
    }

    private fun Display.isUsableExternalDisplay(internalDisplay: Display?): Boolean {
        val hasPresentationFlag = (flags and Display.FLAG_PRESENTATION) == Display.FLAG_PRESENTATION
        val isPrivateDisplay = (flags and Display.FLAG_PRIVATE) == Display.FLAG_PRIVATE
        val hasUsableMode = mode.physicalWidth > 0 && mode.physicalHeight > 0
        val hasDifferentName = internalDisplay == null || name != internalDisplay.name
        return isValid &&
            state == Display.STATE_ON &&
            !isPrivateDisplay &&
            hasDifferentName &&
            hasPresentationFlag &&
            hasUsableMode
    }
}
