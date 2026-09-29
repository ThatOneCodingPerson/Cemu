#include "PerformanceHint.h"

#include <dlfcn.h>
#include <unistd.h>

namespace AndroidPerformanceHint
{
	// the APerformanceHint API is API level 33+ while the app supports older versions, so it's loaded at runtime
	struct APerformanceHintManager;
	struct APerformanceHintSession;

	using PFN_getManager = APerformanceHintManager* (*)();
	using PFN_createSession = APerformanceHintSession* (*)(APerformanceHintManager*, const int32_t*, size_t, int64_t);
	using PFN_closeSession = void (*)(APerformanceHintSession*);
	using PFN_reportActualWorkDuration = int (*)(APerformanceHintSession*, int64_t);
	using PFN_setThreads = int (*)(APerformanceHintSession*, const pid_t*, size_t); // API 34+

	// most titles run at 30 or 60 fps; aiming for 60 gives 30 fps titles extra headroom
	constexpr int64_t TARGET_FRAME_DURATION_NS = 16'666'667;
	// long stalls (loading, shader compilation) shouldn't make the governor overreact
	constexpr int64_t MAX_REPORTED_DURATION_NS = TARGET_FRAME_DURATION_NS * 4;

	struct Api
	{
		PFN_createSession createSession = nullptr;
		PFN_closeSession closeSession = nullptr;
		PFN_reportActualWorkDuration reportActualWorkDuration = nullptr;
		PFN_setThreads setThreads = nullptr;
		APerformanceHintManager* manager = nullptr;
	};

	const Api& GetApi()
	{
		static const Api s_api = [] {
			Api api;
			void* library = dlopen("libandroid.so", RTLD_NOW);
			if (!library)
				return api;
			auto getManager = reinterpret_cast<PFN_getManager>(dlsym(library, "APerformanceHint_getManager"));
			api.createSession = reinterpret_cast<PFN_createSession>(dlsym(library, "APerformanceHint_createSession"));
			api.closeSession = reinterpret_cast<PFN_closeSession>(dlsym(library, "APerformanceHint_closeSession"));
			api.reportActualWorkDuration = reinterpret_cast<PFN_reportActualWorkDuration>(dlsym(library, "APerformanceHint_reportActualWorkDuration"));
			api.setThreads = reinterpret_cast<PFN_setThreads>(dlsym(library, "APerformanceHint_setThreads"));
			if (getManager && api.createSession && api.closeSession && api.reportActualWorkDuration)
				api.manager = getManager();
			cemuLog_log(LogType::Force, "ADPF performance hints: {}", api.manager ? "available" : "not supported");
			return api;
		}();
		return s_api;
	}

	std::mutex s_mutex;
	std::vector<int32_t> s_threadIds;
	APerformanceHintSession* s_session = nullptr;
	std::chrono::steady_clock::time_point s_lastFrame{};

	// expects s_mutex to be held
	void UpdateSession()
	{
		const Api& api = GetApi();
		if (s_session && api.setThreads)
		{
			std::vector<pid_t> threadIds(s_threadIds.begin(), s_threadIds.end());
			if (api.setThreads(s_session, threadIds.data(), threadIds.size()) == 0)
				return;
		}
		// API 33 can't change the threads of a session, replace it
		APerformanceHintSession* newSession = api.createSession(api.manager, s_threadIds.data(), s_threadIds.size(), TARGET_FRAME_DURATION_NS);
		if (!newSession)
		{
			cemuLog_log(LogType::Force, "ADPF: failed to create a performance hint session for {} threads", s_threadIds.size());
			return;
		}
		if (s_session)
			api.closeSession(s_session);
		s_session = newSession;
	}

	void AddCurrentThread()
	{
		if (!GetApi().manager)
			return;
		std::scoped_lock lock(s_mutex);
		const int32_t threadId = gettid();
		if (std::find(s_threadIds.begin(), s_threadIds.end(), threadId) != s_threadIds.end())
			return;
		s_threadIds.push_back(threadId);
		UpdateSession();
	}

	void ReportFrame()
	{
		const Api& api = GetApi();
		if (!api.manager)
			return;
		const auto now = std::chrono::steady_clock::now();
		std::scoped_lock lock(s_mutex);
		const auto previous = s_lastFrame;
		s_lastFrame = now;
		if (!s_session || previous.time_since_epoch().count() == 0)
			return;
		int64_t frameDuration = std::chrono::duration_cast<std::chrono::nanoseconds>(now - previous).count();
		if (frameDuration <= 0)
			return;
		api.reportActualWorkDuration(s_session, std::min(frameDuration, MAX_REPORTED_DURATION_NS));
	}
} // namespace AndroidPerformanceHint
