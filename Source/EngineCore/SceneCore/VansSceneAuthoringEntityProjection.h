#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "VansSceneLightComponentConfig.h"

#include <array>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace Vans
{
struct VansSceneAuthoringTransformProjection
{
	std::optional<std::array<float, 3>> position;
	std::optional<std::array<float, 3>> rotationDegrees;
	std::optional<std::array<float, 3>> scale;
};

struct VansSceneAuthoringComponentProjection
{
	std::string guid;
	std::string type;
	bool enabled = true;
	std::optional<VansSerializedValue> localVolumetricFogComponent;
};

using VansSceneAuthoringLightConfig = std::variant<
	VansSceneDirectionalLightComponentConfig,
	VansScenePointLightComponentConfig,
	VansSceneSpotLightComponentConfig,
	VansSceneRectLightComponentConfig>;

struct VansSceneAuthoringLightProjection
{
	std::string componentType;
	bool enabled = true;
	bool hasCookie = false;
	VansSceneAuthoringLightConfig config;
};

struct VansSceneAuthoringMaterialOverrideProjection
{
	std::string slot;
	std::string materialGuid;
};

struct VansSceneAuthoringEntityProjection
{
	std::string entityGuid;
	std::string name;
	bool active = true;
	std::optional<VansSceneAuthoringTransformProjection> transform;
	std::vector<VansSceneAuthoringComponentProjection> components;
	std::vector<VansSceneAuthoringLightProjection> lights;
	std::vector<VansSceneAuthoringMaterialOverrideProjection> materialOverrides;
};
}
