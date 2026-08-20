#pragma once

#include "VansUpscalerTypes.h"

#include <string>

namespace VansGraphics
{
	struct VansUpscaleResolution
	{
		VansExtent2D renderExtent;
		VansExtent2D outputExtent;
		float mipBias = 0.0f;
		bool valid = false;
		std::string error;
	};

	class VansUpscaleResolutionPolicy
	{
	public:
		static VansUpscaleResolution Resolve(
			const VansUpscalerConfig& config,
			VansExtent2D outputExtent,
			VansExtent2D backendRecommendedRenderExtent = {});
		static float GetFSRScale(VansUpscaleQualityMode quality);
		static float ComputeMipBias(VansExtent2D renderExtent, VansExtent2D outputExtent);
	};
}
