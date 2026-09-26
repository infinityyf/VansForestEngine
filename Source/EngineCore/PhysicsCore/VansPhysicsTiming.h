#pragma once

#include <cmath>
#include <cstdint>

namespace VansEngine
{
	struct VansPhysicsTiming
	{
		static constexpr std::uint32_t kMaximumSupportedSubsteps = 16;
		static constexpr std::uint32_t kMaximumSupportedClothSubsteps = 16;

		float fixedTimeStep = 1.0f / 60.0f;
		std::uint32_t maximumSubsteps = 8;
		float clothFrameTime = 0.03f;
		std::uint32_t clothSubsteps = 8;

		bool IsValid() const
		{
			return std::isfinite(fixedTimeStep) && fixedTimeStep > 0.0f &&
				maximumSubsteps > 0 && maximumSubsteps <= kMaximumSupportedSubsteps &&
				std::isfinite(clothFrameTime) && clothFrameTime > 0.0f &&
				clothSubsteps > 0 && clothSubsteps <= kMaximumSupportedClothSubsteps;
		}
	};
}
