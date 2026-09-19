#include "VansSceneObjectReferenceRemapper.h"

#include "../SceneCore/VansSceneDocument.h"
#include "../SceneCore/VansSceneObjectGraph.h"
#include "../AssetCore/VansAssetGuid.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

namespace Vans
{
SceneEntityDuplicateResult DuplicateSceneEntitySubtree(
    const VansSceneDocument& document, const std::string& rootEntityGuid)
{
    SceneEntityDuplicateResult result;
    const auto root = document.SerializedRootSnapshot();
    const auto* source = FindObjectField(root, "entities");
    VansSerializedValue subtree;
    if (!source || !ExtractSceneObjectSubtree(*source, rootEntityGuid, subtree, result.message))
        return result;
    VansSceneObjectIdentityMap identities;
    for (const auto& entity : subtree.arrayItems)
    {
        identities.entities.emplace(ReadSerializedStringField(entity, "id"), VansEntityGuid::New().ToString());
        const auto* components = FindObjectField(entity, "components");
        if (!components || components->kind != VansSerializedValue::Kind::Array)
        { result.message = "Duplicated entity has no components array"; return result; }
        for (const auto& component : components->arrayItems)
            identities.components.emplace(ReadSerializedStringField(component, "id"), VansComponentGuid::New().ToString());
    }
    if (!RemapSceneObjectGraph(subtree, identities, result.message)) return result;
    result.duplicatedRootGuid = identities.entities.at(rootEntityGuid);
    auto& rootEntity = subtree.arrayItems.front();
    SetSerializedObjectField(rootEntity, "name", VansSerializedValue::String(
        ReadSerializedStringField(rootEntity, "name") + " Copy"));
    result.entities = std::move(subtree.arrayItems);
    result.success = true;
    return result;
}
}
