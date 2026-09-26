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
		static constexpr std::uint32_t MinimumOutputWidth = 320u;
		static constexpr std::uint32_t MinimumOutputHeight = 180u;
		static constexpr std::uint32_t MaximumOutputDimension = 16384u;

		static bool ValidateConfig(
			const VansUpscalerConfig& config,
			std::string& error);
		static bool ValidateOutputExtent(
			VansExtent2D outputExtent,
			bool allowWindowExtent,
			std::uint32_t deviceMaximumDimension,
			std::string& error);
		static VansUpscaleResolution Resolve(
			const VansUpscalerConfig& config,
			VansExtent2D outputExtent,
			VansExtent2D backendRecommendedRenderExtent = {});
		static float GetFSRScale(VansUpscaleQualityMode quality);
		static float ComputeMipBias(VansExtent2D renderExtent, VansExtent2D outputExtent);
	};
}
