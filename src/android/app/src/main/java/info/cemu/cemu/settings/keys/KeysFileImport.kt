package info.cemu.cemu.settings.keys

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import info.cemu.cemu.nativeinterface.NativeActiveSettings
import info.cemu.cemu.nativeinterface.NativeGameTitles
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.ByteArrayOutputStream
import java.io.File
import java.time.LocalDate

/** Keys found in a keys.txt; [invalidLineCount] counts lines that aren't a comment, empty or a key. */
internal data class KeysFileContent(val keys: List<String>, val invalidLineCount: Int)

/**
 * Parses Cemu's keys.txt format (KeyCache.cpp): anything after '#' or ';' is a comment, ' ', '\t', '-' and '_' are
 * ignored, a key is 32 hex digits. Keys are returned in lower case, without duplicates.
 */
internal fun parseKeysFile(text: String): KeysFileContent {
    val keys = LinkedHashSet<String>()
    var invalidLineCount = 0
    for (rawLine in text.lineSequence()) {
        val line = rawLine
            .substringBefore('#')
            .substringBefore(';')
            .filterNot { it == ' ' || it == '\t' || it == '-' || it == '_' || it == '\r' || it == '﻿' }
        if (line.isEmpty())
            continue
        if (line.length == 32 && line.all { it.isDigit() || it.lowercaseChar() in 'a'..'f' })
            keys += line.lowercase()
        else
            invalidLineCount++
    }
    return KeysFileContent(keys.toList(), invalidLineCount)
}

/**
 * Appends the keys of [imported] that [existingText] doesn't contain yet, below a comment naming the source.
 * Returns the new text and the number of added keys. Existing lines are kept as they are.
 */
internal fun mergeKeys(existingText: String, imported: KeysFileContent, sourceName: String, date: String): Pair<String, Int> {
    val existingKeys = parseKeysFile(existingText).keys.toSet()
    val newKeys = imported.keys.filterNot { it in existingKeys }
    if (newKeys.isEmpty())
        return existingText to 0
    val lineEnd = if ("\r\n" in existingText || existingText.isEmpty()) "\r\n" else "\n"
    val text = buildString {
        append(existingText)
        if (existingText.isNotEmpty() && !existingText.endsWith("\n"))
            append(lineEnd)
        append("# imported from ").append(sourceName.replace('\n', ' ').replace('\r', ' ')).append(" on ").append(date).append(lineEnd)
        newKeys.forEach { append(it).append(lineEnd) }
    }
    return text to newKeys.size
}

sealed interface KeysImportResult {
    /** [addedCount] can be 0 if every key was known already. */
    data class Imported(val addedCount: Int, val invalidLineCount: Int) : KeysImportResult
    data object NoKeysFound : KeysImportResult
    data object Error : KeysImportResult
}

// keys.txt files are small; this only guards against picking a wrong, huge file
private const val MAX_KEYS_FILE_SIZE = 1024 * 1024

// Cemu's header for a new keys.txt (KeyCache_Prepare)
private const val NEW_KEYS_FILE_HEADER =
    "# this file contains keys needed for decryption of disc file system data (WUD/WUX)\r\n" +
        "# 1 key per line, any text after a '#' character is considered a comment\r\n" +
        "# the emulator will automatically pick the right key\r\n"

/**
 * Merges the keys of a picked keys.txt into Cemu's keys.txt in the user data folder, which a file manager often
 * can't reach (Android/data), then reloads them.
 */
suspend fun importKeysFile(context: Context, uri: Uri): KeysImportResult = withContext(Dispatchers.IO) {
    try {
        val bytes = context.contentResolver.openInputStream(uri)?.use { input ->
            val output = ByteArrayOutputStream()
            val chunk = ByteArray(8192)
            while (true) {
                val count = input.read(chunk)
                if (count < 0)
                    break
                if (output.size() + count > MAX_KEYS_FILE_SIZE)
                    return@withContext KeysImportResult.Error
                output.write(chunk, 0, count)
            }
            output.toByteArray()
        } ?: return@withContext KeysImportResult.Error

        val imported = parseKeysFile(bytes.decodeToString())
        if (imported.keys.isEmpty())
            return@withContext KeysImportResult.NoKeysFound

        val keysFile = File(NativeActiveSettings.getUserDataPath(), "keys.txt")
        val existingText = if (keysFile.isFile) keysFile.readText() else NEW_KEYS_FILE_HEADER
        val (mergedText, addedCount) = mergeKeys(existingText, imported, displayName(context, uri) ?: "keys file", LocalDate.now().toString())
        if (addedCount > 0) {
            // replaced in one step, a crash while writing can't leave a truncated keys.txt
            val tempFile = File(keysFile.parentFile, "keys.txt.tmp")
            tempFile.writeText(mergedText)
            if (!tempFile.renameTo(keysFile)) {
                tempFile.delete()
                return@withContext KeysImportResult.Error
            }
            NativeGameTitles.reloadKeys()
        }
        KeysImportResult.Imported(addedCount, imported.invalidLineCount)
    } catch (_: Exception) {
        KeysImportResult.Error
    }
}

private fun displayName(context: Context, uri: Uri): String? = try {
    context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
        ?.use { cursor -> if (cursor.moveToFirst()) cursor.getString(0) else null }
} catch (_: Exception) {
    null
}
