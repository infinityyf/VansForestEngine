#pragma once

#include "VansSerializedValue.h"

#include <string>

namespace Vans
{
struct SerializedObjectReferenceValue
{
    std::string domain;
    std::string guid;
    std::string assetType;
    std::string entityGuid;
    std::string componentGuid;
    std::string componentType;
};

VansSerializedValue MakeSerializedProjectAssetObjectReference(
    std::string assetGuid,
    std::string assetType);

VansSerializedValue MakeSerializedSceneEntityObjectReference(
    std::string entityGuid);

VansSerializedValue MakeSerializedSceneComponentObjectReference(
    std::string entityGuid,
    std::string componentGuid,
    std::string componentType);

bool TryReadSerializedObjectReference(
    const VansSerializedValue& value,
    SerializedObjectReferenceValue& reference);

bool HasSerializedObjectReferenceTarget(const SerializedObjectReferenceValue& reference);

bool NormalizeSerializedObjectReference(
    VansSerializedValue& value,
    const SerializedObjectReferenceValue& expected);

bool WriteSerializedObjectReference(
    VansSerializedValue& value,
    const SerializedObjectReferenceValue& reference);
}
