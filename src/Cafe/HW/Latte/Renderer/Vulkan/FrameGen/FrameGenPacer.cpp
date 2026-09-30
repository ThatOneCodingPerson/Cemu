// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

// Ported from the Eden emulator (src/video_core/renderer_vulkan/present/frame_gen_pacer.cpp).

#include "Cafe/HW/Latte/Renderer/Vulkan/FrameGen/FrameGenPacer.h"

namespace FrameGen
{
	namespace
	{
		using Clock = std::chrono::steady_clock;

		constexpr float INTERVAL_SMOOTHING = 0.25f;
		constexpr float MINIMUM_BASE_RATE = 10.0f;
		constexpr float BURST_CADENCE_RATIO = 3.0f;
		constexpr float BURST_TARGET_RATIO = 2.0f;
		constexpr float PROBE_THROUGHPUT_TOLERANCE = 0.95f;
		constexpr float PROBE_BASE_COLLAPSE_RATIO = 0.70f;
		constexpr float PROBE_MARGINAL_GAIN = 1.15f;
		constexpr float TARGET_SATISFIED_RATIO = 0.95f;
		constexpr float UNLOADED_BASE_RETENTION = 0.75f;
		constexpr float CREDIT_EPSILON = 1.0e-4f;
		constexpr uint32 MAX_PROBE_FAILURES = 4;

		constexpr auto STABILIZATION_DURATION = std::chrono::seconds(1);
		constexpr auto PROBE_DURATION = std::chrono::seconds(1);
		constexpr auto DEFICIT_DURATION = std::chrono::seconds(1);
		constexpr auto PROBE_STEP_DELAY = std::chrono::milliseconds(250);

		Clock::duration ProbeBackoff(uint32 failures)
		{
			switch (failures)
			{
			case 1:
				return std::chrono::seconds(5);
			case 2:
				return std::chrono::seconds(15);
			case 3:
				return std::chrono::seconds(30);
			default:
				return std::chrono::seconds(60);
			}
		}
	}

	FrameGenPlan FrameGenPacer::Plan(size_t capacity, size_t fixedGenerations, size_t maxGenerations, float targetRate)
	{
		const size_t ceiling = std::min(capacity, maxGenerations);
		if (ceiling == 0)
		{
			Reset();
			return {};
		}

		const Clock::time_point now = Clock::now();
		const size_t previousGenerations = std::exchange(m_issuedGenerations, 0);
		if (!m_lastFrame)
		{
			m_lastFrame = now;
			return {};
		}

		const Clock::duration interval = now - *m_lastFrame;
		const float intervalSeconds = std::chrono::duration<float>(interval).count();
		m_lastFrame = now;
		if (intervalSeconds <= 0.0f)
		{
			Stabilize(now);
			return {};
		}

		// a burst of frames (loading, catching up) is not the game's frame rate
		if (m_smoothedInterval > 0.0f)
		{
			float burstThreshold = BURST_CADENCE_RATIO / m_smoothedInterval;
			if (targetRate > 0.0f)
				burstThreshold = std::max(burstThreshold, targetRate * BURST_TARGET_RATIO);
			if (1.0f / intervalSeconds > burstThreshold)
			{
				DeferEvaluations(interval);
				m_outputCredit = 0.0f;
				return {};
			}
		}

		// a stall, e.g. a loading screen or shader compilation
		if (intervalSeconds > 1.0f / MINIMUM_BASE_RATE)
		{
			Stabilize(now);
			return {};
		}

		m_smoothedInterval = m_smoothedInterval > 0.0f ? m_smoothedInterval + (intervalSeconds - m_smoothedInterval) * INTERVAL_SMOOTHING : intervalSeconds;

		if (previousGenerations == 0)
		{
			const float measured = 1.0f / m_smoothedInterval;
			m_unloadedBaseRate = m_unloadedBaseRate > 0.0f ? m_unloadedBaseRate + (measured - m_unloadedBaseRate) * INTERVAL_SMOOTHING : measured;
		}

		if (m_stableUntil)
		{
			if (now < *m_stableUntil)
				return {};
			m_stableUntil.reset();
		}

		if (targetRate == 0.0f)
		{
			m_limit = std::min(fixedGenerations, ceiling);
			m_outputCredit = 0.0f;
			m_issuedGenerations = m_limit;
			return {.generations = m_limit, .warm = m_limit > 0};
		}

		UpdateLimit(now, 1.0f / m_smoothedInterval, targetRate, ceiling);

		const size_t allowed = std::min(m_limit, ceiling);
		const float desiredOutputs = m_smoothedInterval * targetRate;
		if (allowed == 0 || desiredOutputs <= 1.0f)
		{
			m_outputCredit = 0.0f;
			return {};
		}

		m_outputCredit += desiredOutputs;
		const size_t outputs = std::max<size_t>(1, static_cast<size_t>(std::floor(m_outputCredit + CREDIT_EPSILON)));
		const size_t generations = std::min(outputs - 1, allowed);
		m_outputCredit -= static_cast<float>(generations + 1);
		if (m_outputCredit < 0.0f)
			m_outputCredit = 0.0f;
		else if (generations == allowed && m_outputCredit >= 1.0f)
			m_outputCredit = std::fmod(m_outputCredit, 1.0f);

		m_issuedGenerations = generations;
		return {.generations = generations, .warm = true};
	}

	// raises the number of generated frames while that brings the output closer to the target rate, and lowers it
	// again if the extra work slows the emulation down
	void FrameGenPacer::UpdateLimit(Clock::time_point now, float baseRate, float targetRate, size_t ceiling)
	{
		m_limit = std::min(m_limit, ceiling);

		if (m_probeUntil)
		{
			if (now < *m_probeUntil)
				return;
			m_probeUntil.reset();
			m_outputCredit = 0.0f;

			const float previousOutput = std::min(targetRate, m_probeBaseRate * static_cast<float>(m_probePreviousLimit + 1));
			const float currentOutput = std::min(targetRate, baseRate * static_cast<float>(m_limit + 1));
			const bool throughputRegressed = currentOutput < previousOutput * PROBE_THROUGHPUT_TOLERANCE;
			const bool collapsedForMarginalGain = baseRate < m_probeBaseRate * PROBE_BASE_COLLAPSE_RATIO && currentOutput < previousOutput * PROBE_MARGINAL_GAIN;
			const bool emulationSlowed = m_unloadedBaseRate > 0.0f && baseRate < m_unloadedBaseRate * UNLOADED_BASE_RETENTION;
			if (throughputRegressed || collapsedForMarginalGain || emulationSlowed)
			{
				m_limit = m_probePreviousLimit;
				m_probeFailures = std::min(m_probeFailures + 1, MAX_PROBE_FAILURES);
				m_nextProbe = now + ProbeBackoff(m_probeFailures);
				m_deficitSince.reset();
				return;
			}
			m_probeFailures = 0;
			m_nextProbe = now + PROBE_STEP_DELAY;
		}

		if (baseRate * static_cast<float>(m_limit + 1) >= targetRate * TARGET_SATISFIED_RATIO || m_limit >= ceiling)
		{
			m_deficitSince.reset();
			return;
		}
		if (!m_deficitSince)
		{
			m_deficitSince = now;
			return;
		}
		if (now - *m_deficitSince < DEFICIT_DURATION)
			return;
		if (m_nextProbe && now < *m_nextProbe)
			return;

		m_probePreviousLimit = m_limit;
		m_probeBaseRate = baseRate;
		++m_limit;
		m_probeUntil = now + PROBE_DURATION;
		m_deficitSince.reset();
		m_outputCredit = 0.0f;
	}

	void FrameGenPacer::DeferEvaluations(Clock::duration amount)
	{
		const auto defer = [amount](std::optional<Clock::time_point>& deadline) {
			if (deadline)
				*deadline += amount;
		};
		defer(m_stableUntil);
		defer(m_probeUntil);
		defer(m_nextProbe);
		m_deficitSince.reset();
	}

	void FrameGenPacer::Stabilize(Clock::time_point now)
	{
		m_stableUntil = now + STABILIZATION_DURATION;
		m_probeUntil.reset();
		m_deficitSince.reset();
		m_smoothedInterval = 0.0f;
		m_outputCredit = 0.0f;
	}

	void FrameGenPacer::Reset()
	{
		m_lastFrame.reset();
		m_stableUntil.reset();
		m_probeUntil.reset();
		m_nextProbe.reset();
		m_deficitSince.reset();
		m_smoothedInterval = 0.0f;
		m_outputCredit = 0.0f;
		m_probeBaseRate = 0.0f;
		m_unloadedBaseRate = 0.0f;
		m_issuedGenerations = 0;
		m_probePreviousLimit = 0;
		m_limit = 0;
		m_probeFailures = 0;
	}
}
