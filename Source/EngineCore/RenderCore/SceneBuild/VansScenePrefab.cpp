#include "../VansScene.h"
#include "../../SceneCore/Prefab/VansPrefabAsset.h"
#include "../../SceneCore/VansSceneRuntimeProjection.h"
#include "../../SceneCore/VansSceneContentBuildPlan.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../ScriptCore/VansScriptContext.h"
#include <unordered_set>
#include "../../RuntimeCore/VansThreadContract.h"

bool VansGraphics::VansScene::CreateSceneEntityBatch(VkDevice& device,
    const Vans::VansSerializedValue& entities, const std::string& projectRoot,
    std::vector<std::string>& created, std::string& error)
{
    VANS_ASSERT_MAIN_THREAD();
    created.clear(); error.clear();
    Vans::VansSceneContentBuildPlan plan;
    if (!Vans::VansSceneRuntimeProjection::BuildRuntimeSceneEntityPlan(entities, projectRoot, plan, error)) return false;
    std::unordered_set<std::string> identities;
    for (const auto& object : plan.objects.objects)
    {
        if (object.entityGuid.empty() || FindObjectByGuid(object.entityGuid) || !identities.insert(object.entityGuid).second)
        { error = "Runtime entity identity collision: " + object.entityGuid; return false; }
        const auto model = object.ResolveModelAssetGuid();
        if (!model.empty() && !GetMeshAsset(model))
        { error = "Prefab model has not been preloaded: " + model; return false; }
    }
    if (identities.empty()) { error = "Empty runtime entity batch"; return false; }
    bool success = LoadSceneObjects(device, plan.objects, projectRoot);
    for (const auto& object : plan.objects.objects)
        success = (FindObjectByGuid(object.entityGuid) != nullptr) && success;
    if (!success)
    {
        for (auto it = plan.objects.objects.rbegin(); it != plan.objects.objects.rend(); ++it)
            if (auto* object = FindObjectByGuid(it->entityGuid)) DestroyEntity(object);
        error = "Runtime entity batch could not initialize its components";
        return false;
    }
    for (const auto& object : plan.objects.objects) created.push_back(object.entityGuid);
    return true;
}

bool VansGraphics::VansScene::InstantiatePrefab(VkDevice& device, const std::string& assetGuid,
    const Vans::VansSerializedValue& placement, std::string& rootEntity, std::string& error)
{
    rootEntity.clear();
    Vans::VansAssetGuid guid;
    if (!Vans::VansAssetGuid::TryParse(assetGuid, guid)) { error = "Invalid prefab asset GUID"; return false; }
    auto& project = Vans::VansProjectManager::Get();
    const auto asset = project.GetAssetObjectRepository().ResolveLatest<Vans::VansPrefabAsset>(guid);
    if (!asset) { error = "Prefab asset is not loaded: " + assetGuid; return false; }
    auto instance = Vans::VansPrefabResolver::MakeInstance(guid);
    Vans::SetSerializedObjectField(instance, "placement", placement);
    Vans::VansSerializedValue entities;
    if (!Vans::VansPrefabResolver::Instantiate(*asset, instance, entities, error)) return false;
    std::vector<std::string> created;
    if (!CreateSceneEntityBatch(device, entities, project.GetProjectRootPath(), created, error)) return false;
    rootEntity = Vans::VansPrefabResolver::InstanceObjectGuid(instance, asset->rootEntity, false);
    return true;
}
