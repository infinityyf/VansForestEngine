#include "VansSceneAssembly.h"
#include "../../SceneCore/VansSceneObjectBuildPlan.h"
#include "../../ScriptCore/VansScriptContext.h"
#include <unordered_map>
#include <unordered_set>
#include "../../RuntimeCore/VansThreadContract.h"

Vans::VansSceneEntityBatchResult VansGraphics::VansSceneAssembly::CreateEntityBatch(
	VansScene& scene,
	VkDevice& device,
	const Vans::VansSceneObjectBuildPlan& buildPlan,
	const std::string& projectRoot)
{
	return VansSceneAssembly(scene).CreateEntityBatchInternal(
		device, buildPlan, projectRoot);
}

Vans::VansSceneEntityBatchResult VansGraphics::VansSceneAssembly::CreateEntityBatchInternal(
    VkDevice& device,
    const Vans::VansSceneObjectBuildPlan& buildPlan,
    const std::string& projectRoot)
{
    VANS_ASSERT_MAIN_THREAD();
	Vans::VansSceneEntityBatchResult result;
	if (!m_Scene.IsSceneReady())
	{
		result.error = "Runtime entity batch requires a ready Scene";
		return result;
	}
	if (!m_RuntimeWorld ||
		!m_GameplayRuntime || !m_GameplayRuntime->IsInitialized() ||
		!m_TimelineRuntime || !m_AIWorld || !m_CameraControlArbiter)
	{
		result.error = "Runtime entity batch requires initialized Scene runtimes";
		return result;
	}
    std::unordered_set<std::string> identities;
	std::unordered_map<std::string, const Vans::VansSceneObjectBuildConfig*> batchObjects;
    for (const auto& object : buildPlan.objects)
    {
        if (object.entityGuid.empty() || m_Scene.FindObjectByGuid(object.entityGuid) ||
			!identities.insert(object.entityGuid).second)
		{
			result.error = "Runtime entity identity collision: " + object.entityGuid;
			return result;
		}
        const auto model = object.ResolveModelAssetGuid();
        if (!model.empty() && !m_Scene.GetMeshAsset(model))
		{
			result.error = "Runtime entity model has not been preloaded: " + model;
			return result;
		}
		batchObjects.emplace(object.entityGuid, &object);
    }
	if (identities.empty())
	{
		result.error = "Empty runtime entity batch";
		return result;
	}

	std::unordered_map<std::string, std::string> parentByChild;
	for (const auto& object : buildPlan.objects)
	{
		if (!object.parent)
			continue;
		if (!object.parent->IsValid())
		{
			result.error = "Runtime entity has an invalid parent: " + object.entityGuid;
			return result;
		}
		const std::string parentGuid = object.parent->entityGuid.ToString();
		if (parentGuid == object.entityGuid)
		{
			result.error = "Runtime entity cannot parent itself: " + object.entityGuid;
			return result;
		}
		const auto batchParent = batchObjects.find(parentGuid);
		if (batchParent == batchObjects.end())
		{
			VansScriptObject* parent = m_Scene.FindObjectByGuid(parentGuid);
			if (!parent || parent->m_TransformID == UINT32_MAX)
			{
				result.error = "Runtime entity parent is unavailable: " + parentGuid;
				return result;
			}
		}
		parentByChild.emplace(object.entityGuid, parentGuid);
	}
	for (const auto& [childGuid, ignored] : parentByChild)
	{
		std::unordered_set<std::string> path;
		std::string current = childGuid;
		while (batchObjects.count(current) > 0)
		{
			if (!path.insert(current).second)
			{
				result.error = "Runtime entity parent cycle contains: " + current;
				return result;
			}
			const auto parent = parentByChild.find(current);
			if (parent == parentByChild.end())
				break;
			current = parent->second;
		}
	}

    const VansSceneObjectBuildResult objectBuild =
        BuildObjectsInternal(device, buildPlan, projectRoot);
    bool success = objectBuild.m_Built;
    for (const auto& object : buildPlan.objects)
        success = (m_Scene.FindObjectByGuid(object.entityGuid) != nullptr) && success;
    if (!success)
    {
        for (auto it = buildPlan.objects.rbegin(); it != buildPlan.objects.rend(); ++it)
            if (auto* object = m_Scene.FindObjectByGuid(it->entityGuid))
				m_Scene.DestroyEntity(object);
		result.error = objectBuild.m_Error.empty()
			? "Runtime entity batch could not initialize its components"
			: objectBuild.m_Error;
		return result;
    }
	result.success = true;
	for (const auto& object : buildPlan.objects)
		result.entityGuids.push_back(object.entityGuid);
	return result;
}
