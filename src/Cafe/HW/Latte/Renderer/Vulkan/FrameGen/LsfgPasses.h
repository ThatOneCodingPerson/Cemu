// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 lsfg-vk
// SPDX-License-Identifier: GPL-3.0-or-later

// Ported from the Eden emulator (src/video_core/renderer_vulkan/present/lsfg_mipmaps.h, lsfg_alpha.h, lsfg_beta.h,
// lsfg_gamma.h, lsfg_delta.h, lsfg_generate.h). LsfgGenerate writes into one image owned by the chain instead of
// the presentation frames.

#pragma once

#include "Cafe/HW/Latte/Renderer/Vulkan/FrameGen/LsfgCommon.h"

namespace FrameGen
{
	constexpr size_t LSFG_MIP_LEVELS = 7;
	constexpr size_t LSFG_ALPHA_STAGES = 4;
	constexpr size_t LSFG_BETA_STAGES = 5;
	constexpr size_t LSFG_BETA_OUTPUTS = 6;
	constexpr size_t LSFG_GAMMA_STAGES = 5;
	constexpr size_t LSFG_GAMMA_TEMPS = 3;
	constexpr size_t LSFG_DELTA_STAGES = 10;
	constexpr size_t LSFG_DELTA_TEMPS = 3;

	// the optical flow input: the luminance of the frame at 7 mip levels
	class LsfgMipmaps
	{
	public:
		LsfgMipmaps() = default;
		LsfgMipmaps(const LsfgContext& context, const LsfgShaders& shaders, LsfgResources& resources, VkDescriptorPool pool, LsfgImagePair& frames, float flowScale);

		void Dispatch(VkCommandBuffer cmd, uint64 frameCount);
		LsfgImage& Output(size_t level) { return m_outImages[level]; }

	private:
		LsfgImagePair* m_frames = nullptr;
		LsfgPass m_pass;
		std::array<VkDescriptorSet, 2> m_descriptorSets{};
		VkExtent2D m_flowExtent{};
		std::array<LsfgImage, LSFG_MIP_LEVELS> m_outImages;
	};

	class LsfgAlphaPasses
	{
	public:
		LsfgAlphaPasses() = default;
		LsfgAlphaPasses(VkDevice device, const LsfgShaders& shaders);

		const LsfgPass& Get(size_t stage) const { return m_passes[stage]; }

	private:
		std::array<LsfgPass, LSFG_ALPHA_STAGES> m_passes;
	};

	// features of one mip level, kept for the last three frames
	class LsfgAlpha
	{
	public:
		LsfgAlpha() = default;
		LsfgAlpha(const LsfgContext& context, const LsfgAlphaPasses& passes, LsfgResources& resources, VkDescriptorPool pool, LsfgImage& input);

		void PushBarriers(LsfgBarriers& barriers, uint64 frameCount, size_t stage);
		void DispatchStage(VkCommandBuffer cmd, uint64 frameCount, size_t stage);
		LsfgImageHistory& Outputs() { return m_outImages; }

	private:
		const LsfgAlphaPasses* m_passes = nullptr;
		LsfgImage* m_input = nullptr;
		std::array<VkDescriptorSet, LSFG_ALPHA_STAGES - 1> m_descriptorSets{};
		std::array<VkDescriptorSet, LSFG_HISTORY_SLOTS> m_lastDescriptorSets{};
		LsfgImage m_temp1;
		LsfgImage m_temp2;
		LsfgImagePair m_temp3;
		LsfgImageHistory m_outImages;
	};

	class LsfgBeta
	{
	public:
		LsfgBeta() = default;
		LsfgBeta(const LsfgContext& context, const LsfgShaders& shaders, LsfgResources& resources, VkDescriptorPool pool, LsfgImageHistory& inputs);

		void Dispatch(VkCommandBuffer cmd, uint64 frameCount);
		LsfgImage& Output(size_t level) { return m_outImages[level]; }

	private:
		LsfgImageHistory* m_inputs = nullptr;
		std::array<LsfgPass, LSFG_BETA_STAGES> m_passes;
		std::array<VkDescriptorSet, LSFG_HISTORY_SLOTS> m_firstDescriptorSets{};
		std::array<VkDescriptorSet, LSFG_BETA_STAGES - 1> m_descriptorSets{};
		LsfgImagePair m_temp1;
		LsfgImagePair m_temp2;
		std::array<LsfgImage, LSFG_BETA_OUTPUTS> m_outImages;
	};

	// the motion of one mip level, refined from the coarser level
	class LsfgGamma
	{
	public:
		LsfgGamma() = default;
		LsfgGamma(const LsfgContext& context, const LsfgShaders& shaders, LsfgResources& resources, VkDescriptorPool pool, LsfgImageHistory& inputs, LsfgImage& flowInput, LsfgImage* previous);

		void Dispatch(VkCommandBuffer cmd, uint64 frameCount, size_t slot);
		LsfgImage& Output() { return m_outImage; }

	private:
		struct Generation
		{
			std::array<VkDescriptorSet, LSFG_HISTORY_SLOTS> firstDescriptorSets{};
			std::array<VkDescriptorSet, LSFG_GAMMA_STAGES - 1> descriptorSets{};
		};

		LsfgImageHistory* m_inputs = nullptr;
		LsfgImage* m_flowInput = nullptr;
		LsfgImage* m_previous = nullptr;
		std::array<LsfgPass, LSFG_GAMMA_STAGES> m_passes;
		std::array<Generation, LSFG_GENERATION_SLOTS> m_generations{};
		std::array<LsfgImage, LSFG_GAMMA_TEMPS> m_temp1;
		LsfgImagePair m_temp2;
		LsfgImage m_outImage;
	};

	class LsfgDelta
	{
	public:
		LsfgDelta() = default;
		LsfgDelta(const LsfgContext& context, const LsfgShaders& shaders, LsfgResources& resources, VkDescriptorPool pool, LsfgImageHistory& inputs, LsfgImage& flowInput,
			LsfgImage* previousGamma, LsfgImage* previous1, LsfgImage* previous2);

		void Dispatch(VkCommandBuffer cmd, uint64 frameCount, size_t slot);
		LsfgImage& Output1() { return m_outImage1; }
		LsfgImage& Output2() { return m_outImage2; }

	private:
		struct Generation
		{
			std::array<VkDescriptorSet, LSFG_HISTORY_SLOTS> firstDescriptorSets{};
			std::array<VkDescriptorSet, LSFG_HISTORY_SLOTS> sixthDescriptorSets{};
			std::array<VkDescriptorSet, LSFG_DELTA_STAGES - 2> descriptorSets{};
		};

		LsfgImageHistory* m_inputs = nullptr;
		LsfgImage* m_flowInput = nullptr;
		LsfgImage* m_previousGamma = nullptr;
		LsfgImage* m_previous1 = nullptr;
		LsfgImage* m_previous2 = nullptr;
		std::array<LsfgPass, LSFG_DELTA_STAGES> m_passes;
		std::array<Generation, LSFG_GENERATION_SLOTS> m_generations{};
		std::array<LsfgImage, LSFG_DELTA_TEMPS> m_temp1;
		LsfgImagePair m_temp2;
		LsfgImage m_outImage1;
		LsfgImage m_outImage2;
	};

	// warps the two frames along the motion into `output`
	class LsfgGenerate
	{
	public:
		LsfgGenerate() = default;
		LsfgGenerate(const LsfgContext& context, const LsfgShaders& shaders, LsfgResources& resources, VkDescriptorPool pool, LsfgImagePair& frames, LsfgImage& motion,
			LsfgImage& detail1, LsfgImage& detail2, LsfgImage& output);

		// leaves `output` ready to be copied (VK_IMAGE_LAYOUT_GENERAL)
		void Dispatch(VkCommandBuffer cmd, uint64 frameCount, size_t slot);

	private:
		struct Generation
		{
			std::array<VkDescriptorSet, 2> descriptorSets{}; // by frame parity
		};

		LsfgImagePair* m_frames = nullptr;
		LsfgImage* m_motion = nullptr;
		LsfgImage* m_detail1 = nullptr;
		LsfgImage* m_detail2 = nullptr;
		LsfgImage* m_output = nullptr;
		LsfgPass m_pass;
		std::array<Generation, LSFG_GENERATION_SLOTS> m_generations{};
	};
}
