#pragma once

#include "../VansScene.h"

#include "../../SceneCore/VansSceneVehicleComponentConfig.h"
#include <string>
#include <unordered_set>
#include <vector>

class VansScriptVehicleComponent;

namespace VansGraphics
{
	struct VansSceneVehicleBuildRequest
	{
		std::string ownerEntityGuid;
		Vans::VansSceneVehicleComponentConfig config;
		std::string componentGuid;
	};

	struct VansSceneBuiltVehicleRuntime
	{
		std::string ownerEntityGuid;
		VansScriptVehicleComponent* component = nullptr;
	};

	struct VansSceneVehicleBuildResult
	{
		bool success = false;
		std::string error;
		std::unordered_set<uint32_t> drivenTransformIds;
		std::vector<VansSceneBuiltVehicleRuntime> builtVehicles;
	};

	class VansSceneVehicleComponentBuilder
	{
	public:
		static VansSceneVehicleBuildResult BuildVehicles(
			VansScene& scene,
			const std::vector<VansSceneVehicleBuildRequest>& requests);
	};
}
