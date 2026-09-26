#include "VansEditorHistoryAdapter.h"

#include "VansEditorRuntimePreviewProjector.h"
#include "VansSceneEditService.h"
#include "../AuthoringCore/VansAssetDocumentEditService.h"
#include "../AuthoringCore/VansAssetDocumentRegistry.h"
#include "../EngineAPILayer/Public/IPcgEditorAPI.h"
#include "../EngineAPILayer/Public/IRuntimeCommandHistoryEditorAPI.h"
#include "../EngineAPILayer/Public/ISceneInteractionEditorAPI.h"
#include "../EngineAPILayer/Public/ITerrainEditorAPI.h"
#include "../SceneCore/VansSceneDocument.h"
#include "../Util/VansLog.h"

#include <utility>

namespace VansGraphics
{
	Vans::VansEditorHistoryService VansEditorHistoryAdapter::Compose(
		Vans::EditorAPI::IRuntimeCommandHistoryEditorAPI& runtimeHistoryAPI,
		Vans::EditorAPI::IPcgEditorAPI& pcgAPI,
		Vans::EditorAPI::ISceneInteractionEditorAPI& sceneInteractionAPI,
		Vans::EditorAPI::ITerrainEditorAPI& terrainAPI,
		Vans::VansSceneDocument* sceneDocument,
		Vans::VansSceneEditService* sceneEdits,
		std::shared_ptr<Vans::VansOpenAssetDocument> selectedAssetDocument,
		std::function<void()> reloadCurrentSceneForEditing)
	{
		const Vans::VansAuthoringHistorySnapshot sceneHistory = sceneEdits
			? sceneEdits->HistorySnapshot()
			: Vans::VansAuthoringHistorySnapshot{};
		const Vans::VansAuthoringHistorySnapshot assetHistory = selectedAssetDocument
			? Vans::VansAssetDocumentEditService::HistorySnapshot(
				selectedAssetDocument->sourceDocument)
			: Vans::VansAuthoringHistorySnapshot{};
		const Vans::EditorAPI::EditorCommandHistorySnapshot runtimeHistory =
			runtimeHistoryAPI.GetRuntimeCommandHistory();
		const Vans::EditorAPI::PcgSplineSnapshot splineEditor =
			pcgAPI.GetPcgSplineSnapshot();
		const bool splineEditingActive = splineEditor.editable && splineEditor.toolEnabled;
		const Vans::EditorAPI::TerrainEditorSnapshot terrainEditor =
			terrainAPI.GetTerrainEditorSnapshot();
		const bool terrainEditingActive = terrainEditor.available && terrainEditor.editable &&
			terrainEditor.brushEnabled;

		auto applySelectedAssetRuntimePatch = [selectedAssetDocument, api = &sceneInteractionAPI]()
		{
			if (!selectedAssetDocument || !selectedAssetDocument->sourceDocument.IsLoaded())
				return;
			api->ApplyRuntimeMaterialPreviewChange(
				Vans::BuildRuntimeMaterialPreviewChange(
					selectedAssetDocument->sourcePath,
					selectedAssetDocument->sourceDocument.SerializedRootSnapshot()));
		};
		auto applySceneRuntimePatchOrReload = [sceneDocument, api = &sceneInteractionAPI,
			reload = std::move(reloadCurrentSceneForEditing)](const Vans::SceneEditResult& result)
		{
			if (!result || result.runtimeChangeApplied)
				return;
			if (result.runtimeParentPreviewSupported)
			{
				Vans::EditorAPI::RuntimeEntityPreviewChange previewChange = sceneDocument
					? Vans::BuildRuntimeEntityPreviewChangeFromSceneRoot(
						sceneDocument->SerializedRootSnapshot(), result.changedEntityGuid)
					: Vans::EditorAPI::RuntimeEntityPreviewChange{};
				previewChange.parentEdits.push_back({
					result.changedEntityGuid,
					result.changedParent,
					Vans::EditorAPI::RuntimeReparentTransformPolicy::KeepLocal });
				if (api->ApplyRuntimeEntityPreviewChange(previewChange))
					return;
			}
			if (result.runtimePreviewSupported && sceneDocument)
			{
				const Vans::EditorAPI::RuntimeEntityPreviewChange previewChange =
					Vans::BuildRuntimeEntityPreviewChangeFromSceneRoot(
						sceneDocument->SerializedRootSnapshot(), result.changedEntityGuid);
				if (!previewChange.Empty() &&
					api->ApplyRuntimeEntityPreviewChange(previewChange))
				{
					return;
				}
			}
			if (reload)
				reload();
		};

		Vans::VansEditorHistoryService history;
		history.AddSource(Vans::VansEditorHistorySource::Runtime,
			{ runtimeHistory.undoSequence, runtimeHistory.redoSequence },
			[api = &runtimeHistoryAPI]() { api->Undo(); return true; },
			[api = &runtimeHistoryAPI]() { api->Redo(); return true; });
		history.AddSource(Vans::VansEditorHistorySource::Scene, sceneHistory,
			[sceneEdits, applySceneRuntimePatchOrReload]()
			{
				if (!sceneEdits) return false;
				const auto result = sceneEdits->Undo();
				applySceneRuntimePatchOrReload(result);
				return static_cast<bool>(result);
			},
			[sceneEdits, applySceneRuntimePatchOrReload]()
			{
				if (!sceneEdits) return false;
				const auto result = sceneEdits->Redo();
				applySceneRuntimePatchOrReload(result);
				return static_cast<bool>(result);
			});
		history.AddSource(Vans::VansEditorHistorySource::Asset, assetHistory,
			[selectedAssetDocument, applySelectedAssetRuntimePatch]()
			{
				if (!selectedAssetDocument) return false;
				const auto result = Vans::VansAssetDocumentEditService::Undo(
					selectedAssetDocument->sourceDocument);
				if (result) applySelectedAssetRuntimePatch();
				else VANS_LOG_ERROR("[AssetEdit] " << result.message);
				return static_cast<bool>(result);
			},
			[selectedAssetDocument, applySelectedAssetRuntimePatch]()
			{
				if (!selectedAssetDocument) return false;
				const auto result = Vans::VansAssetDocumentEditService::Redo(
					selectedAssetDocument->sourceDocument);
				if (result) applySelectedAssetRuntimePatch();
				else VANS_LOG_ERROR("[AssetEdit] " << result.message);
				return static_cast<bool>(result);
			});
		if (terrainEditingActive)
		{
			history.AddSource(Vans::VansEditorHistorySource::Terrain,
				{ terrainEditor.undoSequence, terrainEditor.redoSequence },
				[api = &terrainAPI]()
				{
					const auto result = api->UndoTerrainEdit();
					if (!result.success) VANS_LOG_ERROR("[TerrainEdit] " << result.message);
					return result.success;
				},
				[api = &terrainAPI]()
				{
					const auto result = api->RedoTerrainEdit();
					if (!result.success) VANS_LOG_ERROR("[TerrainEdit] " << result.message);
					return result.success;
				});
		}
		if (splineEditingActive)
		{
			history.AddSource(Vans::VansEditorHistorySource::PcgSpline,
				{ splineEditor.undoSequence, splineEditor.redoSequence },
				[api = &pcgAPI]()
				{
					Vans::EditorAPI::PcgSplineCommandRequest request;
					request.command = Vans::EditorAPI::PcgSplineCommand::Undo;
					return api->ExecutePcgSplineCommand(request).success;
				},
				[api = &pcgAPI]()
				{
					Vans::EditorAPI::PcgSplineCommandRequest request;
					request.command = Vans::EditorAPI::PcgSplineCommand::Redo;
					return api->ExecutePcgSplineCommand(request).success;
				});
		}
		return history;
	}
}
