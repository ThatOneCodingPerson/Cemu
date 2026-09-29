package info.cemu.cemu.common.settings

// Per-game input overlay layouts. A game with its own layout has an entry in perGameRectMaps; every other game
// uses the global inputOverlayRectMap. titleId null or 0: unknown title (e.g. a standalone RPX), global layout.

typealias InputOverlayRectMap = Map<OverlayInputConfig, InputOverlayRect>

fun titleIdKey(titleId: Long): String = "%016x".format(titleId)

private fun Long?.toTitleKey(): String? = this?.takeIf { it != 0L }?.let(::titleIdKey)

fun InputOverlaySettings.hasPerGameLayout(titleId: Long?): Boolean {
    val key = titleId.toTitleKey() ?: return false
    return key in perGameRectMaps
}

/** The settings with the layout this title uses. */
fun InputOverlaySettings.forTitle(titleId: Long?): InputOverlaySettings {
    val key = titleId.toTitleKey() ?: return this
    val perGameRects = perGameRectMaps[key] ?: return this
    return copy(inputOverlayRectMap = perGameRects)
}

/** Stores [rects] as the layout this title uses: its own if it has one, otherwise the global one. */
fun InputOverlaySettings.withLayout(titleId: Long?, rects: InputOverlayRectMap): InputOverlaySettings {
    val key = titleId.toTitleKey()
    if (key != null && key in perGameRectMaps) {
        return copy(perGameRectMaps = perGameRectMaps + (key to rects))
    }
    return copy(inputOverlayRectMap = rects)
}

/**
 * Gives the title its own layout, starting as a copy of the global one, or removes it so the title uses the global
 * layout again. Does nothing for an unknown title.
 */
fun InputOverlaySettings.withPerGameLayout(titleId: Long?, enabled: Boolean): InputOverlaySettings {
    val key = titleId.toTitleKey() ?: return this
    return when {
        enabled && key !in perGameRectMaps -> copy(perGameRectMaps = perGameRectMaps + (key to inputOverlayRectMap))
        !enabled && key in perGameRectMaps -> copy(perGameRectMaps = perGameRectMaps - key)
        else -> this
    }
}
