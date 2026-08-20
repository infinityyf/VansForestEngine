#include "VansTemporalJitterSequence.h"

#include <algorithm>
#include <cmath>

namespace
{
	float RadicalInverse(std::uint32_t index, std::uint32_t base)
	{
		float result = 0.0f;
		float fraction = 1.0f / static_cast<float>(base);
		while (index > 0)
		{
			result += static_cast<float>(index % base) * fraction;
			index /= base;
			fraction /= static_cast<float>(base);
		}
		return result;
	}
}

namespace VansGraphics
{
	std::int32_t VansTemporalJitterSequence::CalculatePhaseCount(
		std::uint32_t renderWidth,
		std::uint32_t outputWidth)
	{
		if (renderWidth == 0 || outputWidth == 0)
			return 0;
		const double ratio = static_cast<double>(outputWidth) /
			static_cast<double>(renderWidth);
		return std::clamp(
			static_cast<std::int32_t>(std::ceil(8.0 * ratio * ratio)), 1, 128);
	}

	bool VansTemporalJitterSequence::Sample(
		std::uint32_t frameIndex,
		std::int32_t phaseCount,
		float& pixelX,
		float& pixelY)
	{
		pixelX = 0.0f;
		pixelY = 0.0f;
		if (phaseCount <= 0)
			return false;
		const std::uint32_t index =
			(frameIndex % static_cast<std::uint32_t>(phaseCount)) + 1u;
		pixelX = RadicalInverse(index, 2u) - 0.5f;
		pixelY = RadicalInverse(index, 3u) - 0.5f;
		return std::isfinite(pixelX) && std::isfinite(pixelY);
	}
}
