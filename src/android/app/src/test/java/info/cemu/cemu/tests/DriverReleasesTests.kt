package info.cemu.cemu.tests

import info.cemu.cemu.settings.customdrivers.DriverRepository
import info.cemu.cemu.settings.customdrivers.parseAdrenoModel
import info.cemu.cemu.settings.customdrivers.parseDriverReleases
import info.cemu.cemu.settings.customdrivers.suggestDriver
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class DriverReleasesTests {
    @Test
    fun parsesAdrenoModelFromVulkanDeviceNames() {
        assertEquals(740, parseAdrenoModel("Adreno (TM) 740"))
        assertEquals(650, parseAdrenoModel("Turnip Adreno (TM) 650"))
        assertEquals(830, parseAdrenoModel("Adreno (TM) 830"))
        assertNull(parseAdrenoModel("Mali-G715"))
        assertNull(parseAdrenoModel(""))
    }

    @Test
    fun suggestsByAdrenoGeneration() {
        assertEquals(DriverRepository.MrPurpleTurnip, suggestDriver(740)?.repository) // AYN Thor
        assertEquals(DriverRepository.MrPurpleTurnip, suggestDriver(650)?.repository) // AYN Thor Lite
        assertEquals(DriverRepository.KimchiTurnip, suggestDriver(702)?.repository)
        assertEquals(DriverRepository.GameHubAdreno8xx, suggestDriver(830)?.repository)
        assertNull(suggestDriver(530))
        assertNull(suggestDriver(null))
    }

    // trimmed response of GET /repos/K11MCH1/AdrenoToolsDrivers/releases (2026-09-28) plus a release without a zip
    // and a draft; the API order there isn't by date
    @Test
    fun parsesReleasesNewestFirstWithZipsOnly() {
        val body = javaClass.classLoader!!.getResource("github-releases-sample.json")!!.readText()

        val releases = parseDriverReleases(body)

        assertEquals(
            listOf("v25.3.0-rc.11", "v25.3.0-rc.10", "v842.6", "v819.2"),
            releases.map { it.tagName },
        )
        assertEquals("Qualcomm Driver v842.6", releases[2].title)
        assertEquals(
            "https://github.com/K11MCH1/AdrenoToolsDrivers/releases/download/v842.6/",
            releases[2].zipAssets.first().downloadUrl.substringBeforeLast('/') + "/",
        )
    }
}
