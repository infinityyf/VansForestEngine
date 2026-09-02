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
#include "../AssetCore/VansBuiltInAssetCatalog.h"
#include "../SceneCore/VansPackagedResourcePlan.h"
#include "../SceneCore/VansSceneAssetDependencyBuilder.h"
#include "../SceneCore/VansSceneResourceLoadContext.h"
#include "../ScriptCore/VansScriptContext.h"
#include "../PhysicsCore/VansPhysics.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../PhysicsCore/VansCharacterControllerNode.h"
#include "../PhysicsCore/VansTerrainPhysicsNode.h"
#include "../PhysicsCore/VansRagdollSystem.h"
#include "VansVideoManager.h"
#include "../AudioCore/VansAudioManager.h"
#include "../AudioCore/VansAudioMixConfig.h"
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
#include "../AnimationCore/VansSkinnedMeshLoader.h"

#include "../Util/VansLog.h"
#include "../Util/VansProfiler.h"
#include "../RuntimeCore/VansThreadContract.h"
#include <iostream>
#include <algorithm>
#include <array>
#include <chrono>
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
namespace
{
	using SceneLoadClock = std::chrono::steady_clock;

	enum class SceneGpuMaintenanceOperation
	{
		WaitIdle,
		RebuildRenderer,
		PrepareScene
	};

	class SceneGpuMaintenanceTransaction final
		: public IVansRenderThreadTransaction
	{
	public:
		SceneGpuMaintenanceTransaction(
			VansScene& scene,
			VansVKDevice& expectedDevice,
			SceneGpuMaintenanceOperation operation)
			: m_Scene(scene),
			  m_ExpectedDevice(expectedDevice),
			  m_Operation(operation) {}

		bool Execute(VansGraphicsDevice& backend) override
		{
			VANS_ASSERT_RENDER_THREAD();
			auto* device = dynamic_cast<VansVKDevice*>(&backend);
			if (device != &m_ExpectedDevice || !backend.WaitForIdle())
				return false;
			if (m_Operation == SceneGpuMaintenanceOperation::RebuildRenderer)
				device->PrepareRenderingData();
			else if (m_Operation == SceneGpuMaintenanceOperation::PrepareScene)
				VansSceneRenderPreparationExecutor::PrepareAfterSceneContentLoaded(
					m_Scene, *device);
			return true;
		}

	private:
		VansScene& m_Scene;
		VansVKDevice& m_ExpectedDevice;
		SceneGpuMaintenanceOperation m_Operation;
	};

	bool ExecuteSceneGpuMaintenance(
		VansScene& scene,
		VansVKDevice& device,
		SceneGpuMaintenanceOperation operation)
	{
		return scene.ExecuteRenderThreadTransaction(
			std::make_unique<SceneGpuMaintenanceTransaction>(
				scene, device, operation));
	}

	double SceneLoadMsSince(SceneLoadClock::time_point start)
	{
		return std::chrono::duration<double, std::milli>(SceneLoadClock::now() - start).count();
	}

	void LogSceneLoadPhase(const char* phase, SceneLoadClock::time_point start)
	{
		VANS_LOG("[SceneLoadProfile] " << phase << "=" << SceneLoadMsSince(start) << "ms");
	}

	bool BindBuiltInRuntimeAssets(VansScene& scene, const char* context)
	{
		for (const Vans::VansBuiltInAssetEntry& entry : Vans::VansBuiltInAssetCatalog::Entries())
		{
			switch (entry.type)
			{
			case Vans::VansAssetType::Model:
				if (VansAsset* asset = scene.FindMeshAsset(entry.guid))
				{
					scene.SetProjectMeshAlias(entry.runtimeAlias, asset);
					break;
				}
				VANS_LOG_ERROR("[BuiltInAssetDatabase] " << context << " mesh alias '"
					<< entry.runtimeAlias << "' references missing guid " << entry.guid);
				return false;

			case Vans::VansAssetType::Texture:
				// 内建纹理的 resource request 直接以 runtimeAlias 注册到纹理表。
				if (scene.GetTextureAsset(entry.runtimeAlias) != nullptr)
					break;
				VANS_LOG_ERROR("[BuiltInAssetDatabase] " << context << " texture alias '"
					<< entry.runtimeAlias << "' references missing guid " << entry.guid);
				return false;

			default:
				VANS_LOG_ERROR("[BuiltInAssetDatabase] " << context
					<< " has unsupported runtime asset type for alias '"
					<< entry.runtimeAlias << "'");
				return false;
			}
		}
		return true;
	}

	void ApplyProjectAudioMixConfig(VansScene& scene)
	{
		const Vans::VansProjectConfig& projectConfig =
			Vans::VansProjectManager::Get().GetConfig();
		if (projectConfig.audioSettings.empty())
			return;

		std::filesystem::path mixPath = std::filesystem::path(projectConfig.audioSettings);
		if (mixPath.is_relative())
			mixPath = std::filesystem::path(Vans::VansProjectManager::Get().GetProjectRootPath()) / mixPath;

		std::error_code ec;
		if (!std::filesystem::is_regular_file(mixPath, ec))
		{
			VANS_LOG_WARN("[AudioMix] Project audio mix config not found: " << mixPath.string());
			return;
		}

		VansEngine::AudioMixConfig mixConfig;
		std::string error;
		if (!VansEngine::VansAudioMixConfigStorage::Load(mixPath, mixConfig, error))
		{
			VANS_LOG_ERROR("[AudioMix] Cannot load project audio mix config: "
				<< mixPath.string() << " (" << error << ")");
			return;
		}

		if (VansEngine::VansAudioManager* audioManager = scene.GetAudioManager())
		{
			audioManager->ApplyMixConfig(mixConfig);
			VANS_LOG("[AudioMix] Applied project audio mix config: " << mixPath.string());
		}
	}
}

bool VansGraphics::VansScene::LoadProjectAssets(Vans::VansAssetDatabase& database,
    const std::filesystem::path& scenePath, VansVKDevice* device)
{
	VANS_PROFILE_SCOPE("SceneLoad.LoadProjectAssets", Vans::ProfileCategory::IO);
	const auto totalStart = SceneLoadClock::now();
    VANS_ASSERT_MAIN_THREAD();
    if (device == nullptr)
    {
        VANS_LOG_ERROR("[VansScene] Cannot load project assets without a Vulkan device");
        return false;
    }
    VANS_LOG("[VansScene] Loading project assets from AssetDatabase: " << database.AssetsRoot().string());
    m_RuntimeResourceDevice = device;
	m_UsingPackagedProjectAssets = false;
	m_AssetRegistry.ClearProjectMeshAliases();
	try
	{
	auto phaseStart = SceneLoadClock::now();
	const Vans::VansAssetScanResult scanResult = database.Scan(Vans::VansAssetOperationPolicy::ReadOnly());
	LogSceneLoadPhase("projectAssets.scan", phaseStart);
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

	phaseStart = SceneLoadClock::now();
	const Vans::VansSceneAssetDependencyBuildResult assetBatch =
		Vans::VansSceneAssetDependencyBuilder::BuildResourcePlan(
			database,
			scenePath,
			Vans::VansProjectManager::Get().GetConfig().runtimeAssetBindings,
			Vans::VansProjectManager::Get().GetBuiltInAssetDatabase());
	LogSceneLoadPhase("projectAssets.dependencyPlan", phaseStart);
	if (!assetBatch.success)
		return false;

	phaseStart = SceneLoadClock::now();
    ReleaseAudioSourceBindings();
    m_VideoManager.Clear();
    m_AudioManager.Clear();
	LogSceneLoadPhase("projectAssets.clearMediaManagers", phaseStart);
	VANS_LOG("[AssetDatabase] Uploading dependency closure: " << assetBatch.resourcePlan.meshes.size()
		<< " models, " << assetBatch.resourcePlan.textures.size() << " textures");

    // Engine shaders are registered during InitializeGraphicsSystem().
	phaseStart = SceneLoadClock::now();
	if (!VansSceneResourceBatchExecutor::Execute(*this, assetBatch.resourcePlan))
		return false;
	LogSceneLoadPhase("projectAssets.resourceBatch", phaseStart);

	phaseStart = SceneLoadClock::now();
	ApplyProjectAudioMixConfig(*this);
	LogSceneLoadPhase("projectAssets.audioMix", phaseStart);

	phaseStart = SceneLoadClock::now();
	if (!BindBuiltInRuntimeAssets(*this, "editor runtime"))
		return false;
	for (const auto& [alias, guid] : Vans::VansProjectManager::Get().GetConfig().runtimeAssetBindings)
	{
		if (Vans::VansBuiltInAssetCatalog::IsReservedRuntimeAlias(alias))
			continue;
		if (VansAsset* asset = GetMeshAsset(guid))
			SetProjectMeshAlias(alias, asset);
		else
			VANS_LOG_ERROR("[AssetDatabase] Runtime mesh binding '" << alias << "' references missing guid " << guid);
	}
	LogSceneLoadPhase("projectAssets.runtimeBindings", phaseStart);
    m_ResourcesLoaded = true;
    VANS_LOG("[VansScene] AssetDatabase project resources loaded");
	VANS_LOG("[AssetDatabase] Dependency closure: " << assetBatch.requiredModels.size() << " models, "
		<< assetBatch.requiredMaterials.size() << " materials, " << assetBatch.requiredTextures.size() << " textures");
	LogSceneLoadPhase("projectAssets.total", totalStart);
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

bool VansGraphics::VansScene::LoadPackagedProjectAssets(
	const Vans::VansPackagedResourcePlan& packagePlan,
	VansVKDevice* device)
{
	VANS_PROFILE_SCOPE("SceneLoad.LoadPackagedProjectAssets", Vans::ProfileCategory::IO);
	const auto totalStart = SceneLoadClock::now();
	VANS_ASSERT_MAIN_THREAD();
	if (device == nullptr)
	{
		VANS_LOG_ERROR("[VansScene] Cannot load packaged project assets without a Vulkan device");
		return false;
	}

	m_RuntimeResourceDevice = device;
	m_UsingPackagedProjectAssets = true;
	m_AssetRegistry.ClearProjectMeshAliases();

	try
	{
		auto phaseStart = SceneLoadClock::now();
		ReleaseAudioSourceBindings();
		m_VideoManager.Clear();
		m_AudioManager.Clear();
		LogSceneLoadPhase("packagedProjectAssets.clearMediaManagers", phaseStart);

		VANS_LOG("[PackageResourcePlan] Loading packaged dependency closure: "
			<< packagePlan.resourcePlan.meshes.size() << " models, "
			<< packagePlan.resourcePlan.textures.size() << " textures");

		phaseStart = SceneLoadClock::now();
		auto config = VansConfigration::GetInstance();
		const Vans::VansSceneResourceLoadContext loadContext =
			Vans::VansSceneResourceLoadContext::ForPackagedRuntime(
				Vans::VansProjectManager::Get().GetProjectRootPath(),
				config ? config->GetProjectRootPath() : Vans::VansProjectManager::Get().GetProjectRootPath(),
				Vans::VansProjectManager::Get().EnumerateAssetRecords());
		if (!VansSceneResourceBatchExecutor::Execute(*this, packagePlan.resourcePlan, loadContext))
			return false;
		LogSceneLoadPhase("packagedProjectAssets.resourceBatch", phaseStart);

		phaseStart = SceneLoadClock::now();
		ApplyProjectAudioMixConfig(*this);
		LogSceneLoadPhase("packagedProjectAssets.audioMix", phaseStart);

		phaseStart = SceneLoadClock::now();
		if (!BindBuiltInRuntimeAssets(*this, "packaged runtime"))
			return false;
		for (const auto& [alias, guid] : packagePlan.runtimeAssetBindings)
		{
			if (Vans::VansBuiltInAssetCatalog::IsReservedRuntimeAlias(alias))
				continue;
			if (VansAsset* asset = GetMeshAsset(guid))
				SetProjectMeshAlias(alias, asset);
			else
				VANS_LOG_ERROR("[PackageResourcePlan] Runtime mesh binding '" << alias
					<< "' references missing guid " << guid);
		}
		LogSceneLoadPhase("packagedProjectAssets.runtimeBindings", phaseStart);

		m_ResourcesLoaded = true;
		VANS_LOG("[PackageResourcePlan] Packaged project resources loaded");
		LogSceneLoadPhase("packagedProjectAssets.total", totalStart);
		return true;
	}
	catch (const std::exception& error)
	{
		m_ResourcesLoaded = false;
		VANS_LOG_ERROR("[PackageResourcePlan] Project asset loading failed: " << error.what());
		return false;
	}
	catch (...)
	{
		m_ResourcesLoaded = false;
		VANS_LOG_ERROR("[PackageResourcePlan] Project asset loading failed with an unknown exception");
		return false;
	}
}

bool VansGraphics::VansScene::LoadSceneForRendering(const char* scenePath, VansVKDevice* device, VansSceneLoadMode mode)
{
    VANS_ASSERT_MAIN_THREAD();

    if (scenePath == nullptr || scenePath[0] == '\0')
    {
        VANS_LOG_ERROR("[VansScene] LoadSceneForRendering requires a non-empty scene path");
        return false;
    }
    if (device == nullptr)
    {
        VANS_LOG_ERROR("[VansScene] LoadSceneForRendering requires a Vulkan device");
        return false;
    }
    if (!m_ResourcesLoaded)
    {
        VANS_LOG_ERROR("[VansScene] LoadSceneForRendering called before project assets were loaded");
        return false;
    }

    VANS_LOG("[VansScene] LoadSceneForRendering: " << scenePath);
    m_RuntimeResourceDevice = device;
	device->RequestUpscalerHistoryReset(VansUpscalerResetReason::SceneChange);

    bool rebuildRenderingDataAfterUnload = false;
    if (m_SceneState == VansSceneState::Ready)
    {
        m_SceneState = VansSceneState::Unloading;
        VANS_LOG("[VansScene] Unloading previous scene...");

        if (!ExecuteSceneGpuMaintenance(
			*this, *device, SceneGpuMaintenanceOperation::WaitIdle))
		{
			VANS_LOG_ERROR("[VansScene] Render thread failed to idle before scene unload");
			m_SceneState = VansSceneState::Ready;
			return false;
		}
        UnLoadScene();

        m_SceneState = VansSceneState::Empty;
        VANS_LOG("[VansScene] Previous scene unloaded");
        rebuildRenderingDataAfterUnload = true;
    }

	m_SceneState = VansSceneState::Loading;
	m_LoadMode = mode;
	VANS_LOG("[VansScene] Loading scene: " << scenePath);

    if (rebuildRenderingDataAfterUnload)
    {
        VANS_LOG("[VansScene] Rebuilding renderer data after scene unload");
        if (!ExecuteSceneGpuMaintenance(
			*this, *device, SceneGpuMaintenanceOperation::RebuildRenderer))
		{
			VANS_LOG_ERROR("[VansScene] Render-thread renderer rebuild failed");
			m_SceneState = VansSceneState::Empty;
			return false;
		}
    }

    if (!VansSceneContentBuildExecutor::BuildFromFile(*this, scenePath))
    {
        VANS_LOG_ERROR("[VansScene] Scene content build failed, unloading partially built scene");
        m_SceneState = VansSceneState::Unloading;
        ExecuteSceneGpuMaintenance(
			*this, *device, SceneGpuMaintenanceOperation::WaitIdle);
        UnLoadScene();
        m_SceneState = VansSceneState::Empty;
        return false;
    }

    if (!ExecuteSceneGpuMaintenance(
		*this, *device, SceneGpuMaintenanceOperation::PrepareScene))
	{
		VANS_LOG_ERROR("[VansScene] Render-thread scene GPU preparation failed");
		m_SceneState = VansSceneState::Unloading;
		ExecuteSceneGpuMaintenance(
			*this, *device, SceneGpuMaintenanceOperation::WaitIdle);
		UnLoadScene();
		m_SceneState = VansSceneState::Empty;
		return false;
	}

	PlayAllSceneVideos();

	m_SceneState = VansSceneState::Ready;
    VANS_LOG("[VansScene] Scene ready for rendering");
    return true;
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

bool VansGraphics::VansScene::SetEntityParentReferenceByGuid(
    const std::string& childEntityGuid,
    const Vans::VansSceneParentReference* parentReference,
    Vans::VansTransformReparentMode mode)
{
	return SetEntityParentReferenceInternal(
		childEntityGuid, parentReference, mode, nullptr);
}

bool VansGraphics::VansScene::BindEntityToAnimationAttachmentProfileByGuid(
	const std::string& childEntityGuid,
	const Vans::VansSceneParentReference& parent)
{
	if (!parent.IsAnchor() || !m_RuntimeWorld)
		return false;
	VansScriptObject* child = FindObjectByGuid(childEntityGuid);
	VansScriptObject* owner = FindObjectByGuid(parent.entityGuid.ToString());
	if (!child || !owner || child->m_ModelAssetGuid.empty())
		return false;

	const Vans::VansComponentHandle component = m_RuntimeWorld->FindComponentByGuid(
		parent.animationComponentGuid.ToString(), Vans::VansRuntimeComponentType_Animation);
	auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeAnimationComponent>*>(
		m_RuntimeWorld->FindStorage(Vans::VansRuntimeComponentType_Animation));
	const Vans::VansRuntimeAnimationComponent* animation = storage ? storage->Get(component) : nullptr;
	const Vans::VansComponentHeader* header = m_RuntimeWorld->GetComponentHeader(component);
	const Vans::VansEntityHandle ownerEntity =
		m_RuntimeWorld->Entities().FindByGuid(owner->m_EntityGuid);
	if (!animation || !animation->animationNode || !header || header->owner != ownerEntity)
		return false;
	const VansAnimationController* controller = animation->animationNode->GetController();
	const VansCompiledAnimationRig* rig = controller ? controller->GetAnimationRig() : nullptr;
	const VansRigAttachmentParentKind parentKind =
		parent.kind == Vans::VansSceneParentKind::Bone
			? VansRigAttachmentParentKind::Bone
			: VansRigAttachmentParentKind::Socket;
	const VansCompiledRigAttachmentProfile* profile = rig
		? rig->FindAttachmentProfile(
			child->m_ModelAssetGuid, parentKind, parent.anchorGuid.ToString())
		: nullptr;
	if (!profile)
		return false;

	Vans::VansLocalTransform localTransform;
	localTransform.position = profile->positionLocal;
	localTransform.rotation = profile->rotationLocal;
	localTransform.scale = profile->scaleLocal;
	return SetEntityParentReferenceInternal(
		childEntityGuid, &parent, Vans::VansTransformReparentMode::KeepLocal,
		&localTransform);
}

bool VansGraphics::VansScene::SetEntityParentReferenceInternal(
	const std::string& childEntityGuid,
	const Vans::VansSceneParentReference* parentReference,
	Vans::VansTransformReparentMode mode,
	const Vans::VansLocalTransform* anchorLocalTransform)
{
	const std::string parentEntityGuid = parentReference
		? parentReference->entityGuid.ToString() : std::string{};
    if (childEntityGuid.empty() || childEntityGuid == parentEntityGuid)
        return false;

    VansScriptObject* child = FindObjectByGuid(childEntityGuid);
    if (!child)
        return false;

    if (!parentEntityGuid.empty())
    {
        VansScriptObject* parent = FindObjectByGuid(parentEntityGuid);
        if (!parent)
            return false;
		const bool transformParentApplied = parentReference->IsAnchor()
			? SetTransformAnchorReference(child->m_TransformID, parent->m_TransformID,
				*parentReference, mode, anchorLocalTransform)
			: m_TransformGraph.SetParent(child->m_TransformID, parent->m_TransformID, mode);
		if (!transformParentApplied)
			return false;
    }
    else if (m_TransformGraph.HasParent(child->m_TransformID))
    {
		if (!m_TransformGraph.ClearParent(child->m_TransformID, mode))
			return false;
    }

    if (m_RuntimeWorld)
    {
        Vans::VansEntityHandle childHandle =
            m_RuntimeWorld->Entities().FindByGuid(childEntityGuid);
        Vans::VansEntityHandle parentHandle;
        if (!parentEntityGuid.empty())
            parentHandle = m_RuntimeWorld->Entities().FindByGuid(parentEntityGuid);
        if (!childHandle.IsValid() || (!parentEntityGuid.empty() && !parentHandle.IsValid()))
            return false;

        m_RuntimeWorld->Commands().SetParent(childHandle, parentHandle);
        m_RuntimeWorld->FlushCommands();

        const Vans::VansEntityRecord* childRecord = m_RuntimeWorld->Entities().Get(childHandle);
        const bool runtimeParentMatches =
            childRecord &&
            (parentEntityGuid.empty() ? !childRecord->parent.IsValid() : childRecord->parent == parentHandle);
        if (!runtimeParentMatches)
            return false;

        for (const Vans::VansComponentHandle component :
            m_RuntimeWorld->CollectComponentsInSubtree(childHandle))
        {
            const bool effectiveEnabled =
                m_RuntimeWorld->IsComponentEffectivelyEnabled(component);
            ApplyRuntimeComponentEnabled(component, effectiveEnabled);
        }
    }

	VansTransformStore::TransformIDToTransformDirty[child->m_TransformID] = true;
	return m_TransformGraph.Resolve();
}

bool VansGraphics::VansScene::TryGetEntityLocalTransformByGuid(
	const std::string& entityGuid,
	Vans::VansLocalTransform& transform) const
{
	const VansScriptObject* object = FindObjectByGuid(entityGuid);
	return object && m_TransformGraph.TryGetLocalTransform(object->m_TransformID, transform);
}

bool VansGraphics::VansScene::TryGetEntityParentReferenceByGuid(
	const std::string& childEntityGuid,
	Vans::VansSceneParentReference& parent,
	bool& hasParent) const
{
	hasParent = false;
	parent = {};
	const VansScriptObject* child = FindObjectByGuid(childEntityGuid);
	if (!child)
		return false;
	const Vans::VansTransformGraphLink* link =
		m_TransformGraph.GetLink(child->m_TransformID);
	if (!link)
		return true;
	const VansScriptObject* parentObject = nullptr;
	for (const VansScriptObject* object : m_SceneObjects)
	{
		if (object && object->m_TransformID == link->parentTransformId)
		{
			parentObject = object;
			break;
		}
	}
	if (!parentObject || !Vans::VansAssetGuid::TryParse(
		parentObject->m_EntityGuid, parent.entityGuid))
		return false;
	parent.kind = Vans::VansSceneParentKind::Entity;
	if (link->usesAnchor)
	{
		if (!m_RuntimeWorld)
			return false;
		auto* storage = static_cast<const Vans::VansComponentStorage<
			Vans::VansRuntimeAnimationComponent>*>(m_RuntimeWorld->FindStorage(
				Vans::VansRuntimeComponentType_Animation));
		if (!storage)
			return false;
		const auto& components = storage->DenseData();
		const auto& headers = storage->Headers();
		bool componentFound = false;
		for (std::size_t index = 0;
			index < components.size() && index < headers.size(); ++index)
		{
			const auto& animation = components[index];
			const Vans::VansEntityRecord* owner =
				m_RuntimeWorld->Entities().Get(headers[index].owner);
			if (!owner || owner->stableGuid != parentObject->m_EntityGuid
				|| animation.skeletonInstanceId != link->anchor.instanceId
				|| animation.skeletonInstanceGeneration != link->anchor.instanceGeneration)
				continue;
			if (!Vans::VansAssetGuid::TryParse(
				headers[index].stableGuid, parent.animationComponentGuid))
				return false;
			componentFound = true;
			break;
		}
		if (!componentFound || !Vans::VansAssetGuid::TryParse(
			link->anchor.anchorGuid, parent.anchorGuid))
			return false;
		parent.kind = link->anchor.kind == Vans::VansTransformAnchorKind::Bone
			? Vans::VansSceneParentKind::Bone : Vans::VansSceneParentKind::Socket;
	}
	hasParent = true;
	return true;
}

bool VansGraphics::VansScene::SetEntityLocalTransformByGuid(
	const std::string& entityGuid,
	const Vans::VansLocalTransform& transform)
{
	VansScriptObject* object = FindObjectByGuid(entityGuid);
	if (!object)
		return false;
	if (m_TransformGraph.HasParent(object->m_TransformID))
	{
		if (!m_TransformGraph.SetLocalTransform(object->m_TransformID, transform))
			return false;
		return m_TransformGraph.Resolve();
	}
	return m_TransformGraph.SetWorldTransform(object->m_TransformID, transform.ToMatrix());
}

bool VansGraphics::VansScene::TryGetEntityWorldTransformByGuid(
	const std::string& entityGuid,
	Vans::VansLocalTransform& transform) const
{
	const VansScriptObject* object = FindObjectByGuid(entityGuid);
	return object && VansTransformStore::IsAllocated(object->m_TransformID)
		&& Vans::VansLocalTransform::TryFromMatrix(
			VansTransformStore::GetTransform(object->m_TransformID).GetModelMatrix(), transform);
}

bool VansGraphics::VansScene::SetEntityWorldTransformByGuid(
	const std::string& entityGuid,
	const Vans::VansLocalTransform& transform)
{
	VansScriptObject* object = FindObjectByGuid(entityGuid);
	return object && m_TransformGraph.SetWorldTransform(
		object->m_TransformID, transform.ToMatrix()) && m_TransformGraph.Resolve();
}

bool VansGraphics::VansScene::SetTransformAnchorReference(
	uint32_t childTransformID,
	uint32_t ownerTransformID,
	const Vans::VansSceneParentReference& parent,
	Vans::VansTransformReparentMode mode,
	const Vans::VansLocalTransform* anchorLocalTransform)
{
	if (!parent.IsAnchor() || !m_RuntimeWorld)
		return false;
	const Vans::VansComponentHandle component = m_RuntimeWorld->FindComponentByGuid(
		parent.animationComponentGuid.ToString(), Vans::VansRuntimeComponentType_Animation);
	auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeAnimationComponent>*>(
		m_RuntimeWorld->FindStorage(Vans::VansRuntimeComponentType_Animation));
	const Vans::VansRuntimeAnimationComponent* animation = storage ? storage->Get(component) : nullptr;
	if (!animation || animation->skeletonInstanceId == 0
		|| animation->skeletonInstanceGeneration == 0)
		return false;
	Vans::VansTransformAnchorHandle anchor;
	anchor.instanceId = animation->skeletonInstanceId;
	anchor.instanceGeneration = animation->skeletonInstanceGeneration;
	anchor.kind = parent.kind == Vans::VansSceneParentKind::Bone
		? Vans::VansTransformAnchorKind::Bone : Vans::VansTransformAnchorKind::Socket;
	anchor.anchorGuid = parent.anchorGuid.ToString();
	return anchorLocalTransform
		? m_TransformGraph.SetAnchorWithLocalTransform(
			childTransformID, ownerTransformID, std::move(anchor), *anchorLocalTransform)
		: m_TransformGraph.SetAnchor(
			childTransformID, ownerTransformID, std::move(anchor), mode);
}

bool VansGraphics::VansScene::SetEntityNameByGuid(
    const std::string& entityGuid,
    const std::string& name)
{
    if (entityGuid.empty())
        return false;
    VansScriptObject* object = FindObjectByGuid(entityGuid);
    if (!object)
        return false;

    bool runtimeUpdated = true;
    if (m_RuntimeWorld)
    {
        const Vans::VansEntityHandle entity =
            m_RuntimeWorld->Entities().FindByGuid(entityGuid);
        if (!entity.IsValid())
        {
            runtimeUpdated = false;
        }
        else
        {
            m_RuntimeWorld->Commands().SetEntityName(entity, name);
            m_RuntimeWorld->FlushCommands();
            const Vans::VansEntityRecord* record = m_RuntimeWorld->Entities().Get(entity);
            runtimeUpdated = record && record->name == name;
        }
    }
    if (!runtimeUpdated)
        return false;

    object->m_ObjectName = name;
    return true;
}

bool VansGraphics::VansScene::SetEntityActiveByGuid(
    const std::string& entityGuid,
    bool active)
{
    if (entityGuid.empty())
        return false;
    VansScriptObject* object = FindObjectByGuid(entityGuid);
    if (!object)
        return false;

    bool runtimeUpdated = true;
    if (m_RuntimeWorld)
    {
        const Vans::VansEntityHandle entity =
            m_RuntimeWorld->Entities().FindByGuid(entityGuid);
        if (!entity.IsValid())
        {
            runtimeUpdated = false;
        }
        else
        {
            m_RuntimeWorld->Commands().SetEntityActive(entity, active);
            m_RuntimeWorld->FlushCommands();
            const Vans::VansEntityRecord* record = m_RuntimeWorld->Entities().Get(entity);
            runtimeUpdated = record && record->selfActive == active;
            if (runtimeUpdated)
            {
                for (const Vans::VansComponentHandle component :
                    m_RuntimeWorld->CollectComponentsInSubtree(entity))
                {
                    const bool effectiveEnabled =
                        m_RuntimeWorld->IsComponentEffectivelyEnabled(component);
                    ApplyRuntimeComponentEnabled(component, effectiveEnabled);
                }
            }
        }
    }
    if (!runtimeUpdated)
        return false;

    object->SetActive(active);
    return true;
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

