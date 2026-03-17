#include "../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansScene.h"
#include "VansShaderRegistry.h"
#include "BRDFData/VansLight.h"
#include "../Configration/VansConfigration.h"

#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "TerrainCore/VansTerrain.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../AnimationCore/VansSkinnedMeshLoader.h"

#include "../../EngineCore/EditorCore/AssetsSystem/VansAssetsFileWatcher.h"
#include "../Util/VansLog.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <filesystem>

// ---------------------------------------------------------------------------
// JSON type-string helpers
// ---------------------------------------------------------------------------
static VansMaterialType ParseMaterialType(const json& typeValue, const std::string& materialName)
{
    if (typeValue.is_number_integer())
        return static_cast<VansMaterialType>(typeValue.get<int>());
    if (typeValue.is_string())
    {
        const std::string s = typeValue.get<std::string>();
        if (s == "pbr")          return VansMaterialType::VAN_PBR;
        if (s == "coat")         return VansMaterialType::VAN_COAT;
        if (s == "transparent")  return VansMaterialType::VAN_TRANSPARENT;
        if (s == "post_process") return VansMaterialType::VAN_POST_PROCESS;
        if (s == "sky_box")      return VansMaterialType::VAN_SKY_BOX;
        if (s == "deferred")     return VansMaterialType::VAN_DEFERRED;
        if (s == "ssao")         return VansMaterialType::VAN_SCREEN_SPACE_AO;
        if (s == "shadow")       return VansMaterialType::VAN_SHAODW;
        if (s == "skin")         return VansMaterialType::VAN_SKIN;
        if (s == "cloth")        return VansMaterialType::VAN_CLOTH;
        VANS_LOG_WARN("[LoadSceneResource] Material '" << materialName << "': unknown type string '" << s << "', defaulting to pbr.");
    }
    return VansMaterialType::VAN_PBR;
}

static VansGraphics::RenderNodeType ParseRenderNodeType(const json& typeValue, const std::string& nodeName)
{
    if (typeValue.is_number_integer())
        return static_cast<VansGraphics::RenderNodeType>(typeValue.get<int>());
    if (typeValue.is_string())
    {
        const std::string s = typeValue.get<std::string>();
        if (s == "opaque")       return VansGraphics::OPAQUE_NODE;
        if (s == "transparent")  return VansGraphics::TRANSPARENT_NODE;
        if (s == "post_process") return VansGraphics::POSTPROCESS_NODE;
        if (s == "sky_box")      return VansGraphics::SKY_BOX_NODE;
        if (s == "deferred")     return VansGraphics::DEFERRED_NODE;
        if (s == "screen_space") return VansGraphics::SCREEN_SPACE_NODE;
        if (s == "terrain")      return VansGraphics::TERRAIN_NODE;
        if (s == "none")         return VansGraphics::NONE_NODE;
        VANS_LOG_WARN("[LoadRenderNodes] Node '" << nodeName << "': unknown type string '" << s << "', defaulting to none.");
    }
    return VansGraphics::NONE_NODE;
}

static bool HasMeshAssetName(const VansGraphics::VansScene& scene, const std::string& name)
{
    for (auto* mesh : scene.m_Meshes)
    {
        if (mesh && mesh->m_AssetName == name)
            return true;
    }
    return false;
}

static bool HasMaterialAssetName(const VansGraphics::VansScene& scene, const std::string& name)
{
    for (auto* material : scene.m_Materials)
    {
        if (material && material->m_AssetName == name)
            return true;
    }
    return false;
}

static std::string MakeUniqueMultiMeshGroupName(const VansGraphics::VansScene& scene, const std::string& baseName)
{
    std::string candidate = baseName;
    int suffix = 1;
    while (scene.m_MultiMeshGroups.find(candidate) != scene.m_MultiMeshGroups.end())
    {
        candidate = baseName + "_grp" + std::to_string(suffix++);
    }
    return candidate;
}

static std::string MakeUniqueMaterialName(const VansGraphics::VansScene& scene, const std::string& baseName)
{
    std::string candidate = baseName;
    int suffix = 1;
    while (HasMaterialAssetName(scene, candidate))
    {
        candidate = baseName + "_mat" + std::to_string(suffix++);
    }
    return candidate;
}

static std::string MakeUniqueRenderNodeName(const VansGraphics::VansScene& scene, const std::string& baseName)
{
    std::string candidate = baseName;
    int suffix = 1;
    while (scene.FindRenderNodeByName(candidate) != nullptr)
    {
        candidate = baseName + "_node" + std::to_string(suffix++);
    }
    return candidate;
}

static std::string MakeUniqueMeshName(const VansGraphics::VansScene& scene, const std::string& baseName)
{
    std::string candidate = baseName;
    int suffix = 1;
    while (HasMeshAssetName(scene, candidate))
    {
        candidate = baseName + "_mesh" + std::to_string(suffix++);
    }
    return candidate;
}

// ===========================================================================
// Light loading
// ===========================================================================

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

// ===========================================================================
// Render node loading from JSON
// ===========================================================================

void VansGraphics::VansScene::LoadRenderNodes(VkDevice& device, json& render_node)
{
    for (const auto& sceneRenderNode : render_node)
    {
        RenderNodeType type = ParseRenderNodeType(sceneRenderNode["type"], sceneRenderNode.value("name", "<unnamed>"));
        std::string meshName = sceneRenderNode.value("mesh", "");

        // ── Resolve mesh ──────────────────────────────────────────────────────
        VansMesh* mesh = static_cast<VansMesh*>(GetMeshAsset(meshName));

        // ── Multi-mesh auto-expansion ─────────────────────────────────────────
        // When the mesh is a multi-mesh container, automatically split it into
        // separate render nodes (one per submesh) with auto-created materials.
        if (mesh && mesh->m_IsMultiMesh)
        {
            glm::vec3 position(0), rotation(0), scale(1);
            if (sceneRenderNode.contains("transform"))
            {
                auto& transform = sceneRenderNode["transform"];
                position = glm::vec3(transform["position"][0], transform["position"][1], transform["position"][2]);
                rotation = glm::vec3(transform["rotation"][0], transform["rotation"][1], transform["rotation"][2]);
                scale    = glm::vec3(transform["scale"][0],    transform["scale"][1],    transform["scale"][2]);
            }
            bool supportShadow = sceneRenderNode.value("support_shadow", false);
            std::string parentName = sceneRenderNode.value("name", "MultiMesh");

            ExpandMultiMeshToRenderNodes(device, mesh, parentName, position, rotation, scale, supportShadow);

            // Remove the multi-mesh container from m_Meshes — it holds no real vertex data.
            auto meshIt = std::find(m_Meshes.begin(), m_Meshes.end(), static_cast<VansAsset*>(mesh));
            if (meshIt != m_Meshes.end())
                m_Meshes.erase(meshIt);

            continue; // The expand function already registered all sub-nodes
        }

        std::string materialName = sceneRenderNode.value("material", "");
        VansMaterial* material = static_cast<VansMaterial*>(GetMaterialAsset(materialName));

        // ── Standard render node creation ─────────────────────────────────────
        VansRenderNode* renderNode = nullptr;
        switch (type)
        {
        case VansGraphics::NONE_NODE:
            break;
        case VansGraphics::OPAQUE_NODE:
            renderNode = new VansCommonRenderNode(device, type);
            if (sceneRenderNode.contains("support_shadow"))
            {
                auto* node = static_cast<VansCommonRenderNode*>(renderNode);
                node->m_SupportShadow = sceneRenderNode["support_shadow"];
            }
            break;
        case VansGraphics::TRANSPARENT_NODE:
            renderNode = new VansTransparentRenderNode(device, type);
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
                m_PunctualShadowRenderNodes.push_back(shadowNode);
            }
        }
    }
}

// ===========================================================================
// Terrain node
// ===========================================================================

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

// ===========================================================================
// Deferred + Screen-space node builders
// ===========================================================================

void VansGraphics::VansScene::AddDeferredNode(VkDevice& device)
{
    VansMesh* mesh = static_cast<VansMesh*>(GetMeshAsset("fullScreenQuad"));

    // Build material directly from the already-loaded "Deferred" shader — no JSON material entry needed.
    VansGraphicsShader* deferredShader = static_cast<VansGraphicsShader*>(GetShaderAsset("Deferred"));
    if (deferredShader == nullptr)
    {
        VANS_LOG_WARN("[VansScene] AddDeferredNode: shader 'Deferred' not found, node skipped.");
        return;
    }
    VansDeferredMaterial* material = new VansDeferredMaterial();
    material->m_Shader = deferredShader;
    material->m_MaterialType = VansMaterialType::VAN_DEFERRED;
    material->SetName("DeferredMaterial");
    m_Materials.push_back(material);

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

    // Each entry: { node/material name, shader name, material type }.
    // Materials are built internally — no JSON material entries needed.
    struct FeatureEntry { const char* name; const char* shaderName; VansMaterialType matType; };
    static const FeatureEntry features[] =
    {
        { "SSAO", "SSAO", VansMaterialType::VAN_SCREEN_SPACE_AO },
    };

    for (const auto& feature : features)
    {
        VansGraphicsShader* shader = static_cast<VansGraphicsShader*>(GetShaderAsset(feature.shaderName));
        if (shader == nullptr)
        {
            VANS_LOG_WARN("[VansScene] AddScreenSpaceFeatureNode: shader '" << feature.shaderName << "' not found, node '" << feature.name << "' skipped.");
            continue;
        }

        // Typed factory for screen-space passes
        VansMaterial* material = nullptr;
        switch (feature.matType)
        {
        case VansMaterialType::VAN_SCREEN_SPACE_AO: material = new VansSSAOMaterial();        break;
        case VansMaterialType::VAN_POST_PROCESS:    material = new VansPostProcessMaterial(); break;
        default:                                    material = new VansMaterial();             break;
        }
        material->m_Shader = shader;
        material->m_MaterialType = feature.matType;
        material->SetName(feature.name);
        m_Materials.push_back(material);

        RenderNodeType type = RenderNodeType::SCREEN_SPACE_NODE;
        VansRenderNode* renderNode = new VansScreenSpaceRenderNode(device, type);

        renderNode->m_Mesh = mesh;
        renderNode->m_Material = material;

        //renderNode->CreateDescriptorSets(m_Camera, m_LightManager,m_MaterialManager);

        renderNode->SetName(feature.name);

        RegistRenderNode(renderNode, type);
    }
}

// ===========================================================================
// Resource loading (meshes, shaders, textures, materials)
// ===========================================================================

void VansGraphics::VansScene::LoadMeshesFromJson(
    const json& meshData,
    const std::string& pathPrefix,
    VkDevice& device,
    VansVKDevice* vkDevice)
{
    for (const auto& sceneMesh : meshData)
    {
        std::string meshPath    = pathPrefix + std::string(sceneMesh["path"]);
        bool import_tangent     = sceneMesh.value("need_tangent", false);
        bool loadMultiMesh      = sceneMesh.value("load_multi_mesh", false);

        if (loadMultiMesh)
        {
            bool generate_as = sceneMesh.value("support_raytracing", false);
            bool needCpuData = sceneMesh.value("need_cpu_data", false);
            VansMesh* mesh   = new VansMesh(needCpuData, /*supportRayTracing=*/false);

            // Optional external animation FBX — only animation clips are read from it;
            // bone weights and skeleton come from the origin model.
            if (sceneMesh.contains("extern_animation"))
            {
                std::string externAnimPath = pathPrefix + sceneMesh["extern_animation"].get<std::string>();
                mesh->m_ExternAnimationPath = externAnimPath;
            }

            mesh->m_RootMotion = sceneMesh.value("root_motion", false);
            if (sceneMesh.contains("root_bone"))
                mesh->m_RootBoneName = sceneMesh["root_bone"].get<std::string>();

            mesh->LoadMultiMesh(device, vkDevice->GetGraphicsQueue(), &(vkDevice->GetCommandBuffer()), meshPath, import_tangent, generate_as, needCpuData);
            mesh->SetName(sceneMesh["name"]);
            m_Meshes.push_back(mesh);
        }
        else
        {
            bool generate_as = sceneMesh.value("support_raytracing", false);
            bool needCpuData = sceneMesh.value("need_cpu_data", false);
            VansMesh* mesh   = new VansMesh(needCpuData, generate_as);
            mesh->LoadMesh(device, vkDevice->GetGraphicsQueue(), &(vkDevice->GetCommandBuffer()), meshPath.c_str(), import_tangent);
            mesh->SetName(sceneMesh["name"]);
            m_Meshes.push_back(mesh);
        }
    }
}

void VansGraphics::VansScene::LoadShadersFromRegistry(
    const std::string& pathPrefix,
    VkDevice& device)
{
    VansGraphics::VansShaderRegistry::Get().ForEach([&](const VansGraphics::VansShaderRegistryEntry& entry)
    {
        LoadShaderFromEntry(entry, pathPrefix, device);
    });
}

void VansGraphics::VansScene::LoadTexturesFromJson(
    const json& textureData,
    const std::string& pathPrefix,
    VansVKDevice* vkDevice)
{
    for (const auto& sceneTexture : textureData)
    {
        std::string texturePath = pathPrefix + std::string(sceneTexture["path"]);
        VansTexture* texture    = new VansTexture();
        texture->m_TextureType  = sceneTexture["type"];
        bool isSRGB             = sceneTexture["sRGB"];
        switch (texture->m_TextureType)
        {
        case TEXTURE_2D:
            texture->LoadTexture(vkDevice->GetCommandBuffer(), texturePath, isSRGB, true, true);
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

    ImportDefaultTextures(pathPrefix + "EngineAssets/Textures/Default/defaultAlbedo.png",    "defaultAlbedo",    vkDevice, false);
    ImportDefaultTextures(pathPrefix + "EngineAssets/Textures/Default/defaultMetal.png",     "defaultMetal",     vkDevice, false);
    ImportDefaultTextures(pathPrefix + "EngineAssets/Textures/Default/defaultRoughness.png", "defaultRoughness", vkDevice, false);
    ImportDefaultTextures(pathPrefix + "EngineAssets/Textures/Default/defaultAo.png",        "defaultAo",        vkDevice, false);
    ImportDefaultTextures(pathPrefix + "EngineAssets/Textures/Default/defaultNormal.png",    "defaultNormal",    vkDevice, false);
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
    json sceneTextures = sceneData["texture"];
    json sceneMaterials = sceneData["material"];

    LoadMeshesFromJson(sceneMeshes, pathPrefix, nativeDevice, vkDevice);
    LoadShadersFromRegistry(pathPrefix, nativeDevice);
    LoadTexturesFromJson(sceneTextures, pathPrefix, vkDevice);

    for (const auto& sceneMaterial : sceneMaterials)
    {
        // ── Typed material factory ─────────────────────────────────────────────
        VansMaterialType matType = ParseMaterialType(sceneMaterial["type"], sceneMaterial.value("name", "<unnamed>"));

        VansMaterial* material = nullptr;
        switch (matType)
        {
        case VansMaterialType::VAN_PBR:
        case VansMaterialType::VAN_COAT:            material = new VansPBRMaterial();          break;
        case VansMaterialType::VAN_TRANSPARENT:     material = new VansTransparentMaterial();  break;
        case VansMaterialType::VAN_POST_PROCESS:    material = new VansPostProcessMaterial();  break;
        case VansMaterialType::VAN_SKY_BOX:         material = new VansSkyBoxMaterial();       break;
        case VansMaterialType::VAN_DEFERRED:        material = new VansDeferredMaterial();     break;
        case VansMaterialType::VAN_SCREEN_SPACE_AO: material = new VansSSAOMaterial();         break;
        case VansMaterialType::VAN_SHAODW:          material = new VansShadowMaterial();       break;
        case VansMaterialType::VAN_SKIN:            material = new VansSkinMaterial();         break;
        case VansMaterialType::VAN_CLOTH:           material = new VansClothMaterial();        break;
        default:                                    material = new VansMaterial();             break;
        }
        material->m_MaterialType = matType;

        // Optional JSON override — kept for custom/per-scene shaders.
        VansGraphicsShader* shader = nullptr;
        const auto* regEntry = VansGraphics::VansShaderRegistry::Get().FindForType(matType);
        if (regEntry)
        {
            shader = static_cast<VansGraphicsShader*>(GetShaderAsset(regEntry->name));
        }

        material->m_Shader = shader;
        if (matType == VansMaterialType::VAN_PBR)
        {
            VansPBRMaterial* pbr = static_cast<VansPBRMaterial*>(material);
            if (sceneMaterial.contains("basecolor_texture"))
            {
                auto textureName = sceneMaterial["basecolor_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultAlbedo"));
                pbr->m_BaseColorTexture = texture;
            }
            if (sceneMaterial.contains("normal_texture"))
            {
                auto textureName = sceneMaterial["normal_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultNormal"));
                pbr->m_NormalTexture = texture;
            }
            if (sceneMaterial.contains("metal_texture"))
            {
                auto textureName = sceneMaterial["metal_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultMetal"));
                pbr->m_MetalTexture = texture;
            }
            if (sceneMaterial.contains("roughness_texture"))
            {
                auto textureName = sceneMaterial["roughness_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultRoughness"));
                pbr->m_RoughnessTexture = texture;
            }
            if (sceneMaterial.contains("ao_texture"))
            {
                auto textureName = sceneMaterial["ao_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultAo"));
                pbr->m_AoTexture = texture;
            }
            pbr->m_BasePBRParam.m_albedo    = glm::vec3(sceneMaterial["albedo"][0], sceneMaterial["albedo"][1], sceneMaterial["albedo"][2]);
            pbr->m_BasePBRParam.m_metallic  = sceneMaterial["metallic"];
            pbr->m_BasePBRParam.m_roughness = sceneMaterial["roughness"];
            pbr->m_BasePBRParam.m_ao        = sceneMaterial["ao"];
        }

        // ── Cloth material: load basecolor + normal textures + scalar params ──
        if (matType == VansMaterialType::VAN_CLOTH)
        {
            VansClothMaterial* cloth = static_cast<VansClothMaterial*>(material);
            if (sceneMaterial.contains("basecolor_texture"))
            {
                auto textureName = sceneMaterial["basecolor_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultAlbedo"));
                cloth->m_BaseColorTexture = texture;
            }
            if (sceneMaterial.contains("normal_texture"))
            {
                auto textureName = sceneMaterial["normal_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultNormal"));
                cloth->m_NormalTexture = texture;
            }
            if (sceneMaterial.contains("roughness_texture"))
            {
                auto textureName = sceneMaterial["roughness_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultRoughness"));
                cloth->m_RoughnessTexture = texture;
            }
            if (sceneMaterial.contains("ao_texture"))
            {
                auto textureName = sceneMaterial["ao_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultAo"));
                cloth->m_AoTexture = texture;
            }
            cloth->m_SheenRoughness = sceneMaterial.value("sheenRoughness", 0.5f);
        }

        // ── Skin material: load basecolor + normal textures ──
        if (matType == VansMaterialType::VAN_SKIN)
        {
            VansSkinMaterial* skin = static_cast<VansSkinMaterial*>(material);
            if (sceneMaterial.contains("basecolor_texture"))
            {
                auto textureName = sceneMaterial["basecolor_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultAlbedo"));
                skin->m_BaseColorTexture = texture;
            }
            if (sceneMaterial.contains("normal_texture"))
            {
                auto textureName = sceneMaterial["normal_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultNormal"));
                skin->m_NormalTexture = texture;
            }
        }

        // ── Transparent material: load textures declared in material JSON ──
        if (matType == VansMaterialType::VAN_TRANSPARENT)
        {
            VansTransparentMaterial* trans = static_cast<VansTransparentMaterial*>(material);
            if (sceneMaterial.contains("textures") && sceneMaterial["textures"].is_array())
            {
                for (const auto& entry : sceneMaterial["textures"])
                {
                    std::string slotName    = entry.value("slot", "");
                    std::string textureName = entry.value("texture", "");
                    VansTexture* tex = nullptr;
                    if (!textureName.empty())
                        tex = static_cast<VansTexture*>(GetTextureAsset(textureName));
                    if (tex == nullptr)
                        VANS_LOG_WARN("[LoadSceneResource] Transparent material '" << sceneMaterial.value("name", "<unnamed>") << "': could not resolve texture for slot '" << slotName << "'");
                    trans->m_TransparentTextureMap.push_back({ slotName, textureName });
                    trans->m_TransparentTextures.push_back(tex);
                }
            }
        }

        if (matType == VansMaterialType::VAN_SKY_BOX)
        {
            VansSkyBoxMaterial* sky = static_cast<VansSkyBoxMaterial*>(material);
            sky->m_AtmospherePBRParam.m_PlanetRadius          = 6340000;
            sky->m_AtmospherePBRParam.m_InitSeaLevel          = 200;
            sky->m_AtmospherePBRParam.m_AtmosphereWidth       = 80000;
            sky->m_AtmospherePBRParam.m_RayleighScalarHeight  = 8500;
            sky->m_AtmospherePBRParam.m_MieScalarHeight       = 1200;
            sky->m_AtmospherePBRParam.m_MieAnisotropy         = 0.78;
            sky->m_AtmospherePBRParam.m_OzoneLevelCenterHeight= 25000;
            sky->m_AtmospherePBRParam.m_OzoneLevelWidth       = 15000;
            sky->m_AtmospherePBRParam.m_SunLuminance          = 10;
        }
        m_Materials.push_back(material);
        material->SetName(sceneMaterial["name"]);
    }
}

// ===========================================================================
// Multi-mesh auto-expansion: one render node per sub-mesh
// ===========================================================================

VansTexture* VansGraphics::VansScene::LoadOrGetTexture(const std::string& absPath, bool isSRGB)
{
    if (absPath.empty())
        return nullptr;

    // Derive a unique name from the file path
    std::string texName = std::filesystem::path(absPath).stem().string();

    // Check if already loaded
    VansTexture* existing = static_cast<VansTexture*>(GetTextureAsset(texName));
    if (existing)
        return existing;

    // Check if the file exists on disk
    if (!std::filesystem::exists(absPath))
    {
        VANS_LOG_WARN("[LoadOrGetTexture] Texture file not found: " << absPath);
        return nullptr;
    }

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);

    VansTexture* texture = new VansTexture();
    texture->m_TextureType = TEXTURE_2D;
    texture->LoadTexture(vkDevice->GetCommandBuffer(), absPath, isSRGB, true, true);
    texture->SetName(texName);
    m_Textures.push_back(texture);

    VANS_LOG("[LoadOrGetTexture] Loaded texture: " << texName << " from " << absPath);
    return texture;
}

void VansGraphics::VansScene::LoadShaderFromEntry(
    const VansGraphics::VansShaderRegistryEntry& entry,
    const std::string& pathPrefix,
    VkDevice& device)
{
    if (GetShaderAsset(entry.name) != nullptr)
        return; // already loaded

    std::string fullPath = pathPrefix + entry.relativePath;
    VansGraphicsShader* shader = new VansGraphicsShader();
    m_SceneFileWatcher->AddWatch(fullPath);
    shader->InitShader(device, fullPath);
    shader->SetDrawStateData(entry.depthTest, entry.depthWrite, entry.depthCompareOp, entry.cullMode);
    if (entry.pushConstantSize > 0) shader->SetPushConstant(entry.pushConstantSize);
    if (entry.enableAlphaBlend)     shader->SetEnableAlphaBlend(VK_TRUE);
    shader->SetName(entry.name);
    m_Shaders.push_back(shader);
}

VansGraphicsShader* VansGraphics::VansScene::GetOrCreateDefaultShader(VansMaterialType matType, VkDevice& device)
{
    // Delegate completely to the C++ shader registry.
    const auto* regEntry = VansGraphics::VansShaderRegistry::Get().FindForType(matType);
    if (!regEntry)
    {
        VANS_LOG_WARN("[GetOrCreateDefaultShader] No registered shader for material type " << static_cast<int>(matType));
        return nullptr;
    }

    // Reuse if already loaded.
    VansGraphicsShader* existing = static_cast<VansGraphicsShader*>(GetShaderAsset(regEntry->name));
    if (existing)
        return existing;

    // Create a new shader from the registry entry.
    auto vansConfigration = VansConfigration::GetInstance();
    std::string fullPath = vansConfigration->GetProjectRootPath() + regEntry->relativePath;

    VansGraphicsShader* shader = new VansGraphicsShader();
    m_SceneFileWatcher->AddWatch(fullPath);
    shader->InitShader(device, fullPath);
    shader->SetDrawStateData(regEntry->depthTest, regEntry->depthWrite, regEntry->depthCompareOp, regEntry->cullMode);
    if (regEntry->pushConstantSize > 0)
        shader->SetPushConstant(regEntry->pushConstantSize);
    if (regEntry->enableAlphaBlend)
        shader->SetEnableAlphaBlend(VK_TRUE);

    shader->SetName(regEntry->name);
    m_Shaders.push_back(shader);

    VANS_LOG("[GetOrCreateDefaultShader] Created shader from registry: " << regEntry->name);
    return shader;
}

void VansGraphics::VansScene::ExpandMultiMeshToRenderNodes(
    VkDevice& device,
    VansMesh* multiMesh,
    const std::string& parentName,
    const glm::vec3& position,
    const glm::vec3& rotation,
    const glm::vec3& scale,
    bool supportShadow)
{
    if (!multiMesh || !multiMesh->m_IsMultiMesh)
        return;

    const std::string resolvedParentName = MakeUniqueMultiMeshGroupName(*this, parentName);
    if (resolvedParentName != parentName)
    {
        VANS_LOG_WARN("[ExpandMultiMesh] Parent group name conflict for '" << parentName
            << "', renamed to '" << resolvedParentName << "'.");
    }

    // ── Create or retrieve the multi-mesh group for hierarchy display ─────
    MultiMeshGroup& group = m_MultiMeshGroups[resolvedParentName];
    group.parentName = resolvedParentName;
    group.position   = position;
    group.rotation   = rotation;
    group.scale      = scale;

    const auto& subMeshes  = multiMesh->m_SubMeshes;
    const auto& matInfos   = multiMesh->m_SubmeshMaterialInfos;

    for (size_t i = 0; i < subMeshes.size(); ++i)
    {
        VansMesh* subMesh = subMeshes[i];
        if (subMesh == nullptr)
        {
            VANS_LOG_WARN("[ExpandMultiMesh] Submesh[" << i << "] is null in group '"
                << resolvedParentName << "'. Skipping.");
            continue;
        }

        const uint32_t vertexCount = subMesh->GetMeshVertexCount();
        const uint32_t indexCount = subMesh->GetIndexCount();
        const uint32_t triangleCount = indexCount / 3;

        if (vertexCount == 0 || triangleCount == 0)
        {
            VANS_LOG_WARN("[ExpandMultiMesh] Submesh[" << i << "] is invalid for group '"
                << resolvedParentName << "' (vertices=" << vertexCount
                << ", triangles=" << triangleCount << "). Skipping.");
            continue;
        }

        // Strict 1:1 submesh-to-material mapping.
        // If the material info array is shorter than the submesh array,
        // fall back to the first material info ("if has multi material use first one").
        const FBXSubmeshMaterialInfo& fbxInfo = matInfos.empty()
            ? FBXSubmeshMaterialInfo{}
            : (i < matInfos.size() ? matInfos[i] : matInfos[0]);

        // ── Determine material type ───────────────────────────────────────
        VansMaterialType matType = fbxInfo.IsTransparent()
            ? VansMaterialType::VAN_TRANSPARENT
            : VansMaterialType::VAN_PBR;

        // ── Unique material name: nodeName + materialName ───────────────────
        const std::string& nodeName = subMesh->m_SourceNodeName;
        const std::string materialBaseName = resolvedParentName + "_" + nodeName + "_" + fbxInfo.materialName;
        std::string matKey = MakeUniqueMaterialName(*this, materialBaseName);

        VansMaterial* material = nullptr;
        {
            // Typed factory for auto-expanded submesh materials
            if (matType == VansMaterialType::VAN_TRANSPARENT)
                material = new VansTransparentMaterial();
            else
                material = new VansPBRMaterial();

            material->m_MaterialType = matType;
            material->SetName(matKey);

            // Assign default shader
            VansGraphicsShader* defaultShader = GetOrCreateDefaultShader(matType, device);
            material->m_Shader = defaultShader;

            if (matType == VansMaterialType::VAN_PBR)
            {
                VansPBRMaterial* pbr = static_cast<VansPBRMaterial*>(material);
                // ── PBR material: load textures from FBX info ─────────────────
                VansTexture* diffTex  = LoadOrGetTexture(fbxInfo.diffuseTexPath, true);
                VansTexture* normTex  = LoadOrGetTexture(fbxInfo.normalTexPath, false);
                VansTexture* metalTex = LoadOrGetTexture(fbxInfo.metallicTexPath, false);
                VansTexture* roughTex = LoadOrGetTexture(fbxInfo.roughnessTexPath, false);
                VansTexture* aoTex    = LoadOrGetTexture(fbxInfo.aoTexPath, false);

                pbr->m_BaseColorTexture = diffTex  ? diffTex  : static_cast<VansTexture*>(GetTextureAsset("defaultAlbedo"));
                pbr->m_NormalTexture    = normTex  ? normTex  : static_cast<VansTexture*>(GetTextureAsset("defaultNormal"));
                pbr->m_MetalTexture     = metalTex ? metalTex : static_cast<VansTexture*>(GetTextureAsset("defaultMetal"));
                pbr->m_RoughnessTexture = roughTex ? roughTex : static_cast<VansTexture*>(GetTextureAsset("defaultRoughness"));
                pbr->m_AoTexture        = aoTex    ? aoTex    : static_cast<VansTexture*>(GetTextureAsset("defaultAo"));

                pbr->m_BasePBRParam.m_albedo    = glm::vec3(1.0f);
                pbr->m_BasePBRParam.m_metallic  = fbxInfo.metallic;
                pbr->m_BasePBRParam.m_roughness = fbxInfo.roughness;
                pbr->m_BasePBRParam.m_ao        = 1.0f;
            }
            else if (matType == VansMaterialType::VAN_TRANSPARENT)
            {
                VansTransparentMaterial* trans = static_cast<VansTransparentMaterial*>(material);
                // ── Transparent material: load diffuse + opacity as texture slots ─
                VansTexture* diffTex    = LoadOrGetTexture(fbxInfo.diffuseTexPath, true);
                VansTexture* opacityTex = LoadOrGetTexture(fbxInfo.opacityTexPath, false);

                if (diffTex)
                {
                    trans->m_TransparentTextureMap.push_back({ "diffuse", diffTex->m_AssetName });
                    trans->m_TransparentTextures.push_back(diffTex);
                }
                if (opacityTex)
                {
                    trans->m_TransparentTextureMap.push_back({ "opacity", opacityTex->m_AssetName });
                    trans->m_TransparentTextures.push_back(opacityTex);
                }
            }

            m_Materials.push_back(material);

            VANS_LOG("[ExpandMultiMesh] Auto-created material: " << matKey
                     << " (type=" << static_cast<int>(matType) << ")");
        }

        // ── Create render node for this submesh ──────────────────────────
        RenderNodeType nodeType = (matType == VansMaterialType::VAN_TRANSPARENT)
            ? RenderNodeType::TRANSPARENT_NODE
            : RenderNodeType::OPAQUE_NODE;

        VansRenderNode* renderNode = nullptr;
        // Multi-mesh sub-meshes do not support ray tracing; their buffers
        // are not created with the required RT flags.  Force RT off.
        subMesh->m_SupportRayTracing = false;

        if (nodeType == RenderNodeType::OPAQUE_NODE)
        {
            auto* opaque = new VansCommonRenderNode(device, nodeType);
            opaque->m_SupportShadow = supportShadow;
            renderNode = opaque;
        }
        else
        {
            renderNode = new VansTransparentRenderNode(device, nodeType);
        }

        const std::string nodeBaseName = resolvedParentName + "_" + nodeName + "_" + fbxInfo.materialName;
        std::string renderNodeName = MakeUniqueRenderNodeName(*this, nodeBaseName);
        std::string meshName = MakeUniqueMeshName(*this, nodeBaseName);

        renderNode->m_Mesh     = subMesh;
        renderNode->m_Material = material;
        renderNode->m_ParentGroupName = resolvedParentName;

        // All children share the first child's transform so they move together.
        if (group.childNodes.empty())
        {
            // First child — owns the transform; set position from JSON.
            renderNode->SetTransformData(position, rotation, scale);
            group.sharedTransformID = renderNode->m_TransformID;
        }
        else
        {
            // Subsequent children — share the first child's transform.
            renderNode->ShareTransform(group.sharedTransformID);
        }
        renderNode->SetName(renderNodeName);

        // Register the sub-mesh in the scene asset list so it can be found by name
        subMesh->SetName(meshName);
        m_Meshes.push_back(subMesh);

        RegistRenderNode(renderNode, nodeType);
        group.childNodes.push_back(renderNode);

        // ── Shadow nodes ──────────────────────────────────────────────────
        if (nodeType == RenderNodeType::OPAQUE_NODE && supportShadow)
        {
            VansMaterial* shadowMaterial = static_cast<VansMaterial*>(GetMaterialAsset("ShadowMaterial"));
            if (shadowMaterial)
            {
                VansRenderNode* shadowNode = new VansShadowRenderNode(device);
                shadowNode->m_Mesh     = subMesh;
                shadowNode->m_Material = shadowMaterial;
                shadowNode->m_ParentGroupName = resolvedParentName;
                shadowNode->ShareTransform(group.sharedTransformID);
                shadowNode->SetName(renderNodeName + "_shadow");
                m_ShadowRenderNodes.push_back(shadowNode);
                group.shadowNodes.push_back(shadowNode);
            }

            VansMaterial* punctualShadowMaterial = static_cast<VansMaterial*>(GetMaterialAsset("PunctualShadowMaterial"));
            if (punctualShadowMaterial)
            {
                VansRenderNode* shadowNode = new VansShadowRenderNode(device);
                shadowNode->m_Mesh     = subMesh;
                shadowNode->m_Material = punctualShadowMaterial;
                shadowNode->m_ParentGroupName = resolvedParentName;
                shadowNode->ShareTransform(group.sharedTransformID);
                shadowNode->SetName(renderNodeName + "_punctual_shadow");
                m_PunctualShadowRenderNodes.push_back(shadowNode);
                group.shadowNodes.push_back(shadowNode);
            }
        }

        VANS_LOG("[ExpandMultiMesh] Created render node: " << renderNodeName
                 << " (type=" << (nodeType == OPAQUE_NODE ? "OPAQUE" : "TRANSPARENT") << ")");
    }

    // ── Auto-create animation node if the multi-mesh has skeletal animation ──────
    if (multiMesh->m_HasAnimation && !group.childNodes.empty())
    {
        VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);

        VansAnimationNode* animNode = new VansAnimationNode(resolvedParentName);
        animNode->SetSkeleton(multiMesh->m_AnimImportResult.skeleton);

        for (auto& clip : multiMesh->m_AnimImportResult.clips)
            animNode->AddClip(clip);

        animNode->SetRenderNodes(group.childNodes);
        animNode->InitGPUResources(vkDevice->GetLogicDevice(), 1 /*framesInFlight*/);

        // Upload per-submesh bone ID and bone weight buffers.
        // This must happen after InitGPUResources (which sets m_Device on the animNode).
        animNode->UploadPerSubmeshBoneBuffers(multiMesh->m_SubMeshBoneData);

        // Auto-play first clip with looping enabled
        auto clipNames = animNode->GetClipNames();
        if (!clipNames.empty())
        {
            AnimationPlaySettings settings;
            settings.loop     = true;
            settings.autoPlay = true;
            animNode->Play(clipNames.front(), settings);
        }

        // ── Root motion setup: give the animation node write-access to the shared transform ──
        animNode->SetTransformID(group.sharedTransformID);

        // Enable root motion if the mesh JSON specified it
        if (!multiMesh->m_RootBoneName.empty())
            animNode->SetRootBone(multiMesh->m_RootBoneName);
        if (multiMesh->m_RootMotion)
        {
            animNode->EnableRootMotion(true);
        }

        // Tag all child render nodes as animated and link back to this animation node.
        // Wire up per-submesh bone ID and weight buffers (no offset needed).
        for (size_t ci = 0; ci < group.childNodes.size(); ci++)
        {
            VansRenderNode* childNode = group.childNodes[ci];
            childNode->m_HasSkeletonBone      = true;
            childNode->m_AnimationEnabled      = true;
            childNode->m_AnimOwner             = animNode;
            childNode->m_AnimSubmeshIndex      = static_cast<uint32_t>(ci);
            // Per-submesh bone ID and weight buffers
            if (ci < animNode->GetSubmeshBufferCount())
            {
                childNode->m_AnimBoneIDBuffer     = &animNode->GetBoneIDBuffer(static_cast<uint32_t>(ci));
                childNode->m_AnimBoneWeightBuffer  = &animNode->GetBoneWeightBuffer(static_cast<uint32_t>(ci));
            }
        }

        m_AnimationNodes.push_back(animNode);

        VANS_LOG("[ExpandMultiMesh] Animation node created for group '" << resolvedParentName
            << "': " << clipNames.size() << " clip(s), "
            << multiMesh->m_AnimImportResult.skeleton.bones.size() << " bones, "
            << group.childNodes.size() << " render node(s) tagged animated.");
    }
}
