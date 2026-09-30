// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 lsfg-vk
// SPDX-License-Identifier: GPL-3.0-or-later

// Ported from the Eden emulator (src/video_core/renderer_vulkan/present/lsfg_common.h, lsfg_shaders.h). Eden's
// Vulkan wrappers are replaced with plain handles owned by these classes, memory comes from Cemu's VKRMemoryManager.

#pragma once

#include "Cafe/HW/Latte/Renderer/Vulkan/VulkanAPI.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/FrameGen/LosslessDll.h"

#include <deque>
#include <initializer_list>
#include <utility>

class VKRMemoryManager;
struct VkImageMemAllocation;

namespace FrameGen
{
	constexpr VkFormat LSFG_DEFAULT_FORMAT = VK_FORMAT_R8G8B8A8_UNORM;
	constexpr VkFormat LSFG_FLOW_FORMAT = VK_FORMAT_R8_UNORM;
	constexpr VkFormat LSFG_MOTION_FORMAT = VK_FORMAT_R16G16B16A16_SFLOAT;

	constexpr size_t LSFG_HISTORY_SLOTS = 3;
	constexpr size_t LSFG_MAX_GENERATIONS = 3;

	// one slot per (generation count, generation) pair
	constexpr size_t LSFG_GENERATION_SLOTS = LSFG_MAX_GENERATIONS * (LSFG_MAX_GENERATIONS + 1) / 2;

	constexpr size_t LsfgGenerationSlot(size_t generationCount, size_t generation)
	{
		return (generationCount - 1) * generationCount / 2 + generation;
	}

	constexpr float LsfgTimestamp(size_t generation, size_t generationCount)
	{
		return static_cast<float>(generation + 1) / static_cast<float>(generationCount + 1);
	}

	constexpr size_t LsfgSlotCount(size_t slot)
	{
		size_t count = 1;
		while (LsfgGenerationSlot(count + 1, 0) <= slot)
			++count;
		return count;
	}

	constexpr float LsfgSlotTimestamp(size_t slot)
	{
		const size_t count = LsfgSlotCount(slot);
		return LsfgTimestamp(slot - LsfgGenerationSlot(count, 0), count);
	}

	struct LsfgContext
	{
		VkDevice device = VK_NULL_HANDLE;
		VKRMemoryManager* memoryManager = nullptr;
	};

	// the compute shader modules, by Lossless Scaling shader id
	class LsfgShaders
	{
	public:
		LsfgShaders(VkDevice device, const ShaderModules& code);
		~LsfgShaders();
		LsfgShaders(const LsfgShaders&) = delete;
		LsfgShaders& operator=(const LsfgShaders&) = delete;

		bool IsValid() const { return m_valid; }
		VkShaderModule Get(uint32 shaderId) const;

	private:
		VkDevice m_device;
		std::map<uint32, VkShaderModule> m_modules;
		bool m_valid = false;
	};

	class LsfgImage
	{
	public:
		LsfgImage() = default;
		LsfgImage(const LsfgContext& context, VkExtent2D extent, VkFormat format = LSFG_DEFAULT_FORMAT);
		~LsfgImage();
		LsfgImage(LsfgImage&& other) noexcept;
		LsfgImage& operator=(LsfgImage&& other) noexcept;
		LsfgImage(const LsfgImage&) = delete;
		LsfgImage& operator=(const LsfgImage&) = delete;

		VkImage Handle() const { return m_image; }
		VkImageView View() const { return m_view; }
		VkExtent2D Extent() const { return m_extent; }
		VkFormat Format() const { return m_format; }
		VkImageLayout Layout() const { return m_layout; }
		void SetLayout(VkImageLayout layout) { m_layout = layout; }

	private:
		void Release();

		LsfgContext m_context{};
		VkImage m_image = VK_NULL_HANDLE;
		VkImageView m_view = VK_NULL_HANDLE;
		VkImageMemAllocation* m_allocation = nullptr;
		VkExtent2D m_extent{};
		VkFormat m_format = VK_FORMAT_UNDEFINED;
		VkImageLayout m_layout = VK_IMAGE_LAYOUT_UNDEFINED;
	};

	using LsfgImagePair = std::array<LsfgImage, 2>;
	using LsfgImageHistory = std::array<LsfgImagePair, LSFG_HISTORY_SLOTS>;

	// samplers and the constant buffers, shared by all passes
	class LsfgResources
	{
	public:
		LsfgResources(const LsfgContext& context, float flowScale) : m_context(context), m_flowScale(flowScale) {}
		~LsfgResources();
		LsfgResources(const LsfgResources&) = delete;
		LsfgResources& operator=(const LsfgResources&) = delete;

		VkSampler GetSampler(VkSamplerAddressMode addressMode = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VkCompareOp compareOp = VK_COMPARE_OP_NEVER, bool whiteBorder = false);
		VkBuffer GetBuffer(float timestamp = 0.0f, bool firstIter = false, bool firstIterS = false);
		static VkDeviceSize BufferSize();

	private:
		struct Buffer
		{
			VkBuffer buffer;
			VkDeviceMemory memory;
		};

		LsfgContext m_context;
		float m_flowScale;
		std::map<uint64, VkSampler> m_samplers;
		std::map<uint64, Buffer> m_buffers;
	};

	// image barriers between compute passes. All images stay in VK_IMAGE_LAYOUT_GENERAL after their first use
	class LsfgBarriers
	{
	public:
		explicit LsfgBarriers(VkCommandBuffer cmd) : m_cmd(cmd) {}

		LsfgBarriers& WriteToRead(LsfgImage& image);
		LsfgBarriers& ReadToWrite(LsfgImage& image);
		LsfgBarriers& WriteToRead(LsfgImage* image);
		LsfgBarriers& ReadToWrite(LsfgImage* image);

		template<typename Range>
		LsfgBarriers& WriteToReadAll(Range& images)
		{
			for (auto& image : images)
				WriteToRead(image);
			return *this;
		}

		template<typename Range>
		LsfgBarriers& ReadToWriteAll(Range& images)
		{
			for (auto& image : images)
				ReadToWrite(image);
			return *this;
		}

		void Build();

	private:
		LsfgBarriers& Push(LsfgImage& image, VkAccessFlags srcAccess, VkAccessFlags dstAccess);

		VkCommandBuffer m_cmd;
		std::vector<VkImageMemoryBarrier> m_barriers;
	};

	// writes one descriptor per binding, in order
	class LsfgDescriptorWriter
	{
	public:
		explicit LsfgDescriptorWriter(VkDescriptorSet set) : m_set(set) {}

		LsfgDescriptorWriter& AddSampler(VkSampler sampler);
		LsfgDescriptorWriter& AddSampledImage(const LsfgImage& image);
		LsfgDescriptorWriter& AddSampledImage(const LsfgImage* image); // nullptr: a null descriptor
		LsfgDescriptorWriter& AddStorageImage(const LsfgImage& image);
		LsfgDescriptorWriter& AddUniformBuffer(VkBuffer buffer, VkDeviceSize size);

		template<typename Range>
		LsfgDescriptorWriter& AddSampledImages(const Range& images)
		{
			for (const auto& image : images)
				AddSampledImage(image);
			return *this;
		}

		template<typename Range>
		LsfgDescriptorWriter& AddStorageImages(const Range& images)
		{
			for (const auto& image : images)
				AddStorageImage(image);
			return *this;
		}

		void Build(VkDevice device);

	private:
		LsfgDescriptorWriter& PushImage(VkDescriptorType type, VkSampler sampler, VkImageView view);

		VkDescriptorSet m_set;
		uint32 m_binding = 0;
		std::deque<VkDescriptorImageInfo> m_imageInfos;
		std::deque<VkDescriptorBufferInfo> m_bufferInfos;
		std::vector<VkWriteDescriptorSet> m_writes;
	};

	// (descriptor count, type) in binding order
	using LsfgBindings = std::initializer_list<std::pair<uint32, VkDescriptorType>>;

	// a compute pipeline with a single descriptor set
	class LsfgPass
	{
	public:
		LsfgPass() = default;
		LsfgPass(VkDevice device, const LsfgShaders& shaders, uint32 shaderId, LsfgBindings bindings);
		~LsfgPass();
		LsfgPass(LsfgPass&& other) noexcept;
		LsfgPass& operator=(LsfgPass&& other) noexcept;
		LsfgPass(const LsfgPass&) = delete;
		LsfgPass& operator=(const LsfgPass&) = delete;

		VkDescriptorSetLayout SetLayout() const { return m_descriptorSetLayout; }

		void Bind(VkCommandBuffer cmd, VkDescriptorSet set) const;
		void BindPipeline(VkCommandBuffer cmd) const;
		void BindSet(VkCommandBuffer cmd, VkDescriptorSet set) const;

	private:
		void Release();

		VkDevice m_device = VK_NULL_HANDLE;
		VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
		VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
		VkPipeline m_pipeline = VK_NULL_HANDLE;
	};

	VkDescriptorPool CreateLsfgDescriptorPool(VkDevice device, uint32 maxSets);
	// descriptor sets are freed with their pool
	std::vector<VkDescriptorSet> AllocateLsfgDescriptorSets(VkDevice device, VkDescriptorPool pool, const std::vector<VkDescriptorSetLayout>& layouts);
}
