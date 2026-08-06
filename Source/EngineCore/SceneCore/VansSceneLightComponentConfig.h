#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace Vans
{
struct VansSceneLightShadowConfig
{
	std::optional<bool> castShadows;
	std::optional<std::string> policy;
	std::optional<int> priority;
	std::optional<std::string> resolution;
	std::optional<std::string> updateMode;
	std::optional<std::string> fallback;
	std::optional<float> maxShadowDistance;
	std::optional<float> nearPlaneOverride;
	std::optional<float> depthBiasTexels;
	std::optional<float> normalBiasTexels;
	std::optional<float> sourceRadius;
	std::optional<bool> affectsVolumetricFog;
	std::optional<bool> affectsGI;
	std::optional<uint32_t> shadowCasterMask;
};

struct VansSceneDirectionalLightComponentConfig
{
	std::optional<std::array<float, 3>> color;
	std::optional<float> intensity;
};

struct VansScenePointLightComponentConfig
{
	std::optional<std::array<float, 3>> color;
	std::optional<float> intensity;
	std::optional<float> radius;
	std::optional<std::string> iesProfile;
	VansSceneLightShadowConfig shadow;
};

struct VansSceneSpotLightComponentConfig
{
	std::optional<std::array<float, 3>> color;
	std::optional<float> intensity;
	std::optional<float> radius;
	std::optional<float> innerCutoffDegrees;
	std::optional<float> outerCutoffDegrees;
	std::optional<std::string> iesProfile;
	std::optional<float> iesIntensityScale;
	VansSceneLightShadowConfig shadow;
};

struct VansSceneRectLightComponentConfig
{
	std::optional<std::array<float, 3>> color;
	std::optional<float> intensity;
	std::optional<float> width;
	std::optional<float> height;
	std::optional<float> range;
	std::optional<bool> twoSided;
	std::optional<float> attenuationExp;
	std::optional<float> textureLodBias;
	std::optional<std::string> emissiveTexture;
	std::optional<std::string> emissiveVideo;
	VansSceneLightShadowConfig shadow;
};

struct VansSceneLightComponentConfig
{
	std::optional<VansSceneDirectionalLightComponentConfig> directionalLight;
	std::optional<VansScenePointLightComponentConfig> pointLight;
	std::optional<VansSceneSpotLightComponentConfig> spotLight;
	std::optional<VansSceneRectLightComponentConfig> rectLight;
};
}
