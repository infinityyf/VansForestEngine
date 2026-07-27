#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../EngineAPILayer/Public/EngineDTOs.h"

#include <filesystem>
#include <string>

namespace Vans
{
EditorAPI::RuntimeMaterialPreviewChange BuildRuntimeMaterialPreviewChange(
    const std::filesystem::path& assetPath,
    const VansSerializedValue& assetRoot,
    const std::string& changedPointer = {});
}
