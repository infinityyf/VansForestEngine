#include "VansSceneAssetPlacementService.h"

#include "VansSceneEditService.h"
#include "../SceneCore/VansSceneDocument.h"

#include <utility>
#include <vector>

namespace VansGraphics
{
VansSceneAssetPlacementService::Result VansSceneAssetPlacementService::PlaceModelAsset(
    Vans::EditorAPI::IEngineEditorAPI& editorAPI,
    Vans::VansSceneEditService& editService,
    const std::string& assetGuid,
    const Vans::EditorAPI::Vec3& worldPosition)
{
    Vans::EditorAPI::ModelAssetPlacementRequest request;
    request.assetGuid = assetGuid;
    request.worldPosition = worldPosition;

    const Vans::EditorAPI::ModelAssetPlacementPayload payload =
        editorAPI.PrepareModelAssetPlacement(request);
    if (!payload.prepared)
        return { false, payload.message };

    std::vector<Vans::SceneJson> entities;
    entities.reserve(payload.sceneEntityJsons.size());
    for (const std::string& entityJson : payload.sceneEntityJsons)
    {
        Vans::SceneJson entity = Vans::SceneJson::parse(entityJson, nullptr, false);
        if (entity.is_discarded() || !entity.is_object())
            return { false, "Prepared model placement payload contains invalid scene entity JSON" };
        entities.push_back(std::move(entity));
    }

    Vans::SceneEditLifecycleHooks hooks;
    if (!payload.runtimeEntityGuid.empty())
    {
        hooks.afterUndo = [&editorAPI, runtimeEntityGuid = payload.runtimeEntityGuid]()
        {
            Vans::EditorAPI::RuntimeEntityDestroyRequest destroyRequest;
            destroyRequest.entityGuid = runtimeEntityGuid;
            editorAPI.DestroyRuntimeEntityByName(destroyRequest);
        };
    }

    const Vans::SceneEditResult result =
        editService.AppendEntities(std::move(entities), std::move(hooks));
    return result ? Result{ true, {} } : Result{ false, result.message };
}
}
