#pragma once

#include <cstdint>

namespace VansGraphics
{
	class VansTemporalJitterSequence
	{
	public:
		static std::int32_t CalculatePhaseCount(
			std::uint32_t renderWidth,
			std::uint32_t outputWidth);
		static bool Sample(
			std::uint32_t frameIndex,
			std::int32_t phaseCount,
			float& pixelX,
			float& pixelY);
	};
}
