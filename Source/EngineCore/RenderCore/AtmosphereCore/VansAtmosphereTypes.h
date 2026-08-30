#pragma once

#include "../VansRenderRuntimeConfig.h"
#include "../../SceneCore/VansSceneRenderSettingsConfig.h"

#include <cstdint>
#include <glm/glm.hpp>

namespace VansGraphics
{
	inline constexpr std::uint32_t VansMaxCelestialBodies = 2;

	struct alignas(16) VansAtmosphereStaticParamsGPU
	{
		glm::vec4 planetRadiiMeters;
		glm::vec4 rayleighScatteringAndScaleHeight;
		glm::vec4 mieScatteringAndScaleHeight;
		glm::vec4 mieAbsorptionAndAnisotropy;
		glm::vec4 ozoneAbsorptionAndCenterAltitude;
		glm::vec4 ozoneHalfWidthAndGroundAlbedo;
		// x: AP distance scale, y: 物理大气主光直射入散射强度。
		glm::vec4 aerialPerspectiveAndVolumetricLighting;
		// x: ground height, y: 1 / visibility, z: density falloff height.
		glm::vec4 heightFogDensityParameters;
		// x: start, y: near fade, z: maximum distance, w: far fade.
		glm::vec4 heightFogDistanceParameters;
		// xyz: single-scattering albedo, w: anisotropy.
		glm::vec4 heightFogAlbedoAndAnisotropy;
		// xyz: emissive per meter, w: sky lighting scale.
		glm::vec4 heightFogEmissiveAndSkyScale;
		// x: main-light scale, y: receive cloud shadow.
		glm::vec4 heightFogLightingParameters;
		glm::uvec4 featureFlags;
		glm::uvec4 lutSampleCounts;
		glm::uvec4 lutDimensions;
	};

	struct alignas(16) VansCelestialBodyGPU
	{
		glm::vec4 directionAndValidity;
		glm::vec4 topOfAtmosphereIrradiance;
		glm::vec4 diskParameters;
	};

	struct alignas(16) VansAtmosphereFrameParamsGPU
	{
		glm::vec4 planetCenterRelativeToCameraMeters;
		glm::vec4 cameraWorldMetersAndMaxDistance;
		glm::vec4 aerialPerspectiveParameters;
		glm::uvec4 viewParameters;
		// x: 当前帧是否存在水面。其余分量保留给大气前表面分类。
		glm::uvec4 surfaceCompositionFlags;
		glm::vec4 preparedMainLightDirectionAndValidity;
		glm::vec4 preparedMainLightColorAndIntensity;
		VansCelestialBodyGPU celestialBodies[VansMaxCelestialBodies];
	};

	struct VansAtmosphereMediumSample
	{
		glm::dvec3 scatteringPerMeter{ 0.0 };
		glm::dvec3 extinctionPerMeter{ 0.0 };
	};

	struct VansAtmosphereInterval
	{
		glm::dvec3 scattering{ 0.0 };
		glm::dvec3 opticalDepth{ 0.0 };
	};
}
