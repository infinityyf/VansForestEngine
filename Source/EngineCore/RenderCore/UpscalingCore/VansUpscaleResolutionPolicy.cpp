#include "VansUpscaleResolutionPolicy.h"

#include <algorithm>
#include <cmath>

namespace
{
	std::uint32_t AlignDownEven(std::uint32_t value)
	{
		if (value <= 2u)
			return std::max(value, 1u);
		return value & ~1u;
	}
}

namespace VansGraphics
{
	bool VansUpscaleResolutionPolicy::ValidateConfig(
		const VansUpscalerConfig& config,
		std::string& error)
	{
		if (config.backend < VansUpscalerBackend::Off ||
			config.backend > VansUpscalerBackend::DLSS)
		{
			error = "Unknown upscaler backend";
			return false;
		}
		if (config.quality < VansUpscaleQualityMode::NativeAA ||
			config.quality > VansUpscaleQualityMode::UltraPerformance)
		{
			error = "Unknown upscaler quality";
			return false;
		}
		if (!std::isfinite(config.fsrSharpness) ||
			config.fsrSharpness < 0.0f || config.fsrSharpness > 1.0f)
		{
			error = "upscaler.fsrSharpness must be finite and in [0, 1]";
			return false;
		}
		if (config.backend == VansUpscalerBackend::Off &&
			config.quality != VansUpscaleQualityMode::NativeAA)
		{
			error = "Off upscaler backend requires NativeAA quality";
			return false;
		}
		error.clear();
		return true;
	}

	bool VansUpscaleResolutionPolicy::ValidateOutputExtent(
		VansExtent2D outputExtent,
		bool allowWindowExtent,
		std::uint32_t deviceMaximumDimension,
		std::string& error)
	{
		if (allowWindowExtent && outputExtent.width == 0u && outputExtent.height == 0u)
		{
			error.clear();
			return true;
		}
		const std::uint32_t maximumDimension = deviceMaximumDimension == 0u
			? MaximumOutputDimension
			: std::min(MaximumOutputDimension, deviceMaximumDimension);
		if (!outputExtent.IsValid() ||
			outputExtent.width < MinimumOutputWidth ||
			outputExtent.height < MinimumOutputHeight ||
			outputExtent.width > maximumDimension ||
			outputExtent.height > maximumDimension)
		{
			error = "outputResolution must be 0x0 (follow window) or an explicit "
				"resolution between " + std::to_string(MinimumOutputWidth) + "x" +
				std::to_string(MinimumOutputHeight) + " and " +
				std::to_string(maximumDimension) + "x" +
				std::to_string(maximumDimension);
			return false;
		}
		error.clear();
		return true;
	}

	float VansUpscaleResolutionPolicy::GetFSRScale(VansUpscaleQualityMode quality)
	{
		switch (quality)
		{
		case VansUpscaleQualityMode::NativeAA: return 1.0f;
		case VansUpscaleQualityMode::Quality: return 1.5f;
		case VansUpscaleQualityMode::Balanced: return 1.7f;
		case VansUpscaleQualityMode::Performance: return 2.0f;
		case VansUpscaleQualityMode::UltraPerformance: return 3.0f;
		default: return 0.0f;
		}
	}

	float VansUpscaleResolutionPolicy::ComputeMipBias(
		VansExtent2D renderExtent,
		VansExtent2D outputExtent)
	{
		if (!renderExtent.IsValid() || !outputExtent.IsValid())
			return 0.0f;
		const float ratioX = static_cast<float>(renderExtent.width) /
			static_cast<float>(outputExtent.width);
		const float ratioY = static_cast<float>(renderExtent.height) /
			static_cast<float>(outputExtent.height);
		const float ratio = std::min(ratioX, ratioY);
		return std::clamp(
			std::log2(std::max(ratio, 0.0001f)) - 1.0f,
			-3.0f,
			0.0f);
	}

	VansUpscaleResolution VansUpscaleResolutionPolicy::Resolve(
		const VansUpscalerConfig& config,
		VansExtent2D outputExtent,
		VansExtent2D backendRecommendedRenderExtent)
	{
		VansUpscaleResolution result;
		result.outputExtent = outputExtent;
		if (!outputExtent.IsValid())
		{
			result.error = "output extent is invalid";
			return result;
		}

		switch (config.backend)
		{
		case VansUpscalerBackend::Off:
			if (config.quality != VansUpscaleQualityMode::NativeAA)
			{
				result.error = "Off backend requires NativeAA quality";
				return result;
			}
			result.renderExtent = outputExtent;
			break;
		case VansUpscalerBackend::FSR:
		{
			const float scale = GetFSRScale(config.quality);
			if (scale <= 0.0f)
			{
				result.error = "unsupported FSR quality";
				return result;
			}
			result.renderExtent = {
				AlignDownEven(static_cast<std::uint32_t>(
					std::floor(static_cast<double>(outputExtent.width) / scale))),
				AlignDownEven(static_cast<std::uint32_t>(
					std::floor(static_cast<double>(outputExtent.height) / scale)))
			};
			break;
		}
		case VansUpscalerBackend::DLSS:
			if (!backendRecommendedRenderExtent.IsValid())
			{
				result.error = "DLSS optimal render extent is unavailable";
				return result;
			}
			if (backendRecommendedRenderExtent.width > outputExtent.width ||
				backendRecommendedRenderExtent.height > outputExtent.height)
			{
				result.error = "DLSS optimal render extent exceeds output extent";
				return result;
			}
			result.renderExtent = backendRecommendedRenderExtent;
			break;
		default:
			result.error = "unknown upscaler backend";
			return result;
		}

		result.mipBias = ComputeMipBias(result.renderExtent, result.outputExtent);
		result.valid = result.renderExtent.IsValid();
		if (!result.valid && result.error.empty())
			result.error = "resolved render extent is invalid";
		return result;
	}
}
