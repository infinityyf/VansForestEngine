#pragma once

#include "VansBaseWindowComponent.h"

#include <cstddef>
#include <string>
#include <vector>
#include <array>

namespace Vans::EditorAPI { class IGAFEditorAPI; }

namespace VansGraphics
{
	class VansProjectSettingsWindow final : public VansBaseWindowComponent
	{
	public:
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;

	private:
		void ReloadGAF(Vans::EditorAPI::IGAFEditorAPI& editorAPI, const std::string& projectRoot);
		void SyncTemplateBuffer();
		void SelectTemplate(std::size_t index);
		void DrawGAFSettings(Vans::EditorAPI::IGAFEditorAPI& editorAPI);
		void DrawNavigationSettings(Vans::EditorAPI::INavigationEditorAPI& editorAPI);
		std::string m_NavigationProjectRoot;
		Vans::VansSerializedValue m_NavigationOriginal, m_NavigationDraft;
		Vans::EditorAPI::NavigationComponentValidation m_NavigationValidation;
		std::string m_NavigationApplyMessage;
		std::string m_NavigationPreviewMap,m_NavigationPreviewMessage;
        std::array<char,512> m_NavigationBakeOutput{};
        std::uint64_t m_NavigationBakePreviewId=0;
		bool m_NavigationInitialized=false;

		std::string m_GAFProjectRoot;
		Vans::EditorAPI::GAFProjectConfigurationSnapshot m_GAFConfiguration;
		Vans::EditorAPI::GAFProjectConfigurationResult m_GAFResult;
		std::size_t m_GAFTemplateIndex = 0;
		std::vector<char> m_GAFTemplateBuffer;
		bool m_UpscalerEditInitialized = false;
		bool m_UpscalerEditDirty = false;
		Vans::EditorAPI::UpscalerSettingsSnapshot m_UpscalerEdit;
		std::string m_UpscalerApplyMessage;
		bool m_UpscalerApplySucceeded = false;
		std::string m_CommandRecordingApplyMessage;
		bool m_CommandRecordingApplySucceeded = false;
        std::string m_ProjectPassProjectRoot;
        std::vector<std::string> m_ProjectPassGuids;
        std::string m_ProjectPassApplyMessage;
        bool m_ProjectPassEditInitialized=false,m_ProjectPassApplySucceeded=false;
	};
}
