#pragma once

#include "UpscalingCore/VansUpscalerTypes.h"

#include <cstdint>

namespace VansGraphics
{
	struct VansCommandRecordingConfig
	{
		bool parallelEnabled = true;
		bool frameContextRingEnabled = false;
		std::uint32_t framesInFlight = 2;
		bool asyncComputeEnabled = false;
	};

	// 最终渲染输出分辨率。0x0 表示继续跟随宿主窗口，兼容未声明该字段的旧项目。
	struct VansRenderOutputConfig
	{
		std::uint32_t width = 0;
		std::uint32_t height = 0;

		bool UsesWindowExtent() const { return width == 0 && height == 0; }
		bool HasExplicitExtent() const { return width > 0 && height > 0; }
	};

	struct VansAtmosphereQualityConfig
	{
		std::uint32_t transmittanceWidth = 256;
		std::uint32_t transmittanceHeight = 64;
		std::uint32_t multiScatteringWidth = 32;
		std::uint32_t multiScatteringHeight = 32;
		std::uint32_t skyViewWidth = 192;
		std::uint32_t skyViewHeight = 108;
		std::uint32_t farAerialTileSize = 12;
		std::uint32_t farAerialSlices = 64;
		float farAerialMaxDistanceMeters = 100000.0f;
		std::uint32_t transmittanceSamples = 40;
		std::uint32_t multiScatteringSamples = 32;
		std::uint32_t skyViewSamples = 32;
		std::uint32_t farAerialSamplesPerSlice = 2;
	};

	struct VansNearMediaQualityConfig
	{
		std::uint32_t tileSize = 8;
		std::uint32_t slices = 128;
		float nearDistanceMeters = 0.5f;
		float farDistanceMeters = 2000.0f;
		float sliceDistributionPower = 2.0f;
		bool temporalReprojection = true;
		float historyWeight = 0.9f;
	};

	struct VansCloudShadowQualityConfig
	{
		std::uint32_t clipmapCount = 2;
		std::uint32_t resolution = 512;
		float nearCoverageMeters = 10000.0f;
		float farCoverageMeters = 100000.0f;
		std::uint32_t rayMarchSamples = 32;
		float clipmapCrossFadeFraction = 0.1f;
	};

	// Renderer-owned configuration boundary shared by editor and packaged runtime.
	// Project serialization maps to this value type; render backends never depend
	// on ProjectSystem or editor DTOs.
	struct VansRenderRuntimeConfig
	{
		VansUpscalerConfig upscaler;
		VansCommandRecordingConfig commandRecording;
		VansRenderOutputConfig output;
		VansAtmosphereQualityConfig atmosphere;
		VansNearMediaQualityConfig nearMedia;
		VansCloudShadowQualityConfig cloudShadow;
	};
}
