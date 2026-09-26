#pragma once

#include "EngineDTOs.h"

namespace Vans::EditorAPI
{
	class IVehicleEditorAPI
	{
	public:
		virtual ~IVehicleEditorAPI() = default;
		virtual VehicleDebugSnapshot GetVehicleDebugSnapshot() const = 0;
	};
}
