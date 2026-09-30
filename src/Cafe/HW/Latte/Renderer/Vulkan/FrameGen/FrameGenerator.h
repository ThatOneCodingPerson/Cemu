// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Frame generation with the shaders of Lossless Scaling (LSFG). Based on the Eden emulator's FrameGen
// (src/video_core/renderer_vulkan/present/frame_gen.h), reworked for Cemu's presentation: Cemu draws the TV picture
// straight into the swapchain image, so the finished frame is copied out of it and the generated frames are copied
// into the next swapchain images.

#pragma once

#include "Cafe/HW/Latte/Renderer/Vulkan/FrameGen/FrameGenPacer.h"
#include "Cafe/HW/Latte/Renderer/Vulkan/FrameGen/LsfgChain.h"

namespace FrameGen
{
	struct FrameGenSettings
	{
		sint32 multiplier = 2; // output frames per rendered frame, 2 to 4
		sint32 targetRate = 0; // frames per second to reach by generating as many frames as needed, 0 for the multiplier
		sint32 flowScalePercent = 0; // resolution of the optical flow, 25 to 100. 0 picks it from the game's resolution
	};

	class FrameGenerator
	{
	public:
		FrameGenerator(VkDevice device, VkPhysicalDevice physicalDevice, VKRMemoryManager* memoryManager, const ShaderModules& modules);
		~FrameGenerator();
		FrameGenerator(const FrameGenerator&) = delete;
		FrameGenerator& operator=(const FrameGenerator&) = delete;

		// false once something failed, frame generation stays off then
		bool IsUsable() const { return m_usable; }
		const std::string& GetError() const { return m_error; }
		// false after ReleaseChain(), until the next CaptureFrame()
		bool HasChain() const { return m_chain != nullptr; }

		// frameImage: the finished frame, an acquired swapchain image in VK_IMAGE_LAYOUT_PRESENT_SRC_KHR.
		// sourceToScreen: the game's rendered width divided by its width on screen, for the automatic flow scale.
		// Copies the frame as the newest input and records the passes shared by the frames generated in front of it.
		// Returns how many frames to generate and present before this one
		size_t CaptureFrame(VkCommandBuffer cmd, VkImage frameImage, VkFormat format, VkExtent2D extent, const FrameGenSettings& settings, float sourceToScreen);
		// a frame that wasn't drawn: the history has a gap
		void SkipFrame() { m_warmStreak = 0; }
		// writes generated frame `generation` into image (acquired, contents undefined), leaves it in PRESENT_SRC_KHR
		void GenerateFrame(VkCommandBuffer cmd, size_t generation, VkImage image);
		// copies the last captured frame into image (acquired, contents undefined), leaves it in PRESENT_SRC_KHR
		void RestoreCapturedFrame(VkCommandBuffer cmd, VkImage image);

		// frees the images and pipelines of the passes. Only when the GPU is idle
		void ReleaseChain();

	private:
		bool Rebuild(VkExtent2D extent, VkFormat format, float flowScale);
		float ConfiguredFlowScale(const FrameGenSettings& settings) const;
		void Fail(std::string error);

		LsfgContext m_context;
		VkPhysicalDevice m_physicalDevice;
		std::unique_ptr<LsfgShaders> m_shaders;
		std::unique_ptr<LsfgChain> m_chain;
		FrameGenPacer m_pacer;

		bool m_usable = false;
		std::string m_error;
		VkExtent2D m_builtExtent{};
		VkFormat m_builtFormat = VK_FORMAT_UNDEFINED;
		float m_builtFlowScale = 0.0f;
		bool m_useBlit = false; // the swapchain format isn't the passes' RGBA8
		float m_peakSourceToScreen = 0.0f;
		uint64 m_frameCount = 0;
		uint64 m_lastCount = 0;
		size_t m_generationCount = 0; // of the last captured frame
		uint32 m_warmStreak = 0;
	};
}
