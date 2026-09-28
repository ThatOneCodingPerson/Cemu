#include "JNIUtils.h"

namespace NativeLocalization
{
	struct StringHash
	{
		using is_transparent = void;
		size_t operator()(std::string_view str) const
		{
			return std::hash<std::string_view>{}(str);
		}
	};

	// keys must own their strings (string_view keys would dangle); lookups by string_view via transparent hashing
	std::unordered_map<std::string, std::string, StringHash, std::equal_to<>> g_messages;
	std::shared_mutex g_messagesMutex;

	std::string Translate(std::string_view msgId)
	{
		std::shared_lock lock(g_messagesMutex);
		if (auto message = g_messages.find(msgId); message != g_messages.end())
		{
			return message->second;
		}

		return std::string{msgId};
	}
} // namespace NativeLocalization

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeLocalization_setTranslations(JNIEnv* env, [[maybe_unused]] jclass clazz, jobject translations)
{
	decltype(NativeLocalization::g_messages) messages;

	jclass mapClass = env->GetObjectClass(translations);
	jmethodID keySetMethodId = env->GetMethodID(mapClass, "keySet", "()Ljava/util/Set;");
	jmethodID getMethodId = env->GetMethodID(mapClass, "get", "(Ljava/lang/Object;)Ljava/lang/Object;");
	jobject keySet = env->CallObjectMethod(translations, keySetMethodId);
	jclass setClass = env->GetObjectClass(keySet);
	jmethodID toArrayMethodId = env->GetMethodID(setClass, "toArray", "()[Ljava/lang/Object;");
	auto keyArray = static_cast<jobjectArray>(env->CallObjectMethod(keySet, toArrayMethodId));
	jint size = env->GetArrayLength(keyArray);

	for (jint i = 0; i < size; i++)
	{
		auto keyJava = static_cast<jstring>(env->GetObjectArrayElement(keyArray, i));
		auto translationJava = static_cast<jstring>(env->CallObjectMethod(translations, getMethodId, keyJava));
		messages.insert_or_assign(JNIUtils::FromJString(env, keyJava), JNIUtils::FromJString(env, translationJava));
		env->DeleteLocalRef(keyJava);
		env->DeleteLocalRef(translationJava);
	}
	env->DeleteLocalRef(keyArray);
	env->DeleteLocalRef(setClass);
	env->DeleteLocalRef(keySet);
	env->DeleteLocalRef(mapClass);

	{
		std::unique_lock lock(NativeLocalization::g_messagesMutex);
		NativeLocalization::g_messages = std::move(messages);
	}

	SetTranslationCallback(NativeLocalization::Translate);
}
