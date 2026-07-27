#include "VansSceneObjectReferenceResolver.h"

#include "../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../SceneCore/VansSceneDocument.h"

#include <utility>

namespace Vans
{
namespace
{
VansSerializedValue EncodeSceneObjectReferenceValue(
    const ObjectReferenceSlotDescriptor& slot,
    const EditorObjectHandle& handle)
{
    if (slot.expectedDomain == EditorObjectDomain::SceneEntity)
    {
        const std::string entityGuid = handle.entityGuid.empty() ? handle.guid : handle.entityGuid;
        return MakeSerializedSceneEntityObjectReference(entityGuid);
    }

    if (slot.expectedDomain == EditorObjectDomain::SceneComponent)
    {
        const std::string entityGuid = handle.entityGuid.empty() ? handle.guid : handle.entityGuid;
        const std::string componentGuid = handle.componentGuid.empty() ? handle.guid : handle.componentGuid;
        return MakeSerializedSceneComponentObjectReference(
            entityGuid,
            componentGuid,
            handle.componentType);
    }

    return VansSerializedValue::Null();
}

const VansSerializedValue* FindSceneEntityByGuid(
    const VansSerializedValue& root,
    const std::string& entityGuid)
{
    const VansSerializedValue* entities = FindObjectField(root, "entities");
    if (!entities || entities->kind != VansSerializedValue::Kind::Array)
        return nullptr;

    for (const VansSerializedValue& entity : entities->arrayItems)
    {
        if (ReadSerializedStringField(entity, "id") == entityGuid)
            return &entity;
    }
    return nullptr;
}

const VansSerializedValue* FindSceneComponentByGuidOrType(
    const VansSerializedValue& entity,
    const std::string& componentGuid,
    const std::string& componentType)
{
    const VansSerializedValue* components = FindObjectField(entity, "components");
    if (!components || components->kind != VansSerializedValue::Kind::Array)
        return nullptr;

    for (const VansSerializedValue& component : components->arrayItems)
    {
        if (!componentGuid.empty() && ReadSerializedStringField(component, "id") == componentGuid)
            return &component;
        if (!componentType.empty() && ReadSerializedStringField(component, "type") == componentType)
            return &component;
    }
    return nullptr;
}
}

SceneObjectReferenceResolution ResolveSceneObjectReference(
    const VansSceneDocument& document,
    const ObjectReferenceSlotDescriptor& slot,
    const EditorObjectHandle& source)
{
    SceneObjectReferenceResolution result;
    result.handle = source;
    const VansSerializedValue root = document.SerializedRootSnapshot();

    const bool clearedReference =
        source.guid.empty() &&
        source.entityGuid.empty() &&
        source.componentGuid.empty();
    if (clearedReference)
    {
        result.handle.domain = slot.expectedDomain;
        if (slot.expectedDomain == EditorObjectDomain::SceneComponent &&
            result.handle.componentType.empty())
        {
            result.handle.componentType = slot.expectedComponentType;
        }
        result.encodedValue = EncodeSceneObjectReferenceValue(slot, result.handle);
        result.success = true;
        return result;
    }

    if (slot.expectedDomain == EditorObjectDomain::SceneEntity)
    {
        const std::string entityGuid = source.entityGuid.empty() ? source.guid : source.entityGuid;
        if (entityGuid.empty())
        {
            result.message = "Scene entity reference has no entity GUID";
            return result;
        }

        const VansSerializedValue* entity = FindSceneEntityByGuid(root, entityGuid);
        if (!entity)
        {
            result.message = "Scene entity reference target does not exist";
            return result;
        }

        result.handle.domain = EditorObjectDomain::SceneEntity;
        result.handle.guid = entityGuid;
        result.handle.entityGuid = entityGuid;
        result.handle.componentGuid.clear();
        result.handle.componentType.clear();
        result.encodedValue = EncodeSceneObjectReferenceValue(slot, result.handle);
        result.entityDisplayName =
            ReadSerializedStringField(*entity, "name", entityGuid.substr(0, 8));
        result.success = true;
        return result;
    }

    if (slot.expectedDomain == EditorObjectDomain::SceneComponent)
    {
        const std::string entityGuid = source.entityGuid.empty() ? source.guid : source.entityGuid;
        if (entityGuid.empty())
        {
            result.message = "Scene component reference has no owner entity GUID";
            return result;
        }

        const VansSerializedValue* entity = FindSceneEntityByGuid(root, entityGuid);
        if (!entity)
        {
            result.message = "Scene component owner entity does not exist";
            return result;
        }
        result.entityDisplayName =
            ReadSerializedStringField(*entity, "name", entityGuid.substr(0, 8));

        std::string componentGuid = source.componentGuid;
        if (componentGuid.empty() && source.domain == EditorObjectDomain::SceneComponent)
            componentGuid = source.guid;
        const std::string componentType =
            !source.componentType.empty() ? source.componentType : slot.expectedComponentType;
        const VansSerializedValue* component =
            FindSceneComponentByGuidOrType(*entity, componentGuid, componentType);
        if (!component)
        {
            result.message = "Scene component reference target does not exist";
            return result;
        }

        result.handle.domain = EditorObjectDomain::SceneComponent;
        result.handle.entityGuid = entityGuid;
        result.handle.componentGuid = ReadSerializedStringField(*component, "id", componentGuid);
        result.handle.guid = result.handle.componentGuid;
        result.handle.componentType = ReadSerializedStringField(*component, "type", componentType);
        result.encodedValue = EncodeSceneObjectReferenceValue(slot, result.handle);
        result.componentDisplayType = result.handle.componentType;
        result.success = true;
        return result;
    }

    result.message = "Unsupported scene object reference domain";
    return result;
}

bool TryEncodeSceneDocumentObjectReferenceAssignment(
    const VansSceneDocument& document,
    const ObjectReferenceAssignment& assignment,
    VansSerializedValue& encodedValue,
    std::string* error)
{
    if (assignment.value.domain == EditorObjectDomain::ProjectAsset)
    {
        return TryEncodeProjectAssetReferenceAssignment(assignment, encodedValue, error);
    }

    if (!assignment.slot.Accepts(assignment.value))
    {
        if (error)
            *error = "Dragged object is not accepted by this reference slot";
        return false;
    }

    SceneObjectReferenceResolution resolved =
        ResolveSceneObjectReference(document, assignment.slot, assignment.value);
    if (!resolved)
    {
        if (error)
            *error = resolved.message;
        return false;
    }
    if (!resolved.encodedValue)
    {
        if (error)
            *error = "Object reference assignment could not be encoded";
        return false;
    }

    encodedValue = std::move(*resolved.encodedValue);
    return true;
}
}
