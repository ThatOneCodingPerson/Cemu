package info.cemu.cemu.tests

import info.cemu.cemu.settings.keys.KeysFileContent
import info.cemu.cemu.settings.keys.mergeKeys
import info.cemu.cemu.settings.keys.parseKeysFile
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

private const val KEY_A = "541b9889519b27d363cd21604b97c67a"
private const val KEY_B = "00112233445566778899aabbccddeeff"

class KeysFileTests {
    @Test
    fun parsesCemuKeysFormat() {
        val text = "# comment line\r\n" +
            "541B9889-519B-27D3 63CD_21604B97C67A # example key\r\n" +
            "\t$KEY_B ; other comment\n" +
            "﻿\r\n" +
            "not a key\n" +
            "abcd\n" +
            KEY_B + "\n"
        val content = parseKeysFile(text)
        assertEquals(listOf(KEY_A, KEY_B), content.keys)
        assertEquals(2, content.invalidLineCount)
    }

    @Test
    fun mergeAppendsOnlyNewKeys() {
        val existing = "# header\r\n$KEY_A # example\r\n"
        val (merged, added) = mergeKeys(existing, KeysFileContent(listOf(KEY_A, KEY_B), 0), "keys.txt", "2026-09-29")
        assertEquals(1, added)
        assertTrue(merged.startsWith(existing))
        assertEquals("# imported from keys.txt on 2026-09-29\r\n$KEY_B\r\n", merged.removePrefix(existing))
        assertEquals(listOf(KEY_A, KEY_B), parseKeysFile(merged).keys)
    }

    @Test
    fun mergeWithoutNewKeysKeepsTheFile() {
        val existing = "$KEY_A\n"
        assertEquals(existing to 0, mergeKeys(existing, KeysFileContent(listOf(KEY_A), 0), "x", "d"))
    }

    @Test
    fun mergeKeepsLineEndingsAndAddsMissingNewline() {
        // CRLF file (Cemu writes CRLF) whose last line has no line end
        val (merged, _) = mergeKeys("# h\r\n$KEY_A", KeysFileContent(listOf(KEY_B), 0), "x", "d")
        assertEquals("# h\r\n$KEY_A\r\n# imported from x on d\r\n$KEY_B\r\n", merged)
        val (mergedLf, _) = mergeKeys("$KEY_A\n", KeysFileContent(listOf(KEY_B), 0), "x", "d")
        assertEquals("$KEY_A\n# imported from x on d\n$KEY_B\n", mergedLf)
    }
}
