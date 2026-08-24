#include "VansSceneObjectReferenceRemapper.h"

#include "../AssetCore/VansAssetGuid.h"
#include "../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../SceneCore/VansSceneDocument.h"
#include "../SceneCore/VansSceneParentReference.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
namespace
{
struct SceneObjectReferenceRemap
{
    std::unordered_map<std::string, std::string> entityGuids;
    std::unordered_map<std::string, std::string> componentGuids;
};

const VansSerializedValue* FindEntity(const VansSerializedValue& entities, const std::string& entityGuid)
{
    if (entities.kind != VansSerializedValue::Kind::Array)
        return nullptr;
    for (const VansSerializedValue& entity : entities.arrayItems)
        if (ReadSerializedStringField(entity, "id") == entityGuid)
            return &entity;
    return nullptr;
}

void BuildChildrenIndex(
    const VansSerializedValue& entities,
    std::unordered_map<std::string, std::vector<std::string>>& children)
{
    if (entities.kind != VansSerializedValue::Kind::Array)
        return;
    for (const VansSerializedValue& entity : entities.arrayItems)
    {
        const std::string id = ReadSerializedStringField(entity, "id");
        if (id.empty())
            continue;
		const VansSerializedValue* parentValue = FindObjectField(entity, "parent");
		const std::string parent = parentValue
			? ReadSceneParentEntityGuid(*parentValue) : std::string{};
        children[parent].push_back(id);
    }
}

void AppendSubtreeEntityGuids(
    const std::string& entityGuid,
    const std::unordered_map<std::string, std::vector<std::string>>& children,
    std::vector<std::string>& orderedGuids)
{
    orderedGuids.push_back(entityGuid);
    const auto found = children.find(entityGuid);
    if (found == children.end())
        return;
    for (const std::string& childGuid : found->second)
        AppendSubtreeEntityGuids(childGuid, children, orderedGuids);
}

void RenameDuplicatedEntity(VansSerializedValue& entity, bool rootEntity)
{
    VansSerializedValue* nameValue = FindObjectField(entity, "name");
    if (!nameValue || nameValue->kind != VansSerializedValue::Kind::String)
        return;
    if (rootEntity)
        nameValue->stringValue += " Copy";
}

bool RemapGuid(std::string& guid, const std::unordered_map<std::string, std::string>& mapping)
{
    const auto found = mapping.find(guid);
    if (found == mapping.end())
        return false;
    guid = found->second;
    return true;
}

void RemapReferenceObject(VansSerializedValue& object, const SceneObjectReferenceRemap& remap)
{
    SerializedObjectReferenceValue reference;
    if (!TryReadSerializedObjectReference(object, reference))
        return;

    bool changed = false;
    if (reference.domain == "SceneEntity")
    {
        changed |= RemapGuid(reference.entityGuid, remap.entityGuids);
        reference.guid = reference.entityGuid;
    }
    else if (reference.domain == "SceneComponent")
    {
        changed |= RemapGuid(reference.entityGuid, remap.entityGuids);
        changed |= RemapGuid(reference.componentGuid, remap.componentGuids);
        reference.guid = reference.componentGuid;
    }

    if (changed)
        WriteSerializedObjectReference(object, reference);
}

void RemapSceneObjectReferences(VansSerializedValue& value, const SceneObjectReferenceRemap& remap)
{
    if (value.kind == VansSerializedValue::Kind::Object)
    {
        RemapReferenceObject(value, remap);
        for (auto& [name, item] : value.objectFields)
            RemapSceneObjectReferences(item, remap);
    }
    else if (value.kind == VansSerializedValue::Kind::Array)
    {
        for (VansSerializedValue& item : value.arrayItems)
            RemapSceneObjectReferences(item, remap);
    }
}

void RemapParentReference(VansSerializedValue& entity, const SceneObjectReferenceRemap& remap)
{
	VansSerializedValue* value = FindObjectField(entity, "parent");
	if (!value || value->kind == VansSerializedValue::Kind::Null)
		return;
	VansSceneParentReference parent;
	std::string error;
	if (!TryReadSceneParentReference(*value, parent, error))
		return;
	std::string entityGuid = parent.entityGuid.ToString();
	if (RemapGuid(entityGuid, remap.entityGuids))
		VansAssetGuid::TryParse(entityGuid, parent.entityGuid);
	if (parent.IsAnchor())
	{
		std::string componentGuid = parent.animationComponentGuid.ToString();
		if (RemapGuid(componentGuid, remap.componentGuids))
			VansAssetGuid::TryParse(componentGuid, parent.animationComponentGuid);
	}
	*value = WriteSceneParentReference(parent);
}
}

SceneEntityDuplicateResult DuplicateSceneEntitySubtree(
    const VansSceneDocument& document,
    const std::string& rootEntityGuid)
{
    SceneEntityDuplicateResult result;
    if (rootEntityGuid.empty())
    {
        result.message = "No scene entity selected";
        return result;
    }
    const VansSerializedValue sceneRoot = document.SerializedRootSnapshot();
    const VansSerializedValue* sourceEntities = FindObjectField(sceneRoot, "entities");
    if (!sourceEntities || sourceEntities->kind != VansSerializedValue::Kind::Array)
    {
        result.message = "Scene document has no entities array";
        return result;
    }

    if (!FindEntity(*sourceEntities, rootEntityGuid))
    {
        result.message = "Selected scene entity does not exist";
        return result;
    }

    std::unordered_map<std::string, std::vector<std::string>> children;
    BuildChildrenIndex(*sourceEntities, children);

    std::vector<std::string> orderedGuids;
    AppendSubtreeEntityGuids(rootEntityGuid, children, orderedGuids);
    std::vector<VansSerializedValue> clonedEntities;
    clonedEntities.reserve(orderedGuids.size());

    SceneObjectReferenceRemap remap;
    for (const std::string& oldEntityGuid : orderedGuids)
    {
        const std::string newEntityGuid = VansEntityGuid::New().ToString();
        remap.entityGuids.emplace(oldEntityGuid, newEntityGuid);
        if (oldEntityGuid == rootEntityGuid)
            result.duplicatedRootGuid = newEntityGuid;
    }

    for (const std::string& oldEntityGuid : orderedGuids)
    {
        const VansSerializedValue* sourceEntity = FindEntity(*sourceEntities, oldEntityGuid);
        if (!sourceEntity)
            continue;
        VansSerializedValue clone = *sourceEntity;
        SetSerializedObjectField(clone, "id",
            VansSerializedValue::String(remap.entityGuids[oldEntityGuid]));
        RenameDuplicatedEntity(clone, oldEntityGuid == rootEntityGuid);

        VansSerializedValue* components = FindObjectField(clone, "components");
        if (components && components->kind == VansSerializedValue::Kind::Array)
        {
            for (VansSerializedValue& component : components->arrayItems)
            {
                if (component.kind != VansSerializedValue::Kind::Object)
                    continue;
                const std::string oldComponentGuid = ReadSerializedStringField(component, "id");
                if (oldComponentGuid.empty())
                    continue;
                const std::string newComponentGuid = VansComponentGuid::New().ToString();
                remap.componentGuids.emplace(oldComponentGuid, newComponentGuid);
                SetSerializedObjectField(component, "id", VansSerializedValue::String(newComponentGuid));
            }
        }

        clonedEntities.push_back(std::move(clone));
    }

	for (VansSerializedValue& clone : clonedEntities)
	{
		RemapParentReference(clone, remap);
        RemapSceneObjectReferences(clone, remap);
	}

    result.entities.reserve(clonedEntities.size());
    for (VansSerializedValue& clone : clonedEntities)
        result.entities.push_back(std::move(clone));

    result.success = !result.entities.empty();
    if (!result.success)
        result.message = "No scene entities duplicated";
    return result;
}
}
