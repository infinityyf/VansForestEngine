#include "../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansScene.h"
#include "VansShaderRegistry.h"
#include "BRDFData/VansLight.h"
#include "../Configration/VansConfigration.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../ScriptCore/VansScriptContext.h"
#include "../PhysicsCore/VansPhysics.h"

#include "VulkanCore/VansMesh.h"
#include "VulkanCore/VansVKDevice.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "TerrainCore/VansTerrain.h"
#include "VegetationCore/VansVegetationSystem.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../AnimationCore/VansAnimationController.h"
#include "../AnimationCore/VansAnimatorIO.h"
#include "../AnimationCore/VansAnimGraph.h"
#include "../AnimationCore/VansAnimationClipLoader.h"
#include "../AnimationCore/VansSkinnedMeshLoader.h"

#include "../../EngineCore/EditorCore/AssetsSystem/VansAssetsFileWatcher.h"
#include "../Util/VansLog.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <filesystem>

namespace VansGraphics
{

// ===========================================================================
// LoadProjectResources — 加载项目级资源（mesh/texture/shader）
// ===========================================================================
void VansScene::LoadProjectResources(const char* resourceJsonPath, VansVKDevice* device)
{
	VANS_LOG("[VansScene] LoadProjectResources: " << resourceJsonPath);

	std::ifstream resFile(resourceJsonPath);
	if (!resFile.is_open())
	{
		VANS_LOG_ERROR("[VansScene] Cannot open resource file: " << resourceJsonPath);
		return;
	}

	json resourceData = json::parse(resFile);

	RegisterEngineShaders();
	LoadResources(resourceData);
	m_ResourcesLoaded = true;

	VANS_LOG("[VansScene] Project resources loaded");
}

// ===========================================================================
// LoadSceneForRendering — 加载场景并准备 GPU 资源
// ===========================================================================
void VansScene::LoadSceneForRendering(const char* scenePath, VansVKDevice* device, VansSceneLoadMode mode)
{
	VANS_LOG("[VansScene] LoadSceneForRendering: " << scenePath);

	// ── 卸载旧场景 ──────────────────────────────────────────────────────
	if (m_SceneState == VansSceneState::Ready)
	{
		m_SceneState = VansSceneState::Unloading;
		VANS_LOG("[VansScene] 开始卸载旧场景...");

		// 等待 GPU 空闲，确保所有命令执行完毕
		device->WaitForDevice();
		UnLoadScene();

		m_SceneState = VansSceneState::Empty;
		VANS_LOG("[VansScene] 旧场景卸载完成");
	}

	if (!m_ResourcesLoaded)
	{
		VANS_LOG_ERROR("[VansScene] LoadSceneForRendering called before LoadProjectResources!");
		return;
	}

	// ── 加载新场景 ──────────────────────────────────────────────────────
	m_SceneState = VansSceneState::Loading;
	VANS_LOG("[VansScene] 开始加载新场景: " << scenePath);

	LoadSceneContent(scenePath);

	// 准备 GPU 端资源
	device->PreparePBRMaterialData();
	device->PrepareInstanceTransformData();
	CreateGlobalDescriptorSet(device->GetLogicDevice());
	CreateNodeDescriptorSets();
	device->PrepareRayTracingData();

	// 记录本次加载模式
	m_LoadMode = mode;

	m_SceneState = VansSceneState::Ready;
	VANS_LOG("[VansScene] 场景就绪，可以开始渲染");
}

} // namespace VansGraphics

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
        if (s == "skin")         return VansMaterialType::VAN_SKIN;
        if (s == "cloth")        return VansMaterialType::VAN_CLOTH;
        if (s == "hair")         return VansMaterialType::VAN_HAIR;
        if (s == "subsurface")   return VansMaterialType::VAN_SUBSURFACE;
        if (s == "grass")        return VansMaterialType::VAN_GRASS;
        VANS_LOG_WARN("[ParseMaterialType] Material '" << materialName << "': unknown type string '" << s << "', defaulting to pbr.");
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
        if (s == "vegetation")   return VansGraphics::VEGETATION_NODE;
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

// ===========================================================================
// Single render node loading (extracted from LoadRenderNodes loop body)
// ===========================================================================

VansGraphics::VansRenderNode* VansGraphics::VansScene::LoadSingleRenderNode(VkDevice& device, const json& sceneRenderNode)
{
    RenderNodeType type = ParseRenderNodeType(sceneRenderNode["type"], sceneRenderNode.value("name", "<unnamed>"));
    std::string meshName = sceneRenderNode.value("mesh", "");

    // ── Resolve mesh ──────────────────────────────────────────────────────
    VansMesh* mesh = static_cast<VansMesh*>(GetMeshAsset(meshName));

    // ── Multi-mesh auto-expansion ─────────────────────────────────────────
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

        // 不从 m_Meshes 中移除父级 multi-mesh，场景切换时仍需通过名称找到它。
        // 子网格会在 ExpandMultiMeshToRenderNodes 内部被添加到 m_Meshes，
        // 并在 UnLoadScene Step 10 中清理。

        // Multi-mesh expansion creates its own render nodes — return nullptr to indicate
        // that no single render node was created.
        return nullptr;
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
        return nullptr;
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

    return renderNode;
}

// ===========================================================================
// Render node loading from JSON (delegates to LoadSingleRenderNode)
// ===========================================================================

void VansGraphics::VansScene::LoadRenderNodes(VkDevice& device, json& render_node)
{
    for (const auto& sceneRenderNode : render_node)
    {
        LoadSingleRenderNode(device, sceneRenderNode);
    }

    // ── Resolve transform parent links ────────────────────────────────────
    // Second pass: now that all render nodes are created, resolve "parent" name
    // references into transform ID links.
    for (const auto& sceneRenderNode : render_node)
    {
        if (!sceneRenderNode.contains("parent")) continue;

        std::string childName  = sceneRenderNode.value("name", "");
        std::string parentName = sceneRenderNode["parent"].get<std::string>();
        if (childName.empty() || parentName.empty()) continue;

        VansRenderNode* childNode  = FindRenderNodeByName(childName);
        VansRenderNode* parentNode = FindRenderNodeByName(parentName);

        if (childNode && parentNode)
        {
            m_TransformParentSystem.SetParent(childNode->m_TransformID, parentNode->m_TransformID);
            VANS_LOG("[TransformParent] '" << childName << "' parented to '" << parentName << "'");
        }
        else
        {
            VANS_LOG_WARN("[TransformParent] Could not resolve parent link: child='" << childName << "' parent='" << parentName << "'");
        }
    }
}

// ===========================================================================
// Terrain node
// ===========================================================================

void VansGraphics::VansScene::AddTerrainNode(VansVKDevice* device, json& terrainData)
{
    auto vansConfigration = VansConfigration::GetInstance();
    auto& projectMgr = Vans::VansProjectManager::Get();
    std::string projectRoot = projectMgr.IsProjectLoaded()
        ? projectMgr.GetProjectRootPath()
        : vansConfigration->GetProjectRootPath();

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
    material->m_MaterialType = VansMaterialType::VAN_DEFERRED;
    material->m_PassShaders[VansPass::DEFERRED] = deferredShader;
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
        material->m_MaterialType = feature.matType;
        // Populate m_PassShaders from registry
        {
            auto& reg = VansGraphics::VansShaderRegistry::Get();
            const auto& passMap = reg.GetMaterialPassMap(feature.matType);
            for (const auto& [passName, shaderName] : passMap)
            {
                VansGraphicsShader* passShader = static_cast<VansGraphicsShader*>(GetShaderAsset(shaderName));
                if (passShader)
                    material->m_PassShaders[passName] = passShader;
            }
        }
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

            // 动画相关字段 (root_bone, root_motion, extern_animation) 已迁移到
            // Scene JSON 中的 animation_node 配置，由 LoadAnimationNodesFromJson 处理。

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
    // Load shaders from the Shader Table
    VansGraphics::VansShaderRegistry::Get().ForEachShader([&](const VansGraphics::VansShaderRegistryEntry& entry)
    {
        LoadShaderFromEntry(entry, pathPrefix, device);
    });
}

void VansGraphics::VansScene::LoadTexturesFromJson(
    const json& textureData,
    const std::string& pathPrefix,
    const std::string& enginePrefix,
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

    // Default textures are always loaded from the engine's EngineAssets directory
    ImportDefaultTextures(enginePrefix + "EngineAssets/Textures/Default/defaultAlbedo.png",    "defaultAlbedo",    vkDevice, false);
    ImportDefaultTextures(enginePrefix + "EngineAssets/Textures/Default/defaultMetal.png",     "defaultMetal",     vkDevice, false);
    ImportDefaultTextures(enginePrefix + "EngineAssets/Textures/Default/defaultRoughness.png", "defaultRoughness", vkDevice, false);
    ImportDefaultTextures(enginePrefix + "EngineAssets/Textures/Default/defaultAo.png",        "defaultAo",        vkDevice, false);
    ImportDefaultTextures(enginePrefix + "EngineAssets/Textures/Default/defaultNormal.png",    "defaultNormal",    vkDevice, false);
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

// ===========================================================================
// LoadResources — load project-wide resources (mesh, texture, shader)
// Called once per project from resource.json, before any scene is loaded.
// ===========================================================================

void VansGraphics::VansScene::LoadResources(json& resourceData)
{
    auto vansConfigration = VansConfigration::GetInstance();
    std::string enginePrefix = vansConfigration->GetProjectRootPath();

    auto& projectMgr = Vans::VansProjectManager::Get();
    std::string assetPrefix = projectMgr.IsProjectLoaded()
        ? projectMgr.GetProjectRootPath()
        : enginePrefix;

    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkDevice nativeDevice = vkDevice->GetLogicDevice();

    if (resourceData.contains("mesh") && resourceData["mesh"].is_array())
    {
        LoadMeshesFromJson(resourceData["mesh"], assetPrefix, nativeDevice, vkDevice);
    }

    LoadShadersFromRegistry(enginePrefix, nativeDevice);

    if (resourceData.contains("texture") && resourceData["texture"].is_array())
    {
        LoadTexturesFromJson(resourceData["texture"], assetPrefix, enginePrefix, vkDevice);
    }

    VANS_LOG("[VansScene] Resources loaded: "
             << m_Meshes.size() << " meshes, "
             << m_Textures.size() << " textures, "
             << m_Shaders.size() << " shaders");
}

// ===========================================================================
// LoadMaterialsFromJson — load materials from scene JSON material array
// Resources (mesh/texture/shader) must already be loaded.
// ===========================================================================

void VansGraphics::VansScene::LoadMaterialsFromJson(const json& materialData)
{
    for (const auto& sceneMaterial : materialData)
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
        case VansMaterialType::VAN_SKIN:            material = new VansSkinMaterial();         break;
        case VansMaterialType::VAN_CLOTH:           material = new VansClothMaterial();        break;
        case VansMaterialType::VAN_HAIR:            material = new VansHairMaterial();         break;
        case VansMaterialType::VAN_SUBSURFACE:      material = new VansSubsurfaceMaterial();   break;
        case VansMaterialType::VAN_GRASS:           material = new VansGrassMaterial();        break;
        default:                                    material = new VansMaterial();             break;
        }
        material->m_MaterialType = matType;

        // ── Populate m_PassShaders from the Material Pass Table ──────────────
        {
            auto& reg = VansGraphics::VansShaderRegistry::Get();
            const auto& passMap = reg.GetMaterialPassMap(matType);
            for (const auto& [passName, shaderName] : passMap)
            {
                VansGraphicsShader* passShader = static_cast<VansGraphicsShader*>(GetShaderAsset(shaderName));
                if (passShader)
                    material->m_PassShaders[passName] = passShader;
            }
        }
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

        if (matType == VansMaterialType::VAN_HAIR)
        {
            VansHairMaterial* hair = static_cast<VansHairMaterial*>(material);
            if (sceneMaterial.contains("basecolor_texture"))
            {
                auto textureName = sceneMaterial["basecolor_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultAlbedo"));
                hair->m_AlbedoAlphaTexture = texture;
            }
            if (sceneMaterial.contains("normal_texture"))
            {
                auto textureName = sceneMaterial["normal_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultNormal"));
                hair->m_NormalTexture = texture;
            }
            if (sceneMaterial.contains("roughness_texture"))
            {
                auto textureName = sceneMaterial["roughness_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultRoughness"));
                hair->m_RoughnessTexture = texture;
            }
            if (sceneMaterial.contains("ao_texture"))
            {
                auto textureName = sceneMaterial["ao_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultAo"));
                hair->m_AoTexture = texture;
            }
            if (sceneMaterial.contains("shift_texture"))
            {
                auto textureName = sceneMaterial["shift_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                hair->m_ShiftTexture = texture;
            }
            if (sceneMaterial.contains("alpha_texture"))
            {
                auto textureName = sceneMaterial["alpha_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                hair->m_AlphaTexture = texture;
            }
            if (sceneMaterial.contains("flow_texture"))
            {
                auto textureName = sceneMaterial["flow_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                hair->m_FlowTexture = texture;
            }
        }

        if (matType == VansMaterialType::VAN_SUBSURFACE)
        {
            VansSubsurfaceMaterial* sss = static_cast<VansSubsurfaceMaterial*>(material);
            if (sceneMaterial.contains("basecolor_texture"))
            {
                auto textureName = sceneMaterial["basecolor_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultAlbedo"));
                sss->m_BaseColorTexture = texture;
            }
            if (sceneMaterial.contains("normal_texture"))
            {
                auto textureName = sceneMaterial["normal_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultNormal"));
                sss->m_NormalTexture = texture;
            }
            if (sceneMaterial.contains("thickness_texture"))
            {
                auto textureName = sceneMaterial["thickness_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                sss->m_ThicknessTexture = texture;
            }
            if (sceneMaterial.contains("roughness_texture"))
            {
                auto textureName = sceneMaterial["roughness_texture"];
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(textureName));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultRoughness"));
                sss->m_RoughnessTexture = texture;
            }
            sss->m_SubsurfacePower = sceneMaterial.value("subsurfacePower", 12.234f);
            sss->m_Thickness       = sceneMaterial.value("thickness", 0.5f);
            if (sceneMaterial.contains("subsurfaceColor") && sceneMaterial["subsurfaceColor"].is_array())
            {
                sss->m_SubsurfaceColor = glm::vec3(
                    sceneMaterial["subsurfaceColor"][0],
                    sceneMaterial["subsurfaceColor"][1],
                    sceneMaterial["subsurfaceColor"][2]);
            }
        }

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
                        VANS_LOG_WARN("[LoadMaterials] Transparent material '" << sceneMaterial.value("name", "<unnamed>") << "': could not resolve texture for slot '" << slotName << "'");
                    trans->m_TransparentTextureMap.push_back({ slotName, textureName });
                    trans->m_TransparentTextures.push_back(tex);
                }
            }
        }

        if (matType == VansMaterialType::VAN_GRASS)
        {
            VansGrassMaterial* grass = static_cast<VansGrassMaterial*>(material);
            if (sceneMaterial.contains("basecolor_texture"))
            {
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(sceneMaterial["basecolor_texture"]));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultAlbedo"));
                grass->m_AlbedoTexture = texture;
            }
            if (sceneMaterial.contains("normal_texture"))
            {
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(sceneMaterial["normal_texture"]));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultNormal"));
                grass->m_NormalTexture = texture;
            }
            if (sceneMaterial.contains("roughness_texture"))
            {
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(sceneMaterial["roughness_texture"]));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultRoughness"));
                grass->m_RoughnessTexture = texture;
            }
            if (sceneMaterial.contains("translucency_texture"))
            {
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(sceneMaterial["translucency_texture"]));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultAo"));
                grass->m_TranslucencyTexture = texture;
            }
            if (sceneMaterial.contains("ao_texture"))
            {
                VansTexture* texture = static_cast<VansTexture*>(GetTextureAsset(sceneMaterial["ao_texture"]));
                if (texture == nullptr)
                    texture = static_cast<VansTexture*>(GetTextureAsset("defaultAo"));
                grass->m_AOTexture = texture;
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
// LoadSceneContent — load scene file when resources are already loaded
// Loads materials, nodes, terrain, vegetation, deferred, screen-space.
// ===========================================================================

bool VansGraphics::VansScene::LoadSceneContent(const char* path)
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkDevice nativeDevice = vkDevice->GetLogicDevice();

    // Parse scene JSON
    std::ifstream jsonFile(path);
    if (!jsonFile.is_open())
    {
        VANS_LOG_ERROR("[VansScene] Cannot open scene file: " << path);
        return false;
    }
    json sceneData = json::parse(jsonFile);

    // Load camera parameters from scene JSON
    if (m_Camera != nullptr)
    {
        if (sceneData.contains("camera"))
        {
            m_Camera->ApplyCameraSettings(sceneData["camera"]);
        }
        else
        {
            m_Camera->ResetToDefaults();
        }
    }

    // Load materials (resources are already loaded from resource.json)
    if (sceneData.contains("material") && sceneData["material"].is_array())
    {
        LoadMaterialsFromJson(sceneData["material"]);
    }

    // Load scene nodes (lights, objects, rendernodes, physics)
    json sceneNode = sceneData["scene"];

    // 从 scene path 推导 project root（Scenes/ → 项目根目录）
    std::string scenePath(path);
    std::string projectRoot = scenePath.substr(0, scenePath.find_last_of("/\\") + 1);
    if (!projectRoot.empty())
    {
        size_t pos = projectRoot.substr(0, projectRoot.size() - 1).find_last_of("/\\");
        if (pos != std::string::npos)
            projectRoot = projectRoot.substr(0, pos + 1);
    }

    LoadLights(nativeDevice, sceneNode[0]["light"]);

    if (sceneNode[0].contains("objects") && sceneNode[0]["objects"].is_array()
        && !sceneNode[0]["objects"].empty())
    {
        LoadSceneObjects(nativeDevice, sceneNode[0]["objects"], projectRoot);

        if (sceneNode[0].contains("rendernode") && !sceneNode[0]["rendernode"].empty())
        {
            LoadRenderNodes(nativeDevice, sceneNode[0]["rendernode"]);
        }
        if (sceneNode[0].contains("physicsnode") && !sceneNode[0]["physicsnode"].empty())
        {
            LoadPhysicsNodes(sceneNode[0]["physicsnode"]);
        }
    }
    else
    {
        LoadRenderNodes(nativeDevice, sceneNode[0]["rendernode"]);

        if (sceneNode[0].contains("physicsnode"))
        {
            LoadPhysicsNodes(sceneNode[0]["physicsnode"]);
        }

        AutoCreateObjectsFromLegacy();
    }

    // Terrain
    if (sceneData.contains("terrain"))
    {
        AddTerrainNode(vkDevice, sceneData["terrain"]);
    }

    // Vegetation
    if (sceneData.contains("vegetation"))
    {
        AddVegetationNode(nativeDevice, sceneData["vegetation"]);
    }

    // Legacy top-level animation_node（向后兼容，新代码应使用 object.components.animation）
    if (sceneNode[0].contains("animation_node") && sceneNode[0]["animation_node"].is_array())
    {
        LoadAnimationNodesFromJson(sceneNode[0]["animation_node"], projectRoot);
    }

    AddDeferredNode(nativeDevice);
    AddScreenSpaceFeatureNode(nativeDevice);

    VANS_LOG("[VansScene] Scene content loaded from: " << path);
    return true;
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

            // Populate m_PassShaders from Material Pass Table
            {
                auto& reg = VansGraphics::VansShaderRegistry::Get();
                const auto& passMap = reg.GetMaterialPassMap(matType);
                for (const auto& [passName, shaderName] : passMap)
                {
                    VansGraphicsShader* passShader = static_cast<VansGraphicsShader*>(GetShaderAsset(shaderName));
                    if (passShader)
                        material->m_PassShaders[passName] = passShader;
                }
            }

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

        // Shadow nodes are no longer created here — shadow passes now iterate
        // opaque nodes and use material->GetPassShader(VansPass::SHADOW).

        VANS_LOG("[ExpandMultiMesh] Created render node: " << renderNodeName
                 << " (type=" << (nodeType == OPAQUE_NODE ? "OPAQUE" : "TRANSPARENT") << ")");
    }

    // ── 动画节点不再在此创建 ──
    // AnimationNode + Controller 由 LoadAnimationNodesFromJson 根据
    // Scene JSON 中的 animation_node 配置独立创建。
    // ExpandMultiMesh 仅负责几何体 → 渲染节点的展开。
}

// ===========================================================================
// LoadAnimationNodesFromJson — 从场景 JSON 加载 controller-based animation nodes
// ===========================================================================

void VansGraphics::VansScene::LoadAnimationNodesFromJson(json& animNodeArray, const std::string& projectRoot)
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkDevice device = vkDevice->GetLogicDevice();

    for (auto& entry : animNodeArray)
    {
        std::string nodeName      = entry.value("name", "");
        std::string meshGroupName = entry.value("mesh_group", "");
        std::string animatorPath  = entry.value("animator", "");
        std::string externClips   = entry.value("extern_clips", "");
        bool enableRootMotion     = entry.value("root_motion", false);
        std::string rootBone      = entry.value("root_bone", "");

        if (nodeName.empty() || meshGroupName.empty())
        {
            VANS_LOG_WARN("[LoadAnimNode] Skipping animation_node entry with empty name or mesh_group");
            continue;
        }

        // 找到对应的 MultiMeshGroup
        auto groupIt = m_MultiMeshGroups.find(meshGroupName);
        if (groupIt == m_MultiMeshGroups.end())
        {
            VANS_LOG_WARN("[LoadAnimNode] mesh_group '" << meshGroupName << "' not found for animation_node '" << nodeName << "'");
            continue;
        }

        MultiMeshGroup& group = groupIt->second;
        if (group.childNodes.empty())
            continue;

        // 查找 mesh 资产（获取 skeleton 和内嵌 clip）
        VansMesh* meshAsset = nullptr;
        for (auto* asset : m_Meshes)
        {
            if (asset->m_AssetName == meshGroupName)
            {
                meshAsset = dynamic_cast<VansMesh*>(asset);
                break;
            }
        }

        if (!meshAsset || !meshAsset->m_HasAnimation)
        {
            VANS_LOG_WARN("[LoadAnimNode] mesh_group '" << meshGroupName
                         << "' has no animation data. Skipping '" << nodeName << "'");
            continue;
        }

        // ── 创建 Controller ──────────────────────────────────────────────
        VansAnimationController* controller = nullptr;

        if (!animatorPath.empty())
        {
            // 路径 A: 从 .vanimator 文件加载完整 controller 定义
            std::string fullAnimatorPath = projectRoot + animatorPath;

            AnimatorAssetData assetData;
            if (!VansAnimatorIO::Load(fullAnimatorPath, assetData))
            {
                VANS_LOG_WARN("[LoadAnimNode] Failed to load .vanimator: " << fullAnimatorPath);
                continue;
            }

            auto clipsMap = VansAnimationClipLoader::LoadClipsFromRefs(
                assetData.clipRefs, projectRoot,
                &meshAsset->m_AnimImportResult.skeleton);

            controller = new VansAnimationController();
            controller->SetName(assetData.name);

            for (const auto& param : assetData.parameters)
            {
                controller->AddParameter(param.name, param.type);
                switch (param.type)
                {
                case AnimatorParamType::Float:   controller->SetFloat(param.name, param.floatVal); break;
                case AnimatorParamType::Bool:    controller->SetBool(param.name, param.boolVal);   break;
                case AnimatorParamType::Int:     controller->SetInt(param.name, param.intVal);     break;
                case AnimatorParamType::Trigger: break;
                }
            }

            for (auto& [name, clip] : clipsMap)
                controller->AddClip(name, std::move(clip));

            for (const auto& state : assetData.states)
                controller->AddState(state);

            for (const auto& trans : assetData.transitions)
                controller->AddTransition(trans);

            controller->SetDefaultState(assetData.defaultStateName);
            controller->BindStateClips();

            // v2: 传递 AnimGraph
            if (assetData.animGraph)
                controller->SetGraph(std::move(assetData.animGraph));

            VANS_LOG("[LoadAnimNode] Loaded controller from .vanimator: " << fullAnimatorPath);
        }
        else
        {
            // 路径 B: 自动生成默认 controller（从 FBX 内嵌 clip + 外部 clip）
            controller = new VansAnimationController();
            controller->SetName(meshGroupName + "_Controller");

            // 收集 clip: 如果指定了外部 FBX，优先使用外部 clip（与旧方案一致，
            // 外部 clip 完全替换内嵌 clip，避免默认播放内嵌 clip 导致骨骼矩阵不匹配）
            bool usedExternClips = false;
            if (!externClips.empty())
            {
                std::string fullExternPath = projectRoot + externClips;
                std::vector<VansAnimationClip> extClips;
                if (VansAnimationClipLoader::ExtractClipsFromFBX(
                        fullExternPath, meshAsset->m_AnimImportResult.skeleton, extClips))
                {
                    for (auto& extClip : extClips)
                        controller->AddClip(extClip.clipName, std::move(extClip));

                    usedExternClips = true;
                    VANS_LOG("[LoadAnimNode] Loaded " << extClips.size()
                             << " extern clip(s) from: " << fullExternPath);
                }
                else
                {
                    VANS_LOG_WARN("[LoadAnimNode] Failed to extract clips from: " << fullExternPath);
                }
            }

            // 仅当没有成功加载外部 clip 时，才使用 mesh 内嵌的 clip（fallback）
            if (!usedExternClips)
            {
                for (auto& clip : meshAsset->m_AnimImportResult.clips)
                    controller->AddClip(clip.clipName, clip);
            }

            // 为每个 clip 创建同名 state
            auto clipNames = controller->GetClipNames();
            for (const auto& clipName : clipNames)
            {
                AnimatorState state;
                state.name        = clipName;
                state.clipName    = clipName;
                state.speed       = 1.0f;
                state.loop        = true;
                state.rootMotion  = enableRootMotion;
                controller->AddState(state);
            }

            if (!clipNames.empty())
                controller->SetDefaultState(clipNames.front());

            controller->BindStateClips();

            VANS_LOG("[LoadAnimNode] Auto-generated controller for '" << meshGroupName
                     << "' with " << clipNames.size() << " clip(s)");
        }

        // Root motion 配置
        if (enableRootMotion)
            controller->EnableRootMotion(true);

        // ── 创建 AnimationNode ───────────────────────────────────────────
        VansAnimationNode* animNode = new VansAnimationNode(nodeName);
        animNode->SetSkeleton(meshAsset->m_AnimImportResult.skeleton);
        animNode->SetRenderNodes(group.childNodes);
        animNode->InitGPUResources(device, 1);
        animNode->UploadPerSubmeshBoneBuffers(meshAsset->m_SubMeshBoneData);
        animNode->SetTransformID(group.sharedTransformID);

        animNode->SetController(controller);

        // 记录 .vanimator 文件路径供编辑器使用
        if (!animatorPath.empty())
            animNode->SetAnimatorFilePath(projectRoot + animatorPath);

        if (!rootBone.empty())
            animNode->SetRootBone(rootBone);

        // Tag render nodes
        for (size_t ci = 0; ci < group.childNodes.size(); ci++)
        {
            VansRenderNode* childNode = group.childNodes[ci];
            childNode->m_HasSkeletonBone   = true;
            childNode->m_AnimationEnabled  = true;
            childNode->m_AnimOwner         = animNode;
            childNode->m_AnimSubmeshIndex   = static_cast<uint32_t>(ci);
            if (ci < animNode->GetSubmeshBufferCount())
            {
                childNode->m_AnimBoneIDBuffer     = &animNode->GetBoneIDBuffer(static_cast<uint32_t>(ci));
                childNode->m_AnimBoneWeightBuffer  = &animNode->GetBoneWeightBuffer(static_cast<uint32_t>(ci));
            }
        }

        m_AnimationNodes.push_back(animNode);
        m_AnimationControllers.push_back(controller);
        controller->Play();

        VANS_LOG("[LoadAnimNode] Created animation_node '" << nodeName
                 << "' with " << controller->GetClipNames().size() << " clip(s), "
                 << meshAsset->m_AnimImportResult.skeleton.bones.size() << " bones, "
                 << group.childNodes.size() << " render node(s)");
    }
}

// ===========================================================================
// LoadSingleAnimationComponent — 从单个 JSON 对象创建 AnimationNode
// 用于 LoadSceneObjects 中的 "animation" component 字段
// 返回创建的 VansAnimationNode 指针（失败时返回 nullptr）
// ===========================================================================

VansGraphics::VansAnimationNode* VansGraphics::VansScene::LoadSingleAnimationComponent(
    const json& animJson,
    const std::string& objectName,
    const std::string& projectRoot)
{
    VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
    VkDevice device = vkDevice->GetLogicDevice();

    std::string meshGroupName  = animJson.value("mesh_group", "");
    std::string animatorPath   = animJson.value("animator", "");
    std::string externClips    = animJson.value("extern_clips", "");
    bool enableRootMotion      = animJson.value("root_motion", false);
    std::string rootBone       = animJson.value("root_bone", "");

    // node 名称：优先读取字段，其次用 object 名称
    std::string nodeName = animJson.value("name", objectName);

    if (meshGroupName.empty())
    {
        VANS_LOG_WARN("[LoadAnimComp] animation component on '" << objectName << "' has no mesh_group, skipping");
        return nullptr;
    }

    // 查找 MultiMeshGroup
    auto groupIt = m_MultiMeshGroups.find(meshGroupName);
    if (groupIt == m_MultiMeshGroups.end())
    {
        VANS_LOG_WARN("[LoadAnimComp] mesh_group '" << meshGroupName << "' not found for object '" << objectName << "'");
        return nullptr;
    }

    MultiMeshGroup& group = groupIt->second;
    if (group.childNodes.empty())
        return nullptr;

    // 查找 mesh 资产
    VansMesh* meshAsset = nullptr;
    for (auto* asset : m_Meshes)
    {
        if (asset->m_AssetName == meshGroupName)
        {
            meshAsset = dynamic_cast<VansMesh*>(asset);
            break;
        }
    }

    if (!meshAsset || !meshAsset->m_HasAnimation)
    {
        VANS_LOG_WARN("[LoadAnimComp] mesh_group '" << meshGroupName
                     << "' has no animation data. Skipping '" << objectName << "'");
        return nullptr;
    }

    // ── 创建 Controller ──────────────────────────────────────────────────
    VansAnimationController* controller = nullptr;

    if (!animatorPath.empty())
    {
        // 路径 A: 从 .vanimator 文件加载完整 controller 定义
        std::string fullAnimatorPath = projectRoot + animatorPath;

        AnimatorAssetData assetData;
        if (!VansAnimatorIO::Load(fullAnimatorPath, assetData))
        {
            VANS_LOG_WARN("[LoadAnimComp] Failed to load .vanimator: " << fullAnimatorPath);
            return nullptr;
        }

        auto clipsMap = VansAnimationClipLoader::LoadClipsFromRefs(
            assetData.clipRefs, projectRoot,
            &meshAsset->m_AnimImportResult.skeleton);

        controller = new VansAnimationController();
        controller->SetName(assetData.name);

        for (const auto& param : assetData.parameters)
        {
            controller->AddParameter(param.name, param.type);
            switch (param.type)
            {
            case AnimatorParamType::Float:   controller->SetFloat(param.name, param.floatVal); break;
            case AnimatorParamType::Bool:    controller->SetBool(param.name, param.boolVal);   break;
            case AnimatorParamType::Int:     controller->SetInt(param.name, param.intVal);     break;
            case AnimatorParamType::Trigger: break;
            }
        }

        for (auto& [name, clip] : clipsMap)
            controller->AddClip(name, std::move(clip));

        for (const auto& state : assetData.states)
            controller->AddState(state);

        for (const auto& trans : assetData.transitions)
            controller->AddTransition(trans);

        controller->SetDefaultState(assetData.defaultStateName);
        controller->BindStateClips();

        // v2: 传递 AnimGraph
        if (assetData.animGraph)
            controller->SetGraph(std::move(assetData.animGraph));

        VANS_LOG("[LoadAnimComp] Loaded controller from .vanimator: " << fullAnimatorPath);
    }
    else
    {
        // 路径 B: 自动生成默认 controller（外部 clip 优先，fallback 使用内嵌 clip）
        controller = new VansAnimationController();
        controller->SetName(meshGroupName + "_Controller");

        bool usedExternClips = false;
        if (!externClips.empty())
        {
            std::string fullExternPath = projectRoot + externClips;
            std::vector<VansAnimationClip> extClips;
            if (VansAnimationClipLoader::ExtractClipsFromFBX(
                    fullExternPath, meshAsset->m_AnimImportResult.skeleton, extClips))
            {
                for (auto& extClip : extClips)
                    controller->AddClip(extClip.clipName, std::move(extClip));

                usedExternClips = true;
                VANS_LOG("[LoadAnimComp] Loaded " << extClips.size()
                         << " extern clip(s) from: " << fullExternPath);
            }
            else
            {
                VANS_LOG_WARN("[LoadAnimComp] Failed to extract clips from: " << fullExternPath);
            }
        }

        if (!usedExternClips)
        {
            for (auto& clip : meshAsset->m_AnimImportResult.clips)
                controller->AddClip(clip.clipName, clip);
        }

        auto clipNames = controller->GetClipNames();
        for (const auto& clipName : clipNames)
        {
            AnimatorState state;
            state.name       = clipName;
            state.clipName   = clipName;
            state.speed      = 1.0f;
            state.loop       = true;
            state.rootMotion = enableRootMotion;
            controller->AddState(state);
        }

        if (!clipNames.empty())
            controller->SetDefaultState(clipNames.front());

        controller->BindStateClips();

        VANS_LOG("[LoadAnimComp] Auto-generated controller for '" << meshGroupName
                 << "' with " << clipNames.size() << " clip(s)");
    }

    if (enableRootMotion)
        controller->EnableRootMotion(true);

    // ── 创建 AnimationNode ───────────────────────────────────────────────
    VansAnimationNode* animNode = new VansAnimationNode(nodeName);
    animNode->SetSkeleton(meshAsset->m_AnimImportResult.skeleton);
    animNode->SetRenderNodes(group.childNodes);
    animNode->InitGPUResources(device, 1);
    animNode->UploadPerSubmeshBoneBuffers(meshAsset->m_SubMeshBoneData);
    animNode->SetTransformID(group.sharedTransformID);
    animNode->SetController(controller);

    // 记录 .vanimator 文件路径供编辑器使用
    if (!animatorPath.empty())
        animNode->SetAnimatorFilePath(projectRoot + animatorPath);

    if (!rootBone.empty())
        animNode->SetRootBone(rootBone);

    // 标记渲染节点
    for (size_t ci = 0; ci < group.childNodes.size(); ci++)
    {
        VansRenderNode* childNode = group.childNodes[ci];
        childNode->m_HasSkeletonBone  = true;
        childNode->m_AnimationEnabled = true;
        childNode->m_AnimOwner        = animNode;
        childNode->m_AnimSubmeshIndex = static_cast<uint32_t>(ci);
        if (ci < animNode->GetSubmeshBufferCount())
        {
            childNode->m_AnimBoneIDBuffer    = &animNode->GetBoneIDBuffer(static_cast<uint32_t>(ci));
            childNode->m_AnimBoneWeightBuffer = &animNode->GetBoneWeightBuffer(static_cast<uint32_t>(ci));
        }
    }

    m_AnimationNodes.push_back(animNode);
    m_AnimationControllers.push_back(controller);
    controller->Play();

    VANS_LOG("[LoadAnimComp] Created animation component '" << nodeName
             << "' with " << controller->GetClipNames().size() << " clip(s), "
             << meshAsset->m_AnimImportResult.skeleton.bones.size() << " bones, "
             << group.childNodes.size() << " render node(s)");

    return animNode;
}

// ===========================================================================
// Vegetation node
// ===========================================================================

void VansGraphics::VansScene::AddVegetationNode(VkDevice& device, json& vegetationData)
{
    // Read optional parameters from JSON
    uint32_t instanceCount = vegetationData.value("instanceCount", 2000000u);
    uint32_t boneCount     = vegetationData.value("boneCount", 6u);
    float    bladeHeight   = vegetationData.value("bladeHeight", 0.5f);
    float    windDirX      = vegetationData.value("windDirX", 1.0f);
    float    windDirZ      = vegetationData.value("windDirZ", 0.0f);
    float    leanDeviation = vegetationData.value("leanDeviation", 35.0f);  // degrees
    std::string materialName = vegetationData.value("material", "grassMaterial");
    std::string name         = vegetationData.value("name", "VegetationNode");

    // Read per-frame simulation parameters (all configurable from JSON)
    uint32_t subBladeCount           = vegetationData.value("subBladeCount",          10u);
    float    subBladeScatterRadiusMin = vegetationData.value("subBladeScatterRadiusMin", 0.15f);
    float    subBladeScatterRadiusMax = vegetationData.value("subBladeScatterRadiusMax", 0.45f);
    float windStrength  = vegetationData.value("windStrength",  4.0f);   // overall wind force
    float windFrequency = vegetationData.value("windFrequency", 0.5f);   // spatial noise frequency
    float windSpeed     = vegetationData.value("windSpeed",     1.5f);   // noise scroll rate (animation speed)
    float windBendMult  = vegetationData.value("windBendMult",  5.0f);   // bend amplification
    float stiffness     = vegetationData.value("stiffness",    15.0f);
    float damping       = vegetationData.value("damping",       0.92f);
    float softness      = vegetationData.value("softness",      0.2f);
    float lodFullDist   = vegetationData.value("lodFullDist",  15.0f);
    float lodFadeDist   = vegetationData.value("lodFadeDist",  20.0f);

    // Create the vegetation system
    m_VegetationSystem = new VansVegetationSystem();
    m_VegetationSystem->SetBladeHeight(bladeHeight);   // must be set before Init()
    m_VegetationSystem->SetInitWindDirection(glm::vec2(windDirX, windDirZ), leanDeviation);
    m_VegetationSystem->SetSubBladeParams(subBladeCount, subBladeScatterRadiusMin, subBladeScatterRadiusMax);  // must be set before Init()

    // ── Parse render configs (multi-mesh/material support) ─────────────────
    if (vegetationData.contains("renderConfigs") && vegetationData["renderConfigs"].is_array())
    {
        std::vector<GrassRenderConfig> configs;
        for (auto& rc : vegetationData["renderConfigs"])
        {
            GrassRenderConfig cfg;
            cfg.meshName     = rc.value("mesh", std::string(""));
            cfg.materialName = rc.value("material", materialName);
            cfg.percent      = rc.value("percent", 1.0f);
            configs.push_back(cfg);
        }
        m_VegetationSystem->SetRenderConfigs(configs);
    }
    else
    {
        // Backward compatible: single material, procedural blade
        GrassRenderConfig defaultCfg;
        defaultCfg.meshName     = "";
        defaultCfg.materialName = materialName;
        defaultCfg.percent      = 1.0f;
        m_VegetationSystem->SetRenderConfigs({ defaultCfg });
    }

    m_VegetationSystem->Init(device, instanceCount, boneCount);

    // Apply runtime simulation parameters loaded from JSON
    m_VegetationSystem->SetSimParams(
        glm::vec2(windDirX, windDirZ),
        windStrength, windFrequency, windSpeed, windBendMult,
        stiffness, damping, softness,
        lodFullDist, lodFadeDist);

    // ── Connect terrain heightmap for ground placement ──────────────────────
    if (m_TerrainRenderNode != nullptr)
    {
        VansTerrainRenderNode* terrainNode = dynamic_cast<VansTerrainRenderNode*>(m_TerrainRenderNode);
        if (terrainNode && terrainNode->GetTerrain())
        {
            VansTerrain* terrain = terrainNode->GetTerrain();
            VansTexture* heightMap = terrain->GetHeightMap();
            if (heightMap)
            {
                // Read terrain height params from JSON or use terrain defaults
                float terrainMaxHeight   = vegetationData.value("terrainMaxHeight", 500.0f);
                float terrainHeightOffset = vegetationData.value("terrainHeightOffset", -23.0f);

                m_VegetationSystem->SetTerrainHeightmap(
                    heightMap->GetImage().GetImageView(),
                    heightMap->GetImage().GetSampler(),
                    terrain->GetTerrainSize(),
                    terrainMaxHeight,
                    terrainHeightOffset);
            }
        }
    }

    // ── 连接 Hi-Z depth pyramid 进行遥测遮挡剪除 ────────────────────────────
    {
        VansTexture* hzbTexture = m_MaterialManager.GetRuntimeRenderTexture(
            VansMaterialManager::RT_HZB_RESULT);
        if (hzbTexture != nullptr)
        {
            m_VegetationSystem->SetHiZDepth(
                hzbTexture->GetImage().GetImageView(),
                hzbTexture->GetImage().GetSampler(),
                static_cast<uint32_t>(m_MaterialManager.m_HIZMipCount),
                0.005f);
        }
        else
        {
            VANS_LOG_WARN("[SceneLoader] HZB texture not available — Hi-Z vegetation cull disabled.");
        }
    }

    // ── Build per-config GPU resources (allocates bone weights, remap, indirect draw) ──
    auto meshLookup = [this](const std::string& name) -> VansMesh* {
        return static_cast<VansMesh*>(GetMeshAsset(name));
    };
    auto materialLookup = [this](const std::string& name) -> VansMaterial* {
        return static_cast<VansMaterial*>(GetMaterialAsset(name));
    };
    m_VegetationSystem->BuildRenderConfigs(meshLookup, materialLookup);

    // Create the vegetation render node
    RenderNodeType type = RenderNodeType::VEGETATION_NODE;
    VansVegetationRenderNode* renderNode = new VansVegetationRenderNode(device, type);
    renderNode->SetVegetationSystem(m_VegetationSystem);

    // Assign the default grass material (kept for backward compat / fallback)
    VansMaterial* material = static_cast<VansMaterial*>(GetMaterialAsset(materialName));
    if (material)
        renderNode->m_Material = material;
    renderNode->m_Mesh = nullptr;  // No mesh asset — geometry is procedural / per-config
    renderNode->SetName(name);

    RegistRenderNode(renderNode, type);
    VANS_LOG("[AddVegetationNode] Vegetation node '" << name << "' created with " << instanceCount << " instances.");
}

// ===========================================================================
// ScriptableObject helpers
// ===========================================================================

VansScriptObject* VansGraphics::VansScene::FindObjectByName(const std::string& name) const
{
    for (auto* obj : m_SceneObjects)
    {
        if (obj && obj->m_ObjectName == name)
            return obj;
    }
    return nullptr;
}

// ===========================================================================
// LoadSceneObjects  — new "objects" JSON format
// ===========================================================================

void VansGraphics::VansScene::LoadSceneObjects(VkDevice& device, json& objectsArray, const std::string& projectRoot)
{
    using namespace VansEngine;

    // ── First pass: create all Objects, render / physics / cloth components ──
    // Defer cloth collision-sphere objectRef resolution to second pass.
    // Also collect render-node parent links for a third pass.
    // Defer animation component resolution to a fourth pass (所有 render 节点均已创建后).
    struct ParentLink { std::string childName; std::string parentName; };
    struct PendingAnimComp
    {
        VansScriptObject*           obj;
        VansScriptAnimationComponent* comp;
        json                        animJson;
        std::string                 objectName;
    };

    std::vector<ParentLink>    parentLinks;
    std::vector<PendingAnimComp> pendingAnimComps;

    for (const auto& objJson : objectsArray)
    {
        VansScriptObject* obj = new VansScriptObject();
        obj->m_ObjectName = objJson.value("name", "");

        auto& components = objJson["components"];

        // ── Render component ──────────────────────────────────────────────
        if (components.contains("render"))
        {
            const auto& renderJson = components["render"];
            VansRenderNode* rn = LoadSingleRenderNode(device, renderJson);
            if (rn)
            {
                auto* rc = new VansScriptRenderComponent();
                rc->m_ComponentName = "render";
                rc->m_RenderNode = rn;
                obj->AddComponent(rc);
                obj->m_TransformID = rn->m_TransformID;

                // Collect parent link if present
                if (renderJson.contains("parent"))
                {
                    ParentLink link;
                    link.childName  = renderJson.value("name", "");
                    link.parentName = renderJson["parent"].get<std::string>();
                    parentLinks.push_back(link);
                }
            }
        }

        // ── Physics component ─────────────────────────────────────────────
        if (components.contains("physics"))
        {
            auto* renderComp = obj->GetComponent<VansScriptRenderComponent>();
            VansRenderNode* associatedNode = renderComp ? renderComp->m_RenderNode : nullptr;
            VansPhysicsNode* pn = LoadSinglePhysicsNode(components["physics"], associatedNode);
            if (pn)
            {
                auto* pc = new VansScriptPhysicsComponent();
                pc->m_ComponentName = "physics";
                pc->m_PhysicsNode = pn;
                obj->AddComponent(pc);
            }
        }

        // ── Cloth component (first pass — collisionSpheres objectRef deferred) ──
        if (components.contains("cloth"))
        {
            auto* renderComp = obj->GetComponent<VansScriptRenderComponent>();
            VansRenderNode* associatedNode = renderComp ? renderComp->m_RenderNode : nullptr;
            VansClothNode* cn = LoadSingleClothNode(components["cloth"], associatedNode);
            if (cn)
            {
                auto* cc = new VansScriptClothComponent();
                cc->m_ComponentName = "cloth";
                cc->m_ClothNode = cn;
                obj->AddComponent(cc);
            }
        }

        // ── Vehicle component ─────────────────────────────────────────────
        if (components.contains("vehicle"))
        {
            const auto& vehJson = components["vehicle"];

            // Resolve body and tire object references after all objects are loaded.
            // For now, store the raw JSON names and defer actual InitVehicle to second pass.
            // We store a placeholder VehicleComponent and resolve in phase 2.
            auto* vc = new VansScriptVehicleComponent();
            vc->m_ComponentName = "vehicle";
            vc->m_Vehicle = nullptr;  // resolved in second pass
            obj->AddComponent(vc);
        }

        // ── Animation component ───────────────────────────────────────────
        if (components.contains("animation"))
        {
            // 创建占位符，延迟到第四阶段（所有对象的 render 组件全部创建后）再调用
            // LoadSingleAnimationComponent，确保依赖的 MultiMeshGroup 已存在
            auto* ac = new VansScriptAnimationComponent();
            ac->m_ComponentName = "animation";
            obj->AddComponent(ac);

            PendingAnimComp pending;
            pending.obj        = obj;
            pending.comp       = ac;
            pending.animJson   = components["animation"];
            pending.objectName = obj->m_ObjectName;
            pendingAnimComps.push_back(std::move(pending));
        }

        // ── Python script components ──────────────────────────────────────
        if (objJson.contains("pyScripts"))
        {
            for (const auto& scriptEntry : objJson["pyScripts"])
            {
                auto* pyComp = new VanPyScriptComponent();
                pyComp->m_ComponentName  = "PyScript";
                pyComp->m_ScriptPath     = scriptEntry["path"].get<std::string>();
                pyComp->m_ScriptClassName = scriptEntry["class"].get<std::string>();
                pyComp->m_OwnerObject    = obj;
                obj->AddComponent(pyComp);
            }
        }

        m_SceneObjects.push_back(obj);
        VANS_LOG("[LoadSceneObjects] Created object '" << obj->m_ObjectName << "'");
    }

    // ── Second pass: resolve Vehicle component references ─────────────────
    int objIndex = 0;
    for (const auto& objJson : objectsArray)
    {
        auto& components = objJson["components"];
        if (components.contains("vehicle"))
        {
            const auto& vehJson = components["vehicle"];
            VansScriptObject* obj = m_SceneObjects[m_SceneObjects.size() - objectsArray.size() + objIndex];
            auto* vc = obj->GetComponent<VansScriptVehicleComponent>();

            // Resolve body object → render node name
            std::string bodyNodeName;
            if (vehJson.contains("bodyObject"))
            {
                std::string bodyObjName = vehJson["bodyObject"].get<std::string>();
                VansScriptObject* bodyObj = FindObjectByName(bodyObjName);
                if (bodyObj)
                {
                    auto* rc = bodyObj->GetComponent<VansScriptRenderComponent>();
                    if (rc && rc->m_RenderNode)
                        bodyNodeName = rc->m_RenderNode->m_NodeName;
                }
            }
            // fallback: legacy bodyRenderNode
            if (bodyNodeName.empty() && vehJson.contains("bodyRenderNode"))
                bodyNodeName = vehJson["bodyRenderNode"].get<std::string>();

            // Resolve tire objects → render node names
            std::vector<std::string> tireNodeNames;
            if (vehJson.contains("tireObjects"))
            {
                for (const auto& t : vehJson["tireObjects"])
                {
                    std::string tireObjName = t.get<std::string>();
                    VansScriptObject* tireObj = FindObjectByName(tireObjName);
                    if (tireObj)
                    {
                        auto* rc = tireObj->GetComponent<VansScriptRenderComponent>();
                        if (rc && rc->m_RenderNode)
                        {
                            tireNodeNames.push_back(rc->m_RenderNode->m_NodeName);
                            continue;
                        }
                    }
                    tireNodeNames.push_back(tireObjName); // fallback: treat as node name
                }
            }
            // fallback: legacy tireRenderNodes
            if (tireNodeNames.empty() && vehJson.contains("tireRenderNodes"))
            {
                for (const auto& t : vehJson["tireRenderNodes"])
                    tireNodeNames.push_back(t.get<std::string>());
            }

            glm::vec3 spawnPos(0.0f, 5.0f, 0.0f);
            if (vehJson.contains("position"))
            {
                auto& p = vehJson["position"];
                spawnPos = glm::vec3(p[0].get<float>(), p[1].get<float>(), p[2].get<float>());
            }

            InitVehicle(&VansEngine::VansPhysicsSystem::GetInstance(), spawnPos, bodyNodeName, tireNodeNames);
            if (vc)
                vc->m_Vehicle = m_Vehicle;
        }
        ++objIndex;
    }

    // ── Third pass: resolve transform parent links ────────────────────────
    for (const auto& link : parentLinks)
    {
        if (link.childName.empty() || link.parentName.empty()) continue;
        VansRenderNode* childNode  = FindRenderNodeByName(link.childName);
        VansRenderNode* parentNode = FindRenderNodeByName(link.parentName);
        if (childNode && parentNode)
        {
            m_TransformParentSystem.SetParent(childNode->m_TransformID, parentNode->m_TransformID);
            VANS_LOG("[TransformParent] '" << link.childName << "' parented to '" << link.parentName << "'");
        }
    }

    // ── Fourth pass: resolve animation components ─────────────────────────
    // 此时所有 render 组件（及对应 MultiMeshGroup）均已创建完毕
    for (auto& pending : pendingAnimComps)
    {
        VansAnimationNode* animNode = LoadSingleAnimationComponent(
            pending.animJson, pending.objectName, projectRoot);
        pending.comp->m_AnimNode = animNode;

        if (!animNode)
        {
            VANS_LOG_WARN("[LoadSceneObjects] Animation component for '"
                         << pending.objectName << "' could not be created");
        }
    }
}

// ===========================================================================
// AutoCreateObjectsFromLegacy — wrap old-style nodes into VansScriptObjects
// ===========================================================================

void VansGraphics::VansScene::AutoCreateObjectsFromLegacy()
{
    // Wrap each opaque render node into an implicit object
    for (auto* rn : m_OpaqueRenderNodes)
    {
        if (!rn) continue;
        VansScriptObject* obj = new VansScriptObject();
        obj->m_ObjectName  = rn->m_NodeName;
        obj->m_TransformID = rn->m_TransformID;

        auto* rc = new VansScriptRenderComponent();
        rc->m_ComponentName = "render";
        rc->m_RenderNode = rn;
        obj->AddComponent(rc);

        m_SceneObjects.push_back(obj);
    }

    // Wrap each transparent render node
    for (auto* rn : m_TransParentRenderNodes)
    {
        if (!rn) continue;
        VansScriptObject* obj = new VansScriptObject();
        obj->m_ObjectName  = rn->m_NodeName;
        obj->m_TransformID = rn->m_TransformID;

        auto* rc = new VansScriptRenderComponent();
        rc->m_ComponentName = "render";
        rc->m_RenderNode = rn;
        obj->AddComponent(rc);

        m_SceneObjects.push_back(obj);
    }

    // Attach physics nodes to matching objects (by transform ID)
    for (auto* pn : m_PhysicsNodes)
    {
        if (!pn) continue;
        for (auto* obj : m_SceneObjects)
        {
            if (obj->m_TransformID == pn->GetTransformID())
            {
                auto* pc = new VansScriptPhysicsComponent();
                pc->m_ComponentName = "physics";
                pc->m_PhysicsNode = pn;
                obj->AddComponent(pc);
                break;
            }
        }
    }

    // Attach cloth nodes to matching objects (via target render node)
    for (auto* cn : m_ClothNodes)
    {
        if (!cn) continue;
        VansRenderNode* targetRN = cn->GetTargetRenderNode();
        if (!targetRN) continue;
        for (auto* obj : m_SceneObjects)
        {
            auto* rc = obj->GetComponent<VansScriptRenderComponent>();
            if (rc && rc->m_RenderNode == targetRN)
            {
                auto* cc = new VansScriptClothComponent();
                cc->m_ComponentName = "cloth";
                cc->m_ClothNode = cn;
                obj->AddComponent(cc);
                break;
            }
        }
    }

    // Attach vehicle to a matching object (by body render node)
    if (m_Vehicle)
    {
        const std::string& bodyName = m_Vehicle->GetBodyRenderNodeName();
        VansRenderNode* bodyRN = FindRenderNodeByName(bodyName);
        if (bodyRN)
        {
            for (auto* obj : m_SceneObjects)
            {
                auto* rc = obj->GetComponent<VansScriptRenderComponent>();
                if (rc && rc->m_RenderNode == bodyRN)
                {
                    auto* vc = new VansScriptVehicleComponent();
                    vc->m_ComponentName = "vehicle";
                    vc->m_Vehicle = m_Vehicle;
                    obj->AddComponent(vc);
                    break;
                }
            }
        }
    }

    VANS_LOG("[AutoCreateObjectsFromLegacy] Created " << m_SceneObjects.size() << " implicit objects from legacy data.");
}
