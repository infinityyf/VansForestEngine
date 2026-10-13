#pragma once

#include "EngineDTOs.h"
#include "../../TerrainCore/VansTerrainInteraction.h"

namespace Vans::EditorAPI
{
	class ITerrainEditorAPI
	{
	public:
		virtual ~ITerrainEditorAPI() = default;
		virtual TerrainSettingsSnapshot GetTerrainSettings() const = 0;
		virtual VansTerrainInteraction GetTerrainInteraction() const = 0;
		virtual TerrainEditorOperationResult ApplyTerrainInteraction(const VansTerrainInteraction& input) = 0;
		virtual TerrainEditorOperationResult ApplyTerrainSettings(
			const TerrainSettingsSnapshot& settings) = 0;
		virtual TerrainEditorSnapshot GetTerrainEditorSnapshot() const = 0;
		virtual TerrainEditorOperationResult ConfigureTerrainBrush(
			const TerrainBrushConfiguration& configuration) = 0;
		virtual TerrainBrushInputResult ApplyTerrainBrushInput(
			const TerrainBrushInput& input) = 0;
		virtual TerrainEditorOperationResult UndoTerrainEdit() = 0;
		virtual TerrainEditorOperationResult RedoTerrainEdit() = 0;
		virtual TerrainEditorOperationResult RevertTerrainEdits() = 0;
		virtual TerrainEditorOperationResult SaveTerrainAsset() = 0;
	};
}
