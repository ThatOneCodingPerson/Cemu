#include "JNIUtils.h"
#include "WindowSystem.h"
#include "config/ActiveSettings.h"

#include <unistd.h>

// Shows WindowSystem::ShowErrorDialog messages as a dialog in the current activity (see NativeErrors.kt).
// Most callers terminate the process right after reporting, so the reporting thread waits until the user closed
// the dialog. The main thread never waits since it's the one showing the dialog.
namespace NativeErrors
{
	constexpr auto MAX_WAIT_FOR_DIALOG = std::chrono::minutes(2);

	JNIUtils::Scopedjclass s_nativeErrorsClass;
	jmethodID s_showErrorDialogMethod = nullptr;

	std::mutex s_mutex;
	std::condition_variable s_dialogClosed;
	std::unordered_set<sint32> s_openDialogs;
	std::atomic_int32_t s_nextDialogId = 1;

	fs::path GetLastErrorPath()
	{
		return ActiveSettings::GetUserDataPath("last_error.txt");
	}

	void ShowErrorDialog(std::string_view message, std::string_view title)
	{
		if (s_showErrorDialogMethod == nullptr)
			return;

		const sint32 dialogId = s_nextDialogId++;
		{
			std::scoped_lock lock(s_mutex);
			s_openDialogs.insert(dialogId);
		}

		std::string titleStr(title);
		std::string messageStr(message);
		JNIUtils::FiberSafeJNICall([&](JNIEnv* env) {
			jstring jTitle = JNIUtils::ToJString(env, titleStr);
			jstring jMessage = JNIUtils::ToJString(env, messageStr);
			env->CallStaticVoidMethod(*s_nativeErrorsClass, s_showErrorDialogMethod, dialogId, jTitle, jMessage);
			if (env->ExceptionCheck())
				env->ExceptionClear();
			env->DeleteLocalRef(jTitle);
			env->DeleteLocalRef(jMessage);
		});

		std::unique_lock lock(s_mutex);
		if (gettid() != getpid())
			s_dialogClosed.wait_for(lock, MAX_WAIT_FOR_DIALOG, [&] { return !s_openDialogs.contains(dialogId); });
		s_openDialogs.erase(dialogId);
	}
} // namespace NativeErrors

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeErrors_initialize(JNIEnv* env, jclass clazz)
{
	NativeErrors::s_nativeErrorsClass = JNIUtils::Scopedjclass(clazz);
	NativeErrors::s_showErrorDialogMethod = env->GetStaticMethodID(clazz, "showErrorDialog", "(ILjava/lang/String;Ljava/lang/String;)V");
	WindowSystem::SetErrorDialogHandler(NativeErrors::ShowErrorDialog);
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeErrors_onErrorDialogClosed([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint dialogId)
{
	{
		std::scoped_lock lock(NativeErrors::s_mutex);
		NativeErrors::s_openDialogs.erase(dialogId);
	}
	NativeErrors::s_dialogClosed.notify_all();
	// the user has seen it, don't show it again on the next launch
	std::error_code ec;
	fs::remove(NativeErrors::GetLastErrorPath(), ec);
}

extern "C" [[maybe_unused]] JNIEXPORT jstring JNICALL
Java_info_cemu_cemu_nativeinterface_NativeErrors_takeLastSessionError(JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	const auto path = NativeErrors::GetLastErrorPath();
	std::error_code ec;
	if (!fs::exists(path, ec))
		return nullptr;
	std::string content;
	{
		std::ifstream file(path);
		content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
	}
	fs::remove(path, ec);
	if (content.empty())
		return nullptr;
	return JNIUtils::ToJString(env, content);
}
