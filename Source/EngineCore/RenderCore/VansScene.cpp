#include "VansScene.h"
#include "../RuntimeCore/VansFramePhase.h"
#include "../RuntimeCore/VansThreadContract.h"
#include "VansShaderManager.h"
#include "BRDFData/VansLight.h"
#include "../Configration/VansConfigration.h"
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
#include "VegetationCore/VansVegetationSystem.h"
#include "VansParticleRenderNode.h"
#include "../ParticleCore/VansParticleManager.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../AnimationCore/VansBoneAttachmentSystem.h"
#include "../AnimationCore/VansSkinnedMeshLoader.h"
#include "../ScriptCore/VansScriptContext.h"
#include "../VansTimer.h"

#include "../Interfaces/IShaderHotReloadService.h"
#include "../AssetCore/VansAssetGuid.h"
#include "../Util/VansLog.h"
#include "../Util/VansProfiler.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <filesystem>

#ifdef _DEBUG
#define VANS_UNLOAD_STEP(index, reason) VANS_LOG("[VansScene][UnLoadScene Step " << index << "] " << reason)
#else
#define VANS_UNLOAD_STEP(index, reason) do { (void)sizeof(index); } while (0)
#endif

namespace
{
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

	void RegisterAssetByName(
		std::unordered_map<std::string, VansGraphics::VansAsset*>& lookup,
		VansGraphics::VansAsset* asset)
	{
		if (asset && !asset->m_AssetName.empty())
			lookup[asset->m_AssetName] = asset;
	}
}


VansGraphics::VansScene::~VansScene()
{
    if (m_SceneState != VansSceneState::Empty || !m_SceneObjects.empty())
    {
        VANS_LOG_WARN("[VansScene] Scene is still loaded during destruction; call UnLoadScene() before delete");
    }
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
	const std::vector<Vans::VansSceneAudioResourceRequest>& audios,
	const std::string& assetPrefix)
{
	m_AudioManager.Load(audios, assetPrefix);
}

void VansGraphics::VansScene::LoadProjectVideoResources(
	const std::vector<Vans::VansSceneVideoResourceRequest>& videos,
	const std::string& assetPrefix)
{
	m_VideoManager.Load(videos, assetPrefix, m_RuntimeResourceDevice);
}

void VansGraphics::VansScene::LoadProjectAudioResourcesFromJson(
	const json& audioData,
	const std::string& assetPrefix)
{
	m_AudioManager.LoadFromJson(audioData, assetPrefix);
}

void VansGraphics::VansScene::LoadProjectVideoResourcesFromJson(
	const json& videoData,
	const std::string& assetPrefix)
{
	m_VideoManager.LoadFromJson(videoData, assetPrefix, m_RuntimeResourceDevice);
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
    //灏唕endernode璁板綍鍒板搴旂被鍨嬬殑vector涓?
    switch (type)
    {
	case SKY_BOX_NODE:
		m_SkyBoxNode = renderNode;
		break;
    case DEFERRED_NODE:
        m_DeferredNode = renderNode;
        break;
	case OPAQUE_NODE:
		m_OpaqueRenderNodes.push_back(renderNode);
		break;
	case HAIR_NODE:
		m_HairRenderNodes.push_back(renderNode);
		break;
	case FORWARD_OPAQUE_AFTER_DEFERRED_NODE:
		m_ForwardOpaqueAfterDeferredRenderNodes.push_back(renderNode);
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
    //閬嶅巻鎵€鏈夌殑node鐢熸垚set
    if (m_SkyBoxNode != nullptr)
    {
        m_SkyBoxNode->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);
    }
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
	for (auto node : m_ForwardOpaqueAfterDeferredRenderNodes)
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

    // 绮掑瓙娓叉煋鑺傜偣锛氫笉渚濊禆 VansMaterial锛岀嫭绔嬭缃弿杩扮
    // 浣跨敤鍏ㄥ眬闆嗭紙Set 0锛夎闂?Camera UBO锛孲et 1 缁戝畾绮掑瓙绾圭悊锛堟澶勪娇鐢?defaultAlbedo 鍗犱綅锛?
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
// Global Descriptor Set (Set 0) 鈥?shared across all render nodes
// Contains: Camera, Lights, Materials, IBL, Bindless textures
// ============================================================
void VansGraphics::VansScene::CreateGlobalDescriptorSet(VkDevice device)
{
    auto descManager = VansVKDescriptorManager::GetInstance();

    // Create global layout + set via factory
    std::vector<VkDescriptorSet> sets;
    VansDescriptorSetLayoutFactory::CreateAndAllocate_Global(m_GlobalDescriptorSetLayout, sets);
    m_GlobalDescriptorSet = sets[0];

    // Create object layout + set (Set 2: Transform SSBO only 鈥?shared by all geometry nodes)
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

    // 鈹€鈹€ Create shared dummy animation buffers and Set 3 for static nodes 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    // Animated VansCommonRenderNodes allocate their own per-node Set 3 with real buffers.
    // Static nodes bind this shared dummy set (never read since animationEnabled==0).
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
    VansDescriptorSetLayoutFactory::CreateAndAllocate_Animation(m_AnimationDescriptorSetLayout, animSets);
    m_AnimationDescriptorSet = animSets[0];

    descManager->BeginDescriptorUpdate();
    // binding 0: Dummy Bone ID SSBO (per-submesh bone IDs)
    descManager->WriteBufferDescriptor(
        m_AnimationDescriptorSet,
        ANIMATION_BINDING_BONEID_SSBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ m_DummyBoneIDBuffer.GetNativeBuffer(), 0, 64 }});
    // binding 1: Dummy Bone Matrices SSBO
    descManager->WriteBufferDescriptor(
        m_AnimationDescriptorSet,
        ANIMATION_BINDING_BONE_SSBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ m_DummyBoneBuffer.GetNativeBuffer(), 0, 64 }});
    // binding 2: Dummy Bone Weight SSBO (per-submesh bone weights)
    descManager->WriteBufferDescriptor(
        m_AnimationDescriptorSet,
        ANIMATION_BINDING_BONEWEIGHT_SSBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ m_DummyWeightBuffer.GetNativeBuffer(), 0, 64 }});
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
    auto descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();

    // Binding 0: Camera UBO
    descManager->WriteBufferDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_CAMERA_UBO,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        {{
            m_Camera->m_CameraDataBuffer.GetNativeBuffer(),
            0,
            m_Camera->m_CameraDataBuffer.GetBufferSize()
        }});

    // Binding 1: Lights SSBO
    descManager->WriteBufferDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_LIGHTS_UBO,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_LightManager.GetLightBuffer().GetNativeBuffer(),
            0,
            m_LightManager.GetLightBuffer().GetBufferSize()
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

    // Binding 4: Pre-convolved diffuse environment
    descManager->WriteImageDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_PRECONV_DIFFUSE,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            m_MaterialManager.m_PreConvDiffuse->GetImage().GetSampler(),
            m_MaterialManager.m_PreConvDiffuse->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }});

    // Binding 5: Pre-convolved specular environment
    descManager->WriteImageDescriptor(
        m_GlobalDescriptorSet,
        GLOBAL_BINDING_PRECONV_SPECULAR,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            m_MaterialManager.m_PreConvSpecular->GetImage().GetSampler(),
            m_MaterialManager.m_PreConvSpecular->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }});

    // Binding 6: SH coefficients buffer
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

void VansGraphics::VansScene::DeferInitialReflectionProbeBake()
{
    m_ReflectionProbeSystem.DeferInitialBakeForGI(
        m_GISettings.spatialUpdateDivisor,
        m_GISettings.directionUpdateSlices);
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
	for (auto* node : m_ForwardOpaqueAfterDeferredRenderNodes)
		if (node && node->m_NodeName == name) return node;
    for (auto* node : m_TransParentRenderNodes)
        if (node && node->m_NodeName == name) return node;
    if (m_SkyBoxNode && m_SkyBoxNode->m_NodeName == name) return m_SkyBoxNode;
    if (m_TerrainRenderNode && m_TerrainRenderNode->m_NodeName == name) return m_TerrainRenderNode;
    if (m_VegetationRenderNode && m_VegetationRenderNode->m_NodeName == name) return m_VegetationRenderNode;
    return nullptr;
}

void VansGraphics::VansScene::UnLoadScene()
{
    VANS_ASSERT_MAIN_THREAD();

	VANS_LOG("[VansScene] UnLoadScene started");

	VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	VkDevice nativeDevice = vkDevice ? vkDevice->GetLogicDevice() : VK_NULL_HANDLE;
	if (nativeDevice != VK_NULL_HANDLE)
		m_ReflectionProbeSystem.Clear(nativeDevice);

	// 鈹€鈹€ 0. 娓呴櫎缂栬緫鍣ㄩ€変腑鐘舵€?鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VANS_UNLOAD_STEP(0, "Clear editor selection state");
	VANS_LOG("[VansScene] Step 0: editor selection cleared");

	// 鈹€鈹€ 1. 娓呯悊鍦烘櫙绾ц繍琛屾椂绾圭悊锛圫H 绯绘暟 + GI Visibility锛夛紝淇濈暀灞忓箷绌洪棿绾圭悊 鈹€鈹€
	//  SSGI / SSAO / HZB / SSR / Fog 绛夊睆骞曠┖闂寸汗鐞嗗湪 PrepareRenderingData()
	//  鏃跺垱寤猴紝涓嶄緷璧栧満鏅唴瀹癸紝鏃犻渶鍦ㄥ満鏅垏鎹㈡椂閿€姣併€?
	//  SH 绾圭悊鐢?RuntimeRenderTextureManager 鎷ユ湁锛屼娇鐢?Remove锛堜細 delete锛夈€?
    VANS_UNLOAD_STEP(1, "娓呯悊鍦烘櫙绾ц繍琛屾椂绾圭悊");
	m_MaterialManager.RemoveRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT);
	m_MaterialManager.RemoveRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT);
	m_MaterialManager.RemoveRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT);
	m_MaterialManager.RemoveRuntimeRenderTexture(VansMaterialManager::RT_GI_VISIBILITY_ATLAS);
	m_MaterialManager.m_SSGITemporalFrame = 0;
	m_MaterialManager.m_FogTemporalFrame  = 0;
	m_MaterialManager.m_FogHistoryValid   = false;
	// SH 绾圭悊宸茬Щ闄わ紝鏍囪娓叉煋 Feature 鐨?descriptor set 闇€瑕侀噸鏂板啓鍏?
	if (vkDevice)
	{
		vkDevice->ResetFeatureDescriptorSets();
	}
	VANS_LOG("[VansScene] Step 1: scene runtime textures cleared; screen-space textures retained");

	// 鈹€鈹€ 2. 娓呯悊鑴氭湰瀵硅薄锛堜粎閲婃斁 wrapper锛屼笉閲婃斁搴曞眰 Node锛?鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
	// 鍏?Teardown 鎵€鏈?VanPyScriptComponent锛屽畨鍏ㄩ噴鏀?py::object锛?
	// 鍐嶅垹闄?VansScriptObject锛堟鏃?m_PyInstance 宸蹭负 py::none()锛夈€?
    VANS_UNLOAD_STEP(2, "Clear script objects and script modules");
	for (auto* obj : m_SceneObjects)
	{
		if (!obj) continue;
		for (auto* comp : obj->m_Components)
		{
			auto* pyComp = dynamic_cast<VanPyScriptComponent*>(comp);
			if (pyComp) pyComp->Teardown();

            auto* particleComp = dynamic_cast<VansScriptParticleComponent*>(comp);
            if (particleComp && particleComp->m_Runtime)
            {
                VansParticleManager::Instance().UnregisterRuntime(particleComp->m_Runtime.get());
            }
		}
	}
	VANS_LOG("[VansScene] Step 2a: 鑴氭湰缁勪欢宸?Teardown");
    VansParticleManager::Instance().Shutdown();

	// ScriptContext 涓殑 tracked modules 涔熶竴骞舵竻鐞?
	if (VansScriptContext::GetInstance())
	{
		VansScriptContext::GetInstance()->ClearTrackedModules();
	}
	for (auto* obj : m_SceneObjects)
	{
		delete obj;
	}
	m_SceneObjects.clear();
    VANS_LOG("[VansScene] Step 2b: SceneObjects released");

	// 鈹€鈹€ 3-5. 娓呯悊鐗╃悊鑺傜偣 / 杞藉叿 / 甯冩枡锛堥渶瑕佹寔鏈夌墿鐞嗙嚎绋嬮攣锛?鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
	// 鐗╃悊妯℃嫙鍦ㄧ嫭绔嬬嚎绋嬭繍琛岋紝蹇呴』鍏堣幏鍙?SimulationMutex 鍐嶆搷浣?PxScene銆?
    VANS_UNLOAD_STEP("3-5", "Clear physics, vehicle, cloth and character controller nodes");
	{
		auto& physicsSystem = VansEngine::VansPhysicsSystem::GetInstance();
		std::lock_guard<std::mutex> simLock(physicsSystem.GetSimulationMutex());

        // 鈹€鈹€ 2c. 娓呯悊甯冨▋濞冪郴缁燂紙鐩存帴鎸佹湁 PxD6Joint / PxRigidDynamic锛夆攢鈹€鈹€鈹€鈹€鈹€
        VansEngine::VansRagdollSystem::GetInstance().Shutdown();
        VANS_LOG("[VansScene] Step 2c: Ragdoll 绯荤粺宸叉竻鐞?(鎸侀攣)");

		// 鈹€鈹€ 3. 娓呯悊鐗╃悊鑺傜偣锛堟瀽鏋勫嚱鏁颁細浠?PxScene 绉婚櫎 actor锛?鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
		for (auto* physicsNode : m_PhysicsNodes)
		{
			if (physicsNode)
			{
				delete physicsNode;
			}
		}
		m_PhysicsNodes.clear();
		VANS_LOG("[VansScene] Step 3: 鐗╃悊鑺傜偣宸叉竻鐞?(鎸侀攣)");

        // 鈹€鈹€ 3b. 娓呯悊鍦板舰楂樺害鍦虹鎾?鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        if (m_TerrainPhysicsNode)
        {
            delete m_TerrainPhysicsNode;
            m_TerrainPhysicsNode = nullptr;
        }
        VANS_LOG("[VansScene] Step 3b: terrain physics nodes cleared under lock");

		// 鈹€鈹€ 4. 娓呯悊杞藉叿 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
		if (m_Vehicle)
		{
			delete m_Vehicle;
			m_Vehicle = nullptr;
		}
            VANS_LOG("[VansScene] Step 4: vehicles cleared");

		// 鈹€鈹€ 5. 娓呯悊甯冩枡鑺傜偣鍜?staging buffer 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
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

		// 鈹€鈹€ 5b. 娓呯悊瑙掕壊鎺у埗鍣ㄨ妭鐐?鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
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
	} // 閲婃斁 SimulationMutex

    VANS_UNLOAD_STEP("5b", "娓呯悊甯冩枡 staging buffer");
	for (auto& stagingBuf : m_ClothStagingBuffers)
	{
		if (stagingBuf.IsMapped())
			stagingBuf.Unmap();
		stagingBuf.DestroyVulkanBuffer(nativeDevice);
	}
	m_ClothStagingBuffers.clear();
        VANS_LOG("[VansScene] Step 5b: cloth staging buffers cleared");

	// 鈹€鈹€ 6. 娓呯悊 transform 鐖跺瓙绯荤粺 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VANS_UNLOAD_STEP(6, "娓呯悊 transform 鐖跺瓙绯荤粺");
	m_TransformParentSystem.Clear();
        VANS_LOG("[VansScene] Step 6: transform parent system cleared");

	// 鈹€鈹€ 7. 娓呯悊妞嶈绯荤粺 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VANS_UNLOAD_STEP(7, "娓呯悊妞嶈绯荤粺");
	if (m_VegetationSystem)
	{
		m_VegetationSystem->Cleanup(nativeDevice);
		delete m_VegetationSystem;
		m_VegetationSystem = nullptr;
	}
        VANS_LOG("[VansScene] Step 7: vegetation system cleared");

	// 鈹€鈹€ 8. 娓呯悊鎵€鏈夋覆鏌撹妭鐐癸紙蹇呴』鍦ㄥ姩鐢昏妭鐐逛箣鍓嶏紝鍥犱负娓叉煋鑺傜偣鐨?descriptor
	//       set 寮曠敤浜嗗姩鐢昏妭鐐圭殑 bone buffer锛岄渶鍦?buffer 閿€姣佸墠閲婃斁 set锛?
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

	for (auto* node : m_ForwardOpaqueAfterDeferredRenderNodes)
		deleteRenderNode(node);
	m_ForwardOpaqueAfterDeferredRenderNodes.clear();

	for (auto* node : m_TransParentRenderNodes)
		deleteRenderNode(node);
	m_TransParentRenderNodes.clear();

	for (auto* node : m_PostProcessRenderNodes)
		deleteRenderNode(node);
	m_PostProcessRenderNodes.clear();

	for (auto* node : m_ScreenSpaceRenderNodes)
		deleteRenderNode(node);
	m_ScreenSpaceRenderNodes.clear();

	// 璐磋姳鑺傜偣娓呯悊
	for (auto* node : m_DecalRenderNodes)
		deleteRenderNode(node);
	m_DecalRenderNodes.clear();

	// 绮掑瓙娓叉煋鑺傜偣娓呯悊
	for (auto* node : m_ParticleRenderNodes)
		deleteRenderNode(node);
	m_ParticleRenderNodes.clear();

	deleteRenderNode(m_SkyBoxNode);
	m_SkyBoxNode = nullptr;

	deleteRenderNode(m_DeferredNode);
	m_DeferredNode = nullptr;

	deleteRenderNode(m_TerrainRenderNode);
	m_TerrainRenderNode = nullptr;

	deleteRenderNode(m_WaterRenderNode);
	m_WaterRenderNode = nullptr;
	m_WaterMaterial   = nullptr; // Material ownership is released with the scene material registry.

	// 閲婃斁姘撮潰绯荤粺锛圴ansWaterSystem 绠＄悊 Wave/GBuf/Compute/Composite GPU 璧勬簮锛?
	if (m_WaterSystem)
	{
		m_WaterSystem->Shutdown();
		delete m_WaterSystem;
		m_WaterSystem = nullptr;
	}
	m_HasWater = false;

	// VegetationRenderNode 鏈鍒楄〃鎸佹湁锛岄渶鍗曠嫭 delete
	deleteRenderNode(m_VegetationRenderNode);
	m_VegetationRenderNode = nullptr;
        VANS_LOG("[VansScene] Step 8: render nodes cleared");

	// 鈹€鈹€ 9. 娓呯悊鍔ㄧ敾鑺傜偣锛堟瀽鏋勫嚱鏁颁細閿€姣?GPU bone buffer锛?鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VANS_UNLOAD_STEP(9, "娓呯悊鍔ㄧ敾鑺傜偣");
	for (auto* animNode : m_AnimationNodes)
	{
		if (animNode)
		{
			delete animNode;
		}
	}
	m_AnimationNodes.clear();
        VANS_LOG("[VansScene] Step 9: animation nodes cleared");

	// 鈹€鈹€ 9b. 娓呯悊鍔ㄧ敾鎺у埗鍣紙Controller 鐢?Scene 鎸佹湁锛孨ode 鍙瓨瑁告寚閽堬級 鈹€鈹€鈹€
    VANS_UNLOAD_STEP("9b", "Clear animation controllers");
	for (auto* ctrl : m_AnimationControllers)
	{
		delete ctrl;
	}
	m_AnimationControllers.clear();
	VANS_LOG("[VansScene] Step 9b: 鍔ㄧ敾鎺у埗鍣ㄥ凡娓呯悊");

    // 鈹€鈹€ 9c. 娓呯悊楠ㄩ纰版挒浣撻檮鐫€鐐圭郴缁?鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VansEngine::VansBoneAttachmentSystem::GetInstance().Shutdown();
    VANS_LOG("[VansScene] Step 9c: 楠ㄩ纰版挒浣撻檮鐫€鐐圭郴缁熷凡娓呯悊");

	// 鈹€鈹€ 10. 娓呯悊 Multi-mesh 鍒嗙粍 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VANS_UNLOAD_STEP(10, "娓呯悊 Multi-mesh 鍒嗙粍鍜屽瓙缃戞牸鏌ユ壘鏉＄洰");
	VANS_LOG("[VansScene] Step 10: clearing multi-mesh groups (count=" << m_MultiMeshGroups.size() << ")");
	m_MultiMeshGroups.clear();

    // 瀛愮綉鏍煎璞℃湰韬敱鐖剁骇 multi-mesh 鐨?m_SubMeshes 鎷ユ湁锛屾澶勪粎娓呴櫎闈炴嫢鏈夋煡鎵惧垪琛紝
    // 闃叉涓嬫 ExpandMultiMeshToRenderNodes 鏃朵骇鐢熼噸澶嶃€?
    m_AssetRegistry.ClearSceneSubMeshes();

        VANS_LOG("[VansScene] Step 10: multi-mesh groups cleared");

	// 鈹€鈹€ 11. 娓呯悊鏉愯川锛堝満鏅骇锛屾寚閽堢敱 Scene 鎷ユ湁锛?鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
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

	// 鈹€鈹€ 12. 娓呯悊鍏ㄥ眬 PBR 鏁版嵁鍜?descriptor 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VANS_UNLOAD_STEP(12, "娓呯悊鍏ㄥ眬 PBR 鏁版嵁鍜?descriptor");
	m_MaterialManager.ClearScenePBRData(nativeDevice);

	// 鈹€鈹€ 13. 娓呯悊鐏厜 CPU 鏁版嵁鍜?GPU 璧勬簮 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VANS_UNLOAD_STEP(13, "娓呯悊鐏厜 CPU 鏁版嵁鍜?GPU 璧勬簮");
	m_LightManager.ClearLights();
	m_LightManager.DestroyGPUResources(nativeDevice);

	// IES profile GPU 绾圭悊鏁扮粍锛坰ampler2DArray锛宐inding=16锛?
	m_IESProfileManager.DestroyGPUResources(nativeDevice);
        VANS_LOG("[VansScene] Step 12-13: PBR and light GPU resources cleared");

	// 鈹€鈹€ 14. 娓呯悊 Ray Tracing TLAS 璧勬簮 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VANS_UNLOAD_STEP(14, "娓呯悊 Ray Tracing TLAS/BLAS 鍦烘櫙璧勬簮");
	if (vkDevice)
	{
		vkDevice->GetRayTracingContext().CleanupSceneResources(nativeDevice);
	}

	// 娓呯悊 Scene 鎸佹湁鐨?TLAS 鏁版嵁
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

	// BLAS vertex/index data锛堢紦瀛樼殑寮曠敤锛屼笉閿€姣佸疄闄呯殑 mesh buffer锛?
	m_BLASVertexData.clear();
	m_BLASIndexData.clear();
	m_TLASInstaneData.clear();
	m_TlasInstanceTextureIndex.clear();
	m_TlasInstanceTextures.clear();
	m_TlasInstanceMaterialToIndex.clear();

	// 閲婃斁椤圭洰绾?mesh 涓婃畫鐣欑殑 BLAS 鍔犻€熺粨鏋勶紝闃叉浜屾 BuildBLAS 鏃惰祫婧愭硠婕?
	for (const auto& meshAsset : m_AssetRegistry.GetMeshes())
	{
		VansMesh* mesh = static_cast<VansMesh*>(meshAsset);
		if (mesh->m_SupportRayTracing)
		{
			mesh->DestroyBLAS(*vkDevice);
		}
	}
        VANS_LOG("[VansScene] Step 14: RT/TLAS resources cleared");

	// 鈹€鈹€ 15. 娓呯悊 Instance Transform Buffer 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VANS_UNLOAD_STEP(15, "娓呯悊 Instance Transform Buffer 涓?descriptor");
	m_InstanceTransformDataBuffer.DestroyVulkanBuffer(nativeDevice);
	m_InstanceTransformData.clear();

	// 鈹€鈹€ 閲嶇疆 Transform Slot Allocator锛堝繀椤诲湪 DestroyVulkanBuffer 涔嬪悗銆佷笅娆?Prepare 涔嬪墠锛夆攢鈹€
	m_TransformSlotAllocator.Reset();

	// 閲婃斁 Transform Data descriptor set 鍜?layout
	auto descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_GlobalTransformDataDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GlobalTransformDataSetLayout);

	// 鈹€鈹€ 16. 娓呯悊 Global / Object / Animation / Empty Descriptor Sets 鈹€鈹€鈹€鈹€鈹€
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

	if (m_AnimationDescriptorSet != VK_NULL_HANDLE)
	{
		std::vector<VkDescriptorSet> tmp = { m_AnimationDescriptorSet };
		descMgr->DestroyDescriptorSet(tmp);
		m_AnimationDescriptorSet = VK_NULL_HANDLE;
	}
	descMgr->DestroyDescriptorSetLayout(m_AnimationDescriptorSetLayout);

	if (m_EmptyPassDescriptorSet != VK_NULL_HANDLE)
	{
		std::vector<VkDescriptorSet> tmp = { m_EmptyPassDescriptorSet };
		descMgr->DestroyDescriptorSet(tmp);
		m_EmptyPassDescriptorSet = VK_NULL_HANDLE;
	}
	descMgr->DestroyDescriptorSetLayout(m_EmptyPassLayout);

	// 鈹€鈹€ 17. 娓呯悊 Dummy Bone Buffer 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VANS_UNLOAD_STEP(17, "娓呯悊 Dummy Bone Buffer");
	m_DummyBoneIDBuffer.DestroyVulkanBuffer(nativeDevice);
	m_DummyBoneBuffer.DestroyVulkanBuffer(nativeDevice);
	m_DummyWeightBuffer.DestroyVulkanBuffer(nativeDevice);

	// 鈹€鈹€ 18. 鏆傚仠瑙嗛鎾斁锛堣棰戜负椤圭洰绾ц祫婧愶紝GPU 绾圭悊淇濈暀锛屽垏鎹㈠満鏅?Play 鏃跺鐢級鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VANS_UNLOAD_STEP(18, "Pause project videos");
	m_VideoManager.PauseAll();

	// 鈹€鈹€ 19. 鍋滄鎵€鏈夐煶棰戞挱鏀撅紙闊抽涓洪」鐩骇璧勬簮锛屼笉閲婃斁宸茶В鐮佹暟鎹級鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VANS_UNLOAD_STEP(19, "Stop project audio");
	m_AudioManager.StopAll();
    m_SceneState = VansSceneState::Empty;

	VANS_LOG("[VansScene] Scene unloaded");
}

void VansGraphics::VansScene::UnloadProjectResources(VansVKDevice* device)
{
    VANS_ASSERT_MAIN_THREAD();

    if (device)
    {
        device->WaitForDevice();
    }

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
    VANS_LOG("[VansScene] Project resources unloaded");
}

void VansGraphics::VansScene::UpdateSceneData()
{
    VANS_ASSERT_FRAME_PHASE(VansFramePhase::RenderPrep);

    VANS_PROFILE_SCOPE("Scene::UpdateSceneData", Vans::ProfileCategory::RenderPrepare);

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkDevice nativeDevice = vkDevice ? vkDevice->GetLogicDevice() : VK_NULL_HANDLE;

    // Per-frame skeletal animation update + GPU bone matrix upload
    // Use the cached frame delta so all per-frame systems observe the same timestep.
    {
        VANS_PROFILE_SCOPE("Animation::UpdateAll", Vans::ProfileCategory::Animation);
        UpdateAnimations(static_cast<float>(VansTimer::GetLastFrameDelta()));
    }

    // 楠ㄩ纰版挒浣撻檮鐫€鐐瑰繀椤荤揣璺熷姩鐢绘洿鏂帮紝纭繚 TransformStore 璇诲彇褰撳墠甯ч楠煎Э鎬併€?
    {
        VANS_PROFILE_SCOPE("BoneAttachment::SyncAll", Vans::ProfileCategory::Physics);
        VansEngine::VansBoneAttachmentSystem::GetInstance().Update();
    }

    // Cloth 渚濊禆 render node 鐨勫綋鍓嶄笘鐣屽彉鎹㈡潵鍚屾鍥哄畾鐐广€?
    // 蹇呴』鍦ㄥ竷鏂欐ā鎷熶箣鍓嶈В鏋愮埗瀛愬叧绯伙紝鍚﹀垯鎸傚湪瑙掕壊楠ㄩ/鐖惰妭鐐逛笅鐨勫竷鏂欎細鐢ㄤ笂涓€甯ф垨鏈В鏋愮殑鍙樻崲妯℃嫙锛?
    // 闅忓悗鍙堜互鏂板彉鎹㈡覆鏌擄紝瀵艰嚧浣嶇疆鏄庢樉閿欎綅銆?
    {
        VANS_PROFILE_SCOPE("Transform::ResolveParentChild", Vans::ProfileCategory::RenderPrepare);
        m_TransformParentSystem.ResolveParentChildTransforms();
    }

    // Sync light components after transform parenting is resolved, then rebuild shadow matrices.
    {
        VANS_PROFILE_SCOPE("Light::SyncTransforms", Vans::ProfileCategory::RenderPrepare);
        SyncLightTransforms();
    }
    {
        VANS_PROFILE_SCOPE("Light::UpdateShadowMatrices", Vans::ProfileCategory::RenderPrepare);
        VansCascadeCameraData shadowCamera = {};
        shadowCamera.position = glm::vec3(m_Camera->GetPosition());
        shadowCamera.forward = glm::normalize(glm::vec3(m_Camera->GetForward()));
        shadowCamera.up = glm::normalize(glm::vec3(m_Camera->GetUp()));
        shadowCamera.verticalFovRadians = glm::radians(m_Camera->GetFov());
        shadowCamera.aspectRatio = m_Camera->GetAspectRatio();
        shadowCamera.nearPlane = m_Camera->GetNearClip();
        shadowCamera.farPlane = m_Camera->GetFarClip();
        m_LightManager.UpdateLightShadowMatrixData(shadowCamera);
    }
    {
        VANS_PROFILE_SCOPE("Light::UpdateCPUData", Vans::ProfileCategory::RenderPrepare);
        m_LightManager.UpdateLightCPUData();
    }

    // Advance cloth simulation and write results to staging buffers
    {
        VANS_PROFILE_SCOPE("Cloth::Simulate", Vans::ProfileCategory::Physics);
        UpdateClothSimulation(0.03f);
    }
    {
        VANS_PROFILE_SCOPE("Cloth::WriteResultsToStaging", Vans::ProfileCategory::Physics);
        WriteClothResultsToStagingBuffers();
    }

    // 鎺ㄨ繘鎵€鏈夎棰戠汗鐞嗙殑鎾斁锛屼笂浼犲氨缁抚鍒?GPU锛堝湪 Vulkan 鍛戒护褰曞埗涔嬪墠鎵ц锛?
    {
        VANS_PROFILE_SCOPE("Video::TickAll", Vans::ProfileCategory::Video);
        m_VideoManager.TickAll(VansTimer::GetLastFrameDelta());
    }

    // 鎺ㄨ繘鎵€鏈夐煶棰戣妭鐐癸細鏇存柊 Listener 浣嶇疆銆侀┍鍔?Streaming 鑺傜偣琛ュ厖 Buffer
    {
        VANS_PROFILE_SCOPE("Audio::TickAll", Vans::ProfileCategory::Audio);
        glm::vec4 camPos = m_Camera->GetPosition();
        glm::vec4 camFwd = m_Camera->GetForward();
        glm::vec4 camUp  = m_Camera->GetUp();
        m_AudioManager.TickAll(
            VansTimer::GetLastFrameDelta(),
            camPos.x, camPos.y, camPos.z,
            camFwd.x, camFwd.y, camFwd.z,
            camUp.x,  camUp.y,  camUp.z);
    }

    // 绮掑瓙绯荤粺锛氬悓姝ュ璞?Transform锛屾帹杩涘悗鍙拌繍琛屾椂锛屽苟涓婁紶鏈抚瀹炰緥鏁版嵁銆?
    if (!m_ParticleRenderNodes.empty())
    {
        const float deltaTime = static_cast<float>(VansTimer::GetLastFrameDelta());
        {
            VANS_PROFILE_SCOPE("Particle::PrepareLocalToWorld", Vans::ProfileCategory::Particles);
            for (auto* obj : m_SceneObjects)
            {
                if (!obj || obj->m_TransformID == 0) continue;

                auto* particleComp = obj->GetComponent<VansScriptParticleComponent>();
                if (!particleComp || !particleComp->m_Runtime) continue;

                if (particleComp->m_HasWorldPositionOverride)
                {
                    glm::mat4x4 overrideMatrix(1.f);
                    overrideMatrix[3] = glm::vec4(particleComp->m_WorldPositionOverride, 1.f);
                    particleComp->m_Runtime->m_LocalToWorld = overrideMatrix;
                }
                else
                {
                    auto& t = VansTransformStore::GetTransform(obj->m_TransformID);
                    particleComp->m_Runtime->m_LocalToWorld = t.GetModelMatrix();
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

        if (nativeDevice != VK_NULL_HANDLE)
        {
            VANS_PROFILE_SCOPE("Particle::UploadInstanceBuffers", Vans::ProfileCategory::Particles);
            const glm::mat4 particleViewMatrix = m_Camera ? m_Camera->GetViewMatrix() : glm::mat4(1.0f);
            for (auto* obj : m_SceneObjects)
            {
                if (!obj) continue;

                auto* particleComp = obj->GetComponent<VansScriptParticleComponent>();
                if (!particleComp || !particleComp->m_Runtime || !particleComp->m_RenderNode) continue;

                particleComp->m_PlayTime  = particleComp->m_Runtime->m_PlayTime;
                particleComp->m_IsPlaying = particleComp->m_Runtime->m_IsPlaying;

                particleComp->m_RenderNode->UpdateInstanceBuffer(
                    nativeDevice,
                    particleComp->m_Runtime->GetRenderBuffer(),
                    particleViewMatrix);
            }
        }
    }

    // 鍚屾绌洪棿闊抽 source 浣嶇疆锛堝湪 ResolveParentChildTransforms 涔嬪悗锛岀‘淇濅笘鐣屽潗鏍囧凡鏈€缁堢‘瀹氾級
    {
        VANS_PROFILE_SCOPE("Audio::SyncSourcePositions", Vans::ProfileCategory::Audio);
        SyncAudioSourcePositions();
    }

    // Update dirty physics transforms to GPU
    {
        VANS_PROFILE_SCOPE("RenderData::UpdateTransforms", Vans::ProfileCategory::RenderPrepare);
        UpdateTransformRenderData();
    }

    //update material data
    {
        VANS_PROFILE_SCOPE("RenderData::UpdateNodesBeforeRecord", Vans::ProfileCategory::RenderPrepare);
        UpdateRenderNodesDataBeforeRecord();
    }
}

void VansGraphics::VansScene::RecordVideoUploads(VansVKCommandBuffer& cmd)
{
    VANS_PROFILE_SCOPE("Video::Upload.RecordCommands", Vans::ProfileCategory::Video);
    m_VideoManager.RecordPendingUploads(cmd);

    // 闈㈠厜婧愯棰戝彂鍏夛細鍐欏叆 emissive 璐村浘鏁扮粍灞傦紝鍚堝苟杩涘綋鍓嶅抚鍛戒护缂撳啿銆?
    {
        VANS_PROFILE_SCOPE("RectLightVideo::RecordCopyFrames", Vans::ProfileCategory::Video);
        VansTexture* emissiveArray = m_MaterialManager.GetRuntimeRenderTexture(
            VansMaterialManager::RT_RECT_LIGHT_EMISSIVE);
        if (!emissiveArray)
            return;

        for (auto* obj : m_SceneObjects)
        {
            if (!obj) continue;
            auto* rectComp = obj->GetComponent<VansScriptRectLightComponent>();
            if (!rectComp || !rectComp->m_VideoComponent) continue;

            VansVideoTexture* vid = rectComp->m_VideoComponent->m_VideoTex;
            if (!vid || !vid->IsReady()) continue;

            vid->RecordNewFrameToArrayLayer(
                emissiveArray, cmd, rectComp->m_LightIndex);
        }
    }
}

// ============================================================
// SyncLightTransforms 鈥?灏?ScriptObject 鐨?Transform 鍚屾鍒扮伅鍏夋暟鎹?
// 姣忓抚鍦?UpdateLightShadowMatrixData 鍓嶈皟鐢ㄣ€?
// 绾﹀畾锛歍ransform 鏃嬭浆 ZYX 椤哄簭锛孼 杞存柟鍚戜负鐏厜姝ｅ悜锛堝厜绾夸紶鎾柟鍚戯級銆?
//        m_Direction 瀛樺偍鏈濆悜鍏夋簮鏂瑰悜锛堜笌姝ｅ悜鐩稿弽锛夛紝涓庡師鏈夐槾褰辩煩闃典唬鐮佷繚鎸佷竴鑷淬€?
// ============================================================
void VansGraphics::VansScene::SyncLightTransforms()
{
    for (auto* obj : m_SceneObjects)
    {
        if (!obj) continue;

        // 鈹€鈹€ 鏂瑰悜鍏夛細鍚屾鏃嬭浆 Z 杞达紙鍙栧弽鍚庯級涓?m_Direction 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        auto* dirComp = obj->GetComponent<VansScriptDirectionalLightComponent>();
        if (dirComp && dirComp->m_LightManager && dirComp->m_LightIndex >= 0)
        {
            auto& lights = dirComp->m_LightManager->GetDirectionLights();
            if (dirComp->m_LightIndex < (int)lights.size())
            {
                const auto& t = VansTransformStore::GetTransform(obj->m_TransformID);
                glm::mat4 rotMat = glm::mat4(1.0f);
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
                glm::vec3 forward = glm::normalize(glm::vec3(rotMat[2]));
                // m_Direction = 鏈濆悜鍏夋簮鏂瑰悜锛堜笌鍏夌嚎浼犳挱鏂瑰悜鐩稿弽锛?
                lights[dirComp->m_LightIndex].m_Direction = -forward;
            }
        }

        // 鈹€鈹€ 鐐瑰厜婧愶細鍚屾浣嶇疆 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        auto* pointComp = obj->GetComponent<VansScriptPointLightComponent>();
        if (pointComp && pointComp->m_LightManager && pointComp->m_LightIndex >= 0)
        {
            auto& lights = pointComp->m_LightManager->GetPointLights();
            if (pointComp->m_LightIndex < (int)lights.size())
            {
                const auto& t = VansTransformStore::GetTransform(obj->m_TransformID);
                lights[pointComp->m_LightIndex].m_Position = t.m_Position;
            }
        }

        // 鈹€鈹€ 鑱氬厜鐏細鍚屾浣嶇疆涓庢柟鍚?鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        auto* spotComp = obj->GetComponent<VansScriptSpotLightComponent>();
        if (spotComp && spotComp->m_LightManager && spotComp->m_LightIndex >= 0)
        {
            auto& lights = spotComp->m_LightManager->GetSpotLight();
            if (spotComp->m_LightIndex < (int)lights.size())
            {
                const auto& t = VansTransformStore::GetTransform(obj->m_TransformID);
                lights[spotComp->m_LightIndex].m_Position = t.m_Position;

                glm::mat4 rotMat = glm::mat4(1.0f);
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
                glm::vec3 forward = glm::normalize(glm::vec3(rotMat[2]));
                lights[spotComp->m_LightIndex].m_Direction = -forward;
            }
        }

        // 鈹€鈹€ 闈㈠厜婧愶細鍚屾浣嶇疆涓庝笁涓熀搴曞悜閲忥紙Right/Up/Normal锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        // 涓?Spot 涓€鑷达細Normal 鎸囧悜鍏夋簮"鐓у皠鏂瑰悜"锛堜笌 SpotLight.m_Direction 鍙栧弽鍚屼箟锛?
        auto* rectComp = obj->GetComponent<VansScriptRectLightComponent>();
        if (rectComp && rectComp->m_LightManager && rectComp->m_LightIndex >= 0)
        {
            auto& lights = rectComp->m_LightManager->GetRectLights();
            if (rectComp->m_LightIndex < (int)lights.size())
            {
                const auto& t = VansTransformStore::GetTransform(obj->m_TransformID);
                glm::mat4 rotMat = glm::mat4(1.0f);
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
                rotMat = glm::rotate(rotMat, glm::radians(t.m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
                glm::vec3 right   = glm::normalize(glm::vec3(rotMat[0]));   // local +X
                glm::vec3 up      = glm::normalize(glm::vec3(rotMat[1]));   // local +Y
                glm::vec3 forward = glm::normalize(glm::vec3(rotMat[2]));   // local +Z
                lights[rectComp->m_LightIndex].m_Position = t.m_Position;
                lights[rectComp->m_LightIndex].m_Right    = right;
                lights[rectComp->m_LightIndex].m_Up       = up;
                lights[rectComp->m_LightIndex].m_Normal   = forward;
            }
        }

    }
}

// ============================================================
// SyncAudioSourcePositions 鈥?姣忓抚灏?spatial 闊抽鑺傜偣鐨?OpenAL source
// 浣嶇疆鍚屾鍒板搴?ScriptObject 鐨勪笘鐣屽潗鏍囥€傞渶鍦?ResolveParentChildTransforms
// 涔嬪悗銆乀ickAll 涔嬪墠璋冪敤锛岀‘淇濅娇鐢ㄦ渶鏂扮殑涓栫晫鍧愭爣銆?
// ============================================================
void VansGraphics::VansScene::SyncAudioSourcePositions()
{
    glm::vec4 camPos = m_Camera->GetPosition();

    for (auto* obj : m_SceneObjects)
    {
        if (!obj) continue;
        auto* audioComp = obj->GetComponent<VansScriptAudioComponent>();
        if (!audioComp || !audioComp->m_AudioNode) continue;
        if (!audioComp->m_AudioNode->GetSpatial()) continue;
        if (obj->m_TransformID == 0) continue;

        const auto& t = VansTransformStore::GetTransform(obj->m_TransformID);
        audioComp->m_AudioNode->SetPosition(t.m_Position.x, t.m_Position.y, t.m_Position.z);

        float dx   = t.m_Position.x - camPos.x;
        float dy   = t.m_Position.y - camPos.y;
        float dz   = t.m_Position.z - camPos.z;
        float dist = std::sqrt(dx*dx + dy*dy + dz*dz);

        // 鎵嬪姩绾挎€ц“鍑忥紝瀹屽叏缁曡繃 OpenAL 璺濈妯″瀷
        // gain = clamp(1 - (dist - ref) / (max - ref), 0, 1)
        float ref  = audioComp->m_AudioNode->GetRefDist();
        float maxD = audioComp->m_AudioNode->GetMaxDist();
        float gain = 1.0f;
        if (dist >= maxD)
        {
            gain = 0.0f;
        }
        else if (dist > ref)
        {
            gain = 1.0f - (dist - ref) / (maxD - ref);
        }
        audioComp->m_AudioNode->SetSpatialGain(gain);
    }
}

void VansGraphics::VansScene::UpdateAnimations(float deltaTime){
    for (VansAnimationNode* animNode : m_AnimationNodes)
    {
        if (animNode && animNode->IsEnabled())
        {
            animNode->Update(deltaTime);
            VansEngine::VansRagdollSystem::GetInstance().PostAnimationUpdate(animNode);
            animNode->UploadBoneMatrices(0); // single frame buffer, always index 0
        }
    }
}

void VansGraphics::VansScene::UpdateRenderNodesDataBeforeRecord()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    if (vkDevice == nullptr)
    {
        return;
    }

    auto updateNode = [&](VansRenderNode* node)
    {
        if (node && node->IsEnabled())
        {
            node->UpdateRenderData(vkDevice, m_MaterialManager, m_LightManager, m_Camera);
            node->UpdateDescriptorSets(m_MaterialManager);
        }
    };

    updateNode(m_SkyBoxNode);
    updateNode(m_DeferredNode);
    updateNode(m_TerrainRenderNode);
    updateNode(m_WaterRenderNode);
    updateNode(m_VegetationRenderNode);

    for (auto* node : m_OpaqueRenderNodes)
        updateNode(node);
	for (auto* node : m_HairRenderNodes)
		updateNode(node);
	for (auto* node : m_ForwardOpaqueAfterDeferredRenderNodes)
		updateNode(node);
    for (auto* node : m_TransParentRenderNodes)
        updateNode(node);
    for (auto* node : m_PostProcessRenderNodes)
        updateNode(node);
    for (auto* node : m_ScreenSpaceRenderNodes)
        updateNode(node);
    // 璐磋姳鑺傜偣锛氭洿鏂?GBuffer2 descriptor 缁戝畾
    for (auto* node : m_DecalRenderNodes)
        updateNode(node);
}
void VansGraphics::VansScene::BuildRayTracingAS(VansVKDevice* vans_device, VansVKCommandBuffer* vans_commandBuffer)
{
    VkDevice device = vans_device->GetLogicDevice();
    for (const auto& meshAsset : m_AssetRegistry.GetMeshes())
    {
        VansMesh* mesh = static_cast<VansMesh*>(meshAsset);
        if (!mesh->m_SupportRayTracing)
        {
            continue;
        }

        mesh->BuildBLAS(*vans_device, *vans_commandBuffer);

        int blasIndex = m_BLASVertexData.size();
        mesh->SetBLASIndex(blasIndex);
        m_BLASVertexData.push_back(mesh->GetBLASVertexBuffer());
        m_BLASIndexData.push_back(mesh->GetIndexBuffer());

    }

    VANS_LOG("[BuildRayTracingAS] BLAS build complete, collecting TLAS instances (opaqueNodes=" << m_OpaqueRenderNodes.size() << ")");

    int nodeIdx = 0;
    for (auto& node : m_OpaqueRenderNodes)
    {
        // 璺宠繃楠ㄩ鍔ㄧ敾鑺傜偣锛堜笉鍙備笌鍏夌嚎杩借釜锛?
        if (node->m_HasSkeletonBone || node->m_AnimOwner)
        {
            ++nodeIdx;
            continue;
        }
        // 澶氱綉鏍肩埗瀹瑰櫒鑺傜偣娌℃湁鑷韩 Mesh锛岄潤榛樿烦杩?
        if (!node->m_Mesh)
        {
            ++nodeIdx;
            continue;
        }
        if (!node->m_Mesh->m_SupportRayTracing)
        {
            ++nodeIdx;
            continue;
        }

        auto transformMatrix = node->GetTransformMatrix();

        // 鍒涘缓瀹炰緥缂撳啿鍖?
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

        // 鑾峰彇BLAS鍦板潃
        VkAccelerationStructureDeviceAddressInfoKHR asAddressInfo{};
        asAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
        asAddressInfo.accelerationStructure = node->m_Mesh->GetBLAS();
        asAddressInfo.pNext = nullptr;
        instance.accelerationStructureReference = vans_device->GetAccelerationAddress(&asAddressInfo);

        m_TlasInstancesInfos.push_back(instance);

        m_TLASInstaneData.push_back(node->m_Mesh->GetBLASIndex());

        //璁板綍璐村浘绱㈠紩 鈥?浠呭 PBR 鏉愯川 (type 0) 鏀堕泦璐村浘
        int textureIndex = -1;
        if (!node->m_Material || node->m_Material->m_MaterialType != VAN_PBR)
        {
            m_TlasInstanceTextureIndex.push_back(-1);
            ++nodeIdx;
            continue;
        }
        auto textureIndexIT = m_TlasInstanceMaterialToIndex.find(node->m_Material->m_AssetName);
        if (textureIndexIT == m_TlasInstanceMaterialToIndex.end())
        {
            textureIndex = m_TlasInstanceTextures.size();
			m_TlasInstanceMaterialToIndex.insert(std::make_pair(node->m_Material->m_AssetName, textureIndex));
			VansPBRMaterial* pbrMat = static_cast<VansPBRMaterial*>(node->m_Material);
			m_TlasInstanceTextures.push_back(pbrMat->m_BaseColorTexture->GetImage());
			m_TlasInstanceTextures.push_back(pbrMat->m_NormalTexture->GetImage());
			m_TlasInstanceTextures.push_back(pbrMat->m_MetalTexture->GetImage());
			m_TlasInstanceTextures.push_back(pbrMat->m_RoughnessTexture->GetImage());
			m_TlasInstanceTextures.push_back(pbrMat->m_AoTexture->GetImage());
        }
        else
        {
			textureIndex = textureIndexIT->second;
        }
        m_TlasInstanceTextureIndex.push_back(textureIndex);
        ++nodeIdx;
    }

    uint32_t countInstance = static_cast<uint32_t>(m_TlasInstancesInfos.size());

    VANS_LOG("[BuildRayTracingAS] TLAS instance collection complete (instances=" << countInstance << ")");

    // No RT instances to build 鈥?skip TLAS entirely
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

    //鑾峰彇as鐨勯鍒嗛厤澶у皬
    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{};
    buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vans_device->GetAccelerationStructureBuildSizes(&buildInfo, maxPrimCount.data(), &buildSizesInfo);

    //scratch izhi
    m_TLASScratchBuffer.CreatVulkanBuffer(
        device,
        buildSizesInfo.buildScratchSize,
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkDeviceAddress scratchAddress = m_TLASScratchBuffer.GetDeviceAddress(device);


    // 鍒涘缓缂撳啿鍖?
    m_TopLevelASBuffer.CreatVulkanBuffer(
        device,
        buildSizesInfo.accelerationStructureSize,
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // 鏋勫缓TLAS
    VkAccelerationStructureCreateInfoKHR accelCreateInfo = {};
    accelCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelCreateInfo.buffer = m_TopLevelASBuffer.GetNativeBuffer();
    accelCreateInfo.size = buildSizesInfo.accelerationStructureSize;
    accelCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    vans_device->CreateAccelerationStructure(&accelCreateInfo, &m_TopLevelAS);

    //as鐨勫湴鍧€
    VkAccelerationStructureDeviceAddressInfoKHR asAddressInfo;
    asAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    asAddressInfo.accelerationStructure = m_TopLevelAS;
    asAddressInfo.pNext = nullptr;
    VkDeviceAddress asAddress = vans_device->GetAccelerationAddress(&asAddressInfo);

    const VkAccelerationStructureBuildRangeInfoKHR* ppRangeInfos[] = 
    {
        m_AsBuildRangeInfo.data() // 瀵逛簬 infoCount=1锛屼粎闇€涓€涓寚閽?
    };

    //琛ュ叏鍓╀笅鐨刡uild info
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
        if (!mesh->m_SupportRayTracing)
        {
            continue;
        }
        mesh->ReleaseASTempData(device);
    }

    m_TLASScratchBuffer.DestroyVulkanBuffer(device);
}
void VansGraphics::VansScene::UpdateTransformRenderData()
{
    for (auto node : m_OpaqueRenderNodes)
    {
        if (!node->IsEnabled()) continue;
        node->UpdateModelData();
    }
	for (auto node : m_ForwardOpaqueAfterDeferredRenderNodes)
	{
		if (!node->IsEnabled()) continue;
		node->UpdateModelData();
	}
    for (auto node : m_TransParentRenderNodes)
    {
        if (!node->IsEnabled()) continue;
        node->UpdateModelData();
    }
    // 璐磋姳鑺傜偣锛氭瘡甯т笂浼犲彉鎹㈢煩闃碉紙OBB 瓒婄晫娴嬭瘯渚濊禆姝ｇ‘鐨?ModelMatrix锛?
    for (auto node : m_DecalRenderNodes)
    {
        if (!node->IsEnabled()) continue;
        node->UpdateModelData();
    }
    VansGraphics::VansTransformStore::TransformIDToTransformDirty.clear();
}

VansGraphics::VansScene* m_Scene = nullptr;


// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
//  Runtime Dynamic Entity API 瀹炵幇
// 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?

std::vector<VansRenderNode*> VansGraphics::VansScene::CollectSSBOManagedRenderNodes() const
{
    std::vector<VansRenderNode*> result;
    result.reserve(m_OpaqueRenderNodes.size()
				 + m_HairRenderNodes.size()
				 + m_ForwardOpaqueAfterDeferredRenderNodes.size()
                 + m_TransParentRenderNodes.size()
                 + m_DecalRenderNodes.size());
    result.insert(result.end(), m_OpaqueRenderNodes.begin(),    m_OpaqueRenderNodes.end());
	result.insert(result.end(), m_HairRenderNodes.begin(), m_HairRenderNodes.end());
	result.insert(result.end(), m_ForwardOpaqueAfterDeferredRenderNodes.begin(), m_ForwardOpaqueAfterDeferredRenderNodes.end());
    result.insert(result.end(), m_TransParentRenderNodes.begin(), m_TransParentRenderNodes.end());
    result.insert(result.end(), m_DecalRenderNodes.begin(),     m_DecalRenderNodes.end());
    return result;
}

bool VansGraphics::VansScene::CanCreateEntity() const
{
    return m_TransformSlotAllocator.GetActiveCount()
         < m_TransformSlotAllocator.GetMaxCapacity();
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
    descManager->WriteBufferDescriptor(
        m_GlobalTransformDataDescriptorSets[0],
        PassBinding::BUFFER_0,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_InstanceTransformDataBuffer.GetNativeBuffer(),
            0,
            m_InstanceTransformDataBuffer.GetBufferSize()
        }});
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

    // 鈹€鈹€ 1. 鍒涘缓鏂扮殑鏇村ぇ Buffer 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    const VkDeviceSize newSize = sizeof(ModelDataStruct) * newCapacity;
    VansVKBuffer newBuffer;
    newBuffer.CreatVulkanBuffer(
        device, newSize, VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    // 鈹€鈹€ 2. 鎷疯礉鎵€鏈?Active Slot 鍒版柊 Buffer 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    // slot 缂栧彿瀹屽叏涓嶅彉, 鍙槸 buffer 瀹归噺鍙樺ぇ
    // 鏃?Buffer 鏄?HOST_VISIBLE 鎸佷箙鏄犲皠鐨勶紝鐩存帴浠?CPU 鏄犲皠鍐呭瓨璇诲彇
    const uint8_t* oldMapped = static_cast<const uint8_t*>(m_InstanceTransformDataBuffer.GetMappedPtr());
    for (uint32_t slot : m_TransformSlotAllocator.GetActiveSlots())
    {
        VkDeviceSize offset = slot * sizeof(ModelDataStruct);
        ModelDataStruct data;
        if (oldMapped)
            memcpy(&data, oldMapped + offset, sizeof(ModelDataStruct));
        newBuffer.SetBufferData(&data, static_cast<int>(offset), sizeof(ModelDataStruct));
    }

    // 鈹€鈹€ 3. 鏇挎崲 Buffer锛堝抚杈圭晫璋冪敤 + WaitIdle 纭繚 GPU 涓嶅湪浣跨敤鏃?Buffer锛夆攢鈹€
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

    // 鈹€鈹€ 4. 鏇存柊 Allocator 瀹归噺 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    m_TransformSlotAllocator.SetMaxCapacity(newCapacity);

    // 鈹€鈹€ 5. Re-write Descriptor Set 2 (鎸囧埌鏂?Buffer) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
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
	case FORWARD_OPAQUE_AFTER_DEFERRED_NODE:
		vec = &m_ForwardOpaqueAfterDeferredRenderNodes;
		break;
    case TRANSPARENT_NODE: vec = &m_TransParentRenderNodes; break;
    case DECAL_NODE:       vec = &m_DecalRenderNodes;       break;
    default: return; // 涓嶅湪 SSBO 绠＄悊鐨勫垪琛ㄤ腑
    }
    auto it = std::find(vec->begin(), vec->end(), node);
    if (it != vec->end()) { std::swap(*it, vec->back()); vec->pop_back(); }
}

void VansGraphics::VansScene::UpdateLightComponentIndex(
    int oldIndex, int newIndex, VansLightType type)
{
    for (auto* obj : m_SceneObjects)
    {
        if (!obj) continue;
        for (auto* comp : obj->m_Components)
        {
            if (!comp) continue;
            switch (type)
            {
            case VansLightType::DIRECTIONAL:
                if (auto* dl = dynamic_cast<VansScriptDirectionalLightComponent*>(comp))
                    if (dl->m_LightIndex == oldIndex) dl->m_LightIndex = newIndex;
                break;
            case VansLightType::POINT:
                if (auto* pl = dynamic_cast<VansScriptPointLightComponent*>(comp))
                    if (pl->m_LightIndex == oldIndex) pl->m_LightIndex = newIndex;
                break;
            case VansLightType::SPOT:
                if (auto* sl = dynamic_cast<VansScriptSpotLightComponent*>(comp))
                    if (sl->m_LightIndex == oldIndex) sl->m_LightIndex = newIndex;
                break;
            case VansLightType::RECT:
                if (auto* rl = dynamic_cast<VansScriptRectLightComponent*>(comp))
                    if (rl->m_LightIndex == oldIndex) rl->m_LightIndex = newIndex;
                break;
            default: break;
            }
        }
    }
}

VansScriptObject* VansGraphics::VansScene::CreateEntity(
    VkDevice& device, const std::string& entityName,
    const std::string& meshName, const std::string& materialName,
    const glm::vec3& position, const glm::vec3& rotation, const glm::vec3& scale,
    const std::string& parentName)
{
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  Step 0: 鍓嶇疆妫€鏌?
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    if (!CanCreateEntity())
    {
        // 鑷姩鎵╁锛歴lot 鑰楀敖鏃跺皾璇曠炕鍊嶅閲?
        if (!GrowTransformBuffer(device, m_TransformSlotAllocator.GetMaxCapacity() * 2))
        {
            VANS_LOG_ERROR("[Scene] CreateEntity: slot exhausted and grow failed ("
                << m_TransformSlotAllocator.GetActiveCount() << "/"
                << m_TransformSlotAllocator.GetMaxCapacity() << ")");
            return nullptr;
        }
    }
    if (FindObjectByName(entityName))
    {
        VANS_LOG_ERROR("[Scene] CreateEntity: '" << entityName << "' already exists");
        return nullptr;
    }

    // 鈹€鈹€ Resolve mesh 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VansAsset* meshAsset = GetMeshAsset(meshName);
    if (!meshAsset)
    {
        VANS_LOG_ERROR("[Scene] CreateEntity: mesh '" << meshName << "' not found");
        return nullptr;
    }
    VansMesh* mesh = static_cast<VansMesh*>(meshAsset);

    // 鈹€鈹€ Resolve material (fallback to first available) 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    VansAsset* matAsset = GetMaterialAsset(materialName);
    if (!matAsset && !m_AssetRegistry.GetMaterials().empty())
        matAsset = m_AssetRegistry.GetMaterials()[0];
    if (!matAsset)
    {
        VANS_LOG_ERROR("[Scene] CreateEntity: no material available");
        return nullptr;
    }
    auto* material = static_cast<VansMaterial*>(matAsset);

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  Step 1: 鍒涘缓 VansScriptObject锛堟ˉ鎺ュ眰瀹瑰櫒锛?
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    auto* obj = new VansScriptObject();
    obj->m_EntityGuid = Vans::VansAssetGuid::New().ToString();
    obj->m_ObjectName = entityName;
    // obj->m_OwnsTransform = false锛堥粯璁わ級锛歍ransform 鐢?RenderNode 鎷ユ湁骞跺湪鍏舵瀽鏋勬椂閲婃斁

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  Step 2: 鍒涘缓 RenderNode
    //
    //  VansCommonRenderNode 鏋勯€犲嚱鏁板唴閮ㄨ皟鐢?VansTransformStore::AllocateTransform()
    //  骞跺皢 ID 瀛樺叆 m_TransformID锛宮_OwnsTransform = true銆?
    //  姝ゅ鐩存帴鐢?SetTransformData 鍐欏叆鍒濆鍊硷紝鏃犻渶澶栭儴鍗曠嫭 Allocate銆?
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    RenderNodeType nodeType = (material->m_MaterialType == VansMaterialType::VAN_TRANSPARENT ||
        material->m_MaterialType == VansMaterialType::VAN_PBR_TRANSMISSION)
        ? TRANSPARENT_NODE : OPAQUE_NODE;
	if (material->m_MaterialType == VansMaterialType::VAN_CUSTOM_SHADER)
	{
		nodeType = material->m_CustomShaderDepthWrite
			? FORWARD_OPAQUE_AFTER_DEFERRED_NODE
			: TRANSPARENT_NODE;
	}

    VansRenderNode* renderNode = nodeType == TRANSPARENT_NODE
        ? static_cast<VansRenderNode*>(new VansTransparentRenderNode(device, nodeType))
        : static_cast<VansRenderNode*>(new VansCommonRenderNode(device, nodeType));
    renderNode->m_NodeName  = entityName;
    renderNode->m_Mesh      = mesh;
    renderNode->m_Material  = material;
    renderNode->SetTransformData(position, rotation, scale);

    // 鈹€鈹€ 鍒嗛厤 Transform SSBO 妲戒綅 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    uint32_t slot = m_TransformSlotAllocator.AllocateSlot();
    assert(slot != TransformSlotAllocator::INVALID_SLOT);
    renderNode->m_TransfromIndex = static_cast<int>(slot);

    // 鈹€鈹€ 鍐欏叆鍒濆 ModelData 鍒版寔涔呮槧灏勭殑 SSBO 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    renderNode->BeforeDrawCall();
    m_InstanceTransformDataBuffer.UpdateMapped(
        &renderNode->m_ModelData,
        slot * sizeof(ModelDataStruct),
        sizeof(ModelDataStruct));

    // 鈹€鈹€ 娉ㄥ唽鍒板搴旇妭鐐瑰悜閲?鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    RegistRenderNode(renderNode, nodeType);

    // 鈹€鈹€ 鍒涘缓鎻忚堪绗﹂泦 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
    renderNode->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  Step 3: 鍒涘缓 VansScriptRenderComponent锛堟ˉ鎺ュ寘瑁呭櫒锛?
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    auto* rc = new VansScriptRenderComponent();
    rc->m_ComponentName = "render";
    rc->m_RenderNode    = renderNode;
    obj->AddComponent(rc);
    obj->m_TransformID  = renderNode->m_TransformID;

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  Step 4: 寤虹珛灞傜骇鍏崇郴
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    if (!parentName.empty())
    {
        VansScriptObject* parent = FindObjectByName(parentName);
        if (parent)
            m_TransformParentSystem.SetParent(
                renderNode->m_TransformID, parent->m_TransformID);
        else
            VANS_LOG("[Scene] CreateEntity: parent '" << parentName
                << "' not found, placed at root");
    }

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  Step 5: 杩藉姞鍒?m_SceneObjects锛屾爣璁?dirty
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    m_SceneObjects.push_back(obj);
    VansTransformStore::TransformIDToTransformDirty[renderNode->m_TransformID] = true;

    VANS_LOG("[Scene] CreateEntity: '" << entityName
        << "' mesh=" << meshName << " slot=" << slot
        << " active=" << m_TransformSlotAllocator.GetActiveCount());

    return obj;
}

bool VansGraphics::VansScene::DestroyEntity(const std::string& entityName)
{
    VansScriptObject* obj = FindObjectByName(entityName);
    if (!obj)
    {
        VANS_LOG("[Scene] DestroyEntity: '" << entityName << "' not found");
        return false;
    }
    return DestroyEntity(obj);
}

bool VansGraphics::VansScene::DestroyEntity(VansScriptObject* obj)
{
    if (!obj) return false;
    const std::string name = obj->m_ObjectName;

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  0. 娓呴櫎缂栬緫鍣ㄩ€変腑鐘舵€侊紙蹇呴』鍦ㄤ换浣?delete 涔嬪墠锛岄槻姝㈡偓鍨傛瘮杈冿級
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  1. 瑙ｉ櫎 TransformParentSystem 鍏宠仈
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    if (m_TransformParentSystem.HasParent(obj->m_TransformID))
        m_TransformParentSystem.ClearParent(obj->m_TransformID);

    // 灏嗕互鏈疄浣撲负 parent 鐨勫瓙鑺傜偣鎻愬崌涓烘牴鑺傜偣
    {
        std::vector<uint32_t> childrenToReparent;
        for (const auto& link : m_TransformParentSystem.GetAllLinks())
            if (link.parentTransformID == obj->m_TransformID)
                childrenToReparent.push_back(link.childTransformID);
        for (uint32_t childID : childrenToReparent)
            m_TransformParentSystem.ClearParent(childID);
    }

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  2. 涓€閬嶆壂鎻?m_Components锛屽畬鎴愶細
    //       a) 鏀堕泦鎵€鏈夊簳灞?Node 鎸囬拡锛堝繀椤诲湪 delete obj 鍓嶏紝鏋愭瀯鍚庢寚閽堟棤鏁堬級
    //       b) 鐢熷懡鍛ㄦ湡鍓嶇疆鎿嶄綔锛圱eardown / Stop / Pause锛?
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    VansGraphics::VansRenderNode*            renderNode   = nullptr;
    VansGraphics::VansParticleRenderNode*    particleRN   = nullptr;
    VansGraphics::VansAnimationNode*         animNode     = nullptr;
    VansEngine::VansPhysicsNode*             physicsNode  = nullptr;
    VansEngine::VansClothNode*               clothNode    = nullptr;
    VansEngine::VansCharacterControllerNode* cctNode      = nullptr;
    VansEngine::VansPhysicsVehicle*          vehicleNode  = nullptr;
    bool                                     hasRagdoll   = false;
    bool                                     ownsTransform = obj->m_OwnsTransform;
    uint32_t                                 transformID   = obj->m_TransformID;

    int dlightIdx = -1, plightIdx = -1, slightIdx = -1, rlightIdx = -1;

    for (auto* comp : obj->m_Components)
    {
        if (!comp) continue;

        // 鈹€鈹€ 鏀堕泦搴曞眰 Node 鎸囬拡 鈹€鈹€
        if      (auto* rc = dynamic_cast<VansScriptRenderComponent*>(comp))
            renderNode   = rc->m_RenderNode;
        else if (auto* pc = dynamic_cast<VansScriptPhysicsComponent*>(comp))
            physicsNode  = pc->m_PhysicsNode;
        else if (auto* cl = dynamic_cast<VansScriptClothComponent*>(comp))
            clothNode    = cl->m_ClothNode;
        else if (auto* ct = dynamic_cast<VansScriptCharacterControllerComponent*>(comp))
            cctNode      = ct->m_ControllerNode;
        else if (auto* an = dynamic_cast<VansScriptAnimationComponent*>(comp))
            animNode     = an->m_AnimNode;
        else if (auto* ve = dynamic_cast<VansScriptVehicleComponent*>(comp))
        {
            if (m_Vehicle == ve->m_Vehicle) vehicleNode = m_Vehicle;
        }
        else if (auto* dl = dynamic_cast<VansScriptDirectionalLightComponent*>(comp))
            dlightIdx    = dl->m_LightIndex;
        else if (auto* pl = dynamic_cast<VansScriptPointLightComponent*>(comp))
            plightIdx    = pl->m_LightIndex;
        else if (auto* sl = dynamic_cast<VansScriptSpotLightComponent*>(comp))
            slightIdx    = sl->m_LightIndex;
        else if (auto* rl = dynamic_cast<VansScriptRectLightComponent*>(comp))
            rlightIdx    = rl->m_LightIndex;
        else if (dynamic_cast<VansScriptRagdollComponent*>(comp))
            hasRagdoll   = true;

        // 鈹€鈹€ Camera锛氬満鏅崟渚嬶紝涓?delete锛屼粎瑙ｇ粦 TransformID 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        if (dynamic_cast<VansScriptCameraComponent*>(comp))
        {
            if (m_Camera)
                m_Camera->SetTransformID(UINT32_MAX);
        }

        // 鈹€鈹€ Particle锛氭敞閿€杩愯鏃讹紙m_Runtime 鐨?unique_ptr 鏋愭瀯鍓嶈鍏堟敞閿€锛夆攢鈹€
        if (auto* pt = dynamic_cast<VansScriptParticleComponent*>(comp))
        {
            if (pt->m_Runtime)
                VansParticleManager::Instance().UnregisterRuntime(pt->m_Runtime.get());
            particleRN = pt->m_RenderNode;
        }

        // 鈹€鈹€ Python锛歍eardown 閲婃斁 py::object锛堟瀽鏋勫墠蹇呴』璋冪敤锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        if (auto* py = dynamic_cast<VanPyScriptComponent*>(comp))
            py->Teardown();

        // 鈹€鈹€ Audio锛氬仠姝㈡挱鏀撅紙椤圭洰绾ц祫婧愶紝涓?delete锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        if (auto* au = dynamic_cast<VansScriptAudioComponent*>(comp))
            if (au->m_AudioNode) au->m_AudioNode->Stop();

        // 鈹€鈹€ Video锛氭殏鍋滐紙椤圭洰绾ц祫婧愶紝涓?delete锛夆攢鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€
        if (auto* vi = dynamic_cast<VansScriptVideoComponent*>(comp))
            if (vi->m_VideoTex) vi->m_VideoTex->Pause();
    }

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  3. 浠?m_SceneObjects 绉婚櫎锛宒elete obj
    //
    //  VansScriptObject 鏋愭瀯鍑芥暟浼氶€愪竴 delete m_Components锛坵rapper 灞傦級銆?
    //  搴曞眰 Node锛圧enderNode / PhysicsNode 绛夛級涓嶅彈褰卞搷鈥斺€攚rapper 鍙寔闈炴嫢鏈夋寚閽堛€?
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    auto sit = std::find(m_SceneObjects.begin(), m_SceneObjects.end(), obj);
    if (sit != m_SceneObjects.end()) m_SceneObjects.erase(sit);
    delete obj;     // 鏋愭瀯鍒犻櫎鎵€鏈?VansScriptComponent wrapper
    obj = nullptr;  // 缃┖闃叉鍚庣画璇敤

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  4. 鎸佺墿鐞嗛攣锛氭竻鐞?Ragdoll / Vehicle / CCT / Cloth / Physics
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    {
        auto& physSys = VansEngine::VansPhysicsSystem::GetInstance();
        std::lock_guard<std::mutex> lock(physSys.GetSimulationMutex());

        // 4a. Ragdoll锛堜緷璧?animNode锛屽繀椤诲湪 PhysicsNode 涔嬪墠锛?
        if (hasRagdoll && animNode)
            VansEngine::VansRagdollSystem::GetInstance().DestroyRagdoll(animNode);

        // 4b. Vehicle锛堝満鏅骇鍗曚緥锛?
        if (vehicleNode) { delete vehicleNode; m_Vehicle = nullptr; }

        // 4c. CharacterController锛堝厛 Release PhysX controller锛屽啀 delete node锛?
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

        // 4d. Cloth锛圫hutdown + delete + 娓呯悊骞宠 staging buffer锛?
        if (clothNode)
        {
            auto ci = std::find(m_ClothNodes.begin(), m_ClothNodes.end(), clothNode);
            if (ci != m_ClothNodes.end())
            {
                size_t idx = static_cast<size_t>(ci - m_ClothNodes.begin());
                clothNode->Shutdown();
                delete clothNode;
                m_ClothNodes.erase(ci);

                // m_ClothStagingBuffers 涓?m_ClothNodes 骞宠绱㈠紩
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

        // 4e. Physics锛堟瀽鏋勫嚱鏁拌嚜鍔ㄤ粠 PxScene remove actor锛?
        if (physicsNode)
        {
            auto pi = std::find(m_PhysicsNodes.begin(), m_PhysicsNodes.end(), physicsNode);
            if (pi != m_PhysicsNodes.end()) { delete physicsNode; m_PhysicsNodes.erase(pi); }
        }
    } // 鈹€鈹€鈹€ 閲婃斁 SimulationMutex 鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€鈹€

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  4.5. MultiMeshGroup 娓呯悊锛堝繀椤诲湪 delete renderNode 涔嬪墠锛?
    //
    //  褰撳墠 CreateEntity 浠呮敮鎸佸崟 Mesh 瀹炰綋锛埪?.4.2 鑼冨洿杈圭晫锛夛紝
    //  浣嗗満鏅姞杞斤紙ExpandMultiMeshToRenderNodes锛変細浜х敓 multi-mesh 瀹炰綋銆?
    //  鑻?DestroyEntity 浣滅敤浜?multi-mesh 瀹炰綋锛屽繀椤绘竻鐞?group 鍏冩暟鎹?
    //  鍜岄潪棣栧瓙鑺傜偣锛圴ansScriptRenderComponent 浠呮寔鏈?childNodes[0]锛夈€?
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    if (renderNode && !renderNode->m_ParentGroupName.empty())
    {
        auto groupIt = m_MultiMeshGroups.find(renderNode->m_ParentGroupName);
        if (groupIt != m_MultiMeshGroups.end())
        {
            const auto& group = groupIt->second;

            // 闈為瀛愯妭鐐癸細涓嶅湪缁勪欢鎵弿鑼冨洿鍐咃紝闇€鏄惧紡娓呯悊
            for (auto* childNode : group.childNodes)
            {
                if (childNode && childNode != renderNode)
                {
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

            m_MultiMeshGroups.erase(groupIt);
            RebuildAssetLookup();
        }
    }

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  5. 娓呯悊 RenderNode锛堝繀椤诲湪 AnimationNode 涔嬪墠锛?
    //
    //  鏍稿績椤哄簭绾︽潫锛?
    //    VansCommonRenderNode 鐨?DescriptorSet (Set 3 Animation) 寮曠敤
    //    VansAnimationNode 绠＄悊鐨?GPU bone buffer銆?
    //    蹇呴』鍏?delete renderNode锛堥噴鏀?DescriptorSet / vkFreeDescriptorSets锛夛紝
    //    鍐?delete animNode锛堥攢姣?GPU bone buffer锛夛紝鍚﹀垯 Vulkan 楠岃瘉灞傛姤閿欍€?
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    if (renderNode)
    {
        // 5a. 鍥炴敹 SSBO 妲戒綅锛岀疆 -1 闃叉涓嬩竴甯?UpdateModelData 鎮瀭鍐欏叆
        if (renderNode->m_TransfromIndex >= 0)
        {
            m_TransformSlotAllocator.FreeSlot(
                static_cast<uint32_t>(renderNode->m_TransfromIndex));
            renderNode->m_TransfromIndex = -1;
        }

        // 5b. swap-pop 绉诲嚭鑺傜偣鍚戦噺
        RemoveRenderNodeFromVector(renderNode);

        // 5c. delete锛?
        //   - 鏋愭瀯鍑芥暟鍐呴儴璋冪敤 DestroyDescriptorSets
        //   - 鑻?m_OwnsTransform==true锛屾瀽鏋勫嚱鏁板悓鏃惰皟鐢?VansTransformStore::FreeTransform
        delete renderNode;
        renderNode = nullptr;
    }

    // 5d. Particle RenderNode锛堢嫭绔嬪垪琛紝鏋愭瀯涓嶇敱 VansScriptParticleComponent 绠＄悊锛?
    if (particleRN)
    {
        auto pi = std::find(m_ParticleRenderNodes.begin(),
                            m_ParticleRenderNodes.end(), particleRN);
        if (pi != m_ParticleRenderNodes.end()) m_ParticleRenderNodes.erase(pi);
        delete particleRN;
    }

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  6. 娓呯悊 AnimationNode + AnimationController锛堝湪 RenderNode 涔嬪悗锛?
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    if (animNode)
    {
        // 6a. 鍏堟竻鐞?AnimationController锛堢敱 animNode->GetController() 鑾峰彇锛?
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

        // 6b. 鍒犻櫎 AnimationNode锛堟瀽鏋勯噴鏀?GPU bone buffer锛?
        auto ai = std::find(m_AnimationNodes.begin(), m_AnimationNodes.end(), animNode);
        if (ai != m_AnimationNodes.end()) { delete animNode; m_AnimationNodes.erase(ai); }
    }

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  7. 娓呯悊 Light Components锛坰wap-pop + 鏇存柊 LightIndex 寮曠敤锛?
    //
    //  LightManager 鏃犵嫭绔?Remove API锛岄€氳繃 swap-pop 缁存姢鍚戦噺绱у噾鎬с€?
    //  swap-pop 鍚庤绉诲叆浣嶇疆鐨勯偅鐩忕伅 oldIndex 鍙樻垚 newIndex锛?
    //  闇€閬嶅巻鎵€鏈?VansScriptObject 鏇存柊瀵瑰簲 m_LightIndex 瀛楁銆?
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    auto removeLightByIndex = [&](auto& lightVec, int index, VansLightType type)
    {
        if (index < 0 || index >= static_cast<int>(lightVec.size())) return;
        int last = static_cast<int>(lightVec.size()) - 1;
        if (index != last)
        {
            std::swap(lightVec[index], lightVec[last]);
            UpdateLightComponentIndex(last, index, type);
        }
        lightVec.pop_back();
    };

    if (dlightIdx >= 0)
        removeLightByIndex(m_LightManager.GetDirectionLights(), dlightIdx,
                           VansLightType::DIRECTIONAL);
    if (plightIdx >= 0)
        removeLightByIndex(m_LightManager.GetPointLights(), plightIdx,
                           VansLightType::POINT);
    if (slightIdx >= 0)
        removeLightByIndex(m_LightManager.GetSpotLight(), slightIdx,
                           VansLightType::SPOT);
    if (rlightIdx >= 0)
    {
        // RectLight 棰濆娓呴櫎鍙戝厜绾圭悊灞?
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
        removeLightByIndex(rects, rlightIdx, VansLightType::RECT);
    }

    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    //  8. 鍥炴敹 Transform Store ID锛堜粎闄愭棤 RenderNode 鐨勭函鐗╃悊/鐩告満瀹炰綋锛?
    //
    //  褰撳瓨鍦?RenderNode 鏃讹紙m_OwnsTransform==false on obj锛夛紝
    //  renderNode->m_OwnsTransform==true锛屽叾鏋愭瀯鍑芥暟锛圫tep 5c锛夊凡璋冪敤
    //  VansTransformStore::FreeTransform(m_TransformID)銆傚閮ㄤ笉鍙噸澶嶈皟鐢ㄣ€?
    //
    //  褰撳疄浣撴棤 RenderNode锛堢函鐗╃悊瀹炰綋锛宱bj->m_OwnsTransform==true锛夋椂锛?
    //  LoadSceneObjects 浼氫负鍏跺崟鐙垎閰?transform 骞惰缃?obj->m_OwnsTransform=true锛?
    //  姝ゅ闇€瑕佹墜鍔ㄩ噴鏀俱€?
    // 鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺愨晲鈺?
    if (ownsTransform && renderNode == nullptr)
        VansTransformStore::FreeTransform(transformID);

    VANS_LOG("[Scene] DestroyEntity: '" << name
        << "' active=" << m_TransformSlotAllocator.GetActiveCount());
    return true;
}


