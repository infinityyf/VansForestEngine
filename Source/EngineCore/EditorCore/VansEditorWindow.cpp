#include "VansEditorWindow.h"
#include "../RenderCore/VansCamera.h"
#include "../VansTimer.h"
#include "../EngineAPILayer/Private/EngineAPIImpl.h"
#include "VansAssetDocumentEditService.h"
#include "Windows/VansHierachyWindow.h"
#include "Windows/VansLightWindow.h"
#include "Windows/VansProjectWindow.h"
#include "Windows/VansProjectSettingsWindow.h"
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
#include "Windows/VansGIWindow.h"
#include "Windows/VansPostProcessWindow.h"
#include "Windows/VansShadowDebuggerWindow.h"
#include "Windows/VansPcgWindow.h"
#include "Windows/VansHiZCullWindow.h"
#include "Windows/VansAudioDebugWindow.h"
#include "Windows/VansSkeletonDebugWindow.h"

#include "../Util/VansProfiler.h"
#include "../Util/VansJobSystem.h"
#include "../EventCore/VansEventBus.h"
#include "../Util/VansInputManager.h"
#include "../Util/VansLog.h"
#include "../RuntimeCore/VansFramePhase.h"
#include "../RuntimeCore/VansRuntimeFrameScheduler.h"
#include "../Configration/VansConfigration.h"

#include "../AssetCore/VansAssetGuid.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AnimationCore/VansAnimationNode.h"
#include "../PackagingCore/VansGamePackageBuilder.h"
#include "Windows/VansProjectSelector.h"
#include "../SceneCore/VansSceneDocumentLoader.h"
#include "../SceneCore/VansSceneSaveService.h"
#include "VansAssetDocumentRegistry.h"
#include "VansEditorAssetSaveService.h"
#include "VansEditorRuntimePreviewProjector.h"
#include "VansSceneEditService.h"
#include "VansScenePropertyValueAdapter.h"
#include "VansEditorSelection.h"
#include "ShaderHotReload/VansEditorShaderHotReloadController.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"

#include <iostream>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>

namespace
{
    Vans::EditorAPI::EngineAPIImpl& GetMutableEditorAPI();

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
        auto& editorAPI = GetMutableEditorAPI();
        const float physicsDeltaTime = editorAPI.GetProjectPhysicsFixedTimeStep();
        if (physicsDeltaTime <= 0.0f)
            return;

        VansGraphics::VansTimer::SetPhysicsDeltaTime(static_cast<double>(physicsDeltaTime));
        editorAPI.SetRuntimePhysicsFixedTimeStep(physicsDeltaTime);
        VANS_LOG("[Editor] Applied project physics delta time: " << physicsDeltaTime << "s");
    }

    Vans::EditorAPI::EngineAPIImpl& GetMutableEditorAPI()
    {
        static Vans::EditorAPI::EngineAPIImpl editorAPI;
        return editorAPI;
    }

    std::string GetEditorPackageEngineRoot()
    {
#ifdef FOREST_ENGINE_SOURCE_ROOT
        return FOREST_ENGINE_SOURCE_ROOT;
#else
        if (VansConfigration* configuration = VansConfigration::GetInstance())
            return configuration->GetProjectRootPath();
        return {};
#endif
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

    Vans::VansSerializedValue SerializedObject(
        std::initializer_list<std::pair<std::string, Vans::VansSerializedValue>> fields)
    {
        return Vans::VansSerializedValue::Object(
            std::vector<std::pair<std::string, Vans::VansSerializedValue>>(fields));
    }

    Vans::VansSerializedValue SerializedArray(
        std::initializer_list<Vans::VansSerializedValue> items)
    {
        return Vans::VansSerializedValue::Array(std::vector<Vans::VansSerializedValue>(items));
    }

    Vans::VansSerializedValue DefaultTransformComponent()
    {
        return SerializedObject({
            { "id", Vans::VansSerializedValue::String(Vans::VansAssetGuid::New().ToString()) },
            { "type", Vans::VansSerializedValue::String("Transform") },
            { "version", Vans::VansSerializedValue::Int(1) },
            { "enabled", Vans::VansSerializedValue::Bool(true) },
            { "data", SerializedObject({
                { "position", SerializedArray({
                    Vans::VansSerializedValue::Float(0.0),
                    Vans::VansSerializedValue::Float(0.0),
                    Vans::VansSerializedValue::Float(0.0)
                }) },
                { "rotation", SerializedArray({
                    Vans::VansSerializedValue::Float(0.0),
                    Vans::VansSerializedValue::Float(0.0),
                    Vans::VansSerializedValue::Float(0.0),
                    Vans::VansSerializedValue::Float(1.0)
                }) },
                { "scale", SerializedArray({
                    Vans::VansSerializedValue::Float(1.0),
                    Vans::VansSerializedValue::Float(1.0),
                    Vans::VansSerializedValue::Float(1.0)
                }) }
            }) }
        });
    }

    Vans::VansSerializedValue MaterialOverride(const std::string& materialGuid)
    {
        return SerializedObject({
            { "default", SerializedObject({
                { "guid", Vans::VansSerializedValue::String(materialGuid) }
            }) }
        });
    }

    const Vans::VansSerializedValue* FindComponent(
        const Vans::VansSerializedValue& entity,
        const std::string& type)
    {
        const Vans::VansSerializedValue* components = Vans::FindObjectField(entity, "components");
        if (!components || components->kind != Vans::VansSerializedValue::Kind::Array)
            return nullptr;
        for (const Vans::VansSerializedValue& component : components->arrayItems)
            if (Vans::ReadSerializedStringField(component, "type") == type)
                return &component;
        return nullptr;
    }

    std::unordered_set<std::string> CollectParentEntityIds(
        const Vans::VansSerializedValue& entities)
    {
        std::unordered_set<std::string> parentIds;
        if (entities.kind != Vans::VansSerializedValue::Kind::Array)
            return parentIds;

        parentIds.reserve(entities.arrayItems.size());
        for (const Vans::VansSerializedValue& entity : entities.arrayItems)
        {
            const std::string parentId = Vans::ReadSerializedStringField(entity, "parent");
            if (!parentId.empty())
                parentIds.insert(parentId);
        }
        return parentIds;
    }

    bool HasRuntimeMultiMeshExpansionCandidates(
        const Vans::VansSerializedValue& entities,
        const std::unordered_set<std::string>& parentEntityIds)
    {
        if (entities.kind != Vans::VansSerializedValue::Kind::Array)
            return false;

        for (const Vans::VansSerializedValue& entity : entities.arrayItems)
        {
            if (entity.kind != Vans::VansSerializedValue::Kind::Object)
                continue;

            const std::string entityId = Vans::ReadSerializedStringField(entity, "id");
            if (entityId.empty())
                continue;

            if (FindComponent(entity, "MultiMeshRoot") != nullptr)
                continue;

            const Vans::VansSerializedValue* renderer = FindComponent(entity, "ModelRenderer");
            if (!renderer || !Vans::ReadSerializedBoolField(*renderer, "enabled", true))
                continue;

            const Vans::VansSerializedValue* rendererData = Vans::FindObjectField(*renderer, "data");
            if (!rendererData || !Vans::ReadSerializedBoolField(*rendererData, "autoExpandSubmeshes"))
                continue;

            if (parentEntityIds.find(entityId) != parentEntityIds.end())
                continue;

            const Vans::VansSerializedValue* model = Vans::FindObjectField(*rendererData, "model");
            const std::string modelGuid =
                model ? Vans::ReadSerializedStringField(*model, "guid") : std::string{};
            if (!modelGuid.empty())
                return true;
        }

        return false;
    }

    Vans::VansSerializedValue BuildRuntimeExpandedModelRendererComponent(
        const Vans::VansSerializedValue& sourceRendererData,
        const std::string& modelGuid,
        const Vans::EditorAPI::RuntimeMultiMeshChildSnapshot& childSnapshot,
        const std::string& slotName)
    {
        return SerializedObject({
            { "id", Vans::VansSerializedValue::String(Vans::VansAssetGuid::New().ToString()) },
            { "type", Vans::VansSerializedValue::String("ModelRenderer") },
            { "version", Vans::VansSerializedValue::Int(1) },
            { "enabled", Vans::VansSerializedValue::Bool(true) },
            { "data", SerializedObject({
                { "model", SerializedObject({
                    { "guid", Vans::VansSerializedValue::String(modelGuid) }
                }) },
                { "submesh", SerializedObject({
                    { "index", Vans::VansSerializedValue::Int(childSnapshot.submeshIndex) },
                    { "sourceNode", Vans::VansSerializedValue::String(childSnapshot.sourceNode) },
                    { "sourceMaterial", Vans::VansSerializedValue::String(childSnapshot.sourceMaterial) },
                    { "slotName", Vans::VansSerializedValue::String(slotName) }
                }) },
                { "castShadows", Vans::VansSerializedValue::Bool(
                    Vans::ReadSerializedBoolField(sourceRendererData, "castShadows", true)) },
                { "receiveShadows", Vans::VansSerializedValue::Bool(
                    Vans::ReadSerializedBoolField(sourceRendererData, "receiveShadows", true)) },
                { "rayTracingMode", Vans::VansSerializedValue::String(
                    Vans::ReadSerializedStringField(sourceRendererData, "rayTracingMode", "auto")) },
                { "visibilityMask", Vans::VansSerializedValue::Int(
                    Vans::ReadSerializedIntField(sourceRendererData, "visibilityMask", 0xffffffffll)) },
                { "shadowCasterMask", Vans::VansSerializedValue::Int(
                    Vans::ReadSerializedIntField(sourceRendererData, "shadowCasterMask", 0xffffffffll)) },
                { "materialOverrides", MaterialOverride(childSnapshot.materialGuid) },
                { "orphanOverrides", Vans::VansSerializedValue::Object({}) },
                { "renderType", Vans::VansSerializedValue::String(
                    Vans::ReadSerializedStringField(sourceRendererData, "renderType", "opaque")) }
            }) }
        });
    }

    Vans::VansSerializedValue BuildRuntimeExpandedMultiMeshRootComponent(
        const std::string& modelGuid,
        std::size_t submeshCount)
    {
        return SerializedObject({
            { "id", Vans::VansSerializedValue::String(Vans::VansAssetGuid::New().ToString()) },
            { "type", Vans::VansSerializedValue::String("MultiMeshRoot") },
            { "version", Vans::VansSerializedValue::Int(1) },
            { "enabled", Vans::VansSerializedValue::Bool(true) },
            { "data", SerializedObject({
                { "model", SerializedObject({
                    { "guid", Vans::VansSerializedValue::String(modelGuid) }
                }) },
                { "submeshCount", Vans::VansSerializedValue::Int(
                    static_cast<std::int64_t>(submeshCount)) },
                { "generation", Vans::VansSerializedValue::String("runtime-object-hierarchy") }
            }) }
        });
    }

    Vans::VansSerializedValue BuildRuntimeExpandedChildEntity(
        const std::string& parentEntityId,
        const std::string& childName,
        const std::string& modelGuid,
        const Vans::VansSerializedValue& sourceRendererData,
        const Vans::EditorAPI::RuntimeMultiMeshChildSnapshot& childSnapshot,
        const std::string& slotName)
    {
        return SerializedObject({
            { "id", Vans::VansSerializedValue::String(Vans::VansAssetGuid::New().ToString()) },
            { "name", Vans::VansSerializedValue::String(childName) },
            { "parent", Vans::VansSerializedValue::String(parentEntityId) },
            { "components", SerializedArray({
                DefaultTransformComponent(),
                BuildRuntimeExpandedModelRendererComponent(
                    sourceRendererData,
                    modelGuid,
                    childSnapshot,
                    slotName)
            }) }
        });
    }

    bool RecreateRuntimeMultiMeshExpansionEntities(
        Vans::EditorAPI::IEngineEditorAPI& editorAPI,
        const std::vector<std::string>& parentEntityIds,
        const std::vector<Vans::VansSerializedValue>& runtimeEntities)
    {
        if (parentEntityIds.empty() || runtimeEntities.empty())
            return false;

        for (const std::string& parentEntityId : parentEntityIds)
        {
            Vans::EditorAPI::RuntimeEntityDestroyRequest destroyRequest;
            destroyRequest.entityGuid = parentEntityId;
            if (!editorAPI.DestroyRuntimeEntity(destroyRequest).destroyed)
            {
                VANS_LOG_WARN("[MultiMeshHierarchy] Runtime destroy failed for expanded parent '"
                    << parentEntityId << "'");
                return false;
            }
        }

        Vans::EditorAPI::RuntimeSceneEntitiesCreateRequest createRequest;
        createRequest.sceneEntities.reserve(runtimeEntities.size());
        for (const Vans::VansSerializedValue& entity : runtimeEntities)
            createRequest.sceneEntities.push_back(Vans::FromSerializedValue(entity));

        const Vans::EditorAPI::RuntimeSceneEntitiesCreateResult createResult =
            editorAPI.CreateRuntimeSceneEntities(createRequest);
        if (!createResult.created)
        {
            if (!createResult.message.empty())
                VANS_LOG_WARN("[MultiMeshHierarchy] Runtime entity rebuild failed: "
                    << createResult.message);
            return false;
        }

        return true;
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
bool VansGraphics::VansEditorWindow::m_ProfilerWindowOpen = false;
bool VansGraphics::VansEditorWindow::m_UIEditorWindowOpen = true;
bool VansGraphics::VansEditorWindow::m_WaterWindowOpen = true;
bool VansGraphics::VansEditorWindow::m_TerrainWindowOpen = true;
bool VansGraphics::VansEditorWindow::m_ReflectionProbeWindowOpen = false;
bool VansGraphics::VansEditorWindow::m_GIWindowOpen = false;
bool VansGraphics::VansEditorWindow::m_PostProcessWindowOpen = false;
bool VansGraphics::VansEditorWindow::m_ShadowDebuggerWindowOpen = false;
bool VansGraphics::VansEditorWindow::m_PcgWindowOpen = false;
bool VansGraphics::VansEditorWindow::m_HiZCullWindowOpen = false;
bool VansGraphics::VansEditorWindow::m_ProjectSettingsWindowOpen = false;
bool VansGraphics::VansEditorWindow::m_AudioDebugWindowOpen = false;
bool VansGraphics::VansEditorWindow::m_SkeletonDebugWindowOpen = false;

bool VansGraphics::VansEditorWindow::m_WireframeMode = false;
bool VansGraphics::VansEditorWindow::m_VehicleDebugGizmos = false;
bool VansGraphics::VansEditorWindow::m_HiZCullDebugVisualization = false;
bool VansGraphics::VansEditorWindow::m_SkeletonDebugGizmos = false;
bool VansGraphics::VansEditorWindow::m_SkeletonDebugSelectedOnly = true;
bool VansGraphics::VansEditorWindow::m_SkeletonDebugShowNames = false;
bool VansGraphics::VansEditorWindow::m_SkeletonDebugShowRetargetSource = true;

VansGraphics::VansBasicWindow VansGraphics::VansEditorWindow::m_VansEditorWindow;
//支持多个相机
std::vector<VansGraphics::VansCamera*> VansGraphics::VansEditorWindow::m_Cameras;

//支持多个窗口
std::vector<std::unique_ptr<VansGraphics::VansBaseWindowComponent>> VansGraphics::VansEditorWindow::m_Windows;

VansGraphics::VansHierachuWindow* VansGraphics::VansEditorWindow::m_HierachyWindow;

VansGraphics::VansLightWindow* VansGraphics::VansEditorWindow::m_LightWindow;

VansGraphics::VansProjectWindow* VansGraphics::VansEditorWindow::m_ProjectWindow;

VansGraphics::VansProjectSettingsWindow* VansGraphics::VansEditorWindow::m_ProjectSettingsWindow;

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
VansGraphics::VansGIWindow* VansGraphics::VansEditorWindow::m_GIWindow;
VansGraphics::VansPostProcessWindow* VansGraphics::VansEditorWindow::m_PostProcessWindow;
VansGraphics::VansShadowDebuggerWindow* VansGraphics::VansEditorWindow::m_ShadowDebuggerWindow;
VansGraphics::VansPcgWindow* VansGraphics::VansEditorWindow::m_PcgWindow;
VansGraphics::VansHiZCullWindow* VansGraphics::VansEditorWindow::m_HiZCullWindow;
VansGraphics::VansAudioDebugWindow* VansGraphics::VansEditorWindow::m_AudioDebugWindow;
VansGraphics::VansSkeletonDebugWindow* VansGraphics::VansEditorWindow::m_SkeletonDebugWindow;

// Project selector overlay
std::unique_ptr<Vans::VansProjectSelector> VansGraphics::VansEditorWindow::m_ProjectSelector;
bool VansGraphics::VansEditorWindow::m_ProjectLoaded = false;
std::string VansGraphics::VansEditorWindow::m_PendingScenePath;

std::string VansGraphics::VansEditorWindow::m_CurrentLoadedScenePath;
// 延迟加载模式：默认 Editor
Vans::EditorAPI::RuntimeSceneLoadMode VansGraphics::VansEditorWindow::m_PendingSceneLoadMode = Vans::EditorAPI::RuntimeSceneLoadMode::Editor;
VansGraphics::VansEditorWindow::VansPendingProjectLoad VansGraphics::VansEditorWindow::m_PendingProjectLoad;
std::unique_ptr<Vans::VansSceneDocument> VansGraphics::VansEditorWindow::m_SceneDocument;
std::unique_ptr<Vans::VansSceneEditService> VansGraphics::VansEditorWindow::m_SceneEditService;
std::unique_ptr<Vans::VansSceneSaveService> VansGraphics::VansEditorWindow::m_SceneSaveService =
    std::make_unique<Vans::VansSceneSaveService>();
Vans::EditorAPI::IEngineEditorAPI* VansGraphics::VansEditorWindow::m_EditorAPI = nullptr;
std::uint64_t VansGraphics::VansEditorWindow::m_RuntimeMultiMeshExpansionScannedStateId = 0;

Vans::VansSceneDocument* VansGraphics::VansEditorWindow::GetSceneDocument()
{
    return m_SceneDocument.get();
}

Vans::VansSceneEditService* VansGraphics::VansEditorWindow::GetSceneEditService()
{
    return m_SceneEditService.get();
}

Vans::EditorAPI::IEngineEditorAPI* VansGraphics::VansEditorWindow::GetEditorAPI()
{
    return m_EditorAPI;
}

bool VansGraphics::VansEditorWindow::IsEditing()
{
	return GetMutableEditorAPI().GetPlayState() == Vans::EditorAPI::EnginePlayState::Edit;
}

void VansGraphics::VansEditorWindow::ReloadCurrentSceneForEditing()
{
	if (!IsEditing() || m_CurrentLoadedScenePath.empty())
		return;
	m_PendingSceneLoadMode = Vans::EditorAPI::RuntimeSceneLoadMode::Editor;
	m_PendingScenePath = m_CurrentLoadedScenePath;
}

void VansGraphics::VansEditorWindow::ProcessRuntimeMultiMeshHierarchyExpansion()
{
    if (!IsEditing() || !m_PendingScenePath.empty())
        return;
    auto& editorAPI = GetMutableEditorAPI();
    if (!editorAPI.IsRuntimeSceneReady() || !m_SceneDocument || !m_SceneEditService || !m_SceneSaveService)
        return;
    const Vans::VansSerializedValue root = m_SceneDocument->SerializedRootSnapshot();
    const Vans::VansSerializedValue* sourceEntities = Vans::FindObjectField(root, "entities");
    if (!sourceEntities || sourceEntities->kind != Vans::VansSerializedValue::Kind::Array)
        return;

    const std::uint64_t documentStateId = m_SceneDocument->CurrentStateId();
    if (m_RuntimeMultiMeshExpansionScannedStateId == documentStateId)
        return;

    const std::unordered_set<std::string> parentEntityIds = CollectParentEntityIds(*sourceEntities);
    m_RuntimeMultiMeshExpansionScannedStateId = documentStateId;
    if (!HasRuntimeMultiMeshExpansionCandidates(*sourceEntities, parentEntityIds))
        return;

    Vans::VansSerializedValue newEntities = *sourceEntities;
    std::vector<Vans::VansSerializedValue> pendingChildEntities;
    std::vector<Vans::VansSerializedValue> runtimeEntitiesToRecreate;
    std::vector<std::string> runtimeParentEntityIdsToReplace;
    bool changed = false;
    const auto groups = editorAPI.BuildRuntimeMultiMeshExpansionSnapshot();
    std::unordered_map<std::string, const Vans::EditorAPI::RuntimeMultiMeshGroupSnapshot*> groupsByName;
    groupsByName.reserve(groups.size());
    for (const auto& group : groups)
        groupsByName[group.parentName] = &group;

    for (Vans::VansSerializedValue& entity : newEntities.arrayItems)
    {
        if (entity.kind != Vans::VansSerializedValue::Kind::Object)
            continue;
        const std::string entityId = Vans::ReadSerializedStringField(entity, "id");
        const std::string entityName = Vans::ReadSerializedStringField(entity, "name");
        if (entityId.empty() || entityName.empty())
            continue;
        if (parentEntityIds.find(entityId) != parentEntityIds.end())
            continue;
        if (FindComponent(entity, "MultiMeshRoot") != nullptr)
            continue;

        const Vans::VansSerializedValue* renderer = FindComponent(entity, "ModelRenderer");
        const Vans::VansSerializedValue* transform = FindComponent(entity, "Transform");
        if (!renderer || !Vans::ReadSerializedBoolField(*renderer, "enabled", true))
            continue;

        const Vans::VansSerializedValue* rendererData = Vans::FindObjectField(*renderer, "data");
        if (!rendererData || !Vans::ReadSerializedBoolField(*rendererData, "autoExpandSubmeshes"))
            continue;

        const Vans::VansSerializedValue* model = Vans::FindObjectField(*rendererData, "model");
        const std::string modelGuid =
            model ? Vans::ReadSerializedStringField(*model, "guid") : std::string{};
        if (modelGuid.empty())
            continue;

        auto groupIt = groupsByName.find(entityName);
        if (groupIt == groupsByName.end())
            continue;
        const Vans::EditorAPI::RuntimeMultiMeshGroupSnapshot& group = *groupIt->second;
        if (group.children.empty())
            continue;

        std::vector<Vans::VansSerializedValue> childEntities;
        std::unordered_set<std::string> usedSlotNames;
        for (const auto& childSnapshot : group.children)
        {
            if (childSnapshot.materialGuid.empty())
                continue;

            const std::string& sourceNode = childSnapshot.sourceNode;
            const std::string& sourceMaterial = childSnapshot.sourceMaterial;
            std::string slotBase = (!sourceNode.empty() || !sourceMaterial.empty())
                ? sourceNode + "/" + sourceMaterial
                : "Submesh_" + std::to_string(childSnapshot.submeshIndex);
            if (slotBase == "/")
                slotBase = "Submesh_" + std::to_string(childSnapshot.submeshIndex);
            std::string slotName = slotBase;
            uint32_t slotSuffix = 1;
            while (!usedSlotNames.insert(slotName).second)
                slotName = slotBase + "_" + std::to_string(slotSuffix++);

            const std::string childName = entityName + "_" + SafeAssetName(sourceNode.empty()
                ? "Submesh_" + std::to_string(childSnapshot.submeshIndex)
                : sourceNode) + "_" + std::to_string(childSnapshot.submeshIndex);

            childEntities.push_back(BuildRuntimeExpandedChildEntity(
                entityId,
                childName,
                modelGuid,
                *rendererData,
                childSnapshot,
                slotName));
        }

        if (childEntities.empty())
            continue;

        std::vector<Vans::VansSerializedValue> components;
        if (transform != nullptr)
            components.push_back(*transform);
        else
            components.push_back(DefaultTransformComponent());
        components.push_back(BuildRuntimeExpandedMultiMeshRootComponent(
            modelGuid,
            childEntities.size()));
        Vans::SetSerializedObjectField(entity, "components",
            Vans::VansSerializedValue::Array(std::move(components)));

        runtimeParentEntityIdsToReplace.push_back(entityId);
        runtimeEntitiesToRecreate.push_back(entity);
        for (auto& childEntity : childEntities)
        {
            runtimeEntitiesToRecreate.push_back(childEntity);
            pendingChildEntities.push_back(std::move(childEntity));
        }

        changed = true;
    }

    if (!changed)
        return;

    for (auto& childEntity : pendingChildEntities)
        newEntities.arrayItems.push_back(std::move(childEntity));

    const Vans::SceneEditResult editResult = m_SceneEditService->Set(
        Vans::MakeDocumentPropertyPath(Vans::DocumentPropertySpace::Scene, "/entities"),
        std::move(newEntities));
    if (!editResult)
    {
        VANS_LOG_ERROR("[MultiMeshHierarchy] Failed to update scene document: " << editResult.message);
        return;
    }

    editorAPI.ScanProjectAssets();

    const Vans::SceneSaveResult saveResult = m_SceneSaveService->Save(*m_SceneDocument);
    if (!saveResult)
    {
        VANS_LOG_ERROR("[MultiMeshHierarchy] Failed to save expanded scene: " << saveResult.message);
        return;
    }

    if (RecreateRuntimeMultiMeshExpansionEntities(
        editorAPI,
        runtimeParentEntityIdsToReplace,
        runtimeEntitiesToRecreate))
    {
        VANS_LOG("[MultiMeshHierarchy] Runtime expansion persisted to scene and applied incrementally.");
        return;
    }

    VANS_LOG_WARN("[MultiMeshHierarchy] Runtime expansion persisted to scene but incremental apply failed. Reloading editor scene.");
    ReloadCurrentSceneForEditing();
}

bool VansGraphics::VansEditorWindow::CreateVansEditorWindow(int width, int height, GRAPHICS_API api)
{
    VansConsole::Get().InitializeEventSubscription();

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
    GetMutableEditorAPI().InstallRuntimeVehiclePhysicsStepCallback();

    //创建功能窗口
    CreateWindowComponents();

    return true;
}


// ============================================================================
// 运行控制：OnPlay / OnPause / OnResume / OnStop
// ============================================================================

void VansGraphics::VansEditorWindow::OnPlay()
{
    if (!IsEditing())
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
    m_PendingSceneLoadMode = Vans::EditorAPI::RuntimeSceneLoadMode::Runtime;
    m_PendingScenePath     = m_CurrentLoadedScenePath;
}

void VansGraphics::VansEditorWindow::OnPause()
{
    if (GetMutableEditorAPI().GetPlayState() != Vans::EditorAPI::EnginePlayState::Play)
        return;

    // 冻结逻辑时间
    VansTimer::SetTimePaused(true);

    // 暂停物理（线程仍存活，仅冻结步进）
    GetMutableEditorAPI().PauseRuntimePhysics();

    GetMutableEditorAPI().SetPlayState(Vans::EditorAPI::EnginePlayState::Pause);
    VANS_LOG("[Editor] Scene paused");
}

void VansGraphics::VansEditorWindow::OnResume()
{
    if (GetMutableEditorAPI().GetPlayState() != Vans::EditorAPI::EnginePlayState::Pause)
        return;

    // 恢复逻辑时间
    VansTimer::SetTimePaused(false);

    // 恢复物理步进
    GetMutableEditorAPI().ResumeRuntimePhysics();

    GetMutableEditorAPI().SetPlayState(Vans::EditorAPI::EnginePlayState::Play);
    VANS_LOG("[Editor] Scene resumed");
}

void VansGraphics::VansEditorWindow::OnStop()
{
    if (IsEditing())
        return;

    // 冻结时间，防止重载期间推进
    VansTimer::SetTimePaused(true);

    // 暂停物理（重载时无需模拟）
    GetMutableEditorAPI().PauseRuntimePhysics();

    // 提前切回编辑模式，避免重载期间触发脚本 Update
    GetMutableEditorAPI().SetPlayState(Vans::EditorAPI::EnginePlayState::Edit);

    // Stop = 卸载场景，以 Editor 模式重新加载
    VANS_LOG("[Editor] Stop: reloading scene in Editor mode: " << m_CurrentLoadedScenePath);
    m_PendingSceneLoadMode = Vans::EditorAPI::RuntimeSceneLoadMode::Editor;
    m_PendingScenePath     = m_CurrentLoadedScenePath;
}

void VansGraphics::VansEditorWindow::OpenSelectedAnimationGraph()
{
    if (!m_AnimGraphEditorWindow)
    {
        VANS_LOG_WARN("[AnimationEditor] Animation Graph Editor window is not initialized");
        return;
    }

    const std::string& selectedGuid = Vans::VansEditorSelection::EntityGuid();
    if (selectedGuid.empty())
    {
        VANS_LOG_WARN("[AnimationEditor] Select a scene entity with an Animation component first");
        return;
    }

    auto& editorAPI = GetMutableEditorAPI();
    VansAnimationNode* animNode = editorAPI.FindRuntimeAnimationNodeByEntityGuid(selectedGuid);
    if (!animNode)
    {
        VANS_LOG_WARN("[AnimationEditor] Selected entity has no runtime Animation node: " << selectedGuid);
        return;
    }

    VansAnimationController* controller = animNode->GetController();
    if (!controller)
    {
        VANS_LOG_WARN("[AnimationEditor] Selected Animation node has no controller: " << animNode->GetName());
        return;
    }

    m_AnimGraphEditorWindow->Open(controller, animNode);
}

// ============================================================================
// 工具栏 UI：Play / Pause / Resume / Stop 按钮
// ============================================================================

void VansGraphics::VansEditorWindow::DrawPlayControlToolbar()
{
    // 此函数在 BeginMenuBar() 内被调用，直接向菜单栏追加控件。
    // 三个按钮始终同时显示，根据当前状态决定各自是否可点击。

    auto& editorAPI = GetMutableEditorAPI();
    const bool sceneReady = editorAPI.IsRuntimeSceneReady() && !editorAPI.IsRuntimeSceneSwitching();
	const Vans::EditorAPI::EnginePlayState playState = editorAPI.GetPlayState();

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
    const bool canPlay = sceneReady && (playState == Vans::EditorAPI::EnginePlayState::Edit);
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
    const bool canPause  = sceneReady && (playState == Vans::EditorAPI::EnginePlayState::Play);
    const bool canResume = sceneReady && (playState == Vans::EditorAPI::EnginePlayState::Pause);
    const bool pauseActive = canPause || canResume;
    if (!pauseActive) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button,        ImVec4(0.50f, 0.40f, 0.05f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.70f, 0.55f, 0.08f, 1.00f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive,  ImVec4(0.40f, 0.32f, 0.04f, 1.00f));
    const char* pauseLabel = (playState == Vans::EditorAPI::EnginePlayState::Pause)
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
    const bool canStop = sceneReady && (playState != Vans::EditorAPI::EnginePlayState::Edit);
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

void VansGraphics::VansEditorWindow::DrawBuildMenu()
{
    constexpr Vans::VansGamePackagePlatform selectedPlatform = Vans::VansGamePackagePlatform::Windows;
    static std::string lastPackageStatus;
    static std::string lastPackageOutputPath;
    static bool lastPackageSucceeded = false;

    auto& editorAPI = GetMutableEditorAPI();

    if (!ImGui::BeginMenu("Build"))
        return;

    const std::string projectRootPath = editorAPI.GetProjectRootPath();
    const bool hasProject = !projectRootPath.empty();
    const bool hasScene = !m_CurrentLoadedScenePath.empty();
    const std::string sceneLabel = hasScene
        ? std::filesystem::path(m_CurrentLoadedScenePath).filename().string()
        : std::string("<none>");

    ImGui::Separator();
    ImGui::Text("Platform: %s", Vans::ToString(selectedPlatform));
    ImGui::Text("Scene: %s", sceneLabel.c_str());

    const bool canPackage = hasProject && hasScene;
    if (!canPackage)
        ImGui::BeginDisabled();

    if (ImGui::MenuItem("Package Current Scene"))
    {
        if (m_SceneDocument && m_SceneDocument->IsDirty())
        {
            lastPackageSucceeded = false;
            lastPackageStatus = "Save the current scene before packaging";
            lastPackageOutputPath.clear();
            VANS_LOG_WARN("[Package] " << lastPackageStatus);
        }
        else if (Vans::VansAssetDocumentRegistry::Get().HasDirtyDocuments())
        {
            lastPackageSucceeded = false;
            lastPackageStatus = "Save dirty assets before packaging";
            lastPackageOutputPath.clear();
            VANS_LOG_WARN("[Package] " << lastPackageStatus);
        }
        else
        {
            Vans::VansGamePackageRequest request;
            request.platform = selectedPlatform;
            request.projectRootPath = projectRootPath;
            request.engineRootPath = GetEditorPackageEngineRoot();
            request.scenePath = m_CurrentLoadedScenePath;

            const Vans::VansGamePackageResult result = Vans::VansGamePackageBuilder::Build(request);
            lastPackageSucceeded = result.success;
            lastPackageStatus = result.message;
            lastPackageOutputPath = result.outputPath;
            if (!result)
            {
                VANS_LOG_ERROR("[Package] " << result.message);
                return;
            }
        }
    }

    if (!canPackage)
        ImGui::EndDisabled();

    if (!hasProject)
        ImGui::TextDisabled("Open a project before packaging.");
    else if (!hasScene)
        ImGui::TextDisabled("Load a scene before packaging.");

    if (!lastPackageStatus.empty())
    {
        ImGui::Separator();
        if (lastPackageSucceeded)
            ImGui::Text("Last package: %s", lastPackageStatus.c_str());
        else
            ImGui::TextDisabled("Last package: %s", lastPackageStatus.c_str());
        if (!lastPackageOutputPath.empty())
            ImGui::TextWrapped("%s", lastPackageOutputPath.c_str());
    }

    ImGui::EndMenu();
}

void VansGraphics::VansEditorWindow::CreateWindowComponents()
{
    m_Windows.clear();

    // Create the project selector overlay (shown before a project is loaded)
    m_ProjectSelector = std::make_unique<Vans::VansProjectSelector>();

    m_HierachyWindow = AddEditorWindowComponent<VansHierachuWindow>(m_Windows);

    m_LightWindow = AddEditorWindowComponent<VansLightWindow>(m_Windows);

    m_ProjectWindow = AddEditorWindowComponent<VansProjectWindow>(m_Windows);

    m_ProjectSettingsWindow = AddEditorWindowComponent<VansProjectSettingsWindow>(m_Windows);

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

    m_GIWindow = AddEditorWindowComponent<VansGIWindow>(m_Windows);

    m_PostProcessWindow = AddEditorWindowComponent<VansPostProcessWindow>(m_Windows);

    m_ShadowDebuggerWindow = AddEditorWindowComponent<VansShadowDebuggerWindow>(m_Windows);

    m_PcgWindow = AddEditorWindowComponent<VansPcgWindow>(m_Windows);

    m_HiZCullWindow = AddEditorWindowComponent<VansHiZCullWindow>(m_Windows);

    m_AudioDebugWindow = AddEditorWindowComponent<VansAudioDebugWindow>(m_Windows);

    m_SkeletonDebugWindow = AddEditorWindowComponent<VansSkeletonDebugWindow>(m_Windows);

}

// ============================================================================
// 延迟场景加载处理
// ============================================================================

void VansGraphics::VansEditorWindow::DetachEditorViewportCamerasFromSceneTransforms()
{
    for (auto* camera : m_Cameras)
    {
        if (!camera)
            continue;

        camera->DetachTransformPreservingPose();
        camera->SetRightMouseDown(false);
    }
}

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

    VANS_LOG("[Editor] Loading deferred scene: " << m_PendingScenePath
             << " [mode=" << (m_PendingSceneLoadMode == Vans::EditorAPI::RuntimeSceneLoadMode::Editor ? "Editor" : "Runtime") << "]");

	Vans::SceneDocumentLoadResult pendingDocumentLoad;
	if (m_PendingSceneLoadMode == Vans::EditorAPI::RuntimeSceneLoadMode::Editor)
	{
		pendingDocumentLoad = Vans::VansSceneDocumentLoader::Load(m_PendingScenePath);
		if (!pendingDocumentLoad)
		{
			for (const auto& diagnostic : pendingDocumentLoad.diagnostics)
				VANS_LOG_ERROR("[SceneDocument] " << diagnostic.propertyPointer << " " << diagnostic.message);
			VANS_LOG_ERROR("[Editor] Scene document validation failed before runtime scene switch: " << m_PendingScenePath);
			m_PendingScenePath.clear();
			return;
		}
	}

    auto& editorAPI = GetMutableEditorAPI();
    const Vans::EditorAPI::RuntimeSceneLoadResult sceneLoadResult =
		editorAPI.LoadRuntimeScene(m_PendingScenePath, m_PendingSceneLoadMode);
    if (!sceneLoadResult)
    {
		for (const auto& diagnostic : sceneLoadResult.diagnostics)
			VANS_LOG_ERROR("[Editor] Scene load " << diagnostic.code << ": " << diagnostic.message);
        VANS_LOG_ERROR("[Editor] Runtime scene load request failed: " << m_PendingScenePath);
        m_PendingScenePath.clear();
        return;
    }

    // 记录当前已加载场景路径（用于 Play/Stop 时重载）
    m_CurrentLoadedScenePath = m_PendingScenePath;

    if (m_PendingSceneLoadMode == Vans::EditorAPI::RuntimeSceneLoadMode::Editor)
    {
		m_SceneDocument = std::move(pendingDocumentLoad.document);
		m_SceneEditService = std::make_unique<Vans::VansSceneEditService>(*m_SceneDocument);
		m_RuntimeMultiMeshExpansionScannedStateId = 0;
		VANS_LOG("[SceneDocument] Document ready: " << m_PendingScenePath
			<< " [revision=" << sceneLoadResult.contentRevision << "]");

        // Editor 模式：冻结时间，Scene 视口控制器负责编辑器相机漫游。
        // 保留场景 Camera component 的初始姿态，但不让预览相机继续受场景 Transform 约束。
        DetachEditorViewportCamerasFromSceneTransforms();
        VansTimer::SetTimePaused(true);
        editorAPI.SetPlayState(Vans::EditorAPI::EnginePlayState::Edit);
    }
    else
    {
        // Runtime 模式：解冻时间，启动物理，进入 Playing 状态。
        // Play 模式下相机由脚本接管，Scene 视口控制器会拒绝编辑器漫游输入。
        VansTimer::SetTimePaused(false);
        GetMutableEditorAPI().InstallRuntimeVehiclePhysicsStepCallback();
        GetMutableEditorAPI().StartRuntimePhysicsIfNeeded();
        editorAPI.SetPlayState(Vans::EditorAPI::EnginePlayState::Play);
        VANS_LOG("[Editor] Scene started playing (Runtime mode)");
    }

    // 更新场景管理器当前场景（尽量使用相对路径）
    GetMutableEditorAPI().SetCurrentProjectScenePath(m_PendingScenePath);

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

    VANS_LOG("[Editor] Processing pending project load: " << pending.m_ProjectPath);

    VansTimer::SetTimePaused(true);
    GetMutableEditorAPI().PauseRuntimePhysics();

    if (m_GraphicsDevice)
    {
        m_GraphicsDevice->WaitForIdle();
    }

    auto& editorAPI = GetMutableEditorAPI();
    editorAPI.UnloadRuntimeScene();
    editorAPI.UnloadRuntimeProjectResources();

    editorAPI.CloseProject();
    Vans::VansAssetDocumentEditService::ClearAllHistories();
    Vans::VansAssetDocumentRegistry::Get().Clear();

    m_ProjectLoaded = false;
	m_SceneEditService.reset();
	m_SceneDocument.reset();
    m_CurrentLoadedScenePath.clear();
    m_PendingScenePath.clear();
    m_PendingSceneLoadMode = Vans::EditorAPI::RuntimeSceneLoadMode::Editor;

    Vans::EditorAPI::ProjectOpenRequest projectOpenRequest;
    projectOpenRequest.projectPath = pending.m_ProjectPath;
    projectOpenRequest.projectName = pending.m_ProjectName;
    projectOpenRequest.createNew = pending.m_CreateNew;
    if (pending.m_CreateNew)
    {
        VANS_LOG("[Editor] Creating project '" << pending.m_ProjectName << "' at " << pending.m_ProjectPath);
    }
    else
    {
        VANS_LOG("[Editor] Opening project: " << pending.m_ProjectPath);
    }

    const Vans::EditorAPI::ProjectOpenResult projectOpenResult = editorAPI.OpenProject(projectOpenRequest);
    if (!projectOpenResult.success)
    {
        VANS_LOG_ERROR("[Editor] Pending project load failed: " << pending.m_ProjectPath);
        return;
    }

    ApplyProjectTimeSettings();
    m_ProjectLoaded = true;
    VANS_LOG("[Editor] Project load completed");

    editorAPI.SetupRuntimeScriptProjectVenv(projectOpenResult.projectRootPath);
	Vans::VansEditorSelection::Clear();

    if (!projectOpenResult.defaultScenePath.empty())
    {
        if (!editorAPI.LoadRuntimeProjectAssetsForScene(projectOpenResult.defaultScenePath))
        {
            VANS_LOG_ERROR("[Editor] Scene project asset loading failed; scene load cancelled");
            return;
        }
    }

    if (!projectOpenResult.defaultScenePath.empty())
    {
        const std::string& absScenePath = projectOpenResult.defaultScenePath;
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

void VansGraphics::VansEditorWindow::QueueProjectOpenForAutomation(const std::string& projectPath)
{
    if (projectPath.empty())
        return;

    m_PendingProjectLoad = {};
    m_PendingProjectLoad.m_Requested = true;
    m_PendingProjectLoad.m_CreateNew = false;
    m_PendingProjectLoad.m_ProjectPath = projectPath;
    VANS_LOG("[Editor] Automation queued project open: " << projectPath);
}

void VansGraphics::VansEditorWindow::DrawEditorWindows(VansGraphicsDevice& device)
{
    // Start the Dear ImGui frame
    m_GUIBackEnd->BeginFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // ── Project Selector Overlay ──────────────────────────────────────────
    // When no project is loaded yet, show the full-screen selector instead
    // of the normal editor windows.
    if (!m_ProjectLoaded)
    {
        auto& editorAPI = GetMutableEditorAPI();
        auto result = m_ProjectSelector->Render(editorAPI);

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
		if (device.CanRecordCurrentFrame())
		{
			device.BeginUIRenderPass();
			m_GUIBackEnd->RenderDrawData(device, draw_data);
			device.EndUIRenderPass();
		}
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
		const bool editingMode = IsEditing();
		const bool sceneDocumentReady = editingMode && m_SceneDocument && m_SceneDocument->IsHealthy();
		const bool hasDirtyAssets = Vans::VansAssetDocumentRegistry::Get().HasDirtyDocuments();
		std::shared_ptr<Vans::VansOpenAssetDocument> selectedAssetDocument;
		if (!Vans::VansEditorSelection::AssetPath().empty())
			selectedAssetDocument = Vans::VansAssetDocumentRegistry::Get().Find(Vans::VansEditorSelection::AssetPath());
		const bool selectedAssetDirty = editingMode && selectedAssetDocument && selectedAssetDocument->IsDirty();
		auto& editorAPI = GetMutableEditorAPI();
		editorAPI.BindGlobalRuntime(&device);
		m_EditorAPI = &editorAPI;
		const std::string& selectedEntityGuid = Vans::VansEditorSelection::EntityGuid();
		VansAnimationNode* selectedAnimationNode = selectedEntityGuid.empty()
			? nullptr
			: editorAPI.FindRuntimeAnimationNodeByEntityGuid(selectedEntityGuid);
		const bool canOpenSelectedAnimationGraph =
			selectedAnimationNode && selectedAnimationNode->GetController();
		const bool canUndoSceneDocument = m_SceneEditService && m_SceneEditService->CanUndo();
		const bool canRedoSceneDocument = m_SceneEditService && m_SceneEditService->CanRedo();
		const bool canUndoAssetDocument = selectedAssetDocument &&
			Vans::VansAssetDocumentEditService::CanUndo(selectedAssetDocument->sourceDocument);
		const bool canRedoAssetDocument = selectedAssetDocument &&
			Vans::VansAssetDocumentEditService::CanRedo(selectedAssetDocument->sourceDocument);
		const bool canUndoRuntimeCommand = editorAPI.CanUndo();
		const bool canRedoRuntimeCommand = editorAPI.CanRedo();
		auto applySelectedAssetRuntimePatch = [&]()
		{
			if (!selectedAssetDocument || !selectedAssetDocument->sourceDocument.IsLoaded())
				return;
			editorAPI.ApplyRuntimeMaterialPreviewChange(
				Vans::BuildRuntimeMaterialPreviewChange(
					selectedAssetDocument->sourcePath,
					selectedAssetDocument->sourceDocument.SerializedRootSnapshot()));
		};
		auto applySceneRuntimePatchOrReload = [&](const Vans::SceneEditResult& result)
		{
			if (!result)
				return;
			if (result.runtimeChangeApplied)
				return;
			if (result.runtimeParentPreviewSupported)
			{
				Vans::EditorAPI::RuntimeEntityPreviewChange previewChange;
				previewChange.parentEdits.push_back({
					result.changedEntityGuid,
					result.changedParentEntityGuid });
				if (editorAPI.ApplyRuntimeEntityPreviewChange(previewChange))
					return;
			}
			if (result.runtimePreviewSupported && m_SceneDocument)
			{
				const Vans::EditorAPI::RuntimeEntityPreviewChange previewChange =
					Vans::BuildRuntimeEntityPreviewChangeFromSceneRoot(
						m_SceneDocument->SerializedRootSnapshot(),
						result.changedEntityGuid);
				if (!previewChange.Empty())
				{
					if (editorAPI.ApplyRuntimeEntityPreviewChange(previewChange))
						return;
				}
			}
			ReloadCurrentSceneForEditing();
		};
		auto undoEditorChange = [&]()
		{
			if (canUndoAssetDocument)
			{
				auto result = Vans::VansAssetDocumentEditService::Undo(selectedAssetDocument->sourceDocument);
				if (result)
					applySelectedAssetRuntimePatch();
				else
					VANS_LOG_ERROR("[AssetEdit] " << result.message);
				return;
			}
			if (canUndoSceneDocument)
			{
				auto result = m_SceneEditService->Undo();
				applySceneRuntimePatchOrReload(result);
				return;
			}
			if (editorAPI.CanUndo())
				editorAPI.Undo();
		};
		auto redoEditorChange = [&]()
		{
			if (canRedoAssetDocument)
			{
				auto result = Vans::VansAssetDocumentEditService::Redo(selectedAssetDocument->sourceDocument);
				if (result)
					applySelectedAssetRuntimePatch();
				else
					VANS_LOG_ERROR("[AssetEdit] " << result.message);
				return;
			}
			if (canRedoSceneDocument)
			{
				auto result = m_SceneEditService->Redo();
				applySceneRuntimePatchOrReload(result);
				return;
			}
			if (editorAPI.CanRedo())
				editorAPI.Redo();
		};
		if (editingMode && !io.WantTextInput && io.KeyCtrl)
		{
			if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S, false))
			{
				const Vans::VansAssetSaveResult assetSaveResult =
					Vans::VansEditorAssetSaveService::Get().SaveAllDirtyAssets(editorAPI);
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
			}
			else if ((canUndoAssetDocument || canUndoSceneDocument || canUndoRuntimeCommand) && ImGui::IsKeyPressed(ImGuiKey_Z, false))
			{
				undoEditorChange();
			}
			else if ((canRedoAssetDocument || canRedoSceneDocument || canRedoRuntimeCommand) && ImGui::IsKeyPressed(ImGuiKey_Y, false))
			{
				redoEditorChange();
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
				}
				if (ImGui::MenuItem("Save Asset", nullptr, false, selectedAssetDirty))
				{
					const Vans::VansAssetSaveResult assetSaveResult =
						Vans::VansEditorAssetSaveService::Get().SaveAsset(editorAPI, Vans::VansEditorSelection::AssetPath());
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
						Vans::VansEditorAssetSaveService::Get().SaveAllDirtyAssets(editorAPI);
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
            DrawBuildMenu();
            if (ImGui::BeginMenu("Edit"))
            {
				if (ImGui::MenuItem("Undo", "Ctrl+Z", false,
					editingMode && (canUndoAssetDocument || canUndoSceneDocument || canUndoRuntimeCommand)))
				{
					undoEditorChange();
				}
				if (ImGui::MenuItem("Redo", "Ctrl+Y", false,
					editingMode && (canRedoAssetDocument || canRedoSceneDocument || canRedoRuntimeCommand)))
				{
					redoEditorChange();
				}
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Window"))
            {
                ImGui::MenuItem("Light", nullptr, &m_LightWindowOpen);
                ImGui::MenuItem("Scripts", nullptr, &m_ScriptorWindowOpen);
                ImGui::MenuItem("Console", nullptr, &m_ConsoleWindowOpen);
                ImGui::MenuItem("Profiler", nullptr, &m_ProfilerWindowOpen);
                ImGui::MenuItem("Project Settings", nullptr, &m_ProjectSettingsWindowOpen);
                ImGui::MenuItem("UI Editor", nullptr, &m_UIEditorWindowOpen);
                ImGui::MenuItem("Audio Debug", nullptr, &m_AudioDebugWindowOpen);
                ImGui::Separator();
                if (ImGui::BeginMenu("Animation"))
                {
                    if (ImGui::MenuItem("Animation Graph", nullptr, false, canOpenSelectedAnimationGraph))
                    {
                        OpenSelectedAnimationGraph();
                    }
                    ImGui::MenuItem("Skeleton Debug", nullptr, &m_SkeletonDebugWindowOpen);
                    if (!canOpenSelectedAnimationGraph)
                    {
                        ImGui::TextDisabled("Select an entity with Animation");
                    }
                    ImGui::EndMenu();
                }
                ImGui::Separator();
                ImGui::MenuItem("GBuffer Visualization", nullptr, &m_GBufferWindowOpen);
                ImGui::MenuItem("Shadow Debugger", nullptr, &m_ShadowDebuggerWindowOpen);
                ImGui::MenuItem("Water GBuffer Visualization", nullptr, &m_WaterGBufferWindowOpen);
                ImGui::MenuItem("Render Debug", nullptr, &m_RenderDebugWindowOpen);
                ImGui::MenuItem("HiZ Occlusion Culling", nullptr, &m_HiZCullWindowOpen);
                ImGui::MenuItem("Hair Debug", nullptr, &m_HairDebugWindowOpen);
                ImGui::MenuItem("Water", nullptr, &m_WaterWindowOpen);
                ImGui::MenuItem("Terrain", nullptr, &m_TerrainWindowOpen);
                ImGui::MenuItem("PCG", nullptr, &m_PcgWindowOpen);
                ImGui::MenuItem("Post Process", nullptr, &m_PostProcessWindowOpen);
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
                if (m_GIWindow)
                {
                    ImGui::MenuItem("GI Inspector", nullptr, &m_GIWindowOpen);
                }
                else
                {
                    ImGui::BeginDisabled();
                    ImGui::MenuItem("GI Inspector");
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
            window->ShowWindow(editorAPI);
        }

		for (const Vans::EditorAPI::ScenePropertyEdit& edit : editorAPI.ConsumeScenePropertyEdits())
		{
			if (!editingMode || !m_SceneEditService)
				continue;
			const Vans::SceneEditResult result = m_SceneEditService->Set(
				Vans::MakeDocumentPropertyPath(Vans::DocumentPropertySpace::Scene, edit.propertyPointer),
				Vans::ToSerializedValue(edit.value));
			if (!result && result.message != "Scene property is unchanged")
				VANS_LOG_ERROR("[SceneSettings] " << result.message);
		}

        ImGui::End();
    }



    //GUI handle rendeing
    ImGui::Render();

    ImDrawData* draw_data = ImGui::GetDrawData();

    // ImGui 编辑器覆盖层渲染到 swapchain
	if (device.CanRecordCurrentFrame())
	{
		device.BeginUIRenderPass();
		m_GUIBackEnd->RenderDrawData(device, draw_data);
		device.EndUIRenderPass();
	}
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
    m_GraphicsDevice->InitializeGpuProfiler();
#endif

    //初始化脚本环境
    auto& startupEditorAPI = GetMutableEditorAPI();
	startupEditorAPI.BindGlobalRuntime(m_GraphicsDevice);
	startupEditorAPI.InitializeRuntimeScripts();
	Vans::VansEditorShaderHotReloadController shaderHotReloadController;
	shaderHotReloadController.Initialize(startupEditorAPI);

    // Main loop
    while (!glfwWindowShouldClose(m_VansEditorWindow.m_VansGraphicsHandle))
    { 
        VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);

        // 项目选择界面阶段没有完整场景帧；Profiler 窗口关闭时只保留轻量帧计数，不采集 scope/GPU timestamp。
        const bool profilerFrameActive = m_ProjectLoaded;
#if VANS_PROFILER_ENABLED
        Vans::VansProfiler::Get().SetCaptureEnabled(profilerFrameActive && VansEditorWindow::m_ProfilerWindowOpen);
#endif
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
            Vans::VansInputManager::Get().RefreshPolledState();
        }
        Vans::VansEventBus::Get().Flush(Vans::VansEventLane::Input);

        // Resize swap chain?
        if (m_VansEditorWindow.m_WindowStatus.swapChainRebuild)
        {
            int width, height;
            glfwGetFramebufferSize(m_VansEditorWindow.m_VansGraphicsHandle, &width, &height);
            if (width > 0 && height > 0)
            {
                m_GraphicsDevice->OnWindowResize(static_cast<uint32_t>(width), static_cast<uint32_t>(height));

                // NOTE: internal render resolution is unchanged, so camera aspect ratio
                // and all SSGI/SSR/GBuffer render targets are unaffected.

                m_VansEditorWindow.m_WindowStatus.swapChainRebuild = false;
            }
            else
            {
                // Window minimized — skip rendering this frame
                if (profilerFrameActive)
                    m_GraphicsDevice->EndGpuProfilerFrame();
                continue;
            }
        }

        {
            VANS_PROFILE_SCOPE("JobSystem::ProcessMainThreadJobs", Vans::ProfileCategory::JobSystem);
            Vans::VansJobSystem::Get().ProcessMainThreadJobs();
        }
        Vans::VansEventBus::Get().Flush(Vans::VansEventLane::MainThread);

        //更新时间
        {
            VANS_PROFILE_SCOPE("Frame::TimerUpdate", Vans::ProfileCategory::Frame);
            VansGraphics::VansTimer::Update();
        }

        auto& editorAPI = GetMutableEditorAPI();
        editorAPI.BindGlobalRuntime(m_GraphicsDevice);

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
        Vans::VansRuntimeGameplayFrame gameplayFrame;
        gameplayFrame.sceneReady = editorAPI.IsRuntimeSceneReady();
        gameplayFrame.simulationRunning =
            gameplayFrame.sceneReady && editorAPI.IsRuntimePhysicsRunning();
        gameplayFrame.gameplayActive =
            gameplayFrame.sceneReady &&
            editorAPI.GetPlayState() == Vans::EditorAPI::EnginePlayState::Play;
        gameplayFrame.syncPhysicsTransforms = [&editorAPI]
        {
            // Uses PxSceneReadLock internally to synchronize with async simulation.
            VANS_PROFILE_SCOPE("Physics::SyncRigidBodies", Vans::ProfileCategory::Physics);
            editorAPI.SyncRuntimePhysicsTransforms();
        };
        gameplayFrame.updateNonCameraScripts = [&editorAPI]
        {
            VANS_PROFILE_SCOPE("Script::Update", Vans::ProfileCategory::Script);
            editorAPI.UpdateRuntimeNonCameraScripts();
        };
        gameplayFrame.flushCharacterControllerTransforms = [&editorAPI]
        {
            VANS_PROFILE_SCOPE("Physics::FlushCharacterController", Vans::ProfileCategory::Physics);
            editorAPI.FlushRuntimeCharacterControllerTransforms();
        };
        gameplayFrame.updateCameraScripts = [&editorAPI]
        {
            VANS_PROFILE_SCOPE("Script::UpdateCameraScripts", Vans::ProfileCategory::Script);
            editorAPI.UpdateRuntimeCameraScripts();
        };
        Vans::VansRuntimeFrameScheduler::RunGameplay(gameplayFrame);

        // ── Deferred resource & scene loading ───────────────────────────
        // Process pending loads BEFORE command buffer recording.

        // 0) Project load/reload orchestration.  This must run before resource/scene load.
        {
            VANS_PROFILE_SCOPE("Project::ProcessPendingProjectLoad", Vans::ProfileCategory::IO);
            ProcessPendingProjectLoad();
        }

        // Load Scene content after the AssetDatabase dependency closure.
        {
            VANS_PROFILE_SCOPE("Resource::ProcessPendingSceneLoad", Vans::ProfileCategory::IO);
            ProcessPendingSceneLoad();
        }
        {
            VANS_PROFILE_SCOPE("Editor::ProcessRuntimeMultiMeshExpansion", Vans::ProfileCategory::IO);
            ProcessRuntimeMultiMeshHierarchyExpansion();
        }
        Vans::VansEventBus::Get().Flush(Vans::VansEventLane::Editor);
        {
            VANS_PROFILE_SCOPE("Editor::ShaderHotReload", Vans::ProfileCategory::IO);
            shaderHotReloadController.TickAndApply(editorAPI);
        }
        Vans::VansEventBus::Get().Flush(Vans::VansEventLane::Diagnostics);
        Vans::VansEventBus::Get().Flush(Vans::VansEventLane::RenderPrep);
        // Rendering, 这里会结束renderpass
        {
            VANS_PROFILE_SCOPE("Render::CameraRendering", Vans::ProfileCategory::CommandRecord);
            camera.Rendering();
        }
        //UI Pass
        m_SceneWindow->RegistCamera(&camera);
        {
            VANS_PROFILE_SCOPE("Editor::DrawWindows", Vans::ProfileCategory::Editor);
            DrawEditorWindows(*m_GraphicsDevice);
        }

        //结束录制
        {
            VANS_PROFILE_SCOPE("Vulkan::Present", Vans::ProfileCategory::VulkanSubmit);
            camera.Present();
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
            if (profilerFrameActive)
                m_GraphicsDevice->EndGpuProfilerFrame();
        }

    }

    m_Cameras.clear();

}

void VansGraphics::VansEditorWindow::DestroyVansEditorWindow()
{
    VansConsole::Get().ShutdownEventSubscription();

    m_ProjectSelector.reset();
    m_Windows.clear();

    m_HierachyWindow = nullptr;
    m_LightWindow = nullptr;
    m_ProjectWindow = nullptr;
    m_ProjectSettingsWindow = nullptr;
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
    m_GIWindow = nullptr;
    m_PostProcessWindow = nullptr;
    m_ShadowDebuggerWindow = nullptr;
    m_PcgWindow = nullptr;
    m_HiZCullWindow = nullptr;
    m_AudioDebugWindow = nullptr;
    m_SkeletonDebugWindow = nullptr;

    // Destroy GPU profiler
#if VANS_PROFILER_ENABLED
    Vans::VansGpuProfiler::Get().Destroy();
#endif

    // Unregister Physics Callback on shutdown to avoid calling into destroyed objects
    GetMutableEditorAPI().ClearRuntimePhysicsStepCallback();

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

