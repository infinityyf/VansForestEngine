#pragma once

#include "EngineDTOs.h"

#include <string>
#include <vector>

namespace Vans::EditorAPI
{
	class ISceneInteractionEditorAPI
	{
	public:
		virtual ~ISceneInteractionEditorAPI() = default;
		virtual EditorViewportCameraState CaptureEditorViewportCamera() const = 0;
		virtual void RestoreEditorViewportCamera(const EditorViewportCameraState& state) = 0;
		virtual bool ApplyRuntimeEntityPreviewChange(const RuntimeEntityPreviewChange& change) = 0;
		virtual bool ApplyRuntimeMaterialPreviewChange(const RuntimeMaterialPreviewChange& change) = 0;
		virtual EditorScenePickResult PickEditorScene(const EditorScenePickRequest& request) = 0;
		virtual EditorSceneBounds QueryEditorSceneBounds(
			const std::vector<std::string>& entityGuids) = 0;
		virtual RuntimeTransformSnapshot GetRuntimeTransform(
			const std::string& entityGuid, RuntimeTransformSpace space) const = 0;
		virtual std::vector<RuntimeMultiMeshGroupSnapshot> BuildRuntimeMultiMeshExpansionSnapshot() = 0;
		virtual std::vector<std::string> GetRuntimeCollisionLayerNames() const = 0;
	};
}
