// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 lsfg-vk
// SPDX-License-Identifier: GPL-3.0-or-later

// Ported from the Eden emulator (src/video_core/renderer_vulkan/present/lsfg_common.cpp, lsfg_shaders.cpp and the
// helpers of present/util.cpp). Vulkan errors throw std::runtime_error, FrameGenerator turns frame generation off then.

#include "Cafe/HW/Latte/Renderer/Vulkan/FrameGen/LsfgCommon.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/VKRMemoryManager.h"

#include <cstring>

namespace FrameGen
{
	namespace
	{
		constexpr uint32 DESCRIPTORS_PER_TYPE = 4096;

		struct LsfgConstants
		{
			std::array<uint32, 2> inputOffset;
			uint32 firstIter;
			uint32 firstIterS;
			uint32 advancedColorKind;
			uint32 hdrSupport;
			float resolutionInvScale;
			float timestamp;
			float uiThreshold;
			std::array<uint32, 3> padding;
		};
		static_assert(sizeof(LsfgConstants) == 48);

		void Check(VkResult result, const char* what)
		{
			if (result != VK_SUCCESS)
				throw std::runtime_error(fmt::format("{} failed: {}", what, (sint32)result));
		}

		VkImageMemoryBarrier MakeBarrier(const LsfgImage& image, VkAccessFlags srcAccess, VkAccessFlags dstAccess)
		{
			VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			barrier.srcAccessMask = srcAccess;
			barrier.dstAccessMask = dstAccess;
			barrier.oldLayout = image.Layout();
			barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image.Handle();
			barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			return barrier;
		}

		VkSampler CreateLsfgSampler(VkDevice device, VkSamplerAddressMode addressMode, VkCompareOp compareOp, bool whiteBorder)
		{
			VkSamplerCreateInfo info{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
			info.magFilter = VK_FILTER_LINEAR;
			info.minFilter = VK_FILTER_LINEAR;
			info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
			info.addressModeU = addressMode;
			info.addressModeV = addressMode;
			info.addressModeW = addressMode;
			info.mipLodBias = 0.0f;
			info.anisotropyEnable = VK_FALSE;
			info.maxAnisotropy = 0.0f;
			info.compareEnable = VK_FALSE;
			info.compareOp = compareOp;
			info.minLod = 0.0f;
			info.maxLod = VK_LOD_CLAMP_NONE;
			info.borderColor = whiteBorder ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
			info.unnormalizedCoordinates = VK_FALSE;
			VkSampler sampler = VK_NULL_HANDLE;
			Check(vkCreateSampler(device, &info, nullptr, &sampler), "vkCreateSampler");
			return sampler;
		}
	}

	LsfgShaders::LsfgShaders(VkDevice device, const ShaderModules& code) : m_device(device)
	{
		for (const auto& [id, words] : code)
		{
			VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			info.codeSize = words.size() * sizeof(uint32);
			info.pCode = words.data();
			VkShaderModule module = VK_NULL_HANDLE;
			if (vkCreateShaderModule(device, &info, nullptr, &module) != VK_SUCCESS)
			{
				cemuLog_log(LogType::Force, "Frame generation: can't create shader module {}", id);
				return;
			}
			m_modules.emplace(id, module);
		}
		m_valid = !m_modules.empty();
	}

	LsfgShaders::~LsfgShaders()
	{
		for (auto& [id, module] : m_modules)
			vkDestroyShaderModule(m_device, module, nullptr);
	}

	VkShaderModule LsfgShaders::Get(uint32 shaderId) const
	{
		const auto hit = m_modules.find(shaderId);
		return hit == m_modules.end() ? VK_NULL_HANDLE : hit->second;
	}

	LsfgImage::LsfgImage(const LsfgContext& context, VkExtent2D extent, VkFormat format)
		: m_context(context), m_extent{std::max(1u, extent.width), std::max(1u, extent.height)}, m_format(format)
	{
		VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.format = m_format;
		imageInfo.extent = {m_extent.width, m_extent.height, 1};
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		Check(vkCreateImage(m_context.device, &imageInfo, nullptr, &m_image), "vkCreateImage");
		try
		{
			m_allocation = m_context.memoryManager->imageMemoryAllocate(m_image);
		}
		catch (...)
		{
			Release();
			throw;
		}

		VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		viewInfo.image = m_image;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = m_format;
		viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		const VkResult result = vkCreateImageView(m_context.device, &viewInfo, nullptr, &m_view);
		if (result != VK_SUCCESS)
		{
			Release();
			Check(result, "vkCreateImageView");
		}
	}

	LsfgImage::~LsfgImage()
	{
		Release();
	}

	LsfgImage::LsfgImage(LsfgImage&& other) noexcept
	{
		*this = std::move(other);
	}

	LsfgImage& LsfgImage::operator=(LsfgImage&& other) noexcept
	{
		if (this != &other)
		{
			Release();
			m_context = other.m_context;
			m_image = std::exchange(other.m_image, VK_NULL_HANDLE);
			m_view = std::exchange(other.m_view, VK_NULL_HANDLE);
			m_allocation = std::exchange(other.m_allocation, nullptr);
			m_extent = other.m_extent;
			m_format = other.m_format;
			m_layout = other.m_layout;
		}
		return *this;
	}

	void LsfgImage::Release()
	{
		if (m_view != VK_NULL_HANDLE)
			vkDestroyImageView(m_context.device, m_view, nullptr);
		if (m_image != VK_NULL_HANDLE)
			vkDestroyImage(m_context.device, m_image, nullptr);
		if (m_allocation)
			m_context.memoryManager->imageMemoryFree(m_allocation);
		m_view = VK_NULL_HANDLE;
		m_image = VK_NULL_HANDLE;
		m_allocation = nullptr;
	}

	LsfgResources::~LsfgResources()
	{
		for (auto& [key, sampler] : m_samplers)
			vkDestroySampler(m_context.device, sampler, nullptr);
		for (auto& [key, buffer] : m_buffers)
			m_context.memoryManager->DeleteBuffer(buffer.buffer, buffer.memory);
	}

	VkDeviceSize LsfgResources::BufferSize()
	{
		return sizeof(LsfgConstants);
	}

	VkSampler LsfgResources::GetSampler(VkSamplerAddressMode addressMode, VkCompareOp compareOp, bool whiteBorder)
	{
		const uint64 key = static_cast<uint64>(addressMode) | (static_cast<uint64>(compareOp) << 8) | (static_cast<uint64>(whiteBorder) << 16);
		const auto it = m_samplers.find(key);
		if (it != m_samplers.end())
			return it->second;
		const VkSampler sampler = CreateLsfgSampler(m_context.device, addressMode, compareOp, whiteBorder);
		m_samplers.emplace(key, sampler);
		return sampler;
	}

	VkBuffer LsfgResources::GetBuffer(float timestamp, bool firstIter, bool firstIterS)
	{
		uint32 timestampBits{};
		std::memcpy(&timestampBits, &timestamp, sizeof(timestampBits));
		const uint64 key = static_cast<uint64>(timestampBits) | (static_cast<uint64>(firstIter) << 32) | (static_cast<uint64>(firstIterS) << 33);
		const auto it = m_buffers.find(key);
		if (it != m_buffers.end())
			return it->second.buffer;

		Buffer buffer{};
		if (!m_context.memoryManager->CreateBuffer(sizeof(LsfgConstants), VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, buffer.buffer, buffer.memory))
			throw std::runtime_error("can't create a constant buffer");

		LsfgConstants constants{};
		constants.inputOffset = {0, 0};
		constants.firstIter = firstIter ? 1u : 0u;
		constants.firstIterS = firstIterS ? 1u : 0u;
		constants.advancedColorKind = 0;
		constants.hdrSupport = 0;
		constants.resolutionInvScale = 1.0f / m_flowScale;
		constants.timestamp = timestamp;
		constants.uiThreshold = 0.5f;
		void* mapped = nullptr;
		if (vkMapMemory(m_context.device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &mapped) != VK_SUCCESS)
		{
			m_context.memoryManager->DeleteBuffer(buffer.buffer, buffer.memory);
			throw std::runtime_error("can't map a constant buffer");
		}
		std::memcpy(mapped, &constants, sizeof(constants));
		vkUnmapMemory(m_context.device, buffer.memory);

		m_buffers.emplace(key, buffer);
		return buffer.buffer;
	}

	LsfgBarriers& LsfgBarriers::Push(LsfgImage& image, VkAccessFlags srcAccess, VkAccessFlags dstAccess)
	{
		m_barriers.push_back(MakeBarrier(image, srcAccess, dstAccess));
		image.SetLayout(VK_IMAGE_LAYOUT_GENERAL);
		return *this;
	}

	LsfgBarriers& LsfgBarriers::WriteToRead(LsfgImage& image)
	{
		return Push(image, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
	}

	LsfgBarriers& LsfgBarriers::ReadToWrite(LsfgImage& image)
	{
		return Push(image, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT);
	}

	LsfgBarriers& LsfgBarriers::WriteToRead(LsfgImage* image)
	{
		return image == nullptr ? *this : WriteToRead(*image);
	}

	LsfgBarriers& LsfgBarriers::ReadToWrite(LsfgImage* image)
	{
		return image == nullptr ? *this : ReadToWrite(*image);
	}

	void LsfgBarriers::Build()
	{
		if (m_barriers.empty())
			return;
		vkCmdPipelineBarrier(m_cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, (uint32)m_barriers.size(), m_barriers.data());
		m_barriers.clear();
	}

	LsfgDescriptorWriter& LsfgDescriptorWriter::PushImage(VkDescriptorType type, VkSampler sampler, VkImageView view)
	{
		VkDescriptorImageInfo& info = m_imageInfos.emplace_back();
		info.sampler = sampler;
		info.imageView = view;
		info.imageLayout = view == VK_NULL_HANDLE ? VK_IMAGE_LAYOUT_UNDEFINED : VK_IMAGE_LAYOUT_GENERAL;

		VkWriteDescriptorSet& write = m_writes.emplace_back();
		write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.dstSet = m_set;
		write.dstBinding = m_binding++;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = type;
		write.pImageInfo = &info;
		return *this;
	}

	LsfgDescriptorWriter& LsfgDescriptorWriter::AddSampler(VkSampler sampler)
	{
		return PushImage(VK_DESCRIPTOR_TYPE_SAMPLER, sampler, VK_NULL_HANDLE);
	}

	LsfgDescriptorWriter& LsfgDescriptorWriter::AddSampledImage(const LsfgImage& image)
	{
		return PushImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE, image.View());
	}

	LsfgDescriptorWriter& LsfgDescriptorWriter::AddSampledImage(const LsfgImage* image)
	{
		return PushImage(VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_NULL_HANDLE, image == nullptr ? VK_NULL_HANDLE : image->View());
	}

	LsfgDescriptorWriter& LsfgDescriptorWriter::AddStorageImage(const LsfgImage& image)
	{
		return PushImage(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_NULL_HANDLE, image.View());
	}

	LsfgDescriptorWriter& LsfgDescriptorWriter::AddUniformBuffer(VkBuffer buffer, VkDeviceSize size)
	{
		VkDescriptorBufferInfo& info = m_bufferInfos.emplace_back();
		info.buffer = buffer;
		info.offset = 0;
		info.range = size;

		VkWriteDescriptorSet& write = m_writes.emplace_back();
		write = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
		write.dstSet = m_set;
		write.dstBinding = m_binding++;
		write.dstArrayElement = 0;
		write.descriptorCount = 1;
		write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		write.pBufferInfo = &info;
		return *this;
	}

	void LsfgDescriptorWriter::Build(VkDevice device)
	{
		if (m_writes.empty())
			return;
		vkUpdateDescriptorSets(device, (uint32)m_writes.size(), m_writes.data(), 0, nullptr);
		m_writes.clear();
	}

	LsfgPass::LsfgPass(VkDevice device, const LsfgShaders& shaders, uint32 shaderId, LsfgBindings bindings) : m_device(device)
	{
		const VkShaderModule module = shaders.Get(shaderId);
		if (module == VK_NULL_HANDLE)
			throw std::runtime_error(fmt::format("shader {} is missing", shaderId));

		std::vector<VkDescriptorSetLayoutBinding> layoutBindings;
		for (const auto& [count, type] : bindings)
		{
			for (uint32 i = 0; i < count; i++)
			{
				VkDescriptorSetLayoutBinding& binding = layoutBindings.emplace_back();
				binding.binding = (uint32)layoutBindings.size() - 1;
				binding.descriptorType = type;
				binding.descriptorCount = 1;
				binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
				binding.pImmutableSamplers = nullptr;
			}
		}

		VkDescriptorSetLayoutCreateInfo setLayoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		setLayoutInfo.bindingCount = (uint32)layoutBindings.size();
		setLayoutInfo.pBindings = layoutBindings.data();
		Check(vkCreateDescriptorSetLayout(device, &setLayoutInfo, nullptr, &m_descriptorSetLayout), "vkCreateDescriptorSetLayout");

		VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
		pipelineLayoutInfo.setLayoutCount = 1;
		pipelineLayoutInfo.pSetLayouts = &m_descriptorSetLayout;
		VkResult result = vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout);
		if (result != VK_SUCCESS)
		{
			Release();
			Check(result, "vkCreatePipelineLayout");
		}

		VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
		pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		pipelineInfo.stage.module = module;
		pipelineInfo.stage.pName = "main";
		pipelineInfo.layout = m_pipelineLayout;
		result = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline);
		if (result != VK_SUCCESS)
		{
			Release();
			throw std::runtime_error(fmt::format("can't create the pipeline for shader {}: {}", shaderId, (sint32)result));
		}
	}

	LsfgPass::~LsfgPass()
	{
		Release();
	}

	LsfgPass::LsfgPass(LsfgPass&& other) noexcept
	{
		*this = std::move(other);
	}

	LsfgPass& LsfgPass::operator=(LsfgPass&& other) noexcept
	{
		if (this != &other)
		{
			Release();
			m_device = other.m_device;
			m_descriptorSetLayout = std::exchange(other.m_descriptorSetLayout, VK_NULL_HANDLE);
			m_pipelineLayout = std::exchange(other.m_pipelineLayout, VK_NULL_HANDLE);
			m_pipeline = std::exchange(other.m_pipeline, VK_NULL_HANDLE);
		}
		return *this;
	}

	void LsfgPass::Release()
	{
		if (m_pipeline != VK_NULL_HANDLE)
			vkDestroyPipeline(m_device, m_pipeline, nullptr);
		if (m_pipelineLayout != VK_NULL_HANDLE)
			vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
		if (m_descriptorSetLayout != VK_NULL_HANDLE)
			vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
		m_pipeline = VK_NULL_HANDLE;
		m_pipelineLayout = VK_NULL_HANDLE;
		m_descriptorSetLayout = VK_NULL_HANDLE;
	}

	void LsfgPass::Bind(VkCommandBuffer cmd, VkDescriptorSet set) const
	{
		BindPipeline(cmd);
		BindSet(cmd, set);
	}

	void LsfgPass::BindPipeline(VkCommandBuffer cmd) const
	{
		vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
	}

	void LsfgPass::BindSet(VkCommandBuffer cmd, VkDescriptorSet set) const
	{
		vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipelineLayout, 0, 1, &set, 0, nullptr);
	}

	std::vector<VkDescriptorSet> AllocateLsfgDescriptorSets(VkDevice device, VkDescriptorPool pool, const std::vector<VkDescriptorSetLayout>& layouts)
	{
		std::vector<VkDescriptorSet> sets(layouts.size());
		VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
		info.descriptorPool = pool;
		info.descriptorSetCount = (uint32)layouts.size();
		info.pSetLayouts = layouts.data();
		Check(vkAllocateDescriptorSets(device, &info, sets.data()), "vkAllocateDescriptorSets");
		return sets;
	}

	VkDescriptorPool CreateLsfgDescriptorPool(VkDevice device, uint32 maxSets)
	{
		const std::array<VkDescriptorPoolSize, 4> poolSizes{{
			{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, DESCRIPTORS_PER_TYPE},
			{VK_DESCRIPTOR_TYPE_SAMPLER, DESCRIPTORS_PER_TYPE},
			{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, DESCRIPTORS_PER_TYPE},
			{VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, DESCRIPTORS_PER_TYPE},
		}};
		VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
		info.maxSets = maxSets;
		info.poolSizeCount = (uint32)poolSizes.size();
		info.pPoolSizes = poolSizes.data();
		VkDescriptorPool pool = VK_NULL_HANDLE;
		Check(vkCreateDescriptorPool(device, &info, nullptr, &pool), "vkCreateDescriptorPool");
		return pool;
	}
}
