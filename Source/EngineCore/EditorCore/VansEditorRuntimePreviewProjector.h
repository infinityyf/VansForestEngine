#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../EngineAPILayer/Public/EngineDTOs.h"

#include <filesystem>
#include <string>

namespace Vans
{
class VansSceneDocument;
namespace EditorAPI { class ISceneInteractionEditorAPI; }

EditorAPI::RuntimeEntityPreviewChange BuildRuntimeEntityHierarchyPreviewChange(const VansSerializedValue& entity);
bool RestoreRuntimeChildHierarchy(EditorAPI::ISceneInteractionEditorAPI& api,
    const VansSceneDocument& document, const std::string& parentGuid);
bool RequiresRuntimeEntityRebuild(const VansSerializedValue& before, const VansSerializedValue& after);

EditorAPI::RuntimeMaterialPreviewChange BuildRuntimeMaterialPreviewChange(
    const std::filesystem::path& assetPath,
    const VansSerializedValue& assetRoot,
    const std::string& changedPointer = {});

EditorAPI::RuntimeEntityPreviewChange BuildRuntimeEntityPreviewChange(
    const VansSerializedValue& entity);

EditorAPI::RuntimeEntityPreviewChange BuildRuntimeEntityPreviewChangeFromSceneRoot(
    const VansSerializedValue& sceneRoot,
    const std::string& entityGuid);
}
