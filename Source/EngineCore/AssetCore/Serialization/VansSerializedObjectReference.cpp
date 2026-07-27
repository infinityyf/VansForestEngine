#include "VansSerializedObjectReference.h"

#include "VansSerializedValueAccess.h"

#include <utility>

namespace Vans
{
namespace
{
bool EnsureStringField(VansSerializedValue& value, const std::string& key, const std::string& fallback)
{
    VansSerializedValue* field = FindObjectField(value, key);
    if (field && field->kind == VansSerializedValue::Kind::String)
        return false;
    SetSerializedObjectField(value, key, VansSerializedValue::String(fallback));
    return true;
}

bool SetStringFieldIfDifferent(VansSerializedValue& value, const std::string& key, const std::string& text)
{
    if (ReadSerializedStringField(value, key) == text)
        return false;
    SetSerializedObjectField(value, key, VansSerializedValue::String(text));
    return true;
}
}

bool WriteSerializedObjectReference(
    VansSerializedValue& value,
    const SerializedObjectReferenceValue& reference);

VansSerializedValue MakeSerializedProjectAssetObjectReference(
    std::string assetGuid,
    std::string assetType)
{
    return VansSerializedValue::Object({
        { "domain", VansSerializedValue::String("ProjectAsset") },
        { "guid", VansSerializedValue::String(std::move(assetGuid)) },
        { "assetType", VansSerializedValue::String(std::move(assetType)) }
    });
}

VansSerializedValue MakeSerializedSceneEntityObjectReference(
    std::string entityGuid)
{
    const std::string guid = entityGuid;
    return VansSerializedValue::Object({
        { "domain", VansSerializedValue::String("SceneEntity") },
        { "guid", VansSerializedValue::String(guid) },
        { "entityGuid", VansSerializedValue::String(std::move(entityGuid)) }
    });
}

VansSerializedValue MakeSerializedSceneComponentObjectReference(
    std::string entityGuid,
    std::string componentGuid,
    std::string componentType)
{
    const std::string guid = componentGuid;
    return VansSerializedValue::Object({
        { "domain", VansSerializedValue::String("SceneComponent") },
        { "guid", VansSerializedValue::String(guid) },
        { "entityGuid", VansSerializedValue::String(std::move(entityGuid)) },
        { "componentGuid", VansSerializedValue::String(std::move(componentGuid)) },
        { "componentType", VansSerializedValue::String(std::move(componentType)) }
    });
}

bool TryReadSerializedObjectReference(
    const VansSerializedValue& value,
    SerializedObjectReferenceValue& reference)
{
    reference = {};
    if (value.kind != VansSerializedValue::Kind::Object)
        return false;

    reference.domain = ReadSerializedStringField(value, "domain");
    reference.guid = ReadSerializedStringField(value, "guid");
    reference.assetType = ReadSerializedStringField(value, "assetType");
    reference.entityGuid = ReadSerializedStringField(value, "entityGuid");
    reference.componentGuid = ReadSerializedStringField(value, "componentGuid");
    reference.componentType = ReadSerializedStringField(value, "componentType");
    if (reference.domain == "SceneEntity")
    {
        if (reference.entityGuid.empty())
            reference.entityGuid = reference.guid;
        if (reference.guid.empty())
            reference.guid = reference.entityGuid;
    }
    else if (reference.domain == "SceneComponent")
    {
        if (reference.componentGuid.empty())
            reference.componentGuid = reference.guid;
        if (reference.guid.empty())
            reference.guid = reference.componentGuid;
    }
    return !reference.domain.empty();
}

bool HasSerializedObjectReferenceTarget(const SerializedObjectReferenceValue& reference)
{
    return !reference.guid.empty() ||
        !reference.entityGuid.empty() ||
        !reference.componentGuid.empty();
}

bool NormalizeSerializedObjectReference(
    VansSerializedValue& value,
    const SerializedObjectReferenceValue& expected)
{
    if (expected.domain == "ProjectAsset")
    {
        if (value.kind != VansSerializedValue::Kind::Object)
        {
            value = MakeSerializedProjectAssetObjectReference("", expected.assetType);
            return true;
        }

        bool changed = false;
        changed |= SetStringFieldIfDifferent(value, "domain", "ProjectAsset");
        changed |= EnsureStringField(value, "guid", "");
        if (!expected.assetType.empty())
            changed |= SetStringFieldIfDifferent(value, "assetType", expected.assetType);
        else
            changed |= EnsureStringField(value, "assetType", "");
        changed |= EraseSerializedObjectField(value, "entityGuid");
        changed |= EraseSerializedObjectField(value, "componentGuid");
        changed |= EraseSerializedObjectField(value, "componentType");
        return changed;
    }

    if (expected.domain == "SceneEntity")
    {
        if (value.kind != VansSerializedValue::Kind::Object)
        {
            value = MakeSerializedSceneEntityObjectReference("");
            return true;
        }

        SerializedObjectReferenceValue existing;
        (void)TryReadSerializedObjectReference(value, existing);
        existing.domain = "SceneEntity";
        if (existing.entityGuid.empty())
            existing.entityGuid = existing.guid;
        existing.guid = existing.entityGuid;
        existing.assetType.clear();
        existing.componentGuid.clear();
        existing.componentType.clear();
        return WriteSerializedObjectReference(value, existing);
    }

    if (expected.domain == "SceneComponent")
    {
        if (value.kind != VansSerializedValue::Kind::Object)
        {
            value = MakeSerializedSceneComponentObjectReference("", "", expected.componentType);
            return true;
        }

        SerializedObjectReferenceValue existing;
        (void)TryReadSerializedObjectReference(value, existing);
        existing.domain = "SceneComponent";
        if (existing.componentGuid.empty())
            existing.componentGuid = existing.guid;
        existing.guid = existing.componentGuid;
        existing.assetType.clear();
        if (!expected.componentType.empty())
            existing.componentType = expected.componentType;
        return WriteSerializedObjectReference(value, existing);
    }

    return false;
}

bool WriteSerializedObjectReference(
    VansSerializedValue& value,
    const SerializedObjectReferenceValue& reference)
{
    if (reference.domain == "ProjectAsset")
    {
        if (value.kind != VansSerializedValue::Kind::Object)
        {
            value = MakeSerializedProjectAssetObjectReference(reference.guid, reference.assetType);
            return true;
        }

        bool changed = false;
        changed |= SetStringFieldIfDifferent(value, "domain", "ProjectAsset");
        changed |= SetStringFieldIfDifferent(value, "guid", reference.guid);
        changed |= SetStringFieldIfDifferent(value, "assetType", reference.assetType);
        changed |= EraseSerializedObjectField(value, "entityGuid");
        changed |= EraseSerializedObjectField(value, "componentGuid");
        changed |= EraseSerializedObjectField(value, "componentType");
        return changed;
    }

    if (reference.domain == "SceneEntity")
    {
        const std::string entityGuid = !reference.entityGuid.empty()
            ? reference.entityGuid
            : reference.guid;
        if (value.kind != VansSerializedValue::Kind::Object)
        {
            value = MakeSerializedSceneEntityObjectReference(entityGuid);
            return true;
        }

        bool changed = false;
        changed |= SetStringFieldIfDifferent(value, "domain", "SceneEntity");
        changed |= SetStringFieldIfDifferent(value, "guid", entityGuid);
        changed |= SetStringFieldIfDifferent(value, "entityGuid", entityGuid);
        changed |= EraseSerializedObjectField(value, "assetType");
        changed |= EraseSerializedObjectField(value, "componentGuid");
        changed |= EraseSerializedObjectField(value, "componentType");
        return changed;
    }

    if (reference.domain == "SceneComponent")
    {
        const std::string componentGuid = !reference.componentGuid.empty()
            ? reference.componentGuid
            : reference.guid;
        if (value.kind != VansSerializedValue::Kind::Object)
        {
            value = MakeSerializedSceneComponentObjectReference(
                reference.entityGuid,
                componentGuid,
                reference.componentType);
            return true;
        }

        bool changed = false;
        changed |= SetStringFieldIfDifferent(value, "domain", "SceneComponent");
        changed |= SetStringFieldIfDifferent(value, "guid", componentGuid);
        changed |= SetStringFieldIfDifferent(value, "entityGuid", reference.entityGuid);
        changed |= SetStringFieldIfDifferent(value, "componentGuid", componentGuid);
        changed |= SetStringFieldIfDifferent(value, "componentType", reference.componentType);
        changed |= EraseSerializedObjectField(value, "assetType");
        return changed;
    }

    return false;
}
}
