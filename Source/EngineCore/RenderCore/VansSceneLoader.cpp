#include "../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansScene.h"
#include "VansSceneLoadPass.h"
#include "VansShaderManager.h"
#include "BRDFData/VansLight.h"
#include "../Configration/VansConfigration.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/VansAssetMeta.h"
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
#include "../AnimationCore/VansBoneAttachmentSystem.h"
#include "../AnimationCore/VansSkinnedMeshLoader.h"
#include "../AnimationCore/FootPlacement/VansFootPlacementTypes.h"

#include "../Interfaces/IShaderHotReloadService.h"
#include "../Util/VansLog.h"
#include "../VansThreadContract.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>
#include <mutex>
#include <cctype>
#include <functional>
#include <glm/gtx/quaternion.hpp>

namespace VansGraphics
{
namespace
{
const json* FindV2Component(const json& entity, const char* type)
{
    if (!entity.contains("components") || !entity["components"].is_array())
        return nullptr;
    for (const json& component : entity["components"])
        if (component.value("type", "") == type)
            return &component;
    return nullptr;
}

std::string ReadStringField(const json& object, const char* key)
{
    if (!object.is_object())
        return {};
    const auto found = object.find(key);
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

bool ReadBoolField(const json& object, const char* key, bool fallback)
{
    if (!object.is_object())
        return fallback;
    const auto found = object.find(key);
    return found != object.end() && found->is_boolean() ? found->get<bool>() : fallback;
}

std::string RuntimeAssetNameFromReference(const json& reference)
{
	if (reference.is_string())
		return reference.get<std::string>();
	if (!reference.is_object())
		return {};
	const std::string guid = ReadStringField(reference, "guid");
	if (guid.empty())
		return {};
	auto* database = Vans::VansProjectManager::Get().GetAssetDatabase();
	if (!database)
		return guid;
	for (const Vans::VansAssetRecord& record : database->All())
	{
		if (record.guid.ToString() != guid)
			continue;
		Vans::VansAssetMeta meta;
		std::string error;
		if (Vans::VansAssetMeta::Load(record.metaPath, meta, error) && meta.settings.is_object())
		{
			const std::string runtimeName = ReadStringField(meta.settings, "runtimeName");
			if (!runtimeName.empty())
				return runtimeName;
		}
		return guid;
	}
	return guid;
}

const json* ReadObjectField(const json& object, const char* key)
{
    if (!object.is_object())
        return nullptr;
    const auto found = object.find(key);
    return found != object.end() && found->is_object() ? &(*found) : nullptr;
}

const json* ReadArrayField(const json& object, const char* key)
{
    if (!object.is_object())
        return nullptr;
    const auto found = object.find(key);
    return found != object.end() && found->is_array() ? &(*found) : nullptr;
}

json RuntimeMaterialFromAsset(const Vans::VansAssetRecord& record)
{
    std::ifstream input(record.sourcePath);
    if (!input)
        return {};
    const json asset = json::parse(input, nullptr, false);
    if (asset.is_discarded() || !asset.is_object())
        return {};

    json material = asset.value("parameters", json::object());
    material["name"] = record.guid.ToString();
    material["type"] = asset.value("materialType", "pbr");
    if (asset.contains("textures") && asset["textures"].is_object())
    {
        for (const auto& [slot, reference] : asset["textures"].items())
        {
            if (reference.is_object() && reference.contains("guid") && reference["guid"].is_string())
                material[slot + "_texture"] = reference["guid"];
        }
    }
    return material;
}

std::string RuntimeComponentKey(const std::string& type)
{
    static const std::unordered_map<std::string, std::string> keys = {
        { "Physics", "physics" }, { "Camera", "camera" }, { "Animation", "animation" },
        { "CharacterController", "charController" }, { "DirectionalLight", "directional_light" },
        { "PointLight", "point_light" }, { "SpotLight", "spot_light" }, { "RectLight", "rect_light" },
        { "Audio", "audio" }, { "Video", "video" }, { "Particle", "particle" },
        { "Cloth", "cloth" }, { "Vehicle", "vehicle" }
    };
    const auto found = keys.find(type);
    if (found != keys.end())
        return found->second;
    if (type.empty())
        return {};
    std::string result = type;
    result.front() = static_cast<char>(std::tolower(static_cast<unsigned char>(result.front())));
    return result;
}

bool BuildRuntimeSceneFromV2(json& sceneData)
{
    if (sceneData.value("schemaVersion", 0u) != 2u)
        return false;
    if (!sceneData.contains("entities") || !sceneData["entities"].is_array())
        return false;

    json projected = sceneData.value("settings", json::object());
    projected["material"] = json::array();
    if (Vans::VansAssetDatabase* database = Vans::VansProjectManager::Get().GetAssetDatabase())
    {
        for (const Vans::VansAssetRecord& record : database->All())
        {
            if (record.type != Vans::VansAssetType::Material || record.state == Vans::VansAssetState::Missing)
                continue;
            json material = RuntimeMaterialFromAsset(record);
            if (!material.empty())
                projected["material"].push_back(std::move(material));
        }
    }

    json objects = json::array();
    json renderNodes = json::array();
    for (const json& entity : sceneData["entities"])
    {
        const json* transformComponent = FindV2Component(entity, "Transform");
        const json* rendererComponent = FindV2Component(entity, "ModelRenderer");
        const json* animationComponent = FindV2Component(entity, "Animation");

        json transform = {
            { "position", { 0.0f, 0.0f, 0.0f } },
            { "rotation", { 0.0f, 0.0f, 0.0f } },
            { "scale", { 1.0f, 1.0f, 1.0f } }
        };
        if (transformComponent && transformComponent->value("enabled", true) && transformComponent->contains("data"))
        {
            const json& data = (*transformComponent)["data"];
            transform["position"] = data.value("position", transform["position"]);
            transform["scale"] = data.value("scale", transform["scale"]);
            if (data.contains("rotation") && data["rotation"].is_array() && data["rotation"].size() == 4)
            {
                const glm::quat rotation(
                    data["rotation"][3].get<float>(), data["rotation"][0].get<float>(),
                    data["rotation"][1].get<float>(), data["rotation"][2].get<float>());
                const glm::vec3 euler = glm::degrees(glm::eulerAngles(rotation));
                transform["rotation"] = { euler.x, euler.y, euler.z };
            }
        }

        json runtimeRender;
        bool specialRenderNode = false;
        if (rendererComponent && rendererComponent->value("enabled", true) && rendererComponent->contains("data"))
        {
            const json& data = (*rendererComponent)["data"];
            const std::string modelGuid = data.value("model", json::object()).value("guid", "");
            std::string materialGuid;
            if (data.contains("materialOverrides") && data["materialOverrides"].is_object() && !data["materialOverrides"].empty())
            {
                const json& overrideValue = data["materialOverrides"].begin().value();
                materialGuid = overrideValue.value("guid", "");
            }
            runtimeRender = {
                { "name", animationComponent && animationComponent->value("enabled", true)
					? (*animationComponent)["data"].value("name", entity.value("name", ""))
					: entity.value("name", "") },
                { "mesh", modelGuid },
                { "material", materialGuid },
                { "type", data.value("renderType", "opaque") },
                { "support_shadow", data.value("castShadows", true) }
            };
            if (data.contains("sourceNode") && data["sourceNode"].is_string())
                runtimeRender["parent"] = data["sourceNode"];
            specialRenderNode = data.contains("renderRole") && data["renderRole"].is_string();
        }

        if (specialRenderNode)
        {
            renderNodes.push_back(std::move(runtimeRender));
            continue;
        }

        json object = {
			{ "entityGuid", entity.value("id", "") },
            { "name", entity.value("name", "") },
            { "transform", std::move(transform) },
            { "components", json::object() }
        };
        if (!runtimeRender.empty())
            object["components"]["render"] = std::move(runtimeRender);
        object["pyScripts"] = json::array();

        for (const json& component : entity["components"])
        {
            const std::string type = component.value("type", "");
            if (type == "Transform" || type == "ModelRenderer")
                continue;
            if (type == "Script")
            {
				if (component.value("enabled", true))
					object["pyScripts"].push_back(component.value("data", json::object()));
                continue;
            }
            json data = component.value("data", json::object());
			if ((type == "Audio" || type == "Video") && data.contains("source"))
				data["source"] = RuntimeAssetNameFromReference(data["source"]);
            // Runtime component loaders consume enabled from their data object,
            // while scene v2 stores it on the component envelope.
            data["enabled"] = component.value("enabled", true);
            object["components"][RuntimeComponentKey(type)] = std::move(data);
        }
        if (object["pyScripts"].empty())
            object.erase("pyScripts");
        objects.push_back(std::move(object));
    }

    projected["scene"] = json::array({ {
        { "objects", std::move(objects) },
        { "rendernode", std::move(renderNodes) }
    } });
    sceneData = std::move(projected);
    return true;
}
}

bool VansScene::LoadProjectAssets(Vans::VansAssetDatabase& database,
    const std::filesystem::path& scenePath, VansVKDevice* device)
{
    VANS_ASSERT_MAIN_THREAD();
    if (device == nullptr)
    {
        VANS_LOG_ERROR("[VansScene] Cannot load project assets without a Vulkan device");
        return false;
    }
    VANS_LOG("[VansScene] Loading project assets from AssetDatabase: " << database.AssetsRoot().string());
	m_ProjectMeshAliases.clear();
	try
	{

    json resourceData;
    resourceData["mesh"] = json::array();
    resourceData["texture"] = json::array();
    const std::filesystem::path projectRoot = database.AssetsRoot().parent_path();
	std::unordered_set<std::string> requiredModels;
	std::unordered_set<std::string> requiredMaterials;
	std::unordered_set<std::string> requiredTextures;
	for (const auto& [alias, guid] : Vans::VansProjectManager::Get().GetConfig().runtimeAssetBindings)
		if (alias != "fullScreenQuad" && alias != "plane") requiredModels.insert(guid);

	std::ifstream sceneInput(scenePath);
	json sceneDocument = sceneInput ? json::parse(sceneInput, nullptr, false) : json();
	if (!sceneDocument.is_object() || sceneDocument.value("schemaVersion", 0u) != 2u)
	{
		VANS_LOG_ERROR("[AssetDatabase] Cannot collect Scene v2 dependencies from " << scenePath.string());
		return false;
	}
	VANS_LOG("[AssetDatabase] Collecting dependencies from " << scenePath.string());
	const json* entities = ReadArrayField(sceneDocument, "entities");
	if (entities == nullptr)
	{
		VANS_LOG_ERROR("[AssetDatabase] Scene v2 entities must be an array");
		return false;
	}
	for (const json& entity : *entities)
	{
		const json* components = ReadArrayField(entity, "components");
		if (components == nullptr)
			continue;
		for (const json& component : *components)
		{
			if (ReadStringField(component, "type") != "ModelRenderer")
				continue;
			const json* data = ReadObjectField(component, "data");
			if (data == nullptr)
				continue;
			const json* modelReference = ReadObjectField(*data, "model");
			const std::string model = modelReference ? ReadStringField(*modelReference, "guid") : std::string{};
			if (!model.empty()) requiredModels.insert(model);
			const json* overrides = ReadObjectField(*data, "materialOverrides");
			if (overrides == nullptr)
				continue;
			for (const auto& [slot, material] : overrides->items())
			{
				const std::string materialGuid = ReadStringField(material, "guid");
				if (!materialGuid.empty()) requiredMaterials.insert(materialGuid);
			}
		}
	}

	// Settings and specialized components (terrain, sky, audio/video, particles)
	// may reference assets outside ModelRenderer. Collect every GUID-shaped value
	// against the database so the dependency closure remains schema-extensible.
	const std::vector<Vans::VansAssetRecord> allRecords = database.All();
	std::unordered_map<std::string, Vans::VansAssetType> assetTypesByGuid;
	for (const Vans::VansAssetRecord& record : allRecords)
		assetTypesByGuid.emplace(record.guid.ToString(), record.type);
	std::function<void(const json&)> collectAssetReferences = [&](const json& value)
	{
		if (value.is_string())
		{
			const auto found = assetTypesByGuid.find(value.get<std::string>());
			if (found == assetTypesByGuid.end()) return;
			switch (found->second)
			{
			case Vans::VansAssetType::Model: requiredModels.insert(found->first); break;
			case Vans::VansAssetType::Texture: requiredTextures.insert(found->first); break;
			case Vans::VansAssetType::Material: requiredMaterials.insert(found->first); break;
			default: break;
			}
			return;
		}
		if (value.is_array())
			for (const json& item : value) collectAssetReferences(item);
		else if (value.is_object())
			for (const auto& [key, item] : value.items()) collectAssetReferences(item);
	};
	collectAssetReferences(sceneDocument);

	for (const Vans::VansAssetRecord& record : database.All())
	{
		if (record.type != Vans::VansAssetType::Material ||
			requiredMaterials.find(record.guid.ToString()) == requiredMaterials.end())
			continue;
		std::ifstream materialInput(record.sourcePath);
		const json material = materialInput ? json::parse(materialInput, nullptr, false) : json();
		if (!material.is_object())
		{
			VANS_LOG_ERROR("[AssetDatabase] Cannot read material dependency data: " << record.sourcePath.string());
			continue;
		}
		collectAssetReferences(material);
		const json* textures = ReadObjectField(material, "textures");
		if (textures == nullptr)
		{
			VANS_LOG_ERROR("[AssetDatabase] Material textures must be an object: " << record.sourcePath.string());
			continue;
		}
		for (const auto& [slot, texture] : textures->items())
		{
			if (!texture.is_object())
				continue;
			const std::string textureGuid = ReadStringField(texture, "guid");
			if (!textureGuid.empty()) requiredTextures.insert(textureGuid);
		}
	}

    for (const Vans::VansAssetRecord& record : database.All())
    {
        if (record.state == Vans::VansAssetState::Missing)
            continue;

        Vans::VansAssetMeta meta;
        std::string metaError;
        if (!Vans::VansAssetMeta::Load(record.metaPath, meta, metaError))
        {
            VANS_LOG_ERROR("[AssetDatabase] " << metaError);
            continue;
        }
		if (!meta.settings.is_object())
		{
			VANS_LOG_ERROR("[AssetDatabase] Asset settings must be an object: " << record.metaPath.string());
			continue;
		}

        std::error_code relativeError;
        const std::string relativePath = std::filesystem::relative(
            record.sourcePath, projectRoot, relativeError).generic_string();
        if (relativeError)
        {
            VANS_LOG_ERROR("[AssetDatabase] Cannot make project-relative path: " << record.sourcePath.string());
            continue;
        }

        if (record.type == Vans::VansAssetType::Model)
        {
			if (requiredModels.find(record.guid.ToString()) == requiredModels.end())
				continue;
            const bool isFbx = record.sourcePath.extension() == ".fbx" || record.sourcePath.extension() == ".FBX";
            resourceData["mesh"].push_back({
                { "name", record.guid.ToString() },
                { "path", relativePath },
                { "need_tangent", ReadBoolField(meta.settings, "generateTangents", true) },
                { "support_raytracing", ReadBoolField(meta.settings, "buildRayTracingData", true) },
                { "need_cpu_data", ReadBoolField(meta.settings, "keepCpuMeshData", false) },
				{ "load_multi_mesh", ReadBoolField(meta.settings, "loadMultiMesh", isFbx) }
            });
        }
        else if (record.type == Vans::VansAssetType::Texture)
        {
			if (requiredTextures.find(record.guid.ToString()) == requiredTextures.end())
				continue;
			const bool isCubemap = record.sourcePath.extension() == ".cubemap";
			const std::string texturePath = isCubemap
				? ReadStringField(meta.settings, "sourcePath") : relativePath;
            resourceData["texture"].push_back({
                { "name", record.guid.ToString() },
				{ "path", texturePath },
				{ "type", isCubemap ? TEXTURE_CUBE : TEXTURE_2D },
                { "sRGB", ReadStringField(meta.settings, "colorSpace") != "linear" }
            });
        }
		else if (record.type == Vans::VansAssetType::Audio)
		{
			const std::string runtimeName = ReadStringField(meta.settings, "runtimeName");
			resourceData["audio"].push_back({
				{ "name", runtimeName.empty() ? record.guid.ToString() : runtimeName },
				{ "path", relativePath },
				{ "play_mode", ReadStringField(meta.settings, "playMode").empty() ? "static" : ReadStringField(meta.settings, "playMode") },
				{ "loop", ReadBoolField(meta.settings, "loop", false) },
				{ "auto_play", ReadBoolField(meta.settings, "autoPlay", false) },
				{ "volume", meta.settings.value("volume", 1.0f) },
				{ "pitch", meta.settings.value("pitch", 1.0f) },
				{ "spatial", ReadBoolField(meta.settings, "spatial", false) },
				{ "ref_dist", meta.settings.value("referenceDistance", 1.0f) },
				{ "max_dist", meta.settings.value("maxDistance", 100.0f) },
				{ "roll_off", meta.settings.value("rolloff", 1.0f) }
			});
		}
		else if (record.type == Vans::VansAssetType::Video)
		{
			const std::string runtimeName = ReadStringField(meta.settings, "runtimeName");
			resourceData["video"].push_back({
				{ "name", runtimeName.empty() ? record.guid.ToString() : runtimeName },
				{ "path", relativePath },
				{ "loop", ReadBoolField(meta.settings, "loop", true) },
				{ "autoplay", ReadBoolField(meta.settings, "autoPlay", false) },
				{ "srgb", ReadBoolField(meta.settings, "sRGB", true) }
			});
		}
    }

    m_VideoManager.Clear();
    m_AudioManager.Clear();
	VANS_LOG("[AssetDatabase] Uploading dependency closure: " << resourceData["mesh"].size()
		<< " models, " << resourceData["texture"].size() << " textures");

    // Engine shaders are registered during InitializeGraphicsSystem().
    LoadResources(resourceData);
	for (const auto& [alias, guid] : Vans::VansProjectManager::Get().GetConfig().runtimeAssetBindings)
	{
		if (alias == "fullScreenQuad" || alias == "plane")
			continue;
		if (VansAsset* asset = GetMeshAsset(guid))
			m_ProjectMeshAliases[alias] = asset;
		else
			VANS_LOG_ERROR("[AssetDatabase] Runtime mesh binding '" << alias << "' references missing guid " << guid);
	}
    m_ResourcesLoaded = true;
    VANS_LOG("[VansScene] AssetDatabase project resources loaded");
	VANS_LOG("[AssetDatabase] Dependency closure: " << requiredModels.size() << " models, "
		<< requiredMaterials.size() << " materials, " << requiredTextures.size() << " textures");
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

// ===========================================================================
// LoadSceneForRendering — 加载场景并准备 GPU 资源
// ===========================================================================
void VansScene::LoadSceneForRendering(const char* scenePath, VansVKDevice* device, VansSceneLoadMode mode)
{
    VANS_ASSERT_MAIN_THREAD();

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
		VANS_LOG_ERROR("[VansScene] LoadSceneForRendering called before project assets were loaded");
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
	// Probe resources depend on both scene JSON and the global descriptor set.
	// Build them after Set 0 exists and before render-node pipelines are created.
	if (m_ReflectionProbeSystem.GetPlacementSettings().enabled &&
		m_ReflectionProbeSystem.GetProbes().size() <= 1)
	{
		m_ReflectionProbeSystem.GenerateAutoProbes(*this, true);
	}
	m_ReflectionProbeSystem.CreateGPUResources(*device, device->GetImmediateGraphicsCommandBuffer());
	m_ReflectionProbeSystem.UpdateGlobalDescriptors(m_GlobalDescriptorSet);
	// ── 修复：WaterSystem 在 AddWaterNode 中已创建，但 CreateGlobalDescriptorSet 后才
	//    有有效的 m_GlobalDescriptorSet。此处同步正确的全局 descriptor set 到 WaterSystem。
	if (m_WaterSystem)
	{
		m_WaterSystem->SetGlobalDescriptorSet(
			m_GlobalDescriptorSetLayout,
			m_GlobalDescriptorSet);
	}
	// 将 Global Descriptor Set（Set 0）同步到 MaterialManager，供视频切换时直接更新 Bindless 槽
	m_MaterialManager.m_VideoBindlessDescriptorSet = m_GlobalDescriptorSet;
	// Write TileLight SSBOs (created during BeforeRendering) into global Set 0 bindings 9/10.
	UpdateGlobalTileLightDescriptors();

	// IES profile GPU 资源：在所有场景内容（含光源 JSON）加载完毕后，一次性创建纹理数组并上传
	device->PrepareIESProfileData();

	CreateNodeDescriptorSets();
	device->PrepareRayTracingData();
	// The local SH volume is updated in spatial phases during rendering. Keep
	// initial probe requests queued until every GI cell has received one update.
	m_ReflectionProbeSystem.DeferInitialBakeForGI(
		m_GISettings.spatialUpdateDivisor, m_GISettings.directionUpdateSlices);

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
    for (auto* mesh : scene.GetMeshAssets())
    {
        if (mesh && mesh->m_AssetName == name)
            return true;
    }
    return false;
}

static bool HasMaterialAssetName(const VansGraphics::VansScene& scene, const std::string& name)
{
    for (auto* material : scene.GetMaterialAssets())
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
	std::string materialName = sceneRenderNode.value("material", "");
	VansMaterial* material = static_cast<VansMaterial*>(GetMaterialAsset(materialName));

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

		ExpandMultiMeshToRenderNodes(
			device, mesh, parentName, position, rotation, scale, supportShadow, material);

        // 不从 m_Meshes 中移除父级 multi-mesh，场景切换时仍需通过名称找到它。
        // 子网格会在 ExpandMultiMeshToRenderNodes 内部被添加到 m_Meshes，
        // 并在 UnLoadScene Step 10 中清理。

        // Multi-mesh expansion creates its own render nodes — return nullptr to indicate
        // that no single render node was created.
        return nullptr;
    }

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

    // Tessellation (optional, defaults from TerrainConfig)
    if (terrainData.contains("tessellation") && terrainData["tessellation"].is_object())
    {
        auto& tessJson = terrainData["tessellation"];
        config.enableTessellation   = tessJson.value("enabled",  config.enableTessellation);
        config.tessellationDistance = tessJson.value("distance", config.tessellationDistance);
        config.maxTessellationLevel = tessJson.value("maxLevel", config.maxTessellationLevel);
        config.tessellationPower        = tessJson.value("power",               config.tessellationPower);
        config.tessLodBias              = tessJson.value("lodBias",             config.tessLodBias);
        config.tessDisplacementStrength = tessJson.value("displacementStrength", config.tessDisplacementStrength);

        // 程序化噪声细节（替代 displacementStrength）
        if (tessJson.contains("noiseDetail") && tessJson["noiseDetail"].is_object())
        {
            auto& noiseJson = tessJson["noiseDetail"];
            config.enableNoiseDetail = noiseJson.value("enabled",       config.enableNoiseDetail);
            config.noiseStrength     = noiseJson.value("strength",      config.noiseStrength);
            config.noiseFrequency    = noiseJson.value("frequency",     config.noiseFrequency);
            config.noiseLacunarity   = noiseJson.value("lacunarity",    config.noiseLacunarity);
            config.noiseGain         = noiseJson.value("gain",          config.noiseGain);
            config.noiseOctaves      = noiseJson.value("octaves",       config.noiseOctaves);
            config.noiseWarpStrength = noiseJson.value("warpStrength",  config.noiseWarpStrength);
            config.noiseFadeStart    = noiseJson.value("fadeStart",     config.noiseFadeStart);
        }
    }

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
// Water node
// ===========================================================================

void VansGraphics::VansScene::AddWaterNode(VkDevice& device, json& waterData)
{
    // ── 类型解析 ───────────────────────────────────────────────────────────
    VansWaterConfig config;

    const std::string typeStr = waterData.value("type", "ocean");
    if      (typeStr == "ocean")  config.m_Type = VansWaterType::Ocean;
    else if (typeStr == "lake")   config.m_Type = VansWaterType::Lake;
    else if (typeStr == "river")  config.m_Type = VansWaterType::River;
    else if (typeStr == "pool")   config.m_Type = VansWaterType::Pool;
    else
        VANS_LOG_WARN("[AddWaterNode] Unknown water type '" << typeStr << "', defaulting to ocean.");

    config.m_WaterLevel        = waterData.value("level", 3.4f);
    config.m_SpecularIntensity = waterData.value("specularIntensity", 1.0f);

    // ── medium 块 ──────────────────────────────────────────────────────────
    if (waterData.contains("medium") && waterData["medium"].is_object())
    {
        auto& m = waterData["medium"];

        // absorption — 支持数组 [r,g,b] 或对象 {r,g,b}
        if (m.contains("absorption"))
        {
            auto& a = m["absorption"];
            if (a.is_array() && a.size() >= 3)
                config.m_Medium.m_AbsorptionCoeff = {a[0], a[1], a[2]};
            else if (a.is_object())
                config.m_Medium.m_AbsorptionCoeff = {
                    a.value("r", 0.05f), a.value("g", 0.08f), a.value("b", 0.20f)};
        }

        if (m.contains("scattering"))
        {
            auto& s = m["scattering"];
            if (s.is_array() && s.size() >= 3)
                config.m_Medium.m_ScatteringCoeff = {s[0], s[1], s[2]};
            else if (s.is_object())
                config.m_Medium.m_ScatteringCoeff = {
                    s.value("r", 0.03f), s.value("g", 0.05f), s.value("b", 0.08f)};
        }

        config.m_Medium.m_IOR          = m.value("ior",          config.m_Medium.m_IOR);
        config.m_Medium.m_FresnelPower = m.value("fresnelPower",  config.m_Medium.m_FresnelPower);
        config.m_Medium.m_Anisotropy   = m.value("anisotropy",   config.m_Medium.m_Anisotropy);
        config.m_Medium.m_WaterRoughness = m.value("roughness",  config.m_Medium.m_WaterRoughness);

        if (m.contains("deepColor"))
        {
            auto& d = m["deepColor"];
            if (d.is_array() && d.size() >= 3)
                config.m_Medium.m_DeepColor = {d[0], d[1], d[2], 1.0f};
            else if (d.is_object())
                config.m_Medium.m_DeepColor = {
                    d.value("r", 0.0f), d.value("g", 0.05f), d.value("b", 0.2f), 1.0f};
        }

        if (m.contains("shallowColor"))
        {
            auto& s = m["shallowColor"];
            if (s.is_array() && s.size() >= 3)
                config.m_Medium.m_ShallowColor = {s[0], s[1], s[2], 1.0f};
            else if (s.is_object())
                config.m_Medium.m_ShallowColor = {
                    s.value("r", 0.1f), s.value("g", 0.3f), s.value("b", 0.4f), 1.0f};
        }
    }

    // ── waves 块 ───────────────────────────────────────────────────────────
    if (waterData.contains("waves") && waterData["waves"].is_object())
    {
        auto& w = waterData["waves"];

        const std::string modeStr = w.value("mode", "gerstner");
        if      (modeStr == "fft")     config.m_Waves.m_Mode = VansWaveMode::FFT;
        else if (modeStr == "hybrid")  config.m_Waves.m_Mode = VansWaveMode::Hybrid;
        else                           config.m_Waves.m_Mode = VansWaveMode::Gerstner;

        config.m_Waves.m_BaseScale        = w.value("baseScale",         config.m_Waves.m_BaseScale);
        config.m_Waves.m_MaxLOD           = w.value("maxLOD",            config.m_Waves.m_MaxLOD);
        config.m_Waves.m_WindSpeed        = w.value("windSpeed",         config.m_Waves.m_WindSpeed);
        config.m_Waves.m_SwellAmplitude   = w.value("swellAmplitude",    config.m_Waves.m_SwellAmplitude);
        config.m_Waves.m_ChopScale        = w.value("chopScale",         config.m_Waves.m_ChopScale);
        config.m_Waves.m_GerstnerWaveCount = w.value("gerstnerWaveCount", config.m_Waves.m_GerstnerWaveCount);
        config.m_Waves.m_FftLODCount      = w.value("fftLODCount",       config.m_Waves.m_FftLODCount);
        config.m_Waves.m_FftResolution    = w.value("fftResolution",     config.m_Waves.m_FftResolution);
        config.m_Waves.m_FFT.m_LODCount   = config.m_Waves.m_FftLODCount;
        config.m_Waves.m_FFT.m_Resolution = config.m_Waves.m_FftResolution;

        if (w.contains("windDirection"))
        {
            auto& d = w["windDirection"];
            if (d.is_array() && d.size() >= 2)
                config.m_Waves.m_WindDirection = {d[0], d[1]};
            else if (d.is_object())
                config.m_Waves.m_WindDirection = {
                    d.value("x", 0.7071f), d.value("y", 0.7071f)};
        }

        if (w.contains("fft") && w["fft"].is_object())
        {
            auto& fft = w["fft"];
            config.m_Waves.m_FFT.m_UseDerivativeNormal = fft.value("useDerivativeNormal", config.m_Waves.m_FFT.m_UseDerivativeNormal);
            config.m_Waves.m_FFT.m_Resolution          = fft.value("resolution",          config.m_Waves.m_FFT.m_Resolution);
            config.m_Waves.m_FFT.m_LODCount            = fft.value("lodCount",            config.m_Waves.m_FFT.m_LODCount);
            config.m_Waves.m_FFT.m_SpectrumAmplitude   = fft.value("spectrumAmplitude",   config.m_Waves.m_FFT.m_SpectrumAmplitude);
            config.m_Waves.m_FFT.m_Choppiness          = fft.value("choppiness",          config.m_Waves.m_FFT.m_Choppiness);
            config.m_Waves.m_FFT.m_SmallWaveDamping    = fft.value("smallWaveDamping",    config.m_Waves.m_FFT.m_SmallWaveDamping);
            config.m_Waves.m_FFT.m_WindDependency      = fft.value("windDependency",      config.m_Waves.m_FFT.m_WindDependency);
            config.m_Waves.m_FFT.m_Depth               = fft.value("depth",               config.m_Waves.m_FFT.m_Depth);
            config.m_Waves.m_FFT.m_RepeatPeriod        = fft.value("repeatPeriod",        config.m_Waves.m_FFT.m_RepeatPeriod);
            config.m_Waves.m_FFT.m_FoamSlopeScale      = fft.value("foamSlopeScale",      config.m_Waves.m_FFT.m_FoamSlopeScale);
            config.m_Waves.m_FFT.m_FoamFoldScale       = fft.value("foamFoldScale",       config.m_Waves.m_FFT.m_FoamFoldScale);
            config.m_Waves.m_FFT.m_FoamFoldThreshold   = fft.value("foamFoldThreshold",   config.m_Waves.m_FFT.m_FoamFoldThreshold);
            config.m_Waves.m_FFT.m_RandomSeed          = fft.value("randomSeed",          config.m_Waves.m_FFT.m_RandomSeed);
            config.m_Waves.m_FftLODCount               = config.m_Waves.m_FFT.m_LODCount;
            config.m_Waves.m_FftResolution             = config.m_Waves.m_FFT.m_Resolution;
        }

        // N-01: detailNormal 子块
        if (w.contains("detailNormal") && w["detailNormal"].is_object())
        {
            auto& dn = w["detailNormal"];
            config.m_Waves.m_DetailNormal.m_Enabled         = dn.value("enabled",    true);
            config.m_Waves.m_DetailNormal.m_Intensity       = dn.value("intensity",  1.0f);
            config.m_Waves.m_DetailNormal.m_Scale           = dn.value("scale",      1.0f);
            config.m_Waves.m_DetailNormal.m_OctaveCount     = dn.value("octaves",    4);
            config.m_Waves.m_DetailNormal.m_TimeOffset      = dn.value("timeOffset", 0.0f);
            config.m_Waves.m_DetailNormal.m_DetailBaseScale = dn.value("baseScale",  32.0f);
        }

        // maxLOD 范围校验
        if (config.m_Waves.m_MaxLOD < 1 || config.m_Waves.m_MaxLOD > 10)
        {
            VANS_LOG_WARN("[AddWaterNode] waves.maxLOD = " << config.m_Waves.m_MaxLOD
                << " 越界（合法范围 1-10），截断至合法值。");
            config.m_Waves.m_MaxLOD = glm::clamp(config.m_Waves.m_MaxLOD, 1, 10);
        }
    }

    // ── foam 块 ────────────────────────────────────────────────────────────
    if (waterData.contains("foam") && waterData["foam"].is_object())
    {
        auto& f = waterData["foam"];
        config.m_Foam.m_Enabled     = f.value("enabled",   true);
        config.m_Foam.m_TextureName = f.value("texture",   std::string{});
        config.m_Foam.m_Intensity   = f.value("intensity", 1.0f);
    }

    // ── normalMap 块 ───────────────────────────────────────────────────────
    if (waterData.contains("normalMap") && waterData["normalMap"].is_object())
    {
        auto& n = waterData["normalMap"];
        config.m_NormalMap.m_TextureName = n.value("texture", std::string{});
        if (n.contains("tiling"))
        {
            auto& t = n["tiling"];
            if (t.is_array() && t.size() >= 2)
                config.m_NormalMap.m_Tiling = {t[0], t[1]};
        }
    }

    // ── caustics 块 ────────────────────────────────────────────────────────
    if (waterData.contains("caustics") && waterData["caustics"].is_object())
    {
        auto& c = waterData["caustics"];
        config.m_Caustics.m_Enabled   = c.value("enabled",   true);
        config.m_Caustics.m_Intensity = c.value("intensity", 1.0f);
        config.m_Caustics.m_Scale     = c.value("scale",     0.5f);
    }

    // ── refraction 块 ─────────────────────────────────────────────────────
    if (waterData.contains("refraction") && waterData["refraction"].is_object())
    {
        auto& r = waterData["refraction"];
        config.m_Refraction.m_Enabled     = r.value("enabled",     true);
        config.m_Refraction.m_MaxDistance = r.value("maxDistance", 50.0f);
        config.m_Refraction.m_Scale       = r.value("scale",       0.5f);
    }

    // ── ssr 块 ────────────────────────────────────────────────────────────
    if (waterData.contains("ssr") && waterData["ssr"].is_object())
    {
        auto& s = waterData["ssr"];
        config.m_SSR.m_Enabled      = s.value("enabled",      true);
        config.m_SSR.m_MaxDistance   = s.value("maxDistance",  500.0f);
        config.m_SSR.m_MaxRoughness  = s.value("maxRoughness", 0.3f);
    }

    // ── sss 块（W-16: 次表面散射）─────────────────────────────────────────
    if (waterData.contains("sss") && waterData["sss"].is_object())
    {
        auto& s = waterData["sss"];
        config.m_SSS.m_Enabled                   = s.value("enabled",        true);
        config.m_SSS.m_MaxThicknessDistance       = s.value("maxThickness",  15.0f);
        config.m_SSS.m_DeepWaterThicknessFallback = s.value("deepFallback",  0.8f);
    }

    // ── lod 块（W-07）─────────────────────────────────────────────────────
    if (waterData.contains("lod") && waterData["lod"].is_object())
    {
        auto& l = waterData["lod"];
        config.m_LOD.m_MaxLOD          = l.value("levels",          config.m_LOD.m_MaxLOD);
        config.m_LOD.m_BasePatchSize   = l.value("basePatchSize",   config.m_LOD.m_BasePatchSize);
        config.m_LOD.m_MeshDim         = l.value("meshDim",         config.m_LOD.m_MeshDim);
        config.m_LOD.m_DetailBalance   = l.value("detailBalance",   config.m_LOD.m_DetailBalance);
        config.m_LOD.m_MorphWidthRatio = l.value("morphWidthRatio", config.m_LOD.m_MorphWidthRatio);
        if (!waterData.contains("waves") || !waterData["waves"].contains("baseScale"))
            config.m_Waves.m_BaseScale = l.value("minDistance", config.m_Waves.m_BaseScale);

        config.m_LOD.m_MaxLOD = glm::clamp(config.m_LOD.m_MaxLOD, 1, 10);
        if (config.m_LOD.m_MeshDim < 3)
            config.m_LOD.m_MeshDim = 65;
        if (((config.m_LOD.m_MeshDim - 1) % 2) != 0)
            ++config.m_LOD.m_MeshDim;

        VANS_LOG("[AddWaterNode] lod block: levels=" << config.m_LOD.m_MaxLOD
            << " basePatchSize=" << config.m_LOD.m_BasePatchSize
            << " meshDim=" << config.m_LOD.m_MeshDim);
    }

    // ── debug 块（W-07）───────────────────────────────────────────────────
    if (waterData.contains("debug") && waterData["debug"].is_object())
    {
        auto& d = waterData["debug"];
        bool showWire   = d.value("showLODWireframe", false);
        bool freezeLOD  = d.value("freezeLOD",        false);
        bool visMorph   = d.value("visualizeMorph",   false);
        VANS_LOG("[AddWaterNode] debug: wireframe=" << showWire
            << " freezeLOD=" << freezeLOD << " visualizeMorph=" << visMorph);
        // Debug 标志存储在 VansWaterConfig 中（可后续添加 m_Debug 子结构）
    }

    // ── 创建 VansWaterMaterial 并展开所有字段 ──────────────────────────────
    VansWaterMaterial* mat = new VansWaterMaterial();
    mat->m_MaterialType = VansMaterialType::VAN_WATER;
    mat->m_Config       = config;

    // 参与介质参数
    mat->m_AbsorptionCoeffs  = config.m_Medium.m_AbsorptionCoeff;
    mat->m_ScatteringCoeffs  = config.m_Medium.m_ScatteringCoeff;
    mat->m_WaterIOR          = config.m_Medium.m_IOR;
    mat->m_FresnelPower      = config.m_Medium.m_FresnelPower;
    mat->m_Anisotropy        = config.m_Medium.m_Anisotropy;
    mat->m_WaterRoughness    = config.m_Medium.m_WaterRoughness;
    mat->m_SpecularIntensity = config.m_SpecularIntensity;
    mat->m_DeepWaterColor    = config.m_Medium.m_DeepColor;
    mat->m_ShallowWaterColor = config.m_Medium.m_ShallowColor;

    // 波形参数
    mat->m_OceanBaseScale      = config.m_Waves.m_BaseScale;
    mat->m_MaxLODCount         = config.m_LOD.m_MaxLOD;
    mat->m_LODBasePatchSize    = config.m_LOD.m_BasePatchSize;
    mat->m_LODMeshDim          = config.m_LOD.m_MeshDim;
    mat->m_LODDetailBalance    = config.m_LOD.m_DetailBalance;
    mat->m_LODMorphWidthRatio  = config.m_LOD.m_MorphWidthRatio;
    mat->m_GerstnerWaveCount   = config.m_Waves.m_GerstnerWaveCount;
    mat->m_FftLODCount         = config.m_Waves.m_FftLODCount;
    mat->m_FftResolution       = config.m_Waves.m_FftResolution;
    mat->m_FFTUseDerivativeNormal = config.m_Waves.m_FFT.m_UseDerivativeNormal;
    mat->m_FFTSpectrumAmplitude = config.m_Waves.m_FFT.m_SpectrumAmplitude;
    mat->m_FFTChoppiness       = config.m_Waves.m_FFT.m_Choppiness;
    mat->m_FFTSmallWaveDamping = config.m_Waves.m_FFT.m_SmallWaveDamping;
    mat->m_FFTWindDependency   = config.m_Waves.m_FFT.m_WindDependency;
    mat->m_FFTDepth            = config.m_Waves.m_FFT.m_Depth;
    mat->m_FFTRepeatPeriod     = config.m_Waves.m_FFT.m_RepeatPeriod;
    mat->m_FFTFoamSlopeScale   = config.m_Waves.m_FFT.m_FoamSlopeScale;
    mat->m_FFTFoamFoldScale    = config.m_Waves.m_FFT.m_FoamFoldScale;
    mat->m_FFTFoamFoldThreshold = config.m_Waves.m_FFT.m_FoamFoldThreshold;
    mat->m_FFTRandomSeed       = config.m_Waves.m_FFT.m_RandomSeed;
    mat->m_WindSpeed           = config.m_Waves.m_WindSpeed;
    mat->m_SwellAmplitude      = config.m_Waves.m_SwellAmplitude;
    mat->m_ChopScale           = config.m_Waves.m_ChopScale;
    mat->m_WindDirection       = config.m_Waves.m_WindDirection;

    // 泡沫
    mat->m_EnableFoam    = config.m_Foam.m_Enabled;
    mat->m_FoamIntensity = config.m_Foam.m_Intensity;

    // 法线贴图平铺
    mat->m_NormalMapTiling = config.m_NormalMap.m_Tiling;

    // 焦散
    mat->m_EnableCaustics    = config.m_Caustics.m_Enabled;
    mat->m_CausticsIntensity = config.m_Caustics.m_Intensity;
    mat->m_CausticsScale     = config.m_Caustics.m_Scale;

    // 折射
    mat->m_EnableRefraction  = config.m_Refraction.m_Enabled;
    mat->m_RefractionMaxDist = config.m_Refraction.m_MaxDistance;
    mat->m_RefractionScale   = config.m_Refraction.m_Scale;

    // SSR
    mat->m_EnableSSR       = config.m_SSR.m_Enabled;
    mat->m_SSRMaxDistance   = config.m_SSR.m_MaxDistance;
    mat->m_SSRMaxRoughness  = config.m_SSR.m_MaxRoughness;

    // SSS（W-16: 次表面散射）
    mat->m_SSSEnabled                = config.m_SSS.m_Enabled;
    mat->m_MaxThicknessDistance      = config.m_SSS.m_MaxThicknessDistance;
    mat->m_DeepWaterThicknessFallback = config.m_SSS.m_DeepWaterThicknessFallback;

    // N-01: 细节法线
    mat->m_DetailNormalEnabled     = config.m_Waves.m_DetailNormal.m_Enabled;
    mat->m_DetailNormalIntensity   = config.m_Waves.m_DetailNormal.m_Intensity;
    mat->m_DetailNormalScale       = config.m_Waves.m_DetailNormal.m_Scale;
    mat->m_DetailNormalOctaves     = config.m_Waves.m_DetailNormal.m_OctaveCount;
    mat->m_DetailNormalTimeOffset  = config.m_Waves.m_DetailNormal.m_TimeOffset;
    mat->m_DetailNormalBaseScale   = config.m_Waves.m_DetailNormal.m_DetailBaseScale;

    // ── 纹理绑定（通过名称查找已加载资产）─────────────────────────────────
    if (!config.m_Foam.m_TextureName.empty())
    {
        mat->m_FoamTexture = static_cast<VansTexture*>(
            GetTextureAsset(config.m_Foam.m_TextureName));
        if (!mat->m_FoamTexture)
            VANS_LOG_WARN("[AddWaterNode] foam texture '" << config.m_Foam.m_TextureName
                << "' not found in the AssetDatabase dependency closure.");
    }

    if (!config.m_NormalMap.m_TextureName.empty())
    {
        mat->m_WaterNormalTexture = static_cast<VansTexture*>(
            GetTextureAsset(config.m_NormalMap.m_TextureName));
        if (!mat->m_WaterNormalTexture)
            VANS_LOG_WARN("[AddWaterNode] normalMap texture '" << config.m_NormalMap.m_TextureName
                << "' not found in the AssetDatabase dependency closure.");
    }

    // ── 注册到场景 ─────────────────────────────────────────────────────────
    mat->SetName(waterData.value("name", "WaterMaterial"));
    AddMaterialAsset(mat);

    // 记录完整配置供 VansWaterSystem 初始化时读取
    m_WaterConfig  = config;
    m_WaterMaterial = mat;
    m_HasWater      = true;

    // ── 创建 VansWaterRenderNode，使用引擎内置 "plane" 网格作为水面几何体 ──
    {
        // "plane" is an engine runtime binding for the unit plane mesh.
        VansMesh* planeMesh = static_cast<VansMesh*>(GetMeshAsset("plane"));
        if (planeMesh == nullptr)
        {
            VANS_LOG_WARN("[AddWaterNode] 网格 'plane' 未找到，水面渲染节点将不可见。");
        }
        else
        {
            VansWaterRenderNode* waterNode = new VansWaterRenderNode(device, WATER_NODE);
            waterNode->m_Mesh     = planeMesh;
            waterNode->m_Material = mat;

            // 水面铺满整个地形范围（与 terrain.terrainSize 一致，使用 config.m_WaterLevel 为 Y 高度）
            const float terrainHalfSize = 512.0f; // 默认 1024×1024 地形的半径
            waterNode->SetTransformData(
                glm::vec3(0.0f, config.m_WaterLevel, 0.0f),  // 位置（Y = water level）
                glm::vec3(-90.0f, 0.0f, 0.0f),               // 旋转（plane 默认朝 Z，绕 X 旋转 -90° 使其水平）
                glm::vec3(terrainHalfSize, terrainHalfSize, 1.0f) // 缩放铺满地形
            );

            const std::string nodeName = waterData.value("name", "WaterNode");
            waterNode->SetName(nodeName);
            RegistRenderNode(waterNode, WATER_NODE);
        }
    }

    VANS_LOG("[AddWaterNode] 水面配置加载完成: type=" << typeStr
        << " level=" << config.m_WaterLevel
        << " lod=" << config.m_LOD.m_MaxLOD
        << " waveLOD=" << config.m_Waves.m_MaxLOD
        << " foam=" << (config.m_Foam.m_Enabled ? "on" : "off")
        << " ssr=" << (config.m_SSR.m_Enabled ? "on" : "off"));

    // ── 创建 VansWaterSystem（设计文档 §12.1）────────────────────────────────
    // VansWaterSystem 管理 Water GBuffer 纹理、波形仿真、Pre-Water Compute 和 Composite pass。
    // 通过 m_Scene->GetWaterSystem() 供 VansVKRenderer 在渲染循环中调度。
    {
        VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
        if (vkDevice)
        {
            VansWaterSystem* waterSystem = new VansWaterSystem();
            waterSystem->SetWaterLevel(config.m_WaterLevel);
            waterSystem->SetWaterMaterial(mat);
            waterSystem->Initialize(vkDevice,
                static_cast<uint32_t>(vkDevice->GetRenderWidth()),
                static_cast<uint32_t>(vkDevice->GetRenderHeight()));

            // SetupDescriptors：绑定 WaterGBuf 纹理到合成集（在 SetupVansWaterGBufferPass 之后调用）
            auto* rp = VansRenderPassManager::GetInstance();
            waterSystem->SetupDescriptors(
                rp,
                m_GlobalDescriptorSetLayout,
                m_GlobalDescriptorSet);

            m_WaterSystem = waterSystem;
        }
        else
        {
            VANS_LOG_WARN("[AddWaterNode] 无法获取 VansVKDevice，VansWaterSystem 未初始化。");
        }
    }
}

// ===========================================================================
// Deferred + Screen-space node builders
// ===========================================================================

void VansGraphics::VansScene::AddDeferredNode(VkDevice& device)
{
    VansMesh* mesh = static_cast<VansMesh*>(GetMeshAsset("fullScreenQuad"));
	if (mesh == nullptr)
	{
		VANS_LOG_ERROR("[VansScene] Missing engine mesh 'fullScreenQuad'");
		return;
	}

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
    AddMaterialAsset(material);

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
	if (mesh == nullptr)
	{
		VANS_LOG_ERROR("[VansScene] Missing engine mesh 'fullScreenQuad'");
		return;
	}

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

        VansMaterial* material = CreateMaterialForType(feature.matType);
        material->m_MaterialType = feature.matType;
        PopulateMaterialPassShaders(material, feature.matType);
        material->SetName(feature.name);
        AddMaterialAsset(material);

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
            AddMeshAsset(mesh);
        }

		else
		{
			bool generate_as = sceneMesh.value("support_raytracing", false);
			bool needCpuData = sceneMesh.value("need_cpu_data", false);
            VansMesh* mesh   = new VansMesh(needCpuData, generate_as);
            mesh->LoadMesh(device, vkDevice->GetGraphicsQueue(), &(vkDevice->GetCommandBuffer()), meshPath.c_str(), import_tangent);
            mesh->SetName(sceneMesh["name"]);
            AddMeshAsset(mesh);
        }
    }
}

void VansGraphics::VansScene::LoadShadersFromRegistry(
    const std::string& pathPrefix,
    VkDevice& device)
{
    auto& manager = VansGraphics::VansShaderManager::Get();

    // Load all registered shaders through the manager.
    // This correctly handles Graphics / Compute / RayTracing shader types
    // and populates the manager's internal records so that FindGraphicsShader /
    // FindComputeShader / FindRayTracingShader return valid pointers.
    manager.LoadAll(pathPrefix, device);

    // Populate VansScene::m_Shaders for backward compatibility with
    // GetShaderAsset() used by material-pass lookups.
    std::vector<VansShader*> loaded = manager.GetLoadedShaderAssets();
    for (VansShader* shader : loaded)
    {
        AddShaderAsset(shader);
        // Set up file watching for hot-reload
        if (auto* hotReload = GetShaderHotReloadService())
            hotReload->WatchFolder(shader->GetShaderFolder());
    }
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
        texture->SetName(sceneTexture["name"]);
        AddTextureAsset(texture);
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
    defaultMetalTexture->SetName(name);
    AddTextureAsset(defaultMetalTexture);
}

// ===========================================================================
// LoadResources — load project-wide resources (mesh, texture, shader)
// Consumes the internal upload batch generated from AssetDatabase records.
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

	// Renderer-owned geometry never depends on project assets.
	json engineMeshes = json::array({
		{
			{ "name", "fullScreenQuad" }, { "path", "EngineAssets/Models/fullscreen.obj" },
			{ "need_tangent", false }, { "support_raytracing", false }, { "need_cpu_data", false }
		},
		{
			{ "name", "plane" }, { "path", "EngineAssets/Models/plane.obj" },
			{ "need_tangent", true }, { "support_raytracing", true }, { "need_cpu_data", false }
		}
	});
	LoadMeshesFromJson(engineMeshes, enginePrefix, nativeDevice, vkDevice);

    if (resourceData.contains("mesh") && resourceData["mesh"].is_array())
    {
        LoadMeshesFromJson(resourceData["mesh"], assetPrefix, nativeDevice, vkDevice);
    }

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

    m_Shaders.clear();
    for (VansShader* shader : VansGraphics::VansShaderManager::Get().GetLoadedShaderAssets())
    {
        AddShaderAsset(shader);
    }

    RebuildAssetLookup();

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

VansMaterial* VansGraphics::VansScene::CreateMaterialForType(VansMaterialType matType)
{
    switch (matType)
    {
    case VansMaterialType::VAN_PBR:
    case VansMaterialType::VAN_COAT:            return new VansPBRMaterial();
    case VansMaterialType::VAN_TRANSPARENT:     return new VansTransparentMaterial();
    case VansMaterialType::VAN_POST_PROCESS:    return new VansPostProcessMaterial();
    case VansMaterialType::VAN_SKY_BOX:         return new VansSkyBoxMaterial();
    case VansMaterialType::VAN_DEFERRED:        return new VansDeferredMaterial();
    case VansMaterialType::VAN_SCREEN_SPACE_AO: return new VansSSAOMaterial();
    case VansMaterialType::VAN_SKIN:            return new VansSkinMaterial();
    case VansMaterialType::VAN_CLOTH:           return new VansClothMaterial();
    case VansMaterialType::VAN_HAIR:            return new VansHairMaterial();
    case VansMaterialType::VAN_SUBSURFACE:      return new VansSubsurfaceMaterial();
    case VansMaterialType::VAN_GRASS:           return new VansGrassMaterial();
    case VansMaterialType::VAN_EMISSIVE:        return new VansEmissiveMaterial();
    case VansMaterialType::VAN_DECAL:           return new VansDecalMaterial();
    default:                                    return new VansMaterial();
    }
}

void VansGraphics::VansScene::PopulateMaterialPassShaders(VansMaterial* material, VansMaterialType matType)
{
    if (!material)
        return;

    auto& reg = VansGraphics::VansShaderManager::Get();
    const auto& passMap = reg.GetMaterialPassMap(matType);
    for (const auto& [passName, shaderName] : passMap)
    {
        VansGraphicsShader* passShader = static_cast<VansGraphicsShader*>(GetShaderAsset(shaderName));
        if (passShader)
            material->m_PassShaders[passName] = passShader;
    }
}

VansTexture* VansGraphics::VansScene::ResolveMaterialTexture(const json& sceneMaterial, const char* key)
{
    if (sceneMaterial.contains(key) && sceneMaterial[key].is_string())
        return static_cast<VansTexture*>(GetTextureAsset(sceneMaterial[key].get<std::string>()));
    return nullptr;
}

VansTexture* VansGraphics::VansScene::ResolveMaterialTextureWithFallback(
    const json& sceneMaterial,
    const char* key,
    const char* fallback)
{
    return ResolveTextureOrDefault(ResolveMaterialTexture(sceneMaterial, key), fallback);
}

VansTexture* VansGraphics::VansScene::ResolveMaterialTextureOrDefault(
    const json& sceneMaterial,
    const char* key,
    const char* fallback)
{
    return ResolveTextureOrDefault(ResolveMaterialTexture(sceneMaterial, key), fallback);
}

void VansGraphics::VansScene::PopulateMaterialFromJson(
    VansMaterial* material,
    VansMaterialType matType,
    const json& sceneMaterial)
{
    if (!material)
        return;

    switch (matType)
    {
    case VansMaterialType::VAN_PBR:
    {
        auto* pbr = static_cast<VansPBRMaterial*>(material);
        pbr->m_BaseColorTexture = ResolveMaterialTextureOrDefault(sceneMaterial, "basecolor_texture", "defaultAlbedo");
        pbr->m_NormalTexture = ResolveMaterialTextureOrDefault(sceneMaterial, "normal_texture", "defaultNormal");
        pbr->m_MetalTexture = ResolveMaterialTextureOrDefault(sceneMaterial, "metal_texture", "defaultMetal");
        pbr->m_RoughnessTexture = ResolveMaterialTextureOrDefault(sceneMaterial, "roughness_texture", "defaultRoughness");
        pbr->m_AoTexture = ResolveMaterialTextureOrDefault(sceneMaterial, "ao_texture", "defaultAo");
        pbr->m_BasePBRParam.m_albedo = glm::vec3(sceneMaterial["albedo"][0], sceneMaterial["albedo"][1], sceneMaterial["albedo"][2]);
        pbr->m_BasePBRParam.m_metallic = sceneMaterial["metallic"];
        pbr->m_BasePBRParam.m_roughness = sceneMaterial["roughness"];
        pbr->m_BasePBRParam.m_ao = sceneMaterial["ao"];
        break;
    }
    case VansMaterialType::VAN_CLOTH:
    {
        auto* cloth = static_cast<VansClothMaterial*>(material);
        cloth->m_BaseColorTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "basecolor_texture", "defaultAlbedo");
        cloth->m_NormalTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "normal_texture", "defaultNormal");
        cloth->m_RoughnessTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "roughness_texture", "defaultRoughness");
        cloth->m_AoTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "ao_texture", "defaultAo");
        cloth->m_SheenRoughness = sceneMaterial.value("sheenRoughness", 0.5f);
        break;
    }
    case VansMaterialType::VAN_SKIN:
    {
        auto* skin = static_cast<VansSkinMaterial*>(material);
        skin->m_BaseColorTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "basecolor_texture", "defaultAlbedo");
        skin->m_NormalTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "normal_texture", "defaultNormal");
        break;
    }
    case VansMaterialType::VAN_HAIR:
    {
        auto* hair = static_cast<VansHairMaterial*>(material);
        hair->m_AlbedoAlphaTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "basecolor_texture", "defaultAlbedo");
        hair->m_NormalTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "normal_texture", "defaultNormal");
        hair->m_RoughnessTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "roughness_texture", "defaultRoughness");
        hair->m_AoTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "ao_texture", "defaultAo");
        hair->m_ShiftTexture = ResolveMaterialTexture(sceneMaterial, "shift_texture");
        hair->m_AlphaTexture = ResolveMaterialTexture(sceneMaterial, "alpha_texture");
        hair->m_FlowTexture = ResolveMaterialTexture(sceneMaterial, "flow_texture");
        break;
    }
    case VansMaterialType::VAN_SUBSURFACE:
    {
        auto* sss = static_cast<VansSubsurfaceMaterial*>(material);
        sss->m_BaseColorTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "basecolor_texture", "defaultAlbedo");
        sss->m_NormalTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "normal_texture", "defaultNormal");
        sss->m_ThicknessTexture = ResolveMaterialTexture(sceneMaterial, "thickness_texture");
        sss->m_RoughnessTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "roughness_texture", "defaultRoughness");
        sss->m_SubsurfacePower = sceneMaterial.value("subsurfacePower", 12.234f);
        sss->m_Thickness = sceneMaterial.value("thickness", 0.5f);
        sss->m_SubsurfaceAmount = sceneMaterial.value("subsurfaceAmount", 1.0f);
        sss->m_CurvatureInfluence = sceneMaterial.value("curvatureInfluence", 0.35f);
        if (sceneMaterial.contains("subsurfaceColor") && sceneMaterial["subsurfaceColor"].is_array())
        {
            sss->m_SubsurfaceColor = glm::vec3(
                sceneMaterial["subsurfaceColor"][0],
                sceneMaterial["subsurfaceColor"][1],
                sceneMaterial["subsurfaceColor"][2]);
        }
        sss->m_BasePBRParam.m_albedo = sss->m_SubsurfaceColor;
        sss->m_BasePBRParam.m_roughness = sss->m_SubsurfacePower;
        sss->m_BasePBRParam.m_metallic = sss->m_Thickness;
        sss->m_BasePBRParam.m_ao = sss->m_SubsurfaceAmount;
        sss->m_BasePBRParam.padding = sss->m_CurvatureInfluence;
        break;
    }
    case VansMaterialType::VAN_TRANSPARENT:
    {
        auto* trans = static_cast<VansTransparentMaterial*>(material);
        if (sceneMaterial.contains("textures") && sceneMaterial["textures"].is_array())
        {
            for (const auto& entry : sceneMaterial["textures"])
            {
                std::string slotName = entry.value("slot", "");
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
        break;
    }
    case VansMaterialType::VAN_GRASS:
    {
        auto* grass = static_cast<VansGrassMaterial*>(material);
        grass->m_AlbedoTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "basecolor_texture", "defaultAlbedo");
        grass->m_NormalTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "normal_texture", "defaultNormal");
        grass->m_RoughnessTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "roughness_texture", "defaultRoughness");
        grass->m_TranslucencyTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "translucency_texture", "defaultAo");
        grass->m_AOTexture = ResolveMaterialTextureWithFallback(sceneMaterial, "ao_texture", "defaultAo");
        break;
    }
    case VansMaterialType::VAN_EMISSIVE:
    {
        auto* emissive = static_cast<VansEmissiveMaterial*>(material);
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

        emissive->m_BasePBRParam.m_roughness = sceneMaterial.value("emissive_intensity", 1.0f);
        emissive->m_BasePBRParam.m_metallic = 0.0f;
        emissive->m_BasePBRParam.m_ao = 1.0f;
        emissive->m_EmissiveTexture = ResolveMaterialTextureOrDefault(sceneMaterial, "emissive_texture", "defaultAlbedo");

        if (sceneMaterial.contains("emissive_video"))
        {
            const std::string videoName = sceneMaterial["emissive_video"];
            VansVideoTexture* videoTex = m_VideoManager.Get(videoName);
            if (videoTex && videoTex->IsReady())
            {
                emissive->m_EmissiveTexture = videoTex->GetTexture();
                emissive->m_VideoName = videoName;
                VANS_LOG("[VansScene] Emissive 材质绑定视频纹理: " << videoName);
            }
            else
            {
                VANS_LOG_WARN("[VansScene] emissive_video 未找到或未就绪: " << videoName);
            }
        }
        break;
    }
    case VansMaterialType::VAN_DECAL:
    {
        auto* decal = static_cast<VansDecalMaterial*>(material);
        if (sceneMaterial.contains("albedo") && sceneMaterial["albedo"].is_array())
            decal->m_BasePBRParam.m_albedo = glm::vec3(sceneMaterial["albedo"][0], sceneMaterial["albedo"][1], sceneMaterial["albedo"][2]);
        else
            decal->m_BasePBRParam.m_albedo = glm::vec3(1.0f);

        decal->m_BasePBRParam.m_metallic = sceneMaterial.value("metallic", 0.0f);
        decal->m_BasePBRParam.m_roughness = sceneMaterial.value("roughness", 0.5f);
        decal->m_BasePBRParam.m_ao = sceneMaterial.value("ao", 1.0f);
        decal->m_BaseColorTexture = ResolveMaterialTextureOrDefault(sceneMaterial, "basecolor_texture", "defaultAlbedo");
        decal->m_NormalTexture = ResolveMaterialTextureOrDefault(sceneMaterial, "normal_texture", "defaultNormal");
        decal->m_MetalTexture = ResolveMaterialTextureOrDefault(sceneMaterial, "metal_texture", "defaultMetal");
        decal->m_RoughnessTexture = ResolveMaterialTextureOrDefault(sceneMaterial, "roughness_texture", "defaultRoughness");
        decal->m_AoTexture = ResolveMaterialTextureOrDefault(sceneMaterial, "ao_texture", "defaultAo");
        break;
    }
    case VansMaterialType::VAN_SKY_BOX:
    {
        auto* sky = static_cast<VansSkyBoxMaterial*>(material);
        sky->m_AtmospherePBRParam.m_PlanetRadius = 6340000;
        sky->m_AtmospherePBRParam.m_InitSeaLevel = 200;
        sky->m_AtmospherePBRParam.m_AtmosphereWidth = 80000;
        sky->m_AtmospherePBRParam.m_RayleighScalarHeight = 8500;
        sky->m_AtmospherePBRParam.m_MieScalarHeight = 1200;
        sky->m_AtmospherePBRParam.m_MieAnisotropy = 0.78;
        sky->m_AtmospherePBRParam.m_OzoneLevelCenterHeight = 25000;
        sky->m_AtmospherePBRParam.m_OzoneLevelWidth = 15000;
        sky->m_AtmospherePBRParam.m_SunLuminance = 10;
        break;
    }
    default:
        break;
    }
}

void VansGraphics::VansScene::LoadMaterialsFromJson(const json& materialData)
{
    for (const auto& sceneMaterial : materialData)
    {
        VansMaterialType matType = ParseMaterialType(sceneMaterial["type"], sceneMaterial.value("name", "<unnamed>"));
        VansMaterial* material = CreateMaterialForType(matType);
        material->m_MaterialType = matType;
        PopulateMaterialPassShaders(material, matType);
        PopulateMaterialFromJson(material, matType, sceneMaterial);
        material->SetName(sceneMaterial["name"]);
        AddMaterialAsset(material);
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
	if (!BuildRuntimeSceneFromV2(sceneData))
	{
		VANS_LOG_ERROR("[VansScene] Invalid Scene v2 document: " << path);
		return false;
	}
	m_ReflectionProbeSystem.LoadFromSceneJson(sceneData, path);

	// GI settings are scene data because changing the volume dimensions requires
	// recreating all probe textures and hit buffers for that scene.
	m_GISettings = VansGISettings{};
	if (sceneData.contains("globalIllumination") && sceneData["globalIllumination"].is_object())
	{
		const json& gi = sceneData["globalIllumination"];
		m_GISettings.gridSize = std::clamp(gi.value("gridSize", m_GISettings.gridSize), 1u, 256u);
		m_GISettings.probeSpacing = std::max(gi.value("probeSpacing", m_GISettings.probeSpacing), 0.001f);
		m_GISettings.raysPerProbe = std::clamp(gi.value("raysPerProbe", m_GISettings.raysPerProbe), 1u, 4096u);
		m_GISettings.spatialUpdateDivisor = std::clamp(
			gi.value("spatialUpdateDivisor", m_GISettings.spatialUpdateDivisor), 1u, m_GISettings.gridSize);
		m_GISettings.directionUpdateSlices = std::clamp(
			gi.value("directionUpdateSlices", m_GISettings.directionUpdateSlices), 1u, m_GISettings.raysPerProbe);
		m_GISettings.maxRayDistance = std::max(gi.value("maxRayDistance", m_GISettings.maxRayDistance), 0.001f);
		m_GISettings.normalBias = std::max(gi.value("normalBias", m_GISettings.normalBias), 0.0f);
		m_GISettings.environmentIntensity = std::max(gi.value("environmentIntensity", m_GISettings.environmentIntensity), 0.0f);
		m_GISettings.maxIndirectRadiance = std::max(gi.value("maxIndirectRadiance", m_GISettings.maxIndirectRadiance), 0.0f);
		m_GISettings.maxSHL0 = std::max(gi.value("maxSHL0", m_GISettings.maxSHL0), 0.0f);
		m_GISettings.temporalBlend = std::clamp(gi.value("temporalBlend", m_GISettings.temporalBlend), 0.0f, 1.0f);

		if (gi.contains("regionCenter") && gi["regionCenter"].is_array() && gi["regionCenter"].size() == 3)
		{
			m_GISettings.regionCenter = glm::vec3(
				gi["regionCenter"][0].get<float>(),
				gi["regionCenter"][1].get<float>(),
				gi["regionCenter"][2].get<float>());
		}
	}

	// SSGI is the final consumer of the spatial SH volume. Keep its existing UBO
	// synchronized when a scene overrides the default GI bounds.
	{
		const float volumeSize = static_cast<float>(m_GISettings.gridSize) * m_GISettings.probeSpacing;
		const glm::vec3 volumeMin = m_GISettings.regionCenter - glm::vec3(volumeSize * 0.5f);
		SSGIParamsGPU volumeData{};
		volumeData.giVolumeMin = glm::vec4(volumeMin, 0.0f);
		volumeData.giVolumeSizeAndBias = glm::vec4(
			volumeSize, volumeSize, volumeSize, m_GISettings.normalBias);
		volumeData.traceParams = glm::vec4(
			m_GISettings.maxRayDistance, 0.75f, 0.0f, 0.0f);
		if (m_MaterialManager.m_SSGICBBuffer.GetNativeBuffer() != VK_NULL_HANDLE)
			m_MaterialManager.m_SSGICBBuffer.SetBufferData(
				&volumeData.giVolumeMin, sizeof(glm::vec4), sizeof(glm::vec4) * 3);
	}

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

    // Scene v2 owns component data; referenced assets were uploaded from the AssetDatabase closure.
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

    // Water
    if (sceneData.contains("water"))
    {
        AddWaterNode(nativeDevice, sceneData["water"]);
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
    AddTextureAsset(texture);

    VANS_LOG("[LoadOrGetTexture] Loaded texture: " << texName << " from " << absPath);
    return texture;
}

void VansGraphics::VansScene::LoadShaderFromEntry(
    const VansGraphics::VansShaderEntry& entry,
    const std::string& pathPrefix,
    VkDevice& device)
{
    if (GetShaderAsset(entry.name) != nullptr)
        return; // already loaded

    std::string fullPath = pathPrefix + entry.relativePath;
    VansGraphicsShader* shader = new VansGraphicsShader();
    if (auto* hotReload = GetShaderHotReloadService())
        hotReload->WatchFolder(fullPath);
    shader->InitShader(device, fullPath);
    shader->SetDrawStateData(entry.depthTest, entry.depthWrite, entry.depthCompareOp, entry.cullMode);
    if (entry.pushConstantSize > 0) shader->SetPushConstant(entry.pushConstantSize);
    if (entry.enableAlphaBlend)     shader->SetEnableAlphaBlend(VK_TRUE);
    if (entry.enableDecalBlend)     shader->SetEnableDecalBlend(VK_TRUE);
    shader->SetName(entry.name);
    AddShaderAsset(shader);
}

void VansGraphics::VansScene::ExpandMultiMeshToRenderNodes(
    VkDevice& device,
    VansMesh* multiMesh,
    const std::string& parentName,
    const glm::vec3& position,
    const glm::vec3& rotation,
    const glm::vec3& scale,
	bool supportShadow,
	VansMaterial* materialOverride)
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
    group.sourceMesh = multiMesh;
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

		VansMaterial* material = materialOverride;
		if (materialOverride)
			matType = materialOverride->m_MaterialType;
		else
        {
            material = CreateMaterialForType(matType);
            material->m_MaterialType = matType;
            material->SetName(matKey);
            PopulateMaterialPassShaders(material, matType);

            if (matType == VansMaterialType::VAN_PBR)
            {
                VansPBRMaterial* pbr = static_cast<VansPBRMaterial*>(material);
                // ── PBR material: load textures from FBX info ─────────────────
                VansTexture* diffTex  = LoadOrGetTexture(fbxInfo.diffuseTexPath, true);
                VansTexture* normTex  = LoadOrGetTexture(fbxInfo.normalTexPath, false);
                VansTexture* metalTex = LoadOrGetTexture(fbxInfo.metallicTexPath, false);
                VansTexture* roughTex = LoadOrGetTexture(fbxInfo.roughnessTexPath, false);
                VansTexture* aoTex    = LoadOrGetTexture(fbxInfo.aoTexPath, false);

                pbr->m_BaseColorTexture = ResolveTextureOrDefault(diffTex, "defaultAlbedo");
                pbr->m_NormalTexture    = ResolveTextureOrDefault(normTex, "defaultNormal");
                pbr->m_MetalTexture     = ResolveTextureOrDefault(metalTex, "defaultMetal");
                pbr->m_RoughnessTexture = ResolveTextureOrDefault(roughTex, "defaultRoughness");
                pbr->m_AoTexture        = ResolveTextureOrDefault(aoTex, "defaultAo");

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

            AddMaterialAsset(material);

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

        // Register the sub-mesh in the scene-level lookup list so it can be found by name.
        // 子网格对象由父级 multi-mesh 持有，不能混入项目级 m_Meshes 所有权列表。
        subMesh->SetName(meshName);
        AddSceneSubMeshAsset(subMesh);

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
    VansMesh* meshAsset = group.sourceMesh;
    for (auto* asset : m_Meshes)
    {
        if (meshAsset == nullptr && asset->m_AssetName == meshGroupName)
        {
            meshAsset = dynamic_cast<VansMesh*>(asset);
            break;
        }
    }

    if (!meshAsset)
    {
        VANS_LOG_WARN("[LoadAnimComp] mesh_group '" << meshGroupName
                     << "' has no source mesh. Skipping '" << objectName << "'");
        return nullptr;
    }

    // A skinned bind-pose model may intentionally contain no embedded clips.
    // In that workflow the .vanimator/.vclip assets provide the animation, so
    // clip presence is not a valid capability check. A skeleton is required by
    // both embedded and external animation paths.
    if (meshAsset->m_AnimImportResult.skeleton.bones.empty())
    {
        VANS_LOG_WARN("[LoadAnimComp] mesh_group '" << meshGroupName
                     << "' has no skeleton. Skipping '" << objectName << "'");
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
            case AnimatorParamType::Vector3: controller->SetVector3(param.name, param.vec3Val); break;
            case AnimatorParamType::Quaternion: controller->SetQuaternion(param.name, param.quatVal); break;
            }
        }

        for (auto& [name, clip] : clipsMap)
            controller->AddClip(name, std::move(clip));

        // 传递 AnimGraph（.vanimator 文件必须包含 graph 节点）
        if (assetData.animGraph)
            controller->SetGraph(std::move(assetData.animGraph));
        else
            VANS_LOG_WARN("[LoadAnimComp] .vanimator 文件不含 graph 节点: " << fullAnimatorPath);

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

        // 将所有 clip 封装为 AnimGraphStateMachineNode，驱动 v2 AnimGraph
        auto clipNames = controller->GetClipNames();

        auto smNode = std::make_unique<AnimGraphStateMachineNode>();
        smNode->m_DefaultStateName  = clipNames.empty() ? "" : clipNames.front();
        smNode->m_CurrentStateName  = smNode->m_DefaultStateName;
        for (const auto& clipName : clipNames)
        {
            AnimatorState state;
            state.name       = clipName;
            state.clipName   = clipName;
            state.speed      = 1.0f;
            state.loop       = true;
            state.rootMotion = enableRootMotion;
            smNode->m_States.push_back(state);
        }

        auto graph   = std::make_unique<VansAnimGraph>();
        int  smId    = graph->AddNode(std::move(smNode));
        int  outId   = graph->AddNode(VansAnimGraph::CreateNodeByType(AnimGraphNodeType::Output));
        graph->AddLink(smId, 0, outId, 0);
        controller->SetGraph(std::move(graph));

        VANS_LOG("[LoadAnimComp] Auto-generated v2 graph controller for '" << meshGroupName
                 << "' with " << clipNames.size() << " clip(s)");
    }

    if (enableRootMotion)
        controller->EnableRootMotion(true);

    // ── 创建 AnimationNode ───────────────────────────────────────────────
    if (animJson.contains("motion_matching") && animJson["motion_matching"].is_object())
    {
        const auto& mmJson = animJson["motion_matching"];
        MotionMatchingSettings mmSettings;
        mmSettings.enabled = mmJson.value("enabled", false);
        mmSettings.autoBuild = mmJson.value("auto_build", true);
        mmSettings.sampleRate = mmJson.value("sample_rate", 30.0f);
        mmSettings.searchThrottle = mmJson.value("search_throttle", 0.15f);
        mmSettings.blendDuration = mmJson.value("blend_duration", 0.18f);
        mmSettings.minSwitchCostImprovement = mmJson.value("min_switch_cost_improvement", 0.02f);
        mmSettings.minSwitchInterval = mmJson.value("min_switch_interval", 0.25f);
        mmSettings.blendInterruptFraction = mmJson.value("blend_interrupt_fraction", 0.75f);
        mmSettings.continuationBias = mmJson.value("continuation_bias", 0.10f);
        mmSettings.loopBias = mmJson.value("loop_bias", 0.04f);
        mmSettings.transitionBias = mmJson.value("transition_bias", 0.08f);
        mmSettings.desiredSpeedScale = mmJson.value("desired_speed_scale", 650.0f);
        mmSettings.trajectoryWeight = mmJson.value("trajectory_weight", 1.0f);
        mmSettings.poseWeight = mmJson.value("pose_weight", 0.7f);
        mmSettings.topCandidateCount = mmJson.value("top_candidates", 8);
        mmSettings.allowLegacyBoneDetection = mmJson.value("allow_legacy_bone_detection", true);

        if (mmJson.contains("rig") && mmJson["rig"].is_object())
        {
            const auto& rigJson = mmJson["rig"];
            mmSettings.rig.root = rigJson.value("root", "");
            mmSettings.rig.trajectoryRoot = rigJson.value("trajectory_root", "");
            mmSettings.rig.pelvis = rigJson.value("pelvis", "");
            mmSettings.rig.leftFoot = rigJson.value("left_foot", "");
            mmSettings.rig.rightFoot = rigJson.value("right_foot", "");
            mmSettings.rig.head = rigJson.value("head", "");
            if (rigJson.contains("forward_axis") && rigJson["forward_axis"].is_array() && rigJson["forward_axis"].size() >= 3)
            {
                mmSettings.rig.forwardAxis = glm::vec3(
                    rigJson["forward_axis"][0].get<float>(),
                    rigJson["forward_axis"][1].get<float>(),
                    rigJson["forward_axis"][2].get<float>());
            }
        }

        if (mmJson.contains("schema") && mmJson["schema"].is_object())
        {
            const auto& schemaJson = mmJson["schema"];
            mmSettings.trajectoryWeight = schemaJson.value("trajectory_weight", mmSettings.trajectoryWeight);
            mmSettings.poseWeight = schemaJson.value("pose_weight", mmSettings.poseWeight);
            if (schemaJson.contains("future_times") && schemaJson["future_times"].is_array())
            {
                const auto& timesJson = schemaJson["future_times"];
                for (size_t i = 0; i < mmSettings.schema.futureTimes.size() && i < timesJson.size(); ++i)
                {
                    if (timesJson[i].is_number())
                        mmSettings.schema.futureTimes[i] = timesJson[i].get<float>();
                }
            }
        }

        if (mmJson.contains("include_clip_tokens") && mmJson["include_clip_tokens"].is_array())
        {
            for (const auto& token : mmJson["include_clip_tokens"])
                if (token.is_string()) mmSettings.includeClipNameTokens.push_back(token.get<std::string>());
        }
        if (mmJson.contains("exclude_clip_tokens") && mmJson["exclude_clip_tokens"].is_array())
        {
            for (const auto& token : mmJson["exclude_clip_tokens"])
                if (token.is_string()) mmSettings.excludeClipNameTokens.push_back(token.get<std::string>());
        }
        const auto readStringArray = [](const nlohmann::json& object,
                                        const char* key,
                                        std::vector<std::string>& out)
        {
            if (!object.contains(key) || !object[key].is_array())
                return;
            for (const auto& item : object[key])
                if (item.is_string()) out.push_back(item.get<std::string>());
        };
        const auto readIntArray = [](const nlohmann::json& object,
                                     const char* key,
                                     std::vector<int>& out)
        {
            if (!object.contains(key) || !object[key].is_array())
                return;
            for (const auto& item : object[key])
                if (item.is_number_integer()) out.push_back(item.get<int>());
        };
        const nlohmann::json* searchGroupsJson = nullptr;
        if (mmJson.contains("search_groups") && mmJson["search_groups"].is_array())
            searchGroupsJson = &mmJson["search_groups"];
        else if (mmJson.contains("searchGroups") && mmJson["searchGroups"].is_array())
            searchGroupsJson = &mmJson["searchGroups"];
        if (searchGroupsJson)
        {
            for (const auto& groupJson : *searchGroupsJson)
            {
                if (!groupJson.is_object())
                    continue;

                MotionMatchingSearchGroup group;
                group.name = groupJson.value("name", "");
                group.stance = groupJson.value("stance", group.stance);
                group.phase = groupJson.value("phase", group.phase);
                readIntArray(groupJson, "move_states", group.moveStates);
                readIntArray(groupJson, "moveStates", group.moveStates);
                readStringArray(groupJson, "include", group.includeClipNameTokens);
                readStringArray(groupJson, "include_tokens", group.includeClipNameTokens);
                readStringArray(groupJson, "include_clip_tokens", group.includeClipNameTokens);
                readStringArray(groupJson, "exclude", group.excludeClipNameTokens);
                readStringArray(groupJson, "exclude_tokens", group.excludeClipNameTokens);
                readStringArray(groupJson, "exclude_clip_tokens", group.excludeClipNameTokens);

                if (!group.name.empty() ||
                    !group.includeClipNameTokens.empty() ||
                    !group.excludeClipNameTokens.empty() ||
                    !group.moveStates.empty())
                {
                    mmSettings.searchGroups.push_back(std::move(group));
                }
            }
        }

        controller->ConfigureMotionMatching(mmSettings);
        VANS_LOG("[LoadAnimComp] Motion Matching configured for '" << objectName
                 << "' enabled=" << mmSettings.enabled
                 << " sampleRate=" << mmSettings.sampleRate
                 << " searchThrottle=" << mmSettings.searchThrottle
                 << " searchGroups=" << mmSettings.searchGroups.size());
    }

    if (animJson.contains("foot_placement") && animJson["foot_placement"].is_object())
    {
        const auto& fpJson = animJson["foot_placement"];
        FootPlacementSettings fpSettings;
        fpSettings.enabled = fpJson.value("enabled", false);
        fpSettings.probeHeightAbove = fpJson.value("probe_height_above", fpSettings.probeHeightAbove);
        fpSettings.probeDistanceBelow = fpJson.value("probe_distance_below", fpSettings.probeDistanceBelow);
        fpSettings.probeFootRadius = fpJson.value("probe_foot_radius", fpSettings.probeFootRadius);
        fpSettings.probeFootForwardExtent = fpJson.value("probe_foot_forward_extent", fpSettings.probeFootForwardExtent);
        fpSettings.probeFootBackwardExtent = fpJson.value("probe_foot_backward_extent", fpSettings.probeFootBackwardExtent);
        fpSettings.probeFootSideExtent = fpJson.value("probe_foot_side_extent", fpSettings.probeFootSideExtent);
        fpSettings.footGroundOffset = fpJson.value("foot_ground_offset", fpSettings.footGroundOffset);
        fpSettings.maxSurfaceAngleDeg = fpJson.value("max_surface_angle_deg", fpSettings.maxSurfaceAngleDeg);
        fpSettings.maxVerticalCorrectionUp = fpJson.value("max_vertical_correction_up", fpSettings.maxVerticalCorrectionUp);
        fpSettings.maxVerticalCorrectionDown = fpJson.value("max_vertical_correction_down", fpSettings.maxVerticalCorrectionDown);
        fpSettings.maxHorizontalFootError = fpJson.value("max_horizontal_foot_error", fpSettings.maxHorizontalFootError);
        fpSettings.minContactQuality = fpJson.value("min_contact_quality", fpSettings.minContactQuality);
        fpSettings.pelvisMaxDown = fpJson.value("pelvis_max_down", fpSettings.pelvisMaxDown);
        fpSettings.pelvisMaxUp = fpJson.value("pelvis_max_up", fpSettings.pelvisMaxUp);
        fpSettings.pelvisInterpSpeed = fpJson.value("pelvis_interp_speed", fpSettings.pelvisInterpSpeed);
        fpSettings.ikWeight = fpJson.value("ik_weight", fpSettings.ikWeight);
        fpSettings.ikWeightSpeed = fpJson.value("ik_weight_speed", fpSettings.ikWeightSpeed);
        fpSettings.crouchWeightScale = fpJson.value("crouch_weight_scale", fpSettings.crouchWeightScale);
        fpSettings.stanceChangeSuppressionTime = fpJson.value("stance_change_suppression_time", fpSettings.stanceChangeSuppressionTime);
        fpSettings.footLockInterpSpeed = fpJson.value("foot_lock_interp_speed", fpSettings.footLockInterpSpeed);
        fpSettings.normalInterpSpeed = fpJson.value("normal_interp_speed", fpSettings.normalInterpSpeed);
        fpSettings.groundHeightInterpSpeed = fpJson.value("ground_height_interp_speed", fpSettings.groundHeightInterpSpeed);
        fpSettings.footPlantFullHeight = fpJson.value("foot_plant_full_height", fpSettings.footPlantFullHeight);
        fpSettings.footPlantFadeHeight = fpJson.value("foot_plant_fade_height", fpSettings.footPlantFadeHeight);
        fpSettings.poleInterpSpeed = fpJson.value("pole_interp_speed", fpSettings.poleInterpSpeed);
        fpSettings.enableFootRotation = fpJson.value("enable_foot_rotation", fpSettings.enableFootRotation);
        fpSettings.footRotationWeight = fpJson.value("foot_rotation_weight", fpSettings.footRotationWeight);
        fpSettings.ankleHeightOffset = fpJson.value("ankle_height_offset", fpSettings.ankleHeightOffset);
        fpSettings.debugVisualization = fpJson.value("debug_visualization", fpSettings.debugVisualization);
        fpSettings.collisionMask = fpJson.value("collision_mask", fpSettings.collisionMask);

        if (fpJson.contains("bones") && fpJson["bones"].is_object())
        {
            const auto& bonesJson = fpJson["bones"];
            fpSettings.bones.pelvis = bonesJson.value("pelvis", fpSettings.bones.pelvis);
            fpSettings.bones.leftHip = bonesJson.value("left_hip", fpSettings.bones.leftHip);
            fpSettings.bones.leftKnee = bonesJson.value("left_knee", fpSettings.bones.leftKnee);
            fpSettings.bones.leftFoot = bonesJson.value("left_foot", fpSettings.bones.leftFoot);
            fpSettings.bones.rightHip = bonesJson.value("right_hip", fpSettings.bones.rightHip);
            fpSettings.bones.rightKnee = bonesJson.value("right_knee", fpSettings.bones.rightKnee);
            fpSettings.bones.rightFoot = bonesJson.value("right_foot", fpSettings.bones.rightFoot);
        }

        controller->ConfigureFootPlacement(fpSettings, meshAsset->m_AnimImportResult.skeleton);
        controller->SetFootPlacementEnabled(fpSettings.enabled);
        VANS_LOG("[LoadAnimComp] FootPlacement configured for '" << objectName
                 << "' enabled=" << fpSettings.enabled
                 << " probe=(" << fpSettings.probeHeightAbove << "+" << fpSettings.probeDistanceBelow << ")");
    }

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

VansGraphics::VansRenderNode* VansGraphics::VansScene::FindPrimaryRenderNodeByEntityGuid(const std::string& guid) const
{
    VansScriptObject* obj = FindObjectByGuid(guid);
    if (!obj) return nullptr;
    if (auto* render = obj->GetComponent<VansScriptRenderComponent>())
        return render->m_RenderNode;
    return nullptr;
}

// ===========================================================================
// LoadSceneObjects  — new "objects" JSON format
// ===========================================================================

void VansGraphics::VansScene::LoadSceneObjects(VkDevice& device, json& objectsArray, const std::string& projectRoot)
{
    using namespace VansEngine;

    // === [VansSceneLoadPass::Pass1_ComponentInstantiation] ===
    // 依赖：项目资源与场景材质已加载；输出：对象、组件、渲染/物理等场景列表。
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
		obj->m_EntityGuid = objJson.value("entityGuid", "");
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

                // ── 恢复 enabled 状态（场景保存时写入 JSON 的 enabled 字段）──
                bool renderEnabled = components["render"].value("enabled", true);
                if (!renderEnabled && rc->m_RenderNode)
                    rc->m_RenderNode->SetEnabled(false);
                rc->m_Enabled = renderEnabled;

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
			if (!associatedNode && hasObjTransform)
			{
				obj->m_TransformID = VansTransformStore::AllocateTransform();
				obj->m_OwnsTransform = true;
				VansTransform& transform = VansTransformStore::GetTransform(obj->m_TransformID);
				transform.m_Position = objPos;
				transform.m_Rotation = objRot;
				transform.m_Scale = objScl;
			}
			const uint32_t standaloneTransformID = obj->m_OwnsTransform ? obj->m_TransformID : UINT32_MAX;
			VansPhysicsNode* pn = LoadSinglePhysicsNode(
				components["physics"], associatedNode, standaloneTransformID);
            if (pn)
            {
                auto* pc = new VansScriptPhysicsComponent();
                pc->m_ComponentName = "physics";
                pc->m_PhysicsNode = pn;

                // ── 恢复 enabled 状态 ──
                bool physEnabled = components["physics"].value("enabled", true);
                if (!physEnabled && pc->m_PhysicsNode)
                    pc->m_PhysicsNode->SetEnabled(false);
                pc->m_Enabled = physEnabled;

                obj->AddComponent(pc);
            }
        }

        // ── Cloth component (first pass — collisionSpheres objectRef deferred) ──
        if (components.contains("cloth"))
        {
            auto* renderComp = obj->GetComponent<VansScriptRenderComponent>();
            VansRenderNode* associatedNode = renderComp ? renderComp->m_RenderNode : nullptr;
            std::string profilePath;
            // 将 profilePath 扩展为绝对路径（相对于 projectRoot）
            json clothJson = components["cloth"];
            if (clothJson.contains("profilePath") && !projectRoot.empty())
            {
                std::string relPath = clothJson["profilePath"].get<std::string>();
                clothJson["profilePath"] = projectRoot + relPath;
            }
            VansClothNode* cn = LoadSingleClothNode(clothJson, associatedNode, &profilePath);
            if (cn)
            {
                auto* cc = new VansScriptClothComponent();
                cc->m_ComponentName = "cloth";
                cc->m_ClothNode     = cn;
                cc->m_ProfilePath   = profilePath;
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
		bool objectTransformAllocated = obj->m_OwnsTransform;

        auto ensureObjectTransform = [&]()
        {
            if (!objectTransformAllocated &&
                obj->GetComponent<VansScriptRenderComponent>() == nullptr)
            {
                obj->m_TransformID = VansTransformStore::AllocateTransform();
				obj->m_OwnsTransform = true;
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
					if (tJson.contains("scale") && tJson["scale"].is_array())
					{
						t.m_Scale = glm::vec3(tJson["scale"][0].get<float>(),
							tJson["scale"][1].get<float>(), tJson["scale"][2].get<float>());
					}
					else
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
                pointLight.m_Intensity        = plJson.value("intensity", 1.0f);
                pointLight.m_Radius           = plJson.value("radius", 10.0f);
                pointLight.m_IESProfileIndex  = -1.0f;
                // 位置由 SyncLightTransforms 每帧覆盖
                pointLight.m_Position  = glm::vec3(0.0f);

                // IES profile（可选）：加载 .ies 文件，获取纹理层索引
                if (plJson.contains("ies_profile") && plJson["ies_profile"].is_string())
                {
                    std::string iesPath = projectRoot + plJson["ies_profile"].get<std::string>();
                    int iesIdx = -1;
                    if (m_IESProfileManager.LoadIESFile(iesPath, iesIdx))
                        pointLight.m_IESProfileIndex = static_cast<float>(iesIdx);
                    else
                        VANS_LOG_WARN("[LoadSceneObjects] 点光源 '" << obj->m_ObjectName << "' IES 加载失败: " << iesPath);
                }

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
                spotLight.m_Intensity         = slJson.value("intensity", 1.0f);
                spotLight.m_Radius            = slJson.value("radius", 10.0f);
                spotLight.m_InnerCutOff       = glm::radians(slJson.value("innercutoff", 30.0f));
                spotLight.m_OuterCutOff       = glm::radians(slJson.value("outerCutoff", 45.0f));
                spotLight.m_IESProfileIndex   = -1.0f;
                spotLight.m_IESIntensityScale = slJson.value("ies_intensity_scale", 1.0f);
                spotLight.m_pad0              = 0.0f;
                // 位置和方向由 SyncLightTransforms 每帧覆盖
                spotLight.m_Position     = glm::vec3(0.0f);
                spotLight.m_Direction    = glm::vec3(0.0f, 1.0f, 0.0f);

                // IES profile（可选）：加载 .ies 文件，获取纹理层索引
                if (slJson.contains("ies_profile") && slJson["ies_profile"].is_string())
                {
                    std::string iesPath = projectRoot + slJson["ies_profile"].get<std::string>();
                    int iesIdx = -1;
                    if (m_IESProfileManager.LoadIESFile(iesPath, iesIdx))
                        spotLight.m_IESProfileIndex = static_cast<float>(iesIdx);
                    else
                        VANS_LOG_WARN("[LoadSceneObjects] 聚光灯 '" << obj->m_ObjectName << "' IES 加载失败: " << iesPath);
                }

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
        // Runtime descriptor format: { "video": { "source": "<asset runtime name>" } }
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

    // === [VansSceneLoadPass::Pass2_VehicleReference] ===
    // 依赖：Pass1 已创建对象与 render 组件；输出：Vehicle body/tire 引用完成绑定。
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

    // === [VansSceneLoadPass::Pass3_TransformParent] ===
    // 依赖：Pass1 已分配 TransformID；输出：TransformParentSystem 父子关系。
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

    // === [VansSceneLoadPass::Pass4_AnimationRagdoll] ===
    // 依赖：Pass1/Pass3 已创建 render 与 transform；输出：动画节点与 ragdoll 绑定。
    // ── Fourth pass: resolve animation components ─────────────────────────
    // 此时所有 render 组件（及对应 MultiMeshGroup）均已创建完毕
    for (auto& pending : pendingAnimComps)
    {
        VansAnimationNode* animNode = LoadSingleAnimationComponent(
            pending.animJson, pending.objectName, projectRoot);
        pending.comp->m_AnimNode = animNode;

        // ── 恢复 enabled 状态 ──
        if (animNode)
        {
            bool animEnabled = pending.animJson.value("enabled", true);
            if (!animEnabled)
                animNode->SetEnabled(false);
            pending.comp->m_Enabled = animEnabled;
        }

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

    // === [VansSceneLoadPass::Pass5_ClothAnimationBinding] ===
    // 依赖：Pass4 已完全填充 m_AnimationNodes。
    // 为启用骨骼跟随（followBones）的布料节点绑定 AnimationNode，
    // 并通过 LateBindBonesFromProfile() 解析骨骼名称→索引映射。
    VANS_LOG("[Pass5] 开始布料骨骼绑定，场景对象数=" << m_SceneObjects.size()
             << "，AnimationNode 数=" << m_AnimationNodes.size());

    for (auto* obj : m_SceneObjects)
    {
        auto* clothComp = obj->GetComponent<VansScriptClothComponent>();
        if (!clothComp || !clothComp->m_ClothNode)
            continue;

        VANS_LOG("[Pass5] 找到布料对象: '" << obj->m_ObjectName
                 << "'，profilePath='" << clothComp->m_ProfilePath << "'");

        if (clothComp->m_ProfilePath.empty())
        {
            VANS_LOG_WARN("[Pass5] profilePath 为空，跳过对象 '" << obj->m_ObjectName << "'");
            continue;
        }

        VansEngine::VansClothNode* clothNode = clothComp->m_ClothNode;
        VANS_LOG("[Pass5] ClothNode FollowBones=" << clothNode->IsFollowBones()
                 << "，已有 AnimNode=" << (clothNode->GetAnimationNode() != nullptr ? "是" : "否"));

        if (!clothNode->IsFollowBones())
        {
            VANS_LOG_WARN("[Pass5] followBones=false，跳过。检查 clothprofile 中 followBones 字段。");
            continue;
        }
        if (clothNode->GetAnimationNode())
        {
            VANS_LOG("[Pass5] AnimNode 已绑定，跳过。");
            continue;
        }

        auto* renderComp = obj->GetComponent<VansScriptRenderComponent>();
        if (!renderComp || !renderComp->m_RenderNode)
        {
            VANS_LOG_WARN("[Pass5] 对象 '" << obj->m_ObjectName << "' 无 RenderComponent，跳过。");
            continue;
        }

        const std::string& nodeName   = renderComp->m_RenderNode->m_NodeName;
        const std::string& parentName = renderComp->m_RenderNode->m_ParentGroupName;

        // m_ParentGroupName 仅 multi-mesh 子网格才有值；对于独立布料对象，
        // 父节点关系存在于 VansTransformParentSystem 中，通过 TransformID 查找。
        uint32_t clothTransformID  = renderComp->m_RenderNode->m_TransformID;
        uint32_t parentTransformID = m_TransformParentSystem.GetParent(clothTransformID);

        VANS_LOG("[Pass5] RenderNode.m_NodeName='" << nodeName
                 << "'，m_ParentGroupName='" << parentName
                 << "'，clothTransformID=" << clothTransformID
                 << "，parentTransformID=" << parentTransformID);

        // 策略1：m_ParentGroupName 非空时按名称匹配（multi-mesh 子节点路径）
        // 策略2：parentTransformID 有效时按 TransformID 匹配（独立对象 render.parent 路径）
        auto FindAnimNodeForCloth = [&]() -> VansAnimationNode*
        {
            for (auto* animNode : m_AnimationNodes)
            {
                for (auto* ownedRN : animNode->GetRenderNodes())
                {
                    // 策略1：名称匹配
                    if (!parentName.empty() && ownedRN->m_NodeName == parentName)
                        return animNode;
                    // 策略2：TransformID 匹配
                    if (parentTransformID != UINT32_MAX
                        && ownedRN->m_TransformID == parentTransformID)
                        return animNode;
                }
            }
            return nullptr;
        };

        // 打印所有 AnimNode 的 RenderNode 信息，便于调试
        VANS_LOG("[Pass5] 共有 " << m_AnimationNodes.size() << " 个 AnimationNode：");
        for (auto* animNode : m_AnimationNodes)
        {
            VANS_LOG("[Pass5]   AnimNode='" << animNode->GetName()
                     << "'，RenderNode 数=" << animNode->GetRenderNodes().size()
                     << "，TransformID=" << animNode->GetTransformID());
            for (auto* ownedRN : animNode->GetRenderNodes())
                VANS_LOG("[Pass5]     RenderNode='" << ownedRN->m_NodeName
                         << "' tid=" << ownedRN->m_TransformID);
        }

        VansAnimationNode* foundAnimNode = FindAnimNodeForCloth();
        if (!foundAnimNode)
        {
            VANS_LOG_WARN("[Pass5] 未找到匹配的 AnimNode（parentName='" << parentName
                          << "' parentTransformID=" << parentTransformID << "）");
        }
        else
        {
            VANS_LOG("[Pass5] 匹配成功：AnimNode='" << foundAnimNode->GetName() << "'");

            // profilePath 已在 Pass1 中扩展为绝对路径
            VansEngine::VansClothProfile profile;
            if (profile.LoadFromFile(clothComp->m_ProfilePath))
            {
                clothNode->LateBindBonesFromProfile(profile, foundAnimNode);
                VANS_LOG("[Pass5] 骨骼绑定完成：Cloth '" << clothNode->GetName()
                         << "' → AnimNode '" << foundAnimNode->GetName() << "'");
            }
            else
            {
                VANS_LOG_ERROR("[Pass5] 无法加载 profile '" << clothComp->m_ProfilePath << "'");
            }
        }
    }

    // ── 场景加载完成后，重新触发 auto_play 音频 ──────────────────────────
    // 原因：场景切换时 StopAll() 会停止所有播放；资源级 auto_play 只在
    // LoadFromJson 中触发一次，Runtime 重载后需在此补充调用。
    m_AudioManager.PlayAutoPlay();
}


