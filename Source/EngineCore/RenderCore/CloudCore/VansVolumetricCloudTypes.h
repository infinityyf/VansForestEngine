#pragma once

#include <cstddef>
#include <glm/glm.hpp>

namespace VansGraphics
{
	// 体积云的 GPU ABI 由 CloudCore 单独拥有，避免材质系统承担环境介质状态。
	struct alignas(16) VansVolumetricCloudMediumGPU final
	{
		float planetBottomRadius = 6340000.0f;
		float cloudMinHeight = 1070.0f;
		float cloudMaxHeight = 7410.0f;
		float density = 0.025f;
		float coverage = 0.350f;
		float sunBrightness = 0.380f;
		float atmosphereShadowStrength = 1.0f;
		float mainTileMeters = 43300.0f;
		float detailTileMeters = 2200.0f;
		float mainHeightScale = 0.260f;
		float detailHeightScale = 3.070f;
		float thresholdLowCoverage = 0.115f;
		float thresholdHighCoverage = 0.720f;
		float densityRemapLow = 0.425f;
		float densityRemapHigh = 0.915f;
		float mainErosionStrength = 1.160f;
		float detailErosionStrength = 1.340f;
		float edgeErosionStrength = 0.500f;
		float verticalShapePower = 1.420f;
		float detailErosionLow = 0.280f;
		float detailErosionHigh = 0.810f;
		float detailEdgeStrength = 0.270f;
		float opticalPadding = 0.0f;
		float sigmaTRef = 1.000f;
		float viewAbsorption = 1.000f;
		float lightAbsorption = 1.000f;
		float singleScatteringAlbedo = 0.999f;
		float forwardEccentricity = 0.700f;
		float backwardEccentricity = 0.250f;
		float msAttenuation = 0.500f;
		float msContribution = 0.500f;
		float msEccentricity = 0.500f;
		float scatteringTintR = 1.000f;
		float scatteringTintG = 1.000f;
		float scatteringTintB = 1.000f;
		float scatterSourceODScale = 0.120f;
		float scatterSourceCurvePow = 1.000f;
		float aoUpwardScale = 1.000f;
		float ambientBottomStrength = 0.100f;
		float ambientTopStrength = 0.350f;
		float ambientDuskWarmth = 0.650f;
		float boundaryConfidence = 0.750f;
		float boundaryWrap = 0.350f;
		float phiFwdIntensity = 0.800f;
		float phiFwdDepthPow = 1.000f;
		float phiFwdDepthBias = 0.050f;
		float phiFwdMSBuildScale = 1.000f;
		float phiFwdCompress = 1.000f;
		float phiFwdMaxDistance = 6000.000f;
		float phiFwdConeRatio = 1.450f;
		float phiFwdMinStep = 80.000f;
		float lightStepCount = 8.000f;
		float boundaryGradientStep = 250.000f;
		float boundaryGradientStrength = 0.000f;
		float shadingDebugMode = 0.000f;
		float padding = 0.0f;
	};
	static_assert(sizeof(VansVolumetricCloudMediumGPU) == 224,
		"Volumetric cloud medium ABI must match CloudParamsUBO");
	static_assert(offsetof(VansVolumetricCloudMediumGPU, density) == 12,
		"Atmosphere cloud-density view must match cloudRuntimeParameters[0].w");

	struct alignas(16) VansVolumetricCloudRuntimeParamsGPU final
	{
		VansVolumetricCloudMediumGPU medium;
		glm::vec4 shadowClipmapParameters{ 0.0f };
		glm::vec4 shadowRayMarchParameters{ 0.0f };
	};
	static_assert(sizeof(VansVolumetricCloudRuntimeParamsGPU) == 256,
		"Volumetric cloud runtime ABI must match the cloud compute shaders");
	static_assert(offsetof(VansVolumetricCloudRuntimeParamsGPU,
		shadowClipmapParameters) == 224,
		"Cloud shadow clipmap parameters must remain at cloudRuntimeParameters[14]");
}
