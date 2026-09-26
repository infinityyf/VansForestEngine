#pragma once

#include "VansBaseWindowComponent.h"
#include "../../EngineAPILayer/Public/EngineDTOs.h"
#include <string>
#include <vector>

namespace Vans::EditorAPI
{
	class ISceneSettingsEditorAPI;
}

namespace VansGraphics
{
	class VansLightWindow : public VansBaseWindowComponent
	{
	private:
		bool DrawDirectionalLights(std::vector<Vans::EditorAPI::DirectionalLightSettings>& directionLights);
		bool DrawPointLights(std::vector<Vans::EditorAPI::PointLightSettings>& pointLights);
		bool DrawSpotLights(std::vector<Vans::EditorAPI::SpotLightSettings>& spotLights);
		bool DrawRectLights(std::vector<Vans::EditorAPI::RectLightSettings>& rectLights);

		void DrawPhysicalAtmosphereParameters(Vans::EditorAPI::ISceneSettingsEditorAPI& editorAPI);
		void DrawHeightFogParameters(Vans::EditorAPI::ISceneSettingsEditorAPI& editorAPI);
		void DrawCloudParameters(Vans::EditorAPI::ISceneSettingsEditorAPI& editorAPI);
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;
	};
}
