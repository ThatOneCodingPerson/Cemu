#include "JNIUtils.h"
#include "util/helpers/helpers.h"

static JavaVM* s_jvm = nullptr;

namespace
{
	thread_local bool t_isJNIWorker = false;

	// Persistent JVM-attached threads for JNIUtils::FiberSafeJNICall. A few of them so a slow SAF call doesn't delay
	// e.g. rumble. Intentionally never destroyed (the process ends with _exit).
	class JNIWorkerPool
	{
	  public:
		static constexpr size_t WORKER_COUNT = 4;

		JNIWorkerPool()
		{
			for (size_t i = 0; i < WORKER_COUNT; i++)
				std::thread(&JNIWorkerPool::WorkerLoop, this, i).detach();
		}

		void Enqueue(std::function<void()> task)
		{
			{
				std::scoped_lock lock(m_mutex);
				m_tasks.emplace_back(std::move(task));
			}
			m_taskAvailable.notify_one();
		}

	  private:
		void WorkerLoop(size_t index)
		{
			SetThreadName(fmt::format("JNIWorker{}", index).c_str());
			t_isJNIWorker = true;
			while (true)
			{
				std::function<void()> task;
				{
					std::unique_lock lock(m_mutex);
					m_taskAvailable.wait(lock, [this] { return !m_tasks.empty(); });
					task = std::move(m_tasks.front());
					m_tasks.pop_front();
				}
				task();
			}
		}

		std::mutex m_mutex;
		std::condition_variable m_taskAvailable;
		std::deque<std::function<void()>> m_tasks;
	};

	JNIWorkerPool& GetJNIWorkerPool()
	{
		static JNIWorkerPool* s_pool = new JNIWorkerPool();
		return *s_pool;
	}
} // namespace

namespace JNIUtils
{
	void SetJavaVM(JavaVM* jvm)
	{
		s_jvm = jvm;
	}

	Scopedjobject::Scopedjobject(Scopedjobject&& other) noexcept
	{
		this->m_jobject = other.m_jobject;
		other.m_jobject = nullptr;
	}

	void Scopedjobject::DeleteReference()
	{
		if (m_jobject)
		{
			GetEnv()->DeleteGlobalRef(m_jobject);
			m_jobject = nullptr;
		}
	}

	Scopedjobject& Scopedjobject::operator=(Scopedjobject&& other) noexcept
	{
		if (this != &other)
		{
			DeleteReference();
			m_jobject = other.m_jobject;
			other.m_jobject = nullptr;
		}
		return *this;
	}

	jobject Scopedjobject::operator*() const
	{
		return m_jobject;
	}

	Scopedjobject::Scopedjobject(jobject obj)
	{
		if (obj)
			m_jobject = GetEnv()->NewGlobalRef(obj);
	}

	Scopedjobject::~Scopedjobject()
	{
		DeleteReference();
	}

	Scopedjclass::Scopedjclass(Scopedjclass&& other) noexcept
	{
		this->m_jclass = other.m_jclass;
		other.m_jclass = nullptr;
	}

	Scopedjclass::Scopedjclass(jclass javaClass)
	{
		if (javaClass)
			m_jclass = static_cast<jclass>(GetEnv()->NewGlobalRef(javaClass));
	}

	Scopedjclass& Scopedjclass::operator=(Scopedjclass&& other) noexcept
	{
		if (this != &other)
		{
			if (m_jclass)
				GetEnv()->DeleteGlobalRef(m_jclass);
			m_jclass = other.m_jclass;
			other.m_jclass = nullptr;
		}
		return *this;
	}

	Scopedjclass::Scopedjclass(const char* className)
	{
		JNIEnv* env = GetEnv();
		jclass tempObj = env->FindClass(className);
		m_jclass = static_cast<jclass>(env->NewGlobalRef(tempObj));
		env->DeleteLocalRef(tempObj);
	}

	Scopedjclass::~Scopedjclass()
	{
		if (m_jclass)
			GetEnv()->DeleteGlobalRef(m_jclass);
	}

	jclass Scopedjclass::operator*() const
	{
		return m_jclass;
	}

	Scopedjobject GetEnumValue(JNIEnv* env, const std::string& enumClassName, const std::string& enumName)
	{
		jclass enumClass = env->FindClass(enumClassName.c_str());
		jfieldID fieldID = env->GetStaticFieldID(enumClass, enumName.c_str(), ("L" + enumClassName + ";").c_str());
		jobject enumValue = env->GetStaticObjectField(enumClass, fieldID);
		env->DeleteLocalRef(enumClass);
		Scopedjobject enumObj = Scopedjobject(enumValue);
		env->DeleteLocalRef(enumValue);
		return enumObj;
	}

	void RunOnJNIWorker(const std::function<void(JNIEnv*)>& task)
	{
		// a Java callback running on a worker that calls back into native code must not wait for another worker
		if (t_isJNIWorker)
		{
			task(GetEnv());
			return;
		}
		std::mutex doneMutex;
		std::condition_variable doneCondition;
		bool done = false;
		GetJNIWorkerPool().Enqueue([&] {
			task(GetEnv());
			// notify while holding the lock: the waiter destroys doneCondition as soon as it can return
			std::scoped_lock lock(doneMutex);
			done = true;
			doneCondition.notify_one();
		});
		std::unique_lock lock(doneMutex);
		doneCondition.wait(lock, [&] { return done; });
	}

	JNIEnv* GetEnv()
	{
		thread_local static struct OwnedEnv
		{
			JNIEnv* env;
			jint result;

			OwnedEnv()
			{
				result = s_jvm->GetEnv((void**)&env, JNI_VERSION_1_6);

				if (result == JNI_EDETACHED)
					s_jvm->AttachCurrentThread(&env, nullptr);
			}

			~OwnedEnv()
			{
				if (result == JNI_EDETACHED)
					s_jvm->DetachCurrentThread();
			}
		} owned;

		return owned.env;
	}
}; // namespace JNIUtils
