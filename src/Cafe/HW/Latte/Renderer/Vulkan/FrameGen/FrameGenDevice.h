#pragma once

#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/FrameGen/LosslessDll.h"

namespace FrameGen
{
	// Runs before the logical device is created. Loads the installed Lossless.dll, reads what its shaders need from
	// the device (SPIR-V version, capabilities, extensions) and checks the physical device for it. VulkanRenderer then
	// enables exactly those features, and only when frame generation can work, so without the DLL the device is
	// created as before.
	class DeviceSetup
	{
	public:
		void Prepare(VkPhysicalDevice physicalDevice, uint32 instanceApiVersion);

		bool IsUsable() const { return m_usable; }
		// empty when Lossless.dll isn't installed
		const std::string& GetUnusableReason() const { return m_unusableReason; }
		// VK_GOOGLE_display_timing is enabled: presents can be given a time (even spacing on fast screens)
		bool HasDisplayTiming() const { return m_usable && m_displayTiming; }

		// links the feature structs to enable in front of `next`, for VkDeviceCreateInfo::pNext
		void* ChainFeatures(void* next);
		void EnableCoreFeatures(VkPhysicalDeviceFeatures& features) const;
		// appends the device extensions to enable, skipping ones already in the list
		void AddExtensions(std::vector<const char*>& extensions) const;

		ShaderModules TakeShaderModules() { return std::move(m_modules); }

	private:
		void SetUnusable(std::string reason);

		bool m_usable = false;
		bool m_displayTiming = false;
		std::string m_unusableReason;
		ShaderModules m_modules;
		std::vector<const char*> m_extensions;

		VkPhysicalDeviceFeatures m_coreFeatures{};
		bool m_enableStorage16 = false;
		bool m_enableFloat16Int8 = false;
		bool m_enableMemoryModel = false;
		VkPhysicalDevice16BitStorageFeatures m_storage16{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
		VkPhysicalDeviceShaderFloat16Int8Features m_float16Int8{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
		VkPhysicalDeviceVulkanMemoryModelFeatures m_memoryModel{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES};
		VkPhysicalDeviceRobustness2FeaturesEXT m_robustness2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
	};
}
