package info.cemu.cemu.nativeinterface

import androidx.annotation.Keep
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update

/**
 * Errors reported by the emulator core (WindowSystem::ShowErrorDialog), shown as dialogs by
 * NativeErrorDialogHost. Native code blocks the reporting thread until [dismiss] is called
 * because it usually terminates the process afterwards.
 */
object NativeErrors {
    data class ErrorDialog(val id: Int, val title: String, val message: String)

    private const val LOCAL_DIALOG_ID = 0

    private val _dialogs = MutableStateFlow<List<ErrorDialog>>(emptyList())
    val dialogs = _dialogs.asStateFlow()

    /** Must be called on a Java thread before native initialization. */
    @JvmStatic
    external fun initialize()

    /** Returns (and deletes) the error that ended the previous session, if any. */
    @JvmStatic
    external fun takeLastSessionError(): String?

    @JvmStatic
    private external fun onErrorDialogClosed(dialogId: Int)

    fun show(title: String, message: String) {
        _dialogs.update { it + ErrorDialog(LOCAL_DIALOG_ID, title, message) }
    }

    fun dismiss(dialog: ErrorDialog) {
        _dialogs.update { dialogs -> dialogs.filterNot { it === dialog } }
        onErrorDialogClosed(dialog.id)
    }

    @Keep
    @JvmStatic
    @Suppress("unused")
    private fun showErrorDialog(dialogId: Int, title: String?, message: String?) {
        _dialogs.update { it + ErrorDialog(dialogId, title.orEmpty(), message.orEmpty()) }
    }
}
