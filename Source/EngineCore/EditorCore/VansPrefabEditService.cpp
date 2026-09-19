#include "VansPrefabEditService.h"
#include "VansScenePropertyValueAdapter.h"
#include "VansEditorAssetSaveService.h"
#include "VansAssetDocumentRegistry.h"
#include "../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "../SceneCore/VansSceneDocument.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AssetCore/Serialization/VansAssetMetaJsonCodec.h"
#include <nlohmann/json.hpp>
#include <algorithm>

namespace Vans
{
namespace
{
using Json = nlohmann::ordered_json;
Json JsonOf(const VansSerializedValue& value) { return EncodeSerializedValueJson<Json>(value); }
int InstanceIndex(const VansSceneDocument& document, const std::string& root)
{
    const auto scene = JsonOf(document.AuthoringRootSnapshot());
    if (!scene.contains("prefabInstances")) return -1;
    const auto& instances = scene.at("prefabInstances");
    for (std::size_t i = 0; i < instances.size(); ++i)
    {
        const auto asset = document.PrefabLookup()(instances[i].at("asset"));
        if (asset && VansPrefabResolver::InstanceObjectGuid(DecodeSerializedValueJson(instances[i]), asset->rootEntity, false) == root)
            return static_cast<int>(i);
    }
    return -1;
}
}

VansPrefabLookup VansPrefabEditService::Lookup(EditorAPI::IEngineEditorAPI& api)
{
    return [&api](const std::string& guid) -> std::shared_ptr<const VansPrefabAsset>
    {
        auto asset = std::make_shared<VansPrefabAsset>(); std::string error;
        if (!VansPrefabCodec::Decode(ToSerializedValue(api.QueryPrefabAsset(guid)), *asset, error)) return {};
        return asset;
    };
}

SceneEditResult VansPrefabEditService::Create(EditorAPI::IEngineEditorAPI& api, VansSceneDocument& document,
    VansSceneEditService& edits, const std::string& root, const std::filesystem::path& directory,
    std::filesystem::path& createdPath)
{
    try
    {
        const auto browser = api.GetProjectBrowserRoot();
        if (!browser.projectLoaded) return {false, "Open a project before creating a prefab"};
        const auto assets = std::filesystem::weakly_canonical(std::filesystem::path(browser.assetsRootPath));
        const auto target = std::filesystem::weakly_canonical(directory);
        const auto relative = target.lexically_relative(assets);
        if (relative.empty() || relative.is_absolute() || !std::filesystem::is_directory(target))
            return {false, "Create prefabs inside the project Assets folder"};
        for (const auto& part : relative) if (part == "..")
            return {false, "Create prefabs inside the project Assets folder"};
        VansPrefabExtraction extraction; std::string error;
        const auto guid = VansAssetGuid::New();
        if (!VansPrefabResolver::Extract(document.SerializedRootSnapshot(), root, guid, extraction, error, document.PrefabLookup()))
            return {false, error};
        std::string name = "Prefab";
        for (const auto& entity : JsonOf(extraction.asset.entities))
            if (entity.at("id") == extraction.asset.rootEntity) name = entity.value("name", name);
        for (char& ch : name) if (static_cast<unsigned char>(ch) < 32 || std::string("<>:\"/\\|?*").find(ch) != std::string::npos) ch = '_';
        while (!name.empty() && (name.back() == '.' || name.back() == ' ')) name.pop_back();
        if (name.empty()) name = "Prefab";
        std::filesystem::path path;
        for (unsigned suffix = 0; ; ++suffix)
        {
            path = target / (name + (suffix ? " " + std::to_string(suffix) : "") + ".vprefab");
            if (!std::filesystem::exists(path) && !std::filesystem::exists(VansAssetMeta::MetaPathFor(path)) &&
                !VansAssetDocumentRegistry::Get().Find(path)) break;
        }
        // 候选文档不注册；只有文件提交、导入成功后才连接场景。
        auto candidate = std::make_shared<VansOpenAssetDocument>();
        candidate->sourcePath = path; candidate->metaPath = VansAssetMeta::MetaPathFor(path);
        VansAssetMeta meta; meta.guid = guid; meta.importer = "PrefabImporter";
        Json metadata;
        if (!VansAssetMetaJsonCodec::Encode(meta, metadata, error) ||
            !candidate->sourceDocument.InitializeNew(path, VansPrefabCodec::Encode(extraction.asset), error) ||
            !candidate->metaDocument.InitializeNew(candidate->metaPath, DecodeSerializedValueJson(metadata), error))
            return {false, error};
        const auto originalLookup = document.PrefabLookup();
        const auto temporaryLookup = [originalLookup, guid, asset = std::make_shared<const VansPrefabAsset>(extraction.asset)](const std::string& key)
            -> std::shared_ptr<const VansPrefabAsset>
        { return key == guid.ToString() ? asset : (originalLookup ? originalLookup(key) : nullptr); };
        VansSerializedValue validated;
        if (!VansPrefabResolver::CaptureScene(extraction.scene, temporaryLookup, validated, error)) return {false, error};
        const auto saved = VansEditorAssetSaveService::Get().SaveAsset(api, candidate);
        if (!saved) return {false, saved.message};
        if (!document.SetPrefabLookup(Lookup(api), error)) return {false, error};
        const auto connected = edits.ReplaceRoot(std::move(extraction.scene));
        if (!connected) return connected;
        createdPath = path;
        return {true, {}};
    }
    catch (const std::exception& e) { return {false, e.what()}; }
}

SceneEditResult VansPrefabEditService::Place(VansSceneDocument& document, VansSceneEditService& edits,
    const std::string& assetGuid, VansSerializedValue placement, SceneEditLifecycleHooks hooks)
{
    VansAssetGuid guid;
    if (!VansAssetGuid::TryParse(assetGuid, guid)) return {false, "Invalid prefab GUID"};
    auto instance = JsonOf(VansPrefabResolver::MakeInstance(guid));
    instance["placement"] = JsonOf(placement);
    auto authoring = JsonOf(document.AuthoringRootSnapshot());
    if (!authoring.contains("prefabInstances")) authoring["prefabInstances"] = Json::array();
    authoring["prefabInstances"].push_back(instance);
    VansSerializedValue resolved; std::string error;
    if (!VansPrefabResolver::ResolveScene(DecodeSerializedValueJson(authoring), document.PrefabLookup(), resolved, error))
        return {false, error};
    return edits.ReplaceRoot(std::move(resolved), std::move(hooks));
}

std::string VansPrefabEditService::SourceAsset(const VansSceneDocument& document, const std::string& root)
{
    const int index = InstanceIndex(document, root);
    return index < 0 ? std::string{} : JsonOf(document.AuthoringRootSnapshot())["prefabInstances"][index]["asset"].get<std::string>();
}

SceneEditResult VansPrefabEditService::Apply(EditorAPI::IEngineEditorAPI& api, VansSceneDocument& document,
    VansSceneEditService& edits, const std::string& root)
{
    const int index = InstanceIndex(document, root);
    if (index < 0) return {false, "Select a prefab instance root"};
    auto scene = JsonOf(document.AuthoringRootSnapshot());
    const auto guid = scene["prefabInstances"][index]["asset"].get<std::string>();
    const auto asset = document.PrefabLookup()(guid);
    if (!asset) return {false, "Prefab source unavailable"};
    VansPrefabAsset updated; VansSerializedValue instance; std::string error;
    if (!VansPrefabResolver::Apply(*asset, DecodeSerializedValueJson(scene["prefabInstances"][index]), updated, instance, error))
        return {false, error};
    scene["prefabInstances"][index] = JsonOf(instance);
    for (const auto& entry : api.QueryAssets({EditorAPI::AssetType::Prefab}))
    {
        if (entry.guid != guid) continue;
        auto source = VansAssetDocumentRegistry::Get().GetOrOpen(entry.relativePath);
        if (!source || !source->sourceDocument.IsLoaded()) return {false, "Prefab authoring document unavailable"};
        const auto result = edits.ApplyPrefab(source, VansPrefabCodec::Encode(updated), DecodeSerializedValueJson(scene));
        if (result) source->saveWithScene = true;
        return result;
    }
    return {false, "Prefab source path is not in the project asset database"};
}

SceneEditResult VansPrefabEditService::Unpack(VansSceneDocument& document, VansSceneEditService& edits, const std::string& root)
{
    const int index = InstanceIndex(document, root);
    if (index < 0) return {false, "Select a prefab instance root"};
    auto scene = JsonOf(document.SerializedRootSnapshot());
    scene["prefabInstances"].erase(index);
    return edits.ReplaceRoot(DecodeSerializedValueJson(scene));
}

SceneEditResult VansPrefabEditService::Revert(VansSceneDocument& document, VansSceneEditService& edits,
    const std::string& root, SceneEditLifecycleHooks hooks)
{
    const int index = InstanceIndex(document, root);
    if (index < 0) return {false, "Select a prefab instance root"};
    auto scene = JsonOf(document.AuthoringRootSnapshot());
    scene["prefabInstances"][index]["overrides"] = Json::array();
    VansSerializedValue resolved; std::string error;
    if (!VansPrefabResolver::ResolveScene(DecodeSerializedValueJson(scene), document.PrefabLookup(), resolved, error))
        return {false, error};
    return edits.ReplaceRoot(std::move(resolved), std::move(hooks));
}
}
