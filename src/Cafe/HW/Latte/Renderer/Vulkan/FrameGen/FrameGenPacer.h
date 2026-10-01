// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Ported from the Eden emulator (src/video_core/renderer_vulkan/present/frame_gen_pacer.h). The settings are passed
// in instead of read from Eden's Settings.

#pragma once

#include <chrono>
#include <optional>

namespace FrameGen
{
	struct FrameGenPlan
	{
		size_t generations = 0;
		bool warm = false;
	};

	class FrameGenPacer
	{
	public:
		// fixedGenerations: generated frames per frame without a target rate. targetRate: frames per second to reach
		// by generating up to maxGenerations frames, 0 for a fixed count
		FrameGenPlan Plan(size_t capacity, size_t fixedGenerations, size_t maxGenerations, float targetRate);
		void Reset();

		// seconds between rendered frames, smoothed. 0 while unknown (start, after a stall)
		float SmoothedInterval() const { return m_smoothedInterval; }

	private:
		using Clock = std::chrono::steady_clock;

		void Stabilize(Clock::time_point now);
		void DeferEvaluations(Clock::duration amount);
		void UpdateLimit(Clock::time_point now, float baseRate, float targetRate, size_t ceiling);

		std::optional<Clock::time_point> m_lastFrame;
		std::optional<Clock::time_point> m_stableUntil;
		std::optional<Clock::time_point> m_probeUntil;
		std::optional<Clock::time_point> m_nextProbe;
		std::optional<Clock::time_point> m_deficitSince;
		float m_smoothedInterval = 0.0f;
		float m_outputCredit = 0.0f;
		float m_probeBaseRate = 0.0f;
		float m_unloadedBaseRate = 0.0f;
		size_t m_issuedGenerations = 0;
		size_t m_probePreviousLimit = 0;
		size_t m_limit = 0;
		uint32 m_probeFailures = 0;
	};
}
