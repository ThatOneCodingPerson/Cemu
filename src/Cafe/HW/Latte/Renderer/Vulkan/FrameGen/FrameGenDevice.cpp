#include "Cafe/HW/Latte/Renderer/Vulkan/FrameGen/FrameGenDevice.h"

namespace FrameGen
{
	namespace
	{
		constexpr const char* ROBUSTNESS2_EXT = "VK_EXT_robustness2";
		constexpr const char* ROBUSTNESS2_KHR = "VK_KHR_robustness2"; // the same feature struct, promoted in 2025

		// SPIR-V specification, 3.31 Capability
		namespace SpirvCapability
		{
			constexpr uint32 Matrix = 0;
			constexpr uint32 Shader = 1;
			constexpr uint32 Float16 = 9;
			constexpr uint32 Float64 = 10;
			constexpr uint32 Int64 = 11;
			constexpr uint32 Int16 = 22;
			constexpr uint32 ImageGatherExtended = 25;
			constexpr uint32 StorageImageMultisample = 27;
			constexpr uint32 UniformBufferArrayDynamicIndexing = 28;
			constexpr uint32 SampledImageArrayDynamicIndexing = 29;
			constexpr uint32 StorageBufferArrayDynamicIndexing = 30;
			constexpr uint32 StorageImageArrayDynamicIndexing = 31;
			constexpr uint32 ImageCubeArray = 34;
			constexpr uint32 Int8 = 39;
			constexpr uint32 MinLod = 42;
			constexpr uint32 Sampled1D = 43;
			constexpr uint32 Image1D = 44;
			constexpr uint32 SampledCubeArray = 45;
			constexpr uint32 SampledBuffer = 46;
			constexpr uint32 ImageBuffer = 47;
			constexpr uint32 StorageImageExtendedFormats = 49;
			constexpr uint32 ImageQuery = 50;
			constexpr uint32 DerivativeControl = 51;
			constexpr uint32 StorageImageReadWithoutFormat = 55;
			constexpr uint32 StorageImageWriteWithoutFormat = 56;
			constexpr uint32 GroupNonUniform = 61;
			constexpr uint32 GroupNonUniformQuad = 68;
			constexpr uint32 StorageBuffer16BitAccess = 4433;
			constexpr uint32 UniformAndStorageBuffer16BitAccess = 4434;
			constexpr uint32 StoragePushConstant16 = 4435;
			constexpr uint32 StorageInputOutput16 = 4436;
			constexpr uint32 VulkanMemoryModel = 5345;
			constexpr uint32 VulkanMemoryModelDeviceScope = 5346;
		}

		// GroupNonUniform, Vote, Arithmetic, Ballot, Shuffle, ShuffleRelative, Clustered, Quad
		constexpr std::array<VkSubgroupFeatureFlagBits, 8> SUBGROUP_OPERATIONS{
			VK_SUBGROUP_FEATURE_BASIC_BIT, VK_SUBGROUP_FEATURE_VOTE_BIT, VK_SUBGROUP_FEATURE_ARITHMETIC_BIT,
			VK_SUBGROUP_FEATURE_BALLOT_BIT, VK_SUBGROUP_FEATURE_SHUFFLE_BIT, VK_SUBGROUP_FEATURE_SHUFFLE_RELATIVE_BIT,
			VK_SUBGROUP_FEATURE_CLUSTERED_BIT, VK_SUBGROUP_FEATURE_QUAD_BIT,
		};

		// formats of the images the passes read and write
		constexpr std::array<VkFormat, 3> PASS_FORMATS{VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_R8_UNORM, VK_FORMAT_R16G16B16A16_SFLOAT};

		uint32 MajorMinor(uint32 apiVersion)
		{
			return VK_MAKE_API_VERSION(0, VK_API_VERSION_MAJOR(apiVersion), VK_API_VERSION_MINOR(apiVersion), 0);
		}
	}

	void DeviceSetup::SetUnusable(std::string reason)
	{
		m_usable = false;
		m_unusableReason = std::move(reason);
		m_modules.clear();
		m_extensions.clear();
		cemuLog_log(LogType::Force, "Frame generation unavailable: {}", m_unusableReason);
	}

	void DeviceSetup::Prepare(VkPhysicalDevice physicalDevice, uint32 instanceApiVersion)
	{
		const LosslessStatus status = LoadShaderModules(m_modules);
		if (status == LosslessStatus::NotInstalled)
			return;
		if (status != LosslessStatus::Ok)
			return SetUnusable(fmt::format("Lossless.dll can't be read (status {})", (sint32)status));
		const ShaderRequirements requirements = GetShaderRequirements(m_modules);

		VkPhysicalDeviceProperties properties{};
		vkGetPhysicalDeviceProperties(physicalDevice, &properties);
		const uint32 apiVersion = std::min(MajorMinor(instanceApiVersion), MajorMinor(properties.apiVersion));

		uint32 extensionCount = 0;
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
		std::vector<VkExtensionProperties> availableExtensions(extensionCount);
		vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, availableExtensions.data());
		auto hasExtension = [&](const char* name) {
			return std::any_of(availableExtensions.cbegin(), availableExtensions.cend(), [name](const VkExtensionProperties& e) { return strcmp(e.extensionName, name) == 0; });
		};
		auto addExtension = [&](const char* name) {
			if (std::none_of(m_extensions.cbegin(), m_extensions.cend(), [name](const char* e) { return strcmp(e, name) == 0; }))
				m_extensions.emplace_back(name);
		};
		// before the API version it was promoted to, a feature needs its extension
		auto useFeatureSource = [&](uint32 coreVersion, const char* extension) {
			if (apiVersion >= coreVersion)
				return true;
			if (!hasExtension(extension))
				return false;
			addExtension(extension);
			return true;
		};

		const bool hasFloat16Int8 = apiVersion >= VK_API_VERSION_1_2 || hasExtension(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
		const bool hasMemoryModel = apiVersion >= VK_API_VERSION_1_2 || hasExtension(VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME);
		const char* robustness2Extension = hasExtension(ROBUSTNESS2_EXT) ? ROBUSTNESS2_EXT : (hasExtension(ROBUSTNESS2_KHR) ? ROBUSTNESS2_KHR : nullptr);

		VkPhysicalDevice16BitStorageFeatures supportedStorage16{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
		VkPhysicalDeviceShaderFloat16Int8Features supportedFloat16Int8{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES};
		VkPhysicalDeviceVulkanMemoryModelFeatures supportedMemoryModel{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_MEMORY_MODEL_FEATURES};
		VkPhysicalDeviceRobustness2FeaturesEXT supportedRobustness2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT};
		VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
		supported.pNext = &supportedStorage16;
		void** chainEnd = &supportedStorage16.pNext;
		if (hasFloat16Int8)
		{
			*chainEnd = &supportedFloat16Int8;
			chainEnd = &supportedFloat16Int8.pNext;
		}
		if (hasMemoryModel)
		{
			*chainEnd = &supportedMemoryModel;
			chainEnd = &supportedMemoryModel.pNext;
		}
		if (robustness2Extension)
			*chainEnd = &supportedRobustness2;
		vkGetPhysicalDeviceFeatures2(physicalDevice, &supported);

		VkPhysicalDeviceSubgroupProperties subgroupProperties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
		VkPhysicalDeviceProperties2 properties2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
		properties2.pNext = &subgroupProperties;
		vkGetPhysicalDeviceProperties2(physicalDevice, &properties2);

		// SPIR-V 1.3 with Vulkan 1.1, 1.4 with VK_KHR_spirv_1_4, 1.5 with Vulkan 1.2 and 1.6 with Vulkan 1.3
		const uint32 maxSpirvVersion = apiVersion >= VK_API_VERSION_1_3 ? 0x10600 : (apiVersion >= VK_API_VERSION_1_2 ? 0x10500 : 0x10300);
		if (requirements.spirvVersion > maxSpirvVersion)
		{
			if (requirements.spirvVersion > 0x10400 || !hasExtension(VK_KHR_SPIRV_1_4_EXTENSION_NAME) || !hasExtension(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME))
				return SetUnusable(fmt::format("the shaders need SPIR-V {}.{}", (requirements.spirvVersion >> 16) & 0xFF, (requirements.spirvVersion >> 8) & 0xFF));
			addExtension(VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
			addExtension(VK_KHR_SPIRV_1_4_EXTENSION_NAME);
		}

		auto requireCore = [&](VkBool32 VkPhysicalDeviceFeatures::*feature, const char* name) {
			if (!(supported.features.*feature))
			{
				SetUnusable(fmt::format("{} is not supported", name));
				return false;
			}
			m_coreFeatures.*feature = VK_TRUE;
			return true;
		};
		auto require = [&]<typename T>(const T& supportedStruct, T& enabledStruct, VkBool32 T::*feature, const char* name) {
			if (!(supportedStruct.*feature))
			{
				SetUnusable(fmt::format("{} is not supported", name));
				return false;
			}
			enabledStruct.*feature = VK_TRUE;
			return true;
		};

		for (const uint32 capability : requirements.capabilities)
		{
			using namespace SpirvCapability;
			bool ok = true;
			switch (capability)
			{
			case Matrix:
			case Shader:
			case Sampled1D:
			case Image1D:
			case SampledBuffer:
			case ImageBuffer:
			case StorageImageExtendedFormats:
			case ImageQuery:
			case DerivativeControl:
				break; // always available
			case Float16:
				ok = require(supportedFloat16Int8, m_float16Int8, &VkPhysicalDeviceShaderFloat16Int8Features::shaderFloat16, "shaderFloat16") &&
					useFeatureSource(VK_API_VERSION_1_2, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
				m_enableFloat16Int8 = true;
				break;
			case Int8:
				ok = require(supportedFloat16Int8, m_float16Int8, &VkPhysicalDeviceShaderFloat16Int8Features::shaderInt8, "shaderInt8") &&
					useFeatureSource(VK_API_VERSION_1_2, VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
				m_enableFloat16Int8 = true;
				break;
			case Int16:
				ok = requireCore(&VkPhysicalDeviceFeatures::shaderInt16, "shaderInt16");
				break;
			case Float64:
				ok = requireCore(&VkPhysicalDeviceFeatures::shaderFloat64, "shaderFloat64");
				break;
			case Int64:
				ok = requireCore(&VkPhysicalDeviceFeatures::shaderInt64, "shaderInt64");
				break;
			case ImageGatherExtended:
				ok = requireCore(&VkPhysicalDeviceFeatures::shaderImageGatherExtended, "shaderImageGatherExtended");
				break;
			case StorageImageMultisample:
				ok = requireCore(&VkPhysicalDeviceFeatures::shaderStorageImageMultisample, "shaderStorageImageMultisample");
				break;
			case UniformBufferArrayDynamicIndexing:
				ok = requireCore(&VkPhysicalDeviceFeatures::shaderUniformBufferArrayDynamicIndexing, "shaderUniformBufferArrayDynamicIndexing");
				break;
			case SampledImageArrayDynamicIndexing:
				ok = requireCore(&VkPhysicalDeviceFeatures::shaderSampledImageArrayDynamicIndexing, "shaderSampledImageArrayDynamicIndexing");
				break;
			case StorageBufferArrayDynamicIndexing:
				ok = requireCore(&VkPhysicalDeviceFeatures::shaderStorageBufferArrayDynamicIndexing, "shaderStorageBufferArrayDynamicIndexing");
				break;
			case StorageImageArrayDynamicIndexing:
				ok = requireCore(&VkPhysicalDeviceFeatures::shaderStorageImageArrayDynamicIndexing, "shaderStorageImageArrayDynamicIndexing");
				break;
			case ImageCubeArray:
			case SampledCubeArray:
				ok = requireCore(&VkPhysicalDeviceFeatures::imageCubeArray, "imageCubeArray");
				break;
			case MinLod:
				ok = requireCore(&VkPhysicalDeviceFeatures::shaderResourceMinLod, "shaderResourceMinLod");
				break;
			case StorageImageReadWithoutFormat:
				ok = requireCore(&VkPhysicalDeviceFeatures::shaderStorageImageReadWithoutFormat, "shaderStorageImageReadWithoutFormat");
				break;
			case StorageImageWriteWithoutFormat:
				ok = requireCore(&VkPhysicalDeviceFeatures::shaderStorageImageWriteWithoutFormat, "shaderStorageImageWriteWithoutFormat");
				break;
			case StorageBuffer16BitAccess:
				ok = require(supportedStorage16, m_storage16, &VkPhysicalDevice16BitStorageFeatures::storageBuffer16BitAccess, "storageBuffer16BitAccess");
				m_enableStorage16 = true;
				break;
			case UniformAndStorageBuffer16BitAccess:
				ok = require(supportedStorage16, m_storage16, &VkPhysicalDevice16BitStorageFeatures::uniformAndStorageBuffer16BitAccess, "uniformAndStorageBuffer16BitAccess");
				m_enableStorage16 = true;
				break;
			case StoragePushConstant16:
				ok = require(supportedStorage16, m_storage16, &VkPhysicalDevice16BitStorageFeatures::storagePushConstant16, "storagePushConstant16");
				m_enableStorage16 = true;
				break;
			case StorageInputOutput16:
				ok = require(supportedStorage16, m_storage16, &VkPhysicalDevice16BitStorageFeatures::storageInputOutput16, "storageInputOutput16");
				m_enableStorage16 = true;
				break;
			case VulkanMemoryModel:
				ok = require(supportedMemoryModel, m_memoryModel, &VkPhysicalDeviceVulkanMemoryModelFeatures::vulkanMemoryModel, "vulkanMemoryModel") &&
					useFeatureSource(VK_API_VERSION_1_2, VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME);
				m_enableMemoryModel = true;
				break;
			case VulkanMemoryModelDeviceScope:
				ok = require(supportedMemoryModel, m_memoryModel, &VkPhysicalDeviceVulkanMemoryModelFeatures::vulkanMemoryModelDeviceScope, "vulkanMemoryModelDeviceScope") &&
					useFeatureSource(VK_API_VERSION_1_2, VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME);
				m_enableMemoryModel = true;
				break;
			default:
				if (capability >= GroupNonUniform && capability <= GroupNonUniformQuad)
				{
					const VkSubgroupFeatureFlagBits operation = SUBGROUP_OPERATIONS[capability - GroupNonUniform];
					if (!(subgroupProperties.supportedStages & VK_SHADER_STAGE_COMPUTE_BIT) || !(subgroupProperties.supportedOperations & operation))
					{
						SetUnusable(fmt::format("subgroup operation {:#x} is not supported in compute shaders", (uint32)operation));
						ok = false;
					}
				}
				else
					cemuLog_log(LogType::Force, "Frame generation: unchecked SPIR-V capability {}", capability);
				break;
			}
			if (!ok)
			{
				if (m_unusableReason.empty())
					SetUnusable(fmt::format("the extension for SPIR-V capability {} is not supported", capability));
				return;
			}
		}

		for (const std::string& extension : requirements.extensions)
		{
			bool ok = true;
			if (extension == "GLSL.std.450" || extension == "SPV_KHR_16bit_storage" || extension == "SPV_KHR_storage_buffer_storage_class" || extension == "SPV_KHR_variable_pointers")
				continue; // Vulkan 1.1
			else if (extension == "SPV_KHR_vulkan_memory_model")
				ok = useFeatureSource(VK_API_VERSION_1_2, VK_KHR_VULKAN_MEMORY_MODEL_EXTENSION_NAME);
			else if (extension == "SPV_KHR_float_controls")
				ok = useFeatureSource(VK_API_VERSION_1_2, VK_KHR_SHADER_FLOAT_CONTROLS_EXTENSION_NAME);
			else if (extension == "SPV_KHR_8bit_storage")
				ok = useFeatureSource(VK_API_VERSION_1_2, VK_KHR_8BIT_STORAGE_EXTENSION_NAME);
			else if (extension == "SPV_KHR_non_semantic_info" || extension.starts_with("NonSemantic."))
				ok = useFeatureSource(VK_API_VERSION_1_3, VK_KHR_SHADER_NON_SEMANTIC_INFO_EXTENSION_NAME);
			else
				cemuLog_log(LogType::Force, "Frame generation: unchecked SPIR-V extension {}", extension);
			if (!ok)
				return SetUnusable(fmt::format("the shaders use {}, which is not supported", extension));
		}

		// some passes leave descriptors empty
		if (!robustness2Extension || !supportedRobustness2.nullDescriptor)
			return SetUnusable("nullDescriptor (VK_EXT_robustness2) is not supported");
		addExtension(robustness2Extension);
		m_robustness2.nullDescriptor = VK_TRUE;

		for (VkFormat format : PASS_FORMATS)
		{
			VkFormatProperties formatProperties{};
			vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);
			constexpr VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
			if ((formatProperties.optimalTilingFeatures & needed) != needed)
				return SetUnusable(fmt::format("format {} can't be used as a storage image", (sint32)format));
		}

		// Android implements it in its swapchain: SurfaceFlinger holds a frame until its desired present time
		m_displayTiming = hasExtension(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);
		if (m_displayTiming)
			addExtension(VK_GOOGLE_DISPLAY_TIMING_EXTENSION_NAME);

		m_usable = true;
		cemuLog_log(LogType::Force, "Frame generation: Lossless.dll loaded, {} shaders, SPIR-V {}.{}, display timing {}", m_modules.size(), (requirements.spirvVersion >> 16) & 0xFF,
			(requirements.spirvVersion >> 8) & 0xFF, m_displayTiming ? "supported" : "not supported");
	}

	void* DeviceSetup::ChainFeatures(void* next)
	{
		if (!m_usable)
			return next;
		m_robustness2.pNext = next;
		next = &m_robustness2;
		if (m_enableStorage16)
		{
			m_storage16.pNext = next;
			next = &m_storage16;
		}
		if (m_enableFloat16Int8)
		{
			m_float16Int8.pNext = next;
			next = &m_float16Int8;
		}
		if (m_enableMemoryModel)
		{
			m_memoryModel.pNext = next;
			next = &m_memoryModel;
		}
		return next;
	}

	void DeviceSetup::EnableCoreFeatures(VkPhysicalDeviceFeatures& features) const
	{
		if (!m_usable)
			return;
		// VkPhysicalDeviceFeatures only has VkBool32 members
		static_assert(sizeof(VkPhysicalDeviceFeatures) % sizeof(VkBool32) == 0);
		VkBool32* enabled = reinterpret_cast<VkBool32*>(&features);
		const VkBool32* wanted = reinterpret_cast<const VkBool32*>(&m_coreFeatures);
		for (size_t i = 0; i < sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32); i++)
		{
			if (wanted[i])
				enabled[i] = VK_TRUE;
		}
	}

	void DeviceSetup::AddExtensions(std::vector<const char*>& extensions) const
	{
		if (!m_usable)
			return;
		for (const char* name : m_extensions)
		{
			if (std::none_of(extensions.cbegin(), extensions.cend(), [name](const char* e) { return strcmp(e, name) == 0; }))
				extensions.emplace_back(name);
		}
	}
}
