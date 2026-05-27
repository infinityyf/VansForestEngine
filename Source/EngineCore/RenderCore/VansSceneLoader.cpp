#include "../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansScene.h"
#include "VansShaderRegistry.h"
#include "BRDFData/VansLight.h"
#include "../Configration/VansConfigration.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../ScriptCore/VansScriptContext.h"
#include "../PhysicsCore/VansPhysics.h"
#include "../PhysicsCore/VansCharacterControllerNode.h"
#include "../PhysicsCore/VansTerrainPhysicsNode.h"
#include "../PhysicsCore/VansRagdollSystem.h"
#include "VansVideoManager.h"
#include "../AudioCore/VansAudioManager.h"
#include "../AudioCore/VansAudioSystem.h"
#include "VansParticleRenderNode.h"
#include "../ParticleCore/VansParticleManager.h"
#include "../ParticleCore/VansParticleAsset.h"
#include "../ParticleCore/VansParticleRuntime.h"

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
#include "../AnimationCore/VansBoneAttachmentSystem.h"
#include "../AnimationCore/VansSkinnedMeshLoader.h"

#include "../../EngineCore/EditorCore/AssetsSystem/VansAssetsFileWatcher.h"
#include "../Util/VansLog.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <filesystem>
#include <mutex>

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

	// 切换项目时清空旧项目的视频纹理（项目级资源随项目生命周期管理）
	m_VideoManager.Clear();

	// 切换项目时清空旧项目的音频资源（项目级资源随项目生命周期管理）
	m_AudioManager.Clear();

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

	// ── VideoComponent Bindless 槽绑定（必须在 PreparePBRMaterialData 之后执行）──
	// PreparePBRMaterialData 会为每个 EmissiveMaterial 分配 m_MaterialIndex。
	// 此处遍历场景对象，将拥有 EmissiveMaterial 视频的对象上的 VideoComponent
	// 绑定到对应的 Bindless 槽位，以便运行时 SwitchSource 可直接更新 GPU 描述符。
	for (auto* obj : m_SceneObjects)
	{
		auto* videoComp  = obj->GetComponent<VansScriptVideoComponent>();
		auto* renderComp = obj->GetComponent<VansScriptRenderComponent>();
		if (!videoComp || !renderComp || !renderComp->m_RenderNode || !renderComp->m_RenderNode->m_Material)
			continue;
		auto* emissiveMat = dynamic_cast<VansEmissiveMaterial*>(renderComp->m_RenderNode->m_Material);
		if (!emissiveMat || emissiveMat->m_VideoName.empty() || emissiveMat->m_MaterialIndex < 0)
			continue;
		const int kSlotsPerMat = 5;
		videoComp->m_BindlessFirstSlot  = emissiveMat->m_MaterialIndex * kSlotsPerMat;
		videoComp->m_MaterialManagerRef = &m_MaterialManager;
		VANS_LOG("[LoadSceneForRendering] VideoComponent '" << obj->m_ObjectName
			<< "' Bindless 槽 " << videoComp->m_BindlessFirstSlot
			<< "~" << videoComp->m_BindlessFirstSlot + kSlotsPerMat - 1 << " 已绑定");
	}
	device->PrepareInstanceTransformData();
	CreateGlobalDescriptorSet(device->GetLogicDevice());
	// 将 Global Descriptor Set（Set 0）同步到 MaterialManager，供视频切换时直接更新 Bindless 槽
	m_MaterialManager.m_VideoBindlessDescriptorSet = m_GlobalDescriptorSet;
	// Write TileLight SSBOs (created during BeforeRendering) into global Set 0 bindings 9/10.
	UpdateGlobalTileLightDescriptors();
	CreateNodeDescriptorSets();
	device->PrepareRayTracingData();

	// 记录本次加载模式
	m_LoadMode = mode;

	// 场景就绪后恢复视频播放（GPU 资源已全部重建，此时启动播放安全）
	m_VideoManager.PlayAll();

	m_SceneState = VansSceneState::Ready;
	VANS_LOG("[VansScene] 场景就绪，可以开始渲染");
}

} // namespace VansGraphics

// ---------------------------------------------------------------------------
// JSON type-string helpers
// ---------------------------------------------------------------------------
static VansMaterialType ParseMaterialType(const json& typeValue, const std::string& materialName)
{
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
        if (s == "emissive")     return VansMaterialType::VAN_EMISSIVE;
        if (s == "decal")        return VansMaterialType::VAN_DECAL;
        VANS_LOG_WARN("[ParseMaterialType] Material '" << materialName << "': unknown type string '" << s << "', defaulting to pbr.");
    }
    return VansMaterialType::VAN_PBR;
}

static VansGraphics::RenderNodeType ParseRenderNodeType(const json& typeValue, const std::string& nodeName)
{
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
        if (s == "decal")        return VansGraphics::DECAL_NODE;
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
    case VansGraphics::DECAL_NODE:
        // 贴花节点：OBB 投影贴花，写入 GBuffer Normal/GBuffer0/GBuffer1
        renderNode = new VansDecalRenderNode(device);
        break;
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
    config.terrainSize = terrainData.value("terrainSize", config.terrainSize);
    config.maxHeight = terrainData.value("maxHeight", config.maxHeight);
    config.heightOffset = terrainData.value("heightOffset", config.heightOffset);
    config.splitDistMult = terrainData.value("splitDistMult", config.splitDistMult);
    config.lodDistanceRatio = terrainData.value("lodDistanceRatio", config.lodDistanceRatio);
    config.morphStartRatio = terrainData.value("morphStartRatio", config.morphStartRatio);
    config.maxPatchInstances = terrainData.value("maxPatchInstances", config.maxPatchInstances);

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

    // Terrain 物理碰撞是可选项，只由 terrain.collision.enabled 控制。
    if (terrainData.contains("collision") && terrainData["collision"].is_object())
    {
        auto& collisionJson = terrainData["collision"];
        VansEngine::TerrainPhysicsProperties terrainPhysicsProps;
        terrainPhysicsProps.enabled = collisionJson.value("enabled", false);
        terrainPhysicsProps.heightmapPath = config.heightmapPath;
        terrainPhysicsProps.terrainSize = config.terrainSize;
        terrainPhysicsProps.maxHeight = config.maxHeight;
        terrainPhysicsProps.heightOffset = config.heightOffset;

        if (collisionJson.contains("terrainSize"))
            terrainPhysicsProps.terrainSize = collisionJson["terrainSize"].get<float>();
        if (collisionJson.contains("maxHeight"))
            terrainPhysicsProps.maxHeight = collisionJson["maxHeight"].get<float>();
        if (collisionJson.contains("heightOffset"))
            terrainPhysicsProps.heightOffset = collisionJson["heightOffset"].get<float>();
        if (collisionJson.contains("layer"))
            terrainPhysicsProps.layerName = collisionJson["layer"].get<std::string>();
        if (collisionJson.contains("flipX"))
            terrainPhysicsProps.flipX = collisionJson["flipX"].get<bool>();
        if (collisionJson.contains("flipZ"))
            terrainPhysicsProps.flipZ = collisionJson["flipZ"].get<bool>();

        if (collisionJson.contains("material") && collisionJson["material"].is_object())
        {
            auto& materialJson = collisionJson["material"];
            if (materialJson.contains("staticFriction"))
                terrainPhysicsProps.material.staticFriction = materialJson["staticFriction"].get<float>();
            if (materialJson.contains("dynamicFriction"))
                terrainPhysicsProps.material.dynamicFriction = materialJson["dynamicFriction"].get<float>();
            if (materialJson.contains("restitution"))
                terrainPhysicsProps.material.restitution = materialJson["restitution"].get<float>();
        }

        if (terrainPhysicsProps.enabled)
        {
            auto& physicsSystem = VansEngine::VansPhysicsSystem::GetInstance();
            std::lock_guard<std::mutex> simLock(physicsSystem.GetSimulationMutex());

            if (m_TerrainPhysicsNode)
            {
                delete m_TerrainPhysicsNode;
                m_TerrainPhysicsNode = nullptr;
            }

            m_TerrainPhysicsNode = new VansEngine::VansTerrainPhysicsNode();
            if (!m_TerrainPhysicsNode->Initialize(terrainPhysicsProps))
            {
                delete m_TerrainPhysicsNode;
                m_TerrainPhysicsNode = nullptr;
                VANS_LOG_WARN("[VansScene] Terrain collision initialization failed.");
            }
        }
    }
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

            // 动画配置由 object.components.animation 统一处理。

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

    if (resourceData.contains("video") && resourceData["video"].is_array())
    {
        m_VideoManager.LoadFromJson(resourceData["video"], assetPrefix, vkDevice);
    }

    if (resourceData.contains("audio") && resourceData["audio"].is_array())
    {
        m_AudioManager.LoadFromJson(resourceData["audio"], assetPrefix);
    }

    VANS_LOG("[VansScene] Resources loaded: "
             << m_Meshes.size() << " meshes, "
             << m_Textures.size() << " textures, "
             << m_Shaders.size() << " shaders, "
             << m_VideoManager.Count() << " videos, "
             << m_AudioManager.Count() << " audios");
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
        case VansMaterialType::VAN_EMISSIVE:        material = new VansEmissiveMaterial();     break;
        case VansMaterialType::VAN_DECAL:           material = new VansDecalMaterial();        break;
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

        if (matType == VansMaterialType::VAN_EMISSIVE)
        {
            VansEmissiveMaterial* emissive = static_cast<VansEmissiveMaterial*>(material);

            // 发光颜色，默认纯白
            if (sceneMaterial.contains("emissive_color") && sceneMaterial["emissive_color"].is_array())
            {
                emissive->m_BasePBRParam.m_albedo = glm::vec3(
                    sceneMaterial["emissive_color"][0],
                    sceneMaterial["emissive_color"][1],
                    sceneMaterial["emissive_color"][2]);
            }
            else
            {
                emissive->m_BasePBRParam.m_albedo = glm::vec3(1.0f);
            }

            // 发光强度，写入 roughness 插槽，默认 1.0（支持 HDR，>1.0 有效）
            emissive->m_BasePBRParam.m_roughness = sceneMaterial.value("emissive_intensity", 1.0f);
            emissive->m_BasePBRParam.m_metallic  = 0.0f;
            emissive->m_BasePBRParam.m_ao        = 1.0f;

            // Slot 0: 自发光纹理（可选，未指定时使用 defaultAlbedo 纯白占位）
            {
                VansTexture* tex = nullptr;
                if (sceneMaterial.contains("emissive_texture"))
                {
                    auto textureName = sceneMaterial["emissive_texture"].get<std::string>();
                    tex = static_cast<VansTexture*>(GetTextureAsset(textureName));
                }
                if (tex == nullptr)
                    tex = static_cast<VansTexture*>(GetTextureAsset("defaultAlbedo"));
                emissive->m_EmissiveTexture = tex;
            }

            // 视频纹理：若指定了 emissive_video，用视频纹理替换发光纹理槽
            // GPU 纹理数据由 VansScriptVideoComponent 统一管理；此处仅绑定 GPU 句柄和记录名称
            if (sceneMaterial.contains("emissive_video"))
            {
                const std::string videoName = sceneMaterial["emissive_video"];
                VansVideoTexture* videoTex  = m_VideoManager.Get(videoName);
                if (videoTex && videoTex->IsReady())
                {
                    emissive->m_EmissiveTexture = videoTex->GetTexture();
                    emissive->m_VideoName       = videoName;
                    VANS_LOG("[VansScene] Emissive 材质绑定视频纹理: " << videoName);
                }
                else
                {
                    VANS_LOG_WARN("[VansScene] emissive_video 未找到或未就绪: " << videoName);
                }
            }
        }

        if (matType == VansMaterialType::VAN_DECAL)
        {
            VansDecalMaterial* decal = static_cast<VansDecalMaterial*>(material);

            // albedo 颜色乘数
            if (sceneMaterial.contains("albedo") && sceneMaterial["albedo"].is_array())
                decal->m_BasePBRParam.m_albedo = glm::vec3(sceneMaterial["albedo"][0], sceneMaterial["albedo"][1], sceneMaterial["albedo"][2]);
            else
                decal->m_BasePBRParam.m_albedo = glm::vec3(1.0f);

            decal->m_BasePBRParam.m_metallic  = sceneMaterial.value("metallic",  0.0f);
            decal->m_BasePBRParam.m_roughness = sceneMaterial.value("roughness", 0.5f);
            decal->m_BasePBRParam.m_ao        = sceneMaterial.value("ao",        1.0f);

            // 贴图绑定（与 PBR 格式相同，未指定时使用 default 占位纹理）
            {
                auto loadTex = [&](const std::string& key, const std::string& fallback) -> VansTexture* {
                    if (sceneMaterial.contains(key))
                    {
                        VansTexture* tex = static_cast<VansTexture*>(GetTextureAsset(sceneMaterial[key].get<std::string>()));
                        if (tex) return tex;
                    }
                    return static_cast<VansTexture*>(GetTextureAsset(fallback));
                };
                decal->m_BaseColorTexture  = loadTex("basecolor_texture", "defaultAlbedo");
                decal->m_NormalTexture     = loadTex("normal_texture",    "defaultNormal");
                decal->m_MetalTexture      = loadTex("metal_texture",     "defaultMetal");
                decal->m_RoughnessTexture  = loadTex("roughness_texture", "defaultRoughness");
                decal->m_AoTexture         = loadTex("ao_texture",        "defaultAo");
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

    // 从 scene path 推导 project root（Scenes/ → 项目根目录）
    // 供 LoadSceneObjects 解析相对路径共用
    std::string scenePath(path);
    std::string projectRoot = scenePath.substr(0, scenePath.find_last_of("/\\") + 1);
    if (!projectRoot.empty())
    {
        size_t pos = projectRoot.substr(0, projectRoot.size() - 1).find_last_of("/\\");
        if (pos != std::string::npos)
            projectRoot = projectRoot.substr(0, pos + 1);
    }

    // 场景文件只负责材质和实例数据；mesh/texture/shader/video 已由 resource.json 加载。
    if (sceneData.contains("material") && sceneData["material"].is_array())
    {
        LoadMaterialsFromJson(sceneData["material"]);
    }

    if (!sceneData.contains("scene") || !sceneData["scene"].is_array() || sceneData["scene"].empty())
    {
        VANS_LOG_ERROR("[VansScene] Scene file has no valid scene array: " << path);
        return false;
    }

    json& sceneNode = sceneData["scene"][0];

    if (!sceneNode.contains("objects") || !sceneNode["objects"].is_array())
    {
        VANS_LOG_ERROR("[VansScene] Scene file must contain scene[0].objects array: " << path);
        return false;
    }

    LoadSceneObjects(nativeDevice, sceneNode["objects"], projectRoot);

    if (sceneNode.contains("rendernode") && sceneNode["rendernode"].is_array()
        && !sceneNode["rendernode"].empty())
    {
        LoadRenderNodes(nativeDevice, sceneNode["rendernode"]);
    }

    // 灯光统一由 object.components.*_light 创建；此处只创建 GPU 侧固定容量 buffer。
    m_LightManager.CreateLightUniformData(nativeDevice);

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
    if (entry.enableDecalBlend)     shader->SetEnableDecalBlend(VK_TRUE);
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

    // ExpandMultiMesh 仅负责几何体 → 渲染节点的展开，动画由 animation component 创建。
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

    if (animJson.contains("bone_bindings") && animJson["bone_bindings"].is_array())
    {
        using namespace VansEngine;

        auto readVec3 = [](const json& source, const char* key, const glm::vec3& defaultValue) -> glm::vec3
        {
            if (!source.contains(key) || !source[key].is_array() || source[key].size() < 3)
                return defaultValue;

            return glm::vec3(
                source[key][0].get<float>(),
                source[key][1].get<float>(),
                source[key][2].get<float>());
        };

        auto parseShapeType = [](const std::string& value) -> PhysicsColliderType
        {
            if (value == "box") return PhysicsColliderType::Box;
            if (value == "sphere") return PhysicsColliderType::Sphere;
            if (value == "capsule") return PhysicsColliderType::Capsule;
            if (value == "mesh") return PhysicsColliderType::Mesh;
            if (value == "convex") return PhysicsColliderType::ConvexMesh;
            return PhysicsColliderType::Capsule;
        };

        BoneColliderBindingSet bindingSet;
        bindingSet.animNode = animNode;

        const Skeleton& skeleton = meshAsset->m_AnimImportResult.skeleton;
        for (const auto& bindJson : animJson["bone_bindings"])
        {
            BoneColliderBinding binding;
            binding.boneName          = bindJson.value("bone_name", "");
            binding.physicsObjectName = bindJson.value("physics_object", "");
            binding.offsetPosition    = readVec3(bindJson, "offset_position", glm::vec3(0.0f));
            binding.offsetRotation    = readVec3(bindJson, "offset_rotation", glm::vec3(0.0f));
            binding.offsetScale       = readVec3(bindJson, "offset_scale", glm::vec3(1.0f));
            binding.syncRotation      = bindJson.value("sync_rotation", true);
            binding.syncScale         = bindJson.value("sync_scale", false);
            binding.layerName         = bindJson.value("layer", "Default");
            binding.isTrigger         = bindJson.value("is_trigger", false);
            binding.enabled           = bindJson.value("enabled", true);
            binding.autoCreateNode    = bindJson.value("auto_create_node", false);
            binding.shapeExtents      = readVec3(bindJson, "shape_extents", glm::vec3(0.1f, 0.25f, 0.1f));
            binding.shapeType         = parseShapeType(bindJson.value("shape_type", "capsule"));

            auto boneIt = skeleton.boneNameToIndex.find(binding.boneName);
            if (boneIt != skeleton.boneNameToIndex.end())
                binding.boneIndex = boneIt->second;
            else
                VANS_LOG_WARN("[LoadAnimComp] bone binding references missing bone '" << binding.boneName << "'");

            if (!binding.physicsObjectName.empty())
            {
                for (auto* physicsNode : m_PhysicsNodes)
                {
                    if (physicsNode && physicsNode->GetName() == binding.physicsObjectName)
                    {
                        binding.physicsNode = physicsNode;
                        binding.attachmentTransformID = physicsNode->GetTransformID();
                        binding.ownsAttachmentTransform = false;
                        break;
                    }
                }

                if (binding.physicsNode == nullptr)
                {
                    VansScriptObject* physicsObject = FindObjectByName(binding.physicsObjectName);
                    auto* physicsComp = physicsObject ? physicsObject->GetComponent<VansScriptPhysicsComponent>() : nullptr;
                    if (physicsComp && physicsComp->m_PhysicsNode)
                    {
                        binding.physicsNode = physicsComp->m_PhysicsNode;
                        binding.attachmentTransformID = binding.physicsNode->GetTransformID();
                        binding.ownsAttachmentTransform = false;
                    }
                }

                if (binding.physicsNode == nullptr)
                {
                    VANS_LOG_WARN("[LoadAnimComp] bone binding physics object not found: " << binding.physicsObjectName);
                }
                else if (binding.physicsNode->GetProperties().bodyType != PhysicsBodyType::Kinematic &&
                         !binding.physicsNode->GetProperties().isTrigger)
                {
                    VANS_LOG_WARN("[LoadAnimComp] bone binding physics object '" << binding.physicsObjectName
                                  << "' is not kinematic/trigger; PhysX may override its transform");
                }
            }

            if (binding.attachmentTransformID == UINT32_MAX)
            {
                binding.attachmentTransformID = VansTransformStore::AllocateTransform();
                binding.ownsAttachmentTransform = true;
            }

            bindingSet.bindings.push_back(std::move(binding));
        }

        VansBoneAttachmentSystem::GetInstance().RegisterBindingSet(std::move(bindingSet));
        VANS_LOG("[LoadAnimComp] Registered " << animJson["bone_bindings"].size()
                 << " bone collider binding(s) for '" << nodeName << "'");
    }

    VANS_LOG("[LoadAnimComp] Created animation component '" << nodeName
             << "' with " << controller->GetClipNames().size() << " clip(s), "
             << meshAsset->m_AnimImportResult.skeleton.bones.size() << " bones, "
             << group.childNodes.size() << " render node(s)");

    return animNode;
}

bool VansGraphics::VansScene::LoadSingleRagdollComponent(
    VansScriptObject* obj,
    VansAnimationNode* animNode,
    const json& ragdollJson,
    const std::string& projectRoot)
{
    if (obj == nullptr || animNode == nullptr || !ragdollJson.is_object())
        return false;

    std::string profilePath = ragdollJson.value("profile", "");
    if (profilePath.empty())
    {
        VANS_LOG_WARN("[LoadRagdollComp] object '" << obj->m_ObjectName << "' ragdoll missing profile");
        return false;
    }

    std::string fullProfilePath = projectRoot + profilePath;
    VansEngine::RagdollProfile profile;
    if (!VansEngine::RagdollProfile::LoadFromFile(fullProfilePath, profile))
    {
        VANS_LOG_WARN("[LoadRagdollComp] failed to load profile: " << fullProfilePath);
        return false;
    }

    VansAnimationController* controller = animNode->GetController();
    if (controller == nullptr)
        return false;

    if (controller->GetCachedGlobalTransforms().empty())
        controller->Update(0.0f, animNode->GetSkeleton());

    if (controller->GetCachedGlobalTransforms().empty())
    {
        VANS_LOG_WARN("[LoadRagdollComp] controller did not produce bind pose for '" << obj->m_ObjectName << "'");
        return false;
    }

    if (VansEngine::VansBoneAttachmentSystem::GetInstance().FindBindingSet(animNode) != nullptr)
    {
        VANS_LOG_WARN("[LoadRagdollComp] object '" << obj->m_ObjectName
            << "' 同时配置了 bone_bindings 与 ragdoll；Physics/Blend 模式下请避免绑定同一骨骼");
    }

    if (!VansEngine::VansRagdollSystem::GetInstance().CreateRagdoll(animNode, profile))
        return false;

    auto parseMode = [](const std::string& value) -> VansEngine::RagdollDriveMode
    {
        if (value == "physics") return VansEngine::RagdollDriveMode::Physics;
        if (value == "blend") return VansEngine::RagdollDriveMode::Blend;
        return VansEngine::RagdollDriveMode::Animation;
    };

    VansEngine::RagdollDriveMode mode = parseMode(ragdollJson.value("drive_mode", "animation"));
    float blendWeight = ragdollJson.value("blend_weight", 0.0f);

    VansEngine::VansRagdollSystem::GetInstance().SetBlendWeight(animNode, blendWeight);
    VansEngine::VansRagdollSystem::GetInstance().SetDriveMode(animNode, mode);

    auto* ragdollComp = new VansScriptRagdollComponent();
    ragdollComp->m_AnimNode = animNode;
    ragdollComp->m_InitialDriveMode = mode;
	ragdollComp->m_ProfilePath = profilePath;
	ragdollComp->m_ProfileName = profile.name;
	ragdollComp->m_ConfiguredBodyCount = static_cast<int>(profile.bodies.size());
	ragdollComp->m_ConfiguredJointCount = static_cast<int>(profile.joints.size());
    obj->AddComponent(ragdollComp);

    // ── 延迟绑定消费：若同对象 CCT 配置了 followRagdoll，绑定刚创建的 animNode ──
    auto* cctComp = obj->GetComponent<VansScriptCharacterControllerComponent>();
    if (cctComp && cctComp->m_ControllerNode &&
        cctComp->m_ControllerNode->HasPendingFollowRagdoll())
    {
        cctComp->m_ControllerNode->SetFollowRagdoll(
            animNode, cctComp->m_ControllerNode->GetPendingFollowRagdollBone());
        cctComp->m_ControllerNode->ConsumePendingFollowRagdoll();
        VANS_LOG("[LoadRagdollComp] CCT followRagdoll \u7ed1\u5b9a\u5b8c\u6210\uff0cobjName='" << obj->m_ObjectName
            << "' bone='" << cctComp->m_ControllerNode->GetFollowRagdollBone() << "'");
    }

    VANS_LOG("[LoadRagdollComp] Created ragdoll component for '" << obj->m_ObjectName
        << "' profile='" << profile.name << "' bodies=" << profile.bodies.size());
    return true;
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
                // 读取植被覆盖参数；未配置时复用 terrain 运行时参数，避免高度不一致
                float terrainMaxHeight = vegetationData.value("terrainMaxHeight", terrain->GetMaxHeight());
                float terrainHeightOffset = vegetationData.value("terrainHeightOffset", terrain->GetHeightOffset());

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

    // ── First pass: create all Objects and component instances ────────────
    // animation component 需要等待所有 render 节点创建完毕后再解析。
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

    // ── 对象级 Transform 解析 helper ─────────────────────────────────────
    auto parseObjTransform = [](const json& objJson,
                                glm::vec3& outPos,
                                glm::vec3& outRot,
                                glm::vec3& outScl) -> bool
    {
        if (!objJson.contains("transform")) return false;
        const auto& t = objJson["transform"];
        if (t.contains("position") && t["position"].is_array())
            outPos = glm::vec3(t["position"][0].get<float>(),
                               t["position"][1].get<float>(),
                               t["position"][2].get<float>());
        if (t.contains("rotation") && t["rotation"].is_array())
            outRot = glm::vec3(t["rotation"][0].get<float>(),
                               t["rotation"][1].get<float>(),
                               t["rotation"][2].get<float>());
        if (t.contains("scale") && t["scale"].is_array())
            outScl = glm::vec3(t["scale"][0].get<float>(),
                               t["scale"][1].get<float>(),
                               t["scale"][2].get<float>());
        return true;
    };

    for (const auto& objJson : objectsArray)
    {
        VansScriptObject* obj = new VansScriptObject();
        obj->m_ObjectName = objJson.value("name", "");

        // ── 读取对象级 transform（新格式：与 components 并列）────────────
        glm::vec3 objPos(0.0f), objRot(0.0f), objScl(1.0f);
        bool hasObjTransform = parseObjTransform(objJson, objPos, objRot, objScl);

        auto& components = objJson["components"];

        // ── Render component ──────────────────────────────────────────────
        if (components.contains("render"))
        {
            const auto& renderJson = components["render"];

            // multi-mesh 展开在 LoadSingleRenderNode 内完成，需要将对象级 transform 传入副本。
            VansRenderNode* rn = nullptr;
            if (hasObjTransform)
            {
                // 为 multi-mesh 展开传递对象级 transform：构造一个带 transform 的副本
                json renderJsonWithTransform = renderJson;
                renderJsonWithTransform["transform"] = objJson["transform"];
                rn = LoadSingleRenderNode(device, renderJsonWithTransform);
            }
            else
            {
                rn = LoadSingleRenderNode(device, renderJson);
            }

            // 多网格对象（multi-mesh）经由 ExpandMultiMeshToRenderNodes 展开，
            // LoadSingleRenderNode 会返回 nullptr 而不创建单个节点。
            // 此处回退为取该 MultiMeshGroup 第一个子节点作为代理，
            // 使 physics / CCT / cloth 组件能获取到合法的 TransformID。
            // 所有子节点共享 MultiMeshGroup::sharedTransformID，
            // 对该 TransformID 的任何写入都会同步移动整个角色。
            if (!rn)
            {
                std::string nodeName = renderJson.value("name", "");
                auto groupIt = m_MultiMeshGroups.find(nodeName);
                if (groupIt != m_MultiMeshGroups.end() && !groupIt->second.childNodes.empty())
                    rn = groupIt->second.childNodes[0];
            }

            if (rn)
            {
                if (hasObjTransform)
                    rn->SetTransformData(objPos, objRot, objScl);

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

        // ── CharController component ──────────────────────────────────────────
        if (components.contains("charController"))
        {
            auto* renderComp = obj->GetComponent<VansScriptRenderComponent>();
            VansRenderNode* associatedNode = renderComp ? renderComp->m_RenderNode : nullptr;
            VansEngine::VansCharacterControllerNode* cctNode =
                LoadSingleCharControllerNode(components["charController"], associatedNode);
            if (cctNode)
            {
                auto* cctComp = new VansScriptCharacterControllerComponent();
                cctComp->m_ControllerNode = cctNode;
                obj->AddComponent(cctComp);
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

        // ── Non-render TransformID 分配（灯光 / 相机等无 render 组件的对象）──
        bool objectTransformAllocated = false;

        auto ensureObjectTransform = [&]()
        {
            if (!objectTransformAllocated &&
                obj->GetComponent<VansScriptRenderComponent>() == nullptr)
            {
                obj->m_TransformID = VansTransformStore::AllocateTransform();
                if (objJson.contains("transform"))
                {
                    const auto& tJson = objJson["transform"];
                    auto& t = VansTransformStore::GetTransform(obj->m_TransformID);
                    if (tJson.contains("position") && tJson["position"].is_array())
                    {
                        t.m_Position = glm::vec3(tJson["position"][0].get<float>(),
                                                 tJson["position"][1].get<float>(),
                                                 tJson["position"][2].get<float>());
                    }
                    if (tJson.contains("rotation") && tJson["rotation"].is_array())
                    {
                        t.m_Rotation = glm::vec3(tJson["rotation"][0].get<float>(),
                                                 tJson["rotation"][1].get<float>(),
                                                 tJson["rotation"][2].get<float>());
                    }
                    t.m_Scale = glm::vec3(1.0f);
                }
                objectTransformAllocated = true;
            }
        };

        // ── Light components (方向光 / 点光源 / 聚光灯) ────────────────────
        {

            // ── 方向光 ────────────────────────────────────────────────────
            if (components.contains("directional_light"))
            {
                ensureObjectTransform();
                const auto& dlJson = components["directional_light"];
                VansDirectionalLight dirLight;
                if (dlJson.contains("color") && dlJson["color"].is_array())
                {
                    dirLight.m_Color = glm::vec3(dlJson["color"][0].get<float>(),
                                                 dlJson["color"][1].get<float>(),
                                                 dlJson["color"][2].get<float>());
                }
                else
                {
                    dirLight.m_Color = glm::vec3(1.0f);
                }
                dirLight.m_Intensity  = dlJson.value("intensity", 1.0f);
                // 方向由 SyncLightTransforms 每帧计算，此处仅给占位初始值
                dirLight.m_Direction  = glm::vec3(0.0f, 1.0f, 0.0f);

                int idx = (int)m_LightManager.GetDirectionLights().size();
                m_LightManager.AddDirectionalLight(dirLight);

                auto* dlComp = new VansScriptDirectionalLightComponent();
                dlComp->m_LightManager = &m_LightManager;
                dlComp->m_LightIndex   = idx;
                obj->AddComponent(dlComp);
                VANS_LOG("[LoadSceneObjects] 创建方向光组件 '" << obj->m_ObjectName << "' idx=" << idx);
            }

            // ── 点光源 ────────────────────────────────────────────────────
            if (components.contains("point_light"))
            {
                ensureObjectTransform();
                const auto& plJson = components["point_light"];
                VansPointLight pointLight;
                if (plJson.contains("color") && plJson["color"].is_array())
                {
                    pointLight.m_Color = glm::vec3(plJson["color"][0].get<float>(),
                                                   plJson["color"][1].get<float>(),
                                                   plJson["color"][2].get<float>());
                }
                else
                {
                    pointLight.m_Color = glm::vec3(1.0f);
                }
                pointLight.m_Intensity = plJson.value("intensity", 1.0f);
                pointLight.m_Radius    = plJson.value("radius", 10.0f);
                // 位置由 SyncLightTransforms 每帧覆盖
                pointLight.m_Position  = glm::vec3(0.0f);

                int idx = (int)m_LightManager.GetPointLights().size();
                m_LightManager.AddPointLight(pointLight);

                auto* plComp = new VansScriptPointLightComponent();
                plComp->m_LightManager = &m_LightManager;
                plComp->m_LightIndex   = idx;
                obj->AddComponent(plComp);
                VANS_LOG("[LoadSceneObjects] 创建点光源组件 '" << obj->m_ObjectName << "' idx=" << idx);
            }

            // ── 聚光灯 ────────────────────────────────────────────────────
            if (components.contains("spot_light"))
            {
                ensureObjectTransform();
                const auto& slJson = components["spot_light"];
                VansSpotLight spotLight;
                if (slJson.contains("color") && slJson["color"].is_array())
                {
                    spotLight.m_Color = glm::vec3(slJson["color"][0].get<float>(),
                                                  slJson["color"][1].get<float>(),
                                                  slJson["color"][2].get<float>());
                }
                else
                {
                    spotLight.m_Color = glm::vec3(1.0f);
                }
                spotLight.m_Intensity    = slJson.value("intensity", 1.0f);
                spotLight.m_Radius       = slJson.value("radius", 10.0f);
                spotLight.m_InnerCutOff  = glm::radians(slJson.value("innercutoff", 30.0f));
                spotLight.m_OuterCutOff  = glm::radians(slJson.value("outerCutoff", 45.0f));
                // 位置和方向由 SyncLightTransforms 每帧覆盖
                spotLight.m_Position     = glm::vec3(0.0f);
                spotLight.m_Direction    = glm::vec3(0.0f, 1.0f, 0.0f);

                int idx = (int)m_LightManager.GetSpotLight().size();
                m_LightManager.AddSpotLight(spotLight);

                auto* slComp = new VansScriptSpotLightComponent();
                slComp->m_LightManager = &m_LightManager;
                slComp->m_LightIndex   = idx;
                obj->AddComponent(slComp);
                VANS_LOG("[LoadSceneObjects] 创建聚光灯组件 '" << obj->m_ObjectName << "' idx=" << idx);
            }

            // ── 面光源 RectLight (LTC) ────────────────────────────────
            if (components.contains("rect_light"))
            {
                ensureObjectTransform();
                const auto& rlJson = components["rect_light"];
                VansRectLight rectLight{};
                if (rlJson.contains("color") && rlJson["color"].is_array())
                {
                    rectLight.m_Color = glm::vec3(rlJson["color"][0].get<float>(),
                                                  rlJson["color"][1].get<float>(),
                                                  rlJson["color"][2].get<float>());
                }
                else
                {
                    rectLight.m_Color = glm::vec3(1.0f);
                }
                rectLight.m_Intensity     = rlJson.value("intensity", 50.0f);
                rectLight.m_HalfWidth     = rlJson.value("width",  1.0f) * 0.5f;
                rectLight.m_HalfHeight    = rlJson.value("height", 1.0f) * 0.5f;
                rectLight.m_Range         = rlJson.value("range",  10.0f);
                rectLight.m_TwoSided      = rlJson.value("two_sided", false) ? 1.0f : 0.0f;
                rectLight.m_AttenuationExp= rlJson.value("attenuation_exp", 2.0f);
                // shadow 占位：-1 = 无阴影；真正槽位在 UpdateLightShadowMatrixData 阶段写入
                rectLight.m_ShadowIndex   = rlJson.value("shadow", false) ? 0.0f : -1.0f;
                // 位置与基底向量由 SyncLightTransforms 每帧覆盖
                rectLight.m_Position      = glm::vec3(0.0f);
                rectLight.m_Normal        = glm::vec3(0.0f, 0.0f, 1.0f);
                rectLight.m_Right         = glm::vec3(1.0f, 0.0f, 0.0f);
                rectLight.m_Up            = glm::vec3(0.0f, 1.0f, 0.0f);
                // 面光源发光贴图：解析 emissive_texture / emissive_video / texture_lod_bias
                rectLight.m_TextureSlot = -1.0f;
                rectLight.m_TexLodBias  = rlJson.value("texture_lod_bias", 0.0f);
                std::string emissiveTexPath;
                std::string emissiveVideoName;
                if (rlJson.contains("emissive_texture") && rlJson["emissive_texture"].is_string())
                {
                    emissiveTexPath = rlJson["emissive_texture"].get<std::string>();
                }
                if (rlJson.contains("emissive_video") && rlJson["emissive_video"].is_string())
                {
                    emissiveVideoName = rlJson["emissive_video"].get<std::string>();
                }

                int idx = (int)m_LightManager.GetRectLights().size();
                m_LightManager.AddRectLight(rectLight);

                auto* rlComp = new VansScriptRectLightComponent();
                rlComp->m_LightManager = &m_LightManager;
                rlComp->m_LightIndex   = idx;
                rlComp->m_EmissiveTexturePath = emissiveTexPath;

                // ── 视频发光贴图（优先级高于静态贴图；两者同时指定时 video 生效）──
                // 创建 VansScriptVideoComponent 统一管理视频播放控制；
                // 面光源只持有指向该组件的非拥有指针，不再直接访问播放参数。
                if (!emissiveVideoName.empty() && idx < 32)
                {
                    VansVideoTexture* videoTex = m_VideoManager.Get(emissiveVideoName);
                    if (videoTex != nullptr)
                    {
                        // VideoComponent 只由显式 "video" JSON 组件创建；
                        // rlComp->m_VideoComponent 由每对象处理末尾的后处理块统一绑定。

                        // 激活 TextureSlot：使着色器进入 RECT_LIGHT_EMISSIVE_ENABLED 分支
                        m_LightManager.GetRectLights()[idx].m_TextureSlot = static_cast<float>(idx);

                        // 向 emissive 数组层写入白色占位帧：
                        // 视频第一帧到达前，着色器已进入纹理采样分支（texSlot >= 0），
                        // 若该层为黑色则 diffLightTerm = 0，面光源完全不发光。
                        // 写入一个 1×1 白色像素（UpdateArrayLayerFromPixels 会 resize 到 256×256），
                        // 使面光源在视频播放前以 baseTint 颜色正常发光，视频帧到来后逐帧覆盖。
                        VansTexture* emissiveArray = m_MaterialManager.GetRuntimeRenderTexture(
                            VansMaterialManager::RT_RECT_LIGHT_EMISSIVE);
                        if (emissiveArray != nullptr)
                        {
                            static const uint8_t kWhitePixel[4] = { 255, 255, 255, 255 };
                            VansVKDevice* texVkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
                            emissiveArray->UpdateArrayLayerFromPixels(
                                texVkDevice->GetCommandBuffer(), kWhitePixel, 1, 1, idx);
                        }

                        VANS_LOG("[LoadSceneObjects] 面光源 '" << obj->m_ObjectName << "' 绑定视频发光 '"
                            << emissiveVideoName << "' slot=" << idx);
                    }
                    else
                    {
                        VANS_LOG_WARN("[LoadSceneObjects] 面光源 '" << obj->m_ObjectName
                            << "' emissive_video '" << emissiveVideoName << "' 未找到，回退到静态贴图");
                        // video 未找到时降级到静态贴图（若有）
                        if (!emissiveTexPath.empty())
                        {
                            VansTexture* emissiveArray = m_MaterialManager.GetRuntimeRenderTexture(
                                VansMaterialManager::RT_RECT_LIGHT_EMISSIVE);
                            if (emissiveArray != nullptr)
                            {
                                VansVKDevice* texVkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
                                std::string absTexPath = projectRoot + emissiveTexPath;
                                if (emissiveArray->LoadTextureLayer(texVkDevice->GetCommandBuffer(), absTexPath, idx))
                                {
                                    m_LightManager.GetRectLights()[idx].m_TextureSlot = static_cast<float>(idx);
                                    VANS_LOG("[LoadSceneObjects] 面光源 '" << obj->m_ObjectName << "' 降级加载静态发光贴图 slot=" << idx);
                                }
                            }
                        }
                    }
                }
                // ── 静态发光贴图（仅在未指定 video 时生效）──────────────────
                else if (!emissiveTexPath.empty() && idx < 32)
                {
                    VansTexture* emissiveArray = m_MaterialManager.GetRuntimeRenderTexture(
                        VansMaterialManager::RT_RECT_LIGHT_EMISSIVE);
                    if (emissiveArray != nullptr)
                    {
                        VansVKDevice* texVkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
                        std::string absTexPath = projectRoot + emissiveTexPath;
                        bool loaded = emissiveArray->LoadTextureLayer(texVkDevice->GetCommandBuffer(), absTexPath, idx);
                        if (loaded)
                        {
                            m_LightManager.GetRectLights()[idx].m_TextureSlot = static_cast<float>(idx);
                            VANS_LOG("[LoadSceneObjects] 面光源 '" << obj->m_ObjectName << "' 加载发光贴图 slot=" << idx);
                        }
                        else
                        {
                            VANS_LOG_WARN("[LoadSceneObjects] 面光源 '" << obj->m_ObjectName << "' 发光贴图加载失败，回退到单色: " << absTexPath);
                        }
                    }
                    else
                    {
                        VANS_LOG_WARN("[LoadSceneObjects] RT_RECT_LIGHT_EMISSIVE 未就绪，跳过发光贴图加载");
                    }
                }

                obj->AddComponent(rlComp);
                VANS_LOG("[LoadSceneObjects] 创建面光源组件 '" << obj->m_ObjectName << "' idx=" << idx);
            }
        }

        // ── Camera component ──────────────────────────────────────────────
        if (components.contains("camera"))
        {
            ensureObjectTransform();

            if (m_Camera == nullptr)
            {
                VANS_LOG_WARN("[LoadSceneObjects] 找到 camera component 但 m_Camera 为 nullptr，跳过");
            }
            else
            {
                // 应用 fov / nearClip / farClip 等参数
                const auto& camJson = components["camera"];
                if (camJson.contains("fov"))
                    m_Camera->SetFov(camJson["fov"].get<float>());
                if (camJson.contains("nearClip"))
                    m_Camera->SetNearClip(camJson["nearClip"].get<float>());
                if (camJson.contains("farClip"))
                    m_Camera->SetFarClip(camJson["farClip"].get<float>());

                // 绑定 Transform：input 和渲染同步均通过此 ID 驱动
                m_Camera->SetTransformID(obj->m_TransformID);

                // 注册组件
                auto* cameraComp = new VansScriptCameraComponent();
                cameraComp->m_Camera = m_Camera;
                obj->AddComponent(cameraComp);

                VANS_LOG("[LoadSceneObjects] Camera component 已挂载到 object: "
                    << obj->m_ObjectName << "，TransformID=" << obj->m_TransformID);
            }
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

        // ── Audio component ───────────────────────────────────────────────
        if (components.contains("audio"))
        {
            std::string audioName = components["audio"]["source"].get<std::string>();
            VansEngine::VansAudioNode* audioNode = m_AudioManager.Get(audioName);
            if (audioNode)
            {
                ensureObjectTransform();   // 确保 spatial 音频能读到世界坐标
                auto* audioComp = new VansScriptAudioComponent();
                audioComp->m_AudioNode    = audioNode;
                audioComp->m_AudioManager = &m_AudioManager;
                obj->AddComponent(audioComp);
            }
            else
            {
                VANS_LOG_WARN("[LoadSceneObjects] 找不到音频节点 '" << audioName
                    << "'，对象: " << obj->m_ObjectName);
            }
        }

        // ── Video component（显式挂载）────────────────────────────────────
        // JSON 格式：{ "video": { "source": "<resource.json 中注册的名称>" } }
        // VideoComponent 只由此处显式创建；emissive 材质和面光源不再自动创建。
        if (components.contains("video"))
        {
            std::string videoName = components["video"]["source"].get<std::string>();
            if (obj->GetComponent<VansScriptVideoComponent>() == nullptr)
            {
                VansVideoTexture* videoTex = m_VideoManager.Get(videoName);
                if (videoTex)
                {
                    auto* videoComp = new VansScriptVideoComponent();
                    videoComp->m_VideoName    = videoName;
                    videoComp->m_VideoTex     = videoTex;
                    videoComp->m_VideoManager = &m_VideoManager;
                    obj->AddComponent(videoComp);
                    VANS_LOG("[LoadSceneObjects] Video component '" << videoName
                        << "' 已挂载到 object: " << obj->m_ObjectName);
                }
                else
                {
                    VANS_LOG_WARN("[LoadSceneObjects] 找不到视频资源 '" << videoName
                        << "'，对象: " << obj->m_ObjectName);
                }
            }
        }

        // ── Particle component ────────────────────────────────────────────
        // JSON 格式：{ "particle": { "asset": "EngineAssets/Particles/fire.particle",
        //                            "play_on_awake": true } }
        if (components.contains("particle"))
        {
            const auto& particleJson = components["particle"];
            std::string assetPath    = particleJson.value("asset", "");
            bool        playOnAwake  = particleJson.value("play_on_awake", true);

            if (!assetPath.empty())
            {
                // 构造绝对路径
                std::string absPath = projectRoot + "/" + assetPath;

                auto* particleComp              = new VansScriptParticleComponent();
                particleComp->m_ParticleAssetPath = assetPath;
                particleComp->m_PlayOnAwake       = playOnAwake;

                // 加载 .particle 资产
                if (particleComp->LoadAsset(absPath))
                {
                    // 创建渲染节点并初始化 Quad 缓冲
                    auto* renderNode = new VansParticleRenderNode(device);
                    if (renderNode->InitQuadBuffers(device))
                    {
                        renderNode->SetName(obj->m_ObjectName);
                        if (obj->m_TransformID != 0)
                        {
                            // 复合对象：粒子节点共享对象已有 Transform，避免 Inspector 出现两套位置。
                            renderNode->ShareTransform(obj->m_TransformID);
                        }
                        else
                        {
                            // 纯粒子对象：使用粒子渲染节点自带 Transform 作为对象 Transform。
                            obj->m_TransformID = renderNode->m_TransformID;
                            if (hasObjTransform)
                                renderNode->SetTransformData(objPos, objRot, objScl);
                        }

                        // 绑定粒子 Billboard Shader
                        renderNode->m_Shader = static_cast<VansGraphicsShader*>(GetShaderAsset("Particle"));
                        if (!renderNode->m_Shader)
                        {
                            VANS_LOG_WARN("[LoadSceneObjects] 粒子 Shader 'Particle' 未找到，粒子将无法渲染");
                        }

                        if (particleComp->m_ParticleAsset && !particleComp->m_ParticleAsset->m_Emitters.empty())
                        {
                            auto* emitter = particleComp->m_ParticleAsset->m_Emitters.front().get();
                            if (emitter)
                            {
                                const VansParticleRendererConfig& rendererConfig = emitter->m_RendererConfig;
                                renderNode->ApplyRendererConfig(rendererConfig);

                                auto resolveParticleTexturePath = [&](const std::string& texPath) -> std::string
                                {
                                    if (texPath.empty()) return "";
                                    std::filesystem::path path(texPath);
                                    if (path.is_absolute()) return texPath;
                                    return projectRoot + "/" + texPath;
                                };

                                if (rendererConfig.m_LightingMode == VansParticleLightingMode::SixWayLit)
                                {
                                    renderNode->m_SixWayShader = static_cast<VansGraphicsShader*>(GetShaderAsset("ParticleSixWay"));
                                    if (!renderNode->m_SixWayShader)
                                    {
                                        VANS_LOG_WARN("[LoadSceneObjects] 粒子 Shader 'ParticleSixWay' 未找到，将回退普通粒子 Shader");
                                        renderNode->m_LightingMode = VansParticleLightingMode::UnlitFlipbook;
                                    }

                                    const auto& sixWay = rendererConfig.m_SixWayLighting;
                                    renderNode->m_PositiveAxesTexture = LoadOrGetTexture(
                                        resolveParticleTexturePath(sixWay.m_PositiveAxesTexture), false);
                                    renderNode->m_NegativeAxesTexture = LoadOrGetTexture(
                                        resolveParticleTexturePath(sixWay.m_NegativeAxesTexture), false);

                                    if (!renderNode->m_PositiveAxesTexture || !renderNode->m_NegativeAxesTexture)
                                    {
                                        VANS_LOG_WARN("[LoadSceneObjects] Six-Way 粒子贴图缺失，将回退普通粒子 Shader: "
                                            << obj->m_ObjectName);
                                        renderNode->m_LightingMode = VansParticleLightingMode::UnlitFlipbook;
                                    }
                                }
                                else if (!rendererConfig.m_Texture.empty())
                                {
                                    renderNode->m_ParticleTexture = LoadOrGetTexture(
                                        resolveParticleTexturePath(rendererConfig.m_Texture), true);
                                }
                            }
                        }

                        particleComp->m_RenderNode = renderNode;

                        // 注册运行时到后台更新线程
                        VansParticleManager::Instance().Initialize();
                        VansParticleManager::Instance().RegisterRuntime(
                            particleComp->m_Runtime.get());

                        if (particleComp->m_PlayOnAwake)
                            particleComp->Play();

                        // 将渲染节点加入场景
                        RegistRenderNode(renderNode, PARTICLE_NODE);

                        VANS_LOG("[LoadSceneObjects] 粒子组件 '" << assetPath
                            << "' 已挂载到 object: " << obj->m_ObjectName);
                    }
                    else
                    {
                        VANS_LOG_WARN("[LoadSceneObjects] 粒子 Quad 缓冲初始化失败: "
                            << obj->m_ObjectName);
                        delete renderNode;
                    }
                }
                else
                {
                    VANS_LOG_WARN("[LoadSceneObjects] 粒子资产加载失败 '" << absPath
                        << "'，对象: " << obj->m_ObjectName);
                }

                obj->AddComponent(particleComp);
            }
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

        // ── 后处理：自动将 VideoComponent 挂接到同对象的面光源 ──────────────
        // 当面光源 JSON 无 emissive_video 字段（使用显式 "video" 组件时），
        // 此处补充完成 m_VideoComponent 指针和 TextureSlot 的绑定。
        {
            auto* rlComp    = obj->GetComponent<VansScriptRectLightComponent>();
            auto* videoComp = obj->GetComponent<VansScriptVideoComponent>();
            if (rlComp && videoComp &&
                rlComp->m_VideoComponent == nullptr &&
                videoComp->m_VideoTex    != nullptr)
            {
                int idx = rlComp->m_LightIndex;
                if (idx >= 0 && idx < 32)
                {
                    rlComp->m_VideoComponent = videoComp;
                    m_LightManager.GetRectLights()[idx].m_TextureSlot = static_cast<float>(idx);

                    VansTexture* emissiveArray = m_MaterialManager.GetRuntimeRenderTexture(
                        VansMaterialManager::RT_RECT_LIGHT_EMISSIVE);
                    if (emissiveArray != nullptr)
                    {
                        static const uint8_t kWhitePixel[4] = { 255, 255, 255, 255 };
                        VansVKDevice* texVkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
                        emissiveArray->UpdateArrayLayerFromPixels(
                            texVkDevice->GetCommandBuffer(), kWhitePixel, 1, 1, idx);
                    }

                    VANS_LOG("[LoadSceneObjects] 面光源 '" << obj->m_ObjectName
                        << "' 自动绑定 VideoComponent '" << videoComp->m_VideoName
                        << "' slot=" << idx);
                }
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
                    VANS_LOG_WARN("[LoadSceneObjects] Vehicle tire object not found: " << tireObjName);
                }
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
        else if (pending.animJson.contains("ragdoll"))
        {
            LoadSingleRagdollComponent(pending.obj,
                                       animNode,
                                       pending.animJson["ragdoll"],
                                       projectRoot);
        }
    }

    // ── 场景加载完成后，重新触发 auto_play 音频 ──────────────────────────
    // 原因：场景切换时 StopAll() 会停止所有播放；资源级 auto_play 只在
    // LoadFromJson 中触发一次，Runtime 重载后需在此补充调用。
    m_AudioManager.PlayAutoPlay();
}

