#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <string>

namespace Vans
{
struct VansSkinProfile
{
	static constexpr int32_t PROFILE_VERSION = 1;

	std::string name = "SkinProfile";
	std::string description;
	std::string basePreset = "custom";

	glm::vec3 scatterColor = glm::vec3(1.0f, 0.34f, 0.22f);
	float scatterAmount = 0.65f;
	float roughness = 0.62f;
	float normalStrength = 0.35f;
	float specularScale = 1.0f;
	float transmissionScale = 1.0f;
	float primaryRoughnessScale = 0.75f;
	float secondaryRoughnessScale = 1.75f;
	float skinIor = 1.4f;
	float specularLobeMix = 0.72f;
	float diffusionRadiusScale = 1.0f;
	float thinnessScale = 1.0f;
	float transmissionDepthScale = 1.0f;
	float ambientScatterScale = 0.35f;
	glm::vec3 scatterRadiusScale = glm::vec3(1.0f);
	float boundaryColorBleed = 1.0f;
	int32_t profileLutLayer = -1;

	void ResetToDefaults();
};
}
