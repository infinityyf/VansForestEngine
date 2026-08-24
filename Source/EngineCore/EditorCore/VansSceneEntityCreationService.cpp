#include "VansSceneEntityCreationService.h"

#include "VansSceneEditService.h"
#include "VansScenePropertyValueAdapter.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../SceneCore/VansSceneDocument.h"
#include "../SceneCore/VansSceneEntityFactory.h"

#include <cstdint>
#include <unordered_set>
#include <utility>
#include <vector>

namespace VansGraphics
{
namespace
{
const Vans::VansSerializedValue* FindSceneEntities(const Vans::VansSerializedValue& root)
{
    const Vans::VansSerializedValue* entities = Vans::FindObjectField(root, "entities");
    return entities && entities->kind == Vans::VansSerializedValue::Kind::Array
        ? entities
        : nullptr;
}

bool ContainsEntityGuid(
    const Vans::VansSerializedValue& entities,
    const std::string& entityGuid)
{
    if (entityGuid.empty())
        return true;
    for (const Vans::VansSerializedValue& entity : entities.arrayItems)
    {
        if (Vans::ReadSerializedStringField(entity, "id") == entityGuid)
            return true;
    }
    return false;
}

bool ContainsAnimationComponentGuid(
    const Vans::VansSerializedValue& entities,
    const Vans::VansSceneParentReference& parent)
{
    for (const Vans::VansSerializedValue& entity : entities.arrayItems)
    {
        if (Vans::ReadSerializedStringField(entity, "id") != parent.entityGuid.ToString())
            continue;
        const Vans::VansSerializedValue* components = Vans::FindObjectField(entity, "components");
        if (!components || components->kind != Vans::VansSerializedValue::Kind::Array)
            return false;
        for (const Vans::VansSerializedValue& component : components->arrayItems)
        {
            if (Vans::ReadSerializedStringField(component, "id")
                    == parent.animationComponentGuid.ToString()
                && Vans::ReadSerializedStringField(component, "type") == "Animation")
            {
                return true;
            }
        }
        return false;
    }
    return false;
}

std::string MakeUniqueEntityName(
    const Vans::VansSerializedValue& entities,
    const std::string& requestedBaseName)
{
    const std::string baseName = requestedBaseName.empty()
        ? "Empty Object"
        : requestedBaseName;
    std::unordered_set<std::string> names;
    names.reserve(entities.arrayItems.size());
    for (const Vans::VansSerializedValue& entity : entities.arrayItems)
    {
        const std::string name = Vans::ReadSerializedStringField(entity, "name");
        if (!name.empty())
            names.insert(name);
    }

    if (names.find(baseName) == names.end())
        return baseName;
    for (std::uint32_t suffix = 1; suffix < UINT32_MAX; ++suffix)
    {
        const std::string candidate = baseName + " (" + std::to_string(suffix) + ")";
        if (names.find(candidate) == names.end())
            return candidate;
    }
    return baseName + " (New)";
}
}

VansSceneEntityCreationService::Result VansSceneEntityCreationService::CreateEmptyObject(
    Vans::EditorAPI::IEngineEditorAPI& editorAPI,
    const Vans::VansSceneDocument& document,
    Vans::VansSceneEditService& editService,
    const EmptyObjectRequest& request)
{
    const Vans::VansSerializedValue sceneRoot = document.SerializedRootSnapshot();
    const Vans::VansSerializedValue* entities = FindSceneEntities(sceneRoot);
    if (!entities)
        return { false, false, {}, "Scene document has no entities array" };
    if (request.parent)
    {
        if (!request.parent->IsValid())
            return { false, false, {}, "Empty object parent reference is invalid" };
        if (!ContainsEntityGuid(*entities, request.parent->entityGuid.ToString()))
            return { false, false, {}, "Empty object parent entity does not exist" };
        if (request.parent->IsAnchor()
            && !ContainsAnimationComponentGuid(*entities, *request.parent))
        {
            return { false, false, {},
                "Empty object anchor owner has no matching Animation component" };
        }
    }

    Vans::SceneEmptyEntityFactoryRequest factoryRequest;
    factoryRequest.entityName = MakeUniqueEntityName(*entities, request.baseName);
    factoryRequest.parent = request.parent;
    Vans::VansSerializedValue entity =
        Vans::VansSceneEntityFactory::BuildEmptyEntity(factoryRequest);
    const std::string entityGuid = Vans::ReadSerializedStringField(entity, "id");
    if (entityGuid.empty())
        return { false, false, {}, "Empty object factory did not produce an entity id" };

    const Vans::EditorAPI::ScenePropertyValue runtimeEntity = Vans::FromSerializedValue(entity);
    auto createRuntimeEntity = [&editorAPI, runtimeEntity]()
    {
        Vans::EditorAPI::RuntimeSceneEntitiesCreateRequest createRequest;
        createRequest.sceneEntities.push_back(runtimeEntity);
        return editorAPI.CreateRuntimeSceneEntities(createRequest).created;
    };

    Vans::SceneEditLifecycleHooks hooks;
    hooks.afterExecute = createRuntimeEntity;
    hooks.afterUndo = [&editorAPI, entityGuid]()
    {
        Vans::EditorAPI::RuntimeEntityDestroyRequest destroyRequest;
        destroyRequest.entityGuid = entityGuid;
        return editorAPI.DestroyRuntimeEntity(destroyRequest).destroyed;
    };
    hooks.afterRedo = createRuntimeEntity;

    std::vector<Vans::VansSerializedValue> appendedEntities;
    appendedEntities.push_back(std::move(entity));
    const Vans::SceneEditResult editResult =
        editService.AppendEntities(std::move(appendedEntities), std::move(hooks));
    if (!editResult)
        return { false, false, {}, editResult.message };

    Result result;
    result.success = true;
    result.runtimeChangeApplied = editResult.runtimeChangeApplied;
    result.entityGuid = entityGuid;
    if (!result.runtimeChangeApplied)
        result.message = "Empty object was added to the scene document, but runtime preview creation failed";
    return result;
}
}
