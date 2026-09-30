#pragma once

#include "util/math/vector2.h"
#include <vulkan/vulkan_core.h>

#if BOOST_PLAT_ANDROID
#include <android/native_window.h>
#endif

struct SwapchainInfoVk
{
	enum class VSync
	{
		// values here must match GeneralSettings2::m_vsync
		Immediate = 0,
		FIFO = 1,
		MAILBOX = 2,
		SYNC_AND_LIMIT = 3, // synchronize emulated vsync events to monitor vsync. But skip events if rate higher than virtual vsync period
	};

	struct SwapchainSupportDetails
	{
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};

	void Cleanup();
	void Create();

	bool IsValid() const;

	void WaitAvailableFence();
	void ResetAvailableFence() const;

	bool AcquireImage();
	// retrieve semaphore of last acquire for submitting a wait operation
	// only one wait operation must be submitted per acquire (which submits a single signal operation)
	// therefore subsequent calls will return a NULL handle
	VkSemaphore ConsumeAcquireSemaphore();

	static void UnrecoverableError(const char* errMsg);

	static SwapchainSupportDetails QuerySwapchainSupport(VkSurfaceKHR surface, const VkPhysicalDevice& device);

	VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& modes);
	VkSurfaceFormatKHR ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
	VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;

	VkSwapchainCreateInfoKHR CreateSwapchainCreateInfo(VkSurfaceKHR surface, const SwapchainSupportDetails& swapchainSupport, const VkSurfaceFormatKHR& surfaceFormat, uint32 imageCount, const VkExtent2D& extent);


	VkExtent2D getExtent() const
	{
		return m_actualExtent;
	}

	// the window's size. With Android pre-rotation getExtent() is the image size, in the display's natural orientation
	VkExtent2D getLogicalExtent() const
	{
#if BOOST_PLAT_ANDROID
		if (IsRotatedQuarterTurn())
			return {m_actualExtent.height, m_actualExtent.width};
#endif
		return m_actualExtent;
	}

#if BOOST_PLAT_ANDROID
	// Vulkan pre-rotation (developer.android.com/games/optimize/vulkan-prerotation): with
	// CemuConfig::vk_pre_rotation the swapchain takes the display's current transform, so the compositor doesn't
	// rotate every frame. The renderer keeps drawing in window coordinates and maps them to the image with these.
	VkSurfaceTransformFlagBitsKHR m_preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
	// a present returned VK_SUBOPTIMAL_KHR: the display may have rotated. Android also returns it for every frame
	// the compositor rotates, so it's only a reason to look
	bool m_preTransformMayBeOutdated = false;

	bool IsRotatedQuarterTurn() const
	{
		return m_preTransform & (VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR | VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR);
	}
	// clockwise, for the output shaders' specialization constant
	sint32 GetPreRotationQuarterTurns() const;
	void ToImagePosition(float& x, float& y) const;
	void ToImageViewport(VkViewport& viewport) const;
	// true if the swapchain should be recreated for a changed display rotation
	bool IsPreTransformOutdated();
#endif

#if BOOST_PLAT_ANDROID
	// only called on the Latte thread (see VulkanRenderer::SyncCanvasWindow), holds its own reference to window
	SwapchainInfoVk(bool mainWindow, Vector2i size, ANativeWindow* window);
	SwapchainInfoVk(SwapchainInfoVk&&) = delete;
#else
	SwapchainInfoVk(bool mainWindow, Vector2i size);
	SwapchainInfoVk(SwapchainInfoVk&&) noexcept = default;
#endif
	SwapchainInfoVk(const SwapchainInfoVk&) = delete;
	~SwapchainInfoVk();

	bool mainWindow{};

#if BOOST_PLAT_ANDROID
	bool surfaceWasLost = false;
	// frame generation was turned on when the swapchain was created (TV only)
	bool m_frameGenRequested = false;
	// and the images can be copied from (the generated frames are copied in), presentation is FIFO
	bool m_frameGenCapable = false;
#endif

	bool m_shouldRecreate = false;
	VSync m_vsyncState = VSync::Immediate;
	bool hasDefinedSwapchainImage{}; // indicates if the swapchain image is in a defined state
	VkInstance m_instance{};
	VkPhysicalDevice m_physicalDevice{};
	VkDevice m_logicalDevice{};
	VkSurfaceKHR m_surface{};
	VkSurfaceFormatKHR m_surfaceFormat{};
	VkSwapchainKHR m_swapchain{};
	Vector2i m_desiredExtent{};
	VkExtent2D m_actualExtent{};
	uint32 swapchainImageIndex = (uint32)-1;
	uint64 m_presentId = 1;
	uint64 m_queueDepth = 0; // number of frames with pending presentation requests
	uint64 m_maxQueued = 0; // the maximum number of frames with presentation requests.


	// swapchain image ringbuffer (indexed by swapchainImageIndex)
	std::vector<VkImage> m_swapchainImages;
	std::vector<VkImageView> m_swapchainImageViews;
	std::vector<VkFramebuffer> m_swapchainFramebuffers;
	std::vector<VkSemaphore> m_presentSemaphores; // indexed by swapchainImageIndex

	VkRenderPass m_swapchainRenderPass = nullptr;

private:
	uint32 m_acquireIndex = 0;
	std::vector<VkSemaphore> m_acquireSemaphores; // indexed by m_acquireIndex
	VkFence m_imageAvailableFence{};
	VkFence m_awaitableFence = VK_NULL_HANDLE;
	VkSemaphore m_currentSemaphore = VK_NULL_HANDLE;

#if BOOST_PLAT_ANDROID
	void RecreateSurface();
	ANativeWindow* m_window = nullptr; // owned reference

	VkSurfaceTransformFlagBitsKHR ChoosePreTransform(const VkSurfaceCapabilitiesKHR& capabilities) const;
	uint32 m_callsSinceTransformCheck = 0;
#endif

	std::array<uint32, 2> m_swapchainQueueFamilyIndices;
};
