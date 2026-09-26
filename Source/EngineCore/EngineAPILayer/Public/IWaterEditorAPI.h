#pragma once

#include "EngineDTOs.h"

namespace Vans::EditorAPI
{
	class IWaterEditorAPI
	{
	public:
		virtual ~IWaterEditorAPI() = default;
		virtual WaterSettingsSnapshot GetWaterSettings() const = 0;
		virtual void ApplyWaterSettings(const WaterSettingsSnapshot& settings) = 0;
		virtual WaterRuntimeStats GetWaterRuntimeStats() const = 0;
	};
}
