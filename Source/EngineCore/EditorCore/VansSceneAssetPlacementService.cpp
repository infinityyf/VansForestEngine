#include "VansSceneAssetPlacementService.h"

#include "VansSceneEditService.h"
#include "VansScenePropertyValueAdapter.h"

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

    std::vector<Vans::VansSerializedValue> entities;
    entities.reserve(payload.sceneEntities.size());
    for (const Vans::EditorAPI::ScenePropertyValue& entityValue : payload.sceneEntities)
    {
        Vans::VansSerializedValue entity = Vans::ToSerializedValue(entityValue);
        if (entity.kind != Vans::VansSerializedValue::Kind::Object)
            return { false, "Prepared model placement payload contains invalid scene entity" };
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
