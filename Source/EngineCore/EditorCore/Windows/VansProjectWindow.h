#pragma once
#include "VansBaseWindowComponent.h"

#include <filesystem>
#include <string>

namespace VansGraphics
{
	class VansProjectWindow : public VansBaseWindowComponent
	{
	public:
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;
		void RequestAssetCreation(Vans::EditorAPI::ProjectAssetCreationKind kind);

	private:
		void DrawProjectContents(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void ProcessAssetCreation(Vans::EditorAPI::IEngineEditorAPI& editorAPI,
			const Vans::EditorAPI::ProjectBrowserRootSnapshot& root);
		void DrawTimelineCreationPopup(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		std::filesystem::path ResolveAssetCreationDirectory(
			const Vans::EditorAPI::ProjectBrowserRootSnapshot& root) const;

		std::filesystem::path m_CurrentDirectory;
		std::string m_CachedRootPath;
		Vans::EditorAPI::ProjectAssetCreationKind m_PendingAssetCreation =
			Vans::EditorAPI::ProjectAssetCreationKind::Timeline;
		bool m_HasPendingAssetCreation = false;
		std::filesystem::path m_TimelineCreationDirectory;
		std::string m_TimelineAssetCreateStatus;
		char m_TimelineName[128] = "NewTimeline";
		bool m_OpenTimelineCreationPopup = false;
	};
}
