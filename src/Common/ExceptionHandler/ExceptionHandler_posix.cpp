#include <signal.h>
#include <execinfo.h>
#include <string.h>
#include <string>
#include "config/CemuConfig.h"
#include "util/helpers/StringHelpers.h"
#include "ExceptionHandler.h"

#include "Cafe/HW/Espresso/Debugger/GDBStub.h"
#include "Cafe/HW/Espresso/Debugger/GDBBreakpoints.h"

#if BOOST_OS_LINUX
#include "ELFSymbolTable.h"
#endif

#if BOOST_PLAT_ANDROID
#include <android/log.h>
#include <fcntl.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "config/ActiveSettings.h"

// On Android the handler only records a short, async-signal-safe note (logcat + crash.txt) and then gives the
// signal back to the previously installed handler (debuggerd, reached through ART's sigchain). That writes a
// tombstone with native backtraces of all threads and lets Android report the crash normally.
namespace
{
	constexpr int ANDROID_HANDLED_SIGNALS[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT, SIGTRAP, SIGSYS};
	struct sigaction s_previousActions[NSIG]{};
	std::atomic_flag s_crashInProgress = ATOMIC_FLAG_INIT;
	char s_crashFilePath[512]{};

	struct SignalSafeText
	{
		char data[256];
		size_t length = 0;

		void Append(const char* text)
		{
			while (*text && length < sizeof(data) - 1)
				data[length++] = *text++;
		}

		void AppendNumber(uint64 value, uint32 base)
		{
			char digits[24];
			size_t count = 0;
			do
			{
				digits[count++] = "0123456789abcdef"[value % base];
				value /= base;
			} while (value != 0 && count < sizeof(digits));
			while (count > 0 && length < sizeof(data) - 1)
				data[length++] = digits[--count];
		}

		void AppendSigned(sint64 value)
		{
			if (value < 0)
			{
				Append("-");
				value = -value;
			}
			AppendNumber((uint64)value, 10);
		}
	};
} // namespace

void handlerDumpingSignalAndroid(int sig, siginfo_t* info, void* context)
{
	if (!s_crashInProgress.test_and_set())
	{
		SignalSafeText text;
		text.Append("Cemu crashed: signal ");
		text.AppendSigned(sig);
		text.Append(" code ");
		text.AppendSigned(info->si_code);
		text.Append(" addr 0x");
		text.AppendNumber((uint64)(uintptr_t)info->si_addr, 16);
#if defined(__aarch64__)
		text.Append(" pc 0x");
		text.AppendNumber(((ucontext_t*)context)->uc_mcontext.pc, 16);
#endif
		text.Append(" tid ");
		text.AppendSigned(gettid());
		text.data[text.length] = '\0';

		__android_log_write(ANDROID_LOG_FATAL, "Cemu", text.data);
		if (s_crashFilePath[0] != '\0')
		{
			int fd = open(s_crashFilePath, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
			if (fd >= 0)
			{
				text.Append("\n");
				(void)!write(fd, text.data, text.length);
				close(fd);
			}
		}
	}
	// restore the previous handler and re-queue the signal. It is delivered once we return (all signals are masked
	// while in here). Other threads crashing at the same time skip the note above and go straight to debuggerd.
	sigaction(sig, &s_previousActions[sig], nullptr);
	syscall(__NR_rt_tgsigqueueinfo, getpid(), gettid(), sig, info);
}
#endif

#if BOOST_OS_LINUX && !BOOST_PLAT_ANDROID
void DemangleAndPrintBacktrace(char** backtrace, size_t size)
{
	ELFSymbolTable symTable;
	for (char** i = backtrace; i < backtrace + size; i++)
	{
		std::string_view traceLine{*i};

		// basic check to see if the backtrace line matches expected format
		size_t parenthesesOpen = traceLine.find_last_of('(');
		size_t parenthesesClose = traceLine.find_last_of(')');
		size_t offsetPlus = traceLine.find_last_of('+');
		if (!parenthesesOpen || !parenthesesClose || !offsetPlus ||
			 offsetPlus < parenthesesOpen || offsetPlus > parenthesesClose)
		{
			// fall back to default string
            CrashLog_WriteLine(traceLine);
			continue;
		}

		// attempt to resolve symbol from regular symbol table if missing from dynamic symbol table
		uint64 newOffset = -1;
		std::string_view symbolName = traceLine.substr(parenthesesOpen+1, offsetPlus-parenthesesOpen-1);
		if (symbolName.empty())
		{
			uint64 symbolOffset = StringHelpers::ToInt64(traceLine.substr(offsetPlus+1,offsetPlus+1-parenthesesClose-1));
			symbolName = symTable.OffsetToSymbol(symbolOffset, newOffset);
		}

        CrashLog_WriteLine(traceLine.substr(0, parenthesesOpen+1), false);

        CrashLog_WriteLine(boost::core::demangle(symbolName.empty() ? "" : symbolName.data()), false);

		// print relative or existing symbol offset.
        CrashLog_WriteLine("+", false);
		if (newOffset != -1)
		{
            CrashLog_WriteLine(fmt::format("0x{:x}", newOffset), false);
            CrashLog_WriteLine(traceLine.substr(parenthesesClose));
		}
		else
		{
            CrashLog_WriteLine(traceLine.substr(offsetPlus+1));
		}
	}
}
#endif

// handle signals that would dump core, print stacktrace and then dump depending on config
void handlerDumpingSignal(int sig, siginfo_t *info, void *context)
{
#if defined(ARCH_X86_64) && BOOST_OS_LINUX
	// Check for hardware breakpoints
	if (info->si_signo == SIGTRAP && info->si_code == TRAP_HWBKPT)
	{
		uint64 dr6 = _ReadDR6();
		g_gdbstub->HandleAccessException(dr6);
		return;
	}
#endif

    if(!CrashLog_Create())
        return; // give up if crashlog was already created

    char* sigName = strsignal(sig);
	if (sigName)
	{
		printf("%s!\n", sigName);
	}
	else
	{
		// should never be the case
		printf("Unknown core dumping signal!\n");
	}
    CrashLog_WriteLine(fmt::format("Error: signal {}:", sig));
#if BOOST_PLAT_ANDROID
    CrashLog_WriteLine("Native backtrace: see the Android tombstone"); // Android uses handlerDumpingSignalAndroid
#else
	void* backtraceArray[128];
	size_t size;

	// get void*'s for all entries on the stack
	size = backtrace(backtraceArray, 128);
    // replace the deepest entry with the actual crash address
#if defined(ARCH_X86_64) && BOOST_OS_LINUX > 0
    ucontext_t *uc = (ucontext_t *)context;
    backtraceArray[0] = (void *)uc->uc_mcontext.gregs[REG_RIP];
#endif

#if BOOST_OS_LINUX
	char** symbol_trace = backtrace_symbols(backtraceArray, size);

	if (symbol_trace)
	{
        DemangleAndPrintBacktrace(symbol_trace, size);
		free(symbol_trace);
	}
	else
	{
        CrashLog_WriteLine("Failed to read backtrace");
	}
#else
	backtrace_symbols_fd(backtraceArray, size, STDERR_FILENO);
#endif
#endif

    std::cerr << fmt::format("\nStacktrace and additional info written to:") << std::endl;
    std::cerr << cemuLog_GetLogFilePath().generic_string() << std::endl;

    CrashLog_SetOutputChannels(false, true);
    ExceptionHandler_LogGeneralInfo();
    CrashLog_SetOutputChannels(true, true);

	if (GetConfig().crash_dump == CrashDump::Enabled)
	{
		// reset signal handler to default and re-raise signal to dump core
		signal(sig, SIG_DFL);
		raise(sig);
		return;
	}
	// exit process ignoring all issues
	_Exit(1);
}

void handler_SIGINT(int sig)
{
	/*
	 * Received when pressing CTRL + C in a console
	 * Ideally should be exiting cleanly after saving settings but currently
	 * there's no clean exit pathway (at least on linux) and exiting the app
	 * by any mean ends up with a SIGABRT from the standard library destroying
	 * threads.
	 */
	_Exit(0);
}

void ExceptionHandler_Init()
{
#if BOOST_PLAT_ANDROID
	try
	{
		const std::string crashFilePath = _pathToUtf8(ActiveSettings::GetUserDataPath("crash.txt"));
		strncpy(s_crashFilePath, crashFilePath.c_str(), sizeof(s_crashFilePath) - 1);
	}
	catch (const std::exception&)
	{
	}
	// SIGQUIT is left alone (ART uses it to dump ANR traces), as are SIGINT/SIGTERM
	struct sigaction androidAction{};
	sigfillset(&androidAction.sa_mask);
	androidAction.sa_flags = SA_SIGINFO | SA_ONSTACK;
	androidAction.sa_sigaction = handlerDumpingSignalAndroid;
	for (int sig : ANDROID_HANDLED_SIGNALS)
		sigaction(sig, &androidAction, &s_previousActions[sig]);
	return;
#endif
	struct sigaction action;
	action.sa_flags = 0;
	sigfillset(&action.sa_mask); // don't allow signals to be interrupted

	action.sa_handler = handler_SIGINT;
	sigaction(SIGINT, &action, nullptr);
	sigaction(SIGTERM, &action, nullptr);

    action.sa_flags = SA_SIGINFO;
    action.sa_handler = nullptr;
	action.sa_sigaction = handlerDumpingSignal;
	sigaction(SIGABRT, &action, nullptr);
	sigaction(SIGBUS, &action, nullptr);
	sigaction(SIGFPE, &action, nullptr);
	sigaction(SIGILL, &action, nullptr);
	sigaction(SIGIOT, &action, nullptr);
	sigaction(SIGQUIT, &action, nullptr);
	sigaction(SIGSEGV, &action, nullptr);
	sigaction(SIGSYS, &action, nullptr);
	sigaction(SIGTRAP, &action, nullptr);
}
