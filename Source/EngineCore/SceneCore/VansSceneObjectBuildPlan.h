#pragma once

#include "VansSceneAnimationComponentConfig.h"
#include "VansSceneAIComponentConfig.h"
#include "VansSceneCameraMediaComponentConfig.h"
#include "VansSceneLightComponentConfig.h"
#include "VansSceneLocalVolumetricFogComponentConfig.h"
#include "VansSceneParticleComponentConfig.h"
#include "VansSceneParentReference.h"
#include "VansScenePhysicsComponentConfig.h"
#include "VansSceneRenderNodeConfig.h"
#include "VansSceneTimelineComponentConfig.h"
#include "VansSceneVehicleComponentConfig.h"
#include "../GameplayActionCore/VansGameplayRuntime.h"
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
	std::optional<VansSceneParentReference> parent;
	bool active = true;
	std::optional<VansSceneTransformConfig> transform;
	std::optional<VansSceneRenderNodeConfig> render;
	bool renderEnabled = true;
	std::optional<VansSceneMultiMeshRootConfig> multiMeshRoot;
	VansScenePhysicsComponentsConfig physicsComponents;
	VansSceneVehicleObjectConfig vehicleObject;
	VansSceneLightComponentConfig lightComponents;
	VansSceneCameraMediaComponentConfig cameraMediaComponents;
	std::optional<VansSceneAudioReverbZoneConfig> audioReverbZone;
	std::optional<VansSceneLocalVolumetricFogComponentConfig> localVolumetricFog;
	std::optional<VansSceneAnimationComponentConfig> animation;
	std::optional<VansSceneParticleComponentConfig> particle;
	std::optional<VansSceneTimelineComponentConfig> timeline;
	std::optional<VansGameplayActionHostSetup> actionHost;
	std::optional<VansSceneNavigationAgentConfig> navigationAgent;
	std::optional<VansSceneAIAgentConfig> aiAgent;
	VansScriptComponentDescriptors scriptComponents;
	VansScriptUIComponentDescriptors uiComponents;
	std::unordered_map<std::string, std::string> componentGuids;

	std::string ResolveModelAssetGuid() const
	{
		if (multiMeshRoot && !multiMeshRoot->modelGuid.empty())
			return multiMeshRoot->modelGuid;
		return render ? render->mesh : std::string{};
	}
};

struct VansSceneObjectBuildPlan
{
	std::vector<VansSceneObjectBuildConfig> objects;
};
}
