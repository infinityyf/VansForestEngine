#include "VansSceneResourceBatchExecutor.h"

#include "../VansScene.h"
#include "VansSceneProjectResourceBuilder.h"
#include "../VansShaderManager.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../../Configration/VansConfigration.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../SceneCore/VansSceneAssetDependencyBuilder.h"
#include "../../Util/VansLog.h"

namespace VansGraphics
{
void VansSceneResourceBatchExecutor::LoadEngineMeshes(VansScene& scene, const std::string& enginePrefix, VansVKDevice* vkDevice)
{
	VkDevice nativeDevice = vkDevice->GetLogicDevice();
	const std::vector<Vans::VansSceneMeshResourceRequest> engineMeshes = {
		{
			"fullScreenQuad",
			"EngineAssets/Models/fullscreen.obj",
			false,
			false,
			false
		},
		{
			"plane",
			"EngineAssets/Models/plane.obj",
			true,
			true,
			false
		}
	};
	VansSceneProjectResourceBuilder::LoadMeshes(scene, engineMeshes, enginePrefix, nativeDevice, vkDevice);
}

void VansSceneResourceBatchExecutor::LoadLegacyAudioVideoShaderBatches(
	VansScene& scene,
	nlohmann::json& resourceData,
	const std::string& assetPrefix)
{
	if (resourceData.contains("video") && resourceData["video"].is_array())
	{
		scene.LoadProjectVideoResourcesFromJson(resourceData["video"], assetPrefix);
	}

	if (resourceData.contains("audio") && resourceData["audio"].is_array())
	{
		scene.LoadProjectAudioResourcesFromJson(resourceData["audio"], assetPrefix);
	}
}

void VansSceneResourceBatchExecutor::FinalizeResourceBatch(VansScene& scene)
{
	scene.FinalizeProjectResourceBatch();
}

void VansSceneResourceBatchExecutor::Execute(VansScene& scene, const Vans::VansSceneResourceBuildPlan& resourcePlan)
{
	auto vansConfigration = VansConfigration::GetInstance();
	std::string enginePrefix = vansConfigration->GetProjectRootPath();

	auto& projectMgr = Vans::VansProjectManager::Get();
	std::string assetPrefix = projectMgr.IsProjectLoaded()
		? projectMgr.GetProjectRootPath()
		: enginePrefix;

	VansVKDevice* vkDevice = scene.GetRuntimeResourceDevice();
	if (!vkDevice)
	{
		VANS_LOG_ERROR("[VansScene] Cannot load resources without a runtime Vulkan device");
		return;
	}
	VkDevice nativeDevice = vkDevice->GetLogicDevice();

	// Renderer-owned geometry never depends on project assets.
	LoadEngineMeshes(scene, enginePrefix, vkDevice);
	VansSceneProjectResourceBuilder::LoadMeshes(scene, resourcePlan.meshes, assetPrefix, nativeDevice, vkDevice);
	VansSceneProjectResourceBuilder::LoadTextures(scene, resourcePlan.textures, assetPrefix, enginePrefix, vkDevice);
	scene.LoadProjectAudioResources(resourcePlan.audios, assetPrefix);
	scene.LoadProjectVideoResources(resourcePlan.videos, assetPrefix);
	VansSceneProjectResourceBuilder::RegisterShaders(scene, resourcePlan.shaders, assetPrefix, nativeDevice);
	FinalizeResourceBatch(scene);
}

void VansSceneResourceBatchExecutor::Execute(VansScene& scene, nlohmann::json& resourceData)
{
	auto vansConfigration = VansConfigration::GetInstance();
	std::string enginePrefix = vansConfigration->GetProjectRootPath();

	auto& projectMgr = Vans::VansProjectManager::Get();
	std::string assetPrefix = projectMgr.IsProjectLoaded()
		? projectMgr.GetProjectRootPath()
		: enginePrefix;

	VansVKDevice* vkDevice = scene.GetRuntimeResourceDevice();
	if (!vkDevice)
	{
		VANS_LOG_ERROR("[VansScene] Cannot load resources without a runtime Vulkan device");
		return;
	}
	VkDevice nativeDevice = vkDevice->GetLogicDevice();

	// Legacy JSON entry point keeps the old mesh/texture batch behaviour.
	LoadEngineMeshes(scene, enginePrefix, vkDevice);
	if (resourceData.contains("mesh") && resourceData["mesh"].is_array())
	{
		VansSceneProjectResourceBuilder::LoadMeshesFromJson(scene, resourceData["mesh"], assetPrefix, nativeDevice, vkDevice);
	}
	if (resourceData.contains("texture") && resourceData["texture"].is_array())
	{
		VansSceneProjectResourceBuilder::LoadTexturesFromJson(scene, resourceData["texture"], assetPrefix, enginePrefix, vkDevice);
	}
	if (resourceData.contains("shader") && resourceData["shader"].is_array())
	{
		VansSceneProjectResourceBuilder::RegisterShadersFromJson(scene, resourceData["shader"], assetPrefix, nativeDevice);
	}

	LoadLegacyAudioVideoShaderBatches(scene, resourceData, assetPrefix);
	FinalizeResourceBatch(scene);
}
}
