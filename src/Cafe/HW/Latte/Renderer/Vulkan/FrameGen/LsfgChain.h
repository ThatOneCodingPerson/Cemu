// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 lsfg-vk
// SPDX-License-Identifier: GPL-3.0-or-later

// Ported from the Eden emulator (src/video_core/renderer_vulkan/present/lsfg_chain.h). The chain owns the image the
// generated frames are written to.

#pragma once

#include "Cafe/HW/Latte/Renderer/Vulkan/FrameGen/LsfgPasses.h"

namespace FrameGen
{
	constexpr size_t LSFG_DELTA_INSTANCES = 3;

	class LsfgChain
	{
	public:
		// extent: of the frames. flowScale: the resolution of the optical flow relative to the frames, 0.25 to 1
		LsfgChain(const LsfgContext& context, const LsfgShaders& shaders, VkExtent2D extent, float flowScale);
		~LsfgChain();
		LsfgChain(const LsfgChain&) = delete;
		LsfgChain& operator=(const LsfgChain&) = delete;

		// the passes shared by all frames generated before frame `frameCount`
		void DispatchShared(VkCommandBuffer cmd, uint64 frameCount);
		// writes the generated frame into Output()
		void DispatchGeneration(VkCommandBuffer cmd, uint64 frameCount, size_t generationCount, size_t generation);

		LsfgImage& Input(uint64 frameCount) { return m_frames[frameCount % m_frames.size()]; }
		LsfgImage& Output() { return m_output; }

	private:
		LsfgContext m_context;
		LsfgResources m_resources;
		VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;

		LsfgImagePair m_frames;
		LsfgImage m_output;
		LsfgMipmaps m_mipmaps;
		LsfgAlphaPasses m_alphaPasses;
		std::array<LsfgAlpha, LSFG_MIP_LEVELS> m_alpha;
		LsfgBeta m_beta;
		std::array<LsfgGamma, LSFG_MIP_LEVELS> m_gamma;
		std::array<LsfgDelta, LSFG_DELTA_INSTANCES> m_delta;
		LsfgGenerate m_generate;
	};
}
