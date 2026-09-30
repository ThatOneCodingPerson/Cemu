// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Based on the Eden emulator's frame_gen.cpp (src/video_core/renderer_vulkan/present/frame_gen.cpp): the warm-up,
// pacing and flow scale logic are Eden's, the copies in and out of the swapchain images are Cemu specific.

#include "Cafe/HW/Latte/Renderer/Vulkan/FrameGen/FrameGenerator.h"

namespace FrameGen
{
	namespace
	{
		// the passes compare the newest frame with the two before it
		constexpr uint64 LSFG_REQUIRED_FRAMES = 2;
		constexpr uint32 LSFG_RECURRENCE_FRAMES = 2;
		constexpr float FLOW_SCALE_STEPS = 20.0f;

		VkImageMemoryBarrier MakeBarrier(VkImage image, VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkImageLayout oldLayout, VkImageLayout newLayout)
		{
			VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
			barrier.srcAccessMask = srcAccess;
			barrier.dstAccessMask = dstAccess;
			barrier.oldLayout = oldLayout;
			barrier.newLayout = newLayout;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.image = image;
			barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
			return barrier;
		}

		void Barrier(VkCommandBuffer cmd, VkPipelineStageFlags srcStages, VkPipelineStageFlags dstStages, std::initializer_list<VkImageMemoryBarrier> barriers)
		{
			vkCmdPipelineBarrier(cmd, srcStages, dstStages, 0, 0, nullptr, 0, nullptr, (uint32)barriers.size(), barriers.begin());
		}

		// between the swapchain format and the passes' RGBA8 a blit converts, for the same format a copy is enough
		void CopyImage(VkCommandBuffer cmd, VkImage src, VkImageLayout srcLayout, VkImage dst, VkImageLayout dstLayout, VkExtent2D extent, bool blit)
		{
			const VkImageSubresourceLayers layers{VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
			if (blit)
			{
				VkImageBlit region{};
				region.srcSubresource = layers;
				region.srcOffsets[1] = {(sint32)extent.width, (sint32)extent.height, 1};
				region.dstSubresource = layers;
				region.dstOffsets[1] = {(sint32)extent.width, (sint32)extent.height, 1};
				vkCmdBlitImage(cmd, src, srcLayout, dst, dstLayout, 1, &region, VK_FILTER_NEAREST);
			}
			else
			{
				VkImageCopy region{};
				region.srcSubresource = layers;
				region.dstSubresource = layers;
				region.extent = {extent.width, extent.height, 1};
				vkCmdCopyImage(cmd, src, srcLayout, dst, dstLayout, 1, &region);
			}
		}

		bool CanBlit(VkPhysicalDevice physicalDevice, VkFormat format)
		{
			VkFormatProperties properties{};
			vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &properties);
			constexpr VkFormatFeatureFlags needed = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
			return (properties.optimalTilingFeatures & needed) == needed;
		}

		// the contents of image are replaced: wait for earlier reads, drop the old contents
		void BeginOverwrite(VkCommandBuffer cmd, VkImage image)
		{
			Barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
				{MakeBarrier(image, 0, VK_ACCESS_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)});
		}

		void EndOverwrite(VkCommandBuffer cmd, VkImage image)
		{
			Barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				{MakeBarrier(image, VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)});
		}
	}

	FrameGenerator::FrameGenerator(VkDevice device, VkPhysicalDevice physicalDevice, VKRMemoryManager* memoryManager, const ShaderModules& modules)
		: m_physicalDevice(physicalDevice)
	{
		m_context.device = device;
		m_context.memoryManager = memoryManager;
		m_shaders = std::make_unique<LsfgShaders>(device, modules);
		if (!m_shaders->IsValid())
		{
			Fail("the shaders of Lossless.dll can't be loaded");
			return;
		}
		m_usable = true;
	}

	FrameGenerator::~FrameGenerator() = default;

	void FrameGenerator::Fail(std::string error)
	{
		m_usable = false;
		m_error = std::move(error);
		cemuLog_log(LogType::Force, "Frame generation turned off: {}", m_error);
	}

	float FrameGenerator::ConfiguredFlowScale(const FrameGenSettings& settings) const
	{
		if (settings.flowScalePercent > 0)
			return std::clamp(static_cast<float>(settings.flowScalePercent) / 100.0f, 0.25f, 1.0f);
		if (m_peakSourceToScreen <= 0.0f)
			return 1.0f;
		// the flow doesn't need more detail than the game rendered. Steps keep small changes from rebuilding
		const float stepped = std::ceil(m_peakSourceToScreen * FLOW_SCALE_STEPS) / FLOW_SCALE_STEPS;
		return std::clamp(stepped, 0.25f, 1.0f);
	}

	bool FrameGenerator::Rebuild(VkExtent2D extent, VkFormat format, float flowScale)
	{
		// the old passes may still be in use
		vkDeviceWaitIdle(m_context.device);
		m_chain.reset();

		m_useBlit = format != LSFG_DEFAULT_FORMAT;
		if (m_useBlit && (!CanBlit(m_physicalDevice, format) || !CanBlit(m_physicalDevice, LSFG_DEFAULT_FORMAT)))
		{
			Fail(fmt::format("the screen's format {} can't be converted", (sint32)format));
			return false;
		}
		try
		{
			m_chain = std::make_unique<LsfgChain>(m_context, *m_shaders, extent, flowScale);
		}
		catch (const std::exception& e)
		{
			Fail(e.what());
			return false;
		}
		m_builtExtent = extent;
		m_builtFormat = format;
		m_builtFlowScale = flowScale;
		m_frameCount = 0;
		m_warmStreak = 0;
		m_generationCount = 0;
		cemuLog_log(LogType::Force, "Frame generation: {}x{}, optical flow at {}%", extent.width, extent.height, (sint32)std::lround(flowScale * 100.0f));
		return true;
	}

	void FrameGenerator::ReleaseChain()
	{
		m_chain.reset();
		m_pacer.Reset();
		m_peakSourceToScreen = 0.0f;
		m_warmStreak = 0;
		m_generationCount = 0;
	}

	size_t FrameGenerator::CaptureFrame(VkCommandBuffer cmd, VkImage frameImage, VkFormat format, VkExtent2D extent, const FrameGenSettings& settings, float sourceToScreen)
	{
		m_generationCount = 0;
		if (!m_usable)
			return 0;

		m_peakSourceToScreen = std::max(m_peakSourceToScreen, sourceToScreen);
		const float flowScale = ConfiguredFlowScale(settings);
		if (!m_chain || m_builtExtent.width != extent.width || m_builtExtent.height != extent.height || m_builtFormat != format || m_builtFlowScale != flowScale)
		{
			if (!Rebuild(extent, format, flowScale))
				return 0;
		}

		const sint32 multiplier = std::clamp<sint32>(settings.multiplier, 2, 4);
		const size_t fixedGenerations = (size_t)multiplier - 1;
		const size_t maxGenerations = settings.targetRate > 0 ? LSFG_MAX_GENERATIONS : fixedGenerations;
		const FrameGenPlan plan = m_pacer.Plan(LSFG_MAX_GENERATIONS, fixedGenerations, maxGenerations, (float)settings.targetRate);

		const uint64 count = m_frameCount++;
		m_lastCount = count;
		const bool warm = plan.warm && count + 1 >= LSFG_REQUIRED_FRAMES;
		m_warmStreak = warm ? m_warmStreak + 1 : 0;
		if (warm && m_warmStreak >= LSFG_RECURRENCE_FRAMES)
			m_generationCount = std::min(plan.generations, LSFG_MAX_GENERATIONS);

		LsfgImage& input = m_chain->Input(count);
		Barrier(cmd, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, {
			MakeBarrier(frameImage, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL),
			MakeBarrier(input.Handle(), VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, input.Layout(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL),
		});
		CopyImage(cmd, frameImage, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, input.Handle(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, extent, m_useBlit);
		Barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, {
			MakeBarrier(frameImage, VK_ACCESS_TRANSFER_READ_BIT, 0, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),
			MakeBarrier(input.Handle(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_GENERAL),
		});
		input.SetLayout(VK_IMAGE_LAYOUT_GENERAL);

		if (warm)
			m_chain->DispatchShared(cmd, count);
		return m_generationCount;
	}

	void FrameGenerator::GenerateFrame(VkCommandBuffer cmd, size_t generation, VkImage image)
	{
		cemu_assert_debug(generation < m_generationCount);
		if (!m_chain)
			return;
		m_chain->DispatchGeneration(cmd, m_lastCount, m_generationCount, generation);
		BeginOverwrite(cmd, image);
		CopyImage(cmd, m_chain->Output().Handle(), VK_IMAGE_LAYOUT_GENERAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_builtExtent, m_useBlit);
		EndOverwrite(cmd, image);
	}

	void FrameGenerator::RestoreCapturedFrame(VkCommandBuffer cmd, VkImage image)
	{
		if (!m_chain)
			return;
		LsfgImage& input = m_chain->Input(m_lastCount);
		Barrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
			{MakeBarrier(input.Handle(), VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_GENERAL)});
		BeginOverwrite(cmd, image);
		CopyImage(cmd, input.Handle(), VK_IMAGE_LAYOUT_GENERAL, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, m_builtExtent, m_useBlit);
		EndOverwrite(cmd, image);
	}
}
