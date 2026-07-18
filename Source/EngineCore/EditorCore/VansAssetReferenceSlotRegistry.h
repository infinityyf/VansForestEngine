#pragma once

#include "../EngineAPILayer/Public/EngineDTOs.h"

#include <string>

namespace Vans
{
struct AssetReferenceSlotDescriptor
{
    EditorAPI::AssetType expectedType = EditorAPI::AssetType::Unknown;
};

class VansAssetReferenceSlotRegistry
{
public:
    static AssetReferenceSlotDescriptor Resolve(
        const std::string& componentType,
        const std::string& parentKey,
        const std::string& fieldKey);
};
}
