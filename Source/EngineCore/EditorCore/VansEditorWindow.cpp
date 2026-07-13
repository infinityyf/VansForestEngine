#include "VansEditorWindow.h"
#include "../RenderCore/VulkanCore/VansVKDevice.h"
#include "../RenderCore/VulkanCore/VansGUIVulkanBackEnd.h"
#include "../RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../RenderCore/VulkanCore/VansMesh.h"
#include "../RenderCore/VulkanCore/VansTexture.h"
#include "../RenderCore/VansCamera.h"
#include "../RenderCore/VansMaterial.h"
#include "../RenderCore/VansScene.h"
#include "../RenderCore/VulkanCore/VansRenderPass.h"
#include "../VansTimer.h"
#include "../PhysicsCore/VansPhysics.h"
#include "Windows/VansHierachyWindow.h"
#include "Windows/VansLightWindow.h"
#include "Windows/VansProjectWindow.h"
#include "Windows/VansSceneWindow.h"
#include "Windows/VansInspectorWindow.h"
#include "Windows/VansGBufferWindow.h"
#include "Windows/VansRenderDebugWindow.h"
#include "Windows/VansScriptorWindow.h"
#include "Windows/VansConsoleWindow.h"
#include "Windows/VansProfilerWindow.h"
#include "Windows/VansAnimGraphEditorWindow.h"
#include "Windows/VansClothProfileEditorWindow.h"
#include "Windows/VansWaterWindow.h"
#include "Windows/VansTerrainWindow.h"
#include "Windows/VansUIEditorWindow.h"
#include "Windows/VansReflectionProbeWindow.h"

#include "../Util/VansProfiler.h"
#include "../Util/VansJobSystem.h"
#include "../Util/VansInputManager.h"
#include "../Util/VansLog.h"
#include "../VansFramePhase.h"

#include "../ProjectSystem/VansProjectManager.h"
#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/VansAssetGuid.h"
#include "../SceneCore/VansSceneDocumentLoader.h"
#include "../SceneCore/VansSceneSaveService.h"
#include "VansAssetDocumentRegistry.h"
#include "VansEditorAssetSaveService.h"
#include "VansSceneEditService.h"
#include "VansEditorSelection.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "backends/imgui_impl_vulkan.h"

#include <iostream>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#ifdef _DEBUG
VansFramePhase g_CurrentFramePhase = VansFramePhase::GameLogic;
#endif

namespace
{
    template <typename T>
    T* AddEditorWindowComponent(std::vector<std::unique_ptr<VansGraphics::VansBaseWindowComponent>>& windows)
    {
        auto window = std::make_unique<T>();
        T* rawWindow = window.get();
        windows.push_back(std::move(window));
        return rawWindow;
    }

    void ApplyProjectTimeSettings()
    {
        auto& projectManager = Vans::VansProjectManager::Get();
        if (!projectManager.IsProjectLoaded())
        {
            return;
        }

        const float physicsDeltaTime = projectManager.GetProjectSettings().GetFixedTimeStep();
        VansGraphics::VansTimer::SetPhysicsDeltaTime(static_cast<double>(physicsDeltaTime));
        VansEngine::VansPhysicsSystem::GetInstance().SetFixedTimeStep(physicsDeltaTime);
        VANS_LOG("[Editor] Applied project physics delta time: " << physicsDeltaTime << "s");
    }

    std::string SafeAssetName(std::string value)
    {
        if (value.empty())
            value = "Unnamed";
        for (char& c : value)
        {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (!std::isalnum(uc) && c != '_' && c != '-')
                c = '_';
        }
        while (!value.empty() && value.front() == '_') value.erase(value.begin());
        while (!value.empty() && value.back() == '_') value.pop_back();
        if (value.empty())
            value = "Unnamed";
        if (value.size() > 96)
            value.resize(96);
        return value;
    }

    std::string SanitizeJsonText(std::string value)
    {
        for (char& c : value)
        {
            const unsigned char uc = static_cast<unsigned char>(c);
            if (uc < 0x20 || uc >= 0x7f)
                c = '_';
        }
        return value;
    }

    Vans::SceneJson Vec3Json(const glm::vec3& value)
    {
        return Vans::SceneJson::array({ value.x, value.y, value.z });
    }

    Vans::VansAssetGuid ReadOrCreateMetaGuid(const std::filesystem::path& metaPath)
    {
        std::ifstream input(metaPath);
        if (input)
        {
            const auto meta = Vans::SceneJson::parse(input, nullptr, false);
            if (!meta.is_discarded() && meta.is_object() && meta.contains("guid") && meta["guid"].is_string())
            {
                Vans::VansAssetGuid parsed;
                if (Vans::VansAssetGuid::TryParse(meta["guid"].get<std::string>(), parsed))
                    return parsed;
            }
        }
        return Vans::VansAssetGuid::New();
    }

    bool WriteJsonFile(const std::filesystem::path& path, const Vans::SceneJson& json)
    {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!output)
            return false;
        output << json.dump(4);
        return static_cast<bool>(output);
    }

    bool IsGuidString(const std::string& value)
    {
        Vans::VansAssetGuid parsed;
        return Vans::VansAssetGuid::TryParse(value, parsed);
    }

    std::string LowerAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    std::string ResolveRuntimeTextureGuid(
        const std::string& textureName,
        Vans::VansAssetDatabase* database,
        const std::string& rootName)
    {
        if (textureName.empty())
            return {};
        if (IsGuidString(textureName))
            return textureName;
        if (database == nullptr)
            return {};

        const std::string wanted = LowerAscii(std::filesystem::path(textureName).stem().string());
        const std::string rootToken = LowerAscii(SafeAssetName(rootName));
        std::string fallbackGuid;
        for (const Vans::VansAssetRecord& record : database->All())
        {
            if (record.type != Vans::VansAssetType::Texture || record.state == Vans::VansAssetState::Missing)
                continue;
            const std::string recordStem = LowerAscii(record.sourcePath.stem().string());
            const std::string recordFile = LowerAscii(record.sourcePath.filename().string());
            if (recordStem != wanted && recordFile != LowerAscii(textureName))
                continue;

            if (fallbackGuid.empty())
                fallbackGuid = record.guid.ToString();
            const std::string recordPath = LowerAscii(record.sourcePath.generic_string());
            if (!rootToken.empty() && recordPath.find(rootToken) != std::string::npos)
                return record.guid.ToString();
        }
        return fallbackGuid;
    }

    std::string ResolveRuntimeTextureGuid(
        VansGraphics::VansTexture* texture,
        Vans::VansAssetDatabase* database,
        const std::string& rootName)
    {
        return texture ? ResolveRuntimeTextureGuid(texture->m_AssetName, database, rootName) : std::string{};
    }

    bool IsDefaultRuntimeTextureName(const std::string& textureName)
    {
        const std::string lowered = LowerAscii(std::filesystem::path(textureName).stem().string());
        return lowered == "defaultalbedo" ||
               lowered == "defaultnormal" ||
               lowered == "defaultmetal" ||
               lowered == "defaultroughness" ||
               lowered == "defaultao";
    }

    void AddTextureRefIfResolvable(
        Vans::SceneJson& textures,
        const char* slot,
        VansGraphics::VansTexture* texture,
        Vans::VansAssetDatabase* database,
        const std::string& rootName)
    {
        const std::string textureGuid = ResolveRuntimeTextureGuid(texture, database, rootName);
        if (!textureGuid.empty())
            textures[slot] = { { "guid", textureGuid } };
    }

    void AddTextureRefFromPathIfResolvable(
        Vans::SceneJson& textures,
        const char* slot,
        const std::string& texturePath,
        Vans::VansAssetDatabase* database,
        const std::string& rootName)
    {
        const std::string textureGuid = ResolveRuntimeTextureGuid(texturePath, database, rootName);
        if (!textureGuid.empty())
            textures[slot] = { { "guid", textureGuid } };
    }

    Vans::SceneJson SerializeFbxMaterialInfo(
        const VansGraphics::FBXSubmeshMaterialInfo& fbxInfo,
        Vans::VansAssetDatabase* database,
        const std::string& rootName)
    {
        Vans::SceneJson json;
        json["schemaVersion"] = 1u;

        if (fbxInfo.IsTransparent())
        {
            json["materialType"] = "transparent";
            json["parameters"] = Vans::SceneJson::object();
            json["textures"] = Vans::SceneJson::array();

            auto addTransparentTexture = [&](const char* slot, const std::string& texturePath)
            {
                const std::string textureGuid = ResolveRuntimeTextureGuid(texturePath, database, rootName);
                if (textureGuid.empty())
                    return;
                json["textures"].push_back({
                    { "slot", slot },
                    { "texture", { { "guid", textureGuid } } }
                });
            };

            addTransparentTexture("diffuse", fbxInfo.diffuseTexPath);
            addTransparentTexture("opacity", fbxInfo.opacityTexPath);
            return json;
        }

        json["materialType"] = "pbr";
        json["parameters"] = {
            { "albedo", Vans::SceneJson::array({
                fbxInfo.diffuseColor[0],
                fbxInfo.diffuseColor[1],
                fbxInfo.diffuseColor[2] }) },
            { "metallic", fbxInfo.metallic },
            { "roughness", fbxInfo.roughness },
            { "ao", 1.0f }
        };

        Vans::SceneJson textures = Vans::SceneJson::object();
        AddTextureRefFromPathIfResolvable(textures, "basecolor", fbxInfo.diffuseTexPath, database, rootName);
        AddTextureRefFromPathIfResolvable(textures, "normal", fbxInfo.normalTexPath, database, rootName);
        AddTextureRefFromPathIfResolvable(textures, "metal", fbxInfo.metallicTexPath, database, rootName);
        AddTextureRefFromPathIfResolvable(textures, "roughness", fbxInfo.roughnessTexPath, database, rootName);
        AddTextureRefFromPathIfResolvable(textures, "ao", fbxInfo.aoTexPath, database, rootName);
        json["textures"] = std::move(textures);
        return json;
    }

    Vans::SceneJson SerializeRuntimeMaterial(
        VansGraphics::VansMaterial* material,
        Vans::VansAssetDatabase* database,
        const std::string& rootName)
    {
        Vans::SceneJson json;
        json["schemaVersion"] = 1u;

        if (auto* pbr = dynamic_cast<VansGraphics::VansPBRMaterial*>(material))
        {
            json["materialType"] = "pbr";
            json["parameters"] = {
                { "albedo", Vec3Json(pbr->m_BasePBRParam.m_albedo) },
                { "metallic", pbr->m_BasePBRParam.m_metallic },
                { "roughness", pbr->m_BasePBRParam.m_roughness },
                { "ao", pbr->m_BasePBRParam.m_ao }
            };
            Vans::SceneJson textures = Vans::SceneJson::object();
            AddTextureRefIfResolvable(textures, "basecolor", pbr->m_BaseColorTexture, database, rootName);
            AddTextureRefIfResolvable(textures, "normal", pbr->m_NormalTexture, database, rootName);
            AddTextureRefIfResolvable(textures, "metal", pbr->m_MetalTexture, database, rootName);
            AddTextureRefIfResolvable(textures, "roughness", pbr->m_RoughnessTexture, database, rootName);
            AddTextureRefIfResolvable(textures, "ao", pbr->m_AoTexture, database, rootName);
            json["textures"] = std::move(textures);
            return json;
        }

        if (auto* transparent = dynamic_cast<VansGraphics::VansTransparentMaterial*>(material))
        {
            json["materialType"] = "transparent";
            json["parameters"] = Vans::SceneJson::object();
            json["textures"] = Vans::SceneJson::array();
            const size_t textureCount = std::max(
                transparent->m_TransparentTextures.size(),
                transparent->m_TransparentTextureMap.size());
            for (size_t index = 0; index < textureCount; ++index)
            {
                const std::string slot = index < transparent->m_TransparentTextureMap.size()
                    ? transparent->m_TransparentTextureMap[index].first
                    : "texture_" + std::to_string(index);
                std::string textureName = index < transparent->m_TransparentTextureMap.size()
                    ? transparent->m_TransparentTextureMap[index].second
                    : std::string{};
                if (textureName.empty() && index < transparent->m_TransparentTextures.size()
                    && transparent->m_TransparentTextures[index] != nullptr)
                {
                    textureName = transparent->m_TransparentTextures[index]->m_AssetName;
                }
                if (textureName.empty())
                    continue;

                Vans::SceneJson textureEntry = { { "slot", slot } };
                const std::string textureGuid = ResolveRuntimeTextureGuid(textureName, database, rootName);
                if (!textureGuid.empty())
                {
                    textureEntry["texture"] = { { "guid", textureGuid } };
                    json["textures"].push_back(std::move(textureEntry));
                }
            }
            return json;
        }

        json["materialType"] = "pbr";
        json["parameters"] = {
            { "albedo", Vans::SceneJson::array({ 1.0f, 1.0f, 1.0f }) },
            { "metallic", 0.0f },
            { "roughness", 0.5f },
            { "ao", 1.0f }
        };
        json["textures"] = Vans::SceneJson::object();
        return json;
    }

    bool HasTextureSlot(const Vans::SceneJson& materialJson, const std::string& slot)
    {
        const auto texturesIt = materialJson.find("textures");
        if (texturesIt == materialJson.end())
            return false;

        if (texturesIt->is_object())
            return texturesIt->contains(slot);

        if (texturesIt->is_array())
        {
            for (const auto& entry : *texturesIt)
            {
                if (entry.is_object() && entry.value("slot", "") == slot)
                    return true;
            }
        }

        return false;
    }

    void AddRuntimeBaseColorFallback(
        Vans::SceneJson& materialJson,
        VansGraphics::VansMaterial* material,
        Vans::VansAssetDatabase* database,
        const std::string& rootName)
    {
        if (material == nullptr || database == nullptr)
            return;

        const std::string materialType = materialJson.value("materialType", "pbr");
        if (materialType == "transparent")
        {
            if (HasTextureSlot(materialJson, "diffuse"))
                return;

            auto* transparent = dynamic_cast<VansGraphics::VansTransparentMaterial*>(material);
            if (transparent == nullptr)
                return;

            std::string textureName;
            for (const auto& [slot, name] : transparent->m_TransparentTextureMap)
            {
                if (slot == "diffuse" || slot == "basecolor" || slot == "baseColor")
                {
                    textureName = name;
                    break;
                }
            }
        if (textureName.empty() && !transparent->m_TransparentTextures.empty()
                && transparent->m_TransparentTextures[0] != nullptr)
            {
                textureName = transparent->m_TransparentTextures[0]->m_AssetName;
            }
            if (IsDefaultRuntimeTextureName(textureName))
            {
                VANS_LOG("[MultiMeshMaterialGen] Skip default transparent diffuse fallback for "
                    << rootName << " material=" << material->m_AssetName
                    << " texture=" << textureName);
                return;
            }

            const std::string textureGuid = ResolveRuntimeTextureGuid(textureName, database, rootName);
            if (!textureGuid.empty())
            {
                if (!materialJson["textures"].is_array())
                    materialJson["textures"] = Vans::SceneJson::array();
                materialJson["textures"].push_back({
                    { "slot", "diffuse" },
                    { "texture", { { "guid", textureGuid } } }
                });
            }
            return;
        }

        if (HasTextureSlot(materialJson, "basecolor"))
            return;

        auto* pbr = dynamic_cast<VansGraphics::VansPBRMaterial*>(material);
        if (pbr == nullptr || pbr->m_BaseColorTexture == nullptr)
            return;

        const std::string runtimeTextureName = pbr->m_BaseColorTexture->m_AssetName;
        if (IsDefaultRuntimeTextureName(runtimeTextureName))
        {
            VANS_LOG("[MultiMeshMaterialGen] Skip default PBR basecolor fallback for "
                << rootName << " material=" << material->m_AssetName
                << " texture=" << runtimeTextureName);
            return;
        }

        const std::string textureGuid = ResolveRuntimeTextureGuid(pbr->m_BaseColorTexture, database, rootName);
        if (!textureGuid.empty())
        {
            if (!materialJson["textures"].is_object())
                materialJson["textures"] = Vans::SceneJson::object();
            materialJson["textures"]["basecolor"] = { { "guid", textureGuid } };
            VANS_LOG("[MultiMeshMaterialGen] Added runtime basecolor fallback for "
                << rootName << " material=" << material->m_AssetName
                << " texture=" << runtimeTextureName
                << " guid=" << textureGuid);
        }
        else
        {
            VANS_LOG_WARN("[MultiMeshMaterialGen] Runtime basecolor fallback unresolved for "
                << rootName << " material=" << material->m_AssetName
                << " texture=" << runtimeTextureName);
        }
    }

    std::string EnsureRuntimeGeneratedMaterialAsset(
        const std::string& rootName,
        VansGraphics::VansRenderNode* node,
        const std::filesystem::path& assetsRoot)
    {
        if (node == nullptr || node->m_Material == nullptr)
            return {};

        const std::string materialName = SafeAssetName(
            rootName + "_" + node->m_Material->m_AssetName + "_" + std::to_string(node->m_SubmeshIndex));
        const std::filesystem::path materialDir = assetsRoot / "Generated" / "MultiMeshMaterials" / SafeAssetName(rootName);
        const std::filesystem::path materialPath = materialDir / (materialName + ".mat");
        const std::filesystem::path metaPath = materialPath.string() + ".meta";
        const Vans::VansAssetGuid guid = ReadOrCreateMetaGuid(metaPath);

        Vans::VansAssetDatabase* database = Vans::VansProjectManager::Get().GetAssetDatabase();
        Vans::SceneJson materialJson;
        if (node->m_SourceMesh != nullptr &&
            node->m_SubmeshIndex != UINT32_MAX &&
            !node->m_SourceMesh->m_SubmeshMaterialInfos.empty())
        {
            const auto& materialInfos = node->m_SourceMesh->m_SubmeshMaterialInfos;
            const VansGraphics::FBXSubmeshMaterialInfo& fbxInfo =
                node->m_SubmeshIndex < materialInfos.size() ? materialInfos[node->m_SubmeshIndex] : materialInfos[0];
            materialJson = SerializeFbxMaterialInfo(fbxInfo, database, rootName);
            AddRuntimeBaseColorFallback(materialJson, node->m_Material, database, rootName);
            const bool hasBaseColor = HasTextureSlot(materialJson, fbxInfo.IsTransparent() ? "diffuse" : "basecolor");
            VANS_LOG("[MultiMeshMaterialGen] " << rootName
                << " submesh=" << node->m_SubmeshIndex
                << " node=" << (node->m_Mesh ? node->m_Mesh->m_SourceNodeName : std::string{})
                << " material=" << node->m_Material->m_AssetName
                << " fbxDiffuse=" << fbxInfo.diffuseTexPath
                << " runtimeBase="
                << (dynamic_cast<VansGraphics::VansPBRMaterial*>(node->m_Material) &&
                    dynamic_cast<VansGraphics::VansPBRMaterial*>(node->m_Material)->m_BaseColorTexture
                    ? dynamic_cast<VansGraphics::VansPBRMaterial*>(node->m_Material)->m_BaseColorTexture->m_AssetName
                    : std::string{})
                << " hasBaseColor=" << (hasBaseColor ? "true" : "false"));
        }
        else
        {
            materialJson = SerializeRuntimeMaterial(node->m_Material, database, rootName);
        }
        materialJson["guid"] = guid.ToString();
        materialJson["importSource"] = {
            { "model", rootName },
            { "sourceNode", SanitizeJsonText(node->m_Mesh ? node->m_Mesh->m_SourceNodeName : std::string{}) },
            { "sourceMaterial", SanitizeJsonText(node->m_Material->m_AssetName) },
            { "submeshIndex", node->m_SubmeshIndex },
            { "generatedFor", "runtimeMultiMeshExpansion" }
        };
        if (!WriteJsonFile(materialPath, materialJson))
            return {};

        Vans::SceneJson metaJson = {
            { "guid", guid.ToString() },
            { "importer", "MaterialImporter" },
            { "version", 1u },
            { "settings", {
                { "generatedFrom", rootName },
                { "generatedFor", "runtimeMultiMeshExpansion" }
            } },
            { "subAssets", Vans::SceneJson::object() }
        };
        if (!WriteJsonFile(metaPath, metaJson))
            return {};

        return guid.ToString();
    }

    const Vans::SceneJson* FindComponent(const Vans::SceneJson& entity, const std::string& type)
    {
        if (!entity.contains("components") || !entity["components"].is_array())
            return nullptr;
        for (const auto& component : entity["components"])
        {
            if (component.is_object() && component.value("type", "") == type)
                return &component;
        }
        return nullptr;
    }

    bool EntityHasChildren(const Vans::SceneJson& root, const std::string& parentId)
    {
        if (!root.contains("entities") || !root["entities"].is_array())
            return false;
        for (const auto& entity : root["entities"])
        {
            if (entity.contains("parent") && entity["parent"].is_string() &&
                entity["parent"].get<std::string>() == parentId)
                return true;
        }
        return false;
    }
}


static void glfw_error_callback(int error, const char* description)
{
    VANS_LOG_ERROR("GLFW Error " << error << ":" << description);
}

static bool CheckGraphicsAPI(VansGraphics::GRAPHICS_API api)
{
    switch (api)
    {
    case VansGraphics::VULKAN:
        if (!glfwVulkanSupported())
        {
            VANS_LOG_ERROR("GLFW: Vulkan Not Supported");
            return false;
        }
        return true;
        break;
    case VansGraphics::INVALIDE:
    default:
        return false;
        break;
    }
}

bool VansGraphics::VansEditorWindow::m_GBufferWindowOpen = false;
bool VansGraphics::VansEditorWindow::m_WaterGBufferWindowOpen = false;

bool VansGraphics::VansEditorWindow::m_RenderDebugWindowOpen = false;
bool VansGraphics::VansEditorWindow::m_HairDebugWindowOpen = false;

bool VansGraphics::VansEditorWindow::m_LightWindowOpen = true;
bool VansGraphics::VansEditorWindow::m_ScriptorWindowOpen = true;
bool VansGraphics::VansEditorWindow::m_ConsoleWindowOpen = true;
bool VansGraphics::VansEditorWindow::m_ProfilerWindowOpen = true;
bool VansGraphics::VansEditorWindow::m_UIEditorWindowOpen = true;
bool VansGraphics::VansEditorWindow::m_WaterWindowOpen = true;
bool VansGraphics::VansEditorWindow::m_TerrainWindowOpen = true;
bool VansGraphics::VansEditorWindow::m_ReflectionProbeWindowOpen = false;

bool VansGraphics::VansEditorWindow::m_WireframeMode = false;
bool VansGraphics::VansEditorWindow::m_VehicleDebugGizmos = false;

VansGraphics::VansBasicWindow VansGraphics::VansEditorWindow::m_VansEditorWindow;
//支持多个相机
std::vector<VansGraphics::VansCamera*> VansGraphics::VansEditorWindow::m_Cameras;

//支持多个窗口
std::vector<std::unique_ptr<VansGraphics::VansBaseWindowComponent>> VansGraphics::VansEditorWindow::m_Windows;

VansGraphics::VansHierachuWindow* VansGraphics::VansEditorWindow::m_HierachyWindow;

VansGraphics::VansLightWindow* VansGraphics::VansEditorWindow::m_LightWindow;

VansGraphics::VansProjectWindow* VansGraphics::VansEditorWindow::m_ProjectWindow;

VansGraphics::VansSceneWindow* VansGraphics::VansEditorWindow::m_SceneWindow;

VansGraphics::VansInspectorWindow* VansGraphics::VansEditorWindow::m_InspectorWindow;

VansGraphics::VansGBufferWindow* VansGraphics::VansEditorWindow::m_GBufferWindow;

VansGraphics::VansRenderDebugWindow* VansGraphics::VansEditorWindow::m_RenderDebugWindow;

VansGraphics::VansScriptorWindow* VansGraphics::VansEditorWindow::m_ScriptorWindow;

VansGraphics::VansConsoleWindow* VansGraphics::VansEditorWindow::m_ConsoleWindow;

VansGraphics::VansProfilerWindow* VansGraphics::VansEditorWindow::m_ProfilerWindow;

VansGraphics::VansAnimGraphEditorWindow* VansGraphics::VansEditorWindow::m_AnimGraphEditorWindow;

VansGraphics::VansUIEditorWindow* VansGraphics::VansEditorWindow::m_UIEditorWindow;

VansGraphics::VansClothProfileEditorWindow* VansGraphics::VansEditorWindow::m_ClothProfileEditorWindow;
VansGraphics::VansWaterWindow* VansGraphics::VansEditorWindow::m_WaterWindow;

VansGraphics::VansTerrainWindow* VansGraphics::VansEditorWindow::m_TerrainWindow;

VansGraphics::VansReflectionProbeWindow* VansGraphics::VansEditorWindow::m_ReflectionProbeWindow;

//脚本上下文
VansScriptContext VansGraphics::VansEditorWindow::m_ScriptContext;

// Project selector overlay
std::unique_ptr<Vans::VansProjectSelector> VansGraphics::VansEditorWindow::m_ProjectSelector;
bool VansGraphics::VansEditorWindow::m_ProjectLoaded = false;
std::string VansGraphics::VansEditorWindow::m_PendingScenePath;

// 运行控制状态：默认处于编辑模式（时间冻结）
VansGraphics::VansEditorPlayState VansGraphics::VansEditorWindow::m_PlayState = VansGraphics::VansEditorPlayState::Editing;
std::string VansGraphics::VansEditorWindow::m_CurrentLoadedScenePath;
// 延迟加载模式：默认 Editor
VansGraphics::VansSceneLoadMode VansGraphics::VansEditorWindow::m_PendingSceneLoadMode = VansGraphics::VansSceneLoadMode::Editor;
VansGraphics::VansEditorWindow::VansPendingProjectLoad VansGraphics::VansEditorWindow::m_PendingProjectLoad;
std::unique_ptr<Vans::VansSceneDocument> VansGraphics::VansEditorWindow::m_SceneDocument;
std::unique_ptr<Vans::VansSceneEditService> VansGraphics::VansEditorWindow::m_SceneEditService;
std::unique_ptr<Vans::VansSceneSaveService> VansGraphics::VansEditorWindow::m_SceneSaveService =
    std::make_unique<Vans::VansSceneSaveService>();
Vans::VansSceneDocument* VansGraphics::VansEditorWindow::GetSceneDocument()
{
    return m_SceneDocument.get();
}

Vans::VansSceneEditService* VansGraphics::VansEditorWindow::GetSceneEditService()
{
    return m_SceneEditService.get();
}

void VansGraphics::VansEditorWindow::ReloadCurrentSceneForEditing()
{
	if (m_PlayState != VansEditorPlayState::Editing || m_CurrentLoadedScenePath.empty())
		return;
	m_PendingSceneLoadMode = VansSceneLoadMode::Editor;
	m_PendingScenePath = m_CurrentLoadedScenePath;
}

void VansGraphics::VansEditorWindow::ProcessRuntimeMultiMeshHierarchyExpansion()
{
    if (m_PlayState != VansEditorPlayState::Editing || !m_PendingScenePath.empty())
        return;
    if (!m_Scene || !m_Scene->IsSceneReady() || !m_SceneDocument || !m_SceneEditService || !m_SceneSaveService)
        return;
    auto& projectManager = Vans::VansProjectManager::Get();
    auto* database = projectManager.GetAssetDatabase();
    if (database == nullptr)
        return;

    const Vans::SceneJson& root = m_SceneDocument->Root();
    if (!root.contains("entities") || !root["entities"].is_array())
        return;

    Vans::SceneJson newEntities = root["entities"];
    bool changed = false;
    const auto& groups = m_Scene->GetMultiMeshGroups();

    for (auto& entity : newEntities)
    {
        if (!entity.is_object())
            continue;
        const std::string entityId = entity.value("id", "");
        const std::string entityName = entity.value("name", "");
        if (entityId.empty() || entityName.empty())
            continue;
        if (EntityHasChildren(root, entityId))
            continue;
        if (FindComponent(entity, "MultiMeshRoot") != nullptr)
            continue;

        const Vans::SceneJson* renderer = FindComponent(entity, "ModelRenderer");
        const Vans::SceneJson* transform = FindComponent(entity, "Transform");
        if (renderer == nullptr || !renderer->value("enabled", true) || !renderer->contains("data"))
            continue;

        const Vans::SceneJson& rendererData = (*renderer)["data"];
        if (!rendererData.value("autoExpandSubmeshes", false))
            continue;

        const std::string modelGuid = rendererData.value("model", Vans::SceneJson::object()).value("guid", "");
        if (modelGuid.empty())
            continue;

        auto groupIt = groups.find(entityName);
        if (groupIt == groups.end())
            continue;
        const MultiMeshGroup& group = groupIt->second;
        if (group.childNodes.empty() || group.sourceMesh == nullptr)
            continue;

        Vans::SceneJson childEntities = Vans::SceneJson::array();
        bool entityExpansionFailed = false;
        std::unordered_set<std::string> usedSlotNames;
        for (VansRenderNode* childNode : group.childNodes)
        {
            if (childNode == nullptr || childNode->m_SubmeshIndex == UINT32_MAX)
                continue;

            const std::string sourceNode = SanitizeJsonText(childNode->m_Mesh ? childNode->m_Mesh->m_SourceNodeName : std::string{});
            const std::string sourceMaterial = SanitizeJsonText(childNode->m_Material ? childNode->m_Material->m_AssetName : std::string{});
            std::string slotBase = (!sourceNode.empty() || !sourceMaterial.empty())
                ? sourceNode + "/" + sourceMaterial
                : "Submesh_" + std::to_string(childNode->m_SubmeshIndex);
            if (slotBase == "/")
                slotBase = "Submesh_" + std::to_string(childNode->m_SubmeshIndex);
            std::string slotName = slotBase;
            uint32_t slotSuffix = 1;
            while (!usedSlotNames.insert(slotName).second)
                slotName = slotBase + "_" + std::to_string(slotSuffix++);

            const std::string materialGuid = EnsureRuntimeGeneratedMaterialAsset(
                entityName, childNode, database->AssetsRoot());
            if (materialGuid.empty())
            {
                entityExpansionFailed = true;
                break;
            }

            const std::string childName = entityName + "_" + SafeAssetName(sourceNode.empty()
                ? "Submesh_" + std::to_string(childNode->m_SubmeshIndex)
                : sourceNode) + "_" + std::to_string(childNode->m_SubmeshIndex);

            Vans::SceneJson child = {
                { "id", Vans::VansAssetGuid::New().ToString() },
                { "name", childName },
                { "parent", entityId },
                { "components", Vans::SceneJson::array({
                    {
                        { "id", Vans::VansAssetGuid::New().ToString() },
                        { "type", "Transform" },
                        { "version", 1u },
                        { "enabled", true },
                        { "data", {
                            { "position", Vans::SceneJson::array({ 0.0f, 0.0f, 0.0f }) },
                            { "rotation", Vans::SceneJson::array({ 0.0f, 0.0f, 0.0f, 1.0f }) },
                            { "scale", Vans::SceneJson::array({ 1.0f, 1.0f, 1.0f }) }
                        } }
                    },
                    {
                        { "id", Vans::VansAssetGuid::New().ToString() },
                        { "type", "ModelRenderer" },
                        { "version", 1u },
                        { "enabled", true },
                        { "data", {
                            { "model", { { "guid", modelGuid } } },
                            { "submesh", {
                                { "index", childNode->m_SubmeshIndex },
                                { "sourceNode", sourceNode },
                                { "sourceMaterial", sourceMaterial },
                                { "slotName", slotName }
                            } },
                            { "castShadows", rendererData.value("castShadows", true) },
                            { "receiveShadows", rendererData.value("receiveShadows", true) },
                            { "rayTracingMode", rendererData.value("rayTracingMode", "auto") },
                            { "visibilityMask", rendererData.value("visibilityMask", 0xffffffffu) },
                            { "materialOverrides", { { "default", { { "guid", materialGuid } } } } },
                            { "orphanOverrides", Vans::SceneJson::object() },
                            { "renderType", rendererData.value("renderType", "opaque") }
                        } }
                    }
                }) }
            };
            childEntities.push_back(std::move(child));
        }

        if (entityExpansionFailed || childEntities.empty())
            continue;

        Vans::SceneJson components = Vans::SceneJson::array();
        if (transform != nullptr)
            components.push_back(*transform);
        else
        {
            components.push_back({
                { "id", Vans::VansAssetGuid::New().ToString() },
                { "type", "Transform" },
                { "version", 1u },
                { "enabled", true },
                { "data", {
                    { "position", Vans::SceneJson::array({ 0.0f, 0.0f, 0.0f }) },
                    { "rotation", Vans::SceneJson::array({ 0.0f, 0.0f, 0.0f, 1.0f }) },
                    { "scale", Vans::SceneJson::array({ 1.0f, 1.0f, 1.0f }) }
                } }
            });
        }
        components.push_back({
            { "id", Vans::VansAssetGuid::New().ToString() },
            { "type", "MultiMeshRoot" },
            { "version", 1u },
            { "enabled", true },
            { "data", {
                { "model", { { "guid", modelGuid } } },
                { "submeshCount", static_cast<uint32_t>(childEntities.size()) },
                { "generation", "runtime-object-hierarchy" }
            } }
        });
        entity["components"] = std::move(components);

        for (auto& childEntity : childEntities)
            newEntities.push_back(std::move(childEntity));

        changed = true;
    }

    if (!changed)
        return;

    const Vans::SceneEditResult editResult = m_SceneEditService->Set("/entities", std::move(newEntities));
    if (!editResult)
    {
        VANS_LOG_ERROR("[MultiMeshHierarchy] Failed to update scene document: " << editResult.message);
        return;
    }

    if (database)
        database->Scan();

    const Vans::SceneSaveResult saveResult = m_SceneSaveService->Save(*m_SceneDocument);
    if (!saveResult)
    {
        VANS_LOG_ERROR("[MultiMeshHierarchy] Failed to save expanded scene: " << saveResult.message);
        return;
    }

    VANS_LOG("[MultiMeshHierarchy] Runtime expansion persisted to scene. Reloading editor scene.");
    ReloadCurrentSceneForEditing();
}

bool VansGraphics::VansEditorWindow::CreateVansEditorWindow(int width, int height, GRAPHICS_API api)
{
    VansLog::Get().RegisterSink(&VansConsole::Get());

    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
    {
        return false;
    }

    // Create window with Vulkan context
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    if (!CheckGraphicsAPI(api))
    {
        glfwTerminate();
        return false;
    }

    m_VansEditorWindow.m_VansGraphicsHandle = glfwCreateWindow(width, height, "ForestEngine", nullptr, nullptr);
    if (!m_VansEditorWindow.m_VansGraphicsHandle)
    {
        VANS_LOG_ERROR("[Editor] Failed to create GLFW window");
        m_VansEditorWindow.m_VansGraphicsHandle = nullptr;
        glfwTerminate();
        return false;
    }

    // Initialize input manager — must be BEFORE ImGui GLFW init so ImGui can chain
    Vans::VansInputManager::Get().Initialize(m_VansEditorWindow.m_VansGraphicsHandle);

    // Register framebuffer resize callback — sets the rebuild flag for the main loop
    glfwSetFramebufferSizeCallback(m_VansEditorWindow.m_VansGraphicsHandle, [](GLFWwindow*, int, int) {
        m_VansEditorWindow.m_WindowStatus.swapChainRebuild = true;
    });

    // Register Physics Pre-Step Callback for Vehicle
    VansEngine::VansPhysicsSystem::GetInstance().SetPreSimulateCallback([](float dt) {
        if (m_Scene && m_Scene->m_Vehicle)
        {
            // This runs on the physics thread!
            // Thread safety note: ensure m_Scene->m_Vehicle is not deleted while this runs.
            // Since shutdown stops physics first, this should be safe.
            m_Scene->m_Vehicle->Step(dt);
        }
    });

    //创建功能窗口
    CreateWindowComponents();

    return true;
}


// ============================================================================
// 运行控制：OnPlay / OnPause / OnResume / OnStop
// ============================================================================

void VansGraphics::VansEditorWindow::OnPlay()
{
    if (m_PlayState != VansEditorPlayState::Editing)
        return;

    if (m_CurrentLoadedScenePath.empty())
    {
        VANS_LOG_WARN("[Editor] OnPlay: no scene loaded, cannot start");
        return;
    }

    if (m_SceneDocument && m_SceneDocument->IsDirty())
    {
        VANS_LOG_WARN("[Editor] Save or undo scene changes before entering Play mode");
        return;
    }

    // Play = 卸载当前场景，以 Runtime 模式重新加载
    // 时间解冻、物理启动均在场景加载完成后（延迟块中）执行
    VANS_LOG("[Editor] Play: reloading scene in Runtime mode: " << m_CurrentLoadedScenePath);
    m_PendingSceneLoadMode = VansGraphics::VansSceneLoadMode::Runtime;
    m_PendingScenePath     = m_CurrentLoadedScenePath;
}

void VansGraphics::VansEditorWindow::OnPause()
{
    if (m_PlayState != VansEditorPlayState::Playing)
        return;

    // 冻结逻辑时间
    VansTimer::SetTimePaused(true);

    // 暂停物理（线程仍存活，仅冻结步进）
    VansEngine::VansPhysicsSystem::GetInstance().PauseSimulation();

    m_PlayState = VansEditorPlayState::Paused;
    VANS_LOG("[Editor] Scene paused");
}

void VansGraphics::VansEditorWindow::OnResume()
{
    if (m_PlayState != VansEditorPlayState::Paused)
        return;

    // 恢复逻辑时间
    VansTimer::SetTimePaused(false);

    // 恢复物理步进
    VansEngine::VansPhysicsSystem::GetInstance().ResumeSimulation();

    m_PlayState = VansEditorPlayState::Playing;
    VANS_LOG("[Editor] Scene resumed");
}

void VansGraphics::VansEditorWindow::OnStop()
{
    if (m_PlayState == VansEditorPlayState::Editing)
        return;

    // 冻结时间，防止重载期间推进
    VansTimer::SetTimePaused(true);

    // 暂停物理（重载时无需模拟）
    VansEngine::VansPhysicsSystem::GetInstance().PauseSimulation();

    // 提前切回编辑模式，避免重载期间触发脚本 Update
    m_PlayState = VansEditorPlayState::Editing;

    // Stop = 卸载场景，以 Editor 模式重新加载
    VANS_LOG("[Editor] Stop: reloading scene in Editor mode: " << m_CurrentLoadedScenePath);
    m_PendingSceneLoadMode = VansGraphics::VansSceneLoadMode::Editor;
    m_PendingScenePath     = m_CurrentLoadedScenePath;
}

// ============================================================================
// 工具栏 UI：Play / Pause / Resume / Stop 按钮
// ============================================================================

void VansGraphics::VansEditorWindow::DrawPlayControlToolbar()
{
    // 此函数在 BeginMenuBar() 内被调用，直接向菜单栏追加控件。
    // 三个按钮始终同时显示，根据当前状态决定各自是否可点击。

    const bool sceneReady = (m_Scene && m_Scene->IsSceneReady() && !m_Scene->IsSceneSwitching());

    constexpr float BUTTON_WIDTH   = 62.0f;
    constexpr float BUTTON_HEIGHT  = 18.0f;
    constexpr float BUTTON_SPACING = 4.0f;

    // 三个按钮始终占据固定宽度，保持菜单栏布局稳定
    const float totalWidth = BUTTON_WIDTH * 3.0f + BUTTON_SPACING * 2.0f;

    // 居中偏移：将光标移至窗口水平中央
    const float windowWidth = ImGui::GetWindowWidth();
    ImGui::SetCursorPosX((windowWidth - totalWidth) * 0.5f);

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(BUTTON_SPACING, 0.0f));

    // ── ? Play ──────────────────────────────────────────────────────────
    // Editing 状态下可点击；其余状态置灰
    const bool canPlay = sceneReady && (m_PlayState == VansEditorPlayState::Editing);
    if (!canPlay) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.13f, 0.45f, 0.13f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.18f, 0.60f, 0.18f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.10f, 0.36f, 0.10f, 1.00f));
    if (ImGui::Button(u8"\u25b6 Play", ImVec2(BUTTON_WIDTH, BUTTON_HEIGHT)))
        OnPlay();
    ImGui::PopStyleColor(3);
    if (!canPlay) ImGui::EndDisabled();

    ImGui::SameLine();

    // ── ? Pause / ? Resume ──────────────────────────────────────────────
    // Playing 时显示 Pause（可点），Paused 时显示 Resume（可点），Editing 时置灰
    const bool canPause  = sceneReady && (m_PlayState == VansEditorPlayState::Playing);
    const bool canResume = sceneReady && (m_PlayState == VansEditorPlayState::Paused);
    const bool pauseActive = canPause || canResume;
    if (!pauseActive) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.50f, 0.40f, 0.05f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.55f, 0.08f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.40f, 0.32f, 0.04f, 1.00f));
    const char* pauseLabel = (m_PlayState == VansEditorPlayState::Paused)
        ? u8"\u25b6 Resume"
        : u8"\u23f8 Pause";
    if (ImGui::Button(pauseLabel, ImVec2(BUTTON_WIDTH, BUTTON_HEIGHT)))
    {
        if (canResume) OnResume();
        else           OnPause();
    }
    ImGui::PopStyleColor(3);
    if (!pauseActive) ImGui::EndDisabled();

    ImGui::SameLine();

    // ── ? Stop ──────────────────────────────────────────────────────────
    // Playing 或 Paused 状态下可点击；Editing 时置灰
    const bool canStop = sceneReady && (m_PlayState != VansEditorPlayState::Editing);
    if (!canStop) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.45f, 0.10f, 0.10f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.65f, 0.14f, 0.14f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.36f, 0.08f, 0.08f, 1.00f));
    if (ImGui::Button(u8"\u23f9 Stop", ImVec2(BUTTON_WIDTH, BUTTON_HEIGHT)))
        OnStop();
    ImGui::PopStyleColor(3);
    if (!canStop) ImGui::EndDisabled();

    ImGui::PopStyleVar(); // ItemSpacing
}

void VansGraphics::VansEditorWindow::CreateWindowComponents()
{
    m_Windows.clear();

    // Create the project selector overlay (shown before a project is loaded)
    m_ProjectSelector = std::make_unique<Vans::VansProjectSelector>();

    m_HierachyWindow = AddEditorWindowComponent<VansHierachuWindow>(m_Windows);

    m_LightWindow = AddEditorWindowComponent<VansLightWindow>(m_Windows);

    m_ProjectWindow = AddEditorWindowComponent<VansProjectWindow>(m_Windows);

    m_SceneWindow = AddEditorWindowComponent<VansSceneWindow>(m_Windows);

    m_InspectorWindow = AddEditorWindowComponent<VansInspectorWindow>(m_Windows);

    m_GBufferWindow = AddEditorWindowComponent<VansGBufferWindow>(m_Windows);

    m_RenderDebugWindow = AddEditorWindowComponent<VansRenderDebugWindow>(m_Windows);

    m_ScriptorWindow = AddEditorWindowComponent<VansScriptorWindow>(m_Windows);

    m_ConsoleWindow = AddEditorWindowComponent<VansConsoleWindow>(m_Windows);

    m_ProfilerWindow = AddEditorWindowComponent<VansProfilerWindow>(m_Windows);

    m_AnimGraphEditorWindow = AddEditorWindowComponent<VansAnimGraphEditorWindow>(m_Windows);

    m_UIEditorWindow = AddEditorWindowComponent<VansUIEditorWindow>(m_Windows);

    m_ClothProfileEditorWindow = AddEditorWindowComponent<VansClothProfileEditorWindow>(m_Windows);

    m_WaterWindow = AddEditorWindowComponent<VansWaterWindow>(m_Windows);

    m_TerrainWindow = AddEditorWindowComponent<VansTerrainWindow>(m_Windows);

    m_ReflectionProbeWindow = AddEditorWindowComponent<VansReflectionProbeWindow>(m_Windows);

}

void VansGraphics::VansEditorWindow::RegisterCameraInputListeners()
{
    Vans::VansInputManager& input = Vans::VansInputManager::Get();

    // Forward keyboard events to all cameras
    input.AddKeyListener("EditorCamera_Key", [](int key, int scancode, int action, int mods) {
        if (action == GLFW_PRESS || action == GLFW_REPEAT)
        {
            for (auto camera : m_Cameras)
            {
                camera->HandleKeyboardInput(key, scancode, action, mods, VansGraphics::VansTimer::GetEditorDeltaTime());
            }
        }
    });

    // Forward mouse move deltas to all cameras
    input.AddMouseMoveListener("EditorCamera_Move", [](double x, double y) {
        double dx, dy;
        Vans::VansInputManager::Get().GetMouseDelta(dx, dy);
        for (auto camera : m_Cameras)
        {
            camera->HandleMouseMovement(static_cast<float>(dx), static_cast<float>(dy));
        }
    });

    // Forward mouse button events to cameras (right-click for look)
    input.AddMouseClickListener("EditorCamera_Click", [](int button, int action, int mods) {
        bool isDown = (action == GLFW_PRESS);
        for (auto camera : m_Cameras)
        {
            camera->SetRightMouseDown(false);
            if (button == GLFW_MOUSE_BUTTON_RIGHT)
            {
                camera->SetRightMouseDown(isDown);
            }
        }
    });
}

void VansGraphics::VansEditorWindow::UnregisterCameraInputListeners()
{
    Vans::VansInputManager& input = Vans::VansInputManager::Get();
    input.RemoveKeyListener("EditorCamera_Key");
    input.RemoveMouseMoveListener("EditorCamera_Move");
    input.RemoveMouseClickListener("EditorCamera_Click");
}

// ============================================================================
// 延迟场景加载处理
// ============================================================================

void VansGraphics::VansEditorWindow::ProcessPendingSceneLoad()
{
    if (m_PendingScenePath.empty())
        return;


    if (m_SceneDocument && m_SceneDocument->IsDirty() &&
        !m_CurrentLoadedScenePath.empty() &&
        std::filesystem::path(m_PendingScenePath).lexically_normal() !=
            std::filesystem::path(m_CurrentLoadedScenePath).lexically_normal())
    {
        VANS_LOG_WARN("[Editor] Scene switch cancelled: save or undo current scene changes first");
        m_PendingScenePath.clear();
        return;
    }

    auto* vkDev = static_cast<VansVKDevice*>(m_GraphicsDevice);
    VANS_LOG("[Editor] Loading deferred scene: " << m_PendingScenePath
             << " [mode=" << (m_PendingSceneLoadMode == VansGraphics::VansSceneLoadMode::Editor ? "Editor" : "Runtime") << "]");
    m_Scene->LoadSceneForRendering(m_PendingScenePath.c_str(), vkDev, m_PendingSceneLoadMode);

    // 记录当前已加载场景路径（用于 Play/Stop 时重载）
    m_CurrentLoadedScenePath = m_PendingScenePath;

    if (m_PendingSceneLoadMode == VansGraphics::VansSceneLoadMode::Editor)
    {
		auto loadResult = Vans::VansSceneDocumentLoader::Load(m_PendingScenePath);
		if (loadResult)
		{
			m_SceneDocument = std::move(loadResult.document);
			m_SceneEditService = std::make_unique<Vans::VansSceneEditService>(*m_SceneDocument);
			VANS_LOG("[SceneDocument] Document ready: " << m_PendingScenePath);
		}
		else
		{
			m_SceneEditService.reset();
			m_SceneDocument.reset();
			for (const auto& diagnostic : loadResult.diagnostics)
				VANS_LOG_ERROR("[SceneDocument] " << diagnostic.jsonPointer << " " << diagnostic.message);
		}

        // Editor 模式：注册相机控制，冻结时间，回到 Editing 状态
        RegisterCameraInputListeners();
        VansTimer::SetTimePaused(true);
        m_PlayState = VansEditorPlayState::Editing;
    }
    else
    {
        // Runtime 模式：解冻时间，启动物理，进入 Playing 状态
        // Play 模式下相机由 Python 脚本接管，注销 Editor 相机控制监听器
        UnregisterCameraInputListeners();
        VansTimer::SetTimePaused(false);
        auto& physics = VansEngine::VansPhysicsSystem::GetInstance();
        if (!physics.IsSimulationRunning())
            physics.StartSimulation();
        else
            physics.ResumeSimulation();
        m_PlayState = VansEditorPlayState::Playing;
        VANS_LOG("[Editor] Scene started playing (Runtime mode)");
    }

    // 更新场景管理器当前场景（尽量使用相对路径）
    auto& projectMgr = Vans::VansProjectManager::Get();
    if (projectMgr.IsProjectLoaded())
    {
        std::string rel = projectMgr.MakeRelativePath(m_PendingScenePath);
        if (!rel.empty())
            projectMgr.GetSceneManager().SetCurrentScene(rel);
    }

    m_PendingScenePath.clear();
}

void VansGraphics::VansEditorWindow::ProcessPendingProjectLoad()
{
    if (!m_PendingProjectLoad.m_Requested)
        return;


    if (m_SceneDocument && m_SceneDocument->IsDirty())
    {
        VANS_LOG_WARN("[Editor] Project switch cancelled: save or undo current scene changes first");
        m_PendingProjectLoad = {};
        return;
    }
    if (Vans::VansAssetDocumentRegistry::Get().HasDirtyDocuments())
    {
        VANS_LOG_WARN("[Editor] Project switch cancelled: save or revert dirty asset changes first");
        m_PendingProjectLoad = {};
        return;
    }

    VansPendingProjectLoad pending = m_PendingProjectLoad;
    m_PendingProjectLoad = {};

    auto* vkDev = static_cast<VansVKDevice*>(m_GraphicsDevice);
    auto& physics = VansEngine::VansPhysicsSystem::GetInstance();
    auto& projectMgr = Vans::VansProjectManager::Get();

    VANS_LOG("[Editor] Processing pending project load: " << pending.m_ProjectPath);

    VansTimer::SetTimePaused(true);
    physics.PauseSimulation();
    UnregisterCameraInputListeners();

    if (vkDev)
    {
        vkDev->WaitForDevice();
    }

    if (m_Scene)
    {
        if (m_Scene->IsSceneReady() || m_Scene->IsSceneSwitching())
        {
            m_Scene->UnLoadScene();
        }
        if (m_Scene->AreResourcesLoaded())
        {
            m_Scene->UnloadProjectResources(vkDev);
        }
    }

    if (projectMgr.IsProjectLoaded())
    {
        projectMgr.CloseProject();
    }
    Vans::VansAssetDocumentRegistry::Get().Clear();

    m_ProjectLoaded = false;
	m_SceneEditService.reset();
	m_SceneDocument.reset();
    m_CurrentLoadedScenePath.clear();
    m_PendingScenePath.clear();
    m_PendingSceneLoadMode = VansGraphics::VansSceneLoadMode::Editor;

    bool loaded = false;
    if (pending.m_CreateNew)
    {
        VANS_LOG("[Editor] Creating project '" << pending.m_ProjectName << "' at " << pending.m_ProjectPath);
        loaded = projectMgr.CreateProject(pending.m_ProjectPath, pending.m_ProjectName);
    }
    else
    {
        VANS_LOG("[Editor] Opening project: " << pending.m_ProjectPath);
        loaded = projectMgr.OpenProject(pending.m_ProjectPath);
    }

    if (!loaded)
    {
        VANS_LOG_ERROR("[Editor] Pending project load failed: " << pending.m_ProjectPath);
        return;
    }

    ApplyProjectTimeSettings();
    m_ProjectLoaded = true;
    VANS_LOG("[Editor] Project load completed");

    m_ScriptContext.SetupProjectVenv(projectMgr.GetProjectRootPath());
	Vans::VansEditorSelection::Clear();

    const Vans::VansProjectConfig& projectConfig = projectMgr.GetConfig();
    if (Vans::VansAssetDatabase* database = projectMgr.GetAssetDatabase())
    {
        auto* vkDev = static_cast<VansVKDevice*>(m_GraphicsDevice);
        const std::filesystem::path scenePath = std::filesystem::path(projectMgr.GetProjectRootPath()) /
            projectConfig.defaultScene;
        if (!m_Scene->LoadProjectAssets(*database, scenePath, vkDev))
        {
            VANS_LOG_ERROR("[Editor] Scene v2 project asset loading failed; scene load cancelled");
            return;
        }
    }
    else
    {
		VANS_LOG_ERROR("[Editor] Scene v2 project has no AssetDatabase");
		return;
    }

    const std::string& defaultScene = projectMgr.GetConfig().defaultScene;
    if (!defaultScene.empty())
    {
        std::string absScenePath = projectMgr.GetProjectRootPath() + defaultScene;
        if (std::filesystem::exists(absScenePath))
        {
            VANS_LOG("[Editor] Deferring default scene load: " << absScenePath);
            m_PendingScenePath = absScenePath;
        }
        else
        {
            VANS_LOG_WARN("[Editor] Default scene not found on disk: " << absScenePath);
        }
    }
}

void VansGraphics::VansEditorWindow::DrawEditorWindows(VansVKDevice* device)
{
    // Start the Dear ImGui frame
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // ── Project Selector Overlay ──────────────────────────────────────────
    // When no project is loaded yet, show the full-screen selector instead
    // of the normal editor windows.
    if (!m_ProjectLoaded)
    {
        auto result = m_ProjectSelector->Render();

        switch (result)
        {
        case Vans::ProjectSelectorResult::OpenExisting:
        {
            const std::string& path = m_ProjectSelector->GetSelectedProjectPath();
            VANS_LOG("[Editor] Queue project open: " << path);
            m_PendingProjectLoad.m_Requested = true;
            m_PendingProjectLoad.m_CreateNew = false;
            m_PendingProjectLoad.m_ProjectPath = path;
            break;
        }
        case Vans::ProjectSelectorResult::CreateNew:
        {
            const std::string& path = m_ProjectSelector->GetSelectedProjectPath();
            const std::string& name = m_ProjectSelector->GetNewProjectName();
            VANS_LOG("[Editor] Queue project creation: " << name << " at " << path);
            m_PendingProjectLoad.m_Requested = true;
            m_PendingProjectLoad.m_CreateNew = true;
            m_PendingProjectLoad.m_ProjectPath = path;
            m_PendingProjectLoad.m_ProjectName = name;
            break;
        }
        case Vans::ProjectSelectorResult::Cancelled:
            glfwSetWindowShouldClose(m_VansEditorWindow.m_VansGraphicsHandle, true);
            break;
        default:
            break;
        }

        // Render the ImGui frame (project selector only)
        ImGui::Render();
        ImDrawData* draw_data = ImGui::GetDrawData();
        device->BeginUIRenderPass();
        ImGui_ImplVulkan_RenderDrawData(draw_data, *static_cast<VkCommandBuffer*>(device->GetNativeCommandBuffer()));
        device->EndUIRenderPass();
        return;
    }

    // ── Normal Editor Windows ─────────────────────────────────────────────
    {
        static bool opt_fullscreen = true;
        static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

        // 设置主窗口标志：无标题栏、无调整大小、无移动、不可停靠（作为容器）
        ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        if (opt_fullscreen)
        {
            const ImGuiViewport* viewport = ImGui::GetMainViewport();
            ImGui::SetNextWindowPos(viewport->WorkPos);
            ImGui::SetNextWindowSize(viewport->WorkSize);
            ImGui::SetNextWindowViewport(viewport->ID);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
            ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
            window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
            window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        }

        if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
            window_flags |= ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        // 开始主容器窗口
        ImGui::Begin("ForestEngine Editor", nullptr, window_flags);
        ImGui::PopStyleVar();

        if (opt_fullscreen)
            ImGui::PopStyleVar(2);

        // 提交 DockSpace
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_DockingEnable)
        {
            ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
            ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);
        }

        // 顶部菜单栏
		const bool editingMode = m_PlayState == VansEditorPlayState::Editing;
		const bool sceneDocumentReady = editingMode && m_SceneDocument && m_SceneDocument->IsHealthy();
		const bool hasDirtyAssets = Vans::VansAssetDocumentRegistry::Get().HasDirtyDocuments();
		std::shared_ptr<Vans::VansOpenAssetDocument> selectedAssetDocument;
		if (!Vans::VansEditorSelection::AssetPath().empty())
			selectedAssetDocument = Vans::VansAssetDocumentRegistry::Get().Find(Vans::VansEditorSelection::AssetPath());
		const bool selectedAssetDirty = editingMode && selectedAssetDocument && selectedAssetDocument->IsDirty();
		if (editingMode && !io.WantTextInput && io.KeyCtrl)
		{
			if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
			{
				const Vans::VansAssetSaveResult assetSaveResult =
					Vans::VansEditorAssetSaveService::Get().SaveAllDirtyAssets();
				if (!assetSaveResult)
				{
					for (const std::string& error : assetSaveResult.errors)
						VANS_LOG_ERROR("[AssetSave] " << error);
				}
				else if (assetSaveResult.wroteFile)
					ReloadCurrentSceneForEditing();
			}
			else if (sceneDocumentReady && ImGui::IsKeyPressed(ImGuiKey_S, false))
			{
				const Vans::SceneSaveResult saveResult = m_SceneSaveService->Save(*m_SceneDocument);
				if (!saveResult) VANS_LOG_ERROR("[SceneSave] " << saveResult.message);
				else if (saveResult.wroteFile) ReloadCurrentSceneForEditing();
			}
			else if (m_SceneEditService && ImGui::IsKeyPressed(ImGuiKey_Z, false))
			{
				auto result = m_SceneEditService->Undo();
				if (result)
					ReloadCurrentSceneForEditing();
			}
			else if (m_SceneEditService && ImGui::IsKeyPressed(ImGuiKey_Y, false))
			{
				auto result = m_SceneEditService->Redo();
				if (result)
					ReloadCurrentSceneForEditing();
			}
		}

        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
				if (ImGui::MenuItem("Save Scene", "Ctrl+S", false,
					sceneDocumentReady && m_SceneDocument->IsDirty()))
				{
					const Vans::SceneSaveResult saveResult = m_SceneSaveService->Save(*m_SceneDocument);
					if (!saveResult) VANS_LOG_ERROR("[SceneSave] " << saveResult.message);
					else if (saveResult.wroteFile) ReloadCurrentSceneForEditing();
				}
				if (ImGui::MenuItem("Save Asset", nullptr, false, selectedAssetDirty))
				{
					const Vans::VansAssetSaveResult assetSaveResult =
						Vans::VansEditorAssetSaveService::Get().SaveAsset(Vans::VansEditorSelection::AssetPath());
					if (!assetSaveResult)
					{
						for (const std::string& error : assetSaveResult.errors)
							VANS_LOG_ERROR("[AssetSave] " << error);
					}
					else if (assetSaveResult.wroteFile)
						ReloadCurrentSceneForEditing();
				}
				if (ImGui::MenuItem("Save All Dirty Assets", "Ctrl+Shift+S", false, hasDirtyAssets))
				{
					const Vans::VansAssetSaveResult assetSaveResult =
						Vans::VansEditorAssetSaveService::Get().SaveAllDirtyAssets();
					if (!assetSaveResult)
					{
						for (const std::string& error : assetSaveResult.errors)
							VANS_LOG_ERROR("[AssetSave] " << error);
					}
					else if (assetSaveResult.wroteFile)
						ReloadCurrentSceneForEditing();
				}
				ImGui::Separator();
                if (ImGui::MenuItem("Exit"))
                {
                    if (m_SceneDocument && m_SceneDocument->IsDirty())
                        VANS_LOG_WARN("[Editor] Exit cancelled: save or undo current scene changes first");
                    else if (Vans::VansAssetDocumentRegistry::Get().HasDirtyDocuments())
                        VANS_LOG_WARN("[Editor] Exit cancelled: save or revert dirty asset changes first");
                    else
                        glfwSetWindowShouldClose(m_VansEditorWindow.m_VansGraphicsHandle, true);
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window"))
            {
                ImGui::MenuItem("Light", nullptr, &m_LightWindowOpen);
                ImGui::MenuItem("Scripts", nullptr, &m_ScriptorWindowOpen);
                ImGui::MenuItem("Console", nullptr, &m_ConsoleWindowOpen);
                ImGui::MenuItem("Profiler", nullptr, &m_ProfilerWindowOpen);
                ImGui::MenuItem("UI Editor", nullptr, &m_UIEditorWindowOpen);
                ImGui::Separator();
                ImGui::MenuItem("GBuffer Visualization", nullptr, &m_GBufferWindowOpen);
                ImGui::MenuItem("Water GBuffer Visualization", nullptr, &m_WaterGBufferWindowOpen);
                ImGui::MenuItem("Render Debug", nullptr, &m_RenderDebugWindowOpen);
                ImGui::MenuItem("Hair Debug", nullptr, &m_HairDebugWindowOpen);
                ImGui::MenuItem("Water", nullptr, &m_WaterWindowOpen);
                ImGui::MenuItem("Terrain", nullptr, &m_TerrainWindowOpen);
                if (m_ReflectionProbeWindow)
                {
                    ImGui::MenuItem("Reflection Probe Inspector", nullptr, &m_ReflectionProbeWindowOpen);
                }
                else
                {
                    ImGui::BeginDisabled();
                    ImGui::MenuItem("Reflection Probe Inspector");
                    ImGui::EndDisabled();
                }
                ImGui::EndMenu();
            }
            // 新增 View 菜单用于控制线框模式
            if (ImGui::BeginMenu("View"))
            {
                if (ImGui::MenuItem("Wireframe", nullptr, &m_WireframeMode))
                {
                }
                ImGui::MenuItem("Vehicle Debug Gizmos", nullptr, &m_VehicleDebugGizmos);
                ImGui::EndMenu();
            }
            // 运行控制工具栏：直接在菜单栏内居中渲染按钮
            DrawPlayControlToolbar();

            ImGui::EndMenuBar();
        }

        //绘制所有窗口
        for (const auto& window : m_Windows)
        {
            window->ShowWindow(*device);
        }

        ImGui::End();
    }



    //GUI handle rendeing
    ImGui::Render();

    ImDrawData* draw_data = ImGui::GetDrawData();

    // ImGui 编辑器覆盖层渲染到 swapchain
    device->BeginUIRenderPass();
    ImGui_ImplVulkan_RenderDrawData(draw_data, *static_cast<VkCommandBuffer*>(device->GetNativeCommandBuffer()));
    device->EndUIRenderPass();
}

void VansGraphics::VansEditorWindow::SetupImGuiStyle()
{
    ImGuiIO& io = ImGui::GetIO();

    // --- Fonts: Latin UI font + Chinese fallback glyphs ---
    ImFont* consolasFont = io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\consolab.ttf", 15.0f);
    if (!consolasFont)
        consolasFont = io.Fonts->AddFontDefault();

    ImFontConfig chineseFontConfig;
    chineseFontConfig.MergeMode = true;
    chineseFontConfig.PixelSnapH = true;

    const ImWchar* chineseGlyphRanges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
    ImFont* chineseFont = io.Fonts->AddFontFromFileTTF(
        "C:\\Windows\\Fonts\\msyh.ttc",
        15.0f,
        &chineseFontConfig,
        chineseGlyphRanges);

    if (!chineseFont)
    {
        chineseFont = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\simhei.ttf",
            15.0f,
            &chineseFontConfig,
            chineseGlyphRanges);
    }

    if (!chineseFont)
    {
        chineseFont = io.Fonts->AddFontFromFileTTF(
            "C:\\Windows\\Fonts\\simsun.ttc",
            15.0f,
            &chineseFontConfig,
            chineseGlyphRanges);
    }

    if (!chineseFont)
    {
        VANS_LOG_WARN("[ImGui] Failed to load a Chinese fallback font. UTF-8 Chinese text may render as '?'");
    }

    io.Fonts->Build();

    // --- Base theme ---
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();

    // --- Shape / Layout ---
    style.WindowRounding    = 4.0f;
    style.ChildRounding     = 4.0f;
    style.FrameRounding     = 3.0f;
    style.PopupRounding     = 4.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabRounding      = 3.0f;
    style.TabRounding       = 4.0f;

    style.WindowPadding     = ImVec2(10.0f, 10.0f);
    style.FramePadding      = ImVec2(6.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 5.0f);
    style.ItemInnerSpacing  = ImVec2(6.0f, 4.0f);
    style.IndentSpacing     = 20.0f;
    style.ScrollbarSize     = 14.0f;
    style.GrabMinSize       = 12.0f;

    style.WindowBorderSize  = 1.0f;
    style.ChildBorderSize   = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;
    style.TabBorderSize     = 0.0f;

    style.WindowTitleAlign   = ImVec2(0.02f, 0.50f);
    style.SeparatorTextAlign = ImVec2(0.0f, 0.5f);

    // --- Colors (UE5 charcoal + slate blue accent) ---
    ImVec4* c = style.Colors;

    // Backgrounds
    c[ImGuiCol_WindowBg]           = ImVec4(0.067f, 0.067f, 0.067f, 1.00f);
    c[ImGuiCol_ChildBg]            = ImVec4(0.067f, 0.067f, 0.067f, 1.00f);
    c[ImGuiCol_PopupBg]            = ImVec4(0.082f, 0.082f, 0.090f, 0.98f);
    c[ImGuiCol_MenuBarBg]          = ImVec4(0.055f, 0.055f, 0.055f, 1.00f);

    // Borders
    c[ImGuiCol_Border]             = ImVec4(0.16f, 0.16f, 0.18f, 0.50f);
    c[ImGuiCol_BorderShadow]       = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

    // Frame (input boxes, sliders, checkboxes)
    c[ImGuiCol_FrameBg]            = ImVec4(0.09f, 0.09f, 0.10f, 1.00f);
    c[ImGuiCol_FrameBgHovered]     = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);
    c[ImGuiCol_FrameBgActive]      = ImVec4(0.10f, 0.28f, 0.50f, 0.80f);

    // Title bar
    c[ImGuiCol_TitleBg]            = ImVec4(0.047f, 0.047f, 0.047f, 1.00f);
    c[ImGuiCol_TitleBgActive]      = ImVec4(0.059f, 0.059f, 0.059f, 1.00f);
    c[ImGuiCol_TitleBgCollapsed]   = ImVec4(0.047f, 0.047f, 0.047f, 0.75f);

    // Tabs
    c[ImGuiCol_Tab]                = ImVec4(0.067f, 0.067f, 0.075f, 1.00f);
    c[ImGuiCol_TabHovered]         = ImVec4(0.15f, 0.33f, 0.55f, 0.80f);
    c[ImGuiCol_TabActive]          = ImVec4(0.12f, 0.28f, 0.48f, 1.00f);
    c[ImGuiCol_TabUnfocused]       = ImVec4(0.055f, 0.055f, 0.060f, 1.00f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.08f, 0.08f, 0.09f, 1.00f);

    // Buttons
    c[ImGuiCol_Button]             = ImVec4(0.13f, 0.13f, 0.15f, 1.00f);
    c[ImGuiCol_ButtonHovered]      = ImVec4(0.15f, 0.33f, 0.55f, 1.00f);
    c[ImGuiCol_ButtonActive]       = ImVec4(0.11f, 0.27f, 0.48f, 1.00f);

    // Headers
    c[ImGuiCol_Header]             = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
    c[ImGuiCol_HeaderHovered]      = ImVec4(0.15f, 0.33f, 0.55f, 0.80f);
    c[ImGuiCol_HeaderActive]       = ImVec4(0.12f, 0.28f, 0.48f, 1.00f);

    // Scrollbar
    c[ImGuiCol_ScrollbarBg]          = ImVec4(0.05f, 0.05f, 0.05f, 0.60f);
    c[ImGuiCol_ScrollbarGrab]        = ImVec4(0.22f, 0.22f, 0.24f, 1.00f);
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.32f, 0.32f, 0.34f, 1.00f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.42f, 0.42f, 0.44f, 1.00f);

    // Slider grab
    c[ImGuiCol_SliderGrab]         = ImVec4(0.22f, 0.46f, 0.73f, 1.00f);
    c[ImGuiCol_SliderGrabActive]   = ImVec4(0.28f, 0.52f, 0.80f, 1.00f);

    // Check mark
    c[ImGuiCol_CheckMark]          = ImVec4(0.28f, 0.56f, 0.88f, 1.00f);

    // Separator
    c[ImGuiCol_Separator]          = ImVec4(0.22f, 0.22f, 0.24f, 0.50f);
    c[ImGuiCol_SeparatorHovered]   = ImVec4(0.18f, 0.38f, 0.62f, 0.78f);
    c[ImGuiCol_SeparatorActive]    = ImVec4(0.14f, 0.34f, 0.58f, 1.00f);

    // Resize grip
    c[ImGuiCol_ResizeGrip]         = ImVec4(0.22f, 0.46f, 0.73f, 0.20f);
    c[ImGuiCol_ResizeGripHovered]  = ImVec4(0.22f, 0.46f, 0.73f, 0.67f);
    c[ImGuiCol_ResizeGripActive]   = ImVec4(0.22f, 0.46f, 0.73f, 0.95f);

    // Docking
    c[ImGuiCol_DockingPreview]     = ImVec4(0.15f, 0.35f, 0.60f, 0.70f);
    c[ImGuiCol_DockingEmptyBg]     = ImVec4(0.04f, 0.04f, 0.04f, 1.00f);

    // Text
    c[ImGuiCol_Text]              = ImVec4(0.86f, 0.86f, 0.88f, 1.00f);
    c[ImGuiCol_TextDisabled]      = ImVec4(0.46f, 0.46f, 0.48f, 1.00f);
    c[ImGuiCol_TextSelectedBg]    = ImVec4(0.18f, 0.40f, 0.68f, 0.43f);

    // Nav / misc
    c[ImGuiCol_NavHighlight]      = ImVec4(0.22f, 0.46f, 0.73f, 1.00f);
    c[ImGuiCol_DragDropTarget]    = ImVec4(0.22f, 0.46f, 0.73f, 0.90f);
    c[ImGuiCol_ModalWindowDimBg]  = ImVec4(0.00f, 0.00f, 0.00f, 0.58f);
    c[ImGuiCol_TableHeaderBg]     = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.16f, 0.16f, 0.18f, 1.00f);
    c[ImGuiCol_TableBorderLight]  = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
    c[ImGuiCol_TableRowBg]        = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);
    c[ImGuiCol_TableRowBgAlt]     = ImVec4(1.00f, 1.00f, 1.00f, 0.02f);
}

void VansGraphics::VansEditorWindow::StartEditorLoop(VansGraphics::VansCamera& camera)
{
    m_Cameras.clear();
    m_Cameras.push_back(&camera);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windowss

    SetupImGuiStyle();

    ImVec4 clear_color = ImVec4(0.08f, 0.08f, 0.08f, 1.00f);

    //初始化GUI的graphics back end
    m_GUIBackEnd->InitBackEnd(*m_GraphicsDevice, m_VansEditorWindow.m_VansGraphicsHandle);

    // Initialize GPU profiler
#if VANS_PROFILER_ENABLED
    {
        auto* vkDev = static_cast<VansVKDevice*>(m_GraphicsDevice);
        Vans::VansGpuProfiler::Get().Init(
            vkDev->GetLogicDevice(),
            vkDev->GetPhysicalDevice(),
            vkDev->GetGraphicsQueueFamilyIndex());
    }
#endif

    //初始化脚本环境
    m_ScriptContext.VansScriptSetup();

    // Main loop
    while (!glfwWindowShouldClose(m_VansEditorWindow.m_VansGraphicsHandle))
    { 
        VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);

        // 项目选择界面阶段没有完整场景帧，Profiler 从项目加载后的下一帧开始记录。
        const bool profilerFrameActive = m_ProjectLoaded;
        if (profilerFrameActive)
            VANS_PROFILER_BEGIN_FRAME();

        // 必须先更新输入帧状态（将 isDown 存入 wasDown），再 PollEvents 接收新事件。
        // 若顺序反转，glfwPollEvents 写入 isDown 后 Update 立即覆盖 wasDown，
        // 导致 IsKeyPressed / IsKeyReleased 永远返回 false。
        {
            VANS_PROFILE_SCOPE("Frame::InputUpdate", Vans::ProfileCategory::Frame);
            Vans::VansInputManager::Get().Update();
        }

        {
            VANS_PROFILE_SCOPE("Frame::PollEvents", Vans::ProfileCategory::Frame);
            glfwPollEvents();
        }

        // Resize swap chain?
        if (m_VansEditorWindow.m_WindowStatus.swapChainRebuild)
        {
            int width, height;
            glfwGetFramebufferSize(m_VansEditorWindow.m_VansGraphicsHandle, &width, &height);
            if (width > 0 && height > 0)
            {
                auto* vkDevice = static_cast<VansVKDevice*>(m_GraphicsDevice);
                vkDevice->OnWindowResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));

                // NOTE: internal render resolution is unchanged, so camera aspect ratio
                // and all SSGI/SSR/GBuffer render targets are unaffected.

                m_VansEditorWindow.m_WindowStatus.swapChainRebuild = false;
            }
            else
            {
                // Window minimized — skip rendering this frame
                auto* vkDev = static_cast<VansVKDevice*>(m_GraphicsDevice);
                if (profilerFrameActive)
                    VANS_PROFILER_END_FRAME(vkDev->GetLogicDevice());
                continue;
            }
        }

        {
            VANS_PROFILE_SCOPE("JobSystem::ProcessMainThreadJobs", Vans::ProfileCategory::JobSystem);
            Vans::VansJobSystem::Get().ProcessMainThreadJobs();
        }

        //更新时间
        {
            VANS_PROFILE_SCOPE("Frame::TimerUpdate", Vans::ProfileCategory::Frame);
            VansGraphics::VansTimer::Update();
        }

        Vans::VansInputManager& input = Vans::VansInputManager::Get();
    VansEngine::VansPhysicsSystem& physics = VansEngine::VansPhysicsSystem::GetInstance();

        // Step Vehicle Physics - MOVED TO PHYSICS THREAD via Callback
        if (m_Scene && m_Scene->IsSceneReady() && m_Scene->m_Vehicle)
        {
            VANS_PROFILE_SCOPE("Frame::VehicleInput", Vans::ProfileCategory::Physics);
            std::lock_guard<std::mutex> simLock(physics.GetSimulationMutex());

            // Vehicle control inputs via InputManager
            const float throttle = input.IsKeyDown(GLFW_KEY_W) ? 1.0f : 0.0f;
            const float brake = input.IsKeyDown(GLFW_KEY_S) ? 1.0f : 0.0f;
            float steer = 0.0f;
            if (input.IsKeyDown(GLFW_KEY_A)) steer -= 1.0f;
            if (input.IsKeyDown(GLFW_KEY_D)) steer += 1.0f;
            m_Scene->m_Vehicle->SetInputs(throttle, brake, steer, 0.0f);
        }

        // Synchronize rigid-body physics transforms to render transforms.
        // IMPORTANT: This uses PxSceneReadLock internally to prevent race conditions
        // with the background physics simulation thread.
        if (physics.IsSimulationRunning() && m_Scene && m_Scene->IsSceneReady())
        {
            VANS_PROFILE_SCOPE("Physics::SyncRigidBodies", Vans::ProfileCategory::Physics);
            m_Scene->UpdatePhysicsTransforms();
        }

        // ── Script update BEFORE CCT flush and BEFORE rendering ──────────
        // Correct game-loop order:
        //   ① UpdatePhysicsTransforms  — read async rigid-body results
        //   ② VansScriptUpdateNonCameraScripts — scripts read input, call queue_move(D)
        //   ③ UpdateCharControllerTransforms — flush D into PhysX (synchronous),
        //                                      write new physics position back to
        //                                      TransformStore so the render below
        //                                      sees the result of THIS frame's input
        //                                      (zero-frame lag)
        //   ④ VansScriptUpdateCameraScripts — camera follows refreshed CCT transform
        //   ⑤ camera.Rendering         — render with up-to-date positions
        m_ScriptContext.SetScene(m_Scene);
        if (m_Scene && m_Scene->IsSceneReady() && m_PlayState == VansEditorPlayState::Playing)
        {
            VANS_PROFILE_SCOPE("Script::Update", Vans::ProfileCategory::Script);
            m_ScriptContext.VansScriptUpdateNonCameraScripts();
        }

        // Flush CCT displacements queued by scripts this frame.
        if (physics.IsSimulationRunning() && m_Scene && m_Scene->IsSceneReady())
        {
            VANS_PROFILE_SCOPE("Physics::FlushCharacterController", Vans::ProfileCategory::Physics);
            m_Scene->UpdateCharControllerTransforms();
        }

        // 相机脚本必须在 CCT 刷新后执行，否则 MainCamera 在场景对象列表中排在角色前面时，
        // 会读取上一帧的角色位置，表现为跟随失效或明显滞后。
        if (m_Scene && m_Scene->IsSceneReady() && m_PlayState == VansEditorPlayState::Playing)
        {
            VANS_PROFILE_SCOPE("Script::UpdateCameraScripts", Vans::ProfileCategory::Script);
            m_ScriptContext.VansScriptUpdateCameraScripts();
        }

        // ── Deferred resource & scene loading ───────────────────────────
        // Process pending loads BEFORE command buffer recording.

        // 0) Project load/reload orchestration.  This must run before resource/scene load.
        {
            VANS_PROFILE_SCOPE("Project::ProcessPendingProjectLoad", Vans::ProfileCategory::IO);
            ProcessPendingProjectLoad();
        }

        // Load Scene v2 content after the AssetDatabase dependency closure.
        {
            VANS_PROFILE_SCOPE("Resource::ProcessPendingSceneLoad", Vans::ProfileCategory::IO);
            ProcessPendingSceneLoad();
            ProcessRuntimeMultiMeshHierarchyExpansion();
        }
        // Rendering, 这里会结束renderpass
        {
            VANS_PROFILE_SCOPE("Render::CameraRendering", Vans::ProfileCategory::CommandRecord);
            camera.Rendering();
        }
        //UI Pass
        m_SceneWindow->RegistCamera(&camera);
        m_SceneWindow->RegistScene(m_Scene);
        m_InspectorWindow->RegistScene(m_Scene);
        m_RenderDebugWindow->RegistScene(m_Scene);
        if (m_ReflectionProbeWindow)
            m_ReflectionProbeWindow->RegistScene(m_Scene);
        {
            VANS_PROFILE_SCOPE("Editor::DrawWindows", Vans::ProfileCategory::Editor);
            DrawEditorWindows(static_cast<VansVKDevice*>(m_GraphicsDevice));
        }

        //结束录制
        {
            VANS_PROFILE_SCOPE("Vulkan::Present", Vans::ProfileCategory::VulkanSubmit);
            camera.Present();
        }

        ImDrawData* draw_data = ImGui::GetDrawData();
        const bool is_minimized = (draw_data->DisplaySize.x <= 0.0f || draw_data->DisplaySize.y <= 0.0f);
        if (!is_minimized)
        {

        }


        // Update and Render additional Platform Windows
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            VANS_PROFILE_SCOPE("ImGui::PlatformWindows", Vans::ProfileCategory::Editor);
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        // Profiler 在 Present 之后结束，确保 Submit / Present CPU 耗时被纳入同一帧。
        {
            auto* vkDev = static_cast<VansVKDevice*>(m_GraphicsDevice);
            if (profilerFrameActive)
                VANS_PROFILER_END_FRAME(vkDev->GetLogicDevice());
        }
    }

    // Unregister camera input listeners
    UnregisterCameraInputListeners();

    m_Cameras.clear();

}

void VansGraphics::VansEditorWindow::DestroyVansEditorWindow()
{
    VansLog::Get().UnregisterSink(&VansConsole::Get());

    m_ProjectSelector.reset();
    m_Windows.clear();

    m_HierachyWindow = nullptr;
    m_LightWindow = nullptr;
    m_ProjectWindow = nullptr;
    m_SceneWindow = nullptr;
    m_InspectorWindow = nullptr;
    m_GBufferWindow = nullptr;
    m_RenderDebugWindow = nullptr;
    m_ScriptorWindow = nullptr;
    m_ConsoleWindow = nullptr;
    m_ProfilerWindow = nullptr;
    m_AnimGraphEditorWindow = nullptr;
    m_UIEditorWindow = nullptr;
    m_ClothProfileEditorWindow = nullptr;
    m_WaterWindow = nullptr;
    m_TerrainWindow = nullptr;
    m_ReflectionProbeWindow = nullptr;

    // Destroy GPU profiler
#if VANS_PROFILER_ENABLED
    Vans::VansGpuProfiler::Get().Destroy();
#endif

    // Unregister Physics Callback on shutdown to avoid calling into destroyed objects
    VansEngine::VansPhysicsSystem::GetInstance().SetPreSimulateCallback(nullptr);

    // Shutdown input manager
    Vans::VansInputManager::Get().Shutdown();

    if (ImGui::GetCurrentContext())
    {
        ImGui::DestroyContext();
    }

    if (m_VansEditorWindow.m_VansGraphicsHandle)
    {
        glfwDestroyWindow(m_VansEditorWindow.m_VansGraphicsHandle);
        m_VansEditorWindow.m_VansGraphicsHandle = nullptr;
    }
    glfwTerminate();
}

