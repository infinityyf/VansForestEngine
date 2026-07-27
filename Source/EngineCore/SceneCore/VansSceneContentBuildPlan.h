#pragma once

#include "VansSceneEnvironmentNodeConfig.h"
#include "VansSceneMaterialConfig.h"
#include "VansSceneObjectBuildPlan.h"
#include "VansSceneReflectionProbeConfig.h"
#include "VansSceneRenderNodeConfig.h"
#include "VansSceneRenderSettingsConfig.h"

#include <optional>
#include <string>

namespace Vans
{
struct VansSceneContentBuildPlan
{
	bool valid = false;
	std::string error;
	VansSceneRenderSettingsConfig renderSettings;
	VansSceneReflectionProbeConfig reflectionProbes;
	VansSceneMaterialConfigs materials;
	VansSceneObjectBuildPlan objects;
	VansSceneRenderNodeConfigs renderNodes;
	std::optional<VansSceneTerrainNodeConfig> terrain;
	std::optional<VansSceneVegetationNodeConfig> vegetation;
	std::optional<VansSceneWaterNodeConfig> water;
};
}
