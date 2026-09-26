#include "VansEditorWindow.h"
#include "VansEditorConfiguration.h"
#include "VansEditorAuthoringCommandController.h"
#include "VansEditorTheme.h"
#include "VansEditorShellCommandController.h"
#include "VansEditorShellMenu.h"
#include "VansEditorPlayCommandController.h"
#include "VansEditorPlayToolbar.h"
#include "VansEditorProjectSwitchController.h"
#include "VansEditorSceneLoadController.h"
#include "VansPrefabEditService.h"
#include "IVansEditorAPIHost.h"
#include "../RenderCore/VansCamera.h"
#include "../RenderCore/VansRenderSystem.h"
#include "../RuntimeUI/Public/VansUISystem.h"
#include "../VansTimer.h"
#include "../EngineAPILayer/Public/IEngineEditorAPI.h"
#include "../EngineAPILayer/Public/IAnimationEditorAPI.h"
#include "../EngineAPILayer/Public/IAssetAuthoringEditorAPI.h"
#include "../EngineAPILayer/Public/IPlayModeEditorAPI.h"
#include "../EngineAPILayer/Public/IProjectEditorAPI.h"
#include "../EngineAPILayer/Public/IRuntimeFrameEditorAPI.h"
#include "../EngineAPILayer/Public/IRuntimePhysicsEditorAPI.h"
#include "../EngineAPILayer/Public/IRuntimeSceneEditorAPI.h"
#include "../EngineAPILayer/Public/ISceneInteractionEditorAPI.h"
#include "../EngineAPILayer/Public/ISceneSettingsEditorAPI.h"
#include "../EngineAPILayer/Public/IScriptLifecycleEditorAPI.h"
#include "../AuthoringCore/VansAssetDocumentEditService.h"
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
#include "Windows/VansSceneAnimationPreviewWindow.h"
#include "Windows/VansBoneMaskEditorWindow.h"
#include "Windows/VansTimelineEditorWindow.h"
#include "Windows/VansGameplayActionEditorWindow.h"
#include "Windows/VansGAFDebuggerWindow.h"
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
#include "Windows/VansParticleDebugWindow.h"
#include "Windows/VansMotionMatchingDebugWindow.h"
#include "Windows/VansAIDebugWindow.h"

#include "../Util/VansProfiler.h"
#include "../Util/VansJobSystem.h"
#include "../EventCore/VansEventBus.h"
#include "../Util/VansInputManager.h"
#include "../Util/VansLog.h"
#include "../RuntimeCore/VansFramePhase.h"
#include "../RuntimeCore/VansRuntimeFrameScheduler.h"

#include "../AssetCore/VansAssetGuid.h"
#include "../AssetCore/VansAssetDatabase.h"
#include "../GameplayActionSchema/VansGameplayAssetSchema.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../PackagingCore/VansGamePackageBuilder.h"
#include "Windows/VansProjectSelector.h"
#include "../SceneCore/VansSceneDocumentLoader.h"
#include "../SceneCore/VansSceneParentReference.h"
#include "../AuthoringCore/VansAssetDocumentRegistry.h"
#include "VansEditorAssetSaveService.h"
#include "VansEditorHistoryAdapter.h"
#include "VansSceneEditService.h"
#include "VansEditorSelectionService.h"
#include "ShaderHotReload/VansEditorShaderHotReloadController.h"

#include "imgui.h"
#include "backends/imgui_impl_glfw.h"
#include "ImGuizmo.h"

#include <iostream>
#include <cstdint>
#include <initializer_list>
#include <typeinfo>
#include <string>
#include <stdexcept>
#include <filesystem>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <nlohmann/json.hpp>

namespace
{
    Vans::EditorAPI::IEngineEditorAPI& GetMutableEditorAPI();

	class VansEditorRuntimeFramePort final :
		public Vans::IVansRuntimeFramePort,
		public Vans::IVansRuntimeFramePreviewPort
	{
	  public:
		explicit VansEditorRuntimeFramePort(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
			: m_RuntimeFrameAPI(editorAPI), m_RuntimePhysicsAPI(editorAPI)
		{
		}

		void SyncPhysicsTransforms(const Vans::VansRuntimeFrameContext&) override
		{
			VANS_PROFILE_SCOPE("Physics::SyncRigidBodies", Vans::ProfileCategory::Physics);
			m_RuntimePhysicsAPI.SyncRuntimePhysicsTransforms();
		}

		void UpdateNonCameraScripts(const Vans::VansRuntimeFrameContext&) override
		{
			VANS_PROFILE_SCOPE("Script::Update", Vans::ProfileCategory::Script);
			m_RuntimeFrameAPI.UpdateRuntimeNonCameraScripts();
		}

		void AdvanceCameraRuntime(const Vans::VansRuntimeFrameContext& context) override
		{
			m_RuntimeFrameAPI.AdvanceCameraRuntime(context.m_DeltaSeconds);
		}

		void UpdateActionsEarly(const Vans::VansRuntimeFrameContext& context) override
		{
			VANS_PROFILE_SCOPE("GameplayAction::TickEarly", Vans::ProfileCategory::Script);
			m_RuntimeFrameAPI.UpdateRuntimeActionsEarly(context.m_DeltaSeconds);
		}

		void UpdateAI(const Vans::VansRuntimeFrameContext& context) override
		{
			VANS_PROFILE_SCOPE("AI::Update", Vans::ProfileCategory::Script);
			m_RuntimeFrameAPI.UpdateRuntimeAI(context.m_DeltaSeconds);
		}

		void PrepareCharacterLocomotion(const Vans::VansRuntimeFrameContext& context) override
		{
			m_RuntimePhysicsAPI.PrepareRuntimeCharacterLocomotion(context.m_DeltaSeconds);
		}

		void FlushCharacterControllerTransforms(const Vans::VansRuntimeFrameContext&) override
		{
			VANS_PROFILE_SCOPE("Physics::FlushCharacterController", Vans::ProfileCategory::Physics);
			m_RuntimePhysicsAPI.FlushRuntimeCharacterControllerTransforms();
		}

		void UpdateTimelinesPostScript(const Vans::VansRuntimeFrameContext& context) override
		{
			VANS_PROFILE_SCOPE("Timeline::PostScript", Vans::ProfileCategory::Script);
			m_RuntimeFrameAPI.UpdateRuntimeTimelinesPostScript(context.m_DeltaSeconds);
		}

		void RunActionLateContinuation(const Vans::VansRuntimeFrameContext&) override
		{
			m_RuntimeFrameAPI.RunRuntimeActionLateContinuation();
		}

		void BeginCameraControlFrame(const Vans::VansRuntimeFrameContext&) override
		{
			m_RuntimeFrameAPI.BeginRuntimeCameraControlFrame();
		}

		void UpdateCameraScripts(const Vans::VansRuntimeFrameContext&) override
		{
			VANS_PROFILE_SCOPE("Script::UpdateCameraScripts", Vans::ProfileCategory::Script);
			m_RuntimeFrameAPI.UpdateRuntimeCameraScripts();
		}

		void CaptureCameraControlBase(const Vans::VansRuntimeFrameContext&) override
		{
			m_RuntimeFrameAPI.CaptureRuntimeCameraControlBase();
		}

		void UpdateTimelinesCamera(const Vans::VansRuntimeFrameContext& context) override
		{
			VANS_PROFILE_SCOPE("Timeline::Camera", Vans::ProfileCategory::Script);
			m_RuntimeFrameAPI.UpdateRuntimeTimelinesCamera(context.m_DeltaSeconds);
		}

		void ResolveCameraControlFrame(const Vans::VansRuntimeFrameContext&) override
		{
			m_RuntimeFrameAPI.ResolveRuntimeCameraControlFrame();
		}

		void UpdatePostScriptControllers(const Vans::VansRuntimeFrameContext& context) override
		{
			m_RuntimeFrameAPI.UpdateTimelinePreviewsPostScript(context.m_DeltaSeconds);
		}

		void UpdateCameraControllers(const Vans::VansRuntimeFrameContext& context) override
		{
			m_RuntimeFrameAPI.UpdateTimelinePreviewsCamera(context.m_DeltaSeconds);
		}

	  private:
		Vans::EditorAPI::IRuntimeFrameEditorAPI& m_RuntimeFrameAPI;
		Vans::EditorAPI::IRuntimePhysicsEditorAPI& m_RuntimePhysicsAPI;
	};

    Vans::EditorAPI::RuntimeSceneDocumentSnapshot BuildRuntimeSceneDocumentSnapshot(
        const Vans::VansSceneDocument& document)
    {
        Vans::EditorAPI::RuntimeSceneDocumentSnapshot snapshot;
        snapshot.sourcePath = document.SourcePath().string();
        snapshot.canonicalJson =
            Vans::EncodeSerializedValueJson<nlohmann::ordered_json>(
                document.SerializedRootSnapshot()).dump();
        snapshot.authoringStateId = document.CurrentStateId();
        return snapshot;
    }

	bool PublishOpenAssetWorkingCopy(
		Vans::EditorAPI::IAssetAuthoringEditorAPI& assetAuthoringAPI,
		const Vans::VansOpenAssetDocument& document,
		std::string& error)
	{
		Vans::EditorAPI::AssetWorkingCopyPublishRequest request;
		request.sourcePath = document.sourcePath.string();
		request.sourceLoaded = document.sourceDocument.IsLoaded();
		if (request.sourceLoaded)
		{
			request.sourceCanonicalJson =
				Vans::EncodeSerializedValueJson<nlohmann::ordered_json>(
					document.sourceDocument.SerializedRootSnapshot()).dump();
		}
		request.metaLoaded = document.metaDocument.IsLoaded();
		if (request.metaLoaded)
		{
			request.metaCanonicalJson =
				Vans::EncodeSerializedValueJson<nlohmann::ordered_json>(
					document.metaDocument.SerializedRootSnapshot()).dump();
		}

		const Vans::EditorAPI::AssetWorkingCopyPublishResult result =
			assetAuthoringAPI.PublishAssetWorkingCopy(request);
		error = result.message;
		return result.success;
	}

    bool ReadAutomationBoolEnv(const char* name)
    {
        const char* value = std::getenv(name);
        if (value == nullptr)
            return false;
        std::string normalized(value);
        std::transform(normalized.begin(), normalized.end(), normalized.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return normalized.empty() ||
            (normalized != "0" && normalized != "false" && normalized != "off" && normalized != "no");
    }

    Vans::EditorAPI::IEngineEditorAPI& GetMutableEditorAPI()
    {
        auto* editorAPI = VansGraphics::VansEditorWindow::GetEditorAPI();
        if (!editorAPI)
            throw std::logic_error("Editor API host is not attached");
        return *editorAPI;
    }

    std::string GetEditorPackageEngineRoot()
    {
#ifdef FOREST_ENGINE_SOURCE_ROOT
        return FOREST_ENGINE_SOURCE_ROOT;
#else
		return Vans::VansProjectManager::Get().GetPathResolver().GetEngineRoot();
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
			const Vans::VansSerializedValue* parentValue = Vans::FindObjectField(entity, "parent");
			const std::string parentId = parentValue
				? Vans::ReadSceneParentEntityGuid(*parentValue) : std::string{};
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

	bool EnsureRuntimeGeneratedMaterialWorkingCopy(
		Vans::EditorAPI::IAssetAuthoringEditorAPI& assetAuthoringAPI,
		const Vans::EditorAPI::RuntimeMultiMeshChildSnapshot& child)
	{
		if (!child.materialRequiresSave)
			return true;
		const nlohmann::ordered_json sourceJson = nlohmann::ordered_json::parse(
			child.materialSourceCanonicalJson, nullptr, false);
		const nlohmann::ordered_json metaJson = nlohmann::ordered_json::parse(
			child.materialMetaCanonicalJson, nullptr, false);
		if (sourceJson.is_discarded() || !sourceJson.is_object() ||
			metaJson.is_discarded() || !metaJson.is_object())
		{
			VANS_LOG_ERROR("[MultiMeshHierarchy] Generated material memory document is invalid");
			return false;
		}

		std::string documentError;
		auto document = Vans::VansAssetDocumentRegistry::Get().CreateInMemory(
			child.materialSourcePath,
			Vans::DecodeSerializedValueJson(sourceJson),
			Vans::DecodeSerializedValueJson(metaJson),
			true,
			documentError);
		if (!document)
		{
			VANS_LOG_ERROR("[MultiMeshHierarchy] Cannot create generated material document: "
				<< documentError);
			return false;
		}

		const Vans::EditorAPI::AssetWorkingCopyPublishResult publication =
			assetAuthoringAPI.PublishAssetWorkingCopy({
				child.materialSourcePath,
				true,
				child.materialSourceCanonicalJson,
				true,
				child.materialMetaCanonicalJson });
		if (!publication.success)
		{
			document->lastError = publication.message;
			VANS_LOG_ERROR("[MultiMeshHierarchy] Cannot publish generated material memory object: "
				<< publication.message);
			return false;
		}
		document->lastError.clear();
		return true;
	}

    bool RecreateRuntimeMultiMeshExpansionEntities(
        Vans::EditorAPI::IRuntimeSceneEditorAPI& runtimeSceneAPI,
        const std::vector<std::string>& parentEntityIds,
        const std::vector<Vans::VansSerializedValue>& runtimeEntities)
    {
        if (parentEntityIds.empty() || runtimeEntities.empty())
            return false;

        for (const std::string& parentEntityId : parentEntityIds)
        {
            Vans::EditorAPI::RuntimeEntityDestroyRequest destroyRequest;
            destroyRequest.entityGuid = parentEntityId;
            if (!runtimeSceneAPI.DestroyRuntimeEntity(destroyRequest).destroyed)
            {
                VANS_LOG_WARN("[MultiMeshHierarchy] Runtime destroy failed for expanded parent '"
                    << parentEntityId << "'");
                return false;
            }
        }

        Vans::EditorAPI::RuntimeSceneEntitiesCreateRequest createRequest;
        createRequest.sceneEntities.reserve(runtimeEntities.size());
        for (const Vans::VansSerializedValue& entity : runtimeEntities)
            createRequest.sceneEntities.push_back(entity);

        const Vans::EditorAPI::RuntimeSceneEntitiesCreateResult createResult =
            runtimeSceneAPI.CreateRuntimeSceneEntities(createRequest);
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
        // RenderCore owns Vulkan loader selection and capability validation.
        // Querying through GLFW here would load the system Vulkan loader before
        // the optional Streamline interposer can become the single dispatch source.
        return true;
    case VansGraphics::INVALIDE:
    default:
        return false;
        break;
    }
}

VansGraphics::VansEditorWindowCatalog VansGraphics::VansEditorWindow::m_WindowCatalog;
VansGraphics::VansEditorPackageSession VansGraphics::VansEditorWindow::m_PackageSession;
VansGraphics::VansEditorPrefabSession VansGraphics::VansEditorWindow::m_PrefabSession;
std::unique_ptr<VansGraphics::VansEditorConfiguration>
	VansGraphics::VansEditorWindow::m_EditorConfiguration;

bool VansGraphics::VansEditorWindow::IsWindowOpen(VansEditorWindowId id)
{
	return m_WindowCatalog.IsOpen(id);
}

bool* VansGraphics::VansEditorWindow::WindowOpenState(VansEditorWindowId id)
{
	return m_WindowCatalog.OpenState(id);
}

bool VansGraphics::VansEditorWindow::DrawSceneAnimationPreviewViewportHandle(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI,
	VansCamera* camera,
	const ImVec2& viewportOrigin,
	const ImVec2& viewportSize)
{
	auto* previewWindow = Window<VansSceneAnimationPreviewWindow>();
	return previewWindow && previewWindow->DrawSceneViewportHandle(
		editorAPI, camera, viewportOrigin, viewportSize);
}

void VansGraphics::VansEditorWindow::DrawParticleDebugSceneOverlay(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI,
	const glm::mat4& viewProjection,
	const ImVec2& origin,
	const ImVec2& size)
{
	if (auto* particleWindow = Window<VansParticleDebugWindow>())
		particleWindow->DrawSceneOverlay(editorAPI, viewProjection, origin, size);
}

void VansGraphics::VansEditorWindow::RequestSceneLoad(const std::string& scenePath)
{
	m_SceneLoadSession.Request(scenePath);
}

VansGraphics::VansBasicWindow VansGraphics::VansEditorWindow::m_VansEditorWindow;
VansGraphics::VansEditorDebugViewState VansGraphics::VansEditorWindow::m_DebugViewState;
//支持多个相机
std::vector<VansGraphics::VansCamera*> VansGraphics::VansEditorWindow::m_Cameras;

VansGraphics::VansEditorWindowRegistry VansGraphics::VansEditorWindow::m_WindowRegistry;

Vans::IVansEditorAPIHost* VansGraphics::VansEditorWindow::m_EditorAPIHost = nullptr;
std::uint64_t VansGraphics::VansEditorWindow::m_RuntimeMultiMeshExpansionScannedStateId = 0;
VansGraphics::VansEditorProjectSession VansGraphics::VansEditorWindow::m_ProjectSession;
VansGraphics::VansEditorSceneDocumentSession VansGraphics::VansEditorWindow::m_SceneDocumentSession;
VansGraphics::VansEditorSceneLoadSession VansGraphics::VansEditorWindow::m_SceneLoadSession;

VansGraphics::VansBasicWindow& VansGraphics::VansEditorWindow::NativeWindow()
{
	return m_VansEditorWindow;
}

void VansGraphics::VansEditorWindow::EnableSkeletonDebugForAutomation()
{
	*WindowOpenState(VansEditorWindowId::SkeletonDebug) = true;
	m_DebugViewState.skeletonDebugGizmos = true;
	m_DebugViewState.skeletonDebugShowRetargetSource = true;
}

Vans::VansSceneDocument* VansGraphics::VansEditorWindow::GetSceneDocument()
{
    return m_SceneDocumentSession.Document();
}

Vans::VansSceneEditService* VansGraphics::VansEditorWindow::GetSceneEditService()
{
    return m_SceneDocumentSession.EditService();
}

Vans::EditorAPI::IEngineEditorAPI* VansGraphics::VansEditorWindow::GetEditorAPI()
{
    if (!m_EditorAPIHost)
        return nullptr;
    return &m_EditorAPIHost->AccessEditorAPI(
		m_SceneDocumentSession.Document(),
		m_SceneDocumentSession.EditService());
}

void VansGraphics::VansEditorWindow::AttachEditorAPIHost(Vans::IVansEditorAPIHost& host)
{
    m_EditorAPIHost = &host;
}

void VansGraphics::VansEditorWindow::DetachEditorAPIHost(Vans::IVansEditorAPIHost& host)
{
    if (m_EditorAPIHost == &host)
        m_EditorAPIHost = nullptr;
}

bool VansGraphics::VansEditorWindow::IsEditing()
{
	auto& playModeAPI = static_cast<Vans::EditorAPI::IPlayModeEditorAPI&>(GetMutableEditorAPI());
	return playModeAPI.GetPlayState() == Vans::EditorAPI::EnginePlayState::Edit;
}

void VansGraphics::VansEditorWindow::ReloadCurrentSceneForEditing()
{
    if (HasPrefabSession()) { RefreshActiveScenePreview(); return; }
	if (!IsEditing() || m_SceneLoadSession.CurrentScenePath().empty())
		return;
	m_SceneLoadSession.Request(
		Vans::EditorAPI::RuntimeSceneLoadMode::Editor,
		m_SceneLoadSession.CurrentScenePath());
}

void VansGraphics::VansEditorWindow::ProcessRuntimeMultiMeshHierarchyExpansion()
{
    if (!IsEditing() || m_SceneLoadSession.HasPendingRequest())
        return;
    auto& editorAPI = GetMutableEditorAPI();
	Vans::EditorAPI::IRuntimeSceneEditorAPI& runtimeSceneAPI = editorAPI;
	Vans::EditorAPI::ISceneInteractionEditorAPI& sceneInteractionAPI = editorAPI;
    if (!runtimeSceneAPI.IsRuntimeSceneReady() || !GetSceneDocument() || !GetSceneEditService())
        return;
    const std::uint64_t documentStateId = GetSceneDocument()->CurrentStateId();
    if (m_RuntimeMultiMeshExpansionScannedStateId == documentStateId)
        return;

    const auto snapshot = GetSceneDocument()->CreateSnapshot();
    const Vans::VansSerializedValue* sourceEntities = Vans::FindObjectField(snapshot.Root(), "entities");
    if (!sourceEntities || sourceEntities->kind != Vans::VansSerializedValue::Kind::Array)
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
    const auto groups = sceneInteractionAPI.BuildRuntimeMultiMeshExpansionSnapshot();
    std::unordered_map<std::string, const Vans::EditorAPI::RuntimeMultiMeshGroupSnapshot*> groupsByEntity;
    groupsByEntity.reserve(groups.size());
    for (const auto& group : groups)
        groupsByEntity[group.parentEntityGuid] = &group;

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

        auto groupIt = groupsByEntity.find(entityId);
        if (groupIt == groupsByEntity.end())
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
			if (!EnsureRuntimeGeneratedMaterialWorkingCopy(editorAPI, childSnapshot))
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

    const Vans::SceneEditResult editResult = GetSceneEditService()->Set(
        Vans::MakeDocumentPropertyPath(Vans::DocumentPropertySpace::Scene, "/entities"),
        std::move(newEntities));
    if (!editResult)
    {
        VANS_LOG_ERROR("[MultiMeshHierarchy] Failed to update scene document: " << editResult.message);
        return;
    }

    if (RecreateRuntimeMultiMeshExpansionEntities(
        runtimeSceneAPI,
        runtimeParentEntityIdsToReplace,
        runtimeEntitiesToRecreate))
    {
        VANS_LOG("[MultiMeshHierarchy] Runtime expansion applied to the in-memory scene document and runtime entities.");
        return;
    }

    VANS_LOG_WARN("[MultiMeshHierarchy] Incremental runtime apply failed. Rebuilding the editor scene from its in-memory document.");
    ReloadCurrentSceneForEditing();
}

bool VansGraphics::VansEditorWindow::CreateVansEditorWindow(int width, int height, GRAPHICS_API api)
{
	m_EditorConfiguration.reset();
	auto configuration = std::make_unique<VansEditorConfiguration>();
	std::string configurationError;
	const std::filesystem::path configurationPath = VansEditorConfiguration::ResolveBuiltInPath();
	if (configurationPath.empty() ||
		!VansEditorConfiguration::Load(configurationPath, *configuration, configurationError))
	{
		VANS_LOG_ERROR("[EditorConfiguration] " <<
			(configurationError.empty() ? "Unable to resolve built-in configuration path" : configurationError));
		return false;
	}
	m_WindowCatalog.ApplyDefaults(configuration->windowDefaults);
	m_EditorConfiguration = std::move(configuration);

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
    static_cast<Vans::EditorAPI::IRuntimePhysicsEditorAPI&>(GetMutableEditorAPI())
        .InstallRuntimeVehiclePhysicsStepCallback();

    //创建功能窗口
    CreateWindowComponents();

    return true;
}


void VansGraphics::VansEditorWindow::ExecutePlayCommand(VansEditorPlayCommand command)
{
	auto& editorAPI = GetMutableEditorAPI();
	Vans::EditorAPI::IPlayModeEditorAPI& playModeAPI = editorAPI;
	Vans::EditorAPI::IRuntimePhysicsEditorAPI& runtimePhysicsAPI = editorAPI;
	VansEditorPlayCommandContext context;
	context.playState = playModeAPI.GetPlayState();
	context.hasPrefabSession = HasPrefabSession();
	context.sceneDirty = GetSceneDocument() && GetSceneDocument()->IsDirty();
	context.currentScenePath = m_SceneLoadSession.CurrentScenePath();

	VansEditorPlayCommandOperations operations;
	operations.setTimePaused = [](bool paused) { VansTimer::SetTimePaused(paused); };
	operations.pauseRuntimePhysics = [&runtimePhysicsAPI] { runtimePhysicsAPI.PauseRuntimePhysics(); };
	operations.resumeRuntimePhysics = [&runtimePhysicsAPI] { runtimePhysicsAPI.ResumeRuntimePhysics(); };
	operations.setPlayState = [&playModeAPI](Vans::EditorAPI::EnginePlayState state)
	{
		playModeAPI.SetPlayState(state);
	};
	operations.requestSceneLoad = [](Vans::EditorAPI::RuntimeSceneLoadMode mode,
		const std::string& scenePath)
	{
		m_SceneLoadSession.Request(mode, scenePath);
	};
	operations.logInfo = [](const std::string& message) { VANS_LOG(message); };
	operations.logWarning = [](const std::string& message) { VANS_LOG_WARN(message); };

	VansEditorPlayCommandController::Execute(command, context, operations);
}

void VansGraphics::VansEditorWindow::OpenSelectedAnimationGraph()
{
	auto* animationGraphWindow = Window<VansAnimGraphEditorWindow>();
	if (!animationGraphWindow)
    {
        VANS_LOG_WARN("[AnimationEditor] Animation Graph Editor window is not initialized");
        return;
    }

    const std::string& selectedGuid = Vans::VansEditorSelectionService::Get().EntityGuid();
    if (selectedGuid.empty())
    {
        VANS_LOG_WARN("[AnimationEditor] Select a scene entity with an Animation component first");
        return;
    }

	auto& editorAPI = GetMutableEditorAPI();
	Vans::EditorAPI::IAnimationEditorAPI& animationAPI = editorAPI;
	const auto binding = animationAPI.GetAnimationAssetBinding(selectedGuid);
    if (!binding.available || binding.animatorAssetPath.empty())
    {
		VANS_LOG_WARN("[AnimationEditor] Selected entity has no Animator asset: " << selectedGuid);
        return;
    }

	animationGraphWindow->Open(binding.animatorAssetPath);
}

void VansGraphics::VansEditorWindow::OpenAnimationAsset(const std::string& sourcePath)
{
	OpenAssetForAuthoring(sourcePath);
}

void VansGraphics::VansEditorWindow::OpenTimelineInstance(
	const std::string& sourcePath,
	const std::string& ownerEntityGuid)
{
	if (auto* timelineWindow = Window<VansTimelineEditorWindow>())
		timelineWindow->Open(sourcePath, ownerEntityGuid);
	else VANS_LOG_WARN("[TimelineEditor] Timeline Editor is not initialized");
}

void VansGraphics::VansEditorWindow::OpenAssetForAuthoring(const std::string& sourcePath)
{
	const Vans::VansAssetType assetType = Vans::VansAssetDatabase::Classify(sourcePath);
    if (assetType == Vans::VansAssetType::Prefab) { QueuePrefabOpen(sourcePath); return; }
	if (assetType == Vans::VansAssetType::AnimatorController)
	{
		if (auto* animationGraphWindow = Window<VansAnimGraphEditorWindow>())
			animationGraphWindow->Open(sourcePath);
		else VANS_LOG_WARN("[AnimationEditor] Animation Graph Editor is not initialized");
		return;
	}
	if (assetType == Vans::VansAssetType::BoneMask)
	{
		if (auto* boneMaskWindow = Window<VansBoneMaskEditorWindow>())
			boneMaskWindow->Open(sourcePath);
		else VANS_LOG_WARN("[AnimationEditor] Bone Mask Editor is not initialized");
		return;
	}
	if (assetType == Vans::VansAssetType::Timeline)
	{
		if (auto* timelineWindow = Window<VansTimelineEditorWindow>())
			timelineWindow->Open(sourcePath);
		else VANS_LOG_WARN("[TimelineEditor] Timeline Editor is not initialized");
		return;
	}
	if (Vans::VansGameplayAssetSchemaRegistry::IsGameplayAssetType(assetType))
	{
		if (auto* gameplayActionWindow = Window<VansGameplayActionEditorWindow>())
			gameplayActionWindow->Open(sourcePath);
		else VANS_LOG_WARN("[GAFEditor] Gameplay Action Editor is not initialized");
		if (auto* debuggerWindow = Window<VansGAFDebuggerWindow>())
			debuggerWindow->SetSimulationSourcePath(sourcePath);
		return;
	}
	VANS_LOG_WARN("[Editor] Unsupported authoring asset: " << sourcePath);
}

void VansGraphics::VansEditorWindow::DrawBuildMenu()
{
	const Vans::VansGamePackagePlatform selectedPlatform = m_EditorConfiguration->packagePlatform;
    auto& editorAPI = GetMutableEditorAPI();
	Vans::EditorAPI::IProjectEditorAPI& projectAPI = editorAPI;

    if (!ImGui::BeginMenu("Build"))
        return;

    const std::string projectRootPath = projectAPI.GetProjectRootPath();
    const bool hasProject = !projectRootPath.empty();
    const bool hasScene = !m_SceneLoadSession.CurrentScenePath().empty();
    const std::string sceneLabel = hasScene
        ? std::filesystem::path(m_SceneLoadSession.CurrentScenePath()).filename().string()
        : std::string("<none>");

    ImGui::Separator();
    ImGui::Text("Platform: %s", Vans::ToString(selectedPlatform));
    ImGui::Text("Scene: %s", sceneLabel.c_str());

    const bool canPackage = hasProject && hasScene;
    if (!canPackage)
        ImGui::BeginDisabled();

    if (ImGui::MenuItem("Package Current Scene"))
    {
		VansEditorPackageContext context;
		context.request.platform = selectedPlatform;
		context.request.projectRootPath = projectRootPath;
		context.request.engineRootPath = GetEditorPackageEngineRoot();
		context.request.scenePath = m_SceneLoadSession.CurrentScenePath();
		context.sceneDirty = GetSceneDocument() && GetSceneDocument()->IsDirty();
		context.assetsDirty = Vans::VansAssetDocumentRegistry::Get().HasDirtyDocuments();
		context.projectDocumentsDirty = projectAPI.GetProjectConfigSnapshot().dirty;
		const VansEditorPackageStatus& status = m_PackageSession.Execute(context);
		if (status.outcome == VansEditorPackageOutcome::DirtyScene ||
			status.outcome == VansEditorPackageOutcome::DirtyAssets ||
			status.outcome == VansEditorPackageOutcome::DirtyProjectDocuments ||
			status.outcome == VansEditorPackageOutcome::MissingInput)
		{
			VANS_LOG_WARN("[Package] " << status.message);
		}
		else if (!status.Succeeded())
		{
			VANS_LOG_ERROR("[Package] " << status.message);
		}
    }

    if (!canPackage)
        ImGui::EndDisabled();

    if (!hasProject)
        ImGui::TextDisabled("Open a project before packaging.");
    else if (!hasScene)
        ImGui::TextDisabled("Load a scene before packaging.");

	const VansEditorPackageStatus& packageStatus = m_PackageSession.Status();
    if (packageStatus.HasAttempt())
    {
        ImGui::Separator();
        if (packageStatus.Succeeded())
            ImGui::Text("Last package: %s", packageStatus.message.c_str());
        else
            ImGui::TextDisabled("Last package: %s", packageStatus.message.c_str());
        if (!packageStatus.outputPath.empty())
            ImGui::TextWrapped("%s", packageStatus.outputPath.c_str());
    }

    ImGui::EndMenu();
}

void VansGraphics::VansEditorWindow::CreateWindowComponents()
{
	m_WindowRegistry.Clear();

    // Create the project selector overlay and reset project-session state.
    m_ProjectSession.Initialize();

	m_WindowRegistry.Add<VansHierachuWindow>();
	m_WindowRegistry.Add<VansLightWindow>();
	m_WindowRegistry.Add<VansProjectWindow>();
	m_WindowRegistry.Add<VansProjectSettingsWindow>();
	auto& sceneWindow = m_WindowRegistry.Add<VansSceneWindow>(m_DebugViewState);
	sceneWindow.SetSceneEditService(GetSceneEditService());
	m_WindowRegistry.Add<VansInspectorWindow>();
	m_WindowRegistry.Add<VansGBufferWindow>();
	m_WindowRegistry.Add<VansRenderDebugWindow>();
	m_WindowRegistry.Add<VansScriptorWindow>();
	m_WindowRegistry.Add<VansConsoleWindow>();
	m_WindowRegistry.Add<VansProfilerWindow>();
	m_WindowRegistry.Add<VansAnimGraphEditorWindow>();
	m_WindowRegistry.Add<VansSceneAnimationPreviewWindow>();
	m_WindowRegistry.Add<VansBoneMaskEditorWindow>();
	m_WindowRegistry.Add<VansTimelineEditorWindow>();
	m_WindowRegistry.Add<VansGameplayActionEditorWindow>();
	m_WindowRegistry.Add<VansGAFDebuggerWindow>();
	m_WindowRegistry.Add<VansUIEditorWindow>();
	m_WindowRegistry.Add<VansClothProfileEditorWindow>();
	m_WindowRegistry.Add<VansWaterWindow>();
	m_WindowRegistry.Add<VansTerrainWindow>();
	m_WindowRegistry.Add<VansReflectionProbeWindow>();
	m_WindowRegistry.Add<VansGIWindow>();
	m_WindowRegistry.Add<VansPostProcessWindow>();
	m_WindowRegistry.Add<VansShadowDebuggerWindow>();
	m_WindowRegistry.Add<VansPcgWindow>();
	m_WindowRegistry.Add<VansHiZCullWindow>(m_DebugViewState);
	m_WindowRegistry.Add<VansAudioDebugWindow>();
	m_WindowRegistry.Add<VansSkeletonDebugWindow>(m_DebugViewState);
	m_WindowRegistry.Add<VansParticleDebugWindow>();
	m_WindowRegistry.Add<VansMotionMatchingDebugWindow>();
	m_WindowRegistry.Add<VansAIDebugWindow>();

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
    if (!m_SceneLoadSession.HasPendingRequest())
        return;
	VansEditorSceneLoadContext context;
	context.pendingScenePath = m_SceneLoadSession.PendingPath();
	context.currentScenePath = m_SceneLoadSession.CurrentScenePath();
	context.mode = m_SceneLoadSession.PendingMode();
	context.hasPrefabSession = HasPrefabSession();
	context.sceneDirty = GetSceneDocument() && GetSceneDocument()->IsDirty();
	const std::filesystem::path normalizedPendingScenePath =
		std::filesystem::path(context.pendingScenePath).lexically_normal();
	context.canReuseCurrentDocument =
		GetSceneDocument() != nullptr &&
		GetSceneDocument()->SourcePath().lexically_normal() == normalizedPendingScenePath;

	Vans::SceneDocumentLoadResult pendingDocumentLoad;
	Vans::VansSceneDocument* sceneDocument = GetSceneDocument();
	auto& editorAPI = GetMutableEditorAPI();
	Vans::EditorAPI::IPlayModeEditorAPI& playModeAPI = editorAPI;
	Vans::EditorAPI::IProjectEditorAPI& projectAPI = editorAPI;
	Vans::EditorAPI::IRuntimePhysicsEditorAPI& runtimePhysicsAPI = editorAPI;
	Vans::EditorAPI::IRuntimeSceneEditorAPI& runtimeSceneAPI = editorAPI;
	VansEditorSceneLoadOperations operations;
	operations.clearPendingRequest = [] { m_SceneLoadSession.ClearPending(); };
	operations.prepareDocument = [&](const std::string& scenePath)
	{
		pendingDocumentLoad = Vans::VansSceneDocumentLoader::Load(
			scenePath,
			Vans::VansPrefabEditService::Lookup(editorAPI));
		if (!pendingDocumentLoad)
		{
			for (const auto& diagnostic : pendingDocumentLoad.diagnostics)
				VANS_LOG_ERROR("[SceneDocument] " << diagnostic.propertyPointer << " " << diagnostic.message);
			return false;
		}
		sceneDocument = pendingDocumentLoad.document.get();
		return true;
	};
	operations.refreshPrefabView = [&]
	{
		std::string prefabError;
		if (sceneDocument->RefreshPrefabView(prefabError))
			return true;
		VANS_LOG_ERROR("[Prefab] " << prefabError);
		return false;
	};
	operations.loadRuntimeScene = [&](Vans::EditorAPI::RuntimeSceneLoadMode mode)
	{
		Vans::EditorAPI::RuntimeSceneLoadRequest request;
		request.document = BuildRuntimeSceneDocumentSnapshot(*sceneDocument);
		request.mode = mode;
		const Vans::EditorAPI::RuntimeSceneLoadResult result =
			runtimeSceneAPI.LoadRuntimeScene(request);
		if (!result)
		{
			for (const auto& diagnostic : result.diagnostics)
				VANS_LOG_ERROR("[Editor] Scene load " << diagnostic.code << ": " << diagnostic.message);
		}
		return VansEditorRuntimeSceneLoadStatus{result.success, result.contentRevision};
	};
	operations.markLoaded = [](const std::string& scenePath)
	{
		m_SceneLoadSession.MarkLoaded(scenePath);
	};
	operations.commitPreparedDocument = [&]
	{
		m_SceneDocumentSession.ReplaceDocument(
			std::move(pendingDocumentLoad.document),
			[] { return RefreshActiveScenePreview(); });
		if (auto* sceneWindow = Window<VansSceneWindow>())
			sceneWindow->SetSceneEditService(GetSceneEditService());
		m_RuntimeMultiMeshExpansionScannedStateId = 0;
	};
	operations.detachEditorViewportCameras = []
	{
		DetachEditorViewportCamerasFromSceneTransforms();
	};
	operations.setTimePaused = [](bool paused) { VansTimer::SetTimePaused(paused); };
	operations.installRuntimeVehiclePhysicsStepCallback = [&runtimePhysicsAPI]
	{
		runtimePhysicsAPI.InstallRuntimeVehiclePhysicsStepCallback();
	};
	operations.startRuntimePhysicsIfNeeded = [&runtimePhysicsAPI]
	{
		runtimePhysicsAPI.StartRuntimePhysicsIfNeeded();
	};
	operations.setPlayState = [&playModeAPI](Vans::EditorAPI::EnginePlayState state)
	{
		playModeAPI.SetPlayState(state);
	};
	operations.setCurrentProjectScenePath = [&projectAPI](const std::string& scenePath)
	{
		projectAPI.SetCurrentProjectScenePath(scenePath);
	};
	operations.logInfo = [](const std::string& message) { VANS_LOG(message); };
	operations.logWarning = [](const std::string& message) { VANS_LOG_WARN(message); };
	operations.logError = [](const std::string& message) { VANS_LOG_ERROR(message); };

	VansEditorSceneLoadController::Execute(context, operations);
}

void VansGraphics::VansEditorWindow::ProcessPendingProjectLoad()
{
	const VansEditorPendingProjectRequest* pendingRequest = m_ProjectSession.PendingRequest();
    if (!pendingRequest)
        return;
    auto& editorAPI = GetMutableEditorAPI();
	Vans::EditorAPI::IProjectEditorAPI& projectAPI = editorAPI;
	Vans::EditorAPI::IRuntimePhysicsEditorAPI& runtimePhysicsAPI = editorAPI;
	Vans::EditorAPI::IRuntimeSceneEditorAPI& runtimeSceneAPI = editorAPI;
	Vans::EditorAPI::IScriptLifecycleEditorAPI& scriptLifecycleAPI = editorAPI;
	VansEditorProjectSwitchContext context;
	context.request = *pendingRequest;
	context.hasPrefabSession = HasPrefabSession();
	context.sceneDirty = GetSceneDocument() && GetSceneDocument()->IsDirty();
	context.assetsDirty = Vans::VansAssetDocumentRegistry::Get().HasDirtyDocuments();
	context.projectDocumentsDirty = projectAPI.GetProjectConfigSnapshot().dirty;

	VansEditorProjectSwitchOperations operations;
	operations.clearPendingRequest = [] { m_ProjectSession.ClearPendingRequest(); };
	operations.setTimePaused = [](bool paused) { VansTimer::SetTimePaused(paused); };
	operations.pauseRuntimePhysics = [&runtimePhysicsAPI] { runtimePhysicsAPI.PauseRuntimePhysics(); };
	operations.unloadRuntimeScene = [&runtimeSceneAPI] { runtimeSceneAPI.UnloadRuntimeScene(); };
	operations.unloadRuntimeProjectResources = [&runtimeSceneAPI]
	{
		runtimeSceneAPI.UnloadRuntimeProjectResources();
	};
	operations.closeProject = [&projectAPI] { projectAPI.CloseProject(); };
	operations.clearAssetHistories = []
	{
		Vans::VansAssetDocumentEditService::ClearAllHistories();
	};
	operations.clearAssetDocuments = []
	{
		Vans::VansAssetDocumentRegistry::Get().Clear();
	};
	operations.markProjectLoaded = [](bool loaded) { m_ProjectSession.MarkLoaded(loaded); };
	operations.clearSceneSession = []
	{
		if (auto* sceneWindow = Window<VansSceneWindow>())
			sceneWindow->SetSceneEditService(nullptr);
		m_SceneDocumentSession.Reset();
		m_SceneLoadSession.Reset();
	};
	operations.openProject = [&projectAPI](const Vans::EditorAPI::ProjectOpenRequest& request)
	{
		return projectAPI.OpenProject(request);
	};
	operations.logInfo = [](const std::string& message) { VANS_LOG(message); };
	operations.logWarning = [](const std::string& message) { VANS_LOG_WARN(message); };
	operations.logError = [](const std::string& message) { VANS_LOG_ERROR(message); };

	const VansEditorProjectSwitchResult switchResult =
		VansEditorProjectSwitchController::Execute(context, operations);
	if (!switchResult.Opened())
		return;
	const Vans::EditorAPI::ProjectOpenResult& projectOpenResult =
		switchResult.projectOpenResult;

	if (const char* autoAsset = std::getenv("FORESTENGINE_AUTOOPEN_ASSET"))
	{
		std::filesystem::path assetPath(autoAsset);
		if (assetPath.is_relative())
			assetPath = std::filesystem::path(projectOpenResult.projectRootPath) / assetPath;
		assetPath = assetPath.lexically_normal();
		if (std::filesystem::is_regular_file(assetPath))
		{
			VANS_LOG("[Editor] Automation opening asset: " << assetPath.string());
			OpenAnimationAsset(assetPath.string());
		}
		else
			VANS_LOG_ERROR("[Editor] Automation asset does not exist: " << assetPath.string());
	}

	scriptLifecycleAPI.SetupRuntimeScriptProjectVenv(projectOpenResult.projectRootPath);
	Vans::VansEditorSelectionService::Get().Clear("EditorWindow");
	const bool skipDefaultSceneForAutomation =
		std::getenv("FORESTENGINE_AUTOMATION_SKIP_DEFAULT_SCENE") != nullptr;
	if (skipDefaultSceneForAutomation)
	{
		VANS_LOG("[Editor] Automation skipping default scene load");
	}

    if (!skipDefaultSceneForAutomation && !projectOpenResult.defaultScenePath.empty())
    {
        const std::string& absScenePath = projectOpenResult.defaultScenePath;
        if (std::filesystem::exists(absScenePath))
        {
            VANS_LOG("[Editor] Deferring default scene load: " << absScenePath);
			m_SceneLoadSession.Request(absScenePath);
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

    m_ProjectSession.QueueOpen(projectPath);
    VANS_LOG("[Editor] Automation queued project open: " << projectPath);
}

std::unique_ptr<VansGraphics::IVansRenderFrameOverlay>
VansGraphics::VansEditorWindow::DrawEditorWindows(VansGraphicsDevice& device)
{
    // Start the Dear ImGui frame
    m_GUIBackEnd->BeginFrame();
	auto& playModeAPI = static_cast<Vans::EditorAPI::IPlayModeEditorAPI&>(GetMutableEditorAPI());
    // 游戏正在隐藏光标时，暂时停用 ImGui 的平台光标写入，避免每帧先显示再隐藏。
    // 只修改这次平台更新；普通编辑器的光标形状和其他配置继续由 ImGui 管理。
    auto& cursorIO = ImGui::GetIO();
    const bool cursorChangesDisabled = (cursorIO.ConfigFlags & ImGuiConfigFlags_NoMouseCursorChange) != 0;
    if (playModeAPI.IsGameCursorHidden())
        cursorIO.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    ImGui_ImplGlfw_NewFrame();
    if (!cursorChangesDisabled)
        cursorIO.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange;
    ImGui::NewFrame();
	// ImGuizmo 在整个编辑器 ImGui 帧中共享状态，统一初始化一次，所有工具窗口
	// 才能在 Scene 窗口关闭或未绘制时继续使用视口手柄。
	ImGuizmo::BeginFrame();

    // ── Project Selector Overlay ──────────────────────────────────────────
    // When no project is loaded yet, show the full-screen selector instead
    // of the normal editor windows.
    if (!m_ProjectSession.IsLoaded())
    {
        auto& editorAPI = GetMutableEditorAPI();
		Vans::EditorAPI::IProjectEditorAPI& projectAPI = editorAPI;
        Vans::VansProjectSelector* projectSelector = m_ProjectSession.Selector();
        if (!projectSelector)
            throw std::logic_error("project session selector is not initialized");
        auto result = projectSelector->Render(projectAPI);

        switch (result)
        {
        case Vans::ProjectSelectorResult::OpenExisting:
        {
            const std::string& path = projectSelector->GetSelectedProjectPath();
            VANS_LOG("[Editor] Queue project open: " << path);
            m_ProjectSession.QueueOpen(path);
            break;
        }
        case Vans::ProjectSelectorResult::CreateNew:
        {
            const std::string& path = projectSelector->GetSelectedProjectPath();
            const std::string& name = projectSelector->GetNewProjectName();
            VANS_LOG("[Editor] Queue project creation: " << name << " at " << path);
            m_ProjectSession.QueueCreate(path, name);
            break;
        }
        case Vans::ProjectSelectorResult::Cancelled:
            glfwSetWindowShouldClose(m_VansEditorWindow.m_VansGraphicsHandle, true);
            break;
        default:
            break;
        }

        playModeAPI.UpdateGameCursorViewport(false);

        // Render the ImGui frame (project selector only)
        ImGui::Render();
        return m_GUIBackEnd->CaptureDrawData(ImGui::GetDrawData());
    }

    // ── Normal Editor Windows ─────────────────────────────────────────────
    {
		auto* projectWindow = Window<VansProjectWindow>();
		auto* sceneWindow = Window<VansSceneWindow>();
		auto* sceneAnimationPreviewWindow = Window<VansSceneAnimationPreviewWindow>();
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
		const bool sceneDocumentReady = editingMode && GetSceneDocument() && GetSceneDocument()->IsHealthy();
		const bool hasDirtyAssets = Vans::VansAssetDocumentRegistry::Get().HasDirtyDocuments();
		std::shared_ptr<Vans::VansOpenAssetDocument> selectedAssetDocument;
		if (!Vans::VansEditorSelectionService::Get().AssetPath().empty())
			selectedAssetDocument = Vans::VansAssetDocumentRegistry::Get().Find(Vans::VansEditorSelectionService::Get().AssetPath());
		const bool selectedAssetDirty = editingMode && selectedAssetDocument && selectedAssetDocument->IsDirty();
		auto& editorAPI = GetMutableEditorAPI();
		Vans::EditorAPI::IAnimationEditorAPI& animationAPI = editorAPI;
		Vans::EditorAPI::IAssetAuthoringEditorAPI& assetAuthoringAPI = editorAPI;
		Vans::EditorAPI::IPcgEditorAPI& pcgAPI = editorAPI;
		Vans::EditorAPI::IProjectEditorAPI& projectAPI = editorAPI;
		Vans::EditorAPI::IRuntimeCommandHistoryEditorAPI& runtimeHistoryAPI = editorAPI;
		Vans::EditorAPI::IRuntimeSceneEditorAPI& runtimeSceneAPI = editorAPI;
		Vans::EditorAPI::ISceneInteractionEditorAPI& sceneInteractionAPI = editorAPI;
		Vans::EditorAPI::ITerrainEditorAPI& terrainAPI = editorAPI;
		Vans::VansAssetDocumentRegistry::Get().SetWorkingCopyPublisher(
			[assetAuthoringAPIAddress = &assetAuthoringAPI](
				const Vans::VansOpenAssetDocument& document,
				std::string& error)
			{
				return PublishOpenAssetWorkingCopy(
					*assetAuthoringAPIAddress, document, error);
			});
		const bool hasDirtyProjectDocuments = projectAPI.GetProjectConfigSnapshot().dirty;
		const std::string& selectedEntityGuid = Vans::VansEditorSelectionService::Get().EntityGuid();
		const auto selectedAnimationBinding = selectedEntityGuid.empty()
			? Vans::EditorAPI::AnimationAssetBindingSnapshot{}
			: animationAPI.GetAnimationAssetBinding(selectedEntityGuid);
		const bool canOpenSelectedAnimationGraph = selectedAnimationBinding.available
			&& !selectedAnimationBinding.animatorAssetPath.empty();
		Vans::VansEditorHistoryService editorHistory = VansEditorHistoryAdapter::Compose(
			runtimeHistoryAPI,
			pcgAPI,
			sceneInteractionAPI,
			terrainAPI,
			GetSceneDocument(),
			GetSceneEditService(),
			selectedAssetDocument,
			[] { ReloadCurrentSceneForEditing(); });
		VansEditorAuthoringCommandContext authoringCommandContext;
		authoringCommandContext.hasPrefabSession = HasPrefabSession();
		authoringCommandContext.sceneDirty =
			GetSceneDocument() && GetSceneDocument()->IsDirty();
		authoringCommandContext.assetsDirty = hasDirtyAssets;
		authoringCommandContext.projectDocumentsDirty = hasDirtyProjectDocuments;
		VansEditorAuthoringCommandOperations authoringCommandOperations;
		authoringCommandOperations.savePrefab = [] { return SavePrefabSession(); };
		authoringCommandOperations.saveSceneAndOwnedAssets = [&]()
		{
			const Vans::VansAssetSaveResult assetResult =
				Vans::VansEditorAssetSaveService::Get().SaveSceneAndOwnedAssets(editorAPI, sceneDocumentReady ? GetSceneDocument() : nullptr);
			if (!assetResult)
			{
				for (const std::string& error : assetResult.errors)
					VANS_LOG_ERROR("[SceneAssetSave] " << error);
				return false;
			}
			return true;
		};
		authoringCommandOperations.saveSelectedAsset = [&]()
		{
			const Vans::VansAssetSaveResult result =
				Vans::VansEditorAssetSaveService::Get().SaveAsset(
					editorAPI, Vans::VansEditorSelectionService::Get().AssetPath());
			if (!result)
			{
				for (const std::string& error : result.errors)
					VANS_LOG_ERROR("[AssetSave] " << error);
			}
			return VansEditorAuthoringSaveStatus{
				static_cast<bool>(result), result.wroteFile };
		};
		authoringCommandOperations.saveAllDirtyAssets = [&]()
		{
			const Vans::VansAssetSaveResult result =
				Vans::VansEditorAssetSaveService::Get().SaveAllDirtyAssets(editorAPI);
			if (!result)
			{
				for (const std::string& error : result.errors)
					VANS_LOG_ERROR("[AssetSave] " << error);
			}
			return VansEditorAuthoringSaveStatus{
				static_cast<bool>(result), result.wroteFile };
		};
		authoringCommandOperations.saveProjectDocuments = [&]()
		{
			const Vans::EditorAPI::ProjectConfigEditResult result =
				projectAPI.SaveProjectDocuments();
			if (!result.success)
				VANS_LOG_ERROR("[ProjectSave] " << result.message);
			return result.success;
		};
		authoringCommandOperations.reloadCurrentSceneForEditing = []
		{
			ReloadCurrentSceneForEditing();
		};
		authoringCommandOperations.requestExit = []
		{
			glfwSetWindowShouldClose(m_VansEditorWindow.m_VansGraphicsHandle, true);
		};
		authoringCommandOperations.logWarning = [](const std::string& message)
		{
			VANS_LOG_WARN(message);
		};
		auto executeAuthoringCommand = [&](VansEditorAuthoringCommand command)
		{
			return VansEditorAuthoringCommandController::Execute(
				command, authoringCommandContext, authoringCommandOperations);
		};
		VansEditorShellCommandOperations shellCommandOperations;
		shellCommandOperations.executeAuthoringCommand = [&](VansEditorAuthoringCommand command)
		{
			executeAuthoringCommand(command);
		};
		shellCommandOperations.requestAssetCreation = [projectWindow](
			Vans::EditorAPI::ProjectAssetCreationKind kind)
		{
			if (projectWindow)
				projectWindow->RequestAssetCreation(kind);
		};
		shellCommandOperations.undo = [&] { editorHistory.Undo(); };
		shellCommandOperations.redo = [&] { editorHistory.Redo(); };
		shellCommandOperations.setSceneAnimationPreviewOpen = [sceneAnimationPreviewWindow](bool open)
		{
			if (sceneAnimationPreviewWindow)
				sceneAnimationPreviewWindow->SetOpen(open);
		};
		shellCommandOperations.openSelectedAnimationGraph = []
		{
			OpenSelectedAnimationGraph();
		};

		VansEditorShellShortcutState shortcutState;
		shortcutState.editingMode = editingMode;
		shortcutState.wantTextInput = io.WantTextInput;
		shortcutState.controlDown = io.KeyCtrl;
		shortcutState.shiftDown = io.KeyShift;
		shortcutState.sceneDocumentReady = sceneDocumentReady;
		shortcutState.canUndo = editorHistory.CanUndo();
		shortcutState.canRedo = editorHistory.CanRedo();
		if (editingMode && !io.WantTextInput && io.KeyCtrl)
		{
			shortcutState.savePressed = ImGui::IsKeyPressed(ImGuiKey_S, false);
			shortcutState.undoPressed = ImGui::IsKeyPressed(ImGuiKey_Z, false);
			shortcutState.redoPressed = ImGui::IsKeyPressed(ImGuiKey_Y, false);
		}
		const VansEditorShellShortcutResolution shortcut =
			VansEditorShellCommandController::ResolveShortcut(shortcutState);
		if (shortcut.available)
			VansEditorShellCommandController::Execute(shortcut.command, shellCommandOperations);

		VansEditorShellMenuState menuState;
		menuState.canSaveScene = sceneDocumentReady && GetSceneDocument()->IsDirty();
		menuState.canSaveAsset = selectedAssetDirty;
		menuState.canSaveProjectDocuments = hasDirtyProjectDocuments;
		menuState.canSaveAll = menuState.canSaveScene || hasDirtyAssets || hasDirtyProjectDocuments;
		menuState.canCreateAssets = [&]()
		{
			const Vans::EditorAPI::ProjectBrowserRootSnapshot assetRoot =
				assetAuthoringAPI.GetProjectBrowserRoot();
			return projectWindow && assetRoot.projectLoaded && !assetRoot.rootPath.empty();
		};
		menuState.canUndo = editingMode && editorHistory.CanUndo();
		menuState.canRedo = editingMode && editorHistory.CanRedo();
		menuState.canOpenSelectedAnimationGraph = canOpenSelectedAnimationGraph;
		menuState.sceneAnimationPreviewAvailable = sceneAnimationPreviewWindow != nullptr;
		menuState.sceneAnimationPreviewOpen = sceneAnimationPreviewWindow &&
			sceneAnimationPreviewWindow->IsOpen();
		menuState.reflectionProbeWindowAvailable =
			Window<VansReflectionProbeWindow>() != nullptr;
		menuState.giWindowAvailable = Window<VansGIWindow>() != nullptr;
		menuState.wireframeMode = &m_DebugViewState.wireframeMode;
		menuState.vehicleDebugGizmos = &m_DebugViewState.vehicleDebugGizmos;

		VansEditorShellMenu::Draw(menuState, m_WindowCatalog,
			[&](const VansEditorShellCommand& command)
			{
				VansEditorShellCommandController::Execute(command, shellCommandOperations);
			},
			[] { DrawBuildMenu(); },
			[&]
			{
				const VansEditorPlayToolbarState toolbarState =
					VansEditorPlayToolbar::Resolve(
						playModeAPI.GetPlayState(),
						runtimeSceneAPI.IsRuntimeSceneReady() &&
							!runtimeSceneAPI.IsRuntimeSceneSwitching());
				VansEditorPlayToolbar::Draw(toolbarState, m_EditorConfiguration->toolbar,
					[](VansEditorPlayCommand command)
					{
						ExecutePlayCommand(command);
					});
			});

        //绘制所有窗口
        for (const auto& window : m_WindowRegistry.All())
        {
            VANS_PROFILE_SCOPE(typeid(*window).name(), Vans::ProfileCategory::Editor);
            window->ShowWindow(editorAPI);
        }

        ImGui::End();
    }



	auto* cursorSceneWindow = Window<VansSceneWindow>();
    playModeAPI.UpdateGameCursorViewport(cursorSceneWindow && cursorSceneWindow->IsGameCursorViewportInteractive() &&
        !ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId | ImGuiPopupFlags_AnyPopupLevel) &&
        ImGui::GetDragDropPayload() == nullptr);

    //GUI handle rendeing
    ImGui::Render();

    return m_GUIBackEnd->CaptureDrawData(ImGui::GetDrawData());
}

void VansGraphics::VansEditorWindow::StartEditorLoop(
    VansGraphics::VansCamera& camera,
    VansGraphics::VansRenderSystem& renderSystem)
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
	// Platform viewport renderer callbacks submit Vulkan work directly from Main.
	// Keep docking, but leave multi-viewport disabled until viewport packets use
	// the render-system work stream as well.
	io.ConfigFlags &= ~ImGuiConfigFlags_ViewportsEnable;

	VansEditorTheme::Apply(*m_EditorConfiguration);

    //初始化GUI的graphics back end
    m_GUIBackEnd->InitBackEnd(*m_GraphicsDevice, m_VansEditorWindow.m_VansGraphicsHandle);
	if (!renderSystem.ExecuteRenderThreadTransaction(
		m_GUIBackEnd->CreateRenderThreadInitialization()))
	{
		VANS_LOG_ERROR("[Editor] Render-thread GUI backend initialization failed.");
		m_GUIBackEnd->ShutdownBackEnd();
		m_Cameras.clear();
		return;
	}

    // Initialize GPU profiler
#if VANS_PROFILER_ENABLED
    renderSystem.InitializeGpuProfiler();
#endif

    //初始化脚本环境
    auto& startupEditorAPI = GetMutableEditorAPI();
	auto& scriptLifecycleAPI =
		static_cast<Vans::EditorAPI::IScriptLifecycleEditorAPI&>(startupEditorAPI);
	scriptLifecycleAPI.InitializeRuntimeScripts();
	Vans::VansEditorShaderHotReloadController shaderHotReloadController;
	shaderHotReloadController.Initialize(startupEditorAPI);
	const auto automationStartedAt = std::chrono::steady_clock::now();
	double automationCloseSeconds = 0.0;
	if (const char* value = std::getenv("FORESTENGINE_AUTOCLOSE_SECONDS"))
	{
		char* end = nullptr;
		automationCloseSeconds = std::strtod(value, &end);
		if (end == value || !std::isfinite(automationCloseSeconds)
			|| automationCloseSeconds < 0.0)
			automationCloseSeconds = 0.0;
	}
	double automationPlayObservationSeconds = 0.0;
	if (const char* value = std::getenv("FORESTENGINE_AUTOPLAY_SECONDS"))
	{
		char* end = nullptr;
		automationPlayObservationSeconds = std::strtod(value, &end);
		if (end == value || !std::isfinite(automationPlayObservationSeconds)
			|| automationPlayObservationSeconds <= 0.0)
			automationPlayObservationSeconds = 0.0;
	}
	bool automationPlayRequested = false;
	bool automationPlayConfirmed = false;
	std::chrono::steady_clock::time_point automationPlayConfirmedAt{};

#if VANS_PROFILER_ENABLED
    // 默认关闭。先暖机，再检查连续窗口，最后导出多帧统计；启动帧不作性能证据。
    std::string automationGpuProfileOutputDir;
    if (const char* value = std::getenv("FORESTENGINE_GPU_PROFILE_DUMP_DIR"))
        automationGpuProfileOutputDir = value;

    std::uint64_t automationGpuProfileWarmupFrames = 128u;
    if (const char* value = std::getenv("FORESTENGINE_GPU_PROFILE_WARMUP_FRAMES"))
    {
        char* end = nullptr;
        const unsigned long long parsed = std::strtoull(value, &end, 10);
        if (end != value && *end == '\0' && value[0] != '-')
            automationGpuProfileWarmupFrames = parsed;
    }

    std::uint64_t automationGpuProfileReadyFrames = 0u;
    auto automationGpuProfileReadyAt = automationStartedAt;
    constexpr double automationGpuProfileMinimumWarmupSeconds = 30.0;
    std::uint32_t automationGpuProfileLastStableWindows = 0;
    std::uint64_t automationGpuProfileLastRestarts = 0;
    bool automationGpuProfileDumped = false;
    const bool automationGpuProfileExitAfterDump =
        ReadAutomationBoolEnv("FORESTENGINE_GPU_PROFILE_EXIT_AFTER_DUMP");
    const auto finishAutomationGpuProfileFrame = [&](bool capturedThisFrame)
    {
        if (!capturedThisFrame || automationGpuProfileDumped)
            return;

        const auto& capture = Vans::VansProfiler::Get().GetStableCapture();
        if (capture.GetStableWindows() != automationGpuProfileLastStableWindows
            || capture.GetRestartCount() != automationGpuProfileLastRestarts)
        {
            automationGpuProfileLastStableWindows = capture.GetStableWindows();
            automationGpuProfileLastRestarts = capture.GetRestartCount();
            VANS_LOG("[Profiler] Stability windows=" << capture.GetStableWindows()
                << "/3 restarts=" << capture.GetRestartCount()
                << " missingFrames=" << capture.GetMissingFrames());
        }
        if (capture.GetPhase() != Vans::VansProfileCapture::Phase::Complete)
            return;

        const bool saved = capture.DumpJson(automationGpuProfileOutputDir.c_str());
        automationGpuProfileDumped = true;
        if (saved)
        {
            Vans::VansProfiler::Get().DumpFrameJson(automationGpuProfileOutputDir.c_str());
            VANS_LOG("[Profiler] Stable capture saved samples=" << capture.GetSamples().size()
                << " first=" << capture.GetSamples().front().frameIndex
                << " last=" << capture.GetSamples().back().frameIndex
                << " restarts=" << capture.GetRestartCount() << " to " << automationGpuProfileOutputDir);
        }
        else
            VANS_LOG_ERROR("[Profiler] Stable capture report could not be saved");
        // 导出后结束聚合和旧快照保留；继续运行时不留下自动验收开销。
        Vans::VansProfiler::Get().CancelStableCapture();
        if (automationGpuProfileExitAfterDump)
        {
            // 自动采样也走既有窗口关闭及完整卸载流程。
            glfwSetWindowShouldClose(m_VansEditorWindow.m_VansGraphicsHandle, true);
        }
    };
#endif

    // Main loop
	while (!glfwWindowShouldClose(m_VansEditorWindow.m_VansGraphicsHandle))
	{
		if (automationPlayConfirmed
			&& std::chrono::duration<double>(std::chrono::steady_clock::now()
				- automationPlayConfirmedAt).count() >= automationPlayObservationSeconds)
		{
			VANS_LOG("[Editor] Automation Play observation completed after "
				<< automationPlayObservationSeconds << " seconds");
			glfwSetWindowShouldClose(m_VansEditorWindow.m_VansGraphicsHandle, true);
			continue;
		}
		if (automationCloseSeconds > 0.0
			&& std::chrono::duration<double>(std::chrono::steady_clock::now()
				- automationStartedAt).count() >= automationCloseSeconds)
		{
#if VANS_PROFILER_ENABLED
            if (!automationGpuProfileOutputDir.empty() && !automationGpuProfileDumped)
            {
                const auto& capture = Vans::VansProfiler::Get().GetStableCapture();
                capture.DumpJson(automationGpuProfileOutputDir.c_str());
                VANS_LOG("[Profiler] Capture timeout; stability/measurement not completed, no performance acceptance");
            }
#endif
			VANS_LOG("[Editor] Automation close timeout reached");
			glfwSetWindowShouldClose(m_VansEditorWindow.m_VansGraphicsHandle, true);
			continue;
		}
        VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);

        // 项目选择界面阶段没有完整场景帧；Profiler 窗口关闭时只保留轻量帧计数，不采集 scope/GPU timestamp。
        const bool profilerFrameActive = m_ProjectSession.IsLoaded();
#if VANS_PROFILER_ENABLED
        bool automationGpuProfileCapture = false;
        if (!automationGpuProfileOutputDir.empty() && !automationGpuProfileDumped)
        {
            auto& profilerEditorAPI = GetMutableEditorAPI();
			Vans::EditorAPI::IRuntimeSceneEditorAPI& profilerRuntimeSceneAPI =
				profilerEditorAPI;
            const bool sceneReady = profilerFrameActive && profilerRuntimeSceneAPI.IsRuntimeSceneReady()
                && !profilerRuntimeSceneAPI.IsRuntimeSceneSwitching();
            if (sceneReady)
            {
                const auto now = std::chrono::steady_clock::now();
                if (automationGpuProfileReadyFrames == 0)
                {
                    automationGpuProfileReadyAt = now;
                    VANS_LOG("[Profiler] Scene ready; starting warmup, minimum 30 seconds and "
                        << automationGpuProfileWarmupFrames << " frames");
                }
                ++automationGpuProfileReadyFrames;
                const double readySeconds = std::chrono::duration<double>(now - automationGpuProfileReadyAt).count();
                if (automationGpuProfileReadyFrames >= automationGpuProfileWarmupFrames
                    && readySeconds >= automationGpuProfileMinimumWarmupSeconds)
                {
                    automationGpuProfileCapture = true;
                    if (!Vans::VansProfiler::Get().GetStableCapture().IsActive())
                    {
                        Vans::VansProfiler::Get().SetPaused(false);
                        Vans::VansProfiler::Get().StartStableCapture(Vans::VansProfileCapture::Settings{});
                        VANS_LOG("[Profiler] Warmup finished after " << readySeconds << " seconds / "
                            << automationGpuProfileReadyFrames << " frames; checking CPU/GPU timing stability");
                    }
                }
            }
            else
            {
                automationGpuProfileReadyFrames = 0u;
                automationGpuProfileLastStableWindows = 0;
                automationGpuProfileLastRestarts = 0;
                Vans::VansProfiler::Get().CancelStableCapture();
            }
        }
        Vans::VansProfiler::Get().SetCaptureEnabled(
            profilerFrameActive
			&& (VansEditorWindow::IsWindowOpen(VansEditorWindowId::Profiler) || automationGpuProfileCapture));
#endif
#if VANS_PROFILER_ENABLED
        Vans::VansProfilerFrameScope profilerFrameScope(profilerFrameActive);
#endif

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
                renderSystem.RequestSurfaceResize(
                    static_cast<uint32_t>(width),
                    static_cast<uint32_t>(height));

                // NOTE: internal render resolution is unchanged, so camera aspect ratio
                // and all SSGI/SSR/GBuffer render targets are unaffected.

                m_VansEditorWindow.m_WindowStatus.swapChainRebuild = false;
            }
            else
            {
                // Window minimized — skip rendering this frame
#if VANS_PROFILER_ENABLED
                finishAutomationGpuProfileFrame(automationGpuProfileCapture);
#endif
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
		Vans::EditorAPI::IPlayModeEditorAPI& playModeAPI = editorAPI;
		Vans::EditorAPI::IRuntimePhysicsEditorAPI& runtimePhysicsAPI = editorAPI;
		Vans::EditorAPI::IRuntimeSceneEditorAPI& runtimeSceneAPI = editorAPI;

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
        //   ⑤ RenderSystem frame build — snapshot and render with up-to-date positions
		VansEditorRuntimeFramePort framePort(editorAPI);
		const bool isSceneReady = runtimeSceneAPI.IsRuntimeSceneReady();
		const Vans::VansRuntimeFramePolicy framePolicy{
			isSceneReady,
			isSceneReady && runtimePhysicsAPI.IsRuntimePhysicsRunning(),
			isSceneReady && playModeAPI.GetPlayState() == Vans::EditorAPI::EnginePlayState::Play,
			isSceneReady
		};
		const Vans::VansRuntimeFrameContext frameContext{
			VansGraphics::VansTimer::GetDeltaTime()
		};
		Vans::VansRuntimeFrameScheduler::RunGameplay(
			framePort, &framePort, framePolicy, frameContext);
		// Noesis IView is Main-affine. Update publishes an immutable render-tree
		// snapshot that the RT consumes later in this frame.
		VansRuntime::VansUISystem::Get().Update(
			static_cast<float>(VansGraphics::VansTimer::GetDeltaTime()));

        // ── Deferred resource & scene loading ───────────────────────────
        // Process pending loads BEFORE command buffer recording.
		// Project/scene replacement changes the stable scene-list slots referenced
		// by immutable frame snapshots.  It is rare structural maintenance, so
		// drain the ordered RT stream before touching those objects; steady-state
		// gameplay keeps the one-frame overlap and never takes this barrier.
		if ((m_ProjectSession.HasPendingRequest() || m_SceneLoadSession.HasPendingRequest() || HasPendingPrefabRequests()) &&
			!renderSystem.WaitForIdle())
		{
			VANS_LOG_ERROR("[Editor] Failed to drain render work before structural scene maintenance.");
			glfwSetWindowShouldClose(m_VansEditorWindow.m_VansGraphicsHandle, true);
			continue;
		}

        // 0) Project load/reload orchestration.  This must run before resource/scene load.
        {
            VANS_PROFILE_SCOPE("Project::ProcessPendingProjectLoad", Vans::ProfileCategory::IO);
            ProcessPendingProjectLoad();
        }

        // Load Scene content after the AssetDatabase dependency closure.
        {
            VANS_PROFILE_SCOPE("Resource::ProcessPendingSceneLoad", Vans::ProfileCategory::IO);
			ProcessPendingSceneLoad();
			ProcessPrefabRequests();
		}
		if (automationPlayObservationSeconds > 0.0
			&& !automationPlayRequested
			&& m_ProjectSession.IsLoaded()
			&& !m_SceneLoadSession.HasPendingRequest()
			&& runtimeSceneAPI.IsRuntimeSceneReady()
			&& playModeAPI.GetPlayState() == Vans::EditorAPI::EnginePlayState::Edit
			&& !m_SceneLoadSession.CurrentScenePath().empty())
		{
			VANS_LOG("[Editor] Automation triggering Play after Editor scene load: "
				<< m_SceneLoadSession.CurrentScenePath());
			ExecutePlayCommand(VansEditorPlayCommand::Play);
			automationPlayRequested = true;
		}
		if (automationPlayRequested
			&& !automationPlayConfirmed
			&& !m_SceneLoadSession.HasPendingRequest()
			&& runtimeSceneAPI.IsRuntimeSceneReady()
			&& playModeAPI.GetPlayState() == Vans::EditorAPI::EnginePlayState::Play)
		{
			automationPlayConfirmed = true;
			automationPlayConfirmedAt = std::chrono::steady_clock::now();
			VANS_LOG("[Editor] Automation Play confirmed: "
				<< m_SceneLoadSession.CurrentScenePath());
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
        // Build the editor overlay before publishing the immutable render frame.
        // Editor actions can therefore complete rare ordered RT maintenance
        // while no frame packet is open, and the following extraction captures
        // every Main-owned change into one coherent frame.
		if (auto* sceneWindow = Window<VansSceneWindow>())
			sceneWindow->RegistCamera(&camera);
		std::unique_ptr<IVansRenderFrameOverlay> editorOverlay;
        {
            VANS_PROFILE_SCOPE("Editor::DrawWindows", Vans::ProfileCategory::Editor);
			editorOverlay = DrawEditorWindows(*m_GraphicsDevice);
		}
		{
			VANS_PROFILE_SCOPE("Render::BuildFrame", Vans::ProfileCategory::RenderPrepare);
			if (!renderSystem.BeginFrame(camera))
			{
				VANS_LOG_ERROR("[Editor] Render-system frame preparation failed.");
				continue;
			}
		}
		{
			VANS_PROFILE_SCOPE("Render::PublishFrame", Vans::ProfileCategory::CommandRecord);
			const auto submitResult = renderSystem.SubmitFrame(std::move(editorOverlay));
			if (!submitResult)
				VANS_LOG_ERROR("[Editor] Render-system frame submission failed.");
        }

        // Profiler 快照由主帧 RAII 边界结束；GPU 查询由 RenderThread 异步回收。
        // 这里不能等待 RenderThread，否则会破坏正常的 N/N-1 独立执行。
        {
#if VANS_PROFILER_ENABLED
            finishAutomationGpuProfileFrame(automationGpuProfileCapture);
#endif
        }

    }

    m_Cameras.clear();

}

void VansGraphics::VansEditorWindow::DestroyVansEditorWindow()
{
    Vans::VansAssetDocumentRegistry::Get().ClearWorkingCopyPublisher();
    VansConsole::Get().ShutdownEventSubscription();

	m_ProjectSession.Shutdown();
	m_PrefabSession.Reset();
	m_WindowRegistry.Clear();

    // Destroy GPU profiler
#if VANS_PROFILER_ENABLED
    Vans::VansGpuProfiler::Get().Destroy();
#endif

    // Unregister Physics Callback on shutdown to avoid calling into destroyed objects
    static_cast<Vans::EditorAPI::IRuntimePhysicsEditorAPI&>(GetMutableEditorAPI())
        .ClearRuntimePhysicsStepCallback();

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
	m_EditorConfiguration.reset();
}

