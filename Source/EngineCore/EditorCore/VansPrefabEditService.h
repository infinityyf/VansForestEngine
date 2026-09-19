#pragma once
#include "../SceneCore/Prefab/VansPrefabAsset.h"
#include "VansSceneEditService.h"
#include <filesystem>

namespace Vans
{
namespace EditorAPI { class IEngineEditorAPI; }
class VansPrefabEditService
{
public:
    static VansPrefabLookup Lookup(EditorAPI::IEngineEditorAPI& api);
    static SceneEditResult Create(EditorAPI::IEngineEditorAPI& api, VansSceneDocument& document,
        VansSceneEditService& edits, const std::string& root, const std::filesystem::path& directory,
        std::filesystem::path& createdPath);
    static SceneEditResult Place(VansSceneDocument& document, VansSceneEditService& edits,
        const std::string& assetGuid, VansSerializedValue placement, SceneEditLifecycleHooks hooks);
    static SceneEditResult Apply(EditorAPI::IEngineEditorAPI& api, VansSceneDocument& document, VansSceneEditService& edits, const std::string& root);
    static SceneEditResult Unpack(VansSceneDocument& document, VansSceneEditService& edits,
        const std::string& root);
    static SceneEditResult Revert(VansSceneDocument& document, VansSceneEditService& edits,
        const std::string& root, SceneEditLifecycleHooks hooks);
    static std::string SourceAsset(const VansSceneDocument& document, const std::string& root);
};
}
