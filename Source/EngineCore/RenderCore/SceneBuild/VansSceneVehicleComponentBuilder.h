#pragma once

#include "../VansScene.h"

#include "../../SceneCore/VansSceneVehicleComponentConfig.h"
#include <unordered_set>

class VansScriptVehicleComponent;

namespace VansGraphics
{
	class VansSceneVehicleComponentBuilder
	{
	public:
		static VansScriptVehicleComponent* AddVehiclePlaceholder(
			VansScriptObject& object,
			const Vans::VansSceneVehicleObjectConfig& objectConfig);

		static std::unordered_set<uint32_t> ResolveVehicles(
			VansScene& scene,
			const Vans::VansSceneVehicleObjectConfigs& objectConfigs);
	};
}
