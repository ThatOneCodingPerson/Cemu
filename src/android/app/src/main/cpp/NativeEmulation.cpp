#include "AndroidFilesystemCallbacks.h"
#include "AndroidInputHelpers.h"
#include "Cafe/CafeSystem.h"
#include "Cafe/HW/Latte/Core/Latte.h"
#include "Cafe/HW/Latte/Core/LatteOverlay.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanRenderer.h"
#include "Cafe/OS/libs/snd_core/ax.h"
#include "Cafe/TitleList/TitleId.h"
#include "Cafe/TitleList/TitleList.h"
#include "Cemu/ncrypto/ncrypto.h"
#include "config/ActiveSettings.h"
#include "input/InputManager.h"
#include "JNIUtils.h"
#include "WindowSystem.h"

#if HAS_CUBEB
#include "audio/CubebAPI.h"
#endif // HAS_CUBEB

#include <android/native_window_jni.h>
#include <dlfcn.h>

// forward declaration from main.cpp
void CemuCommonInit();

namespace NativeEmulation
{

	void CreateAudioDevice(IAudioAPI::AudioAPI audioApi, AudioChannels channels, sint32 volume, bool isTV)
	{
		constexpr int AX_FRAMES_PER_GROUP = 4;
		std::unique_lock lock(g_audioMutex);
		auto& audioDevice = isTV ? g_tvAudio : g_padAudio;

		switch (audioApi)
		{
#if HAS_CUBEB
		case IAudioAPI::AudioAPI::Cubeb:
		{
			audioDevice.reset();
			auto deviceDescriptionPtr = std::make_shared<CubebAPI::CubebDeviceDescription>(nullptr, std::string(), std::wstring());
			audioDevice = IAudioAPI::CreateDevice(
				IAudioAPI::AudioAPI::Cubeb,
				deviceDescriptionPtr,
				48000,
				CemuConfig::AudioChannelsToNChannels(channels),
				snd_core::AX_SAMPLES_PER_3MS_48KHZ * AX_FRAMES_PER_GROUP,
				16);
			audioDevice->SetVolume(volume);
			break;
		}
#endif // HAS_CUBEB
		default:
			cemuLog_log(LogType::Force, "Invalid audio api: {}", audioApi);
			break;
		}
	}

	void InitializeAudioDevices()
	{
		auto& config = GetConfig();
		if (!config.tv_device.empty())
			CreateAudioDevice(IAudioAPI::AudioAPI::Cubeb, config.tv_channels, config.tv_volume, true);

		if (!config.pad_device.empty())
			CreateAudioDevice(IAudioAPI::AudioAPI::Cubeb, config.pad_channels, config.pad_volume, false);
	}

	// exit() (called by the core after fatal errors) runs atexit handlers and static destructors in reverse
	// registration order. Destructors of globals like g_renderer or the Latte std::thread crash while emulation
	// threads are still running, so we register a handler that ends the process first. Registered again after
	// statics created later (renderer init) so it always runs before their destructors.
	void RegisterFastExit()
	{
		std::atexit([] {
			cemuLog_waitForFlush();
			fflush(nullptr);
			_exit(0);
		});
	}

	void SetDefaultDeviceController()
	{
		for (size_t i = 0; i < InputManager::kMaxController; ++i)
		{
			auto emulatedController = InputManager::instance().get_controller(i);

			if (!emulatedController)
			{
				continue;
			}

			for (const auto& controller : emulatedController->get_controllers())
			{
				if (controller->api() == InputAPI::Device)
				{
					return;
				}
			}
		}

		if (auto emulatedController = InputManager::instance().get_controller(0))
		{
			auto controller = CreateDefaultDeviceController();
			emulatedController->add_controller(controller);
		}
	}

	void CreateCemuDirectories()
	{
		const auto mlc = ActiveSettings::GetMlcPath();

		// create sys/usr folder in mlc01
		const auto sysFolder = mlc / "sys";
		fs::create_directories(sysFolder);

		const auto usrFolder = mlc / "usr";
		fs::create_directories(usrFolder);
		fs::create_directories(usrFolder / "title/00050000"); // base
		fs::create_directories(usrFolder / "title/0005000c"); // dlc
		fs::create_directories(usrFolder / "title/0005000e"); // update

		// Mii Maker save folders {0x500101004A000, 0x500101004A100, 0x500101004A200},
		fs::create_directories(mlc / "usr/save/00050010/1004a100/user/common/db");
		fs::create_directories(mlc / "usr/save/00050010/1004a000/user/common/db");
		fs::create_directories(mlc / "usr/save/00050010/1004a200/user/common/db");

		// lang files
		auto langDir = mlc / "sys/title/0005001b/1005c000/content";
		fs::create_directories(langDir);

		auto langFile = langDir / "language.txt";

		if (!fs::exists(langFile))
		{
			if (std::ofstream file{langFile})
			{
				const auto langStrings = {"ja", "en", "fr", "de", "it", "es", "ko", "nl", "pt", "ru", "zh"};
				for (const char* lang : langStrings)
					fmt::println(file, R"("{}",)", lang);
			}
		}

		auto countryFile = langDir / "country.txt";
		if (!fs::exists(countryFile))
		{
			if (std::ofstream file{countryFile})
			{
				for (sint32 i = 0; i < 201; i++)
				{
					const char* countryCode = NCrypto::GetCountryAsString(i);
					if (boost::iequals(countryCode, "NN"))
						fmt::println(file, "NULL,");
					else
						fmt::println(file, R"("{}",)", countryCode);
				}
			}
		}

		// cemu directories
		fs::create_directories(ActiveSettings::GetConfigPath("controllerProfiles"));
		fs::create_directories(ActiveSettings::GetUserDataPath("memorySearcher"));
	}

	enum PrepareTitleResult : sint32
	{
		SUCCESSFUL = 0,
		ERROR_GAME_BASE_FILES_NOT_FOUND = 1,
		ERROR_NO_DISC_KEY = 2,
		ERROR_NO_TITLE_TIK = 3,
		ERROR_UNKNOWN = 4,
		ERROR_INVALID_EXECUTABLE = 5,
		ERROR_UNABLE_TO_MOUNT = 6,
	};

	PrepareTitleResult ToPrepareTitleResult(CafeSystem::PREPARE_STATUS_CODE statusCode)
	{
		switch (statusCode)
		{
		case CafeSystem::PREPARE_STATUS_CODE::SUCCESS:
			return SUCCESSFUL;
		case CafeSystem::PREPARE_STATUS_CODE::INVALID_RPX:
			return ERROR_INVALID_EXECUTABLE;
		case CafeSystem::PREPARE_STATUS_CODE::UNABLE_TO_MOUNT:
			return ERROR_UNABLE_TO_MOUNT;
		}
		return ERROR_UNKNOWN;
	}

	class TestSurface
	{
	  public:
		TestSurface()
		{
			JNIEnv* env = JNIUtils::GetEnv();

			jclass surfaceTextureClass = env->FindClass("android/graphics/SurfaceTexture");
			jmethodID ctorSurfaceTexture = env->GetMethodID(surfaceTextureClass, "<init>", "(I)V");
			jobject localSurfaceTexture = env->NewObject(surfaceTextureClass, ctorSurfaceTexture, 0);

			jclass surfaceClass = env->FindClass("android/view/Surface");
			jmethodID ctorSurface = env->GetMethodID(surfaceClass, "<init>", "(Landroid/graphics/SurfaceTexture;)V");
			jobject localSurface = env->NewObject(surfaceClass, ctorSurface, localSurfaceTexture);

			m_surfaceTexture = env->NewGlobalRef(localSurfaceTexture);
			m_surface = env->NewGlobalRef(localSurface);

			m_window = ANativeWindow_fromSurface(env, m_surface); // returns an acquired reference

			env->DeleteLocalRef(localSurfaceTexture);
			env->DeleteLocalRef(localSurface);
			env->DeleteLocalRef(surfaceTextureClass);
			env->DeleteLocalRef(surfaceClass);
		}

		~TestSurface()
		{
			JNIEnv* env = JNIUtils::GetEnv();

			ANativeWindow_release(m_window);

			jclass surfaceClass = env->FindClass("android/view/Surface");
			jmethodID releaseSurface = env->GetMethodID(surfaceClass, "release", "()V");
			env->CallVoidMethod(m_surface, releaseSurface);
			env->DeleteGlobalRef(m_surface);
			m_surface = nullptr;
			env->DeleteLocalRef(surfaceClass);

			jclass surfaceTextureClass = env->FindClass("android/graphics/SurfaceTexture");
			jmethodID releaseSurfaceTexture = env->GetMethodID(surfaceTextureClass, "release", "()V");
			env->CallVoidMethod(m_surfaceTexture, releaseSurfaceTexture);
			env->DeleteGlobalRef(m_surfaceTexture);
			m_surfaceTexture = nullptr;
			env->DeleteLocalRef(surfaceTextureClass);
		}

		ANativeWindow* getWindow()
		{
			return m_window;
		}

	  private:
		ANativeWindow* m_window;
		jobject m_surface = nullptr;
		jobject m_surfaceTexture = nullptr;
	};
} // namespace NativeEmulation

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setReplaceTVWithPadView([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean swapped)
{
	// Emulate pressing the TAB key for showing DRC instead of TV
	WindowSystem::GetWindowInfo().set_keystate(static_cast<uint32>(WindowSystem::PlatformKeyCodes::TAB), swapped);
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setSwapScreens([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean swapped)
{
	// applied on the GPU thread (LatteRenderTarget_itHLECopyColorBufferToScanBuffer), which owns isDRCPrimary
	WindowSystem::GetWindowInfo().swap_screens = swapped;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setExternalScreenRotatedLeft([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jboolean rotated)
{
	WindowSystem::GetWindowInfo().external_screen_rotated_left = rotated;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeEmulation([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	// filesystem errors (e.g. storage unavailable) must surface as a Java exception, not std::terminate
	JNIUtils::HandleNativeException(env, [&]() {
		FilesystemAndroid::SetFilesystemCallbacks(std::make_shared<AndroidFilesystemCallbacks>());
		GetConfigHandle().SetFilename(ActiveSettings::GetConfigPath("settings.xml").generic_wstring());
		NativeEmulation::CreateCemuDirectories();
		NetworkConfig::LoadOnce();
		ActiveSettings::Init();
		LatteOverlay_init();
		CemuCommonInit();
		NativeEmulation::RegisterFastExit();
	});
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeRenderer(JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	static std::unique_ptr<NativeEmulation::TestSurface> testSurface;

	JNIUtils::HandleNativeException(env, [&]() {
		// without a loader every Vulkan function pointer is null and the renderer would crash with SIGSEGV
		if (!InitializeGlobalVulkan())
			throw std::runtime_error("Failed to load the Vulkan driver. Try a different GPU driver in the settings.");
		testSurface = std::make_unique<NativeEmulation::TestSurface>();

		WindowSystem::GetWindowInfo().window_main.surface = testSurface->getWindow();

		g_renderer = std::make_unique<VulkanRenderer>();
		NativeEmulation::RegisterFastExit();
	});
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setDPI([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jfloat dpi)
{
	auto& windowInfo = WindowSystem::GetWindowInfo();
	windowInfo.dpi_scale = windowInfo.pad_dpi_scale = dpi;
}


extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_supportsLoadingCustomDriver([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	return SupportsLoadingCustomDriver();
}

// Returns {device name, driver version, Vulkan version} of the system driver or null, for the driver download screen.
// Uses its own instance from the system loader, so it neither needs nor touches the emulator's Vulkan state (which
// may use a custom driver). The loader stays loaded, unloading vendor drivers isn't safe on every device.
extern "C" [[maybe_unused]] JNIEXPORT jobjectArray JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_getSystemGpuInfo(JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	void* loader = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
	if (!loader)
		return nullptr;
	auto getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(loader, "vkGetInstanceProcAddr"));
	if (!getInstanceProcAddr)
		return nullptr;
	auto createInstance = reinterpret_cast<PFN_vkCreateInstance>(getInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance"));
	if (!createInstance)
		return nullptr;

	VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
	appInfo.pApplicationName = EMULATOR_NAME;
	appInfo.apiVersion = VK_API_VERSION_1_1;
	VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
	createInfo.pApplicationInfo = &appInfo;
	VkInstance instance = VK_NULL_HANDLE;
	if (createInstance(&createInfo, nullptr, &instance) != VK_SUCCESS)
		return nullptr;

	auto destroyInstance = reinterpret_cast<PFN_vkDestroyInstance>(getInstanceProcAddr(instance, "vkDestroyInstance"));
	auto enumeratePhysicalDevices = reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(getInstanceProcAddr(instance, "vkEnumeratePhysicalDevices"));
	auto getPhysicalDeviceProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(getInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties"));
	std::optional<VkPhysicalDeviceProperties> properties;
	if (enumeratePhysicalDevices && getPhysicalDeviceProperties)
	{
		uint32_t deviceCount = 1;
		VkPhysicalDevice device = VK_NULL_HANDLE;
		VkResult result = enumeratePhysicalDevices(instance, &deviceCount, &device);
		if ((result == VK_SUCCESS || result == VK_INCOMPLETE) && deviceCount > 0)
		{
			properties.emplace();
			getPhysicalDeviceProperties(device, &*properties);
		}
	}
	if (destroyInstance)
		destroyInstance(instance, nullptr);
	if (!properties)
		return nullptr;

	// driverVersion is vendor defined; Qualcomm and Mesa both use the classic 10/10/12 bit layout (e.g. 512.744.0)
	uint32 driverVersion = properties->driverVersion;
	std::vector<std::string> info{
		std::string(properties->deviceName, strnlen(properties->deviceName, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE)),
		fmt::format("{}.{}.{}", driverVersion >> 22, (driverVersion >> 12) & 0x3FF, driverVersion & 0xFFF),
		fmt::format("{}.{}.{}", VK_API_VERSION_MAJOR(properties->apiVersion), VK_API_VERSION_MINOR(properties->apiVersion), VK_API_VERSION_PATCH(properties->apiVersion)),
	};
	return JNIUtils::CreateStringObjectArray(env, info);
}

// Publishes the window of a canvas for the GPU thread, which creates the Vulkan surface/swapchain for it (see
// WindowSystem::AndroidCanvasInfo and VulkanRenderer::SyncCanvasWindow). Never blocks.
extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setSurface(JNIEnv* env, [[maybe_unused]] jclass clazz, jobject surface, jboolean isMainCanvas)
{
	auto& canvas = WindowSystem::GetWindowInfo().GetAndroidCanvas(isMainCanvas);
	ANativeWindow* newWindow = surface != nullptr ? ANativeWindow_fromSurface(env, surface) : nullptr; // acquired reference
	ANativeWindow* oldWindow = nullptr;
	{
		std::scoped_lock lock(canvas.mutex);
		// surfaceChanged is also called for size changes of the same surface; the size is applied by the swapchain
		// update. Recreating the VkSurface for the same window would fail with VK_ERROR_NATIVE_WINDOW_IN_USE_KHR.
		// (We hold a reference to the current window, so a different surface can't have the same address.)
		if (newWindow == canvas.window)
		{
			if (newWindow)
				ANativeWindow_release(newWindow);
			return;
		}
		oldWindow = static_cast<ANativeWindow*>(canvas.window);
		canvas.window = newWindow;
		++canvas.generation;
	}
	if (!isMainCanvas)
		WindowSystem::GetWindowInfo().pad_open = newWindow != nullptr;
	if (oldWindow)
		ANativeWindow_release(oldWindow);
}

// Called from SurfaceHolder.Callback.surfaceDestroyed. Unpublishes the window if it is the current one of the canvas
// and waits (bounded) until the GPU thread stopped using it, since Android tears the surface down once this returns.
// Returns whether the surface was the current one (a stale callback of a replaced view returns false).
extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_clearSurface(JNIEnv* env, [[maybe_unused]] jclass clazz, jobject surface, jboolean isMainCanvas)
{
	constexpr auto MAX_WAIT_FOR_GPU_THREAD = std::chrono::milliseconds(500);

	auto& canvas = WindowSystem::GetWindowInfo().GetAndroidCanvas(isMainCanvas);
	ANativeWindow* window = surface != nullptr ? ANativeWindow_fromSurface(env, surface) : nullptr;
	ANativeWindow* oldWindow = nullptr;
	bool wasCurrent = false;
	{
		std::unique_lock lock(canvas.mutex);
		wasCurrent = window != nullptr && window == canvas.window;
		if (wasCurrent)
		{
			oldWindow = static_cast<ANativeWindow*>(canvas.window);
			canvas.window = nullptr;
			const uint64 generation = ++canvas.generation;
			if (!isMainCanvas)
				WindowSystem::GetWindowInfo().pad_open = false;
			// Never wait unbounded: if the GPU thread is stuck (e.g. compiling shaders) the references it holds keep
			// the window alive and presenting to it just fails with VK_ERROR_SURFACE_LOST_KHR / OUT_OF_DATE
			const bool released = canvas.cv.wait_for(lock, MAX_WAIT_FOR_GPU_THREAD, [&] {
				return !canvas.consumerActive || !canvas.consumerUsesWindow || canvas.appliedGeneration >= generation;
			});
			if (!released)
				cemuLog_log(LogType::Force, "{} surface destroyed while the GPU thread was still using it", isMainCanvas ? "TV" : "GamePad");
		}
	}
	if (oldWindow)
		ANativeWindow_release(oldWindow);
	if (window)
		ANativeWindow_release(window);
	return wasCurrent;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setSurfaceSize([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jint width, jint height, jboolean isMainCanvas)
{
	auto& windowInfo = WindowSystem::GetWindowInfo();
	if (isMainCanvas)
	{
		windowInfo.width = windowInfo.phys_width = width;
		windowInfo.height = windowInfo.phys_height = height;
	}
	else
	{
		windowInfo.pad_width = windowInfo.phys_pad_width = width;
		windowInfo.pad_height = windowInfo.phys_pad_height = height;
	}
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_initializeSystems([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	NativeEmulation::SetDefaultDeviceController();
	WindowSystem::GetWindowInfo().set_keystatesup();
	NativeEmulation::InitializeAudioDevices();
}

extern "C" [[maybe_unused]] JNIEXPORT jint JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_prepareTitle([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jstring launchPathJava)
{
	using enum NativeEmulation::PrepareTitleResult;
	jint result = ERROR_UNKNOWN;
	// filesystem/title parsing errors must not escape JNI (std::terminate); the Kotlin side reports them
	JNIUtils::HandleNativeException(env, [&]() {
		fs::path launchPath = JNIUtils::FromJString(env, launchPathJava);

		TitleInfo launchTitle{launchPath};

		if (launchTitle.IsValid())
		{
			// the title might not be in the TitleList, so we add it as a temporary entry
			CafeTitleList::AddTitleFromPath(launchPath);
			// title is valid, launch from TitleId
			TitleId baseTitleId;
			if (!CafeTitleList::FindBaseTitleId(launchTitle.GetAppTitleId(), baseTitleId))
			{
				result = ERROR_GAME_BASE_FILES_NOT_FOUND;
				return;
			}
			result = NativeEmulation::ToPrepareTitleResult(CafeSystem::PrepareForegroundTitle(baseTitleId));
		}
		else // if (launchTitle.GetFormat() == TitleInfo::TitleDataFormat::INVALID_STRUCTURE )
		{
			// title is invalid, if it's an RPX/ELF we can launch it directly
			// otherwise it's an error
			CafeTitleFileType fileType = DetermineCafeSystemFileType(launchPath);
			if (fileType == CafeTitleFileType::RPX || fileType == CafeTitleFileType::ELF)
				result = NativeEmulation::ToPrepareTitleResult(CafeSystem::PrepareForegroundTitleFromStandaloneRPX(launchPath));
			else if (launchTitle.GetInvalidReason() == TitleInfo::InvalidReason::NO_DISC_KEY)
				result = ERROR_NO_DISC_KEY;
			else if (launchTitle.GetInvalidReason() == TitleInfo::InvalidReason::NO_TITLE_TIK)
				result = ERROR_NO_TITLE_TIK;
			else
				result = ERROR_UNKNOWN;
		}
	});
	return result;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_launchTitle([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	CafeSystem::LaunchForegroundTitle();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_pauseTitle([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	CafeSystem::PauseTitle();
	// the app is going to the background where it may be killed at any time, save new pipelines now
	if (g_renderer && CafeSystem::IsTitleRunning())
		VulkanRenderer::GetInstance()->RequestPipelineCacheSave();
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_resumeTitle([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	CafeSystem::ResumeTitle();
}

namespace NativeEmulation
{
	// The renderer captures a frame on the GPU thread and hands the RGB data to OnScreenshotCaptured on a detached
	// thread. Kotlin encodes and saves it (NativeEmulation.onScreenshotCaptured).
	JNIUtils::Scopedjclass s_nativeEmulationClass;
	jmethodID s_onScreenshotCapturedMethod = nullptr;
	// Renderer::RequestScreenshot isn't synchronized with a capture in progress, so allow one request at a time.
	// A request stays pending while no frame is presented (e.g. paused), allow a new one after a while.
	constexpr auto SCREENSHOT_REQUEST_TIMEOUT = std::chrono::seconds(5);
	std::atomic_bool s_screenshotPending = false;
	std::atomic<std::chrono::steady_clock::time_point> s_screenshotRequestTime;

	std::optional<std::string> OnScreenshotCaptured(const std::vector<uint8>& rgbData, int width, int height, [[maybe_unused]] bool mainWindow)
	{
		const size_t pixelCount = static_cast<size_t>(width) * height;
		if (s_onScreenshotCapturedMethod != nullptr && width > 0 && height > 0 && rgbData.size() >= pixelCount * 3)
		{
			// Android bitmaps (ARGB_8888) store RGBA bytes
			std::vector<uint8> rgbaData(pixelCount * 4);
			for (size_t i = 0; i < pixelCount; i++)
			{
				rgbaData[i * 4 + 0] = rgbData[i * 3 + 0];
				rgbaData[i * 4 + 1] = rgbData[i * 3 + 1];
				rgbaData[i * 4 + 2] = rgbData[i * 3 + 2];
				rgbaData[i * 4 + 3] = 0xFF;
			}
			JNIUtils::RunOnJNIWorker([&](JNIEnv* env) {
				jbyteArray array = env->NewByteArray(static_cast<jsize>(rgbaData.size()));
				if (array == nullptr)
				{
					JNIUtils::CheckAndClearException(env); // OutOfMemoryError
					return;
				}
				env->SetByteArrayRegion(array, 0, static_cast<jsize>(rgbaData.size()), reinterpret_cast<const jbyte*>(rgbaData.data()));
				env->CallStaticVoidMethod(*s_nativeEmulationClass, s_onScreenshotCapturedMethod, array, width, height);
				JNIUtils::CheckAndClearException(env);
				env->DeleteLocalRef(array);
			});
		}
		s_screenshotPending = false;
		return std::nullopt; // Kotlin reports the result
	}
} // namespace NativeEmulation

extern "C" [[maybe_unused]] JNIEXPORT jboolean JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_requestScreenshot(JNIEnv* env, jclass clazz)
{
	if (!g_renderer || !CafeSystem::IsTitleRunning())
		return false;
	if (NativeEmulation::s_onScreenshotCapturedMethod == nullptr)
	{
		NativeEmulation::s_nativeEmulationClass = JNIUtils::Scopedjclass(clazz);
		NativeEmulation::s_onScreenshotCapturedMethod = env->GetStaticMethodID(clazz, "onScreenshotCaptured", "([BII)V");
		if (JNIUtils::CheckAndClearException(env) || NativeEmulation::s_onScreenshotCapturedMethod == nullptr)
			return false;
	}
	const auto now = std::chrono::steady_clock::now();
	if (NativeEmulation::s_screenshotPending && now - NativeEmulation::s_screenshotRequestTime.load() < NativeEmulation::SCREENSHOT_REQUEST_TIMEOUT)
		return false;
	NativeEmulation::s_screenshotPending = true;
	NativeEmulation::s_screenshotRequestTime = now;
	g_renderer->RequestScreenshot(NativeEmulation::OnScreenshotCaptured);
	return true;
}

// the GamePad may be shown on another display with a different density (e.g. the AYN Thor's bottom screen)
extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_setPadDPI([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz, jfloat dpi)
{
	WindowSystem::GetWindowInfo().pad_dpi_scale = dpi;
}

extern "C" [[maybe_unused]] JNIEXPORT void JNICALL
Java_info_cemu_cemu_nativeinterface_NativeEmulation_quitProcess([[maybe_unused]] JNIEnv* env, [[maybe_unused]] jclass clazz)
{
	// exit() would run static destructors (g_renderer, the unjoined Latte thread) while the emulation threads are
	// still running, which crashes. Flush what matters and terminate without them.
	cemuLog_log(LogType::Force, "Quitting emulation");
	// otherwise pipelines compiled since the last periodic save are lost (stutter on the next launch)
	if (g_renderer && CafeSystem::IsTitleRunning() && !VulkanRenderer::GetInstance()->FlushPipelineCache(std::chrono::seconds(2)))
		cemuLog_log(LogType::Force, "Pipeline cache is busy, not saved on quit");
	cemuLog_waitForFlush();
	fflush(nullptr);
	_exit(0);
}
