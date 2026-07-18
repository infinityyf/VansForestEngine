#pragma once

#include "../VansScene.h"

#include <unordered_set>

namespace VansGraphics
{
	class VansSceneVehicleComponentBuilder
	{
	public:
		static void AddVehiclePlaceholder(VansScriptObject& object, const json& components);

		static std::unordered_set<uint32_t> ResolveVehicles(
			VansScene& scene,
			const json& objectsArray);
	};
}
