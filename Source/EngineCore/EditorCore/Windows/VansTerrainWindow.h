#pragma once
#include "VansBaseWindowComponent.h"

#include <string>

namespace VansGraphics
{
    class VansTerrainWindow : public VansBaseWindowComponent
    {
    private:
		enum class BrushMode
		{
			None,
			Sculpt,
			Paint
		};

        void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;

		Vans::EditorAPI::TerrainSettingsSnapshot m_SettingsDraft;
		std::string m_SettingsAssetGuid;
		std::string m_BrushStateAssetGuid;
		std::string m_StatusMessage;
		BrushMode m_ActiveBrushMode = BrushMode::None;
		Vans::EditorAPI::TerrainBrushTool m_LastSculptTool =
			Vans::EditorAPI::TerrainBrushTool::Raise;
		Vans::EditorAPI::TerrainBrushTool m_LastPaintTool =
			Vans::EditorAPI::TerrainBrushTool::PaintLayer;
		bool m_HasSettingsDraft = false;
    };
}
