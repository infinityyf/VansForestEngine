#pragma once

#include "VansPoseTypes.h"

#include <cstdint>

namespace VansGraphics
{
	struct VansAnimationSampleRequest
	{
		float previousTime = 0.0f;
		float currentTime = 0.0f;
		float startTime = 0.0f;
		float endTime = -1.0f;
		bool loop = true;
		std::uint64_t sourceNodeId = 0;
		std::uint64_t sourceLayerId = 0;
	};

	class VansAnimationSampler
	{
	public:
		static bool Sample(const VansAnimationClip& clip,
		                   const Skeleton& skeleton,
		                   const VansAnimationSampleRequest& request,
		                   VansPosePayload& outPayload);

		static float ResolveSampleTime(float rawTime, float startTime,
		                               float endTime, bool loop);
	};
}
