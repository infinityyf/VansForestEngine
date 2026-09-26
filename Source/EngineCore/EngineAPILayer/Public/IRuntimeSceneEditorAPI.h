#pragma once

#include "EngineDTOs.h"

namespace Vans::EditorAPI
{
	class IRuntimeSceneEditorAPI
	{
	public:
		virtual ~IRuntimeSceneEditorAPI() = default;
		virtual RuntimeSceneEntitiesCreateResult CreateRuntimeSceneEntities(
			const RuntimeSceneEntitiesCreateRequest& request) = 0;
		virtual ModelAssetPlacementPayload PrepareModelAssetPlacement(
			const ModelAssetPlacementRequest& request) = 0;
		virtual RuntimeEntityDestroyResult DestroyRuntimeEntity(
			const RuntimeEntityDestroyRequest& request) = 0;
		virtual RuntimeEntityReparentResult ReparentRuntimeEntity(
			const RuntimeEntityReparentRequest& request) = 0;
		virtual bool IsRuntimeSceneReady() const = 0;
		virtual bool IsRuntimeSceneSwitching() const = 0;
		virtual RuntimeSceneLoadResult LoadRuntimeScene(
			const RuntimeSceneLoadRequest& request) = 0;
		virtual void UnloadRuntimeScene() = 0;
		virtual void UnloadRuntimeProjectResources() = 0;
	};
}
