#pragma once

#include "VansBaseWindowComponent.h"
#include "../../EngineAPILayer/Public/EngineDTOs.h"
#include <string>
#include <vector>
namespace VansGraphics
{
	class VansLightWindow : public VansBaseWindowComponent
	{
	private:
		bool DrawDirectionalLights(std::vector<Vans::EditorAPI::DirectionalLightSettings>& directionLights);
		bool DrawPointLights(std::vector<Vans::EditorAPI::PointLightSettings>& pointLights);
		bool DrawSpotLights(std::vector<Vans::EditorAPI::SpotLightSettings>& spotLights);
		bool DrawRectLights(std::vector<Vans::EditorAPI::RectLightSettings>& rectLights);

		void DrawPhysicalAtmosphereParameters(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void DrawHeightFogParameters(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void DrawCloudParameters(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;
	};
}
