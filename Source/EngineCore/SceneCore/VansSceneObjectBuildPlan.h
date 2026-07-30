#pragma once

#include "VansSceneAnimationComponentConfig.h"
#include "VansSceneCameraMediaComponentConfig.h"
#include "VansSceneLightComponentConfig.h"
#include "VansSceneParticleComponentConfig.h"
#include "VansScenePhysicsComponentConfig.h"
#include "VansSceneRenderNodeConfig.h"
#include "VansSceneVehicleComponentConfig.h"
#include "../ScriptCore/VansScriptTypes.h"

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
struct VansSceneMultiMeshRootConfig
{
	std::string modelGuid;
};

struct VansSceneObjectBuildConfig
{
	std::string entityGuid;
	std::string name;
	std::string parentEntityGuid;
	std::optional<VansSceneTransformConfig> transform;
	std::optional<VansSceneRenderNodeConfig> render;
	bool renderEnabled = true;
	std::optional<VansSceneMultiMeshRootConfig> multiMeshRoot;
	VansScenePhysicsComponentsConfig physicsComponents;
	VansSceneVehicleObjectConfig vehicleObject;
	VansSceneLightComponentConfig lightComponents;
	VansSceneCameraMediaComponentConfig cameraMediaComponents;
	std::optional<VansSceneAnimationComponentConfig> animation;
	std::optional<VansSceneParticleComponentConfig> particle;
	VansScriptComponentDescriptors scriptComponents;
	VansScriptUIComponentDescriptors uiComponents;
	std::unordered_map<std::string, std::string> componentGuids;
};

struct VansSceneObjectBuildPlan
{
	std::vector<VansSceneObjectBuildConfig> objects;
};
}
