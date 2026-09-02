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
        return { false, payload.message, {} };

    std::vector<Vans::VansSerializedValue> entities;
    entities.reserve(payload.sceneEntities.size());
    for (const Vans::EditorAPI::ScenePropertyValue& entityValue : payload.sceneEntities)
    {
        Vans::VansSerializedValue entity = Vans::ToSerializedValue(entityValue);
        if (entity.kind != Vans::VansSerializedValue::Kind::Object)
            return { false, "Prepared model placement payload contains invalid scene entity", {} };
        entities.push_back(std::move(entity));
    }

    const bool expectsRuntimeAppend =
        !payload.runtimeEntityGuid.empty() && !payload.sceneEntities.empty();
    Vans::SceneEditLifecycleHooks hooks;
    if (expectsRuntimeAppend)
    {
        std::vector<Vans::EditorAPI::ScenePropertyValue> runtimeSceneEntities =
            payload.sceneEntities;
        auto createRuntimeEntities =
            [&editorAPI, runtimeSceneEntities]()
            {
                Vans::EditorAPI::RuntimeSceneEntitiesCreateRequest createRequest;
                createRequest.sceneEntities = runtimeSceneEntities;
                return editorAPI.CreateRuntimeSceneEntities(createRequest).created;
            };
        hooks.afterExecute = createRuntimeEntities;
        hooks.afterUndo = [&editorAPI, runtimeEntityGuid = payload.runtimeEntityGuid]()
        {
            Vans::EditorAPI::RuntimeEntityDestroyRequest destroyRequest;
            destroyRequest.entityGuid = runtimeEntityGuid;
            return editorAPI.DestroyRuntimeEntity(destroyRequest).destroyed;
        };
        hooks.afterRedo = createRuntimeEntities;
    }

    const Vans::SceneEditResult result =
        editService.AppendEntities(std::move(entities), std::move(hooks));
    if (!result)
        return { false, result.message, {} };
    if (expectsRuntimeAppend && !result.runtimeChangeApplied)
        return { false, "Runtime model entity creation failed", {} };
    return { true, {}, payload.runtimeEntityGuid };
}
}
