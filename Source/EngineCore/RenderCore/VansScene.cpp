#include "VansScene.h"

#include "Animation/VansAnimationWorldQueryBatch.h"
#include "../RuntimeCore/VansFramePhase.h"
#include "../GameplayActionCore/VansGameplayRuntime.h"
#include "../GameplayActionAdapters/Combat/VansCombatActionService.h"
#include "../AICore/VansAIWorld.h"
#include "Timeline/VansVirtualCameraParameterStore.h"

#include "../RuntimeCore/VansThreadContract.h"
#include "VansShaderManager.h"
#include "VansCamera.h"
#include "VansCameraControlArbiter.h"
#include "BRDFData/VansLight.h"
#include "../Configration/VansConfigration.h"
#include "../AudioCore/VansAudioReverbEnvironment.h"
#include "../AudioCore/VansAudioSourceBinding.h"
#include "../AudioCore/VansAudioSystem.h"
#include "../AudioCore/VansAudioVirtualization.h"
#include "../PhysicsCore/VansPhysics.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "../PhysicsCore/VansPhysicsVehicle.h"
#include "../PhysicsCore/VansClothNode.h"
#include "../PhysicsCore/VansCharacterControllerNode.h"
#include "../PhysicsCore/VansTerrainPhysicsNode.h"
#include "../PhysicsCore/VansRagdollSystem.h"

#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "TerrainCore/VansTerrain.h"
#include "WaterCore/VansWaterSystem.h"
#include "AtmosphereCore/VansAtmosphereSystem.h"
#include "AtmosphereCore/VansNearMediaSystem.h"
#include "CloudCore/VansVolumetricCloudSystem.h"
#include "VegetationCore/VansVegetationSystem.h"
#include "VansParticleRenderNode.h"
#include "../ParticleCore/VansParticleManager.h"
#include "../ParticleCore/VansParticleRuntime.h"
#include "../RuntimeUI/Public/VansUIScreen.h"
#include "../RuntimeUI/Public/VansUISystem.h"
#include "../SceneRuntime/VansRuntimeComponentTypes.h"
#include "../SceneRuntime/VansRuntimeWorld.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../AnimationCore/VansSkinnedMeshLoader.h"
#include "../TimelineRuntime/VansTimelineRuntimeSystem.h"
#include "../ScriptCore/VansScriptContext.h"
#include "../VansTimer.h"

#include "../AssetCore/VansAssetGuid.h"
#include "../Util/VansLog.h"
#include "../Util/VansProfiler.h"
#include <iostream>
#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

#ifdef _DEBUG
#define VANS_UNLOAD_STEP(index, reason) VANS_LOG("[VansScene][UnLoadScene Step " << index << "] " << reason)
#else
#define VANS_UNLOAD_STEP(index, reason) do { (void)sizeof(index); } while (0)
#endif

namespace
{
	class SceneEntityDestructionBarrier final
		: public VansGraphics::IVansRenderThreadTransaction
	{
	public:
		bool Execute(VansGraphics::VansGraphicsDevice& backend) override
		{
			VANS_ASSERT_RENDER_THREAD();
			return backend.WaitForIdle();
		}
	};

	struct AudioOcclusionQueryTarget
	{
		Vans::VansComponentHandle component;
		glm::vec3 sourcePosition{ 0.0f };
		float distance = 0.0f;
		uint32_t ignoredTransformID = 0;
	};

	class AudioOcclusionQueryFilter final : public physx::PxQueryFilterCallback
	{
	public:
		explicit AudioOcclusionQueryFilter(uint32_t ignoredTransformID)
			: m_IgnoredTransformID(ignoredTransformID)
		{
		}

		physx::PxQueryHitType::Enum preFilter(
			const physx::PxFilterData&,
			const physx::PxShape* shape,
			const physx::PxRigidActor* actor,
			physx::PxHitFlags&) override
		{
			return Filter(shape, actor);
		}

		physx::PxQueryHitType::Enum postFilter(
			const physx::PxFilterData&,
			const physx::PxQueryHit&,
			const physx::PxShape* shape,
			const physx::PxRigidActor* actor) override
		{
			return Filter(shape, actor);
		}

	private:
		physx::PxQueryHitType::Enum Filter(
			const physx::PxShape* shape,
			const physx::PxRigidActor* actor) const
		{
			if (!shape)
				return physx::PxQueryHitType::eNONE;
			const physx::PxFilterData target = shape->getQueryFilterData();
			if ((target.word2 & 0x1u) != 0u)
				return physx::PxQueryHitType::eNONE;
			if (m_IgnoredTransformID != 0 && actor && actor->userData)
			{
				auto* node = static_cast<VansEngine::VansPhysicsNode*>(actor->userData);
				if (node && node->GetTransformID() == m_IgnoredTransformID)
					return physx::PxQueryHitType::eNONE;
			}
			return physx::PxQueryHitType::eBLOCK;
		}

		uint32_t m_IgnoredTransformID = 0;
	};

	VansGraphics::VansAsset* FindAssetInLookup(
		const std::unordered_map<std::string, VansGraphics::VansAsset*>& lookup,
		const std::string& name)
	{
		const auto it = lookup.find(name);
		return it != lookup.end() ? it->second : nullptr;
	}

	VansGraphics::VansAsset* FindAssetAndBackfillLookup(
		const std::vector<VansGraphics::VansAsset*>& assets,
		std::unordered_map<std::string, VansGraphics::VansAsset*>& lookup,
		const std::string& name)
	{
		for (auto* asset : assets)
		{
			if (asset && asset->m_AssetName == name)
			{
				lookup[name] = asset;
				return asset;
			}
		}
		return nullptr;
	}

	Vans::VansRuntimeScriptState ToRuntimeScriptState(VansLuaScriptState state)
	{
		switch (state)
		{
		case VansLuaScriptState::Loading:
			return Vans::VansRuntimeScriptState::Loading;
		case VansLuaScriptState::Active:
			return Vans::VansRuntimeScriptState::Active;
		case VansLuaScriptState::Disabled:
			return Vans::VansRuntimeScriptState::Disabled;
		case VansLuaScriptState::Faulted:
			return Vans::VansRuntimeScriptState::Faulted;
		case VansLuaScriptState::Destroyed:
			return Vans::VansRuntimeScriptState::Destroyed;
		case VansLuaScriptState::Unloaded:
		default:
			return Vans::VansRuntimeScriptState::Unloaded;
		}
	}

	void RegisterAssetByName(
		std::unordered_map<std::string, VansGraphics::VansAsset*>& lookup,
		VansGraphics::VansAsset* asset)
	{
		if (asset && !asset->m_AssetName.empty())
			lookup[asset->m_AssetName] = asset;
	}

	template <typename T>
	const T* GetRuntimeComponentPayload(
		const Vans::VansRuntimeWorld& runtimeWorld,
		Vans::VansComponentHandle component,
		std::uint16_t expectedType)
	{
		if (component.typeId != expectedType)
			return nullptr;
		const auto* storage = static_cast<const Vans::VansComponentStorage<T>*>(
			runtimeWorld.FindStorage(expectedType));
		return storage ? storage->Get(component) : nullptr;
	}

	std::uint32_t ResolveRuntimeEntityTransformId(
		const Vans::VansRuntimeWorld& runtimeWorld,
		Vans::VansEntityHandle entity)
	{
		for (Vans::VansComponentHandle component : runtimeWorld.CollectComponentsOwnedBy(entity))
		{
			if (const auto* transform = GetRuntimeComponentPayload<Vans::VansRuntimeTransformComponent>(
				runtimeWorld,
				component,
				Vans::VansRuntimeComponentType_Transform))
			{
				return transform->transformStoreId;
			}
		}
		return UINT32_MAX;
	}

	struct RuntimeSceneDestroyReferences
	{
		VansGraphics::VansRenderNode* renderNode = nullptr;
		VansGraphics::VansParticleRenderNode* particleRenderNode = nullptr;
		VansGraphics::VansAnimationNode* animationNode = nullptr;
		VansGraphics::VansSkeletonInstanceHandle skeletonInstance;
		VansEngine::VansPhysicsNode* physicsNode = nullptr;
		VansEngine::VansClothNode* clothNode = nullptr;
		VansEngine::VansCharacterControllerNode* characterControllerNode = nullptr;
		VansEngine::VansPhysicsVehicle* vehicle = nullptr;
		int directionalLightIndex = -1;
		int pointLightIndex = -1;
		int spotLightIndex = -1;
		int rectLightIndex = -1;
		bool hasRagdoll = false;
	};

	RuntimeSceneDestroyReferences CollectRuntimeSceneDestroyReferences(
		const Vans::VansRuntimeWorld& runtimeWorld,
		Vans::VansEntityHandle entity)
	{
		RuntimeSceneDestroyReferences references;
		for (Vans::VansComponentHandle component : runtimeWorld.CollectComponentsOwnedBy(entity))
		{
			switch (component.typeId)
			{
			case Vans::VansRuntimeComponentType_Render:
				if (const auto* render = GetRuntimeComponentPayload<Vans::VansRuntimeRenderComponent>(
					runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_Render))
				{
					references.renderNode = render->renderNode;
					if (!references.renderNode && !render->renderNodes.empty())
						references.renderNode = render->renderNodes.front();
				}
				break;
			case Vans::VansRuntimeComponentType_Physics:
				if (const auto* physics = GetRuntimeComponentPayload<Vans::VansRuntimePhysicsComponent>(
					runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_Physics))
					references.physicsNode = physics->physicsNode;
				break;
			case Vans::VansRuntimeComponentType_Cloth:
				if (const auto* cloth = GetRuntimeComponentPayload<Vans::VansRuntimeClothComponent>(
					runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_Cloth))
					references.clothNode = cloth->clothNode;
				break;
			case Vans::VansRuntimeComponentType_CharacterController:
				if (const auto* cct = GetRuntimeComponentPayload<Vans::VansRuntimeCharacterControllerComponent>(
					runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_CharacterController))
					references.characterControllerNode = cct->controllerNode;
				break;
			case Vans::VansRuntimeComponentType_Vehicle:
				if (const auto* vehicle = GetRuntimeComponentPayload<Vans::VansRuntimeVehicleComponent>(
					runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_Vehicle))
					references.vehicle = vehicle->vehicle;
				break;
			case Vans::VansRuntimeComponentType_Animation:
				if (const auto* animation = GetRuntimeComponentPayload<Vans::VansRuntimeAnimationComponent>(
					runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_Animation))
				{
					references.animationNode = animation->animationNode;
					references.skeletonInstance = {
						animation->skeletonInstanceId,
						animation->skeletonInstanceGeneration };
				}
				break;
			case Vans::VansRuntimeComponentType_Ragdoll:
				if (const auto* ragdoll = GetRuntimeComponentPayload<Vans::VansRuntimeRagdollComponent>(
					runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_Ragdoll))
				{
					references.hasRagdoll = true;
					if (!references.animationNode)
						references.animationNode = ragdoll->animationNode;
				}
				break;
			case Vans::VansRuntimeComponentType_Particle:
				if (const auto* particle = GetRuntimeComponentPayload<Vans::VansRuntimeParticleComponent>(
					runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_Particle))
					references.particleRenderNode = particle->renderNode;
				break;
			case Vans::VansRuntimeComponentType_DirectionalLight:
				if (const auto* light = GetRuntimeComponentPayload<Vans::VansRuntimeLightComponent>(
					runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_DirectionalLight))
					references.directionalLightIndex = light->lightIndex;
				break;
			case Vans::VansRuntimeComponentType_PointLight:
				if (const auto* light = GetRuntimeComponentPayload<Vans::VansRuntimeLightComponent>(
					runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_PointLight))
					references.pointLightIndex = light->lightIndex;
				break;
			case Vans::VansRuntimeComponentType_SpotLight:
				if (const auto* light = GetRuntimeComponentPayload<Vans::VansRuntimeLightComponent>(
					runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_SpotLight))
					references.spotLightIndex = light->lightIndex;
				break;
			case Vans::VansRuntimeComponentType_RectLight:
				if (const auto* light = GetRuntimeComponentPayload<Vans::VansRuntimeLightComponent>(
					runtimeWorld,
					component,
					Vans::VansRuntimeComponentType_RectLight))
					references.rectLightIndex = light->lightIndex;
				break;
			default:
				break;
			}
		}
		return references;
	}

}

VansGraphics::VansScene::VansScene()
{
	m_TransformGraph.SetAnchorProvider(&m_SkeletonAnchorRegistry);
}

VansGraphics::VansScene::~VansScene()
{
    if (m_SceneState != VansSceneState::Empty || !m_SceneObjects.empty())
    {
        VANS_LOG_WARN("[VansScene] Scene is still loaded during destruction; call UnLoadScene() before delete");
    }
}

bool VansGraphics::VansScene::InitializeEnvironmentRendering(VansVKDevice& device)
{
	ShutdownEnvironmentRendering();
	m_AtmosphereSystem = std::make_unique<VansAtmosphereSystem>();
	if (!m_AtmosphereSystem->Initialize(
		device,
		*this,
		device.GetAtmosphereQualityConfig(),
		device.GetRenderWidth(),
		device.GetRenderHeight()))
	{
		m_AtmosphereSystem.reset();
		return false;
	}
	m_NearMediaSystem = std::make_unique<VansNearMediaSystem>();
	if (!m_NearMediaSystem->Initialize(
		device,
		*this,
		device.GetNearMediaQualityConfig(),
		device.GetRenderWidth(),
		device.GetRenderHeight()))
	{
		m_NearMediaSystem.reset();
		m_AtmosphereSystem->Shutdown();
		m_AtmosphereSystem.reset();
		return false;
	}
	m_VolumetricCloudSystem = std::make_unique<VansVolumetricCloudSystem>();
	if (!m_VolumetricCloudSystem->Initialize(
		device,
		*this,
		device.GetCloudShadowQualityConfig(),
		device.GetRenderWidth(),
		device.GetRenderHeight()))
	{
		m_VolumetricCloudSystem.reset();
		m_NearMediaSystem->Shutdown();
		m_NearMediaSystem.reset();
		m_AtmosphereSystem->Shutdown();
		m_AtmosphereSystem.reset();
		return false;
	}
	m_AtmosphereSystem->BindGlobalDescriptors(
		m_GlobalDescriptorSet,
		m_VolumetricCloudSystem->GetShadowDescriptor(),
		m_VolumetricCloudSystem->GetResultDescriptor(),
		m_NearMediaSystem->GetScatteringDescriptor(),
		m_NearMediaSystem->GetOpticalDepthDescriptor());
	m_NearMediaSystem->BindGlobalDescriptor(m_GlobalDescriptorSet);
	m_VolumetricCloudSystem->BindGlobalDescriptors(m_GlobalDescriptorSet);
	return true;
}

void VansGraphics::VansScene::ShutdownEnvironmentRendering()
{
	if (m_VolumetricCloudSystem)
	{
		m_VolumetricCloudSystem->Shutdown();
		m_VolumetricCloudSystem.reset();
	}
	if (m_NearMediaSystem)
	{
		m_NearMediaSystem->Shutdown();
		m_NearMediaSystem.reset();
	}
	if (m_AtmosphereSystem)
	{
		m_AtmosphereSystem->Shutdown();
		m_AtmosphereSystem.reset();
	}
}

bool VansGraphics::VansScene::ReinitializeEnvironmentRendering(
	const VansAtmosphereQualityConfig& quality,
	std::uint32_t renderWidth,
	std::uint32_t renderHeight)
{
	const bool hasAtmosphereSystem = m_AtmosphereSystem != nullptr;
	const bool hasNearMediaSystem = m_NearMediaSystem != nullptr;
	const bool hasVolumetricCloudSystem = m_VolumetricCloudSystem != nullptr;
	if (!hasAtmosphereSystem && !hasNearMediaSystem && !hasVolumetricCloudSystem)
		return true;
	if (!hasAtmosphereSystem || !hasNearMediaSystem || !hasVolumetricCloudSystem)
	{
		VANS_LOG_ERROR("[Atmosphere] Environment subsystem ownership is incomplete during reinitialization");
		return false;
	}
	if (!m_VolumetricCloudSystem->Reinitialize(
		m_RuntimeResourceDevice->GetCloudShadowQualityConfig(), renderWidth, renderHeight))
		return false;
	if (!m_NearMediaSystem->Reinitialize(
		m_RuntimeResourceDevice->GetNearMediaQualityConfig(), renderWidth, renderHeight))
		return false;
	if (!m_AtmosphereSystem->ReinitializeViewResources(
		quality, renderWidth, renderHeight))
		return false;
	m_AtmosphereSystem->BindGlobalDescriptors(
		m_GlobalDescriptorSet,
		m_VolumetricCloudSystem->GetShadowDescriptor(),
		m_VolumetricCloudSystem->GetResultDescriptor(),
		m_NearMediaSystem->GetScatteringDescriptor(),
		m_NearMediaSystem->GetOpticalDepthDescriptor());
	m_NearMediaSystem->BindGlobalDescriptor(m_GlobalDescriptorSet);
	m_VolumetricCloudSystem->BindGlobalDescriptors(m_GlobalDescriptorSet);
	return true;
}

void VansGraphics::VansScene::InjectCamera(VansCamera* camera)
{
	if (m_Camera != camera)
	{
		if (m_RuntimeResourceDevice != nullptr)
			m_RuntimeResourceDevice->RequestUpscalerHistoryReset(
				VansUpscalerResetReason::CameraCut);
		m_Camera = camera;
	}
}

bool VansGraphics::VansScene::ReplaceAnimationRuntimeController(
	VansAnimationNode* animNode,
	std::unique_ptr<VansAnimationController> controller)
{
	std::unique_ptr<VansAnimationController> previous;
	return ExchangeAnimationRuntimeController(
		animNode, std::move(controller), previous);
}

bool VansGraphics::VansScene::ExchangeAnimationRuntimeController(
	VansAnimationNode* animNode,
	std::unique_ptr<VansAnimationController> controller,
	std::unique_ptr<VansAnimationController>& previousController)
{
	if (!animNode || !controller)
		return false;
	VansAnimationController* previous = animNode->GetController();
	auto registered = std::find(m_AnimationControllers.begin(), m_AnimationControllers.end(), previous);
	if (previous && registered == m_AnimationControllers.end())
		return false;

	VansAnimationController* replacement = controller.get();
	if (!animNode->SetController(replacement))
		return false;
	controller.release();
	if (registered != m_AnimationControllers.end())
		*registered = replacement;
	else
		m_AnimationControllers.push_back(replacement);
	previousController.reset(previous);
	return true;
}

VansAsset* VansGraphics::VansScene::GetMeshAsset(const std::string& name)
{
	return m_AssetRegistry.FindMesh(name);
}

VansAsset* VansGraphics::VansScene::GetShaderAsset(const std::string& name)
{
	return m_AssetRegistry.FindShader(name);
}

VansAsset* VansGraphics::VansScene::GetTextureAsset(const std::string& name)
{
	return m_AssetRegistry.FindTexture(name);
}

VansAsset* VansGraphics::VansScene::GetMaterialAsset(const std::string& name)
{
	return m_AssetRegistry.FindMaterial(name);
}

VansTexture* VansGraphics::VansScene::ResolveTextureOrDefault(VansTexture* texture, const char* fallbackName)
{
	if (texture)
		return texture;

	if (!fallbackName || fallbackName[0] == '\0')
		return nullptr;

	return static_cast<VansTexture*>(GetTextureAsset(fallbackName));
}

void VansGraphics::VansScene::RegisterMeshAsset(VansAsset* asset)
{
	m_AssetRegistry.RegisterMesh(asset);
}

void VansGraphics::VansScene::RegisterSceneSubMeshAsset(VansAsset* asset)
{
	m_AssetRegistry.RegisterSceneSubMesh(asset);
}

void VansGraphics::VansScene::RegisterShaderAsset(VansAsset* asset)
{
	m_AssetRegistry.RegisterShader(asset);
}

void VansGraphics::VansScene::RegisterTextureAsset(VansAsset* asset)
{
	m_AssetRegistry.RegisterTexture(asset);
}

void VansGraphics::VansScene::RegisterMaterialAsset(VansAsset* asset)
{
	m_AssetRegistry.RegisterMaterial(asset);
}

void VansGraphics::VansScene::AddMeshAsset(VansAsset* asset)
{
	m_AssetRegistry.AddMesh(asset);
}

void VansGraphics::VansScene::AddSceneSubMeshAsset(VansAsset* asset)
{
	m_AssetRegistry.AddSceneSubMesh(asset);
}

void VansGraphics::VansScene::AddShaderAsset(VansAsset* asset)
{
	m_AssetRegistry.AddShader(asset);
}

void VansGraphics::VansScene::AddTextureAsset(VansAsset* asset)
{
	m_AssetRegistry.AddTexture(asset);
}

void VansGraphics::VansScene::AddMaterialAsset(VansAsset* asset)
{
	m_AssetRegistry.AddMaterial(asset);
}

void VansGraphics::VansScene::LoadProjectAudioResources(
	const std::vector<Vans::VansSceneAudioResourceRequest>& audios)
{
	m_AudioManager.Load(audios);
}

void VansGraphics::VansScene::LoadProjectVideoResources(
	const std::vector<Vans::VansSceneVideoResourceRequest>& videos)
{
	m_VideoManager.Load(videos, m_RuntimeResourceDevice);
}

void VansGraphics::VansScene::SyncShaderAssetsFromShaderManager()
{
	m_AssetRegistry.ClearShaders();
	for (VansShader* shader : VansGraphics::VansShaderManager::Get().GetLoadedShaderAssets())
	{
		AddShaderAsset(shader);
	}
}

void VansGraphics::VansScene::FinalizeProjectResourceBatch()
{
	SyncShaderAssetsFromShaderManager();
	RebuildAssetLookup();

	VANS_LOG("[VansScene] Resources loaded: "
		<< GetMeshAssets().size() << " meshes, "
		<< GetTextureAssets().size() << " textures, "
		<< GetShaderAssets().size() << " shaders, "
		<< m_VideoManager.Count() << " videos, "
		<< m_AudioManager.Count() << " audios");
}

bool VansGraphics::VansScene::HasProjectMeshAlias(const std::string& name) const
{
	return m_AssetRegistry.HasProjectMeshAlias(name);
}

void VansGraphics::VansScene::SetProjectMeshAlias(const std::string& name, VansAsset* asset)
{
	m_AssetRegistry.SetProjectMeshAlias(name, asset);
}

VansAsset* VansGraphics::VansScene::FindMeshAsset(const std::string& name)
{
	return GetMeshAsset(name);
}

VansAsset* VansGraphics::VansScene::FindShaderAsset(const std::string& name)
{
	return GetShaderAsset(name);
}

VansTexture* VansGraphics::VansScene::FindOrLoadTexture(const std::string& absPath, bool isSRGB)
{
	return LoadOrGetTexture(absPath, isSRGB);
}

void VansGraphics::VansScene::RebuildAssetLookup()
{
	m_AssetRegistry.RebuildLookup();
}

void VansGraphics::VansScene::ClearSceneAssetLookup()
{
	m_AssetRegistry.ClearSceneLookup();
}

void VansGraphics::VansScene::RegistRenderNode(VansRenderNode* renderNode, RenderNodeType type)
{
    // 将 renderNode 记录到对应类型的 vector
    switch (type)
    {
    case DEFERRED_NODE:
        m_DeferredNode = renderNode;
        break;
	case OPAQUE_NODE:
		m_OpaqueRenderNodes.push_back(renderNode);
		break;
	case HAIR_NODE:
		m_HairRenderNodes.push_back(renderNode);
		break;
	case FORWARD_OPAQUE_PRE_ATMOSPHERE_NODE:
		m_ForwardOpaquePreAtmosphereRenderNodes.push_back(renderNode);
		break;
    case TERRAIN_NODE:
        m_TerrainRenderNode = renderNode;
		break;
    case WATER_NODE:
        m_WaterRenderNode = renderNode;
        break;
    case TRANSPARENT_NODE:
		m_TransParentRenderNodes.push_back(renderNode);
		break;
    case POSTPROCESS_NODE:
		m_PostProcessRenderNodes.push_back(renderNode);
		break;
    case SCREEN_SPACE_NODE:
        m_ScreenSpaceRenderNodes.push_back(renderNode);
        break;
    case VEGETATION_NODE:
        m_VegetationRenderNode = renderNode;
        break;
    case DECAL_NODE:
        m_DecalRenderNodes.push_back(renderNode);
        break;
    case PARTICLE_NODE:
        m_ParticleRenderNodes.push_back(
            static_cast<VansParticleRenderNode*>(renderNode));
        break;
	default:
		break;
    }
}

void VansGraphics::VansScene::CreateNodeDescriptorSets()
{
    //遍历所有的node生成set
    if (m_DeferredNode != nullptr)
    {
        m_DeferredNode->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);
    }
    for (auto node : m_OpaqueRenderNodes)
    {
        node->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);
	}
	for (auto node : m_HairRenderNodes)
	{
		node->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);
	}
	for (auto node : m_ForwardOpaquePreAtmosphereRenderNodes)
	{
		node->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);
	}
    if (m_TerrainRenderNode != nullptr)
    {
        m_TerrainRenderNode->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);
    }
    if (m_WaterRenderNode != nullptr)
    {
        m_WaterRenderNode->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);
    }
    if (m_VegetationRenderNode != nullptr)
    {
        m_VegetationRenderNode->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);
    }
    for (auto node : m_TransParentRenderNodes)
    {
        node->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);
    }
    for (auto node : m_PostProcessRenderNodes)
    {
        node->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);
    }
    for (auto node : m_ScreenSpaceRenderNodes)
    {
        node->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);
    }
    for (auto node : m_DecalRenderNodes)
    {
        node->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);
    }

    // 粒子渲染节点：不依赖 VansMaterial，独立设置描述符
    // 使用全局集（Set 0）访问 Camera UBO，Set 1 绑定粒子纹理（此处使用 defaultAlbedo 占位）
    VansTexture* defaultParticleTex =
        static_cast<VansTexture*>(GetTextureAsset("defaultAlbedo"));
    for (auto* node : m_ParticleRenderNodes)
    {
        if (node == nullptr) continue;
        node->SetupDescriptors(m_GlobalDescriptorSetLayout,
                               m_GlobalDescriptorSet,
                               defaultParticleTex);
    }
}

// ============================================================
// Global Descriptor Set (Set 0) — shared across all render nodes
// Contains: Camera, Lights, Materials, IBL, Bindless textures
// ============================================================
void VansGraphics::VansScene::CreateGlobalDescriptorSet(VkDevice device)
{
    auto descManager = VansVKDescriptorManager::GetInstance();

    // Create global layout + set via factory
    std::vector<VkDescriptorSet> sets;
    VansDescriptorSetLayoutFactory::CreateAndAllocate_Global(m_GlobalDescriptorSetLayout, sets);
    m_GlobalDescriptorSet = sets[0];

    // Create object layout + set (Set 2: scene transforms + draw-instance records)
    std::vector<VkDescriptorSet> objSets;
    VansDescriptorSetLayoutFactory::CreateAndAllocate_Object(m_ObjectDescriptorSetLayout, objSets);
    m_ObjectDescriptorSet = objSets[0];

    // Write Set 2: binding 0 (Transform SSBO)
    descManager->BeginDescriptorUpdate();
    descManager->WriteBufferDescriptor(
        m_ObjectDescriptorSet,
        OBJECT_BINDING_TRANSFORM_SSBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_InstanceTransformDataBuffer.GetNativeBuffer(),
            0,
            m_InstanceTransformDataBuffer.GetBufferSize()
        }});
    descManager->CommitDescriptorUpdates();

    // Create shared dummy vertex-deformation buffers and Set 3 for static nodes.
    // Skinned geometry allocates per-node Set 3 with real buffers.
    m_DummyBoneIDBuffer.CreatVulkanBuffer(device, 64, VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_DummyBoneBuffer.CreatVulkanBuffer(device, 64, VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_DummyWeightBuffer.CreatVulkanBuffer(device, 64, VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    std::vector<VkDescriptorSet> animSets;
    VansDescriptorSetLayoutFactory::CreateAndAllocate_VertexDeformation(m_VertexDeformationDescriptorSetLayout, animSets);
    m_VertexDeformationDescriptorSet = animSets[0];

    descManager->BeginDescriptorUpdate();
    // binding 0: Dummy Bone ID SSBO (per-submesh bone IDs)
    descManager->WriteBufferDescriptor(
        m_VertexDeformationDescriptorSet,
        VERTEX_DEFORMATION_BINDING_BONEID_SSBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ m_DummyBoneIDBuffer.GetNativeBuffer(), 0, 64 }});
    // binding 1: Dummy Bone Matrices SSBO
    descManager->WriteBufferDescriptor(
        m_VertexDeformationDescriptorSet,
        VERTEX_DEFORMATION_BINDING_BONE_SSBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ m_DummyBoneBuffer.GetNativeBuffer(), 0, 64 }});
    // binding 2: Dummy Bone Weight SSBO (per-submesh bone weights)
    descManager->WriteBufferDescriptor(
        m_VertexDeformationDescriptorSet,
        VERTEX_DEFORMATION_BINDING_BONEWEIGHT_SSBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ m_DummyWeightBuffer.GetNativeBuffer(), 0, 64 }});
    // binding 3: Dummy previous Bone Matrices SSBO
    descManager->WriteBufferDescriptor(
        m_VertexDeformationDescriptorSet,
        VERTEX_DEFORMATION_BINDING_PREVIOUS_BONE_SSBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ m_DummyBoneBuffer.GetNativeBuffer(), 0, 64 }});
    descManager->CommitDescriptorUpdates();

    // Create empty pass layout (Set 1) for passes with no per-pass resources
    std::vector<VkDescriptorSet> emptySets;
    VansDescriptorSetLayoutFactory::CreateAndAllocate_Empty(m_EmptyPassLayout, emptySets);
    m_EmptyPassDescriptorSet = emptySets[0];

    // Write all global resources into Set 0
    UpdateGlobalDescriptorSet();
}

void VansGraphics::VansScene::UpdateGlobalDescriptorSet()
{
	auto* vkDevice = static_cast<VansVKDevice*>(m_GraphicsDevice);
	if (vkDevice == nullptr || !vkDevice->GetDrawInstanceArena().IsReady())
	{
		VANS_LOG_ERROR("[VansScene] Cannot update global descriptors before backend draw-instance resources are ready.");
		return;
	}
	const VansVKBuffer& drawInstanceBuffer = vkDevice->GetDrawInstanceArena().GetBuffer();
    auto descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();

    // Binding 0: Camera UBO
    descManager->WriteBufferDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_CAMERA_UBO,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        {{
			vkDevice->GetCameraDataBuffer().GetNativeBuffer(),
            0,
			vkDevice->GetCameraDataBuffer().GetBufferSize()
        }});

    // Binding 1: Lights SSBO
    descManager->WriteBufferDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_LIGHTS_UBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{
			vkDevice->GetLightDataBuffer().GetNativeBuffer(),
			0,
			vkDevice->GetLightDataBuffer().GetBufferSize()
		}});

    // Binding 2: Material SSBO
    descManager->WriteBufferDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_MATERIAL_SSBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_MaterialManager.m_GlobalPBRDataBuffer.GetNativeBuffer(),
            0,
            m_MaterialManager.m_GlobalPBRDataBuffer.GetBufferSize()
        }});

    // Binding 15: Custom material payload SSBO
    descManager->WriteBufferDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_CUSTOM_MATERIAL_SSBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_MaterialManager.m_GlobalCustomMaterialDataBuffer.GetNativeBuffer(),
            0,
            m_MaterialManager.m_GlobalCustomMaterialDataBuffer.GetBufferSize()
        }});
    descManager->WriteBufferDescriptor(
        m_ObjectDescriptorSet,
        OBJECT_BINDING_DRAW_INSTANCE_SSBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            drawInstanceBuffer.GetNativeBuffer(),
            0,
            drawInstanceBuffer.GetBufferSize()
        }});

    // Binding 16: Cloth extension payloads; indices match Binding 2 exactly.
    descManager->WriteBufferDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_CLOTH_MATERIAL_SSBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_MaterialManager.m_GlobalClothDataBuffer.GetNativeBuffer(),
            0,
            m_MaterialManager.m_GlobalClothDataBuffer.GetBufferSize()
        }});

    // Binding 17: Tree leaf extension payloads; indices match Binding 2 exactly.
    descManager->WriteBufferDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_TREE_LEAF_MATERIAL_SSBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_MaterialManager.m_GlobalTreeLeafDataBuffer.GetNativeBuffer(),
            0,
            m_MaterialManager.m_GlobalTreeLeafDataBuffer.GetBufferSize()
        }});

    // Binding 18: Skin extension payloads; indices match Binding 2 exactly.
    descManager->WriteBufferDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_SKIN_MATERIAL_SSBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_MaterialManager.m_GlobalSkinDataBuffer.GetNativeBuffer(),
            0,
            m_MaterialManager.m_GlobalSkinDataBuffer.GetBufferSize()
        }});

    // Binding 3: BRDF LUT
    descManager->WriteImageDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_BRDF_LUT,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            m_MaterialManager.m_BRDFIntegralLUT->GetImage().GetSampler(),
            m_MaterialManager.m_BRDFIntegralLUT->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }});

    // Binding 4: 启动时由固定 SkyBox 一次性卷积得到的 diffuse IBL。
    descManager->WriteImageDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_PRECONV_DIFFUSE,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            m_MaterialManager.m_PreConvDiffuse->GetImage().GetSampler(),
            m_MaterialManager.m_PreConvDiffuse->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }});

    // Binding 5: 启动时由固定 SkyBox 一次性卷积得到的 specular IBL。
    descManager->WriteImageDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_PRECONV_SPECULAR,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            m_MaterialManager.m_PreConvSpecular->GetImage().GetSampler(),
            m_MaterialManager.m_PreConvSpecular->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }});

    // Binding 6: 同一固定 SkyBox 的 SH 系数。
    descManager->WriteBufferDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_SH_COEFFICIENTS,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_MaterialManager.m_SkySHResultBuffer.GetNativeBuffer(),
            0,
            m_MaterialManager.m_SkySHResultBuffer.GetBufferSize()
        }});

    // Binding 7: Skin pre-integrated BSDF LUT
    descManager->WriteImageDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_SKIN_BSDF_LUT,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            m_MaterialManager.m_SkinBSDFLUT->GetImage().GetSampler(),
            m_MaterialManager.m_SkinBSDFLUT->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }});

    // Binding 19: Skin profile pre-integrated LUT array.
    descManager->WriteImageDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_SKIN_PROFILE_LUT_ARRAY,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            m_MaterialManager.m_SkinProfileLUTArray->GetImage().GetSampler(),
            m_MaterialManager.m_SkinProfileLUTArray->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }});

    // Binding 8: Cloth pre-integrated BRDF LUT
    descManager->WriteImageDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_CLOTH_BRDF_LUT,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            m_MaterialManager.m_ClothBRDFLUT->GetImage().GetSampler(),
            m_MaterialManager.m_ClothBRDFLUT->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }});

    // Binding 11/12: LTC LUTs (area-light BRDF, runtime-uploaded RGBA16F 64x64)
    if (m_MaterialManager.m_LTC1 && m_MaterialManager.m_LTC2)
    {
        descManager->WriteImageDescriptor(
            m_GlobalDescriptorSet,
            GLOBAL_BINDING_LTC1_LUT,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{
                m_MaterialManager.m_LTC1->GetImage().GetSampler(),
                m_MaterialManager.m_LTC1->GetImage().GetImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            }});
        descManager->WriteImageDescriptor(
            m_GlobalDescriptorSet,
            GLOBAL_BINDING_LTC2_LUT,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{
                m_MaterialManager.m_LTC2->GetImage().GetSampler(),
                m_MaterialManager.m_LTC2->GetImage().GetImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            }});
    }

    // Binding 50: Bindless PBR textures
    auto& textures = m_MaterialManager.m_GlobalPBRTextures;
    if (!textures.empty())
    {
        std::vector<VkDescriptorImageInfo> bindlessInfos;
        bindlessInfos.reserve(textures.size());
        for (size_t i = 0; i < textures.size(); ++i)
        {
            bindlessInfos.push_back({
                textures[i]->GetSampler(),
                textures[i]->GetImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            });
        }
        descManager->WriteImageDescriptor(
            m_GlobalDescriptorSet,
            GLOBAL_BINDING_BINDLESS_TEXTURES,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            bindlessInfos);
    }

    descManager->CommitDescriptorUpdates();
}

// NOTE: TileLight bindings (9 and 10) are written in VansVKDevice::UpdateGlobalTileLightDesc
//       after PrepareTileLightData allocates m_TileLightHeaderBuffer and m_TileLightIndexBuffer.

void VansGraphics::VansScene::UpdateGlobalTileLightDescriptors()
{
    auto descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();

    // Binding 9: TileLight Header SSBO
    descManager->WriteBufferDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_TILE_LIGHT_GRID,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_MaterialManager.m_TileLightHeaderBuffer.GetNativeBuffer(),
            0,
            m_MaterialManager.m_TileLightHeaderBuffer.GetBufferSize()
        }});

    // Binding 10: TileLight Index SSBO
    descManager->WriteBufferDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_TILE_LIGHT_INDICES,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_MaterialManager.m_TileLightIndexBuffer.GetNativeBuffer(),
            0,
            m_MaterialManager.m_TileLightIndexBuffer.GetBufferSize()
        }});

    descManager->CommitDescriptorUpdates();
}

void VansGraphics::VansScene::PrepareReflectionProbeRuntime(VansVKDevice& device)
{
    if (m_ReflectionProbeSystem.GetPlacementSettings().enabled &&
        m_ReflectionProbeSystem.GetProbes().size() <= 1)
    {
        m_ReflectionProbeSystem.GenerateAutoProbes(*this, true);
    }
    m_ReflectionProbeSystem.CreateGPUResources(device, device.GetImmediateGraphicsCommandBuffer());
    m_ReflectionProbeSystem.UpdateGlobalDescriptors(m_GlobalDescriptorSet);
}

void VansGraphics::VansScene::BindWaterSystemGlobalDescriptors()
{
    if (m_WaterSystem)
    {
        m_WaterSystem->SetGlobalDescriptorSet(
            m_GlobalDescriptorSetLayout,
            m_GlobalDescriptorSet);
    }
}

void VansGraphics::VansScene::BindMaterialVideoDescriptorSet()
{
    m_MaterialManager.m_VideoBindlessDescriptorSet = m_GlobalDescriptorSet;
}

void VansGraphics::VansScene::PlayAllSceneVideos()
{
    m_VideoManager.PlayAll();
}

void VansGraphics::VansScene::SetWaterRuntimeConfig(const VansWaterConfig& config, VansWaterMaterial* material)
{
    m_WaterMaterial = material;
    if (m_WaterMaterial)
        m_WaterMaterial->m_Config = config;
    m_HasWater = material != nullptr;
}

const VansGraphics::VansWaterConfig& VansGraphics::VansScene::GetWaterConfig() const
{
    static const VansWaterConfig defaultConfig;
    return m_WaterMaterial ? m_WaterMaterial->m_Config : defaultConfig;
}

VansGraphics::VansWaterConfig& VansGraphics::VansScene::EditWaterConfig()
{
    static VansWaterConfig fallbackConfig;
    return m_WaterMaterial ? m_WaterMaterial->m_Config : fallbackConfig;
}

void VansGraphics::VansScene::SetTerrainPhysicsNode(VansEngine::VansTerrainPhysicsNode* terrainPhysicsNode)
{
    if (m_TerrainPhysicsNode && m_TerrainPhysicsNode != terrainPhysicsNode)
        delete m_TerrainPhysicsNode;
    m_TerrainPhysicsNode = terrainPhysicsNode;
}

VansGraphics::MultiMeshGroup* VansGraphics::VansScene::FindAnimationMultiMeshGroup(
    const std::string& meshGroupName,
    const std::string& objectName)
{
    auto groupIt = m_MultiMeshGroups.find(meshGroupName);
    if (groupIt != m_MultiMeshGroups.end())
        return &groupIt->second;

    for (auto& entry : m_MultiMeshGroups)
    {
        MultiMeshGroup& group = entry.second;
        if (group.parentName == objectName ||
            (group.sourceMesh && group.sourceMesh->m_AssetName == meshGroupName))
            return &group;
    }
    return nullptr;
}

VansGraphics::VansRenderNode* VansGraphics::VansScene::FindRenderNodeByName(const std::string& name) const
{
    // Search across all render node lists that store mesh nodes
    for (auto* node : m_OpaqueRenderNodes)
        if (node && node->m_NodeName == name) return node;
	for (auto* node : m_HairRenderNodes)
		if (node && node->m_NodeName == name) return node;
	for (auto* node : m_ForwardOpaquePreAtmosphereRenderNodes)
		if (node && node->m_NodeName == name) return node;
    for (auto* node : m_TransParentRenderNodes)
        if (node && node->m_NodeName == name) return node;
    if (m_TerrainRenderNode && m_TerrainRenderNode->m_NodeName == name) return m_TerrainRenderNode;
    if (m_VegetationRenderNode && m_VegetationRenderNode->m_NodeName == name) return m_VegetationRenderNode;
    return nullptr;
}

void VansGraphics::VansScene::UpdateActionsEarly(double deltaSeconds)
{
	if (m_CameraControlArbiter)
		m_CameraControlArbiter->CoreRuntime().Advance(deltaSeconds);
	if (!m_GameplayRuntime || !m_RuntimeWorld || !m_GameplayRuntime->IsInitialized())
		return;
	m_GameplayRuntime->SynchronizeHostEnablement(*m_RuntimeWorld);
	m_GameplayRuntime->TickEarly(deltaSeconds);
	if (m_CombatActionService)
		m_CombatActionService->Tick(deltaSeconds);
}

void VansGraphics::VansScene::UpdateAI(double deltaSeconds)
{
	if (m_AIWorld)
		m_AIWorld->Update(deltaSeconds);
}

bool VansGraphics::VansScene::RunActionLateContinuation()
{
	return m_GameplayRuntime && m_GameplayRuntime->IsInitialized() &&
		m_GameplayRuntime->RunLateContinuation();
}

void VansGraphics::VansScene::UnLoadScene()
{
    VANS_ASSERT_MAIN_THREAD();
	++m_RenderSceneEpoch;

	VANS_LOG("[VansScene] UnLoadScene started");
	// Scene CPU 对象销毁前只发布稳定句柄 destroy；RT 不得接收即将悬空的 node 指针。
	for (const auto& [node, binding] : m_MainRenderProxyBindings)
	{
		(void)node;
		m_PendingRenderMutations.AddDestroy(binding.handle);
		if (!m_RenderProxyHandleAllocator.Release(binding.handle))
			VANS_LOG_ERROR("[VansScene] Failed to release render proxy handle during scene unload.");
	}
	m_MainRenderProxyBindings.clear();
	if (m_AIWorld)
		m_AIWorld->Shutdown();
	if (m_GameplayRuntime)
		m_GameplayRuntime->Shutdown();
	if (m_TimelineRuntime)
		m_TimelineRuntime->Clear();
	if (m_VirtualCameraParameters)
		m_VirtualCameraParameters->Clear();
	if (m_RuntimeWorld)
		m_RuntimeWorld->FlushCommands();
	ReleaseAudioSourceBindings();
	if (m_RuntimeWorld)
		m_RuntimeWorld->Clear();
	m_GameplayRuntime.reset();
	m_CombatActionService.reset();
	m_AIWorld.reset();
	if (m_CameraControlArbiter)
	{
		m_CameraControlArbiter->Clear(m_Camera);
		m_CameraControlArbiter->CoreRuntime().Clear();
	}

	VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	VkDevice nativeDevice = vkDevice ? vkDevice->GetLogicDevice() : VK_NULL_HANDLE;
	if (vkDevice && m_AtmosphereSystem)
		vkDevice->WaitForDevice();
	ShutdownEnvironmentRendering();
	m_AudioEnvironmentReverbWetGain = 1.0f;
	VansEngine::VansAudioSystem::GetInstance().SetDefaultReverbWetGain(m_AudioEnvironmentReverbWetGain);
	if (nativeDevice != VK_NULL_HANDLE)
		m_ReflectionProbeSystem.Clear(nativeDevice);

	// ── 0. 清除编辑器选中状态 ─────────────────────────────────────────────
    VANS_UNLOAD_STEP(0, "Clear editor selection state");
	VANS_LOG("[VansScene] Step 0: editor selection cleared");

	// ── 1. 清理场景级运行时纹理，保留屏幕空间纹理 ──
	//  SSGI / SSAO / HZB / SSR 等屏幕空间纹理在 PrepareRenderingData()
	//  时创建，不依赖场景内容，无需在场景切换时销毁。
	VANS_UNLOAD_STEP(1, "娓呯悊鍦烘櫙绾ц繍琛屾椂绾圭悊");
	m_MaterialManager.m_SSGITemporalFrame = 0;
	// DDGI region 资源重建后，标记渲染 Feature 的 descriptor set 需要重新写入
	if (vkDevice)
	{
		vkDevice->ResetFeatureDescriptorSets();
	}
	VANS_LOG("[VansScene] Step 1: scene runtime textures cleared; screen-space textures retained");

	// ── 2. 清理脚本对象（组件 Destroy 负责自身生命周期收尾）─────────────────
	VANS_UNLOAD_STEP(2, "Clear script objects and script modules");
	for (auto* obj : m_SceneObjects)
	{
		delete obj;
	}
	m_SceneObjects.clear();
	++m_SceneObjectCollectionGeneration;
	VANS_LOG("[VansScene] Step 2a: SceneObjects released");
    VansParticleManager::Instance().Shutdown();

	// ScriptContext 中的 tracked modules 也一并清理。
	if (VansScriptContext::GetInstance())
	{
		VansScriptContext::GetInstance()->ClearTrackedModules();
	}
	VANS_LOG("[VansScene] Step 2b: script modules and particles cleared");

	// ── 3-5. 清理物理节点 / 载具 / 布料（需要持有物理线程锁）─────────────
	// 物理模拟在独立线程运行，必须先获得 SimulationMutex 再操作 PxScene。
    VANS_UNLOAD_STEP("3-5", "Clear physics, vehicle, cloth and character controller nodes");
	{
		auto& physicsSystem = VansEngine::VansPhysicsSystem::GetInstance();
		std::lock_guard<std::mutex> simLock(physicsSystem.GetSimulationMutex());

        // ── 2c. 清理布娃娃系统（直接持有 PxD6Joint / PxRigidDynamic）──────
        VansEngine::VansRagdollSystem::GetInstance().Shutdown();
        VANS_LOG("[VansScene] Step 2c: Ragdoll 绯荤粺宸叉竻鐞?(鎸侀攣)");

		// ── 3. 清理物理节点（析构函数会从 PxScene 移除 actor）─────────
		for (auto* physicsNode : m_PhysicsNodes)
		{
			if (physicsNode)
			{
				delete physicsNode;
			}
		}
		m_PhysicsNodes.clear();
		VANS_LOG("[VansScene] Step 3: 鐗╃悊鑺傜偣宸叉竻鐞?(鎸侀攣)");

        // ── 3b. 清理地形高度场碰撞 ─────────────────────────────────────
        if (m_TerrainPhysicsNode)
        {
            delete m_TerrainPhysicsNode;
            m_TerrainPhysicsNode = nullptr;
        }
        VANS_LOG("[VansScene] Step 3b: terrain physics nodes cleared under lock");

		// ── 4. 清理载具 ──────────────────────────────────────────────────
		if (m_Vehicle)
		{
			delete m_Vehicle;
			m_Vehicle = nullptr;
		}
            VANS_LOG("[VansScene] Step 4: vehicles cleared");

		// ── 5. 清理布料节点和 staging buffer ─────────────────────────────
		for (auto* clothNode : m_ClothNodes)
		{
			if (clothNode)
			{
				clothNode->Shutdown();
				delete clothNode;
			}
		}
		m_ClothNodes.clear();
            VANS_LOG("[VansScene] Step 5: cloth nodes cleared");

		// ── 5b. 清理角色控制器节点 ──────────────────────────────────────
		for (auto* cctNode : m_CharControllerNodes)
		{
			if (cctNode)
			{
				cctNode->Release();
				delete cctNode;
			}
		}
		m_CharControllerNodes.clear();
		VANS_LOG("[VansScene] Step 5b: 瑙掕壊鎺у埗鍣ㄨ妭鐐瑰凡娓呯悊");
	} // 释放 SimulationMutex

    VANS_UNLOAD_STEP("5b", "娓呯悊甯冩枡 staging buffer");
	for (auto& stagingBuf : m_ClothStagingBuffers)
	{
		if (stagingBuf.IsMapped())
			stagingBuf.Unmap();
		stagingBuf.DestroyVulkanBuffer(nativeDevice);
	}
	m_ClothStagingBuffers.clear();
        VANS_LOG("[VansScene] Step 5b: cloth staging buffers cleared");

	// ── 6. 清理 transform 父子系统 ───────────────────────────────────────
    VANS_UNLOAD_STEP(6, "娓呯悊 transform 鐖跺瓙绯荤粺");
	m_TransformGraph.Clear();
	m_SkeletonAnchorRegistry.Clear();
        VANS_LOG("[VansScene] Step 6: transform parent system cleared");

	// ── 7. 清理植被系统 ─────────────────────────────────────────────────
    VANS_UNLOAD_STEP(7, "娓呯悊妞嶈绯荤粺");
	if (m_VegetationSystem)
	{
		m_VegetationSystem->Cleanup(nativeDevice);
		delete m_VegetationSystem;
		m_VegetationSystem = nullptr;
	}
        VANS_LOG("[VansScene] Step 7: vegetation system cleared");

	// ── 8. 清理所有渲染节点（必须在动画节点之前，因为渲染节点的 descriptor
	//       set 引用了动画节点的 bone buffer，需要在 buffer 销毁前释放 set）
    VANS_UNLOAD_STEP(8, "Clear render nodes");
	auto deleteRenderNode = [](VansRenderNode* node) {
		if (node) delete node;
	};

	for (auto* node : m_OpaqueRenderNodes)
		deleteRenderNode(node);
	m_OpaqueRenderNodes.clear();

	for (auto* node : m_HairRenderNodes)
		deleteRenderNode(node);
	m_HairRenderNodes.clear();

	for (auto* node : m_ForwardOpaquePreAtmosphereRenderNodes)
		deleteRenderNode(node);
	m_ForwardOpaquePreAtmosphereRenderNodes.clear();

	for (auto* node : m_TransParentRenderNodes)
		deleteRenderNode(node);
	m_TransParentRenderNodes.clear();

	for (auto* node : m_PostProcessRenderNodes)
		deleteRenderNode(node);
	m_PostProcessRenderNodes.clear();

	for (auto* node : m_ScreenSpaceRenderNodes)
		deleteRenderNode(node);
	m_ScreenSpaceRenderNodes.clear();

	// 贴花节点清理
	for (auto* node : m_DecalRenderNodes)
		deleteRenderNode(node);
	m_DecalRenderNodes.clear();

	// 粒子渲染节点清理
	for (auto* node : m_ParticleRenderNodes)
		deleteRenderNode(node);
	m_ParticleRenderNodes.clear();

	deleteRenderNode(m_DeferredNode);
	m_DeferredNode = nullptr;

	deleteRenderNode(m_TerrainRenderNode);
	m_TerrainRenderNode = nullptr;

	deleteRenderNode(m_WaterRenderNode);
	m_WaterRenderNode = nullptr;
	m_WaterMaterial   = nullptr; // Material ownership is released with the scene material registry.

	// 释放水面系统（VansWaterSystem 管理 Wave/GBuf/Compute/Composite GPU 资源）
	if (m_WaterSystem)
	{
		m_WaterSystem->Shutdown();
		delete m_WaterSystem;
		m_WaterSystem = nullptr;
	}
	m_HasWater = false;

	// VegetationRenderNode 未被列表持有，需单独 delete
	deleteRenderNode(m_VegetationRenderNode);
	m_VegetationRenderNode = nullptr;
        VANS_LOG("[VansScene] Step 8: render nodes cleared");

	// ── 9. 先清理目标动画控制器。Rig 非拥有地绑定 Node Skeleton，
	// 因此 Controller 必须在拥有 Skeleton 的 AnimationNode 之前析构。
	VANS_UNLOAD_STEP(9, "Clear animation controllers");
	for (auto* ctrl : m_AnimationControllers)
	{
		delete ctrl;
	}
	m_AnimationControllers.clear();
	VANS_LOG("[VansScene] Step 9: animation controllers cleared");

	// ── 9b. 清理动画节点（析构函数会销毁 GPU bone buffer）──────────────
	VANS_UNLOAD_STEP("9b", "Clear animation nodes");
	m_EditorPreviewDrivenAnimationNodes.clear();
	for (auto* animNode : m_AnimationNodes)
	{
		delete animNode;
	}
	m_AnimationNodes.clear();
	VANS_LOG("[VansScene] Step 9b: animation nodes cleared");

	// ── 10. 清理 Multi-mesh 分组 ────────────────────────────────────────
	VANS_UNLOAD_STEP(10, "Clear multi-mesh groups and submesh lookup entries");
	VANS_LOG("[VansScene] Step 10: clearing multi-mesh groups (count=" << m_MultiMeshGroups.size() << ")");
	for (auto& [groupName, group] : m_MultiMeshGroups)
	{
		if (group.ownsSharedTransform)
			VansTransformStore::FreeTransform(group.sharedTransformID);
	}
	m_MultiMeshGroups.clear();

    // 子网格对象本身由父级 multi-mesh 的 m_SubMeshes 拥有，此处仅清除非拥有查找列表，
    // 防止下次 ExpandMultiMeshToRenderNodes 时产生重复。
    m_AssetRegistry.ClearSceneSubMeshes();

        VANS_LOG("[VansScene] Step 10: multi-mesh groups cleared");

	// ── 11. 清理材质（场景级，指针由 Scene 拥有）───────────────────────
    VANS_UNLOAD_STEP(11, "Clear scene materials");
	VANS_LOG("[VansScene] Step 11: clearing materials (count=" << m_AssetRegistry.GetMaterials().size() << ")");
	for (size_t i = 0; i < m_AssetRegistry.GetMaterials().size(); ++i)
	{
		auto* mat = m_AssetRegistry.GetMaterials()[i];
		if (mat)
		{
			auto* realMat = static_cast<VansMaterial*>(mat);
			VANS_LOG("[VansScene] Step 11: 鍒犻櫎鏉愯川 [" << i << "] type=" << realMat->m_MaterialType << " name=" << mat->m_AssetName);
			delete mat;
		}
	}
	m_AssetRegistry.ClearMaterials();
	ClearSceneAssetLookup();
	VANS_LOG("[VansScene] Step 10-11: Multi-mesh 鍜屾潗璐ㄥ凡娓呯悊");

	// ── 12. 清理全局 PBR 数据和 descriptor ──────────────────────────────
    VANS_UNLOAD_STEP(12, "娓呯悊鍏ㄥ眬 PBR 鏁版嵁鍜?descriptor");
	m_MaterialManager.ClearScenePBRData(nativeDevice);

	// ── 13. 清理 Main-owned 灯光输入；backend 通过 scene epoch 重置阴影状态 ──
	VANS_UNLOAD_STEP(13, "娓呯悊鐏厜 CPU 鏁版嵁");
	m_LightManager.ClearLights();

	// IES profile GPU 纹理数组（sampler2DArray，binding=16）
	m_IESProfileManager.DestroyGPUResources(nativeDevice);
        VANS_LOG("[VansScene] Step 12-13: PBR and light GPU resources cleared");

	// ── 14. 清理 Ray Tracing TLAS 资源 ─────────────────────────────────
    VANS_UNLOAD_STEP(14, "娓呯悊 Ray Tracing TLAS/BLAS 鍦烘櫙璧勬簮");
	if (vkDevice)
	{
		vkDevice->GetRayTracingContext().CleanupSceneResources(nativeDevice, &m_MaterialManager);
	}

	// 清理 Scene 持有的 TLAS 数据
	if (vkDevice && m_TopLevelAS != VK_NULL_HANDLE)
	{
		vkDevice->DestroyAccelerationStructure(m_TopLevelAS);
		m_TopLevelAS = VK_NULL_HANDLE;
	}
	m_TopLevelASBuffer.DestroyVulkanBuffer(nativeDevice);
	m_InstancesBuffer.DestroyVulkanBuffer(nativeDevice);
	m_TLASScratchBuffer.DestroyVulkanBuffer(nativeDevice);

	m_TlasInstancesInfos.clear();
	m_AsGeometry.clear();
	m_AsBuildRangeInfo.clear();

	// BLAS vertex/index data（缓存的引用，不销毁实际的 mesh buffer）
	m_BLASVertexData.clear();
	m_BLASIndexData.clear();
	m_TLASInstaneData.clear();
	m_TlasInstanceTextureIndex.clear();
	m_TlasInstanceGIEmission.clear();
	m_TlasInstanceTextures.clear();
	m_TlasInstanceMaterialToIndex.clear();

	// 释放项目级 mesh 及其拥有的子网格 BLAS。multi-mesh 子网格与普通网格
	// 一样可被 TLAS 实例化，因此生命周期必须在这里统一收口。
	for (const auto& meshAsset : m_AssetRegistry.GetMeshes())
	{
		VansMesh* mesh = static_cast<VansMesh*>(meshAsset);
		if (mesh->m_SupportRayTracing)
		{
			mesh->DestroyBLAS(*vkDevice);
		}
		for (VansMesh* subMesh : mesh->m_SubMeshes)
		{
			if (subMesh && subMesh->m_SupportRayTracing)
				subMesh->DestroyBLAS(*vkDevice);
		}
	}
        VANS_LOG("[VansScene] Step 14: RT/TLAS resources cleared");

	// ── 15. 清理 Instance Transform Buffer ──────────────────────────────
	VANS_UNLOAD_STEP(15, "Clear instance-transform buffers and descriptors");
	m_InstanceTransformDataBuffer.DestroyVulkanBuffer(nativeDevice);
	m_InstanceTransformData.clear();

	// ── 重置 Transform Slot Allocator（必须在 DestroyVulkanBuffer 之后、下次 Prepare 之前）──
	m_TransformSlotAllocator.Reset();

	// 释放 Transform Data descriptor set 和 layout
	auto descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_GlobalTransformDataDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GlobalTransformDataSetLayout);

	// ── 16. 清理 Global / Object / Animation / Empty Descriptor Sets ─────
    VANS_UNLOAD_STEP(16, "娓呯悊 Global/Object/Animation/Empty descriptor sets");
	if (m_GlobalDescriptorSet != VK_NULL_HANDLE)
	{
		std::vector<VkDescriptorSet> tmp = { m_GlobalDescriptorSet };
		descMgr->DestroyDescriptorSet(tmp);
		m_GlobalDescriptorSet = VK_NULL_HANDLE;
	}
	descMgr->DestroyDescriptorSetLayout(m_GlobalDescriptorSetLayout);

	if (m_ObjectDescriptorSet != VK_NULL_HANDLE)
	{
		std::vector<VkDescriptorSet> tmp = { m_ObjectDescriptorSet };
		descMgr->DestroyDescriptorSet(tmp);
		m_ObjectDescriptorSet = VK_NULL_HANDLE;
	}
	descMgr->DestroyDescriptorSetLayout(m_ObjectDescriptorSetLayout);

	if (m_VertexDeformationDescriptorSet != VK_NULL_HANDLE)
	{
		std::vector<VkDescriptorSet> tmp = { m_VertexDeformationDescriptorSet };
		descMgr->DestroyDescriptorSet(tmp);
		m_VertexDeformationDescriptorSet = VK_NULL_HANDLE;
	}
	descMgr->DestroyDescriptorSetLayout(m_VertexDeformationDescriptorSetLayout);

	if (m_EmptyPassDescriptorSet != VK_NULL_HANDLE)
	{
		std::vector<VkDescriptorSet> tmp = { m_EmptyPassDescriptorSet };
		descMgr->DestroyDescriptorSet(tmp);
		m_EmptyPassDescriptorSet = VK_NULL_HANDLE;
	}
	descMgr->DestroyDescriptorSetLayout(m_EmptyPassLayout);

	// ── 17. 清理 Dummy Bone Buffer ──────────────────────────────────────
    VANS_UNLOAD_STEP(17, "娓呯悊 Dummy Bone Buffer");
	m_DummyBoneIDBuffer.DestroyVulkanBuffer(nativeDevice);
	m_DummyBoneBuffer.DestroyVulkanBuffer(nativeDevice);
	m_DummyWeightBuffer.DestroyVulkanBuffer(nativeDevice);

	// ── 18. 暂停视频播放（视频为项目级资源，GPU 纹理保留，切换场景 Play 时复用）────────
    VANS_UNLOAD_STEP(18, "Pause project videos");
	m_VideoManager.PauseAll();

	// ── 19. 停止所有音频播放（音频为项目级资源，不释放已解码数据）────────
    VANS_UNLOAD_STEP(19, "Stop project audio");
	m_AudioManager.StopAll();
    m_SceneState = VansSceneState::Empty;

	VANS_LOG("[VansScene] Scene unloaded");
}

void VansGraphics::VansScene::UnloadProjectResources(VansVKDevice* device)
{
    VANS_ASSERT_MAIN_THREAD();
	// GPU/RenderThread barrier 由上层场景切换或关闭编排统一建立。Scene 只负责
	// 项目资源所有权释放，不能绕过 VansRenderSystem 再制造 device-wide stall。

    ReleaseAudioSourceBindings();
    m_VideoManager.Clear();
    m_AudioManager.Clear();

    // Shaders are owned by VansShaderManager. The scene shader list is only the legacy
    // scene lookup view used by GetShaderAsset(), so do not delete entries here.
    m_AssetRegistry.ClearShaders();

    for (auto* texture : m_AssetRegistry.GetTextures())
    {
        delete texture;
    }
    m_AssetRegistry.ClearTextures();

    // Parent multi-mesh assets own their submeshes. The scene submesh list is a
    // non-owning lookup list and must be cleared before deleting project meshes.
    m_AssetRegistry.ClearSceneSubMeshes();
    for (auto* mesh : m_AssetRegistry.GetMeshes())
    {
        delete mesh;
    }
    m_AssetRegistry.ClearMeshes();

	m_AssetRegistry.ClearProjectMeshAliases();
    RebuildAssetLookup();
    m_ResourcesLoaded = false;
	m_UsingPackagedProjectAssets = false;
    VANS_LOG("[VansScene] Project resources unloaded");
}

VansGraphics::VansScene::MainRenderProxyBinding*
VansGraphics::VansScene::EnsureMainRenderProxyBinding(
	VansRenderNode* node,
	VansRenderMutationBatch& mutations)
{
	VANS_ASSERT_MAIN_THREAD();
	if (node == nullptr)
		return nullptr;

	const VansRenderProxyStaticData staticData{
		node->m_TransfromIndex >= 0
			? static_cast<std::uint32_t>(node->m_TransfromIndex)
			: VANS_INVALID_RENDER_TRANSFORM_SLOT,
		node->IsEnabled()
	};
	auto bindingIt = m_MainRenderProxyBindings.find(node);
	if (bindingIt == m_MainRenderProxyBindings.end())
	{
		MainRenderProxyBinding binding;
		binding.handle = m_RenderProxyHandleAllocator.Allocate();
		binding.staticData = staticData;
		bindingIt = m_MainRenderProxyBindings.emplace(node, binding).first;
		mutations.AddCreate(binding.handle, staticData);
	}
	else if (bindingIt->second.staticData != staticData)
	{
		bindingIt->second.staticData = staticData;
		mutations.AddUpdate(bindingIt->second.handle, staticData);
	}
	return &bindingIt->second;
}

VansGraphics::VansRenderProxyHandle
VansGraphics::VansScene::FindMainRenderProxyHandle(const VansRenderNode* node) const
{
	const auto bindingIt = m_MainRenderProxyBindings.find(node);
	return bindingIt != m_MainRenderProxyBindings.end()
		? bindingIt->second.handle
		: VansRenderProxyHandle{};
}

void VansGraphics::VansScene::ReleaseMainRenderProxyBinding(
	VansRenderNode* node,
	VansRenderMutationBatch& mutations)
{
	VANS_ASSERT_MAIN_THREAD();
	const auto bindingIt = m_MainRenderProxyBindings.find(node);
	if (bindingIt == m_MainRenderProxyBindings.end())
		return;
	mutations.AddDestroy(bindingIt->second.handle);
	if (!m_RenderProxyHandleAllocator.Release(bindingIt->second.handle))
		VANS_LOG_ERROR("[VansScene] Failed to release render proxy handle.");
	m_MainRenderProxyBindings.erase(bindingIt);
}

std::optional<VansGraphics::VansRenderFrameSourceOutput>
VansGraphics::VansScene::PrepareMainThreadRenderFrame(
    const VansRenderFramePreparationContext& context)
{
    VANS_ASSERT_MAIN_THREAD();
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::RenderPrep);
	VansRenderFrameSourceOutput output;
	output.mutationsBeforeFrame.Append(std::move(m_PendingRenderMutations));
	output.scene.sceneEpoch = m_RenderSceneEpoch;
    if (!IsSceneReady())
		return output;

    VANS_PROFILE_SCOPE("Scene::PrepareMainThreadRenderFrame", Vans::ProfileCategory::RenderPrepare);
    const VansRenderViewSnapshot& view = context.view;
    const float deltaTime = static_cast<float>(context.timing.deltaSeconds);
	VansRenderSceneFrameSnapshot& snapshot = output.scene;
	snapshot.sceneReady = true;
	snapshot.mainCameraHiZCullSettings = m_MainCameraHiZCullSettings;
	snapshot.gi.settings = m_GISettings;
	snapshot.gi.rebuildProbeResources = m_GIProbeResourcesDirty;
	snapshot.gi.updateParameters = m_GIParametersDirty;
	snapshot.gi.prepared = true;
	m_GIProbeResourcesDirty = false;
	m_GIParametersDirty = false;
	{
		VansPostProcessProfile& profile = m_MaterialManager.m_PostProcessProfile;
		snapshot.postProcess.params = profile.ToGPUParams();
		snapshot.postProcess.params.m_DebugPassthrough =
			IsGIProbeOnlyDeferredOutputEnabled(snapshot.gi.settings) ? 1.0f : 0.0f;
		snapshot.postProcess.exposure = profile.ToExposureAdaptParams(
			static_cast<float>(context.timing.renderDeltaSeconds));
		snapshot.postProcess.bloom = profile.ToBloomParams();
		snapshot.postProcess.bloomShape = profile.ToBloomShapeParams();
		snapshot.postProcess.depthOfField = profile.ToDepthOfFieldParams(
			view.viewportWidth, view.viewportHeight);
		snapshot.postProcess.enableAutoExposure = profile.m_EnableAutoExposure;
		snapshot.postProcess.enableDepthOfField = profile.m_EnableDOF;
		snapshot.postProcess.enableBloom = profile.m_EnableBloom;
		snapshot.postProcess.blurTransmissionBackground =
			profile.m_DOFBlurTransmissionBackground;
		snapshot.postProcess.staticParametersDirty = profile.m_IsDirty;
		snapshot.postProcess.prepared = true;
		profile.m_IsDirty = false;
	}
	std::vector<VansRenderNode*> activeProxyNodes;

    {
        VANS_PROFILE_SCOPE("Animation::UpdateAll", Vans::ProfileCategory::Animation);
        EvaluateAnimations(deltaTime);
		snapshot.animations.reserve(m_AnimationNodes.size());
		for (std::uint32_t animationIndex = 0;
			animationIndex < static_cast<std::uint32_t>(m_AnimationNodes.size());
			++animationIndex)
		{
			VansAnimationNode* animation = m_AnimationNodes[animationIndex];
			const bool editorPreviewDriven = animation &&
				m_LoadMode == VansSceneLoadMode::Editor &&
				m_EditorPreviewDrivenAnimationNodes.find(animation) !=
					m_EditorPreviewDrivenAnimationNodes.end();
			if (!animation || (!animation->IsEnabled() && !editorPreviewDriven))
				continue;
			VansRenderAnimationFrameData frameData;
			frameData.animationNodeIndex = animationIndex;
			frameData.boneMatrices = animation->GetBoneSSBO();
			snapshot.animations.emplace_back(std::move(frameData));
		}
    }
    {
        VANS_PROFILE_SCOPE("Transform::ResolveParentChild", Vans::ProfileCategory::RenderPrepare);
        m_TransformGraph.Resolve();
    }
	{
		VANS_PROFILE_SCOPE("RenderWorld::ExtractProxyMutations", Vans::ProfileCategory::RenderPrepare);
		const std::vector<VansRenderNode*> transformNodes = CollectSSBOManagedRenderNodes();
		activeProxyNodes.reserve(transformNodes.size());
		std::unordered_set<const VansRenderNode*> currentProxyNodes;
		currentProxyNodes.reserve(transformNodes.size());
		for (VansRenderNode* node : transformNodes)
		{
			if (node == nullptr || !currentProxyNodes.insert(node).second)
			{
				continue;
			}

			if (EnsureMainRenderProxyBinding(node, output.mutationsBeforeFrame) != nullptr)
				activeProxyNodes.push_back(node);
		}
		for (auto bindingIt = m_MainRenderProxyBindings.begin();
			bindingIt != m_MainRenderProxyBindings.end();)
		{
			if (currentProxyNodes.find(bindingIt->first) != currentProxyNodes.end())
			{
				++bindingIt;
				continue;
			}
			output.mutationsBeforeFrame.AddDestroy(bindingIt->second.handle);
			if (!m_RenderProxyHandleAllocator.Release(bindingIt->second.handle))
				VANS_LOG_ERROR("[VansScene] Failed to release stale render proxy handle.");
			bindingIt = m_MainRenderProxyBindings.erase(bindingIt);
		}
	}
	{
		VANS_PROFILE_SCOPE("Visibility::ExtractMainCameraCandidates", Vans::ProfileCategory::RenderPrepare);
		snapshot.mainCameraCullInputs.reserve(
			m_OpaqueRenderNodes.size() +
			m_HairRenderNodes.size() +
			m_ForwardOpaquePreAtmosphereRenderNodes.size() +
			m_TransParentRenderNodes.size() +
			m_DecalRenderNodes.size());
		const auto appendCullInput = [&](VansRenderNode* node, VansMainCameraCullClass cullClass)
		{
			if (node == nullptr || !node->IsEnabled())
				return;
			const VansRenderProxyHandle proxy = FindMainRenderProxyHandle(node);
			if (!proxy.IsValid())
				return;

			VansRenderMainCameraCullInput input;
			input.proxy = proxy;
			input.nodeName = node->m_NodeName;
			input.cullClass = cullClass;
			input.hasBounds = TryGetStaticNodeWorldBounds(node, input.bounds);
			snapshot.mainCameraCullInputs.emplace_back(std::move(input));
		};
		for (VansRenderNode* node : m_OpaqueRenderNodes)
			appendCullInput(node, VansMainCameraCullClass::Opaque);
		for (VansRenderNode* node : m_HairRenderNodes)
			appendCullInput(node, VansMainCameraCullClass::Hair);
		for (VansRenderNode* node : m_ForwardOpaquePreAtmosphereRenderNodes)
			appendCullInput(node, VansMainCameraCullClass::ForwardOpaquePreAtmosphere);
		for (VansRenderNode* node : m_TransParentRenderNodes)
			appendCullInput(node, VansMainCameraCullClass::Transparent);
		for (VansRenderNode* node : m_DecalRenderNodes)
			appendCullInput(node, VansMainCameraCullClass::Decal);
	}
    {
        VANS_PROFILE_SCOPE("Light::SyncTransforms", Vans::ProfileCategory::RenderPrepare);
        SyncLightTransforms();
    }
    {
        VANS_PROFILE_SCOPE("Light::UpdateShadowMatrices", Vans::ProfileCategory::RenderPrepare);
        VansCascadeCameraData shadowCamera = {};
        shadowCamera.position = view.position;
        shadowCamera.forward = view.forward;
        shadowCamera.up = view.up;
        shadowCamera.verticalFovRadians = view.fieldOfViewRadians;
        shadowCamera.aspectRatio = view.aspectRatio;
        shadowCamera.nearPlane = view.nearClip;
        shadowCamera.farPlane = view.farClip;
        shadowCamera.viewportWidth = view.viewportWidth;
        shadowCamera.viewportHeight = view.viewportHeight;
        m_LightManager.UpdateLightShadowMatrixData(shadowCamera);
		snapshot.punctualShadow =
			m_LightManager.BuildPunctualShadowFrameInput(shadowCamera);

		auto appendShadowCaster = [&](VansRenderNode* node)
		{
			if (node == nullptr || !node->IsEnabled())
				return;
			auto* common = static_cast<VansCommonRenderNode*>(node);
			if (!common->m_SupportShadow)
				return;
			const VansRenderProxyHandle proxy = FindMainRenderProxyHandle(node);
			if (!proxy.IsValid())
				return;

			VansRenderPunctualShadowCasterInput caster;
			caster.proxy = proxy;
			caster.shadowCasterMask = common->m_ShadowCasterMask;
			caster.dynamic = node->m_AnimationEnabled || node->m_HasSkeletonBone;
			node->UpdateWorldBoundsFromTransform();
			caster.hasBounds = node->HasWorldBounds();
			if (caster.hasBounds)
				caster.bounds = node->GetWorldBounds();
			snapshot.punctualShadow.casters.emplace_back(std::move(caster));
		};
		snapshot.punctualShadow.casters.reserve(
			m_OpaqueRenderNodes.size() + m_HairRenderNodes.size());
		for (VansRenderNode* node : m_OpaqueRenderNodes)
			appendShadowCaster(node);
		for (VansRenderNode* node : m_HairRenderNodes)
			appendShadowCaster(node);
    }
    {
        VANS_PROFILE_SCOPE("Light::BuildFrameData", Vans::ProfileCategory::RenderPrepare);
		snapshot.light = m_LightManager.BuildRenderLightFrameData();
		m_ReflectionProbeSystem.SetRuntimeSkyCubeScales(
			m_LightManager.GetSkyDiffuseScale(),
			m_LightManager.GetSkySpecularScale());
    }
    {
        VANS_PROFILE_SCOPE("Cloth::Simulate", Vans::ProfileCategory::Physics);
        UpdateClothSimulation(0.03f);
		snapshot.cloth.reserve(m_ClothNodes.size());
		for (std::uint32_t clothIndex = 0;
			clothIndex < static_cast<std::uint32_t>(m_ClothNodes.size());
			++clothIndex)
		{
			VansEngine::VansClothNode* clothNode = m_ClothNodes[clothIndex];
			if (!clothNode || !clothNode->IsEnabled())
				continue;
			clothNode->WriteSimResults();
			VansRenderClothFrameData frameData;
			frameData.clothNodeIndex = clothIndex;
			frameData.simulatedVertices = clothNode->GetSimulatedVertexData();
			snapshot.cloth.emplace_back(std::move(frameData));
		}
    }
    {
        VANS_PROFILE_SCOPE("Video::TickAll", Vans::ProfileCategory::Video);
        m_VideoManager.TickAll(context.timing.deltaSeconds);
		if (m_RuntimeWorld)
		{
			auto* rectLightStorage = static_cast<
				Vans::VansComponentStorage<Vans::VansRuntimeLightComponent>*>(
					m_RuntimeWorld->FindStorage(Vans::VansRuntimeComponentType_RectLight));
			auto* videoStorage = static_cast<
				Vans::VansComponentStorage<Vans::VansRuntimeVideoComponent>*>(
					m_RuntimeWorld->FindStorage(Vans::VansRuntimeComponentType_Video));
			if (rectLightStorage && videoStorage)
			{
				for (Vans::VansEntityHandle entity :
					m_RuntimeWorld->Entities().CollectAliveEntities())
				{
					Vans::VansRuntimeLightComponent* rectLight = nullptr;
					Vans::VansRuntimeVideoComponent* video = nullptr;
					for (Vans::VansComponentHandle component :
						m_RuntimeWorld->CollectComponentsOwnedBy(entity))
					{
						if (component.typeId == Vans::VansRuntimeComponentType_RectLight)
							rectLight = rectLightStorage->Get(component);
						else if (component.typeId == Vans::VansRuntimeComponentType_Video)
							video = videoStorage->Get(component);
					}
					if (!rectLight || !video || !video->videoTexture ||
						rectLight->lightIndex < 0)
					{
						continue;
					}
					const std::uint32_t videoIndex =
						m_VideoManager.FindRuntimeIndex(video->videoTexture);
					if (videoIndex == VansVideoManager::InvalidRuntimeIndex)
						continue;
					snapshot.rectLightVideos.push_back({
						videoIndex,
						static_cast<std::int32_t>(rectLight->lightIndex) });
				}
			}
		}
    }
    {
        VANS_PROFILE_SCOPE("Audio::SyncSourcePositions", Vans::ProfileCategory::Audio);
        SyncAudioSourcePositions(deltaTime);
    }
    {
        VANS_PROFILE_SCOPE("Audio::ReverbEnvironment", Vans::ProfileCategory::Audio);
        UpdateAudioReverbEnvironment(deltaTime);
    }
    {
        VANS_PROFILE_SCOPE("Audio::TickAll", Vans::ProfileCategory::Audio);
        m_AudioManager.TickAll(
            context.timing.deltaSeconds,
            view.position.x, view.position.y, view.position.z,
            view.forward.x, view.forward.y, view.forward.z,
            view.up.x, view.up.y, view.up.z,
            m_AudioListenerVelocityX,
            m_AudioListenerVelocityY,
            m_AudioListenerVelocityZ);
    }

    if (!m_ParticleRenderNodes.empty())
    {
        {
            VANS_PROFILE_SCOPE("Particle::PrepareLocalToWorld", Vans::ProfileCategory::Particles);
            if (m_RuntimeWorld)
            {
                auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeParticleComponent>*>(
                    m_RuntimeWorld->FindStorage(Vans::VansRuntimeComponentType_Particle));
                if (storage)
                {
                    for (Vans::VansEntityHandle entity : m_RuntimeWorld->Entities().CollectAliveEntities())
                    {
                        const std::uint32_t transformId = ResolveRuntimeEntityTransformId(*m_RuntimeWorld, entity);
                        if (transformId >= VansTransformStore::GlobalTransforms.size())
                            continue;

                        for (Vans::VansComponentHandle component : m_RuntimeWorld->CollectComponentsOwnedBy(entity))
                        {
                            if (component.typeId != Vans::VansRuntimeComponentType_Particle)
                                continue;
                            auto* particle = storage->Get(component);
                            if (!particle || !particle->runtime)
                                continue;

                            if (particle->hasWorldPositionOverride)
                            {
                                glm::mat4x4 overrideMatrix(1.f);
                                overrideMatrix[3] = glm::vec4(
                                    particle->worldPositionOverrideX,
                                    particle->worldPositionOverrideY,
                                    particle->worldPositionOverrideZ,
                                    1.f);
                                particle->runtime->m_LocalToWorld = overrideMatrix;
                            }
                            else
                            {
                                auto& transform = VansTransformStore::GetTransform(transformId);
                                particle->runtime->m_LocalToWorld = transform.GetModelMatrix();
                            }
                        }
                    }
                }
            }
        }
        {
            VANS_PROFILE_SCOPE("Particle::SignalUpdate", Vans::ProfileCategory::Particles);
            VansParticleManager::Instance().TickMainThread(deltaTime);
        }
        {
            VANS_PROFILE_WAIT("Particle::WaitForUpdate");
            VansParticleManager::Instance().WaitForUpdateAndSwap();
        }
		if (m_RuntimeWorld)
		{
			auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeParticleComponent>*>(
				m_RuntimeWorld->FindStorage(Vans::VansRuntimeComponentType_Particle));
			if (storage)
			{
				for (Vans::VansEntityHandle entity : m_RuntimeWorld->Entities().CollectAliveEntities())
				{
					for (Vans::VansComponentHandle component : m_RuntimeWorld->CollectComponentsOwnedBy(entity))
					{
						if (component.typeId != Vans::VansRuntimeComponentType_Particle)
							continue;
						auto* particle = storage->Get(component);
						if (!particle || !particle->runtime || !particle->renderNode)
							continue;

						particle->playTime = particle->runtime->m_PlayTime;
						particle->isPlaying = particle->runtime->m_IsPlaying;
						const auto renderNodeIt = std::find(
							m_ParticleRenderNodes.begin(),
							m_ParticleRenderNodes.end(),
							particle->renderNode);
						if (renderNodeIt == m_ParticleRenderNodes.end())
							continue;

						VansRenderParticleFrameData frameData;
						frameData.particleRenderNodeIndex = static_cast<std::uint32_t>(
							std::distance(m_ParticleRenderNodes.begin(), renderNodeIt));
						frameData.instances = particle->runtime->GetRenderBuffer();
						snapshot.particles.emplace_back(std::move(frameData));
					}
				}
			}
		}
    }

	{
		VANS_PROFILE_SCOPE("Material::CaptureFrameData", Vans::ProfileCategory::RenderPrepare);
		std::vector<VansMaterial*> activeMaterials;
		activeMaterials.reserve(activeProxyNodes.size());
		for (VansRenderNode* node : activeProxyNodes)
		{
			if (node && node->m_Material)
				activeMaterials.push_back(node->m_Material);
		}
		snapshot.materials =
			m_MaterialManager.CaptureRenderMaterialFrameData(activeMaterials);
	}

	snapshot.transforms.reserve(activeProxyNodes.size());
	for (VansRenderNode* node : activeProxyNodes)
	{
		auto bindingIt = m_MainRenderProxyBindings.find(node);
		if (bindingIt == m_MainRenderProxyBindings.end() ||
			!bindingIt->second.staticData.enabled ||
			bindingIt->second.staticData.transformSlot == VANS_INVALID_RENDER_TRANSFORM_SLOT)
			continue;

		node->PrepareModelDataForRenderFrame();
		const ModelDataStruct& model = node->GetPreparedModelData();
		VansRenderTransformFrameData transform;
		transform.proxy = bindingIt->second.handle;
		transform.modelMatrix = model.ModelMatrix;
		transform.normalMatrix = model.NormalMatrix;
		transform.position = model.Postion;
		transform.scale = model.Scale;
		transform.previousModelMatrix = model.PrevModelMatrix;
		snapshot.transforms.emplace_back(std::move(transform));
	}
	snapshot.features.hasWater = HasWaterNodes();
	snapshot.features.hasDecal = HasDecalNodes();
	snapshot.features.hasForwardOpaquePreAtmosphere =
		HasForwardOpaquePreAtmosphereNodes();
	VansTransformStore::TransformIDToTransformDirty.clear();
    return output;
}

void VansGraphics::VansScene::PrepareRenderBackendData(
    const VansRenderViewSnapshot& view,
    const VansRenderSceneFrameSnapshot& sceneSnapshot,
	const VansRenderWorld& renderWorld)
{
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::RenderThreadConsume);
    VANS_PROFILE_SCOPE("Scene::PrepareRenderBackendData", Vans::ProfileCategory::RenderPrepare);
	if (!sceneSnapshot.sceneReady)
		return;

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkDevice nativeDevice = vkDevice ? vkDevice->GetLogicDevice() : VK_NULL_HANDLE;

    {
        VANS_PROFILE_SCOPE("Cloth::WriteResultsToStaging", Vans::ProfileCategory::Physics);
        WriteClothResultsToStagingBuffers(sceneSnapshot);
    }

	if (!sceneSnapshot.particles.empty() && nativeDevice != VK_NULL_HANDLE)
    {
        VANS_PROFILE_SCOPE("Particle::UploadInstanceBuffers", Vans::ProfileCategory::Particles);
		for (const VansRenderParticleFrameData& particle : sceneSnapshot.particles)
        {
			if (particle.particleRenderNodeIndex >= m_ParticleRenderNodes.size())
				continue;
			VansParticleRenderNode* renderNode =
				m_ParticleRenderNodes[particle.particleRenderNodeIndex];
			if (renderNode)
				renderNode->UpdateInstanceBuffer(
					nativeDevice, particle.instances, view.view);
        }
    }

    {
        VANS_PROFILE_SCOPE("Animation::UploadRenderData", Vans::ProfileCategory::Animation);
        UploadAnimationRenderData(sceneSnapshot);
    }
	{
        VANS_PROFILE_SCOPE("RenderData::UploadTransformSnapshot", Vans::ProfileCategory::RenderPrepare);
		for (const VansRenderTransformFrameData& transform : sceneSnapshot.transforms)
		{
			const VansRenderProxyStaticData* proxy = renderWorld.Resolve(transform.proxy);
			if (proxy == nullptr || !proxy->enabled)
			{
				VANS_LOG_ERROR("[VansScene] Render transform references a stale or disabled proxy handle.");
				continue;
			}
			ModelDataStruct model;
			model.ModelMatrix = transform.modelMatrix;
			model.NormalMatrix = transform.normalMatrix;
			model.Postion = transform.position;
			model.Scale = transform.scale;
			model.PrevModelMatrix = transform.previousModelMatrix;
			UpdateMappedInstanceTransformData(model, proxy->transformSlot);
		}
    }
    {
        VANS_PROFILE_SCOPE("RenderData::UpdateNodesBeforeRecord", Vans::ProfileCategory::RenderPrepare);
		if (!m_MaterialManager.UploadRenderMaterialFrameData(sceneSnapshot.materials))
			VANS_LOG_ERROR("[VansScene] Render-thread material frame upload was rejected.");
		UpdateRenderNodesDataBeforeRecord(view, sceneSnapshot);
    }
}

void VansGraphics::VansScene::RecordVideoUploads(
	VansVKCommandBuffer& cmd,
	const VansRenderSceneFrameSnapshot& sceneSnapshot)
{
    VANS_PROFILE_SCOPE("Video::Upload.RecordCommands", Vans::ProfileCategory::Video);
    m_VideoManager.RecordPendingUploads(cmd);
    m_MaterialManager.RecordPendingSkinProfileLUTUploads(cmd);

    // 面光源视频发光：写入 emissive 贴图数组层，合并进当前帧命令缓冲。
    {
        VANS_PROFILE_SCOPE("RectLightVideo::RecordCopyFrames", Vans::ProfileCategory::Video);
        VansTexture* emissiveArray = m_MaterialManager.GetRuntimeRenderTexture(
            VansMaterialManager::RT_RECT_LIGHT_EMISSIVE);
        if (!emissiveArray)
            return;

		for (const VansRenderRectLightVideoFrameData& binding :
			sceneSnapshot.rectLightVideos)
        {
			VansVideoTexture* vid =
				m_VideoManager.GetByRuntimeIndex(binding.videoRuntimeIndex);
            if (!vid || !vid->IsReady()) continue;

            vid->RecordNewFrameToArrayLayer(
				emissiveArray, cmd, binding.rectLightLayer);
        }
    }
}

// ============================================================
// SyncLightTransforms — 将 ScriptObject 的 Transform 同步到灯光数据
// 每帧在 UpdateLightShadowMatrixData 前调用。
// 约定：Transform 旋转 ZYX 顺序。方向光保持原有约定；SpotLight 的 GPU
//        m_Direction 存储“受光点指向灯”的方向，实际光线沿对象本地 -Z 传播。
// ============================================================
void VansGraphics::VansScene::SyncLightTransforms()
{
    if (!m_RuntimeWorld)
        return;

    for (Vans::VansEntityHandle entity : m_RuntimeWorld->Entities().CollectAliveEntities())
    {
        const std::uint32_t transformId = ResolveRuntimeEntityTransformId(*m_RuntimeWorld, entity);
        if (transformId >= VansTransformStore::GlobalTransforms.size())
            continue;

        const auto& t = VansTransformStore::GetTransform(transformId);
        const std::vector<Vans::VansComponentHandle> components =
            m_RuntimeWorld->CollectComponentsOwnedBy(entity);
        for (Vans::VansComponentHandle component : components)
        {
            const auto* runtimeLight = GetRuntimeComponentPayload<Vans::VansRuntimeLightComponent>(
                *m_RuntimeWorld,
                component,
                component.typeId);
            if (!runtimeLight || !runtimeLight->lightManager || runtimeLight->lightIndex < 0)
                continue;

            if (component.typeId == Vans::VansRuntimeComponentType_DirectionalLight)
            {
                auto& lights = runtimeLight->lightManager->GetDirectionLights();
                if (runtimeLight->lightIndex >= (int)lights.size())
                    continue;
                glm::mat4 rotMat = glm::mat4(1.0f);
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
                glm::vec3 forward = glm::normalize(glm::vec3(rotMat[2]));
                // m_Direction = 朝向光源方向（与光线传播方向相反）
                lights[runtimeLight->lightIndex].m_Direction = -forward;
                continue;
            }

            if (component.typeId == Vans::VansRuntimeComponentType_PointLight)
            {
                auto& lights = runtimeLight->lightManager->GetPointLights();
                if (runtimeLight->lightIndex < (int)lights.size())
                    lights[runtimeLight->lightIndex].m_Position = t.m_Position;
                continue;
            }

            if (component.typeId == Vans::VansRuntimeComponentType_SpotLight)
            {
                auto& lights = runtimeLight->lightManager->GetSpotLight();
                if (runtimeLight->lightIndex >= (int)lights.size())
                    continue;
                lights[runtimeLight->lightIndex].m_Position = t.m_Position;

                glm::mat4 rotMat = glm::mat4(1.0f);
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
                // 聚光灯 shader 使用“受光点指向灯”的方向做比较。
                // Unity 中的聚光灯沿本地 -Z 发光，因此本地 +Z 是 GPU 侧用于
                // 光照、阴影、体积雾和 IES 的锥体轴方向。
                glm::vec3 towardLight = glm::normalize(glm::vec3(rotMat[2]));
                lights[runtimeLight->lightIndex].m_Direction = towardLight;
                continue;
            }

            if (component.typeId == Vans::VansRuntimeComponentType_RectLight)
            {
                auto& lights = runtimeLight->lightManager->GetRectLights();
                if (runtimeLight->lightIndex >= (int)lights.size())
                    continue;
                glm::mat4 rotMat = glm::mat4(1.0f);
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
                glm::vec3 right   = glm::normalize(glm::vec3(rotMat[0]));   // local +X
                glm::vec3 up      = glm::normalize(glm::vec3(rotMat[1]));   // local +Y
                glm::vec3 forward = glm::normalize(glm::vec3(rotMat[2]));   // local +Z
                lights[runtimeLight->lightIndex].m_Position = t.m_Position;
                lights[runtimeLight->lightIndex].m_Right    = right;
                lights[runtimeLight->lightIndex].m_Up       = up;
                lights[runtimeLight->lightIndex].m_Normal   = forward;
            }
        }
    }
}

// ============================================================
// SyncAudioSourcePositions — 每帧将 spatial 音频节点的 OpenAL source
// 位置同步到对应 ScriptObject 的世界坐标。需要在 ResolveParentChildTransforms
// 之后、TickAll 之前调用，确保使用最新的世界坐标。
// ============================================================
void VansGraphics::VansScene::SyncAudioSourcePositions(float deltaTime)
{
    if (!m_Camera || !m_RuntimeWorld)
        return;

    auto* audioStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeAudioComponent>*>(
        m_RuntimeWorld->FindStorage(Vans::VansRuntimeComponentType_Audio));
    if (!audioStorage)
        return;

    struct RuntimeAudioEntry
    {
        Vans::VansEntityHandle entity;
        Vans::VansComponentHandle component;
        Vans::VansRuntimeAudioComponent* audio = nullptr;
        std::uint32_t transformId = UINT32_MAX;
    };

    std::vector<RuntimeAudioEntry> audioEntries;
    for (Vans::VansEntityHandle entity : m_RuntimeWorld->Entities().CollectAliveEntities())
    {
        const std::uint32_t transformId = ResolveRuntimeEntityTransformId(*m_RuntimeWorld, entity);
        for (Vans::VansComponentHandle component : m_RuntimeWorld->CollectComponentsOwnedBy(entity))
        {
            if (component.typeId != Vans::VansRuntimeComponentType_Audio)
                continue;
            auto* audio = audioStorage->Get(component);
            if (!audio || !audio->sourceBinding || !audio->sourceBinding->IsBound())
                continue;
            audioEntries.push_back(RuntimeAudioEntry{ entity, component, audio, transformId });
        }
    }

    const glm::vec4 camPos = m_Camera->GetPosition();
    const glm::vec3 listener(camPos.x, camPos.y, camPos.z);
    const bool canComputeVelocity = deltaTime > 0.0001f;
    const glm::vec3 listenerVelocity =
        canComputeVelocity && m_AudioHasLastListenerPosition
        ? (listener - glm::vec3(
            m_AudioLastListenerX,
            m_AudioLastListenerY,
            m_AudioLastListenerZ)) / deltaTime
        : glm::vec3(0.0f);
    m_AudioHasLastListenerPosition = true;
    m_AudioLastListenerX = listener.x;
    m_AudioLastListenerY = listener.y;
    m_AudioLastListenerZ = listener.z;
    bool anyDopplerEnabled = false;
    std::vector<AudioOcclusionQueryTarget> occlusionTargets;
    int queryBudget = 0;
    std::vector<VansEngine::AudioVoiceCandidate> voiceCandidates;

    for (const RuntimeAudioEntry& entry : audioEntries)
    {
        auto* source = entry.audio->sourceBinding;

        glm::vec3 sourcePosition = listener;
        if (entry.transformId != UINT32_MAX)
        {
            const auto& t = VansTransformStore::GetTransform(entry.transformId);
            sourcePosition = t.m_Position;
        }
        const float dx = sourcePosition.x - listener.x;
        const float dy = sourcePosition.y - listener.y;
        const float dz = sourcePosition.z - listener.z;

        VansEngine::AudioVoiceCandidate candidate;
        candidate.stableIndex = voiceCandidates.size();
        candidate.bound = source->IsBound();
        candidate.objectActive = m_RuntimeWorld->Entities().IsHierarchyActive(entry.entity);
        candidate.componentEnabled = m_RuntimeWorld->IsComponentSelfEnabled(entry.component);
        candidate.playing = source->IsPlaying();
        candidate.spatial = source->GetSpatial();
        candidate.listenerDistance = std::sqrt(dx * dx + dy * dy + dz * dz);
        candidate.maxDistance = source->GetMaxDistance();
        candidate.effectiveGain =
            source->GetVolume() *
            m_AudioManager.GetEffectiveBusGain(source->GetBusName());

        voiceCandidates.push_back(candidate);
    }

    const VansEngine::AudioVoiceSelection voiceSelection =
        VansEngine::SelectAudioVoices(voiceCandidates, m_AudioVoiceBudgetSettings);
    std::vector<std::string> activeAudioBuses;
    m_AudioManager.BeginVoiceLeaseFrame();
    for (std::size_t i = 0; i < audioEntries.size(); ++i)
    {
        auto* source = audioEntries[i].audio->sourceBinding;
        const bool activeVoice = i < voiceSelection.active.size() && voiceSelection.active[i];
        const VansEngine::AudioVoiceCandidate& candidate = voiceCandidates[i];
        const bool virtualized = candidate.playing &&
            (!candidate.objectActive || !candidate.componentEnabled || !activeVoice);
        const bool hardwareActiveBefore = source->IsHardwareVoiceActive();
        source->SetVirtualizationGain(virtualized ? 0.0f : 1.0f);
        m_AudioManager.RecordVoiceLeaseTransition(
            hardwareActiveBefore,
            source->IsHardwareVoiceActive());
        if (candidate.playing && candidate.objectActive && candidate.componentEnabled && activeVoice)
            activeAudioBuses.push_back(source->GetBusName());
    }
    m_AudioManager.UpdateDucking(activeAudioBuses);

    for (RuntimeAudioEntry& entry : audioEntries)
    {
        auto* audio = entry.audio;
        auto* source = audio->sourceBinding;
        if (!source || !source->IsBound())
            continue;

        source->SetBusGain(
            m_AudioManager.GetEffectiveBusGain(source->GetBusName()));
        const VansEngine::AudioBusState masterBusState = m_AudioManager.GetBusState("Master");
        const VansEngine::AudioBusState sourceBusState =
            m_AudioManager.GetBusState(source->GetBusName());
        source->SetBusLowpassHighFrequencyGain(
            masterBusState.lowpassHighFrequencyGain *
                (source->GetBusName() == "Master"
                    ? 1.0f
                    : sourceBusState.lowpassHighFrequencyGain));
        source->Tick();
        if (!source->GetSpatial())
        {
            source->SetVelocity(0.0f, 0.0f, 0.0f);
            audio->hasLastAudioPosition = false;
            continue;
        }
        if (entry.transformId == UINT32_MAX)
            continue;

        const auto& t = VansTransformStore::GetTransform(entry.transformId);
        source->SetPosition(t.m_Position.x, t.m_Position.y, t.m_Position.z);
        source->UpdateDistanceGain(camPos.x, camPos.y, camPos.z);

        const glm::vec3 sourcePosition = t.m_Position;
        glm::vec3 sourceVelocity(0.0f);
        if (audio->dopplerEnabled && canComputeVelocity && audio->hasLastAudioPosition)
        {
            sourceVelocity = (sourcePosition - glm::vec3(
                audio->lastAudioPositionX,
                audio->lastAudioPositionY,
                audio->lastAudioPositionZ)) / deltaTime;
            anyDopplerEnabled = true;
        }
        audio->hasLastAudioPosition = true;
        audio->lastAudioPositionX = sourcePosition.x;
        audio->lastAudioPositionY = sourcePosition.y;
        audio->lastAudioPositionZ = sourcePosition.z;
        source->SetVelocity(sourceVelocity.x, sourceVelocity.y, sourceVelocity.z);

        audio->coneSettings.Normalize();
        if (audio->coneSettings.enabled)
        {
            glm::mat4 rotMat = glm::mat4(1.0f);
            rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec3 forward = glm::normalize(glm::vec3(rotMat[2]));
            source->SetDirection(forward.x, forward.y, forward.z);
        }
        source->SetCone(audio->coneSettings);

        audio->occlusionSettings.Normalize();
        if (!audio->occlusionSettings.enabled)
        {
            audio->occlusionState = VansEngine::AudioOcclusionState{};
            source->SetOcclusion(1.0f, 1.0f);
            continue;
        }

        const float distance = glm::length(sourcePosition - listener);
        if (distance > audio->occlusionSettings.maxQueryDistance || distance <= 0.001f)
        {
            audio->occlusionState.lastBlocked = false;
            audio->occlusionState.queryTimer = 0.0f;
            audio->occlusionState = VansEngine::UpdateAudioOcclusionState(
                audio->occlusionState,
                audio->occlusionSettings,
                false,
                deltaTime);
            source->SetOcclusion(
                audio->occlusionState.gain,
                audio->occlusionState.highFrequencyGain);
            continue;
        }

        audio->occlusionState.queryTimer += std::max(deltaTime, 0.0f);
        queryBudget = std::max(queryBudget, audio->occlusionSettings.maxQueriesPerFrame);
        occlusionTargets.push_back(AudioOcclusionQueryTarget{
            entry.component,
            sourcePosition,
            distance,
            entry.transformId });
    }

    if (!occlusionTargets.empty() && queryBudget > 0)
    {
        auto& physics = VansEngine::VansPhysicsSystem::GetInstance();
        physx::PxScene* scene = physics.GetScene();
        const std::size_t targetCount = occlusionTargets.size();
        m_AudioOcclusionQueryCursor %= targetCount;
        std::size_t nextCursor = m_AudioOcclusionQueryCursor;

        if (scene)
        {
            std::lock_guard<std::mutex> lock(physics.GetSimulationMutex());
            physx::PxSceneReadLock scopedReadLock(*scene);
            for (std::size_t visited = 0; visited < targetCount && queryBudget > 0; ++visited)
            {
                const std::size_t index = (m_AudioOcclusionQueryCursor + visited) % targetCount;
                AudioOcclusionQueryTarget& target = occlusionTargets[index];
                auto* audio = audioStorage->Get(target.component);
                if (!audio)
                    continue;

                auto& state = audio->occlusionState;
                const auto& settings = audio->occlusionSettings;
                if (state.queryTimer < settings.queryIntervalSeconds)
                    continue;

                state.queryTimer = 0.0f;
                --queryBudget;
                nextCursor = (index + 1) % targetCount;

                const glm::vec3 directionVector = target.sourcePosition - listener;
                const float directionLength = glm::length(directionVector);
                if (directionLength <= 0.001f)
                {
                    state.lastBlocked = false;
                    continue;
                }

                const glm::vec3 direction = directionVector / directionLength;
                physx::PxQueryFilterData filterData;
                filterData.flags = physx::PxQueryFlag::eSTATIC |
                    physx::PxQueryFlag::eDYNAMIC |
                    physx::PxQueryFlag::ePREFILTER;
                AudioOcclusionQueryFilter filter(target.ignoredTransformID);
                physx::PxRaycastBuffer hit;
                state.lastBlocked = scene->raycast(
                    physx::PxVec3(listener.x, listener.y, listener.z),
                    physx::PxVec3(direction.x, direction.y, direction.z),
                    std::min(directionLength, settings.maxQueryDistance),
                    hit,
                    physx::PxHitFlag::eDEFAULT,
                    filterData,
                    &filter) && hit.hasBlock;
            }
        }

        m_AudioOcclusionQueryCursor = nextCursor;

        for (AudioOcclusionQueryTarget& target : occlusionTargets)
        {
            auto* audio = audioStorage->Get(target.component);
            if (!audio || !audio->sourceBinding)
                continue;
            auto& state = audio->occlusionState;
            state = VansEngine::UpdateAudioOcclusionState(
                state,
                audio->occlusionSettings,
                state.lastBlocked,
                deltaTime);
            audio->sourceBinding->SetOcclusion(state.gain, state.highFrequencyGain);
        }
    }

    if (anyDopplerEnabled)
    {
        m_AudioListenerVelocityX = listenerVelocity.x;
        m_AudioListenerVelocityY = listenerVelocity.y;
        m_AudioListenerVelocityZ = listenerVelocity.z;
    }
    else
    {
        m_AudioListenerVelocityX = 0.0f;
        m_AudioListenerVelocityY = 0.0f;
        m_AudioListenerVelocityZ = 0.0f;
    }
}

void VansGraphics::VansScene::ReleaseAudioSourceBindings()
{
    if (!m_RuntimeWorld)
        return;

    auto* audioStorage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeAudioComponent>*>(
        m_RuntimeWorld->FindStorage(Vans::VansRuntimeComponentType_Audio));
    if (!audioStorage)
        return;

    for (Vans::VansEntityHandle entity : m_RuntimeWorld->Entities().CollectAliveEntities())
    {
        for (Vans::VansComponentHandle component : m_RuntimeWorld->CollectComponentsOwnedBy(entity))
        {
            if (component.typeId != Vans::VansRuntimeComponentType_Audio)
                continue;
            if (auto* audio = audioStorage->Get(component); audio && audio->sourceBinding)
                audio->sourceBinding->Clear();
        }
    }
}

void VansGraphics::VansScene::UpdateAudioReverbEnvironment(float deltaTime)
{
    auto& audioSystem = VansEngine::VansAudioSystem::GetInstance();
    if (!m_Camera || !audioSystem.IsInitialized() || !m_RuntimeWorld)
        return;

    const glm::vec4 listener = m_Camera->GetPosition();
    bool hasZone = false;
    std::vector<VansEngine::AudioReverbZoneEvaluation> evaluations;

    for (Vans::VansEntityHandle entity : m_RuntimeWorld->Entities().CollectAliveEntities())
    {
        const std::uint32_t transformId = ResolveRuntimeEntityTransformId(*m_RuntimeWorld, entity);
        if (transformId >= VansTransformStore::GlobalTransforms.size())
            continue;

        const auto& transform = VansTransformStore::GetTransform(transformId);
        const std::vector<Vans::VansComponentHandle> components =
            m_RuntimeWorld->CollectComponentsOwnedBy(entity);
        for (Vans::VansComponentHandle component : components)
        {
            if (component.typeId != Vans::VansRuntimeComponentType_AudioReverbZone &&
                component.typeId != Vans::VansRuntimeComponentType_AudioVolume)
            {
                continue;
            }
            if (!m_RuntimeWorld->IsComponentEffectivelyEnabled(component))
                continue;

            const auto* zone = GetRuntimeComponentPayload<Vans::VansRuntimeAudioReverbZoneComponent>(
                *m_RuntimeWorld,
                component,
                component.typeId);
            if (!zone)
                continue;

            hasZone = true;
            VansEngine::AudioReverbZoneState zoneState;
            zoneState.shape = VansEngine::AudioReverbZoneShapeFromString(zone->shape);
            zoneState.centerX = transform.m_Position.x;
            zoneState.centerY = transform.m_Position.y;
            zoneState.centerZ = transform.m_Position.z;
            glm::mat4 zoneRotation = glm::mat4(1.0f);
            zoneRotation = glm::rotate(zoneRotation, glm::radians(transform.m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
            zoneRotation = glm::rotate(zoneRotation, glm::radians(transform.m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
            zoneRotation = glm::rotate(zoneRotation, glm::radians(transform.m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::vec3 zoneRight = glm::normalize(glm::vec3(zoneRotation[0]));
            const glm::vec3 zoneUp = glm::normalize(glm::vec3(zoneRotation[1]));
            const glm::vec3 zoneForward = glm::normalize(glm::vec3(zoneRotation[2]));
            zoneState.rightX = zoneRight.x;
            zoneState.rightY = zoneRight.y;
            zoneState.rightZ = zoneRight.z;
            zoneState.upX = zoneUp.x;
            zoneState.upY = zoneUp.y;
            zoneState.upZ = zoneUp.z;
            zoneState.forwardX = zoneForward.x;
            zoneState.forwardY = zoneForward.y;
            zoneState.forwardZ = zoneForward.z;
            zoneState.radius = zone->radius;
            zoneState.halfExtentX = zone->halfExtentX;
            zoneState.halfExtentY = zone->halfExtentY;
            zoneState.halfExtentZ = zone->halfExtentZ;
            zoneState.fadeDistance = zone->fadeDistance;
            zoneState.wetGain = zone->wetGain;
            zoneState.priority = zone->priority;
            zoneState.preset = VansEngine::AudioReverbPresetFromString(zone->preset);
            zoneState.presetParameters = zone->presetParameters;
            zoneState.overridePresetParameters = zone->overridePresetParameters;

            const VansEngine::AudioReverbZoneEvaluation evaluation =
                VansEngine::EvaluateReverbZone(listener.x, listener.y, listener.z, zoneState);
            evaluations.push_back(evaluation);
        }
    }

    if (!hasZone)
    {
        m_AudioEnvironmentReverbWetGain = 1.0f;
        audioSystem.SetDefaultReverbPreset(VansEngine::AudioReverbPreset::Generic);
        audioSystem.SetDefaultReverbWetGain(m_AudioEnvironmentReverbWetGain);
        return;
    }

    const VansEngine::AudioReverbEnvironmentEvaluation environment =
        VansEngine::EvaluateReverbEnvironment(evaluations);

    audioSystem.SetDefaultReverbPreset(environment.preset);
    audioSystem.SetDefaultReverbParameters(
        environment.presetParameters,
        VansEngine::AudioReverbPresetToString(environment.preset));
    const float targetWetGain = environment.affectsListener ? environment.wetGain : 0.0f;
    m_AudioEnvironmentReverbWetGain = VansEngine::ComputeSmoothedReverbWetGain(
        m_AudioEnvironmentReverbWetGain,
        targetWetGain,
        deltaTime);
    audioSystem.SetDefaultReverbWetGain(m_AudioEnvironmentReverbWetGain);
}

void VansGraphics::VansScene::EvaluateAnimations(float deltaTime){
	const VansAnimationFrameContext animationContext{
		m_LoadMode == VansSceneLoadMode::Runtime
			? VansAnimationEvaluationPurpose::Gameplay
			: VansAnimationEvaluationPurpose::EditorPreview,
		deltaTime };
	const auto isSceneDriven = [this](VansAnimationNode* animationNode)
	{
		return animationNode && animationNode->IsEnabled() &&
			(m_LoadMode != VansSceneLoadMode::Editor ||
			 m_EditorPreviewDrivenAnimationNodes.find(animationNode) ==
				m_EditorPreviewDrivenAnimationNodes.end());
	};
	m_AnimationWorldQueryRequests.clear();
	m_AnimationWorldQueryResults.clear();
	for (VansAnimationNode* animNode : m_AnimationNodes)
		if (isSceneDriven(animNode))
			animNode->PrepareAnimationFrame(animationContext);

	for (VansAnimationNode* animNode : m_AnimationNodes)
	{
		if (!isSceneDriven(animNode)) continue;
		animNode->GatherAnimationWorldQueries();
		const auto& nodeRequests = animNode->GetAnimationWorldQueries();
		m_AnimationWorldQueryRequests.insert(
			m_AnimationWorldQueryRequests.end(), nodeRequests.begin(), nodeRequests.end());
	}
	VansAnimationWorldQueryBatch::Execute(
		m_AnimationWorldQueryRequests, m_AnimationWorldQueryResults);

	for (VansAnimationNode* animNode : m_AnimationNodes)
	{
		if (isSceneDriven(animNode))
		{
			animNode->ResolveAnimationWorldQueries(m_AnimationWorldQueryResults);
			VansEngine::VansRagdollSystem::GetInstance().PostAnimationUpdate(animNode);
		}
	}
}

bool VansGraphics::VansScene::BeginEditorAnimationPreview(
	VansAnimationNode* animationNode)
{
	if (m_LoadMode != VansSceneLoadMode::Editor || !animationNode ||
		std::find(m_AnimationNodes.begin(), m_AnimationNodes.end(), animationNode) ==
			m_AnimationNodes.end())
	{
		return false;
	}
	return m_EditorPreviewDrivenAnimationNodes.insert(animationNode).second;
}

void VansGraphics::VansScene::EndEditorAnimationPreview(
	VansAnimationNode* animationNode)
{
	if (animationNode)
		m_EditorPreviewDrivenAnimationNodes.erase(animationNode);
}

bool VansGraphics::VansScene::EvaluateEditorAnimationPreviewStep(
	VansAnimationNode* animationNode, float deltaTime)
{
	if (m_LoadMode != VansSceneLoadMode::Editor || !animationNode ||
		std::find(m_AnimationNodes.begin(), m_AnimationNodes.end(), animationNode) ==
			m_AnimationNodes.end() ||
		m_EditorPreviewDrivenAnimationNodes.find(animationNode) ==
			m_EditorPreviewDrivenAnimationNodes.end())
	{
		return false;
	}

	animationNode->PrepareAnimationFrame({
		VansAnimationEvaluationPurpose::EditorPreview,
		(std::max)(deltaTime, 0.0f) });
	animationNode->GatherAnimationWorldQueries();
	std::vector<VansWorldQueryResult> results;
	VansAnimationWorldQueryBatch::Execute(
		animationNode->GetAnimationWorldQueries(), results);
	animationNode->ResolveAnimationWorldQueries(results);
	return true;
}

void VansGraphics::VansScene::UploadAnimationRenderData(
	const VansRenderSceneFrameSnapshot& snapshot)
{
	for (const VansRenderAnimationFrameData& animation : snapshot.animations)
	{
		if (animation.animationNodeIndex >= m_AnimationNodes.size())
			continue;
		VansAnimationNode* animationNode =
			m_AnimationNodes[animation.animationNodeIndex];
		if (animationNode)
			animationNode->UploadBoneMatrices(0, animation.boneMatrices);
	}
}

void VansGraphics::VansScene::UpdateRenderNodesDataBeforeRecord(
	const VansRenderViewSnapshot& view,
	const VansRenderSceneFrameSnapshot& sceneSnapshot)
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    if (vkDevice == nullptr)
    {
        return;
    }

    auto updateNode = [&](VansRenderNode* node)
    {
        if (IsRenderNodeEnabledForCurrentFrame(node))
        {
            node->UpdateRenderData(vkDevice, m_MaterialManager, m_LightManager, m_Camera);
            node->UpdateDescriptorSets(m_MaterialManager);
        }
    };

    updateNode(m_DeferredNode);
	if (IsRenderNodeEnabledForCurrentFrame(m_TerrainRenderNode))
	{
		m_TerrainRenderNode->UpdateDescriptorSets(m_MaterialManager);
		auto* terrainNode = static_cast<VansTerrainRenderNode*>(m_TerrainRenderNode);
		if (VansTerrain* terrain = terrainNode->GetTerrain())
			terrain->Update(view.position, view.projection * view.view);
	}
    updateNode(m_WaterRenderNode);
    updateNode(m_VegetationRenderNode);

    for (auto* node : m_OpaqueRenderNodes)
        updateNode(node);
	for (auto* node : m_HairRenderNodes)
		updateNode(node);
	for (auto* node : m_ForwardOpaquePreAtmosphereRenderNodes)
		updateNode(node);
    for (auto* node : m_TransParentRenderNodes)
        updateNode(node);
    for (auto* node : m_PostProcessRenderNodes)
        updateNode(node);
    for (auto* node : m_ScreenSpaceRenderNodes)
        updateNode(node);
    // 贴花节点：更新 GBuffer2 descriptor 绑定
    for (auto* node : m_DecalRenderNodes)
        updateNode(node);
}

void VansGraphics::VansScene::MarkRenderNodeDescriptorSetsDirty()
{
	auto markNode = [](VansRenderNode* node)
	{
		if (node != nullptr)
			node->MarkDescriptorSetsDirty();
	};

	markNode(m_DeferredNode);
	markNode(m_TerrainRenderNode);
	markNode(m_WaterRenderNode);
	markNode(m_VegetationRenderNode);

	for (auto* node : m_OpaqueRenderNodes)
		markNode(node);
	for (auto* node : m_HairRenderNodes)
		markNode(node);
	for (auto* node : m_ForwardOpaquePreAtmosphereRenderNodes)
		markNode(node);
	for (auto* node : m_TransParentRenderNodes)
		markNode(node);
	for (auto* node : m_PostProcessRenderNodes)
		markNode(node);
	for (auto* node : m_ScreenSpaceRenderNodes)
		markNode(node);
	for (auto* node : m_DecalRenderNodes)
		markNode(node);
}
void VansGraphics::VansScene::BuildRayTracingAS(VansVKDevice* vans_device, VansVKCommandBuffer* vans_commandBuffer)
{
    VkDevice device = vans_device->GetLogicDevice();

	// BLAS is mesh-scoped, but GI participation is instance/material-scoped.
	// Only build a BLAS when at least one static, opaque, RT-enabled instance
	// can actually enter the TLAS. This keeps glass and other transparent-only
	// meshes out of both acceleration structures and GI hit generation.
	std::unordered_set<VansMesh*> giEligibleMeshSet;
	std::vector<VansMesh*> giEligibleMeshes;
	for (VansRenderNode* node : m_OpaqueRenderNodes)
	{
		if (!node || !node->IsEnabled() || !node->m_RayTracingEnabled ||
			node->m_HasSkeletonBone || node->m_AnimOwner || !node->m_Mesh ||
			!node->m_Mesh->m_SupportRayTracing)
			continue;
		if (node->m_Material &&
			(node->m_Material->m_MaterialType == VAN_TRANSPARENT ||
			 node->m_Material->m_MaterialType == VAN_PBR_TRANSMISSION))
			continue;
		if (giEligibleMeshSet.insert(node->m_Mesh).second)
			giEligibleMeshes.push_back(node->m_Mesh);
	}

	for (VansMesh* mesh : giEligibleMeshes)
	{
		mesh->SetBLASIndex(-1);
		mesh->BuildBLAS(*vans_device, *vans_commandBuffer);

		int blasIndex = m_BLASVertexData.size();
		mesh->SetBLASIndex(blasIndex);
		m_BLASVertexData.push_back(mesh->GetBLASVertexBuffer());
		m_BLASIndexData.push_back(mesh->GetIndexBuffer());
	}

	VANS_LOG("[BuildRayTracingAS] BLAS build complete, collecting TLAS instances (opaqueNodes="
		<< m_OpaqueRenderNodes.size() << ", eligibleMeshes=" << giEligibleMeshes.size()
		<< ")");

    int nodeIdx = 0;
    uint32_t skippedAnimated = 0;
	uint32_t skippedDisabled = 0;
    uint32_t skippedMissingMesh = 0;
    uint32_t skippedNoRayTracing = 0;
	uint32_t skippedTransparentMaterial = 0;
    for (auto& node : m_OpaqueRenderNodes)
    {
		if (!node || !node->IsEnabled())
		{
			++skippedDisabled;
			++nodeIdx;
			continue;
		}
        // 跳过骨骼动画节点（不参与光线追踪）
        if (node->m_HasSkeletonBone || node->m_AnimOwner)
        {
            ++skippedAnimated;
            ++nodeIdx;
            continue;
        }
        // 多网格父容器节点没有自身 Mesh，静默跳过。
        if (!node->m_Mesh)
        {
            ++skippedMissingMesh;
            ++nodeIdx;
            continue;
        }
		if (!node->m_RayTracingEnabled || !node->m_Mesh->m_SupportRayTracing ||
			node->m_Mesh->GetBLASIndex() < 0 || node->m_Mesh->GetBLAS() == VK_NULL_HANDLE)
        {
            ++skippedNoRayTracing;
            ++nodeIdx;
            continue;
        }
		if (node->m_Material &&
			(node->m_Material->m_MaterialType == VAN_TRANSPARENT ||
			 node->m_Material->m_MaterialType == VAN_PBR_TRANSMISSION))
		{
			++skippedTransparentMaterial;
			++nodeIdx;
			continue;
		}

        auto transformMatrix = node->GetTransformMatrix();

        // 创建实例缓冲区
        VkAccelerationStructureInstanceKHR instance{};
        instance.transform.matrix[0][0] = transformMatrix[0][0];
        instance.transform.matrix[0][1] = transformMatrix[1][0];
        instance.transform.matrix[0][2] = transformMatrix[2][0];
        instance.transform.matrix[0][3] = transformMatrix[3][0]; // translation.x

        instance.transform.matrix[1][0] = transformMatrix[0][1];
        instance.transform.matrix[1][1] = transformMatrix[1][1];
        instance.transform.matrix[1][2] = transformMatrix[2][1];
        instance.transform.matrix[1][3] = transformMatrix[3][1]; // translation.y

        instance.transform.matrix[2][0] = transformMatrix[0][2];
        instance.transform.matrix[2][1] = transformMatrix[1][2];
        instance.transform.matrix[2][2] = transformMatrix[2][2];
        instance.transform.matrix[2][3] = transformMatrix[3][2]; // translation.z
        //instance.transform = {
        //    1.0f, 0.0f, 0.0f, 0.0f,
        //    0.0f, 1.0f, 0.0f, 0.0f,
        //    0.0f, 0.0f, 1.0f, 0.0f
        //};
        instance.instanceCustomIndex = 0;
        instance.mask = 0xFF;
        instance.instanceShaderBindingTableRecordOffset = 0;
        instance.flags = 0;// VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

        // 获取BLAS地址
        VkAccelerationStructureDeviceAddressInfoKHR asAddressInfo{};
        asAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        asAddressInfo.accelerationStructure = node->m_Mesh->GetBLAS();
        asAddressInfo.pNext = nullptr;
        instance.accelerationStructureReference = vans_device->GetAccelerationAddress(&asAddressInfo);

        m_TlasInstancesInfos.push_back(instance);

        m_TLASInstaneData.push_back(node->m_Mesh->GetBLASIndex());

		// The texture table is indexed by TLAS instance, not BLAS index.  Keep
		// material class flags in the high bits and the five-texture base index
		// in the low bits; the closest-hit shader decodes the same contract.
		constexpr uint32_t kInvalidGITextureIndex = 0xFFFFFFFFu;
		constexpr uint32_t kGITextureIndexMask = 0x3FFFFFFFu;
		constexpr uint32_t kGIPureEmissiveFlag = 0x40000000u;
		constexpr uint32_t kGIPBREmissiveFlag = 0x80000000u;
		uint32_t packedTextureIndex = kInvalidGITextureIndex;
		glm::vec4 emissionScale(0.0f);
		if (node->m_Material)
		{
			const VansMaterialType materialType = node->m_Material->m_MaterialType;
			const bool isPBR = materialType == VAN_PBR;
			const bool isPureEmissive = materialType == VAN_EMISSIVE;
			const bool isPBREmissive = materialType == VAN_PBR_EMISSIVE;
			if (isPBR || isPureEmissive || isPBREmissive)
			{
				const std::string materialKey = std::to_string(static_cast<uint32_t>(materialType)) + ":" +
					node->m_Material->m_AssetName;
				auto textureIndexIT = m_TlasInstanceMaterialToIndex.find(materialKey);
				uint32_t textureIndex = 0u;
				if (textureIndexIT == m_TlasInstanceMaterialToIndex.end())
				{
					textureIndex = static_cast<uint32_t>(m_TlasInstanceTextures.size());
					m_TlasInstanceMaterialToIndex.emplace(materialKey, static_cast<int>(textureIndex));
					if (isPBR)
					{
						auto* pbr = static_cast<VansPBRMaterial*>(node->m_Material);
						m_TlasInstanceTextures.push_back(pbr->m_BaseColorTexture->GetImage());
						m_TlasInstanceTextures.push_back(pbr->m_NormalTexture->GetImage());
						m_TlasInstanceTextures.push_back(pbr->m_MetalTexture->GetImage());
						m_TlasInstanceTextures.push_back(pbr->m_RoughnessTexture->GetImage());
						m_TlasInstanceTextures.push_back(pbr->m_AoTexture->GetImage());
					}
					else
					{
						auto* emissive = static_cast<VansEmissiveMaterial*>(node->m_Material);
						if (isPureEmissive)
						{
							for (uint32_t slot = 0u; slot < 5u; ++slot)
								m_TlasInstanceTextures.push_back(emissive->m_EmissiveTexture->GetImage());
						}
						else
						{
							m_TlasInstanceTextures.push_back(emissive->m_BaseColorTexture->GetImage());
							m_TlasInstanceTextures.push_back(emissive->m_NormalTexture->GetImage());
							m_TlasInstanceTextures.push_back(emissive->m_MetalTexture->GetImage());
							m_TlasInstanceTextures.push_back(emissive->m_RoughnessTexture->GetImage());
							m_TlasInstanceTextures.push_back(emissive->m_EmissiveTexture->GetImage());
						}
					}
				}
				else
				{
					textureIndex = static_cast<uint32_t>(textureIndexIT->second);
				}

				packedTextureIndex = textureIndex & kGITextureIndexMask;
				if (isPureEmissive)
				{
					auto* emissive = static_cast<VansEmissiveMaterial*>(node->m_Material);
					packedTextureIndex |= kGIPureEmissiveFlag;
					emissionScale = glm::vec4(emissive->m_BasePBRParam.m_albedo *
						std::max(emissive->m_BasePBRParam.m_roughness, 0.0f), 1.0f);
				}
				else if (isPBREmissive)
				{
					auto* emissive = static_cast<VansEmissiveMaterial*>(node->m_Material);
					packedTextureIndex |= kGIPBREmissiveFlag;
					emissionScale = glm::vec4(std::max(emissive->m_BasePBRParam.padding, 0.0f));
				}
			}
		}
		m_TlasInstanceTextureIndex.push_back(packedTextureIndex);
		m_TlasInstanceGIEmission.push_back(emissionScale);
		++nodeIdx;
    }

    uint32_t countInstance = static_cast<uint32_t>(m_TlasInstancesInfos.size());

    VANS_LOG("[BuildRayTracingAS] TLAS instance collection complete (instances=" << countInstance
		<< ", skippedDisabled=" << skippedDisabled
		<< ", skippedAnimated=" << skippedAnimated
        << ", skippedMissingMesh=" << skippedMissingMesh
		<< ", skippedNoRayTracing=" << skippedNoRayTracing
		<< ", skippedTransparentMaterial=" << skippedTransparentMaterial << ")");

    // No RT instances to build — skip TLAS entirely
    if (countInstance == 0)
    {
        VANS_LOG_WARN("[BuildRayTracingAS] No ray-tracing instances found, skipping TLAS build.");
        return;
    }

    m_InstancesBuffer.CreatVulkanBuffer(
        device,
        sizeof(VkAccelerationStructureInstanceKHR) * countInstance,
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    m_InstancesBuffer.SetBufferData(m_TlasInstancesInfos.data(), 0, sizeof(VkAccelerationStructureInstanceKHR) * countInstance);

    // Barrier: host writes instance buffer -> TLAS build reads
    {
        VkMemoryBarrier hostWriteBarrier{};
        hostWriteBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        hostWriteBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        hostWriteBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vans_commandBuffer->PipelineBarrier(
            VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            { hostWriteBarrier });
    }

    VkDeviceAddress instanceBufferAddress = m_InstancesBuffer.GetDeviceAddress(device);

    // Describes instance data in the acceleration structure.
    VkAccelerationStructureGeometryInstancesDataKHR geometryInstances;
    geometryInstances.sType =  VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    geometryInstances.arrayOfPointers = VK_FALSE;
    geometryInstances.data.deviceAddress = instanceBufferAddress;
    geometryInstances.pNext = nullptr;

    // Set up the geometry to use instance data.
    VkAccelerationStructureGeometryKHR geometry;
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = geometryInstances;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
    geometry.pNext = nullptr;

    // Specifies the number of primitives (instances in this case).
    VkAccelerationStructureBuildRangeInfoKHR rangeInfo{};
    rangeInfo.primitiveCount = static_cast<uint32_t>(countInstance);

    m_AsGeometry.push_back(geometry);
    m_AsBuildRangeInfo.push_back(rangeInfo);
    
    
    
    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;
    buildInfo.dstAccelerationStructure = VK_NULL_HANDLE;
    buildInfo.geometryCount = m_AsGeometry.size();
    buildInfo.pGeometries = m_AsGeometry.data();
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.scratchData.deviceAddress = 0;

    std::vector<uint32_t> maxPrimCount(m_AsBuildRangeInfo.size());
    for (size_t i = 0; i < m_AsBuildRangeInfo.size(); ++i)
    {
        maxPrimCount[i] = m_AsBuildRangeInfo[i].primitiveCount;
    }

    //获取as的预分配大小
    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{};
    buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vans_device->GetAccelerationStructureBuildSizes(&buildInfo, maxPrimCount.data(), &buildSizesInfo);

    //scratch izhi
    m_TLASScratchBuffer.CreatVulkanBuffer(
        device,
        buildSizesInfo.buildScratchSize + vans_device->GetAccelerationStructureScratchAlignment() - 1,
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    const VkDeviceSize scratchAlignment = vans_device->GetAccelerationStructureScratchAlignment();
    const VkDeviceAddress scratchBaseAddress = m_TLASScratchBuffer.GetDeviceAddress(device);
    const VkDeviceAddress scratchAddress =
        (scratchBaseAddress + scratchAlignment - 1) & ~(scratchAlignment - 1);


    // 创建缓冲区
    m_TopLevelASBuffer.CreatVulkanBuffer(
        device,
        buildSizesInfo.accelerationStructureSize,
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // 构建TLAS
    VkAccelerationStructureCreateInfoKHR accelCreateInfo = {};
    accelCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelCreateInfo.buffer = m_TopLevelASBuffer.GetNativeBuffer();
    accelCreateInfo.size = buildSizesInfo.accelerationStructureSize;
    accelCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    vans_device->CreateAccelerationStructure(&accelCreateInfo, &m_TopLevelAS);

    //as的地址
    VkAccelerationStructureDeviceAddressInfoKHR asAddressInfo;
    asAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    asAddressInfo.accelerationStructure = m_TopLevelAS;
    asAddressInfo.pNext = nullptr;
    VkDeviceAddress asAddress = vans_device->GetAccelerationAddress(&asAddressInfo);

    const VkAccelerationStructureBuildRangeInfoKHR* ppRangeInfos[] = 
    {
        m_AsBuildRangeInfo.data() // 对于 infoCount=1，仅需一个指针
    };

    //补全剩下的build info
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
    buildInfo.srcAccelerationStructure = VK_NULL_HANDLE;
    buildInfo.dstAccelerationStructure = m_TopLevelAS;
    buildInfo.scratchData.deviceAddress = scratchAddress;
    buildInfo.pGeometries = m_AsGeometry.data();  // In case the structure was copied, we need to update the pointer

    // Barrier: all prior BLAS builds complete -> TLAS build reads them
    {
        VkMemoryBarrier blasToTlasBarrier{};
        blasToTlasBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        blasToTlasBarrier.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR;
        blasToTlasBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vans_commandBuffer->PipelineBarrier(
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            { blasToTlasBarrier });
    }
    
    vans_commandBuffer->BuildAccelerationStructures(&buildInfo, ppRangeInfos);

    VANS_LOG("[BuildRayTracingAS] TLAS build recorded");
}

void VansGraphics::VansScene::ReleaseASTempBuffer(VansVKDevice* vans_device)
{
    VkDevice device = vans_device->GetLogicDevice();
	for (const auto& meshAsset : m_AssetRegistry.GetMeshes())
	{
		VansMesh* mesh = static_cast<VansMesh*>(meshAsset);
		if (mesh->m_SupportRayTracing)
			mesh->ReleaseASTempData(device);
		for (VansMesh* subMesh : mesh->m_SubMeshes)
		{
			if (subMesh && subMesh->m_SupportRayTracing)
				subMesh->ReleaseASTempData(device);
		}
	}

    m_TLASScratchBuffer.DestroyVulkanBuffer(device);
}
VansGraphics::VansScene* m_Scene = nullptr;


// ══════════════════════════════════════════════════════════════════════════════
//  Runtime Dynamic Entity API 实现
// ══════════════════════════════════════════════════════════════════════════════

std::vector<VansRenderNode*> VansGraphics::VansScene::CollectSSBOManagedRenderNodes() const
{
    std::vector<VansRenderNode*> result;
    result.reserve(m_OpaqueRenderNodes.size()
				 + m_HairRenderNodes.size()
				 + m_ForwardOpaquePreAtmosphereRenderNodes.size()
                 + m_TransParentRenderNodes.size()
                 + m_DecalRenderNodes.size());
    result.insert(result.end(), m_OpaqueRenderNodes.begin(),    m_OpaqueRenderNodes.end());
	result.insert(result.end(), m_HairRenderNodes.begin(), m_HairRenderNodes.end());
	result.insert(result.end(), m_ForwardOpaquePreAtmosphereRenderNodes.begin(), m_ForwardOpaquePreAtmosphereRenderNodes.end());
    result.insert(result.end(), m_TransParentRenderNodes.begin(), m_TransParentRenderNodes.end());
    result.insert(result.end(), m_DecalRenderNodes.begin(),     m_DecalRenderNodes.end());
    return result;
}

size_t VansGraphics::VansScene::GetTransformSlotCount() const
{
    return m_TransformSlotAllocator.GetActiveCount();
}

size_t VansGraphics::VansScene::GetTransformSlotCapacity() const
{
    return static_cast<size_t>(m_TransformSlotAllocator.GetMaxCapacity());
}

float VansGraphics::VansScene::GetTransformSlotUsage() const
{
    return m_TransformSlotAllocator.GetUsageRatio();
}

uint32_t VansGraphics::VansScene::AllocateTransformSlot()
{
    return m_TransformSlotAllocator.AllocateSlot();
}

bool VansGraphics::VansScene::CreateInstanceTransformBuffer(
    VkDevice& device,
    VkDeviceSize size,
    VkBufferUsageFlags usage,
    VkMemoryPropertyFlags memoryProperties)
{
    return m_InstanceTransformDataBuffer.CreatVulkanBuffer(
        device,
        std::max<VkDeviceSize>(size, sizeof(ModelDataStruct)),
        VK_FORMAT_R32_SFLOAT,
        usage,
        memoryProperties);
}

bool VansGraphics::VansScene::SetInstanceTransformData(const ModelDataStruct& data, uint32_t slot)
{
    const VkDeviceSize offset = static_cast<VkDeviceSize>(slot) * sizeof(ModelDataStruct);
    return m_InstanceTransformDataBuffer.SetBufferData(&data, offset, sizeof(ModelDataStruct));
}

void VansGraphics::VansScene::UpdateMappedInstanceTransformData(const ModelDataStruct& data, uint32_t slot)
{
    const VkDeviceSize offset = static_cast<VkDeviceSize>(slot) * sizeof(ModelDataStruct);
    m_InstanceTransformDataBuffer.UpdateMapped(&data, offset, sizeof(ModelDataStruct));
}

bool VansGraphics::VansScene::PersistentlyMapInstanceTransformBuffer()
{
    return m_InstanceTransformDataBuffer.PersistentMap();
}

void VansGraphics::VansScene::CreateGlobalTransformDescriptorSet(VkDescriptorSetLayoutBinding binding)
{
    VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(
        { binding },
        m_GlobalTransformDataSetLayout,
        m_GlobalTransformDataDescriptorSets);
    UpdateTransformDescriptorSet();
}

void VansGraphics::VansScene::UpdateTransformDescriptorSet()
{
    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();
	if (!m_GlobalTransformDataDescriptorSets.empty())
	{
		descManager->WriteBufferDescriptor(
			m_GlobalTransformDataDescriptorSets[0],
			PassBinding::BUFFER_0,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{
				m_InstanceTransformDataBuffer.GetNativeBuffer(),
				0,
				m_InstanceTransformDataBuffer.GetBufferSize()
			}});
	}
	if (m_ObjectDescriptorSet != VK_NULL_HANDLE)
	{
		descManager->WriteBufferDescriptor(
			m_ObjectDescriptorSet,
			OBJECT_BINDING_TRANSFORM_SSBO,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{
				m_InstanceTransformDataBuffer.GetNativeBuffer(),
				0,
				m_InstanceTransformDataBuffer.GetBufferSize()
			}});
	}
    descManager->CommitDescriptorUpdates();
}

void VansGraphics::VansScene::UpdateObjectDescriptorSet()
{
	if (m_ObjectDescriptorSet == VK_NULL_HANDLE)
		return;
	auto* vkDevice = static_cast<VansVKDevice*>(m_GraphicsDevice);
	if (vkDevice == nullptr || !vkDevice->GetDrawInstanceArena().IsReady())
	{
		VANS_LOG_ERROR("[VansScene] Cannot update object descriptors before backend draw-instance resources are ready.");
		return;
	}
	const VansVKBuffer& drawInstanceBuffer = vkDevice->GetDrawInstanceArena().GetBuffer();
	auto* descManager = VansVKDescriptorManager::GetInstance();
	descManager->BeginDescriptorUpdate();
	descManager->WriteBufferDescriptor(
		m_ObjectDescriptorSet,
		OBJECT_BINDING_TRANSFORM_SSBO,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{m_InstanceTransformDataBuffer.GetNativeBuffer(), 0, m_InstanceTransformDataBuffer.GetBufferSize()}});
	descManager->WriteBufferDescriptor(
		m_ObjectDescriptorSet,
		OBJECT_BINDING_DRAW_INSTANCE_SSBO,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{drawInstanceBuffer.GetNativeBuffer(), 0, drawInstanceBuffer.GetBufferSize()}});
	descManager->CommitDescriptorUpdates();
}

bool VansGraphics::VansScene::GrowTransformBuffer(VkDevice& device, uint32_t newCapacity)
{
    const uint32_t oldCapacity = m_TransformSlotAllocator.GetMaxCapacity();
    if (newCapacity <= oldCapacity)
    {
        VANS_LOG("[Scene] GrowTransformBuffer: new capacity ("
                  << newCapacity << ") <= old (" << oldCapacity << ")");
        return false;
    }

    VANS_LOG("[Scene] GrowTransformBuffer: " << oldCapacity << " -> " << newCapacity);

    // ── 1. 创建新的更大 Buffer ───────────────────────────────────────────
    const VkDeviceSize newSize = sizeof(ModelDataStruct) * newCapacity;
    VansVKBuffer newBuffer;
    newBuffer.CreatVulkanBuffer(
        device, newSize, VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    // ── 2. 拷贝所有 Active Slot 到新 Buffer ──────────────────────────────
    // slot 编号完全不变, 只是 buffer 容量变大
    // 旧 Buffer 是 HOST_VISIBLE 持久映射的，直接从 CPU 映射内存读取
    const uint8_t* oldMapped = static_cast<const uint8_t*>(m_InstanceTransformDataBuffer.GetMappedPtr());
    for (uint32_t slot : m_TransformSlotAllocator.GetActiveSlots())
    {
        VkDeviceSize offset = slot * sizeof(ModelDataStruct);
        ModelDataStruct data;
        if (oldMapped)
            memcpy(&data, oldMapped + offset, sizeof(ModelDataStruct));
        newBuffer.SetBufferData(&data, static_cast<int>(offset), sizeof(ModelDataStruct));
    }

    // ── 3. 替换 Buffer（帧边界调用 + WaitIdle 确保 GPU 不在使用旧 Buffer）──
    auto* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    if (vkDevice == nullptr || !vkDevice->WaitForDevice())
    {
        VANS_LOG_ERROR("[Scene] GrowTransformBuffer: failed to wait for device before buffer replacement");
        newBuffer.DestroyVulkanBuffer(device);
        return false;
    }
    m_InstanceTransformDataBuffer.DestroyVulkanBuffer(device);
    m_InstanceTransformDataBuffer = std::move(newBuffer);
    m_InstanceTransformDataBuffer.PersistentMap();

    // ── 4. 更新 Allocator 容量 ───────────────────────────────────────────
    m_TransformSlotAllocator.SetMaxCapacity(newCapacity);

    // ── 5. Re-write Descriptor Set 2 (指到新 Buffer) ─────────────────────
    UpdateTransformDescriptorSet();

    VANS_LOG("[Scene] GrowTransformBuffer: done, new capacity=" << newCapacity);

    return true;
}

void VansGraphics::VansScene::RemoveRenderNodeFromVector(VansRenderNode* node)
{
    if (!node) return;
    std::vector<VansRenderNode*>* vec = nullptr;
    switch (node->GetNodeType())
    {
    case OPAQUE_NODE:      vec = &m_OpaqueRenderNodes;      break;
	case HAIR_NODE:        vec = &m_HairRenderNodes;        break;
	case FORWARD_OPAQUE_PRE_ATMOSPHERE_NODE:
		vec = &m_ForwardOpaquePreAtmosphereRenderNodes;
		break;
    case TRANSPARENT_NODE: vec = &m_TransParentRenderNodes; break;
    case DECAL_NODE:       vec = &m_DecalRenderNodes;       break;
    default: return; // 不在 SSBO 管理的列表中
    }
    auto it = std::find(vec->begin(), vec->end(), node);
    if (it != vec->end()) { std::swap(*it, vec->back()); vec->pop_back(); }
}

void VansGraphics::VansScene::UpdateLightComponentIndex(
    int oldIndex, int newIndex, VansLightType type)
{
    std::optional<VansScriptLightIndexKind> lightKind;
    std::uint16_t runtimeLightType = Vans::VansInvalidComponentTypeId;
    switch (type)
    {
    case VansLightType::DIRECTIONAL:
        lightKind = VansScriptLightIndexKind::Directional;
        runtimeLightType = Vans::VansRuntimeComponentType_DirectionalLight;
        break;
    case VansLightType::POINT:
        lightKind = VansScriptLightIndexKind::Point;
        runtimeLightType = Vans::VansRuntimeComponentType_PointLight;
        break;
    case VansLightType::SPOT:
        lightKind = VansScriptLightIndexKind::Spot;
        runtimeLightType = Vans::VansRuntimeComponentType_SpotLight;
        break;
    case VansLightType::RECT:
        lightKind = VansScriptLightIndexKind::Rect;
        runtimeLightType = Vans::VansRuntimeComponentType_RectLight;
        break;
    default:
        break;
    }
    if (!lightKind)
        return;

    if (m_RuntimeWorld && runtimeLightType != Vans::VansInvalidComponentTypeId)
    {
        auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeLightComponent>*>(
            m_RuntimeWorld->FindStorage(runtimeLightType));
        if (storage)
        {
            for (Vans::VansEntityHandle entity : m_RuntimeWorld->Entities().CollectAliveEntities())
            {
                for (Vans::VansComponentHandle component : m_RuntimeWorld->CollectComponentsOwnedBy(entity))
                {
                    if (component.typeId != runtimeLightType)
                        continue;
                    auto* runtimeLight = storage->Get(component);
                    if (runtimeLight && runtimeLight->lightIndex == oldIndex)
                        runtimeLight->lightIndex = newIndex;
                }
            }
        }
    }

    for (auto* obj : m_SceneObjects)
    {
        if (!obj) continue;
        for (auto* comp : obj->m_Components)
        {
            if (comp)
                comp->RebindSceneLightIndex(*lightKind, oldIndex, newIndex);
        }
    }
}

bool VansGraphics::VansScene::ApplyRuntimeComponentEnabled(
	Vans::VansComponentHandle component,
	bool effectiveEnabled)
{
	if (!m_RuntimeWorld || !component.IsValid())
		return false;

	switch (component.typeId)
	{
	case Vans::VansRuntimeComponentType_Transform:
	case Vans::VansRuntimeComponentType_Vehicle:
	case Vans::VansRuntimeComponentType_AudioReverbZone:
	case Vans::VansRuntimeComponentType_AudioVolume:
	case Vans::VansRuntimeComponentType_Video:
	case Vans::VansRuntimeComponentType_DirectionalLight:
	case Vans::VansRuntimeComponentType_PointLight:
	case Vans::VansRuntimeComponentType_SpotLight:
	case Vans::VansRuntimeComponentType_RectLight:
		return true;
	case Vans::VansRuntimeComponentType_Render:
	{
		auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeRenderComponent>*>(
			m_RuntimeWorld->FindStorage(component.typeId));
		auto* runtimeComponent = storage ? storage->Get(component) : nullptr;
		if (!runtimeComponent)
			return false;
		if (!runtimeComponent->renderNodes.empty())
		{
			for (auto* renderNode : runtimeComponent->renderNodes)
				if (renderNode) renderNode->SetEnabled(effectiveEnabled);
		}
		else if (runtimeComponent->renderNode)
		{
			runtimeComponent->renderNode->SetEnabled(effectiveEnabled);
		}
		return true;
	}
	case Vans::VansRuntimeComponentType_Physics:
	{
		auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimePhysicsComponent>*>(
			m_RuntimeWorld->FindStorage(component.typeId));
		auto* runtimeComponent = storage ? storage->Get(component) : nullptr;
		if (!runtimeComponent)
			return false;
		if (runtimeComponent->physicsNode)
			runtimeComponent->physicsNode->SetEnabled(effectiveEnabled);
		return true;
	}
	case Vans::VansRuntimeComponentType_Cloth:
	{
		auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeClothComponent>*>(
			m_RuntimeWorld->FindStorage(component.typeId));
		auto* runtimeComponent = storage ? storage->Get(component) : nullptr;
		if (!runtimeComponent)
			return false;
		if (runtimeComponent->clothNode)
			runtimeComponent->clothNode->SetEnabled(effectiveEnabled);
		return true;
	}
	case Vans::VansRuntimeComponentType_CharacterController:
	{
		auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeCharacterControllerComponent>*>(
			m_RuntimeWorld->FindStorage(component.typeId));
		auto* runtimeComponent = storage ? storage->Get(component) : nullptr;
		if (!runtimeComponent)
			return false;
		if (runtimeComponent->controllerNode)
			runtimeComponent->controllerNode->SetEnabled(effectiveEnabled);
		return true;
	}
	case Vans::VansRuntimeComponentType_Animation:
	{
		auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeAnimationComponent>*>(
			m_RuntimeWorld->FindStorage(component.typeId));
		auto* runtimeComponent = storage ? storage->Get(component) : nullptr;
		if (!runtimeComponent)
			return false;
		if (runtimeComponent->animationNode)
			runtimeComponent->animationNode->SetEnabled(effectiveEnabled);
		return true;
	}
	case Vans::VansRuntimeComponentType_Ragdoll:
	{
		auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeRagdollComponent>*>(
			m_RuntimeWorld->FindStorage(component.typeId));
		auto* runtimeComponent = storage ? storage->Get(component) : nullptr;
		if (!runtimeComponent)
			return false;
		if (runtimeComponent->animationNode)
			runtimeComponent->animationNode->SetEnabled(effectiveEnabled);
		return true;
	}
	case Vans::VansRuntimeComponentType_Camera:
	{
		auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeCameraComponent>*>(
			m_RuntimeWorld->FindStorage(component.typeId));
		auto* runtimeComponent = storage ? storage->Get(component) : nullptr;
		if (!runtimeComponent)
			return false;
		if (runtimeComponent->camera)
			runtimeComponent->camera->SetEnabled(effectiveEnabled);
		return true;
	}
	case Vans::VansRuntimeComponentType_Audio:
	{
		auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeAudioComponent>*>(
			m_RuntimeWorld->FindStorage(component.typeId));
		auto* runtimeComponent = storage ? storage->Get(component) : nullptr;
		if (!runtimeComponent)
			return false;
		if (runtimeComponent->sourceBinding)
		{
			if (!effectiveEnabled)
				runtimeComponent->sourceBinding->SetVelocity(0.0f, 0.0f, 0.0f);
			runtimeComponent->sourceBinding->SetEnabled(effectiveEnabled);
		}
		else if (runtimeComponent->audioNode)
		{
			runtimeComponent->audioNode->SetEnabled(effectiveEnabled);
		}
		return true;
	}
	case Vans::VansRuntimeComponentType_Particle:
	{
		auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeParticleComponent>*>(
			m_RuntimeWorld->FindStorage(component.typeId));
		auto* runtimeComponent = storage ? storage->Get(component) : nullptr;
		if (!runtimeComponent)
			return false;
		if (runtimeComponent->runtime)
			runtimeComponent->runtime->m_IsPlaying = effectiveEnabled;
		runtimeComponent->isPlaying = effectiveEnabled;
		return true;
	}
	case Vans::VansRuntimeComponentType_UI:
	{
		auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeUIComponent>*>(
			m_RuntimeWorld->FindStorage(component.typeId));
		auto* runtimeComponent = storage ? storage->Get(component) : nullptr;
		if (!runtimeComponent)
			return false;

		if (!VansRuntime::VansUISystem::Get().IsInitialized())
		{
			if (!effectiveEnabled)
				runtimeComponent->openScreens.clear();
			return true;
		}

		if (effectiveEnabled)
		{
			for (const std::string& screenPath : runtimeComponent->preloadScreens)
				VansRuntime::VansUISystem::Get().PreloadScreen(screenPath);
			runtimeComponent->openScreens.clear();
			for (const std::string& screenPath : runtimeComponent->autoOpenScreens)
			{
				auto screen = VansRuntime::VansUISystem::Get().LoadScreen(screenPath);
				if (screen)
					runtimeComponent->openScreens.push_back(screen->GetHandleId());
			}
		}
		else
		{
			for (std::uint64_t screenId : runtimeComponent->openScreens)
				VansRuntime::VansUISystem::Get().CloseScreen(screenId);
			runtimeComponent->openScreens.clear();
			for (const std::string& screenPath : runtimeComponent->preloadScreens)
				VansRuntime::VansUISystem::Get().ReleaseScreen(screenPath);
		}
		return true;
	}
	case Vans::VansRuntimeComponentType_Script:
		return false;
	default:
		return false;
	}
}

bool VansGraphics::VansScene::CopyRuntimeUIOpenScreens(
	Vans::VansComponentHandle component,
	std::vector<std::uint64_t>& outOpenScreens) const
{
	outOpenScreens.clear();
	if (!m_RuntimeWorld || component.typeId != Vans::VansRuntimeComponentType_UI)
		return false;
	auto* storage = static_cast<const Vans::VansComponentStorage<Vans::VansRuntimeUIComponent>*>(
		m_RuntimeWorld->FindStorage(component.typeId));
	const auto* runtimeComponent = storage ? storage->Get(component) : nullptr;
	if (!runtimeComponent)
		return false;
	outOpenScreens = runtimeComponent->openScreens;
	return true;
}

bool VansGraphics::VansScene::SyncRuntimeScriptComponentFromFacade(
	const std::string& componentGuid,
	const VansLuaScriptComponent& component)
{
	if (!m_RuntimeWorld || componentGuid.empty())
		return false;
	const Vans::VansComponentHandle runtimeComponentHandle = m_RuntimeWorld->FindComponentByGuid(
		componentGuid,
		Vans::VansRuntimeComponentType_Script);
	auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeScriptComponent>*>(
		m_RuntimeWorld->FindStorage(Vans::VansRuntimeComponentType_Script));
	auto* runtimeComponent = storage ? storage->Get(runtimeComponentHandle) : nullptr;
	if (!runtimeComponent)
		return false;
	runtimeComponent->enableRequested = component.m_EnableRequested;
	runtimeComponent->state = ToRuntimeScriptState(component.m_State);
	runtimeComponent->isValid = component.m_IsValid;
	runtimeComponent->hasStarted = component.m_HasStarted;
	return true;
}

bool VansGraphics::VansScene::SyncRuntimeAudioComponentFromFacade(VansScriptAudioComponent& component)
{
	if (!m_RuntimeWorld || component.m_ComponentGuid.empty())
		return false;

	const Vans::VansComponentHandle runtimeComponentHandle = m_RuntimeWorld->FindComponentByGuid(
		component.m_ComponentGuid,
		Vans::VansRuntimeComponentType_Audio);
	auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeAudioComponent>*>(
		m_RuntimeWorld->FindStorage(Vans::VansRuntimeComponentType_Audio));
	auto* runtimeComponent = storage ? storage->Get(runtimeComponentHandle) : nullptr;
	if (!runtimeComponent)
		return false;

	runtimeComponent->audioNode = component.m_Source.GetNode();
	runtimeComponent->sourceBinding = &component.m_Source;
	runtimeComponent->sourceName = component.m_Source.GetSourceName();
	runtimeComponent->coneSettings = component.m_ConeSettings;
	runtimeComponent->dopplerEnabled = component.m_DopplerEnabled;
	runtimeComponent->hasLastAudioPosition = component.m_HasLastAudioPosition;
	runtimeComponent->lastAudioPositionX = component.m_LastAudioPositionX;
	runtimeComponent->lastAudioPositionY = component.m_LastAudioPositionY;
	runtimeComponent->lastAudioPositionZ = component.m_LastAudioPositionZ;
	runtimeComponent->occlusionSettings = component.m_OcclusionSettings;
	runtimeComponent->occlusionState = component.m_OcclusionState;
	return true;
}

bool VansGraphics::VansScene::SyncRuntimeVideoComponentFromFacade(VansScriptVideoComponent& component)
{
	if (!m_RuntimeWorld || component.m_ComponentGuid.empty())
		return false;

	const Vans::VansComponentHandle runtimeComponentHandle = m_RuntimeWorld->FindComponentByGuid(
		component.m_ComponentGuid,
		Vans::VansRuntimeComponentType_Video);
	auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeVideoComponent>*>(
		m_RuntimeWorld->FindStorage(Vans::VansRuntimeComponentType_Video));
	auto* runtimeComponent = storage ? storage->Get(runtimeComponentHandle) : nullptr;
	if (!runtimeComponent)
		return false;

	runtimeComponent->videoTexture = component.m_VideoTex;
	runtimeComponent->videoManager = component.m_VideoManager;
	runtimeComponent->bindlessFirstSlot = component.m_BindlessFirstSlot;
	return true;
}

bool VansGraphics::VansScene::SyncRuntimeParticleComponentFromFacade(VansScriptParticleComponent& component)
{
	if (!m_RuntimeWorld || component.m_ComponentGuid.empty())
		return false;

	const Vans::VansComponentHandle runtimeComponentHandle = m_RuntimeWorld->FindComponentByGuid(
		component.m_ComponentGuid,
		Vans::VansRuntimeComponentType_Particle);
	auto* storage = static_cast<Vans::VansComponentStorage<Vans::VansRuntimeParticleComponent>*>(
		m_RuntimeWorld->FindStorage(Vans::VansRuntimeComponentType_Particle));
	auto* runtimeComponent = storage ? storage->Get(runtimeComponentHandle) : nullptr;
	if (!runtimeComponent)
		return false;

	runtimeComponent->runtime = component.m_Runtime.get();
	runtimeComponent->renderNode = component.m_RenderNode;
	runtimeComponent->playOnAwake = component.m_PlayOnAwake;
	runtimeComponent->isPlaying = component.m_IsPlaying;
	runtimeComponent->playTime = component.m_PlayTime;
	runtimeComponent->hasWorldPositionOverride = component.m_HasWorldPositionOverride;
	runtimeComponent->worldPositionOverrideX = component.m_WorldPositionOverride.x;
	runtimeComponent->worldPositionOverrideY = component.m_WorldPositionOverride.y;
	runtimeComponent->worldPositionOverrideZ = component.m_WorldPositionOverride.z;
	return true;
}

bool VansGraphics::VansScene::QueueDestroyEntity(VansScriptObject* obj)
{
    if (!obj || obj->m_EntityGuid.empty())
        return false;

    const auto liveIt = std::find(m_SceneObjects.begin(), m_SceneObjects.end(), obj);
    if (liveIt == m_SceneObjects.end())
        return false;

    if (std::find(m_PendingEntityDestructionGuids.begin(),
                  m_PendingEntityDestructionGuids.end(),
                  obj->m_EntityGuid) == m_PendingEntityDestructionGuids.end())
    {
        m_PendingEntityDestructionGuids.push_back(obj->m_EntityGuid);
    }
    return true;
}

void VansGraphics::VansScene::FlushPendingEntityDestructions()
{
    if (m_PendingEntityDestructionGuids.empty())
        return;

	// Frame N-1 may still reference render nodes and GPU allocations owned by
	// these entities.  Keep the pending list intact unless the ordered render
	// stream and the GPU have both reached a safe structural-mutation point.
	if (m_RenderThreadTransactionExecutor != nullptr &&
		!ExecuteRenderThreadTransaction(
			std::make_unique<SceneEntityDestructionBarrier>()))
	{
		VANS_LOG_ERROR("[VansScene] Could not synchronize deferred entity destruction with RenderThread.");
		return;
	}

    auto pending = std::move(m_PendingEntityDestructionGuids);
    m_PendingEntityDestructionGuids.clear();
    for (const std::string& guid : pending)
    {
        if (VansScriptObject* obj = FindObjectByGuid(guid))
            DestroyEntity(obj);
    }
}

bool VansGraphics::VansScene::DestroyEntity(VansScriptObject* obj)
{
    if (!obj) return false;
    const std::string name = obj->m_ObjectName;
	const std::string guid = obj->m_EntityGuid;

    // ══════════════════════════════════════════════════════════════════════════════
    //  0. 清除编辑器选中状态（必须在任意 delete 之前，防止悬垂比较）
    // ══════════════════════════════════════════════════════════════════════════════

    // ══════════════════════════════════════════════════════════════════════════════
    //  1. 解除 Transform Graph 关联
    // ══════════════════════════════════════════════════════════════════════════════
    if (m_TransformGraph.HasParent(obj->m_TransformID))
        m_TransformGraph.ClearParent(obj->m_TransformID);

    // 将以本实体为 parent 的子节点提升为根节点
    {
        std::vector<uint32_t> childrenToReparent;
        for (const auto& link : m_TransformGraph.GetAllLinks())
            if (link.parentTransformId == obj->m_TransformID)
                childrenToReparent.push_back(link.childTransformId);
        for (uint32_t childID : childrenToReparent)
            m_TransformGraph.ClearParent(childID);
    }

	Vans::VansEntityHandle runtimeEntity;
	if (m_RuntimeWorld && !guid.empty())
		runtimeEntity = m_RuntimeWorld->Entities().FindByGuid(guid);
	if (!m_RuntimeWorld || !runtimeEntity.IsValid())
	{
		VANS_LOG_ERROR("[VansScene] DestroyEntity missing runtime entity for guid='"
			<< guid << "'");
		return false;
	}

    // ══════════════════════════════════════════════════════════════════════════════
    //  2. 从 RuntimeWorld typed component 收集底层 Node 指针与场景级索引。
    //     必须在 delete obj 与 RuntimeWorld destroy 之前完成，避免 wrapper/facade
    //     析构或 typed storage 删除后再访问悬垂指针。
    // ══════════════════════════════════════════════════════════════════════════════
    RuntimeSceneDestroyReferences destroyRefs;
    const uint32_t                           releasedOwnedTransformID = obj->ReleaseOwnedTransform();
    bool                                     ownsTransform = releasedOwnedTransformID != UINT32_MAX;
    uint32_t                                 transformID   = ownsTransform ? releasedOwnedTransformID : obj->m_TransformID;

	destroyRefs = CollectRuntimeSceneDestroyReferences(*m_RuntimeWorld, runtimeEntity);
	if (destroyRefs.skeletonInstance.IsValid())
		m_SkeletonAnchorRegistry.UnregisterInstance(destroyRefs.skeletonInstance);

    VansGraphics::VansRenderNode*            renderNode   = destroyRefs.renderNode;
    VansGraphics::VansParticleRenderNode*    particleRN   = destroyRefs.particleRenderNode;
    VansGraphics::VansAnimationNode*         animNode     = destroyRefs.animationNode;
    VansEngine::VansPhysicsNode*             physicsNode  = destroyRefs.physicsNode;
    VansEngine::VansClothNode*               clothNode    = destroyRefs.clothNode;
    VansEngine::VansCharacterControllerNode* cctNode      = destroyRefs.characterControllerNode;
    VansEngine::VansPhysicsVehicle*          vehicleNode  =
        (destroyRefs.vehicle && m_Vehicle == destroyRefs.vehicle) ? m_Vehicle : nullptr;
    bool                                     hasRagdoll   = destroyRefs.hasRagdoll;
    int dlightIdx = destroyRefs.directionalLightIndex;
    int plightIdx = destroyRefs.pointLightIndex;
    int slightIdx = destroyRefs.spotLightIndex;
    int rlightIdx = destroyRefs.rectLightIndex;

    // ══════════════════════════════════════════════════════════════════════════════
    //  3. 从 m_SceneObjects 移除，delete obj
    //
    //  VansScriptObject 析构函数会逐一 delete m_Components（wrapper 层）。
    //  底层 Node（RenderNode / PhysicsNode 等）不受影响——wrapper 只持非拥有指针。
    // ══════════════════════════════════════════════════════════════════════════════
    auto sit = std::find(m_SceneObjects.begin(), m_SceneObjects.end(), obj);
    if (sit != m_SceneObjects.end())
    {
        m_SceneObjects.erase(sit);
		++m_SceneObjectCollectionGeneration;
	}
    delete obj;     // 析构删除所有 VansScriptComponent wrapper
    obj = nullptr;  // 置空防止后续误用
	m_RuntimeWorld->Commands().DestroyEntity(
		runtimeEntity,
		Vans::VansDestroyChildrenPolicy::ReparentToRoot);
	m_RuntimeWorld->FlushCommands();

    // ══════════════════════════════════════════════════════════════════════════════
    //  4. 持物理锁：清理 Ragdoll / Vehicle / CCT / Cloth / Physics
    // ══════════════════════════════════════════════════════════════════════════════
    {
        auto& physSys = VansEngine::VansPhysicsSystem::GetInstance();
        std::lock_guard<std::mutex> lock(physSys.GetSimulationMutex());

        // 4a. Ragdoll（依赖 animNode，必须在 PhysicsNode 之前）
        if (hasRagdoll && animNode)
            VansEngine::VansRagdollSystem::GetInstance().DestroyRagdoll(animNode);

        // 4b. Vehicle（场景级单例）
        if (vehicleNode) { delete vehicleNode; m_Vehicle = nullptr; }

        // 4c. CharacterController（先 Release PhysX controller，再 delete node）
        if (cctNode)
        {
            auto ci = std::find(m_CharControllerNodes.begin(),
                                m_CharControllerNodes.end(), cctNode);
            if (ci != m_CharControllerNodes.end())
            {
                cctNode->Release();
                delete cctNode;
                m_CharControllerNodes.erase(ci);
            }
        }

        // 4d. Cloth（Shutdown + delete + 清理平行 staging buffer）
        if (clothNode)
        {
            auto ci = std::find(m_ClothNodes.begin(), m_ClothNodes.end(), clothNode);
            if (ci != m_ClothNodes.end())
            {
                size_t idx = static_cast<size_t>(ci - m_ClothNodes.begin());
                clothNode->Shutdown();
                delete clothNode;
                m_ClothNodes.erase(ci);

                // m_ClothStagingBuffers 与 m_ClothNodes 平行索引
                if (idx < m_ClothStagingBuffers.size())
                {
                    VansVKDevice* vkDev = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
                    VkDevice natDev = vkDev ? vkDev->GetLogicDevice() : VK_NULL_HANDLE;
                    if (m_ClothStagingBuffers[idx].IsMapped())
                        m_ClothStagingBuffers[idx].Unmap();
                    if (natDev != VK_NULL_HANDLE)
                        m_ClothStagingBuffers[idx].DestroyVulkanBuffer(natDev);
                    m_ClothStagingBuffers.erase(m_ClothStagingBuffers.begin() + idx);
                }
            }
        }

        // 4e. Physics（析构函数自动从 PxScene remove actor）
        if (physicsNode)
        {
            auto pi = std::find(m_PhysicsNodes.begin(), m_PhysicsNodes.end(), physicsNode);
            if (pi != m_PhysicsNodes.end()) { delete physicsNode; m_PhysicsNodes.erase(pi); }
        }
    } // ─── 释放 SimulationMutex ────────────────────────────────────────

    // ══════════════════════════════════════════════════════════════════════════════
    //  4.5. MultiMeshGroup 清理（必须在 delete renderNode 之前）
    //
    //  当前 CreateEntity 仅支持单 Mesh 实体（v0.4.2 范围边界），
    //  但场景加载（ExpandMultiMeshToRenderNodes）会产生 multi-mesh 实体。
    //  若 DestroyEntity 作用于 multi-mesh 实体，必须清理 group 元数据
    //  和非首子节点（VansScriptRenderComponent 仅持有 childNodes[0]）。
    // ══════════════════════════════════════════════════════════════════════════════
    if (renderNode && !renderNode->m_ParentGroupName.empty())
    {
        auto groupIt = m_MultiMeshGroups.find(renderNode->m_ParentGroupName);
        if (groupIt != m_MultiMeshGroups.end())
        {
            const auto& group = groupIt->second;

            // 非首子节点：不在组件扫描范围内，需显式清理
            for (auto* childNode : group.childNodes)
            {
                if (childNode && childNode != renderNode)
                {
					ReleaseMainRenderProxyBinding(childNode, m_PendingRenderMutations);
                    if (childNode->m_TransfromIndex >= 0)
                    {
                        m_TransformSlotAllocator.FreeSlot(
                            static_cast<uint32_t>(childNode->m_TransfromIndex));
                        childNode->m_TransfromIndex = -1;
                    }
                    RemoveRenderNodeFromVector(childNode);
                    delete childNode;
                }
            }

            // Remove submesh lookup references owned by this multi-mesh group.
            if (group.sourceMesh)
                m_AssetRegistry.RemoveSceneSubMesh(group.sourceMesh);

            if (group.ownsSharedTransform)
                VansTransformStore::FreeTransform(group.sharedTransformID);

            m_MultiMeshGroups.erase(groupIt);
            RebuildAssetLookup();
        }
    }

    // ══════════════════════════════════════════════════════════════════════════════
    //  5. 清理 RenderNode（必须在 AnimationNode 之前）
    //
    //  核心顺序约束：
    //    VansCommonRenderNode 的 DescriptorSet (Set 3 Animation) 引用
    //    VansAnimationNode 管理的 GPU bone buffer。
    //    必须先 delete renderNode（释放 DescriptorSet / vkFreeDescriptorSets），
    //    再 delete animNode（销毁 GPU bone buffer），否则 Vulkan 验证层报错。
    // ══════════════════════════════════════════════════════════════════════════════
    if (renderNode)
    {
		ReleaseMainRenderProxyBinding(renderNode, m_PendingRenderMutations);
        // 5a. 回收 SSBO 槽位，置 -1 防止下一帧 UpdateModelData 悬垂写入
        if (renderNode->m_TransfromIndex >= 0)
        {
            m_TransformSlotAllocator.FreeSlot(
                static_cast<uint32_t>(renderNode->m_TransfromIndex));
            renderNode->m_TransfromIndex = -1;
        }

        // 5b. swap-pop 移出节点向量
        RemoveRenderNodeFromVector(renderNode);

        // 5c. delete：
        //   - 析构函数内部调用 DestroyDescriptorSets
        //   - RenderNode 自身拥有的 transform 由 RenderNode 析构释放
        delete renderNode;
        renderNode = nullptr;
    }

    // 5d. Particle RenderNode（独立列表，析构不由 VansScriptParticleComponent 管理）
    if (particleRN)
    {
        auto pi = std::find(m_ParticleRenderNodes.begin(),
                            m_ParticleRenderNodes.end(), particleRN);
        if (pi != m_ParticleRenderNodes.end()) m_ParticleRenderNodes.erase(pi);
        delete particleRN;
    }

    // ══════════════════════════════════════════════════════════════════════════════
    //  6. 清理 AnimationNode + AnimationController（在 RenderNode 之后）
    // ══════════════════════════════════════════════════════════════════════════════
    if (animNode)
    {
		m_EditorPreviewDrivenAnimationNodes.erase(animNode);
        // 6a. 先清理 AnimationController（由 animNode->GetController() 获取）
        VansAnimationController* ctrl = animNode->GetController();
        if (ctrl)
        {
            auto ci = std::find(m_AnimationControllers.begin(),
                                m_AnimationControllers.end(), ctrl);
            if (ci != m_AnimationControllers.end())
            {
                delete *ci;
                m_AnimationControllers.erase(ci);
            }
        }

        // 6b. 删除 AnimationNode（析构释放 GPU bone buffer）
        auto ai = std::find(m_AnimationNodes.begin(), m_AnimationNodes.end(), animNode);
        if (ai != m_AnimationNodes.end()) { delete animNode; m_AnimationNodes.erase(ai); }
    }

    // ══════════════════════════════════════════════════════════════════════════════
    //  7. 清理 Light Components（swap-pop + 更新 LightIndex 引用）
    //
    //  LightManager 无独立 Remove API，通过 swap-pop 维护向量紧凑性。
    //  swap-pop 后被移入位置的那盏灯 oldIndex 变成 newIndex。
    //  需遍历所有 VansScriptObject 更新对应 m_LightIndex 字段。
    // ══════════════════════════════════════════════════════════════════════════════
    auto removeDirectionalLightByIndex = [&](int index)
    {
		auto& lightVec = m_LightManager.GetDirectionLights();
        if (index < 0 || index >= static_cast<int>(lightVec.size())) return;
        int last = static_cast<int>(lightVec.size()) - 1;
        if (index != last)
        {
            std::swap(lightVec[index], lightVec[last]);
			UpdateLightComponentIndex(last, index, VansLightType::DIRECTIONAL);
        }
        lightVec.pop_back();
    };

    if (dlightIdx >= 0)
		removeDirectionalLightByIndex(dlightIdx);
    if (plightIdx >= 0)
	{
		const int last = static_cast<int>(m_LightManager.GetPointLights().size()) - 1;
		if (plightIdx != last) UpdateLightComponentIndex(last, plightIdx, VansLightType::POINT);
		m_LightManager.RemovePointLight(static_cast<uint32_t>(plightIdx));
	}
    if (slightIdx >= 0)
	{
		const int last = static_cast<int>(m_LightManager.GetSpotLight().size()) - 1;
		if (slightIdx != last) UpdateLightComponentIndex(last, slightIdx, VansLightType::SPOT);
		m_LightManager.RemoveSpotLight(static_cast<uint32_t>(slightIdx));
	}
    if (rlightIdx >= 0)
    {
        // RectLight 额外清除发光纹理。
        auto& rects = m_LightManager.GetRectLights();
	        if (rlightIdx < static_cast<int>(rects.size())
	            && rects[rlightIdx].m_TextureSlot >= 0.0f)
        {
            if (VansTexture* arr = m_MaterialManager.GetRuntimeRenderTexture(
                    VansMaterialManager::RT_RECT_LIGHT_EMISSIVE))
            {
                VansVKDevice* vkDev = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
                if (vkDev)
                {
                    static const uint8_t black[4] = {0, 0, 0, 0};
                    arr->UpdateArrayLayerFromPixels(
                        vkDev->GetCommandBuffer(), black, 1, 1, rlightIdx);
                }
            }
        }
		const int last = static_cast<int>(rects.size()) - 1;
		if (rlightIdx != last) UpdateLightComponentIndex(last, rlightIdx, VansLightType::RECT);
		m_LightManager.RemoveRectLight(static_cast<uint32_t>(rlightIdx));
    }

    // ══════════════════════════════════════════════════════════════════════════════
    //  8. 回收 Transform Store ID（仅限无 RenderNode 的纯物理/相机实体）
    //
    //  当存在 RenderNode 时（m_OwnsTransform==false on obj），
    //  renderNode->m_OwnsTransform==true，其析构函数（Step 5c）已调用
    //  VansTransformStore::FreeTransform(m_TransformID)。外部不可重复调用。
    //
    //  当实体无 RenderNode（纯物理实体，obj 原本拥有 transform）时，
    //  DestroyEntity 已在 delete obj 前通过 ReleaseOwnedTransform() 接管所有权，
    //  因此此处是唯一释放点。
    // ══════════════════════════════════════════════════════════════════════════════
    if (ownsTransform && renderNode == nullptr)
        VansTransformStore::FreeTransform(transformID);

    VANS_LOG("[Scene] DestroyEntity: '" << name
        << "' active=" << m_TransformSlotAllocator.GetActiveCount());
    return true;
}


