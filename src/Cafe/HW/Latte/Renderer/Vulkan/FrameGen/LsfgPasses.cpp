// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// SPDX-FileCopyrightText: Copyright 2025 lsfg-vk
// SPDX-License-Identifier: GPL-3.0-or-later

// Ported from the Eden emulator (src/video_core/renderer_vulkan/present/lsfg_mipmaps.cpp, lsfg_alpha.cpp,
// lsfg_beta.cpp, lsfg_gamma.cpp, lsfg_delta.cpp, lsfg_generate.cpp).

#include "Cafe/HW/Latte/Renderer/Vulkan/FrameGen/LsfgPasses.h"

namespace FrameGen
{
	namespace
	{
		uint32 GroupCount(uint32 size, uint32 tileShift)
		{
			return (size + (1u << tileShift) - 1) >> tileShift;
		}

		VkExtent2D HalveExtent(VkExtent2D extent)
		{
			return {(extent.width + 1) >> 1, (extent.height + 1) >> 1};
		}

		constexpr uint32 MIPMAPS_TILE_SHIFT = 6;
		constexpr uint32 PASS_TILE_SHIFT = 3;
		constexpr uint32 BETA_OUTPUT_TILE_SHIFT = 5;
		constexpr uint32 GENERATE_TILE_SHIFT = 4;
	}

	/* LsfgMipmaps */

	LsfgMipmaps::LsfgMipmaps(const LsfgContext& context, const LsfgShaders& shaders, LsfgResources& resources, VkDescriptorPool pool, LsfgImagePair& frames, float flowScale)
		: m_frames(&frames)
	{
		using namespace PerformanceShader;
		m_pass = LsfgPass(context.device, shaders, MIPMAPS,
			{{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
			 {1, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {LSFG_MIP_LEVELS, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});

		const VkExtent2D inputExtent = (*m_frames)[0].Extent();
		m_flowExtent = {
			std::max(1u, static_cast<uint32>(static_cast<float>(inputExtent.width) * flowScale)),
			std::max(1u, static_cast<uint32>(static_cast<float>(inputExtent.height) * flowScale)),
		};
		for (size_t i = 0; i < LSFG_MIP_LEVELS; ++i)
			m_outImages[i] = LsfgImage(context, {m_flowExtent.width >> i, m_flowExtent.height >> i}, LSFG_FLOW_FORMAT);

		const std::vector<VkDescriptorSetLayout> layouts(m_descriptorSets.size(), m_pass.SetLayout());
		const std::vector<VkDescriptorSet> sets = AllocateLsfgDescriptorSets(context.device, pool, layouts);
		const VkSampler sampler = resources.GetSampler();
		const VkBuffer buffer = resources.GetBuffer();
		for (size_t i = 0; i < m_descriptorSets.size(); ++i)
		{
			m_descriptorSets[i] = sets[i];
			LsfgDescriptorWriter(m_descriptorSets[i])
				.AddUniformBuffer(buffer, LsfgResources::BufferSize())
				.AddSampler(sampler)
				.AddSampledImage((*m_frames)[i])
				.AddStorageImages(m_outImages)
				.Build(context.device);
		}
	}

	void LsfgMipmaps::Dispatch(VkCommandBuffer cmd, uint64 frameCount)
	{
		const size_t slot = frameCount % m_descriptorSets.size();
		LsfgBarriers(cmd).WriteToRead((*m_frames)[slot]).ReadToWriteAll(m_outImages).Build();
		m_pass.Bind(cmd, m_descriptorSets[slot]);
		vkCmdDispatch(cmd, GroupCount(m_flowExtent.width, MIPMAPS_TILE_SHIFT), GroupCount(m_flowExtent.height, MIPMAPS_TILE_SHIFT), 1);
	}

	/* LsfgAlpha */

	LsfgAlphaPasses::LsfgAlphaPasses(VkDevice device, const LsfgShaders& shaders)
	{
		using namespace PerformanceShader;
		m_passes[0] = LsfgPass(device, shaders, ALPHA[0],
			{{1, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		m_passes[1] = LsfgPass(device, shaders, ALPHA[1],
			{{1, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		m_passes[2] = LsfgPass(device, shaders, ALPHA[2],
			{{1, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		m_passes[3] = LsfgPass(device, shaders, ALPHA[3],
			{{1, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
	}

	LsfgAlpha::LsfgAlpha(const LsfgContext& context, const LsfgAlphaPasses& passes, LsfgResources& resources, VkDescriptorPool pool, LsfgImage& input)
		: m_passes(&passes), m_input(&input)
	{
		const VkExtent2D halfExtent = HalveExtent(m_input->Extent());
		const VkExtent2D quarterExtent = HalveExtent(halfExtent);
		m_temp1 = LsfgImage(context, halfExtent);
		m_temp2 = LsfgImage(context, halfExtent);
		for (size_t i = 0; i < m_temp3.size(); ++i)
		{
			m_temp3[i] = LsfgImage(context, quarterExtent);
			for (size_t j = 0; j < LSFG_HISTORY_SLOTS; ++j)
				m_outImages[j][i] = LsfgImage(context, quarterExtent);
		}

		std::vector<VkDescriptorSetLayout> layouts;
		for (size_t i = 0; i < LSFG_ALPHA_STAGES - 1; ++i)
			layouts.push_back(m_passes->Get(i).SetLayout());
		for (size_t i = 0; i < LSFG_HISTORY_SLOTS; ++i)
			layouts.push_back(m_passes->Get(3).SetLayout());
		const std::vector<VkDescriptorSet> sets = AllocateLsfgDescriptorSets(context.device, pool, layouts);
		for (size_t i = 0; i < LSFG_ALPHA_STAGES - 1; ++i)
			m_descriptorSets[i] = sets[i];
		for (size_t i = 0; i < LSFG_HISTORY_SLOTS; ++i)
			m_lastDescriptorSets[i] = sets[LSFG_ALPHA_STAGES - 1 + i];

		const VkSampler sampler = resources.GetSampler();
		LsfgDescriptorWriter(m_descriptorSets[0])
			.AddSampler(sampler)
			.AddSampledImage(*m_input)
			.AddStorageImage(m_temp1)
			.Build(context.device);
		LsfgDescriptorWriter(m_descriptorSets[1])
			.AddSampler(sampler)
			.AddSampledImage(m_temp1)
			.AddStorageImage(m_temp2)
			.Build(context.device);
		LsfgDescriptorWriter(m_descriptorSets[2])
			.AddSampler(sampler)
			.AddSampledImage(m_temp2)
			.AddStorageImages(m_temp3)
			.Build(context.device);
		for (size_t i = 0; i < LSFG_HISTORY_SLOTS; ++i)
		{
			LsfgDescriptorWriter(m_lastDescriptorSets[i])
				.AddSampler(sampler)
				.AddSampledImages(m_temp3)
				.AddStorageImages(m_outImages[i])
				.Build(context.device);
		}
	}

	void LsfgAlpha::PushBarriers(LsfgBarriers& barriers, uint64 frameCount, size_t stage)
	{
		switch (stage)
		{
		case 0:
			barriers.WriteToRead(*m_input).ReadToWrite(m_temp1);
			break;
		case 1:
			barriers.WriteToRead(m_temp1).ReadToWrite(m_temp2);
			break;
		case 2:
			barriers.WriteToRead(m_temp2).ReadToWriteAll(m_temp3);
			break;
		default:
			barriers.WriteToReadAll(m_temp3).ReadToWriteAll(m_outImages[frameCount % LSFG_HISTORY_SLOTS]);
			break;
		}
	}

	void LsfgAlpha::DispatchStage(VkCommandBuffer cmd, uint64 frameCount, size_t stage)
	{
		const VkExtent2D extent = stage < 2 ? m_temp1.Extent() : m_temp3[0].Extent();
		const VkDescriptorSet set = stage < LSFG_ALPHA_STAGES - 1 ? m_descriptorSets[stage] : m_lastDescriptorSets[frameCount % LSFG_HISTORY_SLOTS];
		m_passes->Get(stage).BindSet(cmd, set);
		vkCmdDispatch(cmd, GroupCount(extent.width, PASS_TILE_SHIFT), GroupCount(extent.height, PASS_TILE_SHIFT), 1);
	}

	/* LsfgBeta */

	LsfgBeta::LsfgBeta(const LsfgContext& context, const LsfgShaders& shaders, LsfgResources& resources, VkDescriptorPool pool, LsfgImageHistory& inputs)
		: m_inputs(&inputs)
	{
		using namespace PerformanceShader;
		m_passes[0] = LsfgPass(context.device, shaders, BETA[0],
			{{1, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {6, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		for (size_t i = 1; i < LSFG_BETA_STAGES - 1; ++i)
		{
			m_passes[i] = LsfgPass(context.device, shaders, BETA[i],
				{{1, VK_DESCRIPTOR_TYPE_SAMPLER},
				 {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
				 {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		}
		m_passes[4] = LsfgPass(context.device, shaders, BETA[4],
			{{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
			 {1, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {6, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});

		const VkExtent2D extent = (*m_inputs)[0][0].Extent();
		for (size_t i = 0; i < m_temp1.size(); ++i)
		{
			m_temp1[i] = LsfgImage(context, extent);
			m_temp2[i] = LsfgImage(context, extent);
		}
		for (size_t i = 0; i < LSFG_BETA_OUTPUTS; ++i)
			m_outImages[i] = LsfgImage(context, {extent.width >> i, extent.height >> i}, LSFG_FLOW_FORMAT);

		std::vector<VkDescriptorSetLayout> layouts;
		for (size_t i = 0; i < LSFG_HISTORY_SLOTS; ++i)
			layouts.push_back(m_passes[0].SetLayout());
		for (size_t i = 1; i < LSFG_BETA_STAGES; ++i)
			layouts.push_back(m_passes[i].SetLayout());
		const std::vector<VkDescriptorSet> sets = AllocateLsfgDescriptorSets(context.device, pool, layouts);
		for (size_t i = 0; i < LSFG_HISTORY_SLOTS; ++i)
			m_firstDescriptorSets[i] = sets[i];
		for (size_t i = 0; i < LSFG_BETA_STAGES - 1; ++i)
			m_descriptorSets[i] = sets[LSFG_HISTORY_SLOTS + i];

		const VkSampler sampler = resources.GetSampler();
		const VkSampler borderSampler = resources.GetSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_COMPARE_OP_NEVER, true);
		for (size_t i = 0; i < LSFG_HISTORY_SLOTS; ++i)
		{
			LsfgDescriptorWriter(m_firstDescriptorSets[i])
				.AddSampler(borderSampler)
				.AddSampledImages((*m_inputs)[(i + 1) % LSFG_HISTORY_SLOTS])
				.AddSampledImages((*m_inputs)[(i + 2) % LSFG_HISTORY_SLOTS])
				.AddSampledImages((*m_inputs)[i % LSFG_HISTORY_SLOTS])
				.AddStorageImages(m_temp1)
				.Build(context.device);
		}
		LsfgDescriptorWriter(m_descriptorSets[0])
			.AddSampler(sampler)
			.AddSampledImages(m_temp1)
			.AddStorageImages(m_temp2)
			.Build(context.device);
		LsfgDescriptorWriter(m_descriptorSets[1])
			.AddSampler(sampler)
			.AddSampledImages(m_temp2)
			.AddStorageImages(m_temp1)
			.Build(context.device);
		LsfgDescriptorWriter(m_descriptorSets[2])
			.AddSampler(sampler)
			.AddSampledImages(m_temp1)
			.AddStorageImages(m_temp2)
			.Build(context.device);
		LsfgDescriptorWriter(m_descriptorSets[3])
			.AddUniformBuffer(resources.GetBuffer(0.5f), LsfgResources::BufferSize())
			.AddSampler(sampler)
			.AddSampledImages(m_temp2)
			.AddStorageImages(m_outImages)
			.Build(context.device);
	}

	void LsfgBeta::Dispatch(VkCommandBuffer cmd, uint64 frameCount)
	{
		const VkExtent2D extent = m_temp1[0].Extent();
		const uint32 groupsX = GroupCount(extent.width, PASS_TILE_SHIFT);
		const uint32 groupsY = GroupCount(extent.height, PASS_TILE_SHIFT);

		LsfgBarriers barriers(cmd);
		for (auto& slot : *m_inputs)
			barriers.WriteToReadAll(slot);
		barriers.ReadToWriteAll(m_temp1).Build();
		m_passes[0].Bind(cmd, m_firstDescriptorSets[frameCount % LSFG_HISTORY_SLOTS]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd).WriteToReadAll(m_temp1).ReadToWriteAll(m_temp2).Build();
		m_passes[1].Bind(cmd, m_descriptorSets[0]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd).WriteToReadAll(m_temp2).ReadToWriteAll(m_temp1).Build();
		m_passes[2].Bind(cmd, m_descriptorSets[1]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd).WriteToReadAll(m_temp1).ReadToWriteAll(m_temp2).Build();
		m_passes[3].Bind(cmd, m_descriptorSets[2]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd).WriteToReadAll(m_temp2).ReadToWriteAll(m_outImages).Build();
		m_passes[4].Bind(cmd, m_descriptorSets[3]);
		vkCmdDispatch(cmd, GroupCount(extent.width, BETA_OUTPUT_TILE_SHIFT), GroupCount(extent.height, BETA_OUTPUT_TILE_SHIFT), 1);
	}

	/* LsfgGamma */

	LsfgGamma::LsfgGamma(const LsfgContext& context, const LsfgShaders& shaders, LsfgResources& resources, VkDescriptorPool pool, LsfgImageHistory& inputs, LsfgImage& flowInput, LsfgImage* previous)
		: m_inputs(&inputs), m_flowInput(&flowInput), m_previous(previous)
	{
		using namespace PerformanceShader;
		m_passes[0] = LsfgPass(context.device, shaders, GAMMA[0],
			{{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
			 {2, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		m_passes[1] = LsfgPass(context.device, shaders, GAMMA[1],
			{{1, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		m_passes[2] = LsfgPass(context.device, shaders, GAMMA[2],
			{{1, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		m_passes[3] = LsfgPass(context.device, shaders, GAMMA[3],
			{{1, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		m_passes[4] = LsfgPass(context.device, shaders, GAMMA[4],
			{{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
			 {2, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});

		const VkExtent2D extent = (*m_inputs)[0][0].Extent();
		for (auto& image : m_temp1)
			image = LsfgImage(context, extent);
		for (auto& image : m_temp2)
			image = LsfgImage(context, extent);
		m_outImage = LsfgImage(context, extent, LSFG_MOTION_FORMAT);

		std::vector<VkDescriptorSetLayout> layouts;
		for (size_t slot = 0; slot < LSFG_GENERATION_SLOTS; ++slot)
		{
			for (size_t i = 0; i < LSFG_HISTORY_SLOTS; ++i)
				layouts.push_back(m_passes[0].SetLayout());
			for (size_t i = 1; i < LSFG_GAMMA_STAGES; ++i)
				layouts.push_back(m_passes[i].SetLayout());
		}
		const std::vector<VkDescriptorSet> sets = AllocateLsfgDescriptorSets(context.device, pool, layouts);

		const VkSampler sampler = resources.GetSampler();
		const VkSampler borderSampler = resources.GetSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_COMPARE_OP_NEVER, true);
		const VkSampler edgeSampler = resources.GetSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_COMPARE_OP_ALWAYS, false);

		size_t next = 0;
		for (size_t slot = 0; slot < LSFG_GENERATION_SLOTS; ++slot)
		{
			Generation& pass = m_generations[slot];
			const VkBuffer buffer = resources.GetBuffer(LsfgSlotTimestamp(slot), m_previous == nullptr);
			for (size_t i = 0; i < LSFG_HISTORY_SLOTS; ++i)
				pass.firstDescriptorSets[i] = sets[next++];
			for (size_t i = 0; i < LSFG_GAMMA_STAGES - 1; ++i)
				pass.descriptorSets[i] = sets[next++];

			for (size_t i = 0; i < LSFG_HISTORY_SLOTS; ++i)
			{
				LsfgDescriptorWriter(pass.firstDescriptorSets[i])
					.AddUniformBuffer(buffer, LsfgResources::BufferSize())
					.AddSampler(borderSampler)
					.AddSampler(edgeSampler)
					.AddSampledImages((*m_inputs)[(i + 2) % LSFG_HISTORY_SLOTS])
					.AddSampledImages((*m_inputs)[i % LSFG_HISTORY_SLOTS])
					.AddSampledImage(m_previous)
					.AddStorageImages(m_temp1)
					.Build(context.device);
			}
			LsfgDescriptorWriter(pass.descriptorSets[0])
				.AddSampler(sampler)
				.AddSampledImages(m_temp1)
				.AddStorageImages(m_temp2)
				.Build(context.device);
			LsfgDescriptorWriter(pass.descriptorSets[1])
				.AddSampler(sampler)
				.AddSampledImages(m_temp2)
				.AddStorageImage(m_temp1[0])
				.AddStorageImage(m_temp1[1])
				.Build(context.device);
			LsfgDescriptorWriter(pass.descriptorSets[2])
				.AddSampler(sampler)
				.AddSampledImage(m_temp1[0])
				.AddSampledImage(m_temp1[1])
				.AddStorageImages(m_temp2)
				.Build(context.device);
			LsfgDescriptorWriter(pass.descriptorSets[3])
				.AddUniformBuffer(buffer, LsfgResources::BufferSize())
				.AddSampler(sampler)
				.AddSampler(edgeSampler)
				.AddSampledImages(m_temp2)
				.AddSampledImage(m_previous)
				.AddSampledImage(*m_flowInput)
				.AddStorageImage(m_outImage)
				.Build(context.device);
		}
	}

	void LsfgGamma::Dispatch(VkCommandBuffer cmd, uint64 frameCount, size_t slot)
	{
		const Generation& pass = m_generations[slot];
		const VkExtent2D extent = m_temp1[0].Extent();
		const uint32 groupsX = GroupCount(extent.width, PASS_TILE_SHIFT);
		const uint32 groupsY = GroupCount(extent.height, PASS_TILE_SHIFT);
		const size_t history = frameCount % LSFG_HISTORY_SLOTS;
		const size_t previousHistory = (frameCount + 2) % LSFG_HISTORY_SLOTS;

		LsfgBarriers(cmd)
			.WriteToReadAll((*m_inputs)[previousHistory])
			.WriteToReadAll((*m_inputs)[history])
			.WriteToRead(m_previous)
			.ReadToWriteAll(m_temp1)
			.Build();
		m_passes[0].Bind(cmd, pass.firstDescriptorSets[history]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd).WriteToReadAll(m_temp1).ReadToWriteAll(m_temp2).Build();
		m_passes[1].Bind(cmd, pass.descriptorSets[0]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd)
			.WriteToReadAll(m_temp2)
			.ReadToWrite(m_temp1[0])
			.ReadToWrite(m_temp1[1])
			.Build();
		m_passes[2].Bind(cmd, pass.descriptorSets[1]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd)
			.WriteToRead(m_temp1[0])
			.WriteToRead(m_temp1[1])
			.ReadToWriteAll(m_temp2)
			.Build();
		m_passes[3].Bind(cmd, pass.descriptorSets[2]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd)
			.WriteToReadAll(m_temp2)
			.WriteToRead(m_previous)
			.WriteToRead(*m_flowInput)
			.ReadToWrite(m_outImage)
			.Build();
		m_passes[4].Bind(cmd, pass.descriptorSets[3]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);
	}

	/* LsfgDelta */

	LsfgDelta::LsfgDelta(const LsfgContext& context, const LsfgShaders& shaders, LsfgResources& resources, VkDescriptorPool pool, LsfgImageHistory& inputs, LsfgImage& flowInput,
		LsfgImage* previousGamma, LsfgImage* previous1, LsfgImage* previous2)
		: m_inputs(&inputs), m_flowInput(&flowInput), m_previousGamma(previousGamma), m_previous1(previous1), m_previous2(previous2)
	{
		using namespace PerformanceShader;
		m_passes[0] = LsfgPass(context.device, shaders, DELTA[0],
			{{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
			 {2, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		m_passes[1] = LsfgPass(context.device, shaders, DELTA[1],
			{{1, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {3, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		m_passes[2] = LsfgPass(context.device, shaders, DELTA[2],
			{{1, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		m_passes[3] = LsfgPass(context.device, shaders, DELTA[3],
			{{1, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		m_passes[4] = LsfgPass(context.device, shaders, DELTA[4],
			{{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
			 {2, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {4, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		m_passes[5] = LsfgPass(context.device, shaders, DELTA[5],
			{{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
			 {2, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {6, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		for (size_t i = 6; i < LSFG_DELTA_STAGES - 1; ++i)
		{
			m_passes[i] = LsfgPass(context.device, shaders, DELTA[i],
				{{1, VK_DESCRIPTOR_TYPE_SAMPLER},
				 {1, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
				 {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});
		}
		m_passes[9] = LsfgPass(context.device, shaders, DELTA[9],
			{{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
			 {2, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {2, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});

		const VkExtent2D extent = (*m_inputs)[0][0].Extent();
		for (auto& image : m_temp1)
			image = LsfgImage(context, extent);
		for (auto& image : m_temp2)
			image = LsfgImage(context, extent);
		m_outImage1 = LsfgImage(context, extent, LSFG_MOTION_FORMAT);
		m_outImage2 = LsfgImage(context, extent, LSFG_MOTION_FORMAT);

		std::vector<VkDescriptorSetLayout> layouts;
		for (size_t slot = 0; slot < LSFG_GENERATION_SLOTS; ++slot)
		{
			for (size_t i = 0; i < LSFG_HISTORY_SLOTS; ++i)
				layouts.push_back(m_passes[0].SetLayout());
			for (size_t i = 1; i <= 4; ++i)
				layouts.push_back(m_passes[i].SetLayout());
			for (size_t i = 0; i < LSFG_HISTORY_SLOTS; ++i)
				layouts.push_back(m_passes[5].SetLayout());
			for (size_t i = 6; i < LSFG_DELTA_STAGES; ++i)
				layouts.push_back(m_passes[i].SetLayout());
		}
		const std::vector<VkDescriptorSet> sets = AllocateLsfgDescriptorSets(context.device, pool, layouts);

		const VkSampler sampler = resources.GetSampler();
		const VkSampler borderSampler = resources.GetSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_COMPARE_OP_NEVER, true);
		const VkSampler edgeSampler = resources.GetSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_COMPARE_OP_ALWAYS, false);

		size_t next = 0;
		for (size_t slot = 0; slot < LSFG_GENERATION_SLOTS; ++slot)
		{
			Generation& pass = m_generations[slot];
			const VkBuffer buffer = resources.GetBuffer(LsfgSlotTimestamp(slot), false, m_previousGamma == nullptr);

			for (size_t i = 0; i < LSFG_HISTORY_SLOTS; ++i)
				pass.firstDescriptorSets[i] = sets[next++];
			for (size_t i = 0; i < 4; ++i)
				pass.descriptorSets[i] = sets[next++];
			for (size_t i = 0; i < LSFG_HISTORY_SLOTS; ++i)
				pass.sixthDescriptorSets[i] = sets[next++];
			for (size_t i = 4; i < LSFG_DELTA_STAGES - 2; ++i)
				pass.descriptorSets[i] = sets[next++];

			for (size_t i = 0; i < LSFG_HISTORY_SLOTS; ++i)
			{
				LsfgDescriptorWriter(pass.firstDescriptorSets[i])
					.AddUniformBuffer(buffer, LsfgResources::BufferSize())
					.AddSampler(borderSampler)
					.AddSampler(edgeSampler)
					.AddSampledImages((*m_inputs)[(i + 2) % LSFG_HISTORY_SLOTS])
					.AddSampledImages((*m_inputs)[i % LSFG_HISTORY_SLOTS])
					.AddSampledImage(m_previousGamma)
					.AddStorageImages(m_temp1)
					.Build(context.device);
				LsfgDescriptorWriter(pass.sixthDescriptorSets[i])
					.AddUniformBuffer(buffer, LsfgResources::BufferSize())
					.AddSampler(borderSampler)
					.AddSampler(edgeSampler)
					.AddSampledImages((*m_inputs)[(i + 2) % LSFG_HISTORY_SLOTS])
					.AddSampledImages((*m_inputs)[i % LSFG_HISTORY_SLOTS])
					.AddSampledImage(m_previousGamma)
					.AddSampledImage(m_previous1)
					.AddStorageImage(m_temp2[0])
					.Build(context.device);
			}
			LsfgDescriptorWriter(pass.descriptorSets[0])
				.AddSampler(sampler)
				.AddSampledImages(m_temp1)
				.AddStorageImages(m_temp2)
				.Build(context.device);
			LsfgDescriptorWriter(pass.descriptorSets[1])
				.AddSampler(sampler)
				.AddSampledImages(m_temp2)
				.AddStorageImage(m_temp1[0])
				.AddStorageImage(m_temp1[1])
				.Build(context.device);
			LsfgDescriptorWriter(pass.descriptorSets[2])
				.AddSampler(sampler)
				.AddSampledImage(m_temp1[0])
				.AddSampledImage(m_temp1[1])
				.AddStorageImages(m_temp2)
				.Build(context.device);
			LsfgDescriptorWriter(pass.descriptorSets[3])
				.AddUniformBuffer(buffer, LsfgResources::BufferSize())
				.AddSampler(sampler)
				.AddSampler(edgeSampler)
				.AddSampledImages(m_temp2)
				.AddSampledImage(m_previousGamma)
				.AddSampledImage(*m_flowInput)
				.AddStorageImage(m_outImage1)
				.Build(context.device);
			LsfgDescriptorWriter(pass.descriptorSets[4])
				.AddSampler(sampler)
				.AddSampledImage(m_temp2[0])
				.AddStorageImage(m_temp1[0])
				.Build(context.device);
			LsfgDescriptorWriter(pass.descriptorSets[5])
				.AddSampler(sampler)
				.AddSampledImage(m_temp1[0])
				.AddStorageImage(m_temp2[0])
				.Build(context.device);
			LsfgDescriptorWriter(pass.descriptorSets[6])
				.AddSampler(sampler)
				.AddSampledImage(m_temp2[0])
				.AddStorageImage(m_temp1[0])
				.Build(context.device);
			LsfgDescriptorWriter(pass.descriptorSets[7])
				.AddUniformBuffer(buffer, LsfgResources::BufferSize())
				.AddSampler(sampler)
				.AddSampler(edgeSampler)
				.AddSampledImage(m_temp1[0])
				.AddSampledImage(m_previous2)
				.AddStorageImage(m_outImage2)
				.Build(context.device);
		}
	}

	void LsfgDelta::Dispatch(VkCommandBuffer cmd, uint64 frameCount, size_t slot)
	{
		const Generation& pass = m_generations[slot];
		const VkExtent2D extent = m_temp1[0].Extent();
		const uint32 groupsX = GroupCount(extent.width, PASS_TILE_SHIFT);
		const uint32 groupsY = GroupCount(extent.height, PASS_TILE_SHIFT);
		const size_t history = frameCount % LSFG_HISTORY_SLOTS;
		const size_t previousHistory = (frameCount + 2) % LSFG_HISTORY_SLOTS;

		LsfgBarriers(cmd)
			.WriteToReadAll((*m_inputs)[previousHistory])
			.WriteToReadAll((*m_inputs)[history])
			.WriteToRead(m_previousGamma)
			.ReadToWriteAll(m_temp1)
			.Build();
		m_passes[0].Bind(cmd, pass.firstDescriptorSets[history]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd).WriteToReadAll(m_temp1).ReadToWriteAll(m_temp2).Build();
		m_passes[1].Bind(cmd, pass.descriptorSets[0]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd).WriteToReadAll(m_temp2).ReadToWriteAll(m_temp1).Build();
		m_passes[2].Bind(cmd, pass.descriptorSets[1]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd).WriteToReadAll(m_temp1).ReadToWriteAll(m_temp2).Build();
		m_passes[3].Bind(cmd, pass.descriptorSets[2]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd)
			.WriteToReadAll(m_temp2)
			.WriteToRead(m_previousGamma)
			.WriteToRead(*m_flowInput)
			.ReadToWrite(m_outImage1)
			.Build();
		m_passes[4].Bind(cmd, pass.descriptorSets[3]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd)
			.WriteToReadAll((*m_inputs)[previousHistory])
			.WriteToReadAll((*m_inputs)[history])
			.WriteToRead(m_previousGamma)
			.WriteToRead(m_previous1)
			.ReadToWriteAll(m_temp2)
			.Build();
		m_passes[5].Bind(cmd, pass.sixthDescriptorSets[history]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd)
			.WriteToReadAll(m_temp2)
			.ReadToWrite(m_temp1[0])
			.ReadToWrite(m_temp1[1])
			.Build();
		m_passes[6].Bind(cmd, pass.descriptorSets[4]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd)
			.WriteToRead(m_temp1[0])
			.WriteToRead(m_temp1[1])
			.ReadToWriteAll(m_temp2)
			.Build();
		m_passes[7].Bind(cmd, pass.descriptorSets[5]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd)
			.WriteToReadAll(m_temp2)
			.ReadToWrite(m_temp1[0])
			.ReadToWrite(m_temp1[1])
			.Build();
		m_passes[8].Bind(cmd, pass.descriptorSets[6]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);

		LsfgBarriers(cmd)
			.WriteToRead(m_temp1[0])
			.WriteToRead(m_temp1[1])
			.WriteToRead(m_previous2)
			.ReadToWrite(m_outImage2)
			.Build();
		m_passes[9].Bind(cmd, pass.descriptorSets[7]);
		vkCmdDispatch(cmd, groupsX, groupsY, 1);
	}

	/* LsfgGenerate */

	LsfgGenerate::LsfgGenerate(const LsfgContext& context, const LsfgShaders& shaders, LsfgResources& resources, VkDescriptorPool pool, LsfgImagePair& frames, LsfgImage& motion,
		LsfgImage& detail1, LsfgImage& detail2, LsfgImage& output)
		: m_frames(&frames), m_motion(&motion), m_detail1(&detail1), m_detail2(&detail2), m_output(&output)
	{
		using namespace PerformanceShader;
		m_pass = LsfgPass(context.device, shaders, GENERATE,
			{{1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
			 {2, VK_DESCRIPTOR_TYPE_SAMPLER},
			 {5, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
			 {1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE}});

		const VkSampler sampler = resources.GetSampler();
		const VkSampler edgeSampler = resources.GetSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_COMPARE_OP_ALWAYS, false);
		const std::vector<VkDescriptorSetLayout> layouts(LSFG_GENERATION_SLOTS * 2, m_pass.SetLayout());
		const std::vector<VkDescriptorSet> sets = AllocateLsfgDescriptorSets(context.device, pool, layouts);

		size_t next = 0;
		for (size_t slot = 0; slot < LSFG_GENERATION_SLOTS; ++slot)
		{
			const VkBuffer buffer = resources.GetBuffer(LsfgSlotTimestamp(slot));
			std::array<VkDescriptorSet, 2>& slotSets = m_generations[slot].descriptorSets;
			for (size_t i = 0; i < slotSets.size(); ++i)
			{
				slotSets[i] = sets[next++];
				// previous frame, current frame
				LsfgDescriptorWriter(slotSets[i])
					.AddUniformBuffer(buffer, LsfgResources::BufferSize())
					.AddSampler(sampler)
					.AddSampler(edgeSampler)
					.AddSampledImage((*m_frames)[1 - i])
					.AddSampledImage((*m_frames)[i])
					.AddSampledImage(*m_motion)
					.AddSampledImage(*m_detail1)
					.AddSampledImage(*m_detail2)
					.AddStorageImage(*m_output)
					.Build(context.device);
			}
		}
	}

	void LsfgGenerate::Dispatch(VkCommandBuffer cmd, uint64 frameCount, size_t slot)
	{
		LsfgBarriers(cmd)
			.WriteToReadAll(*m_frames)
			.WriteToRead(*m_motion)
			.WriteToRead(*m_detail1)
			.WriteToRead(*m_detail2)
			.Build();

		// the previous copy out of the output has to finish before it is overwritten, its contents are not needed
		VkImageMemoryBarrier discard{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
		discard.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		discard.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		discard.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		discard.newLayout = VK_IMAGE_LAYOUT_GENERAL;
		discard.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		discard.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		discard.image = m_output->Handle();
		discard.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1, &discard);

		const std::array<VkDescriptorSet, 2>& slotSets = m_generations[slot].descriptorSets;
		m_pass.Bind(cmd, slotSets[frameCount % slotSets.size()]);
		const VkExtent2D extent = m_output->Extent();
		vkCmdDispatch(cmd, GroupCount(extent.width, GENERATE_TILE_SHIFT), GroupCount(extent.height, GENERATE_TILE_SHIFT), 1);

		VkImageMemoryBarrier written = discard;
		written.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
		written.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
		written.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
		vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &written);
		m_output->SetLayout(VK_IMAGE_LAYOUT_GENERAL);
	}
}
