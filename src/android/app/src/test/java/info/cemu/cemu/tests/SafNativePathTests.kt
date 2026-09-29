package info.cemu.cemu.tests

import info.cemu.cemu.nativeinterface.SafDocumentPath
import info.cemu.cemu.nativeinterface.formatSafDocumentPath
import info.cemu.cemu.nativeinterface.parseSafDocumentPath
import info.cemu.cemu.nativeinterface.percentDecodeLenient
import info.cemu.cemu.nativeinterface.percentEncode
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

private const val STORAGE = "com.android.externalstorage.documents"
private const val TREE = "content://$STORAGE/tree/primary%3AGames"

class SafNativePathTests {
    @Test
    fun parsesGamePathsStoredAsDocumentUris() {
        // GamePathsScreen stores DocumentFile.fromTreeUri(...).uri.toString()
        assertEquals(
            SafDocumentPath(STORAGE, "primary:Games", "primary:Games"),
            parseSafDocumentPath("$TREE/document/primary%3AGames"),
        )
    }

    @Test
    fun parsesPathsOfOlderVersions() {
        // encoded up to the last ":" (here one in a folder name), then "/" separators; kept in the title cache and
        // in home screen shortcuts
        assertEquals(
            SafDocumentPath(STORAGE, "primary:Games", "primary:Games/Wii U/Zelda: BotW/code/app.rpx"),
            parseSafDocumentPath("$TREE/document/primary%3AGames%2FWii%20U%2FZelda%3A%20BotW/code/app.rpx"),
        )
        assertEquals(
            SafDocumentPath(STORAGE, "primary:Games", "primary:Games/100%/meta"),
            parseSafDocumentPath("$TREE/document/primary%3AGames%2F100%25/meta"),
        )
    }

    @Test
    fun keepsRealNamesAndRoundTrips() {
        val names = listOf("Zelda: BotW", "Sound #1?.bfstm", "100% Orange", "ゼルダの伝説", "🎮 Games", "a%41b")
        for (name in names) {
            val document = SafDocumentPath(STORAGE, "primary:Games", "primary:Games/$name")
            val path = formatSafDocumentPath(document)
            // native code sees the real file name as the last path component
            assertEquals(name.replace("%", "%25"), path.substringAfterLast('/'))
            assertEquals(document, parseSafDocumentPath(path))
        }
    }

    @Test
    fun childNamesAppendedByNativeCodeResolve() {
        val gameFolder = formatSafDocumentPath(SafDocumentPath(STORAGE, "primary:Games", "primary:Games/Zelda: BotW"))
        assertEquals(
            SafDocumentPath(STORAGE, "primary:Games", "primary:Games/Zelda: BotW/content/Sound/BGM #1 (Title).bfstm"),
            parseSafDocumentPath("$gameFolder/content/Sound/BGM #1 (Title).bfstm"),
        )
    }

    @Test
    fun treeAndPlainDocumentUris() {
        assertEquals(SafDocumentPath(STORAGE, "primary:Games", null), parseSafDocumentPath(TREE))
        assertEquals(TREE, formatSafDocumentPath(SafDocumentPath(STORAGE, "primary:Games", null)))

        val downloads = "com.android.providers.downloads.documents"
        assertEquals(
            SafDocumentPath(downloads, null, "msf:123"),
            parseSafDocumentPath("content://$downloads/document/msf%3A123"),
        )
    }

    @Test
    fun otherPathsAreNotSafDocuments() {
        assertNull(parseSafDocumentPath("content://media/external/file/12"))
        assertNull(parseSafDocumentPath("/storage/emulated/0/Games/app.rpx"))
        assertNull(parseSafDocumentPath("content://"))
        assertNull(parseSafDocumentPath("$TREE/document/"))
        // native parent_path() above the document id
        assertNull(parseSafDocumentPath("$TREE/document"))
    }

    @Test
    fun percentEncodingMatchesAndroidUri() {
        // android.net.Uri.encode("primary:Games/A B (1)") == "primary%3AGames%2FA%20B%20(1)"
        assertEquals("primary%3AGames%2FA%20B%20(1)", percentEncode("primary:Games/A B (1)"))
        assertEquals("%E3%82%BC", percentEncode("ゼ"))
        assertEquals("ゼ", percentDecodeLenient("%E3%82%BC"))
        assertEquals("100% sure %2", percentDecodeLenient("100% sure %2"))
    }
}
