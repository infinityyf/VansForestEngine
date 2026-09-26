#pragma once

#include "VansEditorHistoryService.h"

#include <functional>
#include <memory>

namespace Vans
{
	struct VansOpenAssetDocument;
	class VansSceneDocument;
	class VansSceneEditService;
}

namespace Vans::EditorAPI
{
	class IPcgEditorAPI;
	class IRuntimeCommandHistoryEditorAPI;
	class ISceneInteractionEditorAPI;
	class ITerrainEditorAPI;
}

namespace VansGraphics
{
	class VansEditorHistoryAdapter final
	{
	public:
		static Vans::VansEditorHistoryService Compose(
			Vans::EditorAPI::IRuntimeCommandHistoryEditorAPI& runtimeHistoryAPI,
			Vans::EditorAPI::IPcgEditorAPI& pcgAPI,
			Vans::EditorAPI::ISceneInteractionEditorAPI& sceneInteractionAPI,
			Vans::EditorAPI::ITerrainEditorAPI& terrainAPI,
			Vans::VansSceneDocument* sceneDocument,
			Vans::VansSceneEditService* sceneEdits,
			std::shared_ptr<Vans::VansOpenAssetDocument> selectedAssetDocument,
			std::function<void()> reloadCurrentSceneForEditing);
	};
}
