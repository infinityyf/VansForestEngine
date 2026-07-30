#include "VansScenePickingService.h"

#include "VansEditorSelectionService.h"
#include "../EngineAPILayer/Public/IEngineEditorAPI.h"

namespace Vans
{
ScenePickResult VansScenePickingService::PickRuntimeEntity(
	EditorAPI::IEngineEditorAPI& editorAPI,
	const EditorAPI::Ray& ray,
	const std::string& source)
{
	ScenePickResult result;
	result.entityGuid = editorAPI.PickRuntimeEntity(ray);
	result.hit = !result.entityGuid.empty();
	if (result.hit)
		VansEditorSelectionService::Get().SelectEntity(result.entityGuid, source);
	return result;
}
}
