#pragma once

#include <cstdint>

namespace Vans
{
struct VansGAFPerformanceBudget
{
	static constexpr std::uint32_t DefaultMaximumActiveActionsPerHost = 64;
	static constexpr std::uint32_t DefaultMaximumTasksPerAction = 64;
	static constexpr std::uint32_t DefaultMaximumGraphTransitionsPerTick = 1024;
	static constexpr std::uint32_t DefaultMaximumEffectsPerHost = 256;
	static constexpr std::uint32_t DefaultMaximumCueHistoryPerHost = 4096;
	static constexpr double DefaultMinimumEffectPeriodSeconds = 1.0 / 240.0;
	static constexpr std::uint32_t DefaultMaximumEffectPulsesPerTick = 256;
	static constexpr std::uint32_t DefaultMaximumPayloadBytes = 4096;

	std::uint32_t maximumActiveActionsPerHost = DefaultMaximumActiveActionsPerHost;
	std::uint32_t maximumTasksPerAction = DefaultMaximumTasksPerAction;
	std::uint32_t maximumGraphTransitionsPerTick = DefaultMaximumGraphTransitionsPerTick;
	std::uint32_t maximumEffectsPerHost = DefaultMaximumEffectsPerHost;
	std::uint32_t maximumCueHistoryPerHost = DefaultMaximumCueHistoryPerHost;
	double minimumEffectPeriodSeconds = DefaultMinimumEffectPeriodSeconds;
	std::uint32_t maximumEffectPulsesPerTick = DefaultMaximumEffectPulsesPerTick;
	std::uint32_t maximumPayloadBytes = DefaultMaximumPayloadBytes;
};
}
