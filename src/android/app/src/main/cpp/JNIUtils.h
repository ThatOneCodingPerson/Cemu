#pragma once

#include <jni.h>
#include <boost/nowide/utf/convert.hpp>

namespace JNIUtils
{
	void SetJavaVM(JavaVM* jvm);

	// Java's *UTF chars functions use modified UTF-8 (characters outside the BMP such as emoji become two 3-byte
	// surrogates), which mangles such paths and names. Convert via UTF-16 instead.
	inline std::string FromJString(JNIEnv* env, jstring jstr)
	{
		if (jstr == nullptr)
			return {};
		const jsize length = env->GetStringLength(jstr);
		const jchar* chars = env->GetStringChars(jstr, nullptr);
		if (chars == nullptr)
			return {};
		const auto* utf16 = reinterpret_cast<const char16_t*>(chars);
		std::string str = boost::nowide::utf::convert_string<char>(utf16, utf16 + length);
		env->ReleaseStringChars(jstr, chars);
		return str;
	}

	inline jstring ToJString(JNIEnv* env, const std::string& str)
	{
		const std::u16string utf16 = boost::nowide::utf::convert_string<char16_t>(str.data(), str.data() + str.size());
		return env->NewString(reinterpret_cast<const jchar*>(utf16.data()), static_cast<jsize>(utf16.size()));
	}

	inline jstring ToJString(JNIEnv* env, std::string_view str)
	{
		return ToJString(env, std::string(str));
	}

	inline jstring ToJString(JNIEnv* env, std::wstring_view str)
	{
		return ToJString(env, boost::nowide::narrow(str));
	}

	inline void HandleNativeException(JNIEnv* env, std::invocable auto fn)
	{
		try
		{
			fn();
		} catch (const std::exception& exception)
		{
			jclass exceptionClass = env->FindClass("info/cemu/cemu/nativeinterface/NativeException");
			env->ThrowNew(exceptionClass, exception.what());
		} catch (...)
		{
			jclass exceptionClass = env->FindClass("info/cemu/cemu/nativeinterface/NativeException");
			env->ThrowNew(exceptionClass, "Unknown native exception");
		}
	}

	JNIEnv* GetEnv();

	// Must be called after calling into Java from native code. A pending exception makes the next JNI call abort,
	// and on natively attached threads it takes the app down when the thread detaches. Logs and clears it.
	// Returns whether an exception was pending.
	inline bool CheckAndClearException(JNIEnv* env)
	{
		if (!env->ExceptionCheck())
			return false;
		env->ExceptionDescribe(); // prints the Java stack trace to logcat
		env->ExceptionClear();
		return true;
	}

	class Scopedjobject
	{
	  public:
		Scopedjobject() = default;

		Scopedjobject(Scopedjobject&& other) noexcept;

		void DeleteReference();

		Scopedjobject& operator=(Scopedjobject&& other) noexcept;

		jobject operator*() const;

		explicit Scopedjobject(jobject obj);

		~Scopedjobject();

	  private:
		jobject m_jobject = nullptr;
	};

	class Scopedjclass
	{
	  public:
		Scopedjclass() = default;

		Scopedjclass(Scopedjclass&& other) noexcept;

		explicit Scopedjclass(jclass javaClass);

		Scopedjclass& operator=(Scopedjclass&& other) noexcept;

		explicit Scopedjclass(const char* className);

		~Scopedjclass();

		jclass operator*() const;

	  private:
		jclass m_jclass = nullptr;
	};

	Scopedjobject GetEnumValue(JNIEnv* env, const std::string& enumClassName, const std::string& enumName);

	template<std::ranges::sized_range Range>
	jlongArray CreateLongArray(JNIEnv* env, Range&& range)
		requires std::convertible_to<std::ranges::range_value_t<Range>, jlong>
	{
		auto size = std::ranges::size(range);
		jlongArray array = env->NewLongArray(static_cast<jsize>(size));

		std::vector<jlong> buffer;
		buffer.reserve(size);

		for (auto v : range)
		{
			buffer.push_back(static_cast<jlong>(v));
		}

		env->SetLongArrayRegion(array, 0, static_cast<jsize>(size), buffer.data());

		return array;
	}

	template<std::ranges::input_range Range>
		requires(!std::ranges::sized_range<Range>)
	jlongArray CreateLongArray(JNIEnv* env, Range&& range)
	{
		std::vector<std::ranges::range_value_t<Range>> vector;

		for (auto&& e : range)
		{
			vector.push_back(static_cast<decltype(e)&&>(e));
		}

		return CreateLongArray(env, vector);
	}

	template<typename F, typename Elem, typename Result>
	concept JNITransform = requires(F f, Elem e) {{ f(e) } -> std::convertible_to<Result>; };

	template<std::ranges::sized_range Range, typename Transform>
		requires JNITransform<Transform, std::ranges::range_value_t<Range>, jobject>
	jobjectArray CreateObjectArray(JNIEnv* env, jclass elementClass, Range&& range, Transform&& transform)
	{
		auto size = std::ranges::size(range);

		jobjectArray array = env->NewObjectArray(
			static_cast<jsize>(size),
			elementClass,
			nullptr);

		jsize index = 0;
		for (auto&& item : range)
		{
			jobject obj = transform(item);
			env->SetObjectArrayElement(array, index++, obj);
			env->DeleteLocalRef(obj);
		}

		return array;
	}

	template<std::ranges::input_range Range, typename Transform>
		requires(!std::ranges::sized_range<Range> && JNITransform<Transform, std::ranges::range_value_t<Range>, jobject>)
	jobjectArray CreateObjectArray(JNIEnv* env, jclass elementClass, Range&& range, Transform&& transform)
	{
		std::vector<std::ranges::range_value_t<Range>> vector;

		for (auto&& e : range)
		{
			vector.push_back(static_cast<decltype(e)&&>(e));
		}

		return CreateObjectArray(env, elementClass, vector, std::forward<Transform>(transform));
	}

	template<std::ranges::sized_range Range>
	jobjectArray CreateStringObjectArray(JNIEnv* env, Range&& range)
		requires std::same_as<std::ranges::range_value_t<Range>, std::string>
	{
		jclass elementClass = env->FindClass("java/lang/String");

		jobjectArray array = CreateObjectArray(
			env,
			elementClass,
			range,
			[env](const std::string& str) -> jstring { return ToJString(env, str); });

		env->DeleteLocalRef(elementClass);

		return array;
	}

	template<std::ranges::input_range Range>
		requires(!std::ranges::sized_range<Range> && std::same_as<std::ranges::range_value_t<Range>, std::string>)
	jobjectArray CreateStringObjectArray(JNIEnv* env, Range&& range)
	{
		std::vector<std::ranges::range_value_t<Range>> vector;

		for (auto&& e : range)
		{
			vector.push_back(static_cast<decltype(e)&&>(e));
		}

		return CreateStringObjectArray(env, vector);
	}

	template<typename... TArgs>
	jobject NewObject(JNIEnv* env, const char* className, const std::string& ctrSig = "()V", TArgs&&... args)
	{
		jclass javaClass = env->FindClass(className);
		jmethodID ctrId = env->GetMethodID(javaClass, "<init>", ctrSig.c_str());
		jobject obj = env->NewObject(javaClass, ctrId, std::forward<TArgs>(args)...);
		env->DeleteLocalRef(javaClass);
		return obj;
	}

	// runs task synchronously on one of a few persistent JVM-attached worker threads
	void RunOnJNIWorker(const std::function<void(JNIEnv*)>& task);

	// JNI must not be used from fiber stacks (the PPC threads run on fibers), so the call is executed on a worker
	// thread and this waits for it. Workers are reused: creating and attaching a thread per call (SAF accesses,
	// rumble every frame) was slow.
	inline void FiberSafeJNICall(std::invocable<JNIEnv*> auto func)
	{
		RunOnJNIWorker(func);
	}
} // namespace JNIUtils
