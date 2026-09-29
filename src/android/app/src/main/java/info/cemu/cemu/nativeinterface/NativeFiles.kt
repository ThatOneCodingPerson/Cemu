@file:Suppress("unused")

package info.cemu.cemu.nativeinterface

import android.content.ContentResolver
import android.net.Uri
import android.provider.DocumentsContract
import android.util.Log
import androidx.annotation.Keep
import androidx.core.net.toUri

private const val MODE = "r"

/** The path native code uses for this SAF document, see SafDocumentPath. Other URIs are passed as they are. */
fun Uri.toNativePath(): String {
    val authority = authority ?: return toString()
    val treeId = if (DocumentsContract.isTreeUri(this)) DocumentsContract.getTreeDocumentId(this) else null
    val documentId = try {
        DocumentsContract.getDocumentId(this)
    } catch (_: IllegalArgumentException) {
        null // the tree URI itself
    }
    if (treeId == null && documentId == null) {
        return toString()
    }
    return formatSafDocumentPath(SafDocumentPath(authority, treeId, documentId))
}

fun String.fromNativePath(): Uri {
    val document = parseSafDocumentPath(this) ?: return toUri()
    val treeId = document.treeId
    val documentId = document.documentId
    return when {
        treeId != null && documentId != null -> DocumentsContract.buildDocumentUriUsingTree(
            DocumentsContract.buildTreeDocumentUri(document.authority, treeId),
            documentId,
        )

        treeId != null -> DocumentsContract.buildTreeDocumentUri(document.authority, treeId)
        documentId != null -> DocumentsContract.buildDocumentUri(document.authority, documentId)
        else -> toUri()
    }
}

object NativeFiles {
    private lateinit var contentResolver: ContentResolver

    fun initialize(contentResolver: ContentResolver) {
        this.contentResolver = contentResolver
    }

    @Keep
    @JvmStatic
    fun openContentUri(uri: String): Int {
        try {
            val parcelFileDescriptor =
                contentResolver.openFileDescriptor(
                    uri.fromNativePath(), MODE
                )
            if (parcelFileDescriptor != null) {
                val fd = parcelFileDescriptor.detachFd()
                parcelFileDescriptor.close()
                return fd
            }
        } catch (e: Exception) {
            Log.d("NativeFiles", "Cannot open content uri, error: ${e.message}")
        }
        return -1
    }

    // These are called from native code: they must never throw (a pending exception aborts the native
    // caller), failures are reported as "not found"/empty instead.

    @Keep
    @JvmStatic
    fun listFiles(uri: String): Array<String?> {
        val files = ArrayList<String>()
        try {
            val directoryUri = uri.fromNativePath()
            // throws IllegalArgumentException for URIs that aren't tree/document URIs
            val childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(
                directoryUri,
                DocumentsContract.getDocumentId(directoryUri)
            )
            val authority = directoryUri.authority!!
            val treeId = DocumentsContract.getTreeDocumentId(directoryUri)
            contentResolver.query(
                childrenUri,
                arrayOf(DocumentsContract.Document.COLUMN_DOCUMENT_ID),
                null,
                null,
                null
            ).use { cursor ->
                while (cursor != null && cursor.moveToNext()) {
                    val documentId = cursor.getString(0) ?: continue
                    files.add(formatSafDocumentPath(SafDocumentPath(authority, treeId, documentId)))
                }
            }
        } catch (e: Exception) {
            Log.d("NativeFiles", "Cannot list files: ${e.message}")
        }
        var filesArray = arrayOfNulls<String>(files.size)
        filesArray = files.toArray(filesArray)
        return filesArray
    }

    /** Mime type of the document, null if it doesn't exist or isn't accessible. */
    private fun getMimeType(uri: String): String? = try {
        contentResolver.getType(uri.fromNativePath())
    } catch (e: Exception) {
        Log.d("NativeFiles", "Cannot get type of $uri: ${e.message}")
        null
    }

    @Keep
    @JvmStatic
    fun isDirectory(uri: String): Boolean {
        return getMimeType(uri) == DocumentsContract.Document.MIME_TYPE_DIR
    }

    @Keep
    @JvmStatic
    fun isFile(uri: String): Boolean {
        // a missing or inaccessible document is neither a file nor a directory. Some providers
        // don't report a type for existing files, so check existence in that case
        val mimeType = getMimeType(uri) ?: return exists(uri)
        return mimeType != DocumentsContract.Document.MIME_TYPE_DIR
    }

    @Keep
    @JvmStatic
    fun exists(uri: String): Boolean {
        try {
            contentResolver.query(uri.fromNativePath(), null, null, null, null).use { cursor ->
                return cursor != null && cursor.moveToFirst()
            }
        } catch (e: Exception) {
            Log.d("NativeFiles", "Failed checking if file exists: ${e.message}")
            return false
        }
    }
}
