#pragma once

#include "VansBaseWindowComponent.h"

#include <cstddef>
#include <string>
#include <vector>

namespace VansGraphics
{
	class VansProjectSettingsWindow final : public VansBaseWindowComponent
	{
	public:
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;

	private:
		void ReloadGAF(Vans::EditorAPI::IEngineEditorAPI& editorAPI, const std::string& projectRoot);
		void SyncTemplateBuffer();
		void SelectTemplate(std::size_t index);
		void DrawGAFSettings(Vans::EditorAPI::IEngineEditorAPI& editorAPI);

		std::string m_GAFProjectRoot;
		Vans::EditorAPI::GAFProjectConfigurationSnapshot m_GAFConfiguration;
		Vans::EditorAPI::GAFProjectConfigurationResult m_GAFResult;
		std::size_t m_GAFTemplateIndex = 0;
		std::vector<char> m_GAFTemplateBuffer;
	};
}
