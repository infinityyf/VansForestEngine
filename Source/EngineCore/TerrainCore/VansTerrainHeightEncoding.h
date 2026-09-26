#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Vans
{
struct VansTerrainHeightEncoding
{
	static constexpr std::uint16_t kMaximumSample =
		(std::numeric_limits<std::uint16_t>::max)();
	static constexpr float kMaximumSampleFloat = static_cast<float>(kMaximumSample);
	static constexpr std::int16_t kMaximumPhysicsSample =
		(std::numeric_limits<std::int16_t>::max)();
	static constexpr float kMaximumPhysicsSampleFloat =
		static_cast<float>(kMaximumPhysicsSample);

	static float DecodeNormalized(std::uint16_t sample)
	{
		return static_cast<float>(sample) / kMaximumSampleFloat;
	}

	static float DecodeWorld(std::uint16_t sample, float maximumHeight, float heightOffset)
	{
		return DecodeNormalized(sample) * maximumHeight + heightOffset;
	}

	static std::uint16_t EncodeNormalized(float normalizedHeight)
	{
		return static_cast<std::uint16_t>(std::lround(
			std::clamp(normalizedHeight, 0.0f, 1.0f) * kMaximumSampleFloat));
	}

	static std::int16_t EncodePhysics(std::uint16_t sample)
	{
		return static_cast<std::int16_t>(std::lround(
			static_cast<float>(sample) * kMaximumPhysicsSampleFloat / kMaximumSampleFloat));
	}

	static float PhysicsHeightScale(float maximumHeight)
	{
		return maximumHeight / kMaximumPhysicsSampleFloat;
	}
};
}
