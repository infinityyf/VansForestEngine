#include "VansSceneObjectGraph.h"

#include "VansSceneParentReference.h"
#include "../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <unordered_set>

namespace Vans
{
namespace
{
std::string Map(const std::string& guid, const std::unordered_map<std::string, std::string>& map)
{
    const auto it = map.find(guid);
    return it == map.end() ? guid : it->second;
}

bool VisitValue(VansSerializedValue& value, const std::string& path,
    const VansSceneObjectReferenceVisitor& visitor, std::string& error)
{
    if (value.kind == VansSerializedValue::Kind::Object)
    {
        SerializedObjectReferenceValue reference;
        if (TryReadSerializedObjectReference(value, reference) &&
            (reference.domain == "SceneEntity" || reference.domain == "SceneComponent"))
        {
            VansSceneObjectReference target{ reference.entityGuid, reference.componentGuid, path };
            if (!visitor(target, error)) return false;
            reference.entityGuid = target.entityGuid;
            reference.componentGuid = target.componentGuid;
            reference.guid = reference.domain == "SceneEntity" ? target.entityGuid : target.componentGuid;
            WriteSerializedObjectReference(value, reference);
            return true;
        }
        for (auto& field : value.objectFields)
            if (!VisitValue(field.second, path + "/" + field.first, visitor, error)) return false;
    }
    else if (value.kind == VansSerializedValue::Kind::Array)
        for (std::size_t i = 0; i < value.arrayItems.size(); ++i)
            if (!VisitValue(value.arrayItems[i], path + "/" + std::to_string(i), visitor, error)) return false;
    return true;
}
}

bool VisitSceneObjectReferences(VansSerializedValue& entities,
    const VansSceneObjectReferenceVisitor& visitor, std::string& error)
{
    error.clear();
    if (entities.kind != VansSerializedValue::Kind::Array)
    { error = "Object graph requires an entities array"; return false; }
    for (auto& entity : entities.arrayItems)
    {
        const std::string path = "entity[" + ReadSerializedStringField(entity, "id") + "]";
        auto* parentValue = FindObjectField(entity, "parent");
        if (parentValue && !parentValue->IsNull())
        {
            VansSceneParentReference parent;
            if (!TryReadSceneParentReference(*parentValue, parent, error)) return false;
            VansSceneObjectReference target{ parent.entityGuid.ToString(),
                parent.IsAnchor() ? parent.animationComponentGuid.ToString() : std::string{}, path + "/parent" };
            if (!visitor(target, error)) return false;
            if (target.entityGuid.empty()) *parentValue = VansSerializedValue::Null();
            else
            {
                if (!VansAssetGuid::TryParse(target.entityGuid, parent.entityGuid))
                { error = target.propertyPath + ": invalid parent entity"; return false; }
                if (parent.IsAnchor() && !VansAssetGuid::TryParse(target.componentGuid, parent.animationComponentGuid))
                { error = target.propertyPath + ": invalid parent animation component"; return false; }
                *parentValue = WriteSceneParentReference(parent);
            }
        }
        auto* components = FindObjectField(entity, "components");
        if (!components || components->kind != VansSerializedValue::Kind::Array)
        { error = path + ": components must be an array"; return false; }
        for (auto& component : components->arrayItems)
        {
            const std::string componentPath = path + "/component[" + ReadSerializedStringField(component, "id") + "]";
            if (!VisitValue(component, componentPath, visitor, error)) return false;
            // Timeline 的 binding 使用独立字段；不能依赖通用对象引用解析器。
            if (ReadSerializedStringField(component, "type") != "Timeline") continue;
            auto* data = FindObjectField(component, "data");
            auto* bindings = data ? FindObjectField(*data, "bindingOverrides") : nullptr;
            if (!bindings) continue;
            if (bindings->kind != VansSerializedValue::Kind::Array)
            { error = componentPath + "/data/bindingOverrides must be an array"; return false; }
            for (auto& binding : bindings->arrayItems)
            {
                const auto original = ReadSerializedStringField(binding, "targetEntity");
                VansSceneObjectReference target{ original == "owner" ? std::string{} : original,
                    ReadSerializedStringField(binding, "targetComponent"), componentPath + "/data/bindingOverrides" };
                if (!visitor(target, error)) return false;
                if (original != "owner" && FindObjectField(binding, "targetEntity"))
                    SetSerializedObjectField(binding, "targetEntity", VansSerializedValue::String(target.entityGuid));
                if (FindObjectField(binding, "targetComponent"))
                    SetSerializedObjectField(binding, "targetComponent", VansSerializedValue::String(target.componentGuid));
            }
        }
    }
    return true;
}

bool RemapSceneObjectGraph(VansSerializedValue& entities,
    const VansSceneObjectIdentityMap& identities, std::string& error)
{
    if (!VisitSceneObjectReferences(entities, [&](VansSceneObjectReference& reference, std::string&)
    {
        reference.entityGuid = Map(reference.entityGuid, identities.entities);
        reference.componentGuid = Map(reference.componentGuid, identities.components);
        return true;
    }, error)) return false;
    for (auto& entity : entities.arrayItems)
    {
        SetSerializedObjectField(entity, "id", VansSerializedValue::String(
            Map(ReadSerializedStringField(entity, "id"), identities.entities)));
        for (auto& component : FindObjectField(entity, "components")->arrayItems)
            SetSerializedObjectField(component, "id", VansSerializedValue::String(
                Map(ReadSerializedStringField(component, "id"), identities.components)));
    }
    return true;
}

bool ExtractSceneObjectSubtree(const VansSerializedValue& entities,
    const std::string& rootGuid, VansSerializedValue& subtree, std::string& error)
{
    error.clear();
    subtree = VansSerializedValue::Array({});
    if (entities.kind != VansSerializedValue::Kind::Array)
    { error = "Object graph requires an entities array"; return false; }
    std::unordered_map<std::string, const VansSerializedValue*> objects;
    std::unordered_map<std::string, std::vector<std::string>> children;
    for (const auto& entity : entities.arrayItems)
    {
        const auto id = ReadSerializedStringField(entity, "id");
        if (id.empty() || !objects.emplace(id, &entity).second)
        { error = "Missing or duplicate entity identity"; return false; }
        const auto* parent = FindObjectField(entity, "parent");
        if (parent && !parent->IsNull()) children[ReadSceneParentEntityGuid(*parent)].push_back(id);
    }
    if (!objects.count(rootGuid)) { error = "Selected entity no longer exists"; return false; }
    std::unordered_set<std::string> visited;
    std::vector<std::string> pending{ rootGuid };
    while (!pending.empty())
    {
        const auto id = pending.back(); pending.pop_back();
        if (!visited.insert(id).second) { error = "Object hierarchy contains a cycle"; return false; }
        subtree.arrayItems.push_back(*objects.at(id));
        const auto& descendants = children[id];
        pending.insert(pending.end(), descendants.rbegin(), descendants.rend());
    }
    return true;
}
}
