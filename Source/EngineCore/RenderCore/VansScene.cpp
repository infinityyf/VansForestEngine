#include "../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansScene.h"
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

#include "../../EngineCore/EditorCore/AssetsSystem/VansAssetsFileWatcher.h"
#include "../Util/VansLog.h"
#include <iostream>
#include <fstream>
#include <unordered_map>


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

    // Create object layout + set (Set 2: Transform SSBO)
    std::vector<VkDescriptorSet> objSets;
    VansDescriptorSetLayoutFactory::CreateAndAllocate_Object(m_ObjectDescriptorSetLayout, objSets);
    m_ObjectDescriptorSet = objSets[0];

    // Write transform SSBO to the object set (binding 0)
    descManager->ResetState();
    descManager->m_BufferDescInfos.push_back(
        {
            m_ObjectDescriptorSet,
            0, // OBJECT_BINDING_TRANSFORM_SSBO
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

    // Binding 50: Bindless PBR textures
    auto& textures = m_MaterialManager.m_GlobalPBRTextures;
    if (!textures.empty())
    {
        std::vector<VkDescriptorImageInfo> bindlessInfos;
        bindlessInfos.reserve(textures.size());
        for (auto* img : textures)
        {
            bindlessInfos.push_back({
                img->GetSampler(),
                img->GetImageView(),
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


    //找到scene 节点，包含rendernode， camera，light数据
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

void VansGraphics::VansScene::LoadLights(VkDevice& device, json& light_node)
{
    for (const auto& light : light_node)
    {
        VansLightType type = light["type"];
        if (type == VansLightType::DIRECTIONAL)
        {
            VansDirectionalLight dirLight;
            dirLight.m_Direction = glm::vec3(light["direction"][0], light ["direction"][1], light["direction"][2]);
            dirLight.m_Direction = -glm::normalize(dirLight.m_Direction);
			dirLight.m_Color = glm::vec3(light["color"][0], light["color"][1], light["color"][2]);
            dirLight.m_Intensity = light["intensity"];
            m_LightManager.AddDirectionalLight(dirLight);
		}
        else if (type == VansLightType::POINT)
        {
            VansPointLight pointLight;
			pointLight.m_Position = glm::vec3(light["position"][0], light["position"][1], light["position"][2]);
			pointLight.m_Color = glm::vec3(light["color"][0], light["color"][1], light["color"][2]);
            pointLight.m_Intensity = light["intensity"];
			pointLight.m_Radius = light["radius"];
            m_LightManager.AddPointLight(pointLight);
		}
        else if (type == VansLightType::SPOT)
        {
            VansSpotLight spotLight;
			spotLight.m_Position = glm::vec3(light["position"][0], light["position"][1], light["position"][2]);
			spotLight.m_Direction = glm::vec3(light["direction"][0], light["direction"][1], light["direction"][2]);
            spotLight.m_Direction = -glm::normalize(spotLight.m_Direction);
            spotLight.m_Color = glm::vec3(light["color"][0], light["color"][1], light["color"][2]);
            spotLight.m_Intensity = light["intensity"];
            spotLight.m_InnerCutOff = glm::radians<float>(light["innercutoff"]);
			spotLight.m_OuterCutOff = glm::radians<float>(light["outerCutoff"]);
            spotLight.m_Radius = light["radius"];
            m_LightManager.AddSpotLight(spotLight);
		}
    }

    m_LightManager.CreateLightUniformData(device);
}

void VansGraphics::VansScene::LoadRenderNodes(VkDevice& device, json& render_node)
{
    for (const auto& sceneRenderNode : render_node)
    {
        RenderNodeType type = sceneRenderNode["type"];
        std::string meshName = sceneRenderNode.value("mesh", "");

        // ── Resolve mesh ──────────────────────────────────────────────────────
        VansMesh* mesh = static_cast<VansMesh*>(GetMeshAsset(meshName));

        std::string materialName = sceneRenderNode.value("material", "");
        VansMaterial* material = static_cast<VansMaterial*>(GetMaterialAsset(materialName));

        // ── Standard render node creation ─────────────────────────────────────
        VansRenderNode* renderNode = nullptr;
        switch (type)
        {
        case VansGraphics::NONE_NODE:
            break;
        case VansGraphics::OPAQUE_NODE:
        case VansGraphics::TRANSPARENT_NODE:
            renderNode = new VansCommonRenderNode(device, type);
            if (sceneRenderNode.contains("support_shadow"))
            {
                auto* node = static_cast<VansCommonRenderNode*>(renderNode);
                node->m_SupportShadow = sceneRenderNode["support_shadow"];
            }
            break;
        case VansGraphics::POSTPROCESS_NODE:
            renderNode = new VansPostProcessRenderNode(device, type);
            break;
        case VansGraphics::SKY_BOX_NODE:
            renderNode = new VansSkyBoxRenderNode(device, type);
            break;
        default:
            break;
        }

        if (renderNode == nullptr)
        {
            continue;
        }

        // Multi-mesh nodes are rendered via a custom path; skip default passes.
        renderNode->m_SkipDefaultRender = (mesh && mesh->m_IsMultiMesh);

        if (sceneRenderNode.contains("transform"))
        {
            auto& transform = sceneRenderNode["transform"];
            glm::vec3 postion  = glm::vec3(transform["position"][0], transform["position"][1], transform["position"][2]);
            glm::vec3 rotation = glm::vec3(transform["rotation"][0], transform["rotation"][1], transform["rotation"][2]);
            glm::vec3 scale    = glm::vec3(transform["scale"][0],    transform["scale"][1],    transform["scale"][2]);
            renderNode->SetTransformData(postion, rotation, scale);
        }

        renderNode->m_Mesh     = mesh;
        renderNode->m_Material = material;
        renderNode->SetName(sceneRenderNode["name"]);

        // ── Multi-mesh: build per-submesh material list ───────────────────────
        if (mesh && mesh->m_IsMultiMesh && sceneRenderNode.contains("submeshMaterials"))
        {
            std::unordered_map<std::string, std::string> matOverrides;
            for (auto& [key, val] : sceneRenderNode["submeshMaterials"].items())
                matOverrides[key] = val.get<std::string>();

            for (auto* sub : mesh->m_SubMeshes)
            {
                VansMaterial* mat = nullptr;
                auto it = matOverrides.find(sub->m_SourceMaterialName);
                if (it != matOverrides.end())
                    mat = static_cast<VansMaterial*>(GetMaterialAsset(it->second));
                renderNode->m_MaterialList.push_back(mat);
            }
        }

        RegistRenderNode(renderNode, type);

        // ── Shadow nodes ──────────────────────────────────────────────────────
        VansMaterial* shadowMaterial = static_cast<VansMaterial*>(GetMaterialAsset("ShadowMaterial"));
        if (type == OPAQUE_NODE && shadowMaterial != nullptr)
        {
            auto* node = static_cast<VansCommonRenderNode*>(renderNode);
            if (node->m_SupportShadow)
            {
                VansRenderNode* shadowNode = new VansShadowRenderNode(device);
                shadowNode->m_Mesh     = mesh;
                shadowNode->m_Material = shadowMaterial;
                shadowNode->SetTransformData(node->GetTransformPosition(), node->GetTransformRotation(), node->GetTransformScale());
                shadowNode->SetName("shadow");
                shadowNode->m_SkipDefaultRender = renderNode->m_SkipDefaultRender;
                m_ShadowRenderNodes.push_back(shadowNode);
            }
        }

        VansMaterial* punctualShadowMaterial = static_cast<VansMaterial*>(GetMaterialAsset("PunctualShadowMaterial"));
        if (type == OPAQUE_NODE && punctualShadowMaterial != nullptr)
        {
            auto* node = static_cast<VansCommonRenderNode*>(renderNode);
            if (node->m_SupportShadow)
            {
                VansRenderNode* shadowNode = new VansShadowRenderNode(device);
                shadowNode->m_Mesh     = mesh;
                shadowNode->m_Material = punctualShadowMaterial;
                shadowNode->SetTransformData(node->GetTransformPosition(), node->GetTransformRotation(), node->GetTransformScale());
                shadowNode->SetName("punctual_shadow");
                shadowNode->m_SkipDefaultRender = renderNode->m_SkipDefaultRender;
                m_PunctualShadowRenderNodes.push_back(shadowNode);
            }
        }
    }
}

void VansGraphics::VansScene::AddTerrainNode(VansVKDevice* device, json& terrainData)
{
    auto vansConfigration = VansConfigration::GetInstance();
    std::string projectRoot = vansConfigration->GetProjectRootPath();

    TerrainConfig config;

    // Heightmap (required)
    config.heightmapPath = projectRoot + terrainData["heightmap"].get<std::string>();

    // Splatmaps (required, array of 2)
    if (terrainData.contains("splatmaps") && terrainData["splatmaps"].is_array())
    {
        auto& splatmaps = terrainData["splatmaps"];
        if (splatmaps.size() >= 1)
            config.splatmap0Path = projectRoot + splatmaps[0].get<std::string>();
        if (splatmaps.size() >= 2)
            config.splatmap1Path = projectRoot + splatmaps[1].get<std::string>();
    }

    // Layers (up to 8)
    if (terrainData.contains("layers") && terrainData["layers"].is_array())
    {
        for (auto& layerJson : terrainData["layers"])
        {
            TerrainLayerConfig layer;

            // Support texture name references (look up from scene texture manager)
            if (layerJson.contains("albedo_texture"))
            {
                std::string texName = layerJson["albedo_texture"].get<std::string>();
                layer.albedoTex = static_cast<VansTexture*>(GetTextureAsset(texName));
            }
            else if (layerJson.contains("albedo"))
                layer.albedoPath = projectRoot + layerJson["albedo"].get<std::string>();

            if (layerJson.contains("normal_texture"))
            {
                std::string texName = layerJson["normal_texture"].get<std::string>();
                layer.normalTex = static_cast<VansTexture*>(GetTextureAsset(texName));
            }
            else if (layerJson.contains("normal"))
                layer.normalPath = projectRoot + layerJson["normal"].get<std::string>();

            if (layerJson.contains("roughness_texture"))
            {
                std::string texName = layerJson["roughness_texture"].get<std::string>();
                layer.roughnessTex = static_cast<VansTexture*>(GetTextureAsset(texName));
            }
            else if (layerJson.contains("roughness"))
                layer.roughnessPath = projectRoot + layerJson["roughness"].get<std::string>();

            if (layerJson.contains("tiling"))
                layer.tiling = layerJson["tiling"].get<float>();
            config.layers.push_back(layer);
        }
    }

    RenderNodeType type = RenderNodeType::TERRAIN_NODE;
    VansRenderNode* renderNode = new VansTerrainRenderNode(device, config, type);

    // Read optional name
    std::string name = "TerrainNode";
    if (terrainData.contains("name"))
    {
        name = terrainData["name"].get<std::string>();
    }
    renderNode->SetName(name);
    RegistRenderNode(renderNode, type);
}

VansGraphics::VansRenderNode* VansGraphics::VansScene::FindRenderNodeByName(const std::string& name)
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

void VansGraphics::VansScene::InitVehicle(VansEngine::VansPhysicsSystem* physicsSystem, const glm::vec3& position,
    const std::string& bodyRenderNodeName, const std::vector<std::string>& tireRenderNodeNames)
{
    if (m_Vehicle) return; // Already initialized

    m_Vehicle = new VansEngine::VansPhysicsVehicle();
    m_Vehicle->SetBodyRenderNodeName(bodyRenderNodeName);
    m_Vehicle->SetTireRenderNodeNames(tireRenderNodeNames);
    // Convert glm::vec3 to PxTransform (physx uses right-handed Y-up, similar to GLM standard)
    PxTransform startPose(PxVec3(position.x, position.y + 2.0f, position.z), PxQuat(PxIdentity));

    // Initialize with default parameters (empty path triggers built-in defaults)
    m_Vehicle->Initialize(physicsSystem, "", startPose);

    VANS_LOG("[VansScene] Vehicle initialized at " << position.x << ", " << position.y << ", " << position.z
              << ", bodyNode='" << bodyRenderNodeName << "', tires=" << tireRenderNodeNames.size());
}

void VansGraphics::VansScene::AddDeferredNode(VkDevice& device)
{
    VansMesh* mesh = static_cast<VansMesh*>(GetMeshAsset("fullScreenQuad"));
    std::string materialName = "DeferredMaterial";
    VansMaterial* material = static_cast<VansMaterial*>(GetMaterialAsset(materialName));

    RenderNodeType type = RenderNodeType::DEFERRED_NODE;
    VansRenderNode* renderNode = new VansDeferredRenderNode(device, type);

    renderNode->m_Mesh = mesh;
    renderNode->m_Material = material;

    //renderNode->CreateDescriptorSets(m_Camera, m_LightManager, m_MaterialManager);

    renderNode->SetName("DeferredNode");

    RegistRenderNode(renderNode, type);
}

void VansGraphics::VansScene::AddScreenSpaceFeatureNode(VkDevice& device)
{
    VansMesh* mesh = static_cast<VansMesh*>(GetMeshAsset("fullScreenQuad"));

    std::vector<std::string> featureName = {
        "SSAO"
    };

    for (auto& name : featureName)
    {
        std::string materialName = name;
        VansMaterial* material = static_cast<VansMaterial*>(GetMaterialAsset(materialName));

        RenderNodeType type = RenderNodeType::SCREEN_SPACE_NODE;
        VansRenderNode* renderNode = new VansScreenSpaceRenderNode(device, type);

        renderNode->m_Mesh = mesh;
        renderNode->m_Material = material;

        //renderNode->CreateDescriptorSets(m_Camera, m_LightManager,m_MaterialManager);

        renderNode->SetName(name);

        RegistRenderNode(renderNode, type);
    }
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

    //delete mesh;
    //delete shader;
    //delete fullScreenMesh;
    //delete fullScreenShader;
    //delete m_Texture;
}

void VansGraphics::VansScene::UpdateSceneData()
{
    m_LightManager.UpdateLightShadowMatrixData();

    m_LightManager.UpdateLightCPUData();

    // Update dirty physics transforms to GPU
    UpdateTransformRenderData();

    //update material data
    UpdateRenderNodesDataBeforeRecord();
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
        }
    };

    updateNode(m_SkyBoxNode);
    updateNode(m_DeferredNode);
    updateNode(m_TerrainRenderNode);

    for (auto* node : m_OpaqueRenderNodes)
    {
        updateNode(node);
    }
    for (auto* node : m_TransParentRenderNodes)
    {
        updateNode(node);
    }
    for (auto* node : m_PostProcessRenderNodes)
    {
        updateNode(node);
    }
    for (auto* node : m_ScreenSpaceRenderNodes)
    {
        updateNode(node);
    }
    for (auto* node : m_ShadowRenderNodes)
    {
        updateNode(node);
    }
    for (auto* node : m_PunctualShadowRenderNodes)
    {
        updateNode(node);
    }
}

void VansGraphics::VansScene::ImportDefaultTextures(const std::string& path, const std::string& name, VansVKDevice* vkDevice, bool isSRGB)
{
    //默认pbr贴图
    std::string texturePath = path;
    VansTexture* defaultMetalTexture = new VansTexture();
    defaultMetalTexture->m_TextureType = TEXTURE_2D;
    defaultMetalTexture->LoadTexture(vkDevice->GetCommandBuffer(), texturePath, isSRGB, true, true);
    m_Textures.push_back(defaultMetalTexture);
    defaultMetalTexture->SetName(name);
}

void VansGraphics::VansScene::LoadSceneResource(json& sceneData)
{
    auto vansConfigration = VansConfigration::GetInstance();
    std::string pathPrefix = vansConfigration->GetProjectRootPath();
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkDevice nativeDevice = vkDevice->GetLogicDevice();

    json sceneMeshes = sceneData["mesh"];
    json sceneShaders = sceneData["shader"];
    json sceneTextures = sceneData["texture"];
    json sceneMaterials = sceneData["material"];

    for (const auto& sceneMesh : sceneMeshes)
    {
        std::string meshPath = pathPrefix + std::string(sceneMesh["path"]);
        std::string meshType = sceneMesh.value("mesh_type", "single_mesh");
        bool import_tangent = sceneMesh.value("need_tangent", false);

        // Multi-mesh assets are loaded up front and will be skipped by default passes.
        if (meshType == "multi_mesh")
        {
            VansMesh* mesh = new VansMesh(/*needCPUData=*/false, /*supportRayTracing=*/false);
            mesh->LoadMultiMesh(nativeDevice, vkDevice->GetGraphicsQueue(), &(vkDevice->GetCommandBuffer()), meshPath, import_tangent);
            mesh->SetName(sceneMesh["name"]);
            m_Meshes.push_back(mesh);
            continue;
        }

        bool generate_as = false;
        if (sceneMesh.contains("support_raytracing"))
        {
            generate_as = sceneMesh["support_raytracing"];
        }
		bool needCpuData = false;
        if (sceneMesh.contains("need_cpu_data"))
        {
			needCpuData = sceneMesh["need_cpu_data"];
        }
        VansMesh* mesh = new VansMesh(needCpuData, generate_as);

        mesh->LoadMesh(nativeDevice, vkDevice->GetGraphicsQueue(), &(vkDevice->GetCommandBuffer()), meshPath.c_str(), import_tangent);
        m_Meshes.push_back(mesh);
        mesh->SetName(sceneMesh["name"]);
    }


    for (const auto& sceneShader : sceneShaders)
    {
        std::string shaderPath = pathPrefix + std::string(sceneShader["path"]);
        VkBool32 depthTest = sceneShader["depthTest"];
        VkBool32 depthWrite = sceneShader["depthWrite"];
        VkCompareOp depthCompareOp = sceneShader["depthCompareOp"];
        VkCullModeFlags cullMode = sceneShader["cullMode"];

        VansGraphicsShader* shader = new VansGraphicsShader();

		//监控shader文件变化
        m_SceneFileWatcher->AddWatch(shaderPath);

        shader->InitShader(nativeDevice, shaderPath);
        shader->SetDrawStateData(depthTest, depthWrite, depthCompareOp, cullMode);
        if (sceneShader.contains("support_push_constant"))
        {
            int pushConstantSize = sceneShader["support_push_constant"];
            shader->SetPushConstant(pushConstantSize);
        }
        m_Shaders.push_back(shader);
        shader->SetName(sceneShader["name"]);
    }


    for (const auto& sceneTexture : sceneTextures)
    {
        std::string texturePath = pathPrefix + std::string(sceneTexture["path"]);
        VansTexture* texture = new VansTexture();
        texture->m_TextureType = sceneTexture["type"];
        bool isSRGB = sceneTexture["sRGB"];
        switch (texture->m_TextureType)
        {
        case TEXTURE_2D:
            texture->LoadTexture(vkDevice->GetCommandBuffer(), texturePath, isSRGB,true,true);
            break;
        case TEXTURE_CUBE:
            texture->LoadCubeTexture(vkDevice->GetCommandBuffer(), texturePath, isSRGB);
            break;
        default:
            break;
        }

        m_Textures.push_back(texture);
        texture->SetName(sceneTexture["name"]);
    }

    ImportDefaultTextures(pathPrefix + "EngineAssets/Textures/Default/defaultAlbedo.png", "defaultAlbedo", vkDevice, false);
    ImportDefaultTextures(pathPrefix + "EngineAssets/Textures/Default/defaultMetal.png", "defaultMetal", vkDevice, false);
    ImportDefaultTextures(pathPrefix + "EngineAssets/Textures/Default/defaultRoughness.png","defaultRoughness", vkDevice, false);
    ImportDefaultTextures(pathPrefix + "EngineAssets/Textures/Default/defaultAo.png","defaultAo", vkDevice, false);
    ImportDefaultTextures(pathPrefix + "EngineAssets/Textures/Default/defaultNormal.png","defaultNormal", vkDevice, false);

    for (const auto& sceneMaterial : sceneMaterials)
    {
        VansMaterial* material = new VansMaterial();

        std::string shaderName = sceneMaterial["shader"];
        VansGraphicsShader* shader = static_cast<VansGraphicsShader*>(GetShaderAsset(shaderName));

        material->m_Shader = shader;
        material->m_MaterialType = sceneMaterial["type"];
        if (material->m_MaterialType == VansMaterialType::VAN_PBR)
        {
            if (sceneMaterial.contains("basecolor_texture"))
            {
                auto textureName = sceneMaterial["basecolor_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                {
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultAlbedo"));
                }
                material->m_BaseColorTexture = texture;
            }
            if (sceneMaterial.contains("normal_texture"))
            {
                auto textureName = sceneMaterial["normal_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                {
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultNormal"));
                }
                material->m_NormalTexture = texture;
            }
            if (sceneMaterial.contains("metal_texture"))
            {
                auto textureName = sceneMaterial["metal_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                {
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultMetal"));
                }
                material->m_MetalTexture = texture;
            }
            if (sceneMaterial.contains("roughness_texture"))
            {
                auto textureName = sceneMaterial["roughness_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                {
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultRoughness"));
                }
                material->m_RoughnessTexture = texture;
            }
            if (sceneMaterial.contains("ao_texture"))
            {
                auto textureName = sceneMaterial["ao_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                {
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultAo"));
                }
                material->m_AoTexture = texture;
            }

            //如果没有使用默认贴图

            material->m_BasePBRParam.m_albedo = glm::vec3(sceneMaterial["albedo"][0], sceneMaterial["albedo"][1], sceneMaterial["albedo"][2]);
            material->m_BasePBRParam.m_metallic = sceneMaterial["metallic"];
            material->m_BasePBRParam.m_roughness = sceneMaterial["roughness"];
            material->m_BasePBRParam.m_ao = sceneMaterial["ao"];
        }

        if (material->m_MaterialType == VansMaterialType::VAN_SKY_BOX)
        {
            material->m_AtmospherePBRParam.m_PlanetRadius = 6340000;
            material->m_AtmospherePBRParam.m_InitSeaLevel = 200;
            material->m_AtmospherePBRParam.m_AtmosphereWidth = 80000;
            material->m_AtmospherePBRParam.m_RayleighScalarHeight = 8500;
            material->m_AtmospherePBRParam.m_MieScalarHeight = 1200;
            material->m_AtmospherePBRParam.m_MieAnisotropy = 0.78;
            material->m_AtmospherePBRParam.m_OzoneLevelCenterHeight = 25000;
            material->m_AtmospherePBRParam.m_OzoneLevelWidth = 15000;
            material->m_AtmospherePBRParam.m_SunLuminance = 10;
        }
        m_Materials.push_back(material);
        material->SetName(sceneMaterial["name"]);
    }
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
			m_TlasInstanceTextures.push_back(node->m_Material->m_BaseColorTexture->GetImage());
			m_TlasInstanceTextures.push_back(node->m_Material->m_NormalTexture->GetImage());
			m_TlasInstanceTextures.push_back(node->m_Material->m_MetalTexture->GetImage());
			m_TlasInstanceTextures.push_back(node->m_Material->m_RoughnessTexture->GetImage());
			m_TlasInstanceTextures.push_back(node->m_Material->m_AoTexture->GetImage());
        }
        else
        {
			textureIndex = textureIndexIT->second;
        }
        m_TlasInstanceTextureIndex.push_back(textureIndex);
    }

    uint32_t countInstance = static_cast<uint32_t>(m_TlasInstancesInfos.size());

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

void VansGraphics::VansScene::DrawShadowNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node : m_ShadowRenderNodes)
    {
        if (node == nullptr || node->m_SkipDefaultRender)
        {
            continue;
        }
        node->Draw(cmd, globalStateData);
    }
}

void VansGraphics::VansScene::DrawPointShadow(int lightIndex)
{
    auto vansConfigration = VansConfigration::GetInstance();
    float punctualShadowSize = vansConfigration->GetPunctualShadowMapWidth();
    float patchShadowSize = punctualShadowSize / 8;

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (int shadowDirection = 0; shadowDirection < 6; shadowDirection++)
    {
        float regionOffsetX = (lightIndex * 6 + shadowDirection) % 8 * patchShadowSize;
        float regionOffsetY = (lightIndex * 6 + shadowDirection) / 8 * patchShadowSize;

        VkViewport viewPort = {};
        viewPort.x = regionOffsetX;
        viewPort.y = regionOffsetY;
        viewPort.width = patchShadowSize;
        viewPort.height = patchShadowSize;
        viewPort.minDepth = 0.0f;
        viewPort.maxDepth = 1.0f;

        VkRect2D scissor = {};
        scissor.offset = { (int)(regionOffsetX), (int)(regionOffsetY) };
        scissor.extent = { (uint32_t)(patchShadowSize), (uint32_t)(patchShadowSize) };

        cmd.SetViewport(0, { viewPort });
        cmd.SetScissor(0, { scissor });

        for (auto& node : m_PunctualShadowRenderNodes)
        {
            if (node == nullptr || node->m_SkipDefaultRender)
            {
                continue;
            }
            node->DrawPunctualShadow(cmd, globalStateData, lightIndex, shadowDirection);
        }
    }
}

void VansGraphics::VansScene::DrawSpotShadow(int pointCount, int lightIndex)
{
    auto vansConfigration = VansConfigration::GetInstance();
    float punctualShadowSize = vansConfigration->GetPunctualShadowMapWidth();
    float patchShadowSize = punctualShadowSize / 8;

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();

    float regionOffsetX = (pointCount * 6 + lightIndex) % 8 * patchShadowSize;
    float regionOffsetY = (pointCount * 6 + lightIndex) / 8 * patchShadowSize;

    VkViewport viewPort = {};
    viewPort.x = regionOffsetX;
    viewPort.y = regionOffsetY;
    viewPort.width = patchShadowSize;
    viewPort.height = patchShadowSize;
    viewPort.minDepth = 0.0f;
    viewPort.maxDepth = 1.0f;

    VkRect2D scissor = {};
    scissor.offset = { (int)(regionOffsetX), (int)(regionOffsetY) };
    scissor.extent = { (uint32_t)(patchShadowSize), (uint32_t)(patchShadowSize) };

    cmd.SetViewport(0, { viewPort });
    cmd.SetScissor(0, { scissor });

    for (auto& node : m_PunctualShadowRenderNodes)
    {
        if (node == nullptr || node->m_SkipDefaultRender)
        {
            continue;
        }
        node->DrawPunctualShadow(cmd, globalStateData, pointCount + lightIndex, 0);
    }
}

void VansGraphics::VansScene::DrawSkyBoxNode()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    m_SkyBoxNode->Draw(cmd, globalStateData);
}

void VansGraphics::VansScene::DrawOpaqueNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node : m_OpaqueRenderNodes)
    {
        if (node == nullptr || node->m_SkipDefaultRender)
        {
            continue;
        }
        node->Draw(cmd, globalStateData);
    }
}

void VansGraphics::VansScene::DrawTerrainNode(bool shadowPass)
{
    if(m_TerrainRenderNode== nullptr)
    {
        return;
	}
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    if (shadowPass)
    {
        static_cast<VansTerrainRenderNode*>(m_TerrainRenderNode)->DrawShadow(cmd, globalStateData);
    }
    else
    {
        static_cast<VansTerrainRenderNode*>(m_TerrainRenderNode)->Draw(cmd, globalStateData);
    }
    
}

void VansGraphics::VansScene::DrawTransParentNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node : m_TransParentRenderNodes)
    {

        ////apply mesh
        //cmd.BindMesh(*node.m_Mesh, 0, globalStateData);

        ////apply shader，确认pipeline以及创建完毕
        //cmd.BindShader(*(node.m_Material->m_Shader), globalStateData, { uniformBufferLayout,textureResourceLayout });

        //cmd.BindDescriptorSets(VK_PIPELINE_BIND_POINT_GRAPHICS, *(node.m_Material->m_Shader), 0, { uniformBufferDescriptorSets[0],textureResourceDescriptorSets[0] }, {});

        //cmd.DrawMesh(*node.m_Mesh, *(node.m_Material->m_Shader), 1);
    }
}

void VansGraphics::VansScene::DrawPostProcessNodes()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node  : m_PostProcessRenderNodes)
    {
        //apply mesh
        node->Draw(cmd, globalStateData);
    }
}

//ssao
//ssr
//contact shadow
void VansGraphics::VansScene::DrawScreenSpaceFeatureNode()
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();
    for (auto& node : m_ScreenSpaceRenderNodes)
    {
        //apply mesh
        node->Draw(cmd, globalStateData);
    }
}

void VansGraphics::VansScene::DeferredShading()
{
    //绘制全屏mesh
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VansVKCommandBuffer cmd = vkDevice->GetCommandBuffer();
    GlobalStateData globalStateData = vkDevice->GetGlobalRenderStateData();

    m_DeferredNode->Draw(cmd, globalStateData);
}

void VansGraphics::VansScene::LoadPhysicsNodes(json& physics_node)
{
    using namespace VansEngine;

    for (const auto& physicsNodeJson : physics_node)
    {
        // ── Vehicle physics node ─────────────────────────────────────────────
        if (physicsNodeJson.contains("nodeType") && physicsNodeJson["nodeType"].get<std::string>() == "vehicle")
        {
            if (m_Vehicle)
            {
                VANS_LOG_WARN("[VansScene] LoadPhysicsNodes: vehicle node already initialized, skipping.");
                continue;
            }

            // Spawn position (optional, defaults to safe above-ground origin)
            glm::vec3 spawnPos(0.0f, 5.0f, 0.0f);
            if (physicsNodeJson.contains("position"))
            {
                auto& p = physicsNodeJson["position"];
                spawnPos = glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
            }

            // Car body render node name (used to update body mesh transform each frame)
            std::string bodyNodeName;
            if (physicsNodeJson.contains("bodyRenderNode"))
                bodyNodeName = physicsNodeJson["bodyRenderNode"].get<std::string>();

            // Tire render node names, ordered by wheel index (0=FL, 1=FR, 2=RL, 3=RR)
            std::vector<std::string> tireNodeNames;
            if (physicsNodeJson.contains("tireRenderNodes"))
            {
                for (const auto& t : physicsNodeJson["tireRenderNodes"])
                    tireNodeNames.push_back(t.get<std::string>());
            }

            InitVehicle(&VansPhysicsSystem::GetInstance(), spawnPos, bodyNodeName, tireNodeNames);
            continue;
        }
        // ── Regular physics node ─────────────────────────────────────────────

        // Parse physics properties from JSON
        PhysicsNodeProperties properties;

        // Required fields
        if (!physicsNodeJson.contains("enabled"))
        {
            continue; // Skip if not marked for physics
        }
        
        properties.enabled = physicsNodeJson["enabled"];
        
        if (!properties.enabled)
        {
            continue; // Skip disabled physics nodes
        }

        // Body type: "static", "dynamic", "kinematic"
        if (physicsNodeJson.contains("bodyType"))
        {
            std::string bodyTypeStr = physicsNodeJson["bodyType"];
            if (bodyTypeStr == "static")
                properties.bodyType = PhysicsBodyType::Static;
            else if (bodyTypeStr == "dynamic")
                properties.bodyType = PhysicsBodyType::Dynamic;
            else if (bodyTypeStr == "kinematic")
                properties.bodyType = PhysicsBodyType::Kinematic;
        }

        // Collider type: "box", "sphere", "capsule", "mesh", "convex"
        if (physicsNodeJson.contains("colliderType"))
        {
            std::string colliderTypeStr = physicsNodeJson["colliderType"];
            if (colliderTypeStr == "box")
                properties.colliderType = PhysicsColliderType::Box;
            else if (colliderTypeStr == "sphere")
                properties.colliderType = PhysicsColliderType::Sphere;
            else if (colliderTypeStr == "capsule")
                properties.colliderType = PhysicsColliderType::Capsule;
            else if (colliderTypeStr == "mesh")
                properties.colliderType = PhysicsColliderType::Mesh;
            else if (colliderTypeStr == "convex")
                properties.colliderType = PhysicsColliderType::ConvexMesh;
        }

        // Mass (for dynamic objects)
        if (physicsNodeJson.contains("mass"))
        {
            properties.mass = physicsNodeJson["mass"];
        }

        // Use mesh collider flag
        if (physicsNodeJson.contains("useMeshCollider"))
        {
            properties.useMeshCollider = physicsNodeJson["useMeshCollider"];
        }

        // Convex decomposition flag
        if (physicsNodeJson.contains("useConvexDecomposition"))
        {
            properties.useConvexDecomposition = physicsNodeJson["useConvexDecomposition"];
        }

        // Physics material properties
        if (physicsNodeJson.contains("material"))
        {
            auto& materialJson = physicsNodeJson["material"];
            if (materialJson.contains("staticFriction"))
                properties.material.staticFriction = materialJson["staticFriction"];
            if (materialJson.contains("dynamicFriction"))
                properties.material.dynamicFriction = materialJson["dynamicFriction"];
            if (materialJson.contains("restitution"))
                properties.material.restitution = materialJson["restitution"];
        }

        // Collision shape parameters
        if (physicsNodeJson.contains("boxExtents"))
        {
            auto& extents = physicsNodeJson["boxExtents"];
            properties.boxExtents = glm::vec3(extents[0], extents[1], extents[2]);
        }

        if (physicsNodeJson.contains("sphereRadius"))
        {
            properties.sphereRadius = physicsNodeJson["sphereRadius"];
        }

        if (physicsNodeJson.contains("capsuleRadius"))
        {
            properties.capsuleRadius = physicsNodeJson["capsuleRadius"];
        }

        if (physicsNodeJson.contains("capsuleHalfHeight"))
        {
            properties.capsuleHalfHeight = physicsNodeJson["capsuleHalfHeight"];
        }

        // Get transform ID (link to existing render node)
        uint32_t transformID = 0;
        if (physicsNodeJson.contains("transformID"))
        {
            transformID = physicsNodeJson["transformID"];
        }
        else if (physicsNodeJson.contains("name"))
        {
            // Try to find matching render node by name
            std::string nodeName = physicsNodeJson["name"];
            for (auto* renderNode : m_OpaqueRenderNodes)
            {
                if (renderNode->m_NodeName == nodeName)
                {
                    transformID = renderNode->m_TransformID;
                    break;
                }
            }
            for (auto* renderNode : m_TransParentRenderNodes)
            {
                if (renderNode->m_NodeName == nodeName)
                {
                    transformID = renderNode->m_TransformID;
                    break;
                }
            }
        }

        // Get mesh reference if needed
        VansMesh* mesh = nullptr;
        if (properties.useMeshCollider && physicsNodeJson.contains("mesh"))
        {
            std::string meshName = physicsNodeJson["mesh"];
            mesh = static_cast<VansMesh*>(GetMeshAsset(meshName));
        }

        // Create physics node
        VansPhysicsNode* physicsNode = new VansPhysicsNode();
        physicsNode->Initialize(properties, transformID, mesh);

        if (physicsNodeJson.contains("name"))
        {
            physicsNode->SetName(physicsNodeJson["name"]);
        }

        m_PhysicsNodes.push_back(physicsNode);
    }
}

void VansGraphics::VansScene::UpdatePhysicsTransforms()
{
    using namespace VansEngine;
    
    // Get physics system
    VansPhysicsSystem& physics = VansPhysicsSystem::GetInstance();
    
    // 1. Acquire the simulation lock FIRST
    // This blocks if the simulation thread is currently inside its update loop (simulate -> fetchResults)
    // Once we have this lock, we know the simulation thread is waiting or sleeping, and NOT writing to the scene.
    // Use std::lock_guard or std::unique_lock with the mutex
    std::lock_guard<std::mutex> simLock(physics.GetSimulationMutex());

    PxScene* scene = physics.GetScene();
    if (!scene)
        return;
    
    // 2. Now it is safe to acquire the PhysX read lock
    // Even though we have the mutex, proper PhysX usage still prefers using the ReadLock
    // just in case internal PxScene operations require it.
    PxSceneReadLock scopedLock(*scene);
    
    // Update all physics nodes from physics simulation
    for (auto* physicsNode : m_PhysicsNodes)
    {
        if (physicsNode && physicsNode->IsEnabled())
        {
            if (physicsNode->UpdateTransformFromPhysics())
            {
                // Record the transform ID if it has changed
                uint32_t transformID = physicsNode->GetTransformID();
                if (transformID != 0) // Invalid ID check
                {
					VansGraphics::VansTransformStore::TransformIDToTransformDirty.insert({ transformID, true });
                }
            }
        }
    }

    // ── Update vehicle render node transforms ────────────────────────────────
    if (m_Vehicle)
    {
        // Helper: convert PxQuat to Euler angles in degrees for VansTransform
        auto PxQuatToEulerDeg = [](const PxQuat& q) -> glm::vec3
        {
            glm::quat gq(q.w, q.x, q.y, q.z);
            return glm::degrees(glm::eulerAngles(gq));
        };

        // Update car body render node
        const std::string& bodyNodeName = m_Vehicle->GetBodyRenderNodeName();
        if (!bodyNodeName.empty())
        {
            VansRenderNode* bodyNode = FindRenderNodeByName(bodyNodeName);
            if (bodyNode)
            {
                const PxTransform& bodyPose = m_Vehicle->GetTransform();
                VansTransform& t = VansTransformStore::GetTransform(bodyNode->m_TransformID);
                t.m_Position = glm::vec3(bodyPose.p.x, bodyPose.p.y, bodyPose.p.z);
                t.m_Rotation = PxQuatToEulerDeg(bodyPose.q);
                VansTransformStore::TransformIDToTransformDirty.insert({ bodyNode->m_TransformID, true });
            }
        }

        // Update tire render nodes (one per wheel index)
        const std::vector<std::string>& tireNodeNames = m_Vehicle->GetTireRenderNodeNames();
        const uint32_t numTires = static_cast<uint32_t>(tireNodeNames.size());
        for (uint32_t wi = 0; wi < numTires; ++wi)
        {
            const std::string& tireName = tireNodeNames[wi];
            if (tireName.empty()) continue;
            VansRenderNode* tireNode = FindRenderNodeByName(tireName);
            if (!tireNode) continue;

            PxTransform wheelPose = m_Vehicle->GetWheelWorldPose(wi);
            VansTransform& t = VansTransformStore::GetTransform(tireNode->m_TransformID);
            t.m_Position = glm::vec3(wheelPose.p.x, wheelPose.p.y, wheelPose.p.z);
            t.m_Rotation = PxQuatToEulerDeg(wheelPose.q);
            VansTransformStore::TransformIDToTransformDirty.insert({ tireNode->m_TransformID, true });
        }
    }
}

void VansGraphics::VansScene::UpdateTransformRenderData()
{
    for (auto node : m_OpaqueRenderNodes)
    {
        node->UpdateModelData();
	}
    VansGraphics::VansTransformStore::TransformIDToTransformDirty.clear();
}

VansGraphics::VansScene* m_Scene = nullptr;

VansAssetsFileWatcher* m_SceneFileWatcher = nullptr;