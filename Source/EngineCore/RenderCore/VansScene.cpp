#include "../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansScene.h"
#include "VansShaderRegistry.h"
#include "BRDFData/VansLight.h"
#include "../Configration/VansConfigration.h"
#include "../PhysicsCore/VansPhysics.h"
#include "../PhysicsCore/VansPhysicsNode.h"
#include "../PhysicsCore/VansPhysicsVehicle.h"

#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "TerrainCore/VansTerrain.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../AnimationCore/VansSkinnedMeshLoader.h"
#include "../VansTimer.h"

#include "../../EngineCore/EditorCore/AssetsSystem/VansAssetsFileWatcher.h"
#include "../Util/VansLog.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <filesystem>


VansAsset* VansGraphics::VansScene::GetMeshAsset(const std::string& name)
{
    //搜索对应的mesh
    for (auto mesh : m_Meshes)
    {
        if (mesh->m_AssetName== name)
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
	VANS_LOG("[VansScene] UnLoadScene 开始卸载当前场景...");

	VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	VkDevice nativeDevice = vkDevice ? vkDevice->GetLogicDevice() : VK_NULL_HANDLE;

	// ── 0. 清除编辑器选中状态 ─────────────────────────────────────────────
	m_SelectedNode = nullptr;
	m_SelectedObject = nullptr;
	VANS_LOG("[VansScene] Step 0: 编辑器选中状态已清除");

	// ── 1. 清理场景级运行时纹理（SH 系数 + GI Visibility），保留屏幕空间纹理 ──
	//  SSGI / SSAO / HZB / SSR / Fog 等屏幕空间纹理在 PrepareRenderingData()
	//  时创建，不依赖场景内容，无需在场景切换时销毁。
	//  SH 纹理由 RuntimeRenderTextureManager 拥有，使用 Remove（会 delete）。
	//  RT_GI_VISIBILITY 的实际对象由 VansRayTracing::CleanupSceneResources 销毁，
	//  此处仅从注册表移除引用（Unregister），防止悬空指针，同时避免 double-free。
	m_MaterialManager.RemoveRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT);
	m_MaterialManager.RemoveRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT);
	m_MaterialManager.RemoveRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT);
	m_MaterialManager.UnregisterRuntimeRenderTexture(VansMaterialManager::RT_GI_VISIBILITY);
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
	for (auto* obj : m_SceneObjects)
	{
		if (!obj) continue;
		for (auto* comp : obj->m_Components)
		{
			auto* pyComp = dynamic_cast<VanPyScriptComponent*>(comp);
			if (pyComp) pyComp->Teardown();
		}
	}
	VANS_LOG("[VansScene] Step 2a: 脚本组件已 Teardown");

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
	{
		auto& physicsSystem = VansEngine::VansPhysicsSystem::GetInstance();
		std::lock_guard<std::mutex> simLock(physicsSystem.GetSimulationMutex());

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

	for (auto& stagingBuf : m_ClothStagingBuffers)
	{
		if (stagingBuf.IsMapped())
			stagingBuf.Unmap();
		stagingBuf.DestroyVulkanBuffer(nativeDevice);
	}
	m_ClothStagingBuffers.clear();
	VANS_LOG("[VansScene] Step 5b: 布料 staging buffer 已清理");

	// ── 6. 清理 transform 父子系统 ───────────────────────────────────────
	m_TransformParentSystem.Clear();
	VANS_LOG("[VansScene] Step 6: Transform 父子系统已清理");

	// ── 7. 清理植被系统 ─────────────────────────────────────────────────
	if (m_VegetationSystem)
	{
		m_VegetationSystem->Cleanup(nativeDevice);
		delete m_VegetationSystem;
		m_VegetationSystem = nullptr;
	}
	VANS_LOG("[VansScene] Step 7: 植被系统已清理");

	// ── 8. 清理所有渲染节点（必须在动画节点之前，因为渲染节点的 descriptor
	//       set 引用了动画节点的 bone buffer，需在 buffer 销毁前释放 set）
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

	deleteRenderNode(m_SkyBoxNode);
	m_SkyBoxNode = nullptr;

	deleteRenderNode(m_DeferredNode);
	m_DeferredNode = nullptr;

	deleteRenderNode(m_TerrainRenderNode);
	m_TerrainRenderNode = nullptr;

	// VegetationRenderNode 未被列表持有，需单独 delete
	deleteRenderNode(m_VegetationRenderNode);
	m_VegetationRenderNode = nullptr;
	VANS_LOG("[VansScene] Step 8: 渲染节点已全部清理");

	// ── 9. 清理动画节点（析构函数会销毁 GPU bone buffer） ─────────────────
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
	for (auto* ctrl : m_AnimationControllers)
	{
		delete ctrl;
	}
	m_AnimationControllers.clear();
	VANS_LOG("[VansScene] Step 9b: 动画控制器已清理");

	// ── 10. 清理 Multi-mesh 分组 ────────────────────────────────────────
	VANS_LOG("[VansScene] Step 10: 开始清理 Multi-mesh 分组 (数量=" << m_MultiMeshGroups.size() << ")");
	m_MultiMeshGroups.clear();

	// 移除 ExpandMultiMeshToRenderNodes 添加到 m_Meshes 中的子网格条目。
	// 子网格对象本身由父级 multi-mesh 的 m_SubMeshes 拥有，此处仅清除查找列表中的条目，
	// 防止下次 ExpandMultiMeshToRenderNodes 时产生重复。
	m_Meshes.erase(
		std::remove_if(m_Meshes.begin(), m_Meshes.end(),
			[](VansAsset* asset) {
				return static_cast<VansMesh*>(asset)->m_IsSubmesh;
			}),
		m_Meshes.end());

	VANS_LOG("[VansScene] Step 10: Multi-mesh 分组已清理");

	// ── 11. 清理材质（场景级，指针由 Scene 拥有） ───────────────────────
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
	m_MaterialManager.ClearScenePBRData(nativeDevice);

	// ── 13. 清理灯光 CPU 数据和 GPU 资源 ────────────────────────────────
	m_LightManager.ClearLights();
	m_LightManager.DestroyGPUResources(nativeDevice);
	VANS_LOG("[VansScene] Step 12-13: PBR 和灯光 GPU 资源已清理");

	// ── 14. 清理 Ray Tracing TLAS 资源 ─────────────────────────────────
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
	m_InstanceTransformDataBuffer.DestroyVulkanBuffer(nativeDevice);
	m_InstanceTransformData.clear();

	// 释放 Transform Data descriptor set 和 layout
	auto descMgr = VansVKDescriptorManager::GetInstance();
	descMgr->DestroyDescriptorSet(m_GlobalTransformDataDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GlobalTransformDataSetLayout);

	// ── 16. 清理 Global / Object / Animation / Empty Descriptor Sets ─────
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
	m_DummyBoneIDBuffer.DestroyVulkanBuffer(nativeDevice);
	m_DummyBoneBuffer.DestroyVulkanBuffer(nativeDevice);
	m_DummyWeightBuffer.DestroyVulkanBuffer(nativeDevice);

	// ── 18. 暂停视频播放（视频为项目级资源，GPU 纹理保留，切换场景/Play 时复用）────────
	m_VideoManager.PauseAll();

	VANS_LOG("[VansScene] 场景卸载完成");
}

void VansGraphics::VansScene::UpdateSceneData()
{
    // 先将灯光组件 transform 数据同步到灯光结构体，再计算阴影矩阵
    SyncLightTransforms();
    m_LightManager.UpdateLightShadowMatrixData(glm::vec3(m_Camera->GetPosition()));
    m_LightManager.UpdateLightCPUData();

    // Per-frame skeletal animation update + GPU bone matrix upload
    // Use the cached frame delta so all per-frame systems observe the same timestep.
    UpdateAnimations(static_cast<float>(VansTimer::GetLastFrameDelta()));

    // Advance cloth simulation and write results to staging buffers
    UpdateClothSimulation(0.03f);
    WriteClothResultsToStagingBuffers();

    // 推进所有视频纹理的播放，上传就绪帧到 GPU（在 Vulkan 命令录制之前执行）
    m_VideoManager.TickAll(VansTimer::GetLastFrameDelta());

    // 面光源视频发光：将有新帧的视频像素更新到 emissive 贴图数组层
    {
        VansTexture* emissiveArray = m_MaterialManager.GetRuntimeRenderTexture(
            VansMaterialManager::RT_RECT_LIGHT_EMISSIVE);
        VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
        if (emissiveArray && vkDevice)
        {
            for (auto* obj : m_SceneObjects)
            {
                if (!obj) continue;
                auto* rectComp = obj->GetComponent<VansScriptRectLightComponent>();
                if (!rectComp || !rectComp->m_EmissiveVideo) continue;

                VansVideoTexture* vid = rectComp->m_EmissiveVideo;
                if (!vid->IsReady() || !vid->HasNewFrame()) continue;

                const int layerIdx = rectComp->m_LightIndex;
                emissiveArray->UpdateArrayLayerFromPixels(
                    vkDevice->GetCommandBuffer(),
                    vid->GetLastFramePixels().data(),
                    vid->GetWidth(), vid->GetHeight(),
                    layerIdx);
                vid->ConsumeNewFrame();
            }
        }
    }

    // Resolve parent-child transform relationships before GPU upload
    m_TransformParentSystem.ResolveParentChildTransforms();

    // Update dirty physics transforms to GPU
    UpdateTransformRenderData();

    //update material data
    UpdateRenderNodesDataBeforeRecord();
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

void VansGraphics::VansScene::UpdateAnimations(float deltaTime){
    for (VansAnimationNode* animNode : m_AnimationNodes)
    {
        if (animNode)
        {
            animNode->Update(deltaTime);
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
        if (node)
        {
            node->UpdateRenderData(vkDevice, m_MaterialManager, m_LightManager, m_Camera);
            node->UpdateDescripterSets(m_MaterialManager);
        }
    };

    updateNode(m_SkyBoxNode);
    updateNode(m_DeferredNode);
    updateNode(m_TerrainRenderNode);
    updateNode(m_VegetationRenderNode);

    for (auto* node : m_OpaqueRenderNodes)
        updateNode(node);
    for (auto* node : m_TransParentRenderNodes)
        updateNode(node);
    for (auto* node : m_PostProcessRenderNodes)
        updateNode(node);
    for (auto* node : m_ScreenSpaceRenderNodes)
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
        node->UpdateModelData();
	}
    for (auto node : m_TransParentRenderNodes)
    {
        node->UpdateModelData();
    }
    VansGraphics::VansTransformStore::TransformIDToTransformDirty.clear();
}

VansGraphics::VansScene* m_Scene = nullptr;

VansAssetsFileWatcher* m_SceneFileWatcher = nullptr;
