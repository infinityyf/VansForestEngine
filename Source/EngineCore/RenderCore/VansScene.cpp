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
    for (auto node : m_ShadowRenderNodes)
    {
        node->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);
    }
    for (auto node : m_PunctualShadowRenderNodes)
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

    // Binding 1: Lights UBO
    descManager->m_BufferDescInfos.push_back(
        {
            m_GlobalDescriptorSet,
            GLOBAL_BINDING_LIGHTS_UBO,
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
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

bool VansGraphics::VansScene::LoadScene(const char* path)
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkDevice nativeDevice = vkDevice->GetLogicDevice();

    //解析json
    std::ifstream jsonFile(path);
    json sceneData = json::parse(jsonFile);
    RegisterEngineShaders();   // populate shader registry before resource loading
    LoadSceneResource(sceneData);


    VansGraphicsShader* shadowShader = static_cast<VansGraphicsShader*>(GetShaderAsset("Shadow"));
    if (shadowShader != nullptr)
    {
        VansMaterial* shadowMaterial = new VansMaterial();
        shadowMaterial->m_Shader = shadowShader;
        shadowMaterial->m_MaterialType = VansMaterialType::VAN_SHAODW;
        shadowMaterial->SetName("ShadowMaterial");
        m_Materials.push_back(shadowMaterial);
    }
    VansGraphicsShader* punctualShadowShader = static_cast<VansGraphicsShader*>(GetShaderAsset("PunctualShadow"));
    if (punctualShadowShader != nullptr)
    {
        VansMaterial* punctualShadowMaterial = new VansMaterial();
        punctualShadowMaterial->m_Shader = punctualShadowShader;
        punctualShadowMaterial->m_MaterialType = VansMaterialType::VAN_SHAODW;
        punctualShadowMaterial->SetName("PunctualShadowMaterial");
        m_Materials.push_back(punctualShadowMaterial);
    }


    //找到scene 节点，包含rendernode，camera，light数据
    json sceneNode = sceneData["scene"];

    //loadLightsData
    LoadLights(nativeDevice, sceneNode[0]["light"]);

    //根据引用关系创建render node
    LoadRenderNodes(nativeDevice, sceneNode[0]["rendernode"]);

    // Load physics nodes if present
    if (sceneNode[0].contains("physicsnode"))
    {
        LoadPhysicsNodes(sceneNode[0]["physicsnode"]);
    }


    //加载subscene中的资源
    json subScenes = sceneData["subScene"];
    //load scene data frome subscenes path
    for (const auto& ss : subScenes)
    {
        std::string subPath = ss["name"].get<std::string>();
        std::ifstream subFile(subPath);
        json subSceneData = json::parse(subFile);
        LoadSceneResource(subSceneData);

        json sceneNode = subSceneData["scene"];
        //加载并配置关系的render node
        LoadRenderNodes(nativeDevice, sceneNode[0]["rendernode"]);
        
        // Load physics nodes if present in subscene
        if (sceneNode[0].contains("physicsnode"))
        {
            LoadPhysicsNodes(sceneNode[0]["physicsnode"]);
        }
    }

    //添加地形节点（从JSON读取，如果不存在则不创建）
    if (sceneData.contains("terrain"))
    {
        AddTerrainNode(vkDevice, sceneData["terrain"]);
    }

    AddDeferredNode(nativeDevice);

    AddScreenSpaceFeatureNode(nativeDevice);

    return true;
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
    return nullptr;
}

void VansGraphics::VansScene::UnLoadScene()
{
m_MaterialManager.ClearRuntimeRenderTextures();

    // Clean up physics nodes
    for (auto* physicsNode : m_PhysicsNodes)
    {
        if (physicsNode)
        {
            delete physicsNode;
        }
    }
    m_PhysicsNodes.clear();

    // Clean up cloth nodes
    VansVKDevice* vkDeviceForCloth = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkDevice nativeDeviceForCloth = vkDeviceForCloth ? vkDeviceForCloth->GetLogicDevice() : VK_NULL_HANDLE;
    for (auto* clothNode : m_ClothNodes)
    {
        if (clothNode)
        {
            clothNode->Shutdown();
            delete clothNode;
        }
    }
    m_ClothNodes.clear();

    // Destroy scene-owned cloth staging buffers
    for (auto& stagingBuf : m_ClothStagingBuffers)
    {
        if (stagingBuf.IsMapped())
            stagingBuf.Unmap();
        stagingBuf.DestroyVulkanBuffer(nativeDeviceForCloth);
    }
    m_ClothStagingBuffers.clear();

    //delete mesh;
    //delete shader;
    //delete fullScreenMesh;
    //delete fullScreenShader;
    //delete m_Texture;
}

void VansGraphics::VansScene::UpdateSceneData()
{
    m_LightManager.UpdateLightShadowMatrixData(glm::vec3(m_Camera->GetPosition()));
    m_LightManager.UpdateLightCPUData();

    // Per-frame skeletal animation update + GPU bone matrix upload
    // Use the cached frame delta - GetDeltaTime() returns ~0 here because
    // VansTimer::Update() already reset lastFrameTime earlier this frame.
    UpdateAnimations(static_cast<float>(VansTimer::GetLastFrameDelta()));

    // Advance cloth simulation and write results to staging buffers
    UpdateClothSimulation(static_cast<float>(VansTimer::GetLastFrameDelta()));
    WriteClothResultsToStagingBuffers();

    // Update dirty physics transforms to GPU
    UpdateTransformRenderData();

    //update material data
    UpdateRenderNodesDataBeforeRecord();
}

void VansGraphics::VansScene::UpdateAnimations(float deltaTime)
{
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

    for (auto* node : m_OpaqueRenderNodes)
        updateNode(node);
    for (auto* node : m_TransParentRenderNodes)
        updateNode(node);
    for (auto* node : m_PostProcessRenderNodes)
        updateNode(node);
    for (auto* node : m_ScreenSpaceRenderNodes)
        updateNode(node);
    for (auto* node : m_ShadowRenderNodes)
        updateNode(node);
    for (auto* node : m_PunctualShadowRenderNodes)
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

    for (auto& node : m_OpaqueRenderNodes)
    {
        if (!node->m_Mesh->m_SupportRayTracing)
        {
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

        //记录贴图索引
		int textureIndex = -1;
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
    }

    uint32_t countInstance = static_cast<uint32_t>(m_TlasInstancesInfos.size());

    // No RT instances to build — skip TLAS entirely
    if (countInstance == 0)
    {
        VANS_LOG_WARN("[BuildRayTracingAS] No ray-tracing instances found, skipping TLAS build.");
        return;
    }

    // 创建实例缓冲区
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
    m_TLASScratchBuffer.CreatVulkanBuffer(
        device,
        buildSizesInfo.buildScratchSize,
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VkBufferDeviceAddressInfo scratchBufferAddressInfo;
    scratchBufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    scratchBufferAddressInfo.buffer = m_TLASScratchBuffer.GetNativeBuffer();
    scratchBufferAddressInfo.pNext = nullptr;
    VkDeviceAddress scratchAddress = vans_device->GetBufferAddress(&scratchBufferAddressInfo);


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
        vkCmdPipelineBarrier(commandBuffer,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR,
            0,
            1, &blasToTlasBarrier,
            0, nullptr,
            0, nullptr);
    }
    
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
