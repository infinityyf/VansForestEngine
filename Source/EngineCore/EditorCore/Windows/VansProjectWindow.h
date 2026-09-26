#pragma once
#include "VansBaseWindowComponent.h"

#include <filesystem>
#include <string>

namespace Vans::EditorAPI
{
	class IAssetAuthoringEditorAPI;
	class IAssetEditorAPI;
}

namespace VansGraphics
{
	class VansProjectWindow : public VansBaseWindowComponent
	{
	public:
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;
		void RequestAssetCreation(Vans::EditorAPI::ProjectAssetCreationKind kind);

	private:
		void DrawProjectContents(Vans::EditorAPI::IAssetAuthoringEditorAPI& assetAuthoringAPI,
			Vans::EditorAPI::IAssetEditorAPI& assetAPI);
		void ProcessAssetCreation(Vans::EditorAPI::IAssetAuthoringEditorAPI& assetAuthoringAPI,
			const Vans::EditorAPI::ProjectBrowserRootSnapshot& root);
		void DrawTimelineCreationPopup(Vans::EditorAPI::IAssetAuthoringEditorAPI& assetAuthoringAPI);
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
