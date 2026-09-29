package info.cemu.cemu.emulation

import android.content.ContentValues
import android.content.Context
import android.graphics.Bitmap
import android.os.Environment
import android.provider.MediaStore
import info.cemu.cemu.nativeinterface.NativeEmulation
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import java.nio.ByteBuffer
import java.time.LocalDateTime
import java.time.format.DateTimeFormatter

const val SCREENSHOTS_FOLDER = "Pictures/Cemu"

/**
 * Saves a captured frame as PNG to [SCREENSHOTS_FOLDER], where gallery apps show it. Apps need no storage
 * permission to add their own media files on Android 10+.
 */
suspend fun saveScreenshotToGallery(context: Context, screenshot: NativeEmulation.Screenshot): Boolean =
    withContext(Dispatchers.IO) {
        val bitmap = try {
            Bitmap.createBitmap(screenshot.width, screenshot.height, Bitmap.Config.ARGB_8888).apply {
                copyPixelsFromBuffer(ByteBuffer.wrap(screenshot.rgba))
            }
        } catch (_: Exception) {
            return@withContext false
        }

        val resolver = context.contentResolver
        val fileName = "Cemu_${LocalDateTime.now().format(DateTimeFormatter.ofPattern("yyyy-MM-dd_HH-mm-ss"))}.png"
        val values = ContentValues().apply {
            put(MediaStore.Images.Media.DISPLAY_NAME, fileName)
            put(MediaStore.Images.Media.MIME_TYPE, "image/png")
            put(MediaStore.Images.Media.RELATIVE_PATH, "${Environment.DIRECTORY_PICTURES}/Cemu")
            // hidden from other apps until it's completely written
            put(MediaStore.Images.Media.IS_PENDING, 1)
        }

        try {
            val uri = resolver.insert(
                MediaStore.Images.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY),
                values,
            ) ?: return@withContext false
            try {
                val written = resolver.openOutputStream(uri)?.use {
                    bitmap.compress(Bitmap.CompressFormat.PNG, 100, it)
                } ?: false
                if (!written) {
                    resolver.delete(uri, null, null)
                    return@withContext false
                }
                values.clear()
                values.put(MediaStore.Images.Media.IS_PENDING, 0)
                resolver.update(uri, values, null, null)
                true
            } catch (_: Exception) {
                resolver.delete(uri, null, null)
                false
            }
        } catch (_: Exception) {
            false
        } finally {
            bitmap.recycle()
        }
    }
