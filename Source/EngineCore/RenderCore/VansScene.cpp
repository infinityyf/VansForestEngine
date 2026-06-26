#include "../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansScene.h"
#include "../VansFramePhase.h"
#include "../VansThreadContract.h"
#include "VansShaderManager.h"
#include "BRDFData/VansLight.h"
#include "../Configration/VansConfigration.h"
#include "../PhysicsCore/VansPhysics.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "../PhysicsCore/VansPhysicsVehicle.h"
#include "../PhysicsCore/VansTerrainPhysicsNode.h"
#include "../PhysicsCore/VansRagdollSystem.h"

#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "TerrainCore/VansTerrain.h"
#include "WaterCore/VansWaterSystem.h"
#include "VansParticleRenderNode.h"
#include "../ParticleCore/VansParticleManager.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../AnimationCore/VansBoneAttachmentSystem.h"
#include "../AnimationCore/VansSkinnedMeshLoader.h"
#include "../VansTimer.h"

#include "../../EngineCore/EditorCore/AssetsSystem/VansAssetsFileWatcher.h"
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


VansGraphics::VansScene::~VansScene()
{
    if (m_SceneState != VansSceneState::Empty || !m_SceneObjects.empty())
    {
        VANS_LOG_WARN("[VansScene] 析构时场景仍非空，请在 delete 前显式调用 UnLoadScene()");
    }
}

VansAsset* VansGraphics::VansScene::GetMeshAsset(const std::string& name)
{
	if (const auto alias = m_ProjectMeshAliases.find(name); alias != m_ProjectMeshAliases.end())
		return alias->second;
    //搜索对应的mesh
    for (auto mesh : m_Meshes)
    {
        if (mesh->m_AssetName== name)
        {
			return mesh;
		}
	}
    for (auto mesh : m_SceneSubMeshes)
    {
        if (mesh->m_AssetName == name)
        {
            return mesh;
        }
    }
    return nullptr;
}

VansAsset* VansGraphics::VansScene::GetShaderAsset(const std::string& name)
{
    //搜索对应shader
    for (auto shader : m_Shaders)
    {
        if (shader->m_AssetName == name)
        {
			return shader;
		}
	}
    return nullptr;
}

VansAsset* VansGraphics::VansScene::GetTextureAsset(const std::string& name)
{
    //搜索texture
    for (auto texture : m_Textures)
    {
        if (texture->m_AssetName == name)
        {
            return texture;
        }
    }

    return nullptr;
}

VansAsset* VansGraphics::VansScene::GetMaterialAsset(const std::string& name)
{
    //搜索material
    for (auto material : m_Materials)
    {
        if (material->m_AssetName == name)
        {
			return material;
		}
	}
    return nullptr;
}

void VansGraphics::VansScene::RegistRenderNode(VansRenderNode* renderNode, RenderNodeType type)
{
    //将rendernode记录到对应类型的vector中
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

    // Create object layout + set (Set 2: Transform SSBO only — shared by all geometry nodes)
    std::vector<VkDescriptorSet> objSets;
    VansDescriptorSetLayoutFactory::CreateAndAllocate_Object(m_ObjectDescriptorSetLayout, objSets);
    m_ObjectDescriptorSet = objSets[0];

    // Write Set 2: binding 0 (Transform SSBO)
    descManager->ResetState();
    descManager->m_BufferDescInfos.push_back(
        {
            m_ObjectDescriptorSet,
            OBJECT_BINDING_TRANSFORM_SSBO,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_InstanceTransformDataBuffer.GetNativeBuffer(),
                    0,
                    m_InstanceTransformDataBuffer.GetBufferSize()
                }
            }
        }
    );
    descManager->UpdateDescriptorSets();

    // ── Create shared dummy animation buffers and Set 3 for static nodes ─────────
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

    descManager->ResetState();
    // binding 0: Dummy Bone ID SSBO (per-submesh bone IDs)
    descManager->m_BufferDescInfos.push_back(
        {
            m_AnimationDescriptorSet,
            ANIMATION_BINDING_BONEID_SSBO,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {{ m_DummyBoneIDBuffer.GetNativeBuffer(), 0, 64 }}
        }
    );
    // binding 1: Dummy Bone Matrices SSBO
    descManager->m_BufferDescInfos.push_back(
        {
            m_AnimationDescriptorSet,
            ANIMATION_BINDING_BONE_SSBO,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {{ m_DummyBoneBuffer.GetNativeBuffer(), 0, 64 }}
        }
    );
    // binding 2: Dummy Bone Weight SSBO (per-submesh bone weights)
    descManager->m_BufferDescInfos.push_back(
        {
            m_AnimationDescriptorSet,
            ANIMATION_BINDING_BONEWEIGHT_SSBO,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {{ m_DummyWeightBuffer.GetNativeBuffer(), 0, 64 }}
        }
    );
    descManager->UpdateDescriptorSets();

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
    descManager->ResetState();

    // Binding 0: Camera UBO
    descManager->m_BufferDescInfos.push_back(
        {
            m_GlobalDescriptorSet,
            GLOBAL_BINDING_CAMERA_UBO,
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            {
                {
                    m_Camera->m_CameraDataBuffer.GetNativeBuffer(),
                    0,
                    m_Camera->m_CameraDataBuffer.GetBufferSize()
                }
            }
        }
    );

    // Binding 1: Lights SSBO
    descManager->m_BufferDescInfos.push_back(
        {
            m_GlobalDescriptorSet,
            GLOBAL_BINDING_LIGHTS_UBO,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_LightManager.GetLightBuffer().GetNativeBuffer(),
                    0,
                    m_LightManager.GetLightBuffer().GetBufferSize()
                }
            }
        }
    );

    // Binding 2: Material SSBO
    descManager->m_BufferDescInfos.push_back(
        {
            m_GlobalDescriptorSet,
            GLOBAL_BINDING_MATERIAL_SSBO,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_MaterialManager.m_GlobalPBRDataBuffer.GetNativeBuffer(),
                    0,
                    m_MaterialManager.m_GlobalPBRDataBuffer.GetBufferSize()
                }
            }
        }
    );

    // Binding 3: BRDF LUT
    descManager->m_ImageDescInfos.push_back(
        {
            m_GlobalDescriptorSet,
            GLOBAL_BINDING_BRDF_LUT,
            0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {
                {
                    m_MaterialManager.m_BRDFIntegralLUT->GetImage().GetSampler(),
                    m_MaterialManager.m_BRDFIntegralLUT->GetImage().GetImageView(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                }
            }
        }
    );

    // Binding 4: Pre-convolved diffuse environment
    descManager->m_ImageDescInfos.push_back(
        {
            m_GlobalDescriptorSet,
            GLOBAL_BINDING_PRECONV_DIFFUSE,
            0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {
                {
                    m_MaterialManager.m_PreConvDiffuse->GetImage().GetSampler(),
                    m_MaterialManager.m_PreConvDiffuse->GetImage().GetImageView(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                }
            }
        }
    );

    // Binding 5: Pre-convolved specular environment
    descManager->m_ImageDescInfos.push_back(
        {
            m_GlobalDescriptorSet,
            GLOBAL_BINDING_PRECONV_SPECULAR,
            0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {
                {
                    m_MaterialManager.m_PreConvSpecular->GetImage().GetSampler(),
                    m_MaterialManager.m_PreConvSpecular->GetImage().GetImageView(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                }
            }
        }
    );

    // Binding 6: SH coefficients buffer
    descManager->m_BufferDescInfos.push_back(
        {
            m_GlobalDescriptorSet,
            GLOBAL_BINDING_SH_COEFFICIENTS,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_MaterialManager.m_SkySHResultBuffer.GetNativeBuffer(),
                    0,
                    m_MaterialManager.m_SkySHResultBuffer.GetBufferSize()
                }
            }
        }
    );

    // Binding 7: Skin pre-integrated BSDF LUT
    descManager->m_ImageDescInfos.push_back(
        {
            m_GlobalDescriptorSet,
            GLOBAL_BINDING_SKIN_BSDF_LUT,
            0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {
                {
                    m_MaterialManager.m_SkinBSDFLUT->GetImage().GetSampler(),
                    m_MaterialManager.m_SkinBSDFLUT->GetImage().GetImageView(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                }
            }
        }
    );

    // Binding 11/12: LTC LUTs (area-light BRDF, runtime-uploaded RGBA16F 64x64)
    if (m_MaterialManager.m_LTC1 && m_MaterialManager.m_LTC2)
    {
        descManager->m_ImageDescInfos.push_back(
            {
                m_GlobalDescriptorSet,
                GLOBAL_BINDING_LTC1_LUT,
                0,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {
                    {
                        m_MaterialManager.m_LTC1->GetImage().GetSampler(),
                        m_MaterialManager.m_LTC1->GetImage().GetImageView(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    }
                }
            }
        );
        descManager->m_ImageDescInfos.push_back(
            {
                m_GlobalDescriptorSet,
                GLOBAL_BINDING_LTC2_LUT,
                0,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                {
                    {
                        m_MaterialManager.m_LTC2->GetImage().GetSampler(),
                        m_MaterialManager.m_LTC2->GetImage().GetImageView(),
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                    }
                }
            }
        );
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
        descManager->m_ImageDescInfos.push_back(
            {
                m_GlobalDescriptorSet,
                GLOBAL_BINDING_BINDLESS_TEXTURES,
                0,
                VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                bindlessInfos
            }
        );
    }

    descManager->UpdateDescriptorSets();
}

// NOTE: TileLight bindings (9 and 10) are written in VansVKDevice::UpdateGlobalTileLightDesc
//       after PrepareTileLightData allocates m_TileLightHeaderBuffer and m_TileLightIndexBuffer.

void VansGraphics::VansScene::UpdateGlobalTileLightDescriptors()
{
    auto descManager = VansVKDescriptorManager::GetInstance();
    descManager->ResetState();

    // Binding 9: TileLight Header SSBO
    descManager->m_BufferDescInfos.push_back(
        {
            m_GlobalDescriptorSet,
            GLOBAL_BINDING_TILE_LIGHT_GRID,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_MaterialManager.m_TileLightHeaderBuffer.GetNativeBuffer(),
                    0,
                    m_MaterialManager.m_TileLightHeaderBuffer.GetBufferSize()
                }
            }
        }
    );

    // Binding 10: TileLight Index SSBO
    descManager->m_BufferDescInfos.push_back(
        {
            m_GlobalDescriptorSet,
            GLOBAL_BINDING_TILE_LIGHT_INDICES,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_MaterialManager.m_TileLightIndexBuffer.GetNativeBuffer(),
                    0,
                    m_MaterialManager.m_TileLightIndexBuffer.GetBufferSize()
                }
            }
        }
    );

    descManager->UpdateDescriptorSets();
}

VansGraphics::VansRenderNode* VansGraphics::VansScene::FindRenderNodeByName(const std::string& name) const
{
    // Search across all render node lists that store mesh nodes
    for (auto* node : m_OpaqueRenderNodes)
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

	VANS_LOG("[VansScene] UnLoadScene 开始卸载当前场景...");

	VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	VkDevice nativeDevice = vkDevice ? vkDevice->GetLogicDevice() : VK_NULL_HANDLE;
	if (nativeDevice != VK_NULL_HANDLE)
		m_ReflectionProbeSystem.Clear(nativeDevice);

	// ── 0. 清除编辑器选中状态 ─────────────────────────────────────────────
    VANS_UNLOAD_STEP(0, "清除编辑器选中状态");
	m_SelectedNode = nullptr;
	m_SelectedObject = nullptr;
	VANS_LOG("[VansScene] Step 0: 编辑器选中状态已清除");

	// ── 1. 清理场景级运行时纹理（SH 系数 + GI Visibility），保留屏幕空间纹理 ──
	//  SSGI / SSAO / HZB / SSR / Fog 等屏幕空间纹理在 PrepareRenderingData()
	//  时创建，不依赖场景内容，无需在场景切换时销毁。
	//  SH 纹理由 RuntimeRenderTextureManager 拥有，使用 Remove（会 delete）。
    VANS_UNLOAD_STEP(1, "清理场景级运行时纹理");
	m_MaterialManager.RemoveRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT);
	m_MaterialManager.RemoveRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT);
	m_MaterialManager.RemoveRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT);
	m_MaterialManager.m_SSGITemporalFrame = 0;
	m_MaterialManager.m_FogTemporalFrame  = 0;
	// SH 纹理已移除，标记渲染 Feature 的 descriptor set 需要重新写入
	if (vkDevice)
	{
		vkDevice->ResetFeatureDescriptorSets();
	}
	VANS_LOG("[VansScene] Step 1: 场景级运行时纹理已清理 (屏幕空间纹理保留)");

	// ── 2. 清理脚本对象（仅释放 wrapper，不释放底层 Node） ─────────────────
	// 先 Teardown 所有 VanPyScriptComponent，安全释放 py::object，
	// 再删除 VansScriptObject（此时 m_PyInstance 已为 py::none()）。
    VANS_UNLOAD_STEP(2, "清理脚本对象与脚本模块跟踪");
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
	VANS_LOG("[VansScene] Step 2a: 脚本组件已 Teardown");
    VansParticleManager::Instance().Shutdown();

	// ScriptContext 中的 tracked modules 也一并清理
	if (VansScriptContext::GetInstance())
	{
		VansScriptContext::GetInstance()->ClearTrackedModules();
	}
	for (auto* obj : m_SceneObjects)
	{
		delete obj;
	}
	m_SceneObjects.clear();
	VANS_LOG("[VansScene] Step 2b: SceneObjects 已全部释放");

	// ── 3-5. 清理物理节点 / 载具 / 布料（需要持有物理线程锁） ─────────────
	// 物理模拟在独立线程运行，必须先获取 SimulationMutex 再操作 PxScene。
    VANS_UNLOAD_STEP("3-5", "持有物理锁清理物理节点、载具、布料和角色控制器");
	{
		auto& physicsSystem = VansEngine::VansPhysicsSystem::GetInstance();
		std::lock_guard<std::mutex> simLock(physicsSystem.GetSimulationMutex());

        // ── 2c. 清理布娃娃系统（直接持有 PxD6Joint / PxRigidDynamic）──────
        VansEngine::VansRagdollSystem::GetInstance().Shutdown();
        VANS_LOG("[VansScene] Step 2c: Ragdoll 系统已清理 (持锁)");

		// ── 3. 清理物理节点（析构函数会从 PxScene 移除 actor） ─────────
		for (auto* physicsNode : m_PhysicsNodes)
		{
			if (physicsNode)
			{
				delete physicsNode;
			}
		}
		m_PhysicsNodes.clear();
		VANS_LOG("[VansScene] Step 3: 物理节点已清理 (持锁)");

        // ── 3b. 清理地形高度场碰撞 ─────────────────────────────────────
        if (m_TerrainPhysicsNode)
        {
            delete m_TerrainPhysicsNode;
            m_TerrainPhysicsNode = nullptr;
        }
        VANS_LOG("[VansScene] Step 3b: 地形物理节点已清理 (持锁)");

		// ── 4. 清理载具 ──────────────────────────────────────────────────
		if (m_Vehicle)
		{
			delete m_Vehicle;
			m_Vehicle = nullptr;
		}
		VANS_LOG("[VansScene] Step 4: 载具已清理");

		// ── 5. 清理布料节点和 staging buffer ──────────────────────────────
		for (auto* clothNode : m_ClothNodes)
		{
			if (clothNode)
			{
				clothNode->Shutdown();
				delete clothNode;
			}
		}
		m_ClothNodes.clear();
		VANS_LOG("[VansScene] Step 5: 布料节点已清理");

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
		VANS_LOG("[VansScene] Step 5b: 角色控制器节点已清理");
	} // 释放 SimulationMutex

    VANS_UNLOAD_STEP("5b", "清理布料 staging buffer");
	for (auto& stagingBuf : m_ClothStagingBuffers)
	{
		if (stagingBuf.IsMapped())
			stagingBuf.Unmap();
		stagingBuf.DestroyVulkanBuffer(nativeDevice);
	}
	m_ClothStagingBuffers.clear();
	VANS_LOG("[VansScene] Step 5b: 布料 staging buffer 已清理");

	// ── 6. 清理 transform 父子系统 ───────────────────────────────────────
    VANS_UNLOAD_STEP(6, "清理 transform 父子系统");
	m_TransformParentSystem.Clear();
	VANS_LOG("[VansScene] Step 6: Transform 父子系统已清理");

	// ── 7. 清理植被系统 ─────────────────────────────────────────────────
    VANS_UNLOAD_STEP(7, "清理植被系统");
	if (m_VegetationSystem)
	{
		m_VegetationSystem->Cleanup(nativeDevice);
		delete m_VegetationSystem;
		m_VegetationSystem = nullptr;
	}
	VANS_LOG("[VansScene] Step 7: 植被系统已清理");

	// ── 8. 清理所有渲染节点（必须在动画节点之前，因为渲染节点的 descriptor
	//       set 引用了动画节点的 bone buffer，需在 buffer 销毁前释放 set）
    VANS_UNLOAD_STEP(8, "清理所有渲染节点");
	auto deleteRenderNode = [](VansRenderNode* node) {
		if (node) delete node;
	};

	for (auto* node : m_OpaqueRenderNodes)
		deleteRenderNode(node);
	m_OpaqueRenderNodes.clear();

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

	deleteRenderNode(m_SkyBoxNode);
	m_SkyBoxNode = nullptr;

	deleteRenderNode(m_DeferredNode);
	m_DeferredNode = nullptr;

	deleteRenderNode(m_TerrainRenderNode);
	m_TerrainRenderNode = nullptr;

	deleteRenderNode(m_WaterRenderNode);
	m_WaterRenderNode = nullptr;
	m_WaterMaterial   = nullptr; // 材质已随 m_Materials 一起释放，清空指针即可

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
	VANS_LOG("[VansScene] Step 8: 渲染节点已全部清理");

	// ── 9. 清理动画节点（析构函数会销毁 GPU bone buffer） ─────────────────
    VANS_UNLOAD_STEP(9, "清理动画节点");
	for (auto* animNode : m_AnimationNodes)
	{
		if (animNode)
		{
			delete animNode;
		}
	}
	m_AnimationNodes.clear();
	VANS_LOG("[VansScene] Step 9: 动画节点已清理");

	// ── 9b. 清理动画控制器（Controller 由 Scene 持有，Node 只存裸指针） ───
    VANS_UNLOAD_STEP("9b", "清理动画控制器");
	for (auto* ctrl : m_AnimationControllers)
	{
		delete ctrl;
	}
	m_AnimationControllers.clear();
	VANS_LOG("[VansScene] Step 9b: 动画控制器已清理");

    // ── 9c. 清理骨骼碰撞体附着点系统 ────────────────────────────────
    VansEngine::VansBoneAttachmentSystem::GetInstance().Shutdown();
    VANS_LOG("[VansScene] Step 9c: 骨骼碰撞体附着点系统已清理");

	// ── 10. 清理 Multi-mesh 分组 ────────────────────────────────────────
    VANS_UNLOAD_STEP(10, "清理 Multi-mesh 分组和子网格查找条目");
	VANS_LOG("[VansScene] Step 10: 开始清理 Multi-mesh 分组 (数量=" << m_MultiMeshGroups.size() << ")");
	m_MultiMeshGroups.clear();

    // 子网格对象本身由父级 multi-mesh 的 m_SubMeshes 拥有，此处仅清除非拥有查找列表，
    // 防止下次 ExpandMultiMeshToRenderNodes 时产生重复。
    m_SceneSubMeshes.clear();

	VANS_LOG("[VansScene] Step 10: Multi-mesh 分组已清理");

	// ── 11. 清理材质（场景级，指针由 Scene 拥有） ───────────────────────
    VANS_UNLOAD_STEP(11, "清理场景级材质");
	VANS_LOG("[VansScene] Step 11: 开始清理材质 (数量=" << m_Materials.size() << ")");
	for (size_t i = 0; i < m_Materials.size(); ++i)
	{
		auto* mat = m_Materials[i];
		if (mat)
		{
			auto* realMat = static_cast<VansMaterial*>(mat);
			VANS_LOG("[VansScene] Step 11: 删除材质 [" << i << "] type=" << realMat->m_MaterialType << " name=" << mat->m_AssetName);
			delete mat;
		}
	}
	m_Materials.clear();
	VANS_LOG("[VansScene] Step 10-11: Multi-mesh 和材质已清理");

	// ── 12. 清理全局 PBR 数据和 descriptor ──────────────────────────────
    VANS_UNLOAD_STEP(12, "清理全局 PBR 数据和 descriptor");
	m_MaterialManager.ClearScenePBRData(nativeDevice);

	// ── 13. 清理灯光 CPU 数据和 GPU 资源 ────────────────────────────────
    VANS_UNLOAD_STEP(13, "清理灯光 CPU 数据和 GPU 资源");
	m_LightManager.ClearLights();
	m_LightManager.DestroyGPUResources(nativeDevice);

	// IES profile GPU 纹理数组（sampler2DArray，binding=16）
	m_IESProfileManager.DestroyGPUResources(nativeDevice);
	VANS_LOG("[VansScene] Step 12-13: PBR 和灯光 GPU 资源已清理");

	// ── 14. 清理 Ray Tracing TLAS 资源 ─────────────────────────────────
    VANS_UNLOAD_STEP(14, "清理 Ray Tracing TLAS/BLAS 场景资源");
	if (vkDevice)
	{
		vkDevice->GetRayTracingContext().CleanupSceneResources(nativeDevice);
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
	m_TlasInstanceTextures.clear();
	m_TlasInstanceMaterialToIndex.clear();

	// 释放项目级 mesh 上残留的 BLAS 加速结构，防止二次 BuildBLAS 时资源泄漏
	for (const auto& meshAsset : m_Meshes)
	{
		VansMesh* mesh = static_cast<VansMesh*>(meshAsset);
		if (mesh->m_SupportRayTracing)
		{
			mesh->DestroyBLAS(nativeDevice);
		}
	}
	VANS_LOG("[VansScene] Step 14: RT/TLAS 资源已清理");

	// ── 15. 清理 Instance Transform Buffer ──────────────────────────────
    VANS_UNLOAD_STEP(15, "清理 Instance Transform Buffer 与 descriptor");
	m_InstanceTransformDataBuffer.DestroyVulkanBuffer(nativeDevice);
	m_InstanceTransformData.clear();

	// ── 重置 Transform Slot Allocator（必须在 DestroyVulkanBuffer 之后、下次 Prepare 之前）──
	m_TransformSlotAllocator.Reset();

	// 释放 Transform Data descriptor set 和 layout
	auto descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_GlobalTransformDataDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GlobalTransformDataSetLayout);

	// ── 16. 清理 Global / Object / Animation / Empty Descriptor Sets ─────
    VANS_UNLOAD_STEP(16, "清理 Global/Object/Animation/Empty descriptor sets");
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

	// ── 17. 清理 Dummy Bone Buffer ──────────────────────────────────────
    VANS_UNLOAD_STEP(17, "清理 Dummy Bone Buffer");
	m_DummyBoneIDBuffer.DestroyVulkanBuffer(nativeDevice);
	m_DummyBoneBuffer.DestroyVulkanBuffer(nativeDevice);
	m_DummyWeightBuffer.DestroyVulkanBuffer(nativeDevice);

	// ── 18. 暂停视频播放（视频为项目级资源，GPU 纹理保留，切换场景/Play 时复用）────────
    VANS_UNLOAD_STEP(18, "暂停项目级视频播放");
	m_VideoManager.PauseAll();

	// ── 19. 停止所有音频播放（音频为项目级资源，不释放已解码数据）────────
    VANS_UNLOAD_STEP(19, "停止项目级音频播放");
	m_AudioManager.StopAll();
    m_SceneState = VansSceneState::Empty;

	VANS_LOG("[VansScene] 场景卸载完成");
}

void VansGraphics::VansScene::UnloadProjectResources(VansVKDevice* device)
{
    VANS_ASSERT_MAIN_THREAD();

    // 重构-08 会在这里完整释放项目级资源。当前阶段只提供统一入口，避免改变既有资源生命周期。
    (void)device;
	m_ProjectMeshAliases.clear();
    VANS_LOG("[VansScene] UnloadProjectResources 空桩已调用，完整项目资源释放将在重构-08 实现");
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

    // 骨骼碰撞体附着点必须紧跟动画更新，确保 TransformStore 读取当前帧骨骼姿态。
    {
        VANS_PROFILE_SCOPE("BoneAttachment::SyncAll", Vans::ProfileCategory::Physics);
        VansEngine::VansBoneAttachmentSystem::GetInstance().Update();
    }

    // Cloth 依赖 render node 的当前世界变换来同步固定点。
    // 必须在布料模拟之前解析父子关系，否则挂在角色骨骼/父节点下的布料会用上一帧或未解析的变换模拟，
    // 随后又以新变换渲染，导致位置明显错位。
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
        shadowCamera.forward = glm::normalize(glm::vec3(-m_Camera->GetForward()));
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

    // 推进所有视频纹理的播放，上传就绪帧到 GPU（在 Vulkan 命令录制之前执行）
    {
        VANS_PROFILE_SCOPE("Video::TickAll", Vans::ProfileCategory::Video);
        m_VideoManager.TickAll(VansTimer::GetLastFrameDelta());
    }

    // 推进所有音频节点：更新 Listener 位置、驱动 Streaming 节点补充 Buffer
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

    // 粒子系统：同步对象 Transform，推进后台运行时，并上传本帧实例数据。
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
            for (auto* obj : m_SceneObjects)
            {
                if (!obj) continue;

                auto* particleComp = obj->GetComponent<VansScriptParticleComponent>();
                if (!particleComp || !particleComp->m_Runtime || !particleComp->m_RenderNode) continue;

                particleComp->m_PlayTime  = particleComp->m_Runtime->m_PlayTime;
                particleComp->m_IsPlaying = particleComp->m_Runtime->m_IsPlaying;

                particleComp->m_RenderNode->UpdateInstanceBuffer(
                    nativeDevice,
                    particleComp->m_Runtime->GetRenderBuffer());
            }
        }
    }

    // 同步空间音频 source 位置（在 ResolveParentChildTransforms 之后，确保世界坐标已最终确定）
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

    // 面光源视频发光：写入 emissive 贴图数组层，合并进当前帧命令缓冲。
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
// SyncLightTransforms — 将 ScriptObject 的 Transform 同步到灯光数据
// 每帧在 UpdateLightShadowMatrixData 前调用。
// 约定：Transform 旋转 ZYX 顺序，Z 轴方向为灯光正向（光线传播方向）。
//        m_Direction 存储朝向光源方向（与正向相反），与原有阴影矩阵代码保持一致。
// ============================================================
void VansGraphics::VansScene::SyncLightTransforms()
{
    for (auto* obj : m_SceneObjects)
    {
        if (!obj) continue;

        // ── 方向光：同步旋转 Z 轴（取反后）为 m_Direction ────────────────
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
                // m_Direction = 朝向光源方向（与光线传播方向相反）
                lights[dirComp->m_LightIndex].m_Direction = -forward;
            }
        }

        // ── 点光源：同步位置 ───────────────────────────────────────────────
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

        // ── 聚光灯：同步位置与方向 ────────────────────────────────────────
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

        // ── 面光源：同步位置与三个基底向量（Right/Up/Normal）────────────────
        // 与 Spot 一致：Normal 指向光源"照射方向"（与 SpotLight.m_Direction 取反同义）
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
// SyncAudioSourcePositions — 每帧将 spatial 音频节点的 OpenAL source
// 位置同步到对应 ScriptObject 的世界坐标。需在 ResolveParentChildTransforms
// 之后、TickAll 之前调用，确保使用最新的世界坐标。
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

        // 手动线性衰减，完全绕过 OpenAL 距离模型
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
            node->UpdateDescripterSets(m_MaterialManager);
        }
    };

    updateNode(m_SkyBoxNode);
    updateNode(m_DeferredNode);
    updateNode(m_TerrainRenderNode);
    updateNode(m_WaterRenderNode);
    updateNode(m_VegetationRenderNode);

    for (auto* node : m_OpaqueRenderNodes)
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
VkDeviceAddress VansVKDevice::GetAccelerationAddress(VkAccelerationStructureDeviceAddressInfoKHR* addressInfo)
{
    return vkGetAccelerationStructureDeviceAddressKHR(m_VansVKLogicDevice, addressInfo);
}
VkDeviceAddress VansVKDevice::GetBufferAddress(VkBufferDeviceAddressInfo* bufferInfo)
{
    return vkGetBufferDeviceAddressKHR(m_VansVKLogicDevice, bufferInfo);
}
void VansVKDevice::GetAccelerationStructureBuildSizes(VkAccelerationStructureBuildGeometryInfoKHR* buildInfo, uint32_t* maxPrimitiveCounts, VkAccelerationStructureBuildSizesInfoKHR* buildSizeInfo)
{
    vkGetAccelerationStructureBuildSizesKHR(m_VansVKLogicDevice, VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        buildInfo, maxPrimitiveCounts, buildSizeInfo);
}
void VansVKDevice::CreateAccelerationStructure(VkAccelerationStructureCreateInfoKHR* createInfo, VkAccelerationStructureKHR* as)
{
    vkCreateAccelerationStructureKHR(m_VansVKLogicDevice, createInfo, nullptr, as);
}

void VansVKDevice::DestroyAccelerationStructure(VkAccelerationStructureKHR as)
{
    if (as != VK_NULL_HANDLE)
    {
        vkDestroyAccelerationStructureKHR(m_VansVKLogicDevice, as, nullptr);
    }
}

void VansGraphics::VansScene::BuildRayTracingAS(VansVKDevice* vans_device, VansVKCommandBuffer* vans_commandBuffer)
{
    VkDevice device = vans_device->GetLogicDevice();
    VkCommandBuffer commandBuffer = vans_commandBuffer->GetVKCommandBuffer();
    for (const auto& meshAsset : m_Meshes)
    {
        VansMesh* mesh = static_cast<VansMesh*>(meshAsset);
        if (!mesh->m_SupportRayTracing)
        {
            continue;
        }

        mesh->BuildBLAS(device, commandBuffer);

        int blasIndex = m_BLASVertexData.size();
        mesh->SetBLASIndex(blasIndex);
        m_BLASVertexData.push_back(mesh->GetBLASVertexBuffer());
        m_BLASIndexData.push_back(mesh->GetIndexBuffer());

        VANS_LOG("blas build done" << mesh->m_AssetName);
    }

    VANS_LOG("[BuildRayTracingAS] BLAS 阶段完成，开始收集 TLAS 实例数据 (opaqueNodes=" << m_OpaqueRenderNodes.size() << ")");

    int nodeIdx = 0;
    for (auto& node : m_OpaqueRenderNodes)
    {
        // 跳过骨骼动画节点（不参与光线追踪）
        if (node->m_HasSkeletonBone || node->m_AnimOwner)
        {
            ++nodeIdx;
            continue;
        }
        // 多网格父容器节点没有自身 Mesh，静默跳过
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

        VANS_LOG("[BuildRayTracingAS] node[" << nodeIdx << "] '" << node->m_NodeName
            << "' mesh='" << node->m_Mesh->m_AssetName
            << "' matType=" << (node->m_Material ? static_cast<int>(node->m_Material->m_MaterialType) : -1));

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

        //记录贴图索引 — 仅对 PBR 材质 (type 0) 收集贴图
		int textureIndex = -1;
        if (!node->m_Material || node->m_Material->m_MaterialType != VAN_PBR)
        {
            VANS_LOG_WARN("[BuildRayTracingAS] node[" << nodeIdx << "] 非 PBR 材质，跳过贴图收集");
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

    VANS_LOG("[BuildRayTracingAS] TLAS 实例收集完成 (instances=" << countInstance << ")");

    // No RT instances to build — skip TLAS entirely
    if (countInstance == 0)
    {
        VANS_LOG_WARN("[BuildRayTracingAS] No ray-tracing instances found, skipping TLAS build.");
        return;
    }

    // 创建实例缓冲区
    VANS_LOG("[BuildRayTracingAS] 开始创建 TLAS Instance 缓冲区 (size=" << sizeof(VkAccelerationStructureInstanceKHR) * countInstance << " bytes)");
    m_InstancesBuffer.CreatVulkanBuffer(
        device,
        sizeof(VkAccelerationStructureInstanceKHR) * countInstance,
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    VANS_LOG("[BuildRayTracingAS] Instance 缓冲区创建完成，开始写入数据");
    m_InstancesBuffer.SetBufferData(m_TlasInstancesInfos.data(), 0, sizeof(VkAccelerationStructureInstanceKHR) * countInstance);

    // Barrier: host writes instance buffer -> TLAS build reads
    {
        VkMemoryBarrier hostWriteBarrier{};
        hostWriteBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        hostWriteBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
        hostWriteBarrier.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR;
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0,
            1, &hostWriteBarrier,
            0, nullptr,
            0, nullptr);
    }

    VkBufferDeviceAddressInfo bufferAddressInfo;
    bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferAddressInfo.buffer = m_InstancesBuffer.GetNativeBuffer();
    bufferAddressInfo.pNext = nullptr;
    VkDeviceAddress instanceBufferAddress = vans_device->GetBufferAddress(&bufferAddressInfo);

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
    VANS_LOG("[BuildRayTracingAS] 开始创建 TLAS Scratch 缓冲区 (size=" << buildSizesInfo.buildScratchSize << ")");
    m_TLASScratchBuffer.CreatVulkanBuffer(
        device,
        buildSizesInfo.buildScratchSize,
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VANS_LOG("[BuildRayTracingAS] TLAS Scratch 缓冲区创建完成");

    VkBufferDeviceAddressInfo scratchBufferAddressInfo;
    scratchBufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchBufferAddressInfo.buffer = m_TLASScratchBuffer.GetNativeBuffer();
    scratchBufferAddressInfo.pNext = nullptr;
    VkDeviceAddress scratchAddress = vans_device->GetBufferAddress(&scratchBufferAddressInfo);


    // 创建缓冲区
    VANS_LOG("[BuildRayTracingAS] 开始创建 TLAS AS 缓冲区 (size=" << buildSizesInfo.accelerationStructureSize << ")");
    m_TopLevelASBuffer.CreatVulkanBuffer(
        device,
        buildSizesInfo.accelerationStructureSize,
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    VANS_LOG("[BuildRayTracingAS] TLAS AS 缓冲区创建完成");

    // 构建TLAS
    VANS_LOG("[BuildRayTracingAS] 开始创建 TLAS 加速结构");
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
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0,
            1, &blasToTlasBarrier,
            0, nullptr,
            0, nullptr);
    }
    
    VANS_LOG("[BuildRayTracingAS] 开始提交 TLAS Build 命令");
    vans_commandBuffer->BuildAccelerationStructures(&buildInfo, *ppRangeInfos);

    VANS_LOG("tlas build done");
}

void VansGraphics::VansScene::ReleaseASTempBuffer(VansVKDevice* vans_device)
{
    VkDevice device = vans_device->GetLogicDevice();
    for (const auto& meshAsset : m_Meshes)
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
    for (auto node : m_TransParentRenderNodes)
    {
        if (!node->IsEnabled()) continue;
        node->UpdateModelData();
    }
    // 贴花节点：每帧上传变换矩阵（OBB 越界测试依赖正确的 ModelMatrix）
    for (auto node : m_DecalRenderNodes)
    {
        if (!node->IsEnabled()) continue;
        node->UpdateModelData();
    }
    VansGraphics::VansTransformStore::TransformIDToTransformDirty.clear();
}

VansGraphics::VansScene* m_Scene = nullptr;

VansAssetsFileWatcher* m_SceneFileWatcher = nullptr;

// ═══════════════════════════════════════════════════════════════════════════════
//  Runtime Dynamic Entity API 实现
// ═══════════════════════════════════════════════════════════════════════════════

std::vector<VansRenderNode*> VansGraphics::VansScene::CollectSSBOManagedRenderNodes() const
{
    std::vector<VansRenderNode*> result;
    result.reserve(m_OpaqueRenderNodes.size()
                 + m_TransParentRenderNodes.size()
                 + m_DecalRenderNodes.size());
    result.insert(result.end(), m_OpaqueRenderNodes.begin(),    m_OpaqueRenderNodes.end());
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

void VansGraphics::VansScene::UpdateTransformDescriptorSet()
{
    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->ResetState();
    descManager->m_BufferDescInfos.push_back(
    {
        m_GlobalTransformDataDescriptorSets[0],
        PassBinding::BUFFER_0,
        0,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_InstanceTransformDataBuffer.GetNativeBuffer(),
            0,
            m_InstanceTransformDataBuffer.GetBufferSize()
        }}
    });
    descManager->UpdateDescriptorSets();
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
    vkDeviceWaitIdle(device);
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
    // ═══════════════════════════════════════════════════════════════════════
    //  Step 0: 前置检查
    // ═══════════════════════════════════════════════════════════════════════
    if (!CanCreateEntity())
    {
        // 自动扩容：slot 耗尽时尝试翻倍容量
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

    // ── Resolve mesh ────────────────────────────────────────────────────
    VansAsset* meshAsset = GetMeshAsset(meshName);
    if (!meshAsset)
    {
        VANS_LOG_ERROR("[Scene] CreateEntity: mesh '" << meshName << "' not found");
        return nullptr;
    }
    VansMesh* mesh = static_cast<VansMesh*>(meshAsset);

    // ── Resolve material (fallback to first available) ──────────────────
    VansAsset* matAsset = GetMaterialAsset(materialName);
    if (!matAsset && !m_Materials.empty())
        matAsset = m_Materials[0];
    if (!matAsset)
    {
        VANS_LOG_ERROR("[Scene] CreateEntity: no material available");
        return nullptr;
    }
    auto* material = static_cast<VansMaterial*>(matAsset);

    // ═══════════════════════════════════════════════════════════════════════
    //  Step 1: 创建 VansScriptObject（桥接层容器）
    // ═══════════════════════════════════════════════════════════════════════
    auto* obj = new VansScriptObject();
    obj->m_EntityGuid = Vans::VansAssetGuid::New().ToString();
    obj->m_ObjectName = entityName;
    // obj->m_OwnsTransform = false（默认）：Transform 由 RenderNode 拥有并在其析构时释放

    // ═══════════════════════════════════════════════════════════════════════
    //  Step 2: 创建 RenderNode
    //
    //  VansCommonRenderNode 构造函数内部调用 VansTransformStore::AllocateTransform()
    //  并将 ID 存入 m_TransformID，m_OwnsTransform = true。
    //  此处直接用 SetTransformData 写入初始值，无需外部单独 Allocate。
    // ═══════════════════════════════════════════════════════════════════════
    RenderNodeType nodeType = (material->m_MaterialType == VansMaterialType::VAN_TRANSPARENT)
        ? TRANSPARENT_NODE : OPAQUE_NODE;

    auto* renderNode = new VansCommonRenderNode(device, nodeType);
    renderNode->m_NodeName  = entityName;
    renderNode->m_Mesh      = mesh;
    renderNode->m_Material  = material;
    renderNode->SetTransformData(position, rotation, scale);

    // ── 分配 Transform SSBO 槽位 ─────────────────────────────────────────
    uint32_t slot = m_TransformSlotAllocator.AllocateSlot();
    assert(slot != TransformSlotAllocator::INVALID_SLOT);
    renderNode->m_TransfromIndex = static_cast<int>(slot);

    // ── 写入初始 ModelData 到持久映射的 SSBO ─────────────────────────────
    renderNode->BeforeDrawCall();
    m_InstanceTransformDataBuffer.UpdateMapped(
        &renderNode->m_ModelData,
        slot * sizeof(ModelDataStruct),
        sizeof(ModelDataStruct));

    // ── 注册到对应节点向量 ────────────────────────────────────────────────
    RegistRenderNode(renderNode, nodeType);

    // ── 创建描述符集 ──────────────────────────────────────────────────────
    renderNode->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);

    // ═══════════════════════════════════════════════════════════════════════
    //  Step 3: 创建 VansScriptRenderComponent（桥接包装器）
    // ═══════════════════════════════════════════════════════════════════════
    auto* rc = new VansScriptRenderComponent();
    rc->m_ComponentName = "render";
    rc->m_RenderNode    = renderNode;
    obj->AddComponent(rc);
    obj->m_TransformID  = renderNode->m_TransformID;

    // ═══════════════════════════════════════════════════════════════════════
    //  Step 4: 建立层级关系
    // ═══════════════════════════════════════════════════════════════════════
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

    // ═══════════════════════════════════════════════════════════════════════
    //  Step 5: 追加到 m_SceneObjects，标记 dirty
    // ═══════════════════════════════════════════════════════════════════════
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

    // ═══════════════════════════════════════════════════════════════════════
    //  0. 清除编辑器选中状态（必须在任何 delete 之前，防止悬垂比较）
    // ═══════════════════════════════════════════════════════════════════════
    if (m_SelectedObject == obj)
    {
        m_SelectedObject = nullptr;
        m_SelectedNode   = nullptr;
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  1. 解除 TransformParentSystem 关联
    // ═══════════════════════════════════════════════════════════════════════
    if (m_TransformParentSystem.HasParent(obj->m_TransformID))
        m_TransformParentSystem.ClearParent(obj->m_TransformID);

    // 将以本实体为 parent 的子节点提升为根节点
    {
        std::vector<uint32_t> childrenToReparent;
        for (const auto& link : m_TransformParentSystem.GetAllLinks())
            if (link.parentTransformID == obj->m_TransformID)
                childrenToReparent.push_back(link.childTransformID);
        for (uint32_t childID : childrenToReparent)
            m_TransformParentSystem.ClearParent(childID);
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  2. 一遍扫描 m_Components，完成：
    //       a) 收集所有底层 Node 指针（必须在 delete obj 前，析构后指针无效）
    //       b) 生命周期前置操作（Teardown / Stop / Pause）
    // ═══════════════════════════════════════════════════════════════════════
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

        // ── 收集底层 Node 指针 ──
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

        // ── Camera：场景单例，不 delete，仅解绑 TransformID ─────────────
        if (dynamic_cast<VansScriptCameraComponent*>(comp))
        {
            if (m_Camera)
                m_Camera->SetTransformID(UINT32_MAX);
        }

        // ── Particle：注销运行时（m_Runtime 的 unique_ptr 析构前要先注销）──
        if (auto* pt = dynamic_cast<VansScriptParticleComponent*>(comp))
        {
            if (pt->m_Runtime)
                VansParticleManager::Instance().UnregisterRuntime(pt->m_Runtime.get());
            particleRN = pt->m_RenderNode;
        }

        // ── Python：Teardown 释放 py::object（析构前必须调用）──────────────
        if (auto* py = dynamic_cast<VanPyScriptComponent*>(comp))
            py->Teardown();

        // ── Audio：停止播放（项目级资源，不 delete）────────────────────────
        if (auto* au = dynamic_cast<VansScriptAudioComponent*>(comp))
            if (au->m_AudioNode) au->m_AudioNode->Stop();

        // ── Video：暂停（项目级资源，不 delete）────────────────────────────
        if (auto* vi = dynamic_cast<VansScriptVideoComponent*>(comp))
            if (vi->m_VideoTex) vi->m_VideoTex->Pause();
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  3. 从 m_SceneObjects 移除，delete obj
    //
    //  VansScriptObject 析构函数会逐一 delete m_Components（wrapper 层）。
    //  底层 Node（RenderNode / PhysicsNode 等）不受影响——wrapper 只持非拥有指针。
    // ═══════════════════════════════════════════════════════════════════════
    auto sit = std::find(m_SceneObjects.begin(), m_SceneObjects.end(), obj);
    if (sit != m_SceneObjects.end()) m_SceneObjects.erase(sit);
    delete obj;     // 析构删除所有 VansScriptComponent wrapper
    obj = nullptr;  // 置空防止后续误用

    // ═══════════════════════════════════════════════════════════════════════
    //  4. 持物理锁：清理 Ragdoll / Vehicle / CCT / Cloth / Physics
    // ═══════════════════════════════════════════════════════════════════════
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

    // ═══════════════════════════════════════════════════════════════════════
    //  4.5. MultiMeshGroup 清理（必须在 delete renderNode 之前）
    //
    //  当前 CreateEntity 仅支持单 Mesh 实体（§4.4.2 范围边界），
    //  但场景加载（ExpandMultiMeshToRenderNodes）会产生 multi-mesh 实体。
    //  若 DestroyEntity 作用于 multi-mesh 实体，必须清理 group 元数据
    //  和非首子节点（VansScriptRenderComponent 仅持有 childNodes[0]）。
    // ═══════════════════════════════════════════════════════════════════════
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

            // 清理 m_SceneSubMeshes 中属于此 group 的子 mesh 资产引用
            if (group.sourceMesh)
            {
                auto subIt = std::remove(m_SceneSubMeshes.begin(),
                                          m_SceneSubMeshes.end(),
                                          group.sourceMesh);
                m_SceneSubMeshes.erase(subIt, m_SceneSubMeshes.end());
            }

            m_MultiMeshGroups.erase(groupIt);
        }
    }

    // ═══════════════════════════════════════════════════════════════════════
    //  5. 清理 RenderNode（必须在 AnimationNode 之前）
    //
    //  核心顺序约束：
    //    VansCommonRenderNode 的 DescriptorSet (Set 3 Animation) 引用
    //    VansAnimationNode 管理的 GPU bone buffer。
    //    必须先 delete renderNode（释放 DescriptorSet / vkFreeDescriptorSets），
    //    再 delete animNode（销毁 GPU bone buffer），否则 Vulkan 验证层报错。
    // ═══════════════════════════════════════════════════════════════════════
    if (renderNode)
    {
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
        //   - 若 m_OwnsTransform==true，析构函数同时调用 VansTransformStore::FreeTransform
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

    // ═══════════════════════════════════════════════════════════════════════
    //  6. 清理 AnimationNode + AnimationController（在 RenderNode 之后）
    // ═══════════════════════════════════════════════════════════════════════
    if (animNode)
    {
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

    // ═══════════════════════════════════════════════════════════════════════
    //  7. 清理 Light Components（swap-pop + 更新 LightIndex 引用）
    //
    //  LightManager 无独立 Remove API，通过 swap-pop 维护向量紧凑性。
    //  swap-pop 后被移入位置的那盏灯 oldIndex 变成 newIndex，
    //  需遍历所有 VansScriptObject 更新对应 m_LightIndex 字段。
    // ═══════════════════════════════════════════════════════════════════════
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
        // RectLight 额外清除发光纹理层
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

    // ═══════════════════════════════════════════════════════════════════════
    //  8. 回收 Transform Store ID（仅限无 RenderNode 的纯物理/相机实体）
    //
    //  当存在 RenderNode 时（m_OwnsTransform==false on obj），
    //  renderNode->m_OwnsTransform==true，其析构函数（Step 5c）已调用
    //  VansTransformStore::FreeTransform(m_TransformID)。外部不可重复调用。
    //
    //  当实体无 RenderNode（纯物理实体，obj->m_OwnsTransform==true）时，
    //  LoadSceneObjects 会为其单独分配 transform 并设置 obj->m_OwnsTransform=true，
    //  此处需要手动释放。
    // ═══════════════════════════════════════════════════════════════════════
    if (ownsTransform && renderNode == nullptr)
        VansTransformStore::FreeTransform(transformID);

    VANS_LOG("[Scene] DestroyEntity: '" << name
        << "' active=" << m_TransformSlotAllocator.GetActiveCount());
    return true;
}
