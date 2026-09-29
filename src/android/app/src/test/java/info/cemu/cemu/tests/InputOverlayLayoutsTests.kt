package info.cemu.cemu.tests

import info.cemu.cemu.common.settings.InputOverlayRect
import info.cemu.cemu.common.settings.InputOverlaySettings
import info.cemu.cemu.common.settings.OverlayInputConfig
import info.cemu.cemu.common.settings.forTitle
import info.cemu.cemu.common.settings.hasPerGameLayout
import info.cemu.cemu.common.settings.titleIdKey
import info.cemu.cemu.common.settings.withLayout
import info.cemu.cemu.common.settings.withPerGameLayout
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Test

private const val GAME = 0x00050000101C9400L
private const val OTHER_GAME = 0x0005000010145D00L

private val GLOBAL = mapOf(OverlayInputConfig.BUTTON_A to InputOverlayRect(0, 0, 10, 10))
private val MOVED = mapOf(OverlayInputConfig.BUTTON_A to InputOverlayRect(50, 50, 60, 60))

class InputOverlayLayoutsTests {
    private val settings = InputOverlaySettings(inputOverlayRectMap = GLOBAL)

    @Test
    fun titleIdKeyIsSixteenHexDigits() {
        assertEquals("00050000101c9400", titleIdKey(GAME))
    }

    @Test
    fun gamesUseTheGlobalLayoutByDefault() {
        assertFalse(settings.hasPerGameLayout(GAME))
        assertEquals(GLOBAL, settings.forTitle(GAME).inputOverlayRectMap)
        assertSame(settings, settings.forTitle(null))
    }

    @Test
    fun separateLayoutStartsAsACopyAndTakesTheEdits() {
        val separate = settings.withPerGameLayout(GAME, true)
        assertTrue(separate.hasPerGameLayout(GAME))
        assertEquals(GLOBAL, separate.forTitle(GAME).inputOverlayRectMap)

        val edited = separate.withLayout(GAME, MOVED)
        assertEquals(MOVED, edited.forTitle(GAME).inputOverlayRectMap)
        // the shared layout and other games are untouched
        assertEquals(GLOBAL, edited.inputOverlayRectMap)
        assertEquals(GLOBAL, edited.forTitle(OTHER_GAME).inputOverlayRectMap)
    }

    @Test
    fun editsWithoutASeparateLayoutChangeTheSharedOne() {
        val edited = settings.withLayout(GAME, MOVED)
        assertEquals(MOVED, edited.inputOverlayRectMap)
        assertFalse(edited.hasPerGameLayout(GAME))
    }

    @Test
    fun resetKeepsTheSeparateLayoutWithDefaultPositions() {
        val reset = settings.withPerGameLayout(GAME, true).withLayout(GAME, emptyMap())
        assertTrue(reset.hasPerGameLayout(GAME))
        assertEquals(emptyMap<OverlayInputConfig, InputOverlayRect>(), reset.forTitle(GAME).inputOverlayRectMap)
        assertEquals(GLOBAL, reset.inputOverlayRectMap)
    }

    @Test
    fun turningItOffGoesBackToTheSharedLayout() {
        val off = settings.withPerGameLayout(GAME, true).withLayout(GAME, MOVED).withPerGameLayout(GAME, false)
        assertFalse(off.hasPerGameLayout(GAME))
        assertEquals(GLOBAL, off.forTitle(GAME).inputOverlayRectMap)
    }

    @Test
    fun unknownTitleOnlyUsesTheSharedLayout() {
        assertSame(settings, settings.withPerGameLayout(0L, true))
        assertSame(settings, settings.withPerGameLayout(null, true))
        assertEquals(MOVED, settings.withLayout(0L, MOVED).inputOverlayRectMap)
    }
}
