#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"

#include <string>
#include <vector>

namespace Vans
{
class VansSceneDocument;

struct SceneEntityDuplicateResult
{
    bool success = false;
    std::string message;
    std::string duplicatedRootGuid;
    std::vector<VansSerializedValue> entities;

    explicit operator bool() const { return success; }
};

SceneEntityDuplicateResult DuplicateSceneEntitySubtree(
    const VansSceneDocument& document,
    const std::string& rootEntityGuid);
}
