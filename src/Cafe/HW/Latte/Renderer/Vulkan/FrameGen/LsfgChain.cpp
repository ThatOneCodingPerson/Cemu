// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 lsfg-vk
// SPDX-License-Identifier: GPL-3.0-or-later

// Ported from the Eden emulator (src/video_core/renderer_vulkan/present/lsfg_chain.cpp).

#include "Cafe/HW/Latte/Renderer/Vulkan/FrameGen/LsfgChain.h"

namespace FrameGen
{
	namespace
	{
		constexpr uint32 FIXED_DESCRIPTOR_SETS = 64;
		constexpr uint32 DESCRIPTOR_SETS_PER_SLOT = 112;
		constexpr size_t FIRST_DELTA_LEVEL = 4;
	}

	LsfgChain::LsfgChain(const LsfgContext& context, const LsfgShaders& shaders, VkExtent2D extent, float flowScale)
		: m_context(context), m_resources(context, flowScale)
	{
		m_descriptorPool = CreateLsfgDescriptorPool(context.device, FIXED_DESCRIPTOR_SETS + DESCRIPTOR_SETS_PER_SLOT * static_cast<uint32>(LSFG_GENERATION_SLOTS));
		try
		{
			for (auto& image : m_frames)
				image = LsfgImage(context, extent);
			m_output = LsfgImage(context, extent);

			m_mipmaps = LsfgMipmaps(context, shaders, m_resources, m_descriptorPool, m_frames, flowScale);
			m_alphaPasses = LsfgAlphaPasses(context.device, shaders);
			for (size_t i = 0; i < LSFG_MIP_LEVELS; ++i)
				m_alpha[i] = LsfgAlpha(context, m_alphaPasses, m_resources, m_descriptorPool, m_mipmaps.Output(i));
			m_beta = LsfgBeta(context, shaders, m_resources, m_descriptorPool, m_alpha[0].Outputs());

			// from the coarsest level to the finest, each refines the motion of the one before
			for (size_t i = 0; i < LSFG_MIP_LEVELS; ++i)
			{
				const size_t level = LSFG_MIP_LEVELS - 1 - i;
				m_gamma[i] = LsfgGamma(context, shaders, m_resources, m_descriptorPool, m_alpha[level].Outputs(),
					m_beta.Output(std::min(level, LSFG_BETA_OUTPUTS - 1)), i == 0 ? nullptr : &m_gamma[i - 1].Output());
				if (i < FIRST_DELTA_LEVEL)
					continue;
				const size_t index = i - FIRST_DELTA_LEVEL;
				m_delta[index] = LsfgDelta(context, shaders, m_resources, m_descriptorPool, m_alpha[level].Outputs(), m_beta.Output(level),
					i == FIRST_DELTA_LEVEL ? nullptr : &m_gamma[i - 1].Output(),
					i == FIRST_DELTA_LEVEL ? nullptr : &m_delta[index - 1].Output1(),
					i == FIRST_DELTA_LEVEL ? nullptr : &m_delta[index - 1].Output2());
			}

			m_generate = LsfgGenerate(context, shaders, m_resources, m_descriptorPool, m_frames, m_gamma[LSFG_MIP_LEVELS - 1].Output(),
				m_delta[LSFG_DELTA_INSTANCES - 1].Output1(), m_delta[LSFG_DELTA_INSTANCES - 1].Output2(), m_output);
		}
		catch (...)
		{
			// members built so far are destroyed after this, the pool isn't a member object
			vkDestroyDescriptorPool(m_context.device, m_descriptorPool, nullptr);
			throw;
		}
	}

	LsfgChain::~LsfgChain()
	{
		vkDestroyDescriptorPool(m_context.device, m_descriptorPool, nullptr);
	}

	void LsfgChain::DispatchShared(VkCommandBuffer cmd, uint64 frameCount)
	{
		m_mipmaps.Dispatch(cmd, frameCount);
		for (size_t stage = 0; stage < LSFG_ALPHA_STAGES; ++stage)
		{
			LsfgBarriers barriers(cmd);
			for (auto& level : m_alpha)
				level.PushBarriers(barriers, frameCount, stage);
			barriers.Build();

			m_alphaPasses.Get(stage).BindPipeline(cmd);
			for (auto& level : m_alpha)
				level.DispatchStage(cmd, frameCount, stage);
		}
		m_beta.Dispatch(cmd, frameCount);
	}

	void LsfgChain::DispatchGeneration(VkCommandBuffer cmd, uint64 frameCount, size_t generationCount, size_t generation)
	{
		const size_t slot = LsfgGenerationSlot(generationCount, generation);
		for (size_t i = 0; i < LSFG_MIP_LEVELS; ++i)
		{
			m_gamma[i].Dispatch(cmd, frameCount, slot);
			if (i >= FIRST_DELTA_LEVEL)
				m_delta[i - FIRST_DELTA_LEVEL].Dispatch(cmd, frameCount, slot);
		}
		m_generate.Dispatch(cmd, frameCount, slot);
	}
}
