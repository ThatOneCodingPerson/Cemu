package info.cemu.cemu.games.shadercache

import android.content.Context
import android.net.Uri
import android.provider.OpenableColumns
import info.cemu.cemu.nativeinterface.NativeShaderCache
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.io.File
import java.io.InputStream
import java.util.zip.ZipEntry
import java.util.zip.ZipInputStream
import java.util.zip.ZipOutputStream

/** Totals of an import of one or more picked files (a .bin each, or zips containing them). */
data class ShaderCacheImportSummary(
    val addedShaders: Int,
    val addedPipelines: Int,
    val filesOfOtherGames: Int,
    val failedFiles: Int,
)

object ShaderCacheTransfer {
    // transferable caches of big games reach a few hundred MB; anything bigger isn't one
    private const val MAX_FILE_SIZE = 1024L * 1024 * 1024

    suspend fun import(context: Context, titleId: Long, uris: List<Uri>): ShaderCacheImportSummary =
        withContext(Dispatchers.IO) {
            var summary = ShaderCacheImportSummary(0, 0, 0, 0)
            val tempDir = File(context.cacheDir, "shader-cache-import").apply {
                deleteRecursively()
                mkdirs()
            }
            try {
                for (uri in uris) {
                    val name = displayName(context, uri) ?: "cache.bin"
                    val files = try {
                        context.contentResolver.openInputStream(uri)?.use { input ->
                            if (name.endsWith(".zip", ignoreCase = true)) extractBinFiles(input, tempDir)
                            else listOf(copyToTemp(input, tempDir, name) to name)
                        } ?: emptyList()
                    } catch (_: Exception) {
                        summary = summary.copy(failedFiles = summary.failedFiles + 1)
                        continue
                    }
                    for ((file, fileName) in files) {
                        // desktop Cemu's Metal caches (<titleid>_mtlshaders.bin) have the same header, not for Vulkan
                        if (fileName.contains("_mtl", ignoreCase = true)) {
                            summary = summary.copy(filesOfOtherGames = summary.filesOfOtherGames + 1)
                            file.delete()
                            continue
                        }
                        val outcome = NativeShaderCache.import(
                            titleId = titleId,
                            sourcePath = file.path,
                            // the old format doesn't name its game, trust a file name that does
                            allowLegacy = fileName.contains("%016x".format(titleId), ignoreCase = true),
                        )
                        summary = when (outcome.result) {
                            NativeShaderCache.ImportResult.IMPORTED ->
                                if (outcome.kind == NativeShaderCache.CacheKind.PIPELINES)
                                    summary.copy(addedPipelines = summary.addedPipelines + outcome.addedCount)
                                else
                                    summary.copy(addedShaders = summary.addedShaders + outcome.addedCount)

                            NativeShaderCache.ImportResult.NOT_A_CACHE_OF_THIS_TITLE ->
                                summary.copy(filesOfOtherGames = summary.filesOfOtherGames + 1)

                            else -> summary.copy(failedFiles = summary.failedFiles + 1)
                        }
                        file.delete()
                    }
                }
            } finally {
                tempDir.deleteRecursively()
            }
            summary
        }

    /** Writes the game's transferable caches into a zip. False if there is nothing to export or it failed. */
    suspend fun export(context: Context, titleId: Long, targetUri: Uri): Boolean = withContext(Dispatchers.IO) {
        val files = NativeShaderCache.getTransferableCachePaths(titleId).map(::File).filter { it.isFile }
        if (files.isEmpty())
            return@withContext false
        try {
            context.contentResolver.openOutputStream(targetUri, "wt")?.use { output ->
                ZipOutputStream(output.buffered()).use { zip ->
                    for (file in files) {
                        zip.putNextEntry(ZipEntry(file.name))
                        file.inputStream().use { it.copyTo(zip) }
                        zip.closeEntry()
                    }
                }
                true
            } ?: false
        } catch (_: Exception) {
            false
        }
    }

    private fun copyToTemp(input: InputStream, tempDir: File, name: String): File {
        val target = File.createTempFile("cache", ".bin", tempDir)
        target.outputStream().use { output -> copyLimited(input, output, name) }
        return target
    }

    // only the .bin entries, flat (names are kept for the legacy-format check)
    private fun extractBinFiles(input: InputStream, tempDir: File): List<Pair<File, String>> {
        val files = mutableListOf<Pair<File, String>>()
        ZipInputStream(input).use { zip ->
            while (true) {
                val entry = zip.nextEntry ?: break
                val entryName = entry.name.substringAfterLast('/')
                if (entry.isDirectory || !entryName.endsWith(".bin", ignoreCase = true))
                    continue
                val target = File.createTempFile("cache", ".bin", tempDir)
                target.outputStream().use { output -> copyLimited(zip, output, entryName) }
                files += target to entryName
            }
        }
        return files
    }

    private fun copyLimited(input: InputStream, output: java.io.OutputStream, name: String) {
        val buffer = ByteArray(64 * 1024)
        var total = 0L
        while (true) {
            val count = input.read(buffer)
            if (count < 0)
                break
            total += count
            if (total > MAX_FILE_SIZE)
                throw java.io.IOException("$name is too big for a shader cache")
            output.write(buffer, 0, count)
        }
    }

    private fun displayName(context: Context, uri: Uri): String? = try {
        context.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)
            ?.use { cursor -> if (cursor.moveToFirst()) cursor.getString(0) else null }
    } catch (_: Exception) {
        null
    }
}
