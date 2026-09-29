package info.cemu.cemu.nativeinterface

import java.io.ByteArrayOutputStream

/**
 * How native code names SAF documents. A document URI with the document id appended *unencoded*, so the id's "/"
 * separators are path separators and the last component is the real file name:
 *
 *     content://<authority>/tree/<percent-encoded tree id>/document/<document id, only "%" escaped as "%25">
 *     e.g. content://com.android.externalstorage.documents/tree/primary%3AGames/document/primary:Games/Zelda: BotW
 *
 * Native code appends plain child names to these paths. Parsing percent-decodes the document id leniently, so
 * paths written by older versions (the fully encoded URIs stored as game paths, and paths with the id encoded up to
 * its last ":", kept in the title cache and in shortcuts) name the same document.
 *
 * Pure string code (no android.net.Uri), unit tested in SafNativePathTests.
 */
internal data class SafDocumentPath(
    val authority: String,
    /** Null for plain document URIs (content://<authority>/document/<id>). */
    val treeId: String?,
    /** Null for the tree URI itself. */
    val documentId: String?,
)

private const val CONTENT_PREFIX = "content://"
private const val TREE_SEGMENT = "tree/"
private const val DOCUMENT_SEGMENT = "document/"

/** Null if [path] isn't a SAF tree or document path, e.g. a MediaStore URI. */
internal fun parseSafDocumentPath(path: String): SafDocumentPath? {
    if (!path.startsWith(CONTENT_PREFIX))
        return null
    val authorityEnd = path.indexOf('/', CONTENT_PREFIX.length)
    if (authorityEnd <= CONTENT_PREFIX.length)
        return null
    val authority = path.substring(CONTENT_PREFIX.length, authorityEnd)
    val uriPath = path.substring(authorityEnd + 1)

    if (uriPath.startsWith(DOCUMENT_SEGMENT)) {
        val documentId = uriPath.substring(DOCUMENT_SEGMENT.length)
        if (documentId.isEmpty())
            return null
        return SafDocumentPath(authority, null, percentDecodeLenient(documentId))
    }
    if (!uriPath.startsWith(TREE_SEGMENT))
        return null
    val treePath = uriPath.substring(TREE_SEGMENT.length)
    val treeIdEnd = treePath.indexOf('/')
    if (treeIdEnd == -1) {
        return if (treePath.isEmpty()) null else SafDocumentPath(authority, percentDecodeLenient(treePath), null)
    }
    val treeId = percentDecodeLenient(treePath.substring(0, treeIdEnd))
    val documentPath = treePath.substring(treeIdEnd + 1)
    if (!documentPath.startsWith(DOCUMENT_SEGMENT))
        return null
    val documentId = documentPath.substring(DOCUMENT_SEGMENT.length)
    if (documentId.isEmpty())
        return null
    return SafDocumentPath(authority, treeId, percentDecodeLenient(documentId))
}

internal fun formatSafDocumentPath(document: SafDocumentPath): String = buildString {
    append(CONTENT_PREFIX).append(document.authority).append('/')
    if (document.treeId != null) {
        append(TREE_SEGMENT).append(percentEncode(document.treeId))
        document.documentId?.let { append('/').append(DOCUMENT_SEGMENT).append(it.replace("%", "%25")) }
    } else {
        append(DOCUMENT_SEGMENT).append(document.documentId?.replace("%", "%25").orEmpty())
    }
}

/**
 * Decodes %XX escapes (UTF-8) and keeps everything else, including a "%" that doesn't start an escape and
 * characters that are already decoded.
 */
internal fun percentDecodeLenient(text: String): String {
    if ('%' !in text)
        return text
    val bytes = ByteArrayOutputStream(text.length)
    var i = 0
    while (i < text.length) {
        val c = text[i]
        if (c == '%' && i + 2 < text.length) {
            val high = Character.digit(text[i + 1], 16)
            val low = Character.digit(text[i + 2], 16)
            if (high != -1 && low != -1) {
                bytes.write(high * 16 + low)
                i += 3
                continue
            }
        }
        val codePointLength = Character.charCount(text.codePointAt(i))
        bytes.write(text.substring(i, i + codePointLength).toByteArray(Charsets.UTF_8))
        i += codePointLength
    }
    return String(bytes.toByteArray(), Charsets.UTF_8)
}

// the characters android.net.Uri.encode leaves as they are, so tree ids are written the way URIs contain them
private const val UNRESERVED_CHARACTERS = "_-!.~'()*"

internal fun percentEncode(text: String): String = buildString {
    for (byte in text.toByteArray(Charsets.UTF_8)) {
        val c = (byte.toInt() and 0xFF).toChar()
        if (c in 'a'..'z' || c in 'A'..'Z' || c in '0'..'9' || c in UNRESERVED_CHARACTERS) {
            append(c)
        } else {
            append('%').append("0123456789ABCDEF"[(byte.toInt() shr 4) and 0xF]).append("0123456789ABCDEF"[byte.toInt() and 0xF])
        }
    }
}
