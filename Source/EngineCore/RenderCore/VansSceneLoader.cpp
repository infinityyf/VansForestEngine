#include "VansScene.h"
#include "SceneBuild/VansSceneAnimationComponentBuilder.h"
#include "SceneBuild/VansSceneCameraMediaComponentBuilder.h"
#include "SceneBuild/VansSceneClothAnimationBindingExecutor.h"
#include "SceneBuild/VansSceneContentBuildExecutor.h"
#include "SceneBuild/VansSceneEnvironmentNodeBuilder.h"
#include "SceneBuild/VansSceneLightComponentBuilder.h"
#include "SceneBuild/VansSceneLoadPass.h"
#include "SceneBuild/VansSceneMaterialBuilder.h"
#include "SceneBuild/VansSceneParticleComponentBuilder.h"
#include "SceneBuild/VansSceneProjectResourceBuilder.h"
#include "SceneBuild/VansScenePhysicsComponentBuilder.h"
#include "SceneBuild/VansSceneRenderNodeBuilder.h"
#include "SceneBuild/VansSceneRenderPreparationExecutor.h"
#include "SceneBuild/VansSceneResourceBatchExecutor.h"
#include "SceneBuild/VansSceneScriptComponentBuilder.h"
#include "SceneBuild/VansSceneVehicleComponentBuilder.h"
#include "VansShaderManager.h"
#include "BRDFData/VansLight.h"
#include "../Configration/VansConfigration.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../AssetCore/VansAssetDatabase.h"
#include "../SceneCore/VansSceneAssetDependencyBuilder.h"
#include "../ScriptCore/VansScriptContext.h"
#include "../PhysicsCore/VansPhysics.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../PhysicsCore/VansCharacterControllerNode.h"
#include "../PhysicsCore/VansTerrainPhysicsNode.h"
#include "../PhysicsCore/VansRagdollSystem.h"
#include "VansVideoManager.h"
#include "../AudioCore/VansAudioManager.h"
#include "../AudioCore/VansAudioSystem.h"

#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "VulkanCore/VansRenderPass.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "TerrainCore/VansTerrain.h"
#include "VegetationCore/VansVegetationSystem.h"
#include "WaterCore/VansWaterMaterial.h"
#include "WaterCore/VansWaterSystem.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../AnimationCore/VansAnimationController.h"
#include "../AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../AnimationCore/VansAnimatorIO.h"
#include "../AnimationCore/VansAnimGraph.h"
#include "../AnimationCore/VansAnimationClipLoader.h"
#include "../AnimationCore/VansBoneAttachmentSystem.h"
#include "../AnimationCore/VansSkinnedMeshLoader.h"
#include "../AnimationCore/FootPlacement/VansFootPlacementTypes.h"

#include "../Util/VansLog.h"
#include "../RuntimeCore/VansThreadContract.h"
#include <iostream>
#include <algorithm>
#include <array>
#include <cfloat>
#include <cmath>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <mutex>
#include <cctype>
#include <random>
#include <glm/gtx/quaternion.hpp>

namespace VansGraphics
{
bool VansGraphics::VansScene::LoadProjectAssets(Vans::VansAssetDatabase& database,
    const std::filesystem::path& scenePath, VansVKDevice* device)
{
    VANS_ASSERT_MAIN_THREAD();
    if (device == nullptr)
    {
        VANS_LOG_ERROR("[VansScene] Cannot load project assets without a Vulkan device");
        return false;
    }
    VANS_LOG("[VansScene] Loading project assets from AssetDatabase: " << database.AssetsRoot().string());
    m_RuntimeResourceDevice = device;
	m_AssetRegistry.ClearProjectMeshAliases();
	try
	{
	const Vans::VansAssetScanResult scanResult = database.Scan();
	if (!scanResult)
	{
		for (const std::string& error : scanResult.errors)
			VANS_LOG_ERROR("[AssetDatabase] Scan error: " << error);
	}
	else
	{
		VANS_LOG("[AssetDatabase] Scan refreshed " << scanResult.registered
			<< " assets, generated " << scanResult.generatedMeta << " meta files");
	}

	const Vans::VansSceneAssetDependencyBuildResult assetBatch =
		Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
			database,
			scenePath,
			Vans::VansProjectManager::Get().GetConfig().runtimeAssetBindings);
	if (!assetBatch.success)
		return false;

    m_VideoManager.Clear();
    m_AudioManager.Clear();
	VANS_LOG("[AssetDatabase] Uploading dependency closure: " << assetBatch.resourcePlan.meshes.size()
		<< " models, " << assetBatch.resourcePlan.textures.size() << " textures");

    // Engine shaders are registered during InitializeGraphicsSystem().
    VansSceneResourceBatchExecutor::Execute(*this, assetBatch.resourcePlan);
	for (const auto& [alias, guid] : Vans::VansProjectManager::Get().GetConfig().runtimeAssetBindings)
	{
		if (alias == "fullScreenQuad" || alias == "plane")
			continue;
		if (VansAsset* asset = GetMeshAsset(guid))
			SetProjectMeshAlias(alias, asset);
		else
			VANS_LOG_ERROR("[AssetDatabase] Runtime mesh binding '" << alias << "' references missing guid " << guid);
	}
    m_ResourcesLoaded = true;
    VANS_LOG("[VansScene] AssetDatabase project resources loaded");
	VANS_LOG("[AssetDatabase] Dependency closure: " << assetBatch.requiredModels.size() << " models, "
		<< assetBatch.requiredMaterials.size() << " materials, " << assetBatch.requiredTextures.size() << " textures");
	return true;
	}
	catch (const std::exception& error)
	{
		m_ResourcesLoaded = false;
		VANS_LOG_ERROR("[AssetDatabase] Project asset loading failed: " << error.what());
		return false;
	}
	catch (...)
	{
		m_ResourcesLoaded = false;
		VANS_LOG_ERROR("[AssetDatabase] Project asset loading failed with an unknown exception");
		return false;
	}
}

void VansGraphics::VansScene::LoadSceneForRendering(const char* scenePath, VansVKDevice* device, VansSceneLoadMode mode)
{
    VANS_ASSERT_MAIN_THREAD();

    VANS_LOG("[VansScene] LoadSceneForRendering: " << scenePath);
    m_RuntimeResourceDevice = device;

    if (m_SceneState == VansSceneState::Ready)
    {
        m_SceneState = VansSceneState::Unloading;
        VANS_LOG("[VansScene] Unloading previous scene...");

        device->WaitForDevice();
        UnLoadScene();

        m_SceneState = VansSceneState::Empty;
        VANS_LOG("[VansScene] Previous scene unloaded");
    }

    if (!m_ResourcesLoaded)
    {
        VANS_LOG_ERROR("[VansScene] LoadSceneForRendering called before project assets were loaded");
        return;
    }

    m_SceneState = VansSceneState::Loading;
    VANS_LOG("[VansScene] Loading scene: " << scenePath);

    if (!VansSceneContentBuildExecutor::BuildFromFile(*this, scenePath))
    {
        VANS_LOG_ERROR("[VansScene] Scene content build failed, unloading partially built scene");
        m_SceneState = VansSceneState::Unloading;
        device->WaitForDevice();
        UnLoadScene();
        m_SceneState = VansSceneState::Empty;
        return;
    }

    VansSceneRenderPreparationExecutor::PrepareAfterSceneContentLoaded(*this, *device);

    m_LoadMode = mode;
    m_SceneState = VansSceneState::Ready;
    VANS_LOG("[VansScene] Scene ready for rendering");
}
// ===========================================================================
// Single render node loading (extracted from LoadRenderNodes loop body)

// ===========================================================================
// Resource loading (meshes, shaders, textures, materials)
// ===========================================================================

bool VansGraphics::VansScene::LoadSceneContent(const char* path)
{
    return VansSceneContentBuildExecutor::BuildFromFile(*this, path);
}

VansTexture* VansGraphics::VansScene::LoadOrGetTexture(const std::string& absPath, bool isSRGB)
{
    return VansSceneProjectResourceBuilder::LoadOrGetTexture(*this, absPath, isSRGB);
}

VansScriptObject* VansGraphics::VansScene::FindObjectByName(const std::string& name) const
{
    for (auto* obj : m_SceneObjects)
    {
        if (obj && obj->m_ObjectName == name)
            return obj;
    }
    return nullptr;
}

VansScriptObject* VansGraphics::VansScene::FindObjectByGuid(const std::string& guid) const
{
    if (guid.empty()) return nullptr;
    for (auto* obj : m_SceneObjects)
    {
        if (obj && obj->m_EntityGuid == guid)
            return obj;
    }
    return nullptr;
}

VansGraphics::VansRenderNode* VansGraphics::VansScene::FindPrimaryRenderNodeByEntityGuid(const std::string& guid) const
{
    VansScriptObject* obj = FindObjectByGuid(guid);
    if (!obj) return nullptr;
    if (auto* render = obj->GetComponent<VansScriptRenderComponent>())
        return render->m_RenderNode;
    return nullptr;
}










}

