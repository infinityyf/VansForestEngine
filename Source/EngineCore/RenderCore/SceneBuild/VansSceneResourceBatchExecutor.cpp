#include "VansSceneResourceBatchExecutor.h"

#include "../VansScene.h"
#include "VansSceneProjectResourceBuilder.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../../Configration/VansConfigration.h"
#include "../../ProjectSystem/VansProjectManager.h"
#include "../../SceneCore/VansSceneResourcePlan.h"
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
	if (resourcePlan.includeDefaultTextureSet || !resourcePlan.textures.empty())
	{
		VansSceneProjectResourceBuilder::LoadTextures(
			scene,
			resourcePlan.textures,
			assetPrefix,
			enginePrefix,
			vkDevice,
			resourcePlan.includeDefaultTextureSet);
	}
	scene.LoadProjectAudioResources(resourcePlan.audios, assetPrefix);
	scene.LoadProjectVideoResources(resourcePlan.videos, assetPrefix);
	if (resourcePlan.loadRegisteredShaders || !resourcePlan.shaders.empty())
	{
		VansSceneProjectResourceBuilder::RegisterShaders(
			scene,
			resourcePlan.shaders,
			assetPrefix,
			nativeDevice,
			resourcePlan.loadRegisteredShaders);
	}
	FinalizeResourceBatch(scene);
}

}
