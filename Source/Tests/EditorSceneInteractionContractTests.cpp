#include "../EngineCore/SceneRuntime/Transform/VansTransformStore.h"
#include "../EngineCore/Util/VansSceneViewMath.h"
#include "../EngineCore/EditorCore/VansEditorSelectionService.h"
#include "../EngineCore/EditorCore/VansEditorCameraController.h"
#include "../EngineCore/EditorCore/VansEditorHistoryService.h"
#include "../EngineCore/EditorCore/VansEditorHistoryAdapter.h"
#include "../EngineCore/EditorCore/VansEditorRuntimePreviewProjector.h"
#include "../EngineCore/EditorCore/VansEditorAuthoringCommandController.h"
#include "../EngineCore/EditorCore/VansEditorDebugViewState.h"
#include "../EngineCore/EditorCore/VansEditorShellCommandController.h"
#include "../EngineCore/EditorCore/VansSceneEditService.h"
#include "../EngineCore/EditorCore/VansEditorWindowCatalog.h"
#include "../EngineCore/EditorCore/VansEditorWindowRegistry.h"
#include "../EngineCore/EditorCore/VansEditorShellMenu.h"
#include "../EngineCore/EditorCore/VansEditorPackageSession.h"
#include "../EngineCore/EditorCore/VansEditorPrefabSession.h"
#include "../EngineCore/EditorCore/VansEditorPlayCommandController.h"
#include "../EngineCore/EditorCore/VansEditorPlayToolbar.h"
#include "../EngineCore/EditorCore/VansEditorProjectSession.h"
#include "../EngineCore/EditorCore/VansEditorProjectSwitchController.h"
#include "../EngineCore/EditorCore/VansEditorSceneDocumentSession.h"
#include "../EngineCore/EditorCore/VansEditorSceneLoadController.h"
#include "../EngineCore/EditorCore/VansEditorSceneLoadSession.h"
#include "../EngineCore/EditorCore/VansEditorConfiguration.h"
#include "../EngineCore/EngineAPILayer/Private/VansEditorSceneQuery.h"
#include "../EngineCore/EngineAPILayer/Public/IAIEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IAnimationEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IAnimationPreviewEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IAssetAuthoringEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IAssetEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IAudioEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IGAFEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IGIEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IMotionMatchingEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IParticleEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IPcgEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IPlayModeEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IProjectEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IReflectionProbeEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IRenderEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IRuntimeCommandHistoryEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IRuntimeFrameEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IRuntimePhysicsEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IRuntimeSceneEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/ISceneInteractionEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/ISceneSettingsEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IShaderEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IScriptLifecycleEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/ITerrainEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/ITimelineEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IUIEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IVehicleEditorAPI.h"
#include "../EngineCore/EngineAPILayer/Public/IWaterEditorAPI.h"
#include "../EngineCore/AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../EngineCore/RenderCore/VansCamera.h"
#include "../EngineCore/RenderCore/VansCameraControlArbiter.h"
#include "../EngineCore/RenderCore/VansRenderNode.h"
#include "../EngineCore/RenderCore/VulkanCore/VansMesh.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/SceneRuntime/VansRuntimeWorld.h"
#include "../EngineCore/SceneRuntime/VansRuntimeComponentTypes.h"
#include "../EngineCore/SceneCore/VansSceneDocument.h"
#include "../EngineCore/SceneCore/VansSceneSchema.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <type_traits>
#include <nlohmann/json.hpp>

namespace
{
using namespace Vans;
using namespace Vans::EditorAPI;
using namespace VansGraphics;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
glm::vec3 Vec(Vec3 v) { return {v.x, v.y, v.z}; }
class CameraDevice final : public VansGraphicsDevice
{
public:
    CameraDevice() { m_RenderWidth = 1280; m_RenderHeight = 720; }
    bool BeforeRendering() override { return true; }
    void Rendering() override {}
    void Present() override {}
    void AfterRendering() override {}
    void* GetNativeGraphicsDevice() override { return nullptr; }
    void* GetNativeCommandBuffer() override { return nullptr; }
};
class GeometryNode final : public VansRenderNode
{
public:
    explicit GeometryNode(VkDevice& device) : VansRenderNode(device, OPAQUE_NODE)
    {
        // 无绘制资源的查询 fixture，仍使用真实节点变换与 GPU 网格。
        modelBufferLayout = textureResourceLayout = frameBufferInputLayout = VK_NULL_HANDLE;
    }
};
class GeometryExecutor final : public IVansRenderThreadTransactionExecutor
{
public:
    explicit GeometryExecutor(VansVKDevice& device) : device(device) {}
    bool ExecuteRenderThreadTransaction(std::unique_ptr<IVansRenderThreadTransaction> transaction) override
    { ++reads; return transaction->Execute(device); }
    VansVKDevice& device;
    int reads = 0;
};

class RegistryWindowA final : public VansBaseWindowComponent {};
class RegistryWindowB final : public VansBaseWindowComponent {};
}

bool TestEditorSceneInteractionContract()
{
    try
    {
		std::filesystem::path sourceFile = std::filesystem::path(__FILE__);
		if (sourceFile.is_relative())
			sourceFile = std::filesystem::absolute(sourceFile);
		const std::filesystem::path engineRoot =
			sourceFile.parent_path().parent_path().parent_path();
		static_assert(std::is_base_of_v<IAudioEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IAnimationEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IAnimationPreviewEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IAssetAuthoringEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IAssetEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IGAFEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IGIEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IAIEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IMotionMatchingEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IParticleEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IPcgEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IPlayModeEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IProjectEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IReflectionProbeEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IRenderEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IRuntimeCommandHistoryEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IRuntimeFrameEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IRuntimePhysicsEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IRuntimeSceneEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<ISceneInteractionEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<ISceneSettingsEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IShaderEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IScriptLifecycleEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<ITerrainEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<ITimelineEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IUIEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IVehicleEditorAPI, IEngineEditorAPI>);
		static_assert(std::is_base_of_v<IWaterEditorAPI, IEngineEditorAPI>);
		VansEditorConfiguration editorConfiguration;
		std::string editorConfigurationError;
		const std::filesystem::path editorConfigurationPath =
			engineRoot / "EngineAssets" / "Editor" / "EditorConfiguration.json";
		Check(VansEditorConfiguration::Load(
			editorConfigurationPath, editorConfiguration, editorConfigurationError),
			editorConfigurationError.c_str());
		VansEditorWindowCatalog windowCatalog;
		VansEditorWindowCatalog independentWindowCatalog;
		windowCatalog.ApplyDefaults(editorConfiguration.windowDefaults);
		independentWindowCatalog.ApplyDefaults(editorConfiguration.windowDefaults);
		std::array<bool, static_cast<std::size_t>(VansEditorWindowId::Count)> seenWindowIds{};
		std::size_t generalWindowCount = 0;
		std::size_t animationWindowCount = 0;
		std::size_t renderingWindowCount = 0;
		std::size_t defaultOpenWindowCount = 0;
		for (const VansEditorWindowDescriptor& descriptor : VansEditorWindowCatalog::All())
		{
			const std::size_t index = static_cast<std::size_t>(descriptor.id);
			Check(index < seenWindowIds.size() && !seenWindowIds[index],
				"editor window catalog contains an invalid or duplicate id");
			seenWindowIds[index] = true;
			Check(!descriptor.configurationKey.empty() && !descriptor.menuLabel.empty(),
				"editor window catalog contains an empty configuration key or menu label");
			const bool defaultOpen = editorConfiguration.windowDefaults[index];
			defaultOpenWindowCount += defaultOpen ? 1u : 0u;
			switch (descriptor.menuGroup)
			{
			case VansEditorWindowMenuGroup::General: ++generalWindowCount; break;
			case VansEditorWindowMenuGroup::Animation: ++animationWindowCount; break;
			case VansEditorWindowMenuGroup::Rendering: ++renderingWindowCount; break;
			}
			Check(windowCatalog.IsOpen(descriptor.id) == defaultOpen,
				"editor window catalog did not apply configured default visibility");
		}
		Check(VansEditorWindowCatalog::All().size() == seenWindowIds.size() &&
			generalWindowCount == 9 && animationWindowCount == 2 && renderingWindowCount == 13 &&
			defaultOpenWindowCount == 6,
			"editor window catalog coverage or menu grouping changed unexpectedly");
		for (bool seen : seenWindowIds)
			Check(seen, "editor window catalog does not cover every window id");
		Check(windowCatalog.IsOpen(VansEditorWindowId::Light) &&
			windowCatalog.IsOpen(VansEditorWindowId::Scriptor) &&
			windowCatalog.IsOpen(VansEditorWindowId::Console) &&
			windowCatalog.IsOpen(VansEditorWindowId::UIEditor) &&
			windowCatalog.IsOpen(VansEditorWindowId::Water) &&
			windowCatalog.IsOpen(VansEditorWindowId::Terrain) &&
			!windowCatalog.IsOpen(VansEditorWindowId::Profiler),
			"editor window catalog changed the legacy default-open set");
		*windowCatalog.OpenState(VansEditorWindowId::Profiler) = true;
		Check(windowCatalog.IsOpen(VansEditorWindowId::Profiler) &&
			!independentWindowCatalog.IsOpen(VansEditorWindowId::Profiler),
			"editor window visibility leaked between catalog instances");
		Check(editorConfiguration.fonts.sizePixels == 15.0f &&
			editorConfiguration.fonts.primaryCandidates == std::vector<std::string>{ "consolab.ttf" } &&
			editorConfiguration.fonts.fallbackCandidates ==
				std::vector<std::string>{ "msyh.ttc", "simhei.ttf", "simsun.ttc" } &&
			editorConfiguration.style.windowPadding == std::array<float, 2>{ 10.0f, 10.0f } &&
			editorConfiguration.style.windowRounding == 4.0f &&
			editorConfiguration.toolbar.buttonWidth == 62.0f &&
			editorConfiguration.toolbar.buttonHeight == 18.0f &&
			editorConfiguration.toolbar.buttonSpacing == 4.0f &&
			editorConfiguration.packagePlatform == Vans::VansGamePackagePlatform::Windows,
			"built-in Editor configuration changed legacy fonts, layout, toolbar, or platform");
		std::ifstream editorConfigurationInput(editorConfigurationPath, std::ios::binary);
		nlohmann::ordered_json invalidEditorConfiguration =
			nlohmann::ordered_json::parse(editorConfigurationInput);
		invalidEditorConfiguration["windows"].erase("GI");
		VansEditorConfiguration rejectedConfiguration;
		Check(!VansEditorConfiguration::Decode(
			invalidEditorConfiguration.dump(), rejectedConfiguration, editorConfigurationError),
			"Editor configuration accepted a missing window default");
		std::ifstream secondEditorConfigurationInput(editorConfigurationPath, std::ios::binary);
		invalidEditorConfiguration = nlohmann::ordered_json::parse(secondEditorConfigurationInput);
		invalidEditorConfiguration["package"]["platform"] = "Unsupported";
		Check(!VansEditorConfiguration::Decode(
			invalidEditorConfiguration.dump(), rejectedConfiguration, editorConfigurationError),
			"Editor configuration accepted an unsupported package platform");

		VansEditorWindowRegistry windowRegistry;
		auto& registryWindowA = windowRegistry.Add<RegistryWindowA>();
		auto& registryWindowB = windowRegistry.Add<RegistryWindowB>();
		Check(windowRegistry.Size() == 2 &&
			windowRegistry.All()[0].get() == &registryWindowA &&
			windowRegistry.All()[1].get() == &registryWindowB &&
			windowRegistry.Find<RegistryWindowA>() == &registryWindowA &&
			windowRegistry.Find<RegistryWindowB>() == &registryWindowB,
			"editor window registry changed insertion order or typed lookup");
		bool duplicateWindowRejected = false;
		try { windowRegistry.Add<RegistryWindowA>(); }
		catch (const std::logic_error&) { duplicateWindowRejected = true; }
		Check(duplicateWindowRejected && windowRegistry.Size() == 2,
			"editor window registry accepted a duplicate concrete window type");
		windowRegistry.Clear();
		Check(windowRegistry.Empty() && windowRegistry.Find<RegistryWindowA>() == nullptr,
			"editor window registry did not clear ordered and typed ownership together");

		std::array<bool, 20> seenAssetCreationKinds{};
		std::array<std::size_t, 5> assetMenuGroupCounts{};
		for (const VansEditorAssetMenuEntry& entry :
			VansEditorShellMenu::AssetCreationEntries())
		{
			const std::size_t kind = static_cast<std::size_t>(entry.kind);
			const std::size_t group = static_cast<std::size_t>(entry.group);
			Check(!entry.label.empty() && kind < seenAssetCreationKinds.size() &&
				!seenAssetCreationKinds[kind] && group < assetMenuGroupCounts.size(),
				"editor asset menu contains an empty label, duplicate kind, or invalid group");
			seenAssetCreationKinds[kind] = true;
			++assetMenuGroupCounts[group];
		}
		Check(assetMenuGroupCounts == std::array<std::size_t, 5>{ 1, 11, 1, 2, 3 },
			"editor asset menu changed its root/gameplay/rendering/animation/audio grouping");

		int packageBuildCalls = 0;
		bool packageBuildSucceeds = true;
		Vans::VansGamePackageRequest capturedPackageRequest;
		VansEditorPackageSession packageSession(
			[&](const Vans::VansGamePackageRequest& request)
			{
				++packageBuildCalls;
				capturedPackageRequest = request;
				Vans::VansGamePackageResult result;
				result.success = packageBuildSucceeds;
				result.message = packageBuildSucceeds ? "package complete" : "package failed";
				result.outputPath = "Builds/Test";
				return result;
			});
		VansEditorPackageContext packageContext;
		packageContext.request.projectRootPath = "D:/Project";
		packageContext.request.engineRootPath = "D:/Engine";
		packageContext.request.scenePath = "Scenes/Test.vscene";
		packageContext.sceneDirty = true;
		packageContext.assetsDirty = true;
		packageContext.projectDocumentsDirty = true;
		Check(packageSession.Execute(packageContext).outcome ==
			VansEditorPackageOutcome::DirtyScene && packageBuildCalls == 0,
			"package session did not reject dirty Scene before other documents");
		packageContext.sceneDirty = false;
		Check(packageSession.Execute(packageContext).outcome ==
			VansEditorPackageOutcome::DirtyAssets && packageBuildCalls == 0,
			"package session did not reject dirty assets before project documents");
		packageContext.assetsDirty = false;
		Check(packageSession.Execute(packageContext).outcome ==
			VansEditorPackageOutcome::DirtyProjectDocuments && packageBuildCalls == 0,
			"package session did not reject dirty project documents");
		packageContext.projectDocumentsDirty = false;
		Check(packageSession.Execute(packageContext).Succeeded() && packageBuildCalls == 1 &&
			capturedPackageRequest.projectRootPath == packageContext.request.projectRootPath &&
			capturedPackageRequest.engineRootPath == packageContext.request.engineRootPath &&
			capturedPackageRequest.scenePath == packageContext.request.scenePath,
			"package session did not forward the clean request to its build operation");
		packageContext.sceneDirty = true;
		const VansEditorPackageStatus& dirtyAfterSuccess = packageSession.Execute(packageContext);
		Check(dirtyAfterSuccess.outcome == VansEditorPackageOutcome::DirtyScene &&
			dirtyAfterSuccess.outputPath.empty() && packageBuildCalls == 1,
			"package session retained stale output or built after a dirty rejection");
		packageContext.sceneDirty = false;
		packageBuildSucceeds = false;
		const VansEditorPackageStatus& failedPackage = packageSession.Execute(packageContext);
		Check(failedPackage.outcome == VansEditorPackageOutcome::BuildFailed &&
			failedPackage.message == "package failed" &&
			failedPackage.outputPath == "Builds/Test" && packageBuildCalls == 2,
			"package session did not retain the builder failure result");

		const VansEditorPlayToolbarState unavailableEdit =
			VansEditorPlayToolbar::Resolve(EnginePlayState::Edit, false);
		const VansEditorPlayToolbarState editingToolbar =
			VansEditorPlayToolbar::Resolve(EnginePlayState::Edit, true);
		const VansEditorPlayToolbarState playingToolbar =
			VansEditorPlayToolbar::Resolve(EnginePlayState::Play, true);
		const VansEditorPlayToolbarState pausedToolbar =
			VansEditorPlayToolbar::Resolve(EnginePlayState::Pause, true);
		Check(!unavailableEdit.playEnabled && !unavailableEdit.pauseResumeEnabled &&
			!unavailableEdit.stopEnabled,
			"play toolbar enabled a command without a ready Scene");
		Check(editingToolbar.playEnabled && !editingToolbar.pauseResumeEnabled &&
			!editingToolbar.stopEnabled && !editingToolbar.showResume &&
			editingToolbar.pauseResumeCommand == VansEditorPlayCommand::Pause,
			"play toolbar changed Edit-state policy");
		Check(!playingToolbar.playEnabled && playingToolbar.pauseResumeEnabled &&
			playingToolbar.stopEnabled && !playingToolbar.showResume &&
			playingToolbar.pauseResumeCommand == VansEditorPlayCommand::Pause,
			"play toolbar changed Playing-state policy");
		Check(!pausedToolbar.playEnabled && pausedToolbar.pauseResumeEnabled &&
			pausedToolbar.stopEnabled && pausedToolbar.showResume &&
			pausedToolbar.pauseResumeCommand == VansEditorPlayCommand::Resume,
			"play toolbar changed Paused-state policy");

		VansEditorProjectSession projectSession;
		VansEditorDebugViewState debugViewState;
		VansEditorDebugViewState independentDebugViewState;
		Check(!debugViewState.wireframeMode && !debugViewState.vehicleDebugGizmos &&
			!debugViewState.hiZCullDebugVisualization && !debugViewState.skeletonDebugGizmos &&
			debugViewState.skeletonDebugSelectedOnly && !debugViewState.skeletonDebugShowNames &&
			debugViewState.skeletonDebugShowRetargetSource,
			"debug-view state changed the legacy defaults");
		debugViewState.skeletonDebugGizmos = true;
		Check(debugViewState.skeletonDebugGizmos && !independentDebugViewState.skeletonDebugGizmos,
			"debug-view state leaked between owners");

		VansEditorShellShortcutState shortcutState;
		shortcutState.editingMode = true;
		shortcutState.controlDown = true;
		shortcutState.shiftDown = true;
		shortcutState.sceneDocumentReady = true;
		shortcutState.canUndo = true;
		shortcutState.canRedo = true;
		shortcutState.savePressed = true;
		shortcutState.undoPressed = true;
		shortcutState.redoPressed = true;
		auto shortcut = VansEditorShellCommandController::ResolveShortcut(shortcutState);
		Check(shortcut.available && shortcut.command.type == VansEditorShellCommandType::SaveAll,
			"Shell shortcut changed Ctrl+Shift+S priority");
		shortcutState.shiftDown = false;
		shortcut = VansEditorShellCommandController::ResolveShortcut(shortcutState);
		Check(shortcut.available && shortcut.command.type == VansEditorShellCommandType::SaveScene,
			"Shell shortcut changed Ctrl+S priority");
		shortcutState.savePressed = false;
		shortcut = VansEditorShellCommandController::ResolveShortcut(shortcutState);
		Check(shortcut.available && shortcut.command.type == VansEditorShellCommandType::Undo,
			"Shell shortcut changed Ctrl+Z priority");
		shortcutState.undoPressed = false;
		shortcut = VansEditorShellCommandController::ResolveShortcut(shortcutState);
		Check(shortcut.available && shortcut.command.type == VansEditorShellCommandType::Redo,
			"Shell shortcut changed Ctrl+Y routing");
		shortcutState.wantTextInput = true;
		Check(!VansEditorShellCommandController::ResolveShortcut(shortcutState).available,
			"Shell shortcut ignored text-input capture");

		std::vector<std::string> shellCommandSteps;
		VansEditorShellCommandOperations shellCommandOperations;
		shellCommandOperations.executeAuthoringCommand = [&](VansEditorAuthoringCommand command)
		{
			shellCommandSteps.push_back("authoring:" + std::to_string(static_cast<int>(command)));
		};
		shellCommandOperations.requestAssetCreation = [&](ProjectAssetCreationKind kind)
		{
			shellCommandSteps.push_back("asset:" + std::to_string(static_cast<int>(kind)));
		};
		shellCommandOperations.undo = [&] { shellCommandSteps.push_back("undo"); };
		shellCommandOperations.redo = [&] { shellCommandSteps.push_back("redo"); };
		shellCommandOperations.setSceneAnimationPreviewOpen = [&](bool open)
		{
			shellCommandSteps.push_back(open ? "preview:open" : "preview:close");
		};
		shellCommandOperations.openSelectedAnimationGraph = [&]
		{
			shellCommandSteps.push_back("animation:open");
		};
		Check(VansEditorShellCommandController::Execute(
			{ VansEditorShellCommandType::SaveAll }, shellCommandOperations) &&
			VansEditorShellCommandController::Execute(
				{ VansEditorShellCommandType::Undo }, shellCommandOperations) &&
			VansEditorShellCommandController::Execute(
				{ VansEditorShellCommandType::Redo }, shellCommandOperations) &&
			VansEditorShellCommandController::Execute(
				{ VansEditorShellCommandType::SetSceneAnimationPreviewOpen,
					ProjectAssetCreationKind::Timeline, true }, shellCommandOperations) &&
			VansEditorShellCommandController::Execute(
				{ VansEditorShellCommandType::OpenSelectedAnimationGraph }, shellCommandOperations),
			"Shell command controller rejected configured operations");
		Check(shellCommandSteps == std::vector<std::string>{
			"authoring:" + std::to_string(static_cast<int>(VansEditorAuthoringCommand::SaveAll)),
			"undo", "redo", "preview:open", "animation:open" },
			"Shell command controller changed semantic dispatch order");
		Check(!VansEditorShellCommandController::Execute(
			{ VansEditorShellCommandType::CreateAsset }, {}),
			"Shell command controller accepted a missing operation");

		Check(!projectSession.IsLoaded() && !projectSession.HasPendingRequest() &&
			projectSession.Selector() == nullptr,
			"project session did not start empty");
		projectSession.Initialize();
		Check(projectSession.Selector() != nullptr && !projectSession.IsLoaded(),
			"project session did not initialize its selector in unloaded state");
		projectSession.QueueOpen("D:/Projects/Existing");
		const VansEditorPendingProjectRequest openRequest = projectSession.TakePendingRequest();
		Check(!openRequest.createNew && openRequest.projectPath == "D:/Projects/Existing" &&
			openRequest.projectName.empty() && !projectSession.HasPendingRequest(),
			"project session changed open-request take semantics");
		projectSession.QueueCreate("D:/Projects/New", "NewProject");
		const VansEditorPendingProjectRequest createRequest = projectSession.TakePendingRequest();
		Check(createRequest.createNew && createRequest.projectPath == "D:/Projects/New" &&
			createRequest.projectName == "NewProject" && !projectSession.HasPendingRequest(),
			"project session changed create-request take semantics");
		projectSession.MarkLoaded(true);
		projectSession.QueueOpen("D:/Projects/Replacement");
		projectSession.Shutdown();
		Check(!projectSession.IsLoaded() && !projectSession.HasPendingRequest() &&
			projectSession.Selector() == nullptr,
			"project session shutdown retained selector, loaded, or pending state");

		VansEditorSceneLoadSession sceneLoadSession;
		Check(sceneLoadSession.CurrentScenePath().empty() &&
			!sceneLoadSession.HasPendingRequest() &&
			sceneLoadSession.PendingMode() == RuntimeSceneLoadMode::Editor,
			"Scene load session did not start in empty Editor mode");
		sceneLoadSession.Request(RuntimeSceneLoadMode::Runtime, "Scenes/Play.vscene");
		Check(sceneLoadSession.HasPendingRequest() &&
			sceneLoadSession.PendingPath() == "Scenes/Play.vscene" &&
			sceneLoadSession.PendingMode() == RuntimeSceneLoadMode::Runtime,
			"Scene load session changed explicit-mode request state");
		sceneLoadSession.ClearPending();
		sceneLoadSession.Request("Scenes/Next.vscene");
		Check(sceneLoadSession.PendingPath() == "Scenes/Next.vscene" &&
			sceneLoadSession.PendingMode() == RuntimeSceneLoadMode::Runtime,
			"Scene path-only request did not preserve the legacy pending mode");
		sceneLoadSession.MarkLoaded(sceneLoadSession.PendingPath());
		sceneLoadSession.ClearPending();
		Check(sceneLoadSession.CurrentScenePath() == "Scenes/Next.vscene" &&
			!sceneLoadSession.HasPendingRequest(),
			"Scene load session did not separate committed and pending paths");
		sceneLoadSession.Reset();
		Check(sceneLoadSession.CurrentScenePath().empty() &&
			!sceneLoadSession.HasPendingRequest() &&
			sceneLoadSession.PendingMode() == RuntimeSceneLoadMode::Editor,
			"Scene load session reset retained path or mode state");

		std::vector<std::string> sceneLoadSteps;
		bool sceneDocumentLoadSucceeds = true;
		bool prefabRefreshSucceeds = true;
		VansEditorRuntimeSceneLoadStatus runtimeSceneLoadStatus{true, 73};
		VansEditorSceneLoadOperations sceneLoadOperations;
		sceneLoadOperations.clearPendingRequest = [&]
		{
			sceneLoadSteps.push_back("request:clear");
		};
		sceneLoadOperations.prepareDocument = [&](const std::string& path)
		{
			sceneLoadSteps.push_back("document:prepare:" + path);
			return sceneDocumentLoadSucceeds;
		};
		sceneLoadOperations.refreshPrefabView = [&]
		{
			sceneLoadSteps.push_back("prefab:refresh");
			return prefabRefreshSucceeds;
		};
		sceneLoadOperations.loadRuntimeScene = [&](RuntimeSceneLoadMode mode)
		{
			sceneLoadSteps.push_back(mode == RuntimeSceneLoadMode::Editor
				? "runtime:load:editor" : "runtime:load:play");
			return runtimeSceneLoadStatus;
		};
		sceneLoadOperations.markLoaded = [&](const std::string& path)
		{
			sceneLoadSteps.push_back("loaded:" + path);
		};
		sceneLoadOperations.commitPreparedDocument = [&]
		{
			sceneLoadSteps.push_back("document:commit");
		};
		sceneLoadOperations.detachEditorViewportCameras = [&]
		{
			sceneLoadSteps.push_back("camera:detach");
		};
		sceneLoadOperations.setTimePaused = [&](bool paused)
		{
			sceneLoadSteps.push_back(paused ? "time:paused" : "time:running");
		};
		sceneLoadOperations.installRuntimeVehiclePhysicsStepCallback = [&]
		{
			sceneLoadSteps.push_back("vehicle:install");
		};
		sceneLoadOperations.startRuntimePhysicsIfNeeded = [&]
		{
			sceneLoadSteps.push_back("physics:start");
		};
		sceneLoadOperations.setPlayState = [&](EnginePlayState state)
		{
			sceneLoadSteps.push_back("state:" + std::to_string(static_cast<int>(state)));
		};
		sceneLoadOperations.setCurrentProjectScenePath = [&](const std::string& path)
		{
			sceneLoadSteps.push_back("project_scene:" + path);
		};
		sceneLoadOperations.logInfo = [&](const std::string& message)
		{
			sceneLoadSteps.push_back("info:" + message);
		};
		sceneLoadOperations.logWarning = [&](const std::string& message)
		{
			sceneLoadSteps.push_back("warning:" + message);
		};
		sceneLoadOperations.logError = [&](const std::string& message)
		{
			sceneLoadSteps.push_back("error:" + message);
		};

		VansEditorSceneLoadContext sceneLoadContext;
		Check(VansEditorSceneLoadController::Execute(
			sceneLoadContext, sceneLoadOperations) == VansEditorSceneLoadOutcome::NoRequest &&
			sceneLoadSteps.empty(),
			"Scene load controller changed the empty-request no-op");
		sceneLoadContext.pendingScenePath = "Scenes/Next.vscene";
		sceneLoadContext.hasPrefabSession = true;
		Check(VansEditorSceneLoadController::Execute(
			sceneLoadContext, sceneLoadOperations) ==
				VansEditorSceneLoadOutcome::RejectedPrefabSession &&
			sceneLoadSteps == std::vector<std::string>{
				"warning:[Prefab] Close Prefab mode before switching scenes or playing",
				"request:clear" },
			"Scene load controller changed the Prefab rejection order");
		sceneLoadSteps.clear();
		sceneLoadContext.hasPrefabSession = false;
		sceneLoadContext.sceneDirty = true;
		sceneLoadContext.currentScenePath = "Scenes/Current.vscene";
		Check(VansEditorSceneLoadController::Execute(
			sceneLoadContext, sceneLoadOperations) ==
				VansEditorSceneLoadOutcome::RejectedDirtyScene &&
			sceneLoadSteps == std::vector<std::string>{
				"warning:[Editor] Scene switch cancelled: save or undo current scene changes first",
				"request:clear" },
			"Scene load controller changed the dirty-Scene rejection order");
		sceneLoadSteps.clear();
		sceneLoadContext.sceneDirty = false;
		sceneLoadContext.currentScenePath.clear();
		sceneDocumentLoadSucceeds = false;
		Check(VansEditorSceneLoadController::Execute(
			sceneLoadContext, sceneLoadOperations) ==
				VansEditorSceneLoadOutcome::DocumentLoadFailed &&
			sceneLoadSteps == std::vector<std::string>{
				"info:[Editor] Loading deferred scene: Scenes/Next.vscene [mode=Editor]",
				"document:prepare:Scenes/Next.vscene",
				"error:[Editor] Scene document validation failed before runtime scene switch: Scenes/Next.vscene",
				"request:clear" },
			"Scene load controller changed document failure cleanup");
		sceneLoadSteps.clear();
		sceneDocumentLoadSucceeds = true;
		prefabRefreshSucceeds = false;
		Check(VansEditorSceneLoadController::Execute(
			sceneLoadContext, sceneLoadOperations) ==
				VansEditorSceneLoadOutcome::PrefabRefreshFailed &&
			sceneLoadSteps == std::vector<std::string>{
				"info:[Editor] Loading deferred scene: Scenes/Next.vscene [mode=Editor]",
				"document:prepare:Scenes/Next.vscene", "prefab:refresh", "request:clear" },
			"Scene load controller changed Prefab refresh failure cleanup");
		sceneLoadSteps.clear();
		prefabRefreshSucceeds = true;
		sceneLoadContext.canReuseCurrentDocument = true;
		runtimeSceneLoadStatus = {false, 0};
		Check(VansEditorSceneLoadController::Execute(
			sceneLoadContext, sceneLoadOperations) ==
				VansEditorSceneLoadOutcome::RuntimeLoadFailed &&
			sceneLoadSteps == std::vector<std::string>{
				"info:[Editor] Loading deferred scene: Scenes/Next.vscene [mode=Editor]",
				"prefab:refresh", "runtime:load:editor",
				"error:[Editor] Runtime scene load request failed: Scenes/Next.vscene",
				"request:clear" },
			"Scene load controller published state after Runtime load failure");
		sceneLoadSteps.clear();
		sceneLoadContext.canReuseCurrentDocument = false;
		runtimeSceneLoadStatus = {true, 73};
		Check(VansEditorSceneLoadController::Execute(
			sceneLoadContext, sceneLoadOperations) ==
				VansEditorSceneLoadOutcome::LoadedEditor &&
			sceneLoadSteps == std::vector<std::string>{
				"info:[Editor] Loading deferred scene: Scenes/Next.vscene [mode=Editor]",
				"document:prepare:Scenes/Next.vscene", "prefab:refresh", "runtime:load:editor",
				"loaded:Scenes/Next.vscene", "document:commit",
				"info:[SceneDocument] Document ready: Scenes/Next.vscene [revision=73]",
				"camera:detach", "time:paused",
				"state:" + std::to_string(static_cast<int>(EnginePlayState::Edit)),
				"project_scene:Scenes/Next.vscene", "request:clear" },
			"Scene load controller changed Editor publish/mode ordering");
		sceneLoadSteps.clear();
		sceneLoadContext.mode = RuntimeSceneLoadMode::Runtime;
		sceneLoadContext.canReuseCurrentDocument = true;
		Check(VansEditorSceneLoadController::Execute(
			sceneLoadContext, sceneLoadOperations) ==
				VansEditorSceneLoadOutcome::LoadedRuntime &&
			sceneLoadSteps == std::vector<std::string>{
				"info:[Editor] Loading deferred scene: Scenes/Next.vscene [mode=Runtime]",
				"prefab:refresh", "runtime:load:play", "loaded:Scenes/Next.vscene",
				"time:running", "vehicle:install", "physics:start",
				"state:" + std::to_string(static_cast<int>(EnginePlayState::Play)),
				"info:[Editor] Scene started playing (Runtime mode)",
				"project_scene:Scenes/Next.vscene", "request:clear" },
			"Scene load controller changed Runtime publish/mode ordering");

		std::vector<std::string> authoringCommandSteps;
		bool prefabSaveSucceeds = true;
		bool sceneSaveSucceeds = true;
		bool projectSaveSucceeds = true;
		VansEditorAuthoringSaveStatus selectedAssetSaveStatus{true, true};
		VansEditorAuthoringSaveStatus allAssetSaveStatus{true, true};
		VansEditorAuthoringCommandOperations authoringCommandOperations;
		authoringCommandOperations.savePrefab = [&]
		{
			authoringCommandSteps.push_back("save:prefab");
			return prefabSaveSucceeds;
		};
		authoringCommandOperations.saveSceneAndOwnedAssets = [&]
		{
			authoringCommandSteps.push_back("save:scene");
			return sceneSaveSucceeds;
		};
		authoringCommandOperations.saveSelectedAsset = [&]
		{
			authoringCommandSteps.push_back("save:selected_asset");
			return selectedAssetSaveStatus;
		};
		authoringCommandOperations.saveAllDirtyAssets = [&]
		{
			authoringCommandSteps.push_back("save:all_assets");
			return allAssetSaveStatus;
		};
		authoringCommandOperations.saveProjectDocuments = [&]
		{
			authoringCommandSteps.push_back("save:project");
			return projectSaveSucceeds;
		};
		authoringCommandOperations.reloadCurrentSceneForEditing = [&]
		{
			authoringCommandSteps.push_back("scene:reload");
		};
		authoringCommandOperations.requestExit = [&]
		{
			authoringCommandSteps.push_back("exit:request");
		};
		authoringCommandOperations.logWarning = [&](const std::string& message)
		{
			authoringCommandSteps.push_back("warning:" + message);
		};
		VansEditorAuthoringCommandContext authoringCommandContext;
		Check(VansEditorAuthoringCommandController::Execute(
			VansEditorAuthoringCommand::SaveScene,
			authoringCommandContext,
			authoringCommandOperations) == VansEditorAuthoringCommandOutcome::Executed &&
			authoringCommandSteps == std::vector<std::string>{"save:scene"},
			"authoring command router changed normal Scene save dispatch");
		authoringCommandSteps.clear();
		authoringCommandContext.hasPrefabSession = true;
		Check(VansEditorAuthoringCommandController::Execute(
			VansEditorAuthoringCommand::SaveScene,
			authoringCommandContext,
			authoringCommandOperations) == VansEditorAuthoringCommandOutcome::Executed &&
			authoringCommandSteps == std::vector<std::string>{"save:prefab"},
			"authoring command router bypassed the Prefab save branch");
		authoringCommandSteps.clear();
		authoringCommandContext.hasPrefabSession = false;
		Check(VansEditorAuthoringCommandController::Execute(
			VansEditorAuthoringCommand::SaveAsset,
			authoringCommandContext,
			authoringCommandOperations) == VansEditorAuthoringCommandOutcome::Executed &&
			authoringCommandSteps == std::vector<std::string>{
				"save:selected_asset", "scene:reload" },
			"authoring command router changed selected Asset reload policy");
		authoringCommandSteps.clear();
		authoringCommandContext.assetsDirty = true;
		authoringCommandContext.projectDocumentsDirty = true;
		Check(VansEditorAuthoringCommandController::Execute(
			VansEditorAuthoringCommand::SaveAll,
			authoringCommandContext,
			authoringCommandOperations) == VansEditorAuthoringCommandOutcome::Executed &&
			authoringCommandSteps == std::vector<std::string>{
				"save:scene", "save:all_assets", "scene:reload", "save:project" },
			"authoring command router changed Save All ordering");
		authoringCommandSteps.clear();
		allAssetSaveStatus = {false, false};
		Check(VansEditorAuthoringCommandController::Execute(
			VansEditorAuthoringCommand::SaveAll,
			authoringCommandContext,
			authoringCommandOperations) == VansEditorAuthoringCommandOutcome::SaveFailed &&
			authoringCommandSteps == std::vector<std::string>{
				"save:scene", "save:all_assets", "save:project" },
			"authoring command router stopped before Project save after Asset failure");
		authoringCommandSteps.clear();
		allAssetSaveStatus = {true, true};
		sceneSaveSucceeds = false;
		Check(VansEditorAuthoringCommandController::Execute(
			VansEditorAuthoringCommand::SaveAll,
			authoringCommandContext,
			authoringCommandOperations) == VansEditorAuthoringCommandOutcome::SaveFailed &&
			authoringCommandSteps == std::vector<std::string>{"save:scene"},
			"authoring command router continued Save All after Scene failure");
		sceneSaveSucceeds = true;
		authoringCommandSteps.clear();
		authoringCommandContext.sceneDirty = true;
		Check(VansEditorAuthoringCommandController::Execute(
			VansEditorAuthoringCommand::Exit,
			authoringCommandContext,
			authoringCommandOperations) ==
				VansEditorAuthoringCommandOutcome::ExitRejectedDirtyScene &&
			authoringCommandSteps == std::vector<std::string>{
				"warning:[Editor] Exit cancelled: save or undo current scene changes first" },
			"authoring command router changed exit dirty priority");
		authoringCommandSteps.clear();
		authoringCommandContext.sceneDirty = false;
		authoringCommandContext.assetsDirty = false;
		authoringCommandContext.projectDocumentsDirty = false;
		Check(VansEditorAuthoringCommandController::Execute(
			VansEditorAuthoringCommand::Exit,
			authoringCommandContext,
			authoringCommandOperations) == VansEditorAuthoringCommandOutcome::Executed &&
			authoringCommandSteps == std::vector<std::string>{"exit:request"},
			"authoring command router rejected a clean exit");

		std::vector<std::string> projectSwitchSteps;
		Vans::EditorAPI::ProjectOpenRequest capturedProjectOpenRequest;
		bool projectOpenSucceeds = true;
		VansEditorProjectSwitchOperations projectSwitchOperations;
		projectSwitchOperations.clearPendingRequest = [&]
		{
			projectSwitchSteps.push_back("request:clear");
		};
		projectSwitchOperations.setTimePaused = [&](bool paused)
		{
			projectSwitchSteps.push_back(paused ? "time:paused" : "time:running");
		};
		projectSwitchOperations.pauseRuntimePhysics = [&] { projectSwitchSteps.push_back("physics:paused"); };
		projectSwitchOperations.unloadRuntimeScene = [&] { projectSwitchSteps.push_back("runtime:scene_unload"); };
		projectSwitchOperations.unloadRuntimeProjectResources = [&] { projectSwitchSteps.push_back("runtime:project_unload"); };
		projectSwitchOperations.closeProject = [&] { projectSwitchSteps.push_back("project:close"); };
		projectSwitchOperations.clearAssetHistories = [&] { projectSwitchSteps.push_back("assets:histories_clear"); };
		projectSwitchOperations.clearAssetDocuments = [&] { projectSwitchSteps.push_back("assets:documents_clear"); };
		projectSwitchOperations.markProjectLoaded = [&](bool loaded)
		{
			projectSwitchSteps.push_back(loaded ? "loaded:true" : "loaded:false");
		};
		projectSwitchOperations.clearSceneSession = [&] { projectSwitchSteps.push_back("scene:session_clear"); };
		projectSwitchOperations.openProject = [&](const Vans::EditorAPI::ProjectOpenRequest& request)
		{
			capturedProjectOpenRequest = request;
			projectSwitchSteps.push_back("project:open");
			Vans::EditorAPI::ProjectOpenResult result;
			result.success = projectOpenSucceeds;
			result.projectRootPath = "D:/Opened";
			return result;
		};
		projectSwitchOperations.logInfo = [&](const std::string& message)
		{
			projectSwitchSteps.push_back("info:" + message);
		};
		projectSwitchOperations.logWarning = [&](const std::string& message)
		{
			projectSwitchSteps.push_back("warning:" + message);
		};
		projectSwitchOperations.logError = [&](const std::string& message)
		{
			projectSwitchSteps.push_back("error:" + message);
		};

		VansEditorProjectSwitchContext projectSwitchContext;
		projectSwitchContext.request.projectPath = "D:/Projects/New";
		projectSwitchContext.request.projectName = "NewProject";
		projectSwitchContext.request.createNew = true;
		projectSwitchContext.hasPrefabSession = true;
		projectSwitchContext.sceneDirty = true;
		projectSwitchContext.assetsDirty = true;
		projectSwitchContext.projectDocumentsDirty = true;
		Check(VansEditorProjectSwitchController::Execute(
			projectSwitchContext, projectSwitchOperations).outcome ==
			VansEditorProjectSwitchOutcome::RejectedPrefabSession &&
			projectSwitchSteps == std::vector<std::string>{
				"warning:[Prefab] Close Prefab mode before switching projects",
				"request:clear" },
			"project switch did not reject Prefab before dirty documents");
		projectSwitchSteps.clear();
		projectSwitchContext.hasPrefabSession = false;
		Check(VansEditorProjectSwitchController::Execute(
			projectSwitchContext, projectSwitchOperations).outcome ==
			VansEditorProjectSwitchOutcome::RejectedDirtyScene &&
			projectSwitchSteps == std::vector<std::string>{
				"warning:[Editor] Project switch cancelled: save or undo current scene changes first",
				"request:clear" },
			"project switch changed Scene-dirty rejection order");
		projectSwitchSteps.clear();
		projectSwitchContext.sceneDirty = false;
		Check(VansEditorProjectSwitchController::Execute(
			projectSwitchContext, projectSwitchOperations).outcome ==
			VansEditorProjectSwitchOutcome::RejectedDirtyAssets &&
			projectSwitchSteps == std::vector<std::string>{
				"warning:[Editor] Project switch cancelled: save or revert dirty asset changes first",
				"request:clear" },
			"project switch did not reject dirty assets before project documents");
		projectSwitchSteps.clear();
		projectSwitchContext.assetsDirty = false;
		Check(VansEditorProjectSwitchController::Execute(
			projectSwitchContext, projectSwitchOperations).outcome ==
			VansEditorProjectSwitchOutcome::RejectedDirtyProjectDocuments &&
			projectSwitchSteps == std::vector<std::string>{
				"warning:[Editor] Project switch cancelled: save or revert dirty project documents first",
				"request:clear" },
			"project switch changed project-document dirty rejection");
		projectSwitchSteps.clear();
		projectSwitchContext.projectDocumentsDirty = false;
		const VansEditorProjectSwitchResult openedProject =
			VansEditorProjectSwitchController::Execute(
				projectSwitchContext, projectSwitchOperations);
		Check(openedProject.Opened() && openedProject.projectOpenResult.projectRootPath == "D:/Opened" &&
			capturedProjectOpenRequest.projectPath == "D:/Projects/New" &&
			capturedProjectOpenRequest.projectName == "NewProject" &&
			capturedProjectOpenRequest.createNew &&
			projectSwitchSteps == std::vector<std::string>{
				"request:clear",
				"info:[Editor] Processing pending project load: D:/Projects/New",
				"time:paused", "physics:paused", "runtime:scene_unload",
				"runtime:project_unload", "project:close", "assets:histories_clear",
				"assets:documents_clear", "loaded:false", "scene:session_clear",
				"info:[Editor] Creating project 'NewProject' at D:/Projects/New",
				"project:open", "loaded:true", "info:[Editor] Project load completed" },
			"project switch changed core unload/open/publish order");
		projectSwitchSteps.clear();
		projectOpenSucceeds = false;
		const VansEditorProjectSwitchResult failedProject =
			VansEditorProjectSwitchController::Execute(
				projectSwitchContext, projectSwitchOperations);
		Check(failedProject.outcome == VansEditorProjectSwitchOutcome::OpenFailed &&
			std::find(projectSwitchSteps.begin(), projectSwitchSteps.end(), "loaded:true") ==
				projectSwitchSteps.end() &&
			projectSwitchSteps.back() ==
				"error:[Editor] Pending project load failed: D:/Projects/New",
			"project switch published loaded state after OpenProject failure");

		std::vector<std::string> playCommandSteps;
		VansEditorPlayCommandOperations playOperations;
		playOperations.setTimePaused = [&](bool paused)
		{
			playCommandSteps.push_back(paused ? "time:paused" : "time:running");
		};
		playOperations.pauseRuntimePhysics = [&]
		{
			playCommandSteps.push_back("physics:paused");
		};
		playOperations.resumeRuntimePhysics = [&]
		{
			playCommandSteps.push_back("physics:running");
		};
		playOperations.setPlayState = [&](EnginePlayState state)
		{
			playCommandSteps.push_back("state:" + std::to_string(static_cast<int>(state)));
		};
		playOperations.requestSceneLoad = [&](RuntimeSceneLoadMode mode, const std::string& path)
		{
			playCommandSteps.push_back(std::string("scene:") +
				(mode == RuntimeSceneLoadMode::Runtime ? "runtime:" : "editor:") + path);
		};
		playOperations.logInfo = [&](const std::string& message)
		{
			playCommandSteps.push_back("info:" + message);
		};
		playOperations.logWarning = [&](const std::string& message)
		{
			playCommandSteps.push_back("warning:" + message);
		};

		VansEditorPlayCommandContext playContext;
		playContext.playState = EnginePlayState::Edit;
		playContext.currentScenePath = "Scenes/Test.vscene";
		playContext.hasPrefabSession = true;
		Check(VansEditorPlayCommandController::Execute(
			VansEditorPlayCommand::Play, playContext, playOperations) ==
			VansEditorPlayCommandOutcome::RejectedPrefabSession &&
			playCommandSteps == std::vector<std::string>{
				"warning:[Prefab] Close Prefab mode before playing the scene" },
			"play command changed the Prefab-session rejection");
		playCommandSteps.clear();
		playContext.hasPrefabSession = false;
		playContext.currentScenePath.clear();
		Check(VansEditorPlayCommandController::Execute(
			VansEditorPlayCommand::Play, playContext, playOperations) ==
			VansEditorPlayCommandOutcome::RejectedMissingScene &&
			playCommandSteps == std::vector<std::string>{
				"warning:[Editor] OnPlay: no scene loaded, cannot start" },
			"play command changed the missing-Scene rejection");
		playCommandSteps.clear();
		playContext.currentScenePath = "Scenes/Test.vscene";
		playContext.sceneDirty = true;
		Check(VansEditorPlayCommandController::Execute(
			VansEditorPlayCommand::Play, playContext, playOperations) ==
			VansEditorPlayCommandOutcome::RejectedDirtyScene &&
			playCommandSteps == std::vector<std::string>{
				"warning:[Editor] Save or undo scene changes before entering Play mode" },
			"play command changed the dirty-Scene rejection");
		playCommandSteps.clear();
		playContext.sceneDirty = false;
		Check(VansEditorPlayCommandController::Execute(
			VansEditorPlayCommand::Play, playContext, playOperations) ==
			VansEditorPlayCommandOutcome::Executed &&
			playCommandSteps == std::vector<std::string>{
				"info:[Editor] Play: reloading scene in Runtime mode: Scenes/Test.vscene",
				"scene:runtime:Scenes/Test.vscene" },
			"play command changed runtime reload scheduling order");
		playCommandSteps.clear();
		playContext.playState = EnginePlayState::Play;
		Check(VansEditorPlayCommandController::Execute(
			VansEditorPlayCommand::Pause, playContext, playOperations) ==
			VansEditorPlayCommandOutcome::Executed &&
			playCommandSteps == std::vector<std::string>{
				"time:paused", "physics:paused",
				"state:" + std::to_string(static_cast<int>(EnginePlayState::Pause)),
				"info:[Editor] Scene paused" },
			"pause command changed Timer/Physics/state ordering");
		playCommandSteps.clear();
		playContext.playState = EnginePlayState::Pause;
		Check(VansEditorPlayCommandController::Execute(
			VansEditorPlayCommand::Resume, playContext, playOperations) ==
			VansEditorPlayCommandOutcome::Executed &&
			playCommandSteps == std::vector<std::string>{
				"time:running", "physics:running",
				"state:" + std::to_string(static_cast<int>(EnginePlayState::Play)),
				"info:[Editor] Scene resumed" },
			"resume command changed Timer/Physics/state ordering");
		playCommandSteps.clear();
		Check(VansEditorPlayCommandController::Execute(
			VansEditorPlayCommand::Stop, playContext, playOperations) ==
			VansEditorPlayCommandOutcome::Executed &&
			playCommandSteps == std::vector<std::string>{
				"time:paused", "physics:paused",
				"state:" + std::to_string(static_cast<int>(EnginePlayState::Edit)),
				"info:[Editor] Stop: reloading scene in Editor mode: Scenes/Test.vscene",
				"scene:editor:Scenes/Test.vscene" },
			"stop command changed Timer/Physics/state/reload ordering");

		int runtimeUndos = 0, sceneUndos = 0, assetUndos = 0, terrainUndos = 0;
		VansEditorHistoryService history;
		history.AddSource(VansEditorHistorySource::Runtime, { 11, 41 },
			[&]() { ++runtimeUndos; return true; }, []() { return true; });
		history.AddSource(VansEditorHistorySource::Scene, { 22, 32 },
			[&]() { ++sceneUndos; return true; }, []() { return true; });
		history.AddSource(VansEditorHistorySource::Asset, { 33, 23 },
			[&]() { ++assetUndos; return true; }, []() { return true; });
		history.AddSource(VansEditorHistorySource::Terrain, { 33, 23 },
			[&]() { ++terrainUndos; return true; }, []() { return true; });
		const VansEditorHistoryResult undoResult = history.Undo();
		const VansEditorHistoryResult redoResult = history.Redo();
		Check(undoResult.success && undoResult.source == VansEditorHistorySource::Terrain &&
			terrainUndos == 1 && assetUndos == 0 && sceneUndos == 0 && runtimeUndos == 0,
			"global history did not select the latest undo or specialized tie owner");
		Check(redoResult.success && redoResult.source == VansEditorHistorySource::Terrain,
			"global history did not select the earliest redo or specialized tie owner");

		VansSceneData sceneData;
		sceneData.sceneGuid = VansAssetGuid::New();
		sceneData.settings = VansSceneSchema::MakeDefaultSettings();
		VansSceneEntityData entity;
		entity.id = VansAssetGuid::New();
		entity.name = "Transform transaction";
		entity.components.push_back(VansSceneSchema::MakeTransform());
		sceneData.entities.push_back(entity);
		std::string sceneError;
		auto firstSessionDocument = VansSceneDocument::CreateInMemory(
			DecodeSerializedValueJson(VansSceneSchema::SerializeSceneJson(sceneData)),
			{},
			sceneError);
		Check(firstSessionDocument != nullptr, sceneError.c_str());
		auto* firstSessionDocumentAddress = firstSessionDocument.get();
		VansEditorSceneDocumentSession sceneDocumentSession;
		VansEditorSceneDocumentState emptySceneState =
			sceneDocumentSession.ReplaceDocument(
				std::move(firstSessionDocument),
				[] { return true; });
		Check(emptySceneState.Empty() &&
			sceneDocumentSession.Document() == firstSessionDocumentAddress &&
			sceneDocumentSession.EditService() != nullptr,
			"Scene document session did not install its first document/edit pair");
		auto* firstSessionEditAddress = sceneDocumentSession.EditService();
		auto secondSessionDocument = VansSceneDocument::CreateInMemory(
			DecodeSerializedValueJson(VansSceneSchema::SerializeSceneJson(sceneData)),
			{},
			sceneError);
		Check(secondSessionDocument != nullptr, sceneError.c_str());
		auto* secondSessionDocumentAddress = secondSessionDocument.get();
		VansEditorSceneDocumentState previousSceneState =
			sceneDocumentSession.ReplaceDocument(
				std::move(secondSessionDocument),
				[] { return true; });
		Check(previousSceneState.Document() == firstSessionDocumentAddress &&
			previousSceneState.EditService() == firstSessionEditAddress &&
			sceneDocumentSession.Document() == secondSessionDocumentAddress &&
			sceneDocumentSession.EditService() != firstSessionEditAddress,
			"Scene document session split or lost the previous document/edit pair");
		sceneDocumentSession.Restore(std::move(previousSceneState));
		Check(sceneDocumentSession.Document() == firstSessionDocumentAddress &&
			sceneDocumentSession.EditService() == firstSessionEditAddress,
			"Scene document session did not restore the Prefab-stage pair atomically");
		sceneDocumentSession.Reset();
		Check(sceneDocumentSession.Empty(),
			"Scene document session reset retained a document or edit service");

		auto sceneDocument = VansSceneDocument::CreateInMemory(
			DecodeSerializedValueJson(VansSceneSchema::SerializeSceneJson(sceneData)),
			{},
			sceneError);
		Check(sceneDocument != nullptr, sceneError.c_str());
		VansSceneEditService sceneEdits(*sceneDocument);
		RuntimeTransformSnapshot localTransform;
		localTransform.available = true;
		localTransform.entityGuid = entity.id.ToString();
		localTransform.space = RuntimeTransformSpace::Local;
		localTransform.position = { 3.0f, 4.0f, 5.0f };
		localTransform.rotationDegrees = { 10.0f, 20.0f, 30.0f };
		localTransform.scale = { 1.5f, 2.0f, 2.5f };
		Check(sceneEdits.SetEntityTransform(localTransform.entityGuid, localTransform).success,
			"gizmo transform commit was not accepted by the scene document");
		const VansAuthoringHistorySnapshot transformHistory = sceneEdits.HistorySnapshot();
		Check(transformHistory.undoSequence != 0 && transformHistory.redoSequence == 0,
			"one transform commit did not create exactly one scene history head");
		auto projectedTransform = BuildRuntimeEntityPreviewChangeFromSceneRoot(
			sceneDocument->SerializedRootSnapshot(), localTransform.entityGuid);
		Check(projectedTransform.hasTransform &&
			projectedTransform.transform.space == RuntimeTransformSpace::Local &&
			projectedTransform.transform.position.x == 3.0f &&
			projectedTransform.transform.scale.z == 2.5f,
			"scene transform commit did not preserve local-space values");
		Check(sceneEdits.Undo().success,
			"scene transform transaction could not be undone");
		projectedTransform = BuildRuntimeEntityPreviewChangeFromSceneRoot(
			sceneDocument->SerializedRootSnapshot(), localTransform.entityGuid);
		Check(projectedTransform.hasTransform && projectedTransform.transform.position.x == 0.0f,
			"scene transform undo did not restore the document value");
		Check(sceneEdits.Redo().success,
			"scene transform transaction could not be redone");
		const VansSerializedValue previewEntity = DecodeSerializedValueJson(nlohmann::json{
			{"id", "preview-entity"}, {"name", "Preview Entity"}, {"active", false},
			{"components", nlohmann::json::array({
				{{"id", "spot-component"}, {"type", "SpotLight"}, {"enabled", true},
				 {"data", {{"color", {0.25, 0.5, 0.75}}, {"intensity", 4.0},
					{"radius", 12.0}, {"innercutoff", 15.0}, {"outerCutoff", 30.0},
					{"cookie", {{"enabled", true}, {"strength", 0.4}}}}}},
				{{"id", "renderer-component"}, {"type", "ModelRenderer"}, {"enabled", true},
				 {"data", {{"materialOverrides", {{"default", {{"guid", "material-a"}}}}},
					{"submeshMaterialOverrides", {{"body", "material-b"}}}}}}
			})}
		});
		const RuntimeEntityPreviewChange previewChange =
			BuildRuntimeEntityPreviewChange(previewEntity);
		Check(previewChange.nameEdits.size() == 1 &&
			previewChange.activeEdits.size() == 1 && !previewChange.activeEdits[0].active &&
			previewChange.componentEnabled.size() == 2 && previewChange.lights.size() == 1 &&
			previewChange.lights[0].writeCookie && previewChange.lights[0].writeColor &&
			previewChange.lights[0].writeIntensity && previewChange.lights[0].writeRadius &&
			previewChange.lights[0].writeInnerCutoff && previewChange.lights[0].writeOuterCutoff &&
			previewChange.materialOverrides.size() == 2,
			"Scene projection to Editor preview DTO lost entity, light, or material override fields");

		auto readSource = [](const std::filesystem::path& path)
		{
			std::ifstream input(path, std::ios::binary);
			return std::string(std::istreambuf_iterator<char>(input),
				std::istreambuf_iterator<char>());
		};
		const std::string gizmoSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansGizmos.cpp");
		Check(!gizmoSource.empty() &&
			gizmoSource.find("api.ApplyRuntimeTransform(") == std::string::npos &&
			gizmoSource.find("GetSceneEditService(") == std::string::npos &&
			gizmoSource.find("ApplyRuntimeEntityPreviewChange(preview)") != std::string::npos &&
			gizmoSource.find("sceneEdits.SetEntityTransform(") != std::string::npos,
			"gizmo transform flow bypasses preview/scene transaction ownership");
		const std::string apiSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Private" / "EngineAPIImpl.cpp");
		const std::size_t previewBegin = apiSource.find(
			"bool EngineAPIImpl::ApplyRuntimeEntityPreviewChange");
		const std::size_t previewEnd = apiSource.find(
			"bool EngineAPIImpl::SetRuntimeComponentEnabled", previewBegin);
		Check(previewBegin != std::string::npos && previewEnd != std::string::npos,
			"runtime entity preview implementation could not be located");
		const std::string previewSection = apiSource.substr(
			previewBegin, previewEnd - previewBegin);
		Check(previewSection.find("SubmitCommand(") == std::string::npos &&
			previewSection.find("ApplyRuntimeTransform(") == std::string::npos &&
			previewSection.find("ApplyRuntimeTransformById(") != std::string::npos,
			"runtime entity preview still writes the runtime command history");
		auto countText = [](const std::string& text, const std::string& token)
		{
			std::size_t count = 0;
			for (std::size_t position = 0;
				(position = text.find(token, position)) != std::string::npos;
				position += token.size())
			{
				++count;
			}
			return count;
		};
		const std::string apiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Private" / "EngineAPIImpl.h");
		const std::string editorApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IEngineEditorAPI.h");
		const std::string audioApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IAudioEditorAPI.h");
		const std::string animationApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IAnimationEditorAPI.h");
		const std::string animationPreviewApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IAnimationPreviewEditorAPI.h");
		const std::string assetAuthoringApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IAssetAuthoringEditorAPI.h");
		const std::string assetApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IAssetEditorAPI.h");
		const std::string gafApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IGAFEditorAPI.h");
		const std::string giApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IGIEditorAPI.h");
		const std::string aiApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IAIEditorAPI.h");
		const std::string motionMatchingApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IMotionMatchingEditorAPI.h");
		const std::string particleApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IParticleEditorAPI.h");
		const std::string pcgApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IPcgEditorAPI.h");
		const std::string playModeApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IPlayModeEditorAPI.h");
		const std::string projectApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IProjectEditorAPI.h");
		const std::string reflectionProbeApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IReflectionProbeEditorAPI.h");
		const std::string renderApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IRenderEditorAPI.h");
		const std::string runtimePhysicsApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IRuntimePhysicsEditorAPI.h");
		const std::string runtimeCommandHistoryApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IRuntimeCommandHistoryEditorAPI.h");
		const std::string runtimeFrameApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IRuntimeFrameEditorAPI.h");
		const std::string runtimeSceneApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IRuntimeSceneEditorAPI.h");
		const std::string sceneInteractionApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "ISceneInteractionEditorAPI.h");
		const std::string sceneSettingsApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "ISceneSettingsEditorAPI.h");
		const std::string scriptLifecycleApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IScriptLifecycleEditorAPI.h");
		const std::string publicDtoHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "EngineDTOs.h");
		const std::string modelPlacementHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Private" / "ModelAssetPlacementPreparationService.h");
		const std::string modelPlacementSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Private" / "ModelAssetPlacementPreparationService.cpp");
		const std::string animationRigAuthoringSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Private" / "AnimationPreviewRigAuthoringService.cpp");
		const std::string animationAttachmentAuthoringSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Private" / "AnimationPreviewAttachmentAuthoringService.cpp");
		const std::string pcgCreationSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Private" / "EngineAPIImpl.PcgCreation.cpp");
		const std::string enginePcgSplinesSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Private" / "EngineAPIImpl.PcgSplines.cpp");
		const std::string authoringAssetCreationSource = readSource(engineRoot / "Source" / "EngineCore" /
			"AuthoringCore" / "VansAuthoringAssetCreationService.cpp");
		const std::string renderSceneHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"RenderCore" / "VansScene.h");
		const std::string runtimeWorldHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"SceneRuntime" / "VansRuntimeWorld.h");
		const std::string runtimeWorldSource = readSource(engineRoot / "Source" / "EngineCore" /
			"SceneRuntime" / "VansRuntimeWorld.cpp");
		const std::string sceneObjectBuildSource = readSource(engineRoot / "Source" / "EngineCore" /
			"RenderCore" / "SceneBuild" / "VansSceneObjectBuildExecutor.cpp");
		const std::string sceneVehicleBuilderHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"RenderCore" / "SceneBuild" / "VansSceneVehicleComponentBuilder.h");
		const std::string sceneVehicleBuilderSource = readSource(engineRoot / "Source" / "EngineCore" /
			"RenderCore" / "SceneBuild" / "VansSceneVehicleComponentBuilder.cpp");
		const std::string materialLiveEditHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Private" / "VansMaterialLiveEditService.h");
		const std::string materialLiveEditSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Private" / "VansMaterialLiveEditService.cpp");
		const std::string cameraArbiterHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"RenderCore" / "VansCameraControlArbiter.h");
		const std::string cameraArbiterSource = readSource(engineRoot / "Source" / "EngineCore" /
			"RenderCore" / "VansCameraControlArbiter.cpp");
		const std::string enginePathsHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"ProjectSystem" / "VansEnginePaths.h");
		const std::string enginePathsSource = readSource(engineRoot / "Source" / "EngineCore" /
			"ProjectSystem" / "VansEnginePaths.cpp");
		const std::string projectManagerHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"ProjectSystem" / "VansProjectManager.h");
		const std::string projectManagerSource = readSource(engineRoot / "Source" / "EngineCore" /
			"ProjectSystem" / "VansProjectManager.cpp");
		const std::string renderBootstrapHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"RenderCore" / "VansRenderBootstrapSettings.h");
		const std::string configurationEditorRuntimeHostSource = readSource(engineRoot / "Source" / "Application" /
			"VansEditorRuntimeHost.cpp");
		const std::string runtimeHostSource = readSource(engineRoot / "Source" / "RuntimeExport" /
			"VansRuntimeHost.cpp");
		const std::string sourceManifest = readSource(engineRoot / "cmake" / "ForestSourceManifest.cmake");
		const std::string shaderApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IShaderEditorAPI.h");
		const std::string terrainApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "ITerrainEditorAPI.h");
		const std::string timelineApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "ITimelineEditorAPI.h");
		const std::string uiApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IUIEditorAPI.h");
		const std::string vehicleApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IVehicleEditorAPI.h");
		const std::string waterApiHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "IWaterEditorAPI.h");
		const std::string projectWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansProjectWindow.cpp");
		const std::string editorWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorWindow.cpp");
		const std::string editorWindowHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorWindow.h");
		const std::string editorShellMenuSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorShellMenu.cpp");
		const std::string editorPackageSessionSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorPackageSession.cpp");
		const std::string editorPlayToolbarSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorPlayToolbar.cpp");
		const std::string editorPlayCommandSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorPlayCommandController.cpp");
		const std::string editorProjectSessionSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorProjectSession.cpp");
		const std::string editorProjectSwitchSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorProjectSwitchController.cpp");
		const std::string editorSceneDocumentSessionSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorSceneDocumentSession.cpp");
		const std::string editorSceneLoadControllerSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorSceneLoadController.cpp");
		const std::string editorAuthoringCommandSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorAuthoringCommandController.cpp");
		const std::string editorShellCommandSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorShellCommandController.cpp");
		const std::string editorHistoryAdapterSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorHistoryAdapter.cpp");
		const std::string editorAssetSaveServiceSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorAssetSaveService.cpp");
		const std::string editorAssetSaveServiceHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorAssetSaveService.h");
		const std::string animationPreviewIKSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansSceneAnimationPreviewIKEditor.cpp");
		const std::string runtimePreviewProjectorSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorRuntimePreviewProjector.cpp");
		const std::string sceneRuntimeProjectionHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"SceneCore" / "VansSceneRuntimeProjection.h");
		const std::string sceneRuntimeProjectionSource = readSource(engineRoot / "Source" / "EngineCore" /
			"SceneCore" / "VansSceneRuntimeProjection.cpp");
		const std::string sceneDocumentSource = readSource(engineRoot / "Source" / "EngineCore" /
			"SceneCore" / "VansSceneDocument.cpp");
		const std::string sceneSchemaHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"SceneCore" / "VansSceneSchema.h");
		const std::string sceneSchemaSource = readSource(engineRoot / "Source" / "EngineCore" /
			"SceneCore" / "VansSceneSchema.cpp");
		const std::string sceneAssetPlacementHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansSceneAssetPlacementService.h");
		const std::string sceneEntityCreationHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansSceneEntityCreationService.h");
		const std::string editorMaterialSchemaServiceSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorMaterialSchemaService.cpp");
		const std::string editorPrefabSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorWindow.Prefab.cpp");
		const std::string editorPrefabSessionHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorPrefabSession.h");
		const std::string editorSelectionServiceHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorSelectionService.h");
		const std::string editorSelectionServiceSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorSelectionService.cpp");
		const std::string prefabEditServiceSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansPrefabEditService.cpp");
		const std::string sceneWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansSceneWindow.cpp");
		const std::string sceneWindowHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansSceneWindow.h");
		const std::string sceneWindowSplinesSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansSceneWindow.Splines.cpp");
		const std::string hiZWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansHiZCullWindow.cpp");
		const std::string giWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansGIWindow.cpp");
		const std::string skeletonDebugWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansSkeletonDebugWindow.cpp");
		const std::string audioDebugWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansAudioDebugWindow.cpp");
		const std::string aiDebugWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansAIDebugWindow.cpp");
		const std::string motionMatchingDebugWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansMotionMatchingDebugWindow.cpp");
		const std::string particleDebugWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansParticleDebugWindow.cpp");
		const std::string gBufferWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansGBufferWindow.cpp");
		const std::string renderDebugWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansRenderDebugWindow.cpp");
		const std::string shadowDebuggerWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansShadowDebuggerWindow.cpp");
		const std::string reflectionProbeWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansReflectionProbeWindow.cpp");
		const std::string waterWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansWaterWindow.cpp");
		const std::string lightWindowHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansLightWindow.h");
		const std::string lightWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansLightWindow.cpp");
		const std::string postProcessWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansPostProcessWindow.cpp");
		const std::string shaderHotReloadHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "ShaderHotReload" / "VansEditorShaderHotReloadController.h");
		const std::string shaderHotReloadSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "ShaderHotReload" / "VansEditorShaderHotReloadController.cpp");
		const std::string inspectorWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansInspectorWindow.cpp");
		const std::string clothProfileEditorHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansClothProfileEditorWindow.h");
		const std::string clothProfileEditorSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansClothProfileEditorWindow.cpp");
		const std::string boneMaskEditorSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansBoneMaskEditorWindow.cpp");
		const std::string boneMaskEditorHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansBoneMaskEditorWindow.h");
		const std::string animGraphEditorSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansAnimGraphEditorWindow.cpp");
		const std::string animGraphEditorHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansAnimGraphEditorWindow.h");
		const std::string hierarchyWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansHierachyWindow.cpp");
		const std::string sceneViewCommandsSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansSceneViewCommands.cpp");
		const std::string scenePickingServiceHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansScenePickingService.h");
		const std::string scenePickingServiceSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansScenePickingService.cpp");
		const std::string pcgConfigurationSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansPcgWindow.Configuration.cpp");
		const std::string pcgWindowHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansPcgWindow.h");
		const std::string pcgWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansPcgWindow.cpp");
		const std::string pcgMaskSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansPcgWindow.Mask.cpp");
		const std::string pcgInstancesSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansPcgWindow.Instances.cpp");
		const std::string pcgSplinesSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansPcgWindow.Splines.cpp");
		const std::string sceneAnimationPreviewSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansSceneAnimationPreviewWindow.cpp");
		const std::string sceneAnimationPreviewHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansSceneAnimationPreviewWindow.h");
		const std::string sceneAnimationPreviewIKSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansSceneAnimationPreviewIKEditor.cpp");
		const std::string sceneAnimationPreviewRigSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansSceneAnimationPreviewRigEditor.cpp");
		const std::string gameplayActionEditorHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansGameplayActionEditorWindow.h");
		const std::string gameplayActionEditorSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansGameplayActionEditorWindow.cpp");
		const std::string gafDebuggerHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansGAFDebuggerWindow.h");
		const std::string gafDebuggerSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansGAFDebuggerWindow.cpp");
		const std::string projectSettingsWindowHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansProjectSettingsWindow.h");
		const std::string projectSettingsWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansProjectSettingsWindow.cpp");
		const std::string projectSelectorSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansProjectSelector.cpp");
		const std::string scriptorWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansScriptorWindow.cpp");
		const std::string uiEditorWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansUIEditorWindow.cpp");
		const std::string terrainWindowSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansTerrainWindow.cpp");
		const std::string timelinePreviewSessionHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Timeline" / "VansTimelinePreviewSession.h");
		const std::string timelinePreviewSessionSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Timeline" / "VansTimelinePreviewSession.cpp");
		const std::string timelineEditorHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansTimelineEditorWindow.h");
		const std::string timelineEditorSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "Windows" / "VansTimelineEditorWindow.cpp");
		const std::string engineDtosHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EngineAPILayer" / "Public" / "EngineDTOs.h");
		const std::string editorObjectReferenceHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"AuthoringCore" / "VansEditorObjectReference.h");
		const std::string assetDocumentEditSource = readSource(engineRoot / "Source" / "EngineCore" /
			"AuthoringCore" / "VansAssetDocumentEditService.cpp");
		const std::string sceneEditServiceSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansSceneEditService.cpp");
		const std::string editorRuntimeHostSource = readSource(engineRoot / "Source" / "Application" /
			"VansEditorRuntimeHost.cpp");
		const std::string editorThemeSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorTheme.cpp");
		const std::string editorConfigurationSource = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorConfiguration.cpp");
		const std::string editorConfigurationAsset = readSource(
			engineRoot / "EngineAssets" / "Editor" / "EditorConfiguration.json");
		const std::string editorWindowCatalogHeader = readSource(engineRoot / "Source" / "EngineCore" /
			"EditorCore" / "VansEditorWindowCatalog.h");
		const std::string packageToolSource = readSource(engineRoot / "Source" / "Application" /
			"ForestPackageToolEntry.cpp");
		const std::string packageBuilderSource = readSource(engineRoot / "Source" / "EngineCore" /
			"PackagingCore" / "VansGamePackageBuilder.cpp");
		Check(countText(apiSource,
			"VansEditorTextureBridge::RegisterTexture(") == 1 &&
			apiSource.find("static std::vector<PreviewTextureCache>") == std::string::npos &&
			apiSource.find("GetAnimationPreviewSessions()") == std::string::npos &&
			apiSource.find("m_EditorPreviewRegistry->RegisterTexture(") != std::string::npos &&
			apiSource.find("m_AnimationPreviewSessionOwner->sessions") != std::string::npos &&
			apiSource.find("AnimationPreviewRigAuthoringService rigAuthoring") != std::string::npos &&
			apiSource.find("AnimationPreviewAttachmentAuthoringService attachmentAuthoring") != std::string::npos &&
			animationRigAuthoringSource.find("static std::unordered_map<AnimationPreviewSessionId") == std::string::npos &&
			animationAttachmentAuthoringSource.find("static std::unordered_map<AnimationPreviewSessionId") == std::string::npos &&
			apiSource.find("ClearEditorRenderTexturePreviewCaches(*m_EditorPreviewRegistry, oldDevice)") != std::string::npos &&
			apiHeader.find("std::unique_ptr<VansEditorPreviewRegistry> m_EditorPreviewRegistry") != std::string::npos &&
			apiHeader.find("std::unique_ptr<VansAnimationPreviewSessionOwner> m_AnimationPreviewSessionOwner") != std::string::npos,
			"editor preview textures or animation sessions bypass instance ownership");
		Check(projectWindowSource.find("VansEditorWindow::m_PendingScenePath") == std::string::npos &&
			projectWindowSource.find("VansEditorWindow::RequestSceneLoad(scenePath)") != std::string::npos,
			"project window bypasses the deferred scene-load request boundary");
		Check(editorWindowSource.find("VansEditorTheme::Apply(*m_EditorConfiguration)") != std::string::npos &&
			countText(editorWindowSource, "VansEditorTheme::PushToolbarActionColors(") == 0 &&
			editorWindowSource.find("AddFontFromFileTTF(") == std::string::npos &&
			editorWindowSource.find("ImGui::PushStyleColor(ImGuiCol_Button") == std::string::npos &&
			countText(editorPlayToolbarSource, "VansEditorTheme::PushToolbarActionColors(") == 3 &&
			countText(editorPlayToolbarSource, "ImGui::Button(") == 3 &&
			countText(editorThemeSource, "AddFontFromFileTTF(") == 2 &&
			countText(editorThemeSource, "c[ImGuiCol_") == 49 &&
			countText(editorThemeSource, "style.") == 22 &&
			editorThemeSource.find("consolab.ttf") == std::string::npos &&
			editorThemeSource.find("msyh.ttc") == std::string::npos,
			"editor font, base style, color, or toolbar theme ownership drifted");
		Check(editorConfigurationSource.find("RequireExactFields(") != std::string::npos &&
			editorConfigurationSource.find("ResolveBuiltInPath()") != std::string::npos &&
			editorConfigurationAsset.find("\"consolab.ttf\"") != std::string::npos &&
			editorConfigurationAsset.find("\"package\"") != std::string::npos &&
			editorWindowCatalogHeader.find("defaultOpen") == std::string::npos &&
			editorWindowSource.find("m_WindowCatalog.ApplyDefaults(configuration->windowDefaults)") != std::string::npos &&
			editorPlayToolbarSource.find("62.0f") == std::string::npos &&
			editorWindowSource.find("selectedPlatform = m_EditorConfiguration->packagePlatform") != std::string::npos &&
			packageToolSource.find("return Vans::ParseGamePackagePlatform(value, platform);") != std::string::npos &&
			countText(packageBuilderSource, "value == \"Windows\"") == 1,
			"Editor defaults, layout, fonts, or package platform escaped the strict configuration path");
		Check(countText(editorWindowSource, "m_WindowRegistry.Add<") == 32 &&
			editorWindowSource.find("m_WindowRegistry.All()") != std::string::npos &&
			editorWindowSource.find("AddEditorWindowComponent") == std::string::npos &&
			editorWindowHeader.find("m_Windows") == std::string::npos &&
			editorWindowHeader.find("m_SceneAnimationPreviewWindow") == std::string::npos &&
			editorWindowHeader.find("m_ParticleDebugWindow") == std::string::npos &&
			gizmoSource.find("VansEditorWindow::m_ParticleDebugWindow") == std::string::npos &&
			sceneWindowSource.find("VansEditorWindow::m_SceneAnimationPreviewWindow") == std::string::npos,
			"editor windows bypass ordered registry ownership or leak raw window pointers");
		Check(countText(editorWindowSource, "ImGui::BeginMenu(") == 1 &&
			countText(editorWindowSource, "VansEditorShellMenu::Draw(") == 1 &&
			countText(editorShellMenuSource, "ImGui::BeginMenu(") == 11 &&
			editorShellMenuSource.find("state.canCreateAssets && state.canCreateAssets()") != std::string::npos,
			"editor Shell menu rendering or lazy asset availability escaped its owner");
		Check(editorWindowSource.find("static std::string lastPackageStatus") == std::string::npos &&
			editorWindowSource.find("VansGamePackageBuilder::Build(") == std::string::npos &&
			countText(editorPackageSessionSource, "Vans::VansGamePackageBuilder::Build(") == 1,
			"editor package status or build execution escaped its explicit session");
		Check(editorWindowHeader.find("static void OnPlay()") == std::string::npos &&
			editorWindowHeader.find("static void OnPause()") == std::string::npos &&
			editorWindowHeader.find("static void OnResume()") == std::string::npos &&
			editorWindowHeader.find("static void OnStop()") == std::string::npos &&
			countText(editorWindowSource, "VansEditorPlayCommandController::Execute(") == 1 &&
			countText(editorWindowSource, "VansTimer::SetTimePaused(") == 3 &&
			countText(editorWindowSource, ".ResumeRuntimePhysics()") == 1 &&
			countText(editorPlayCommandSource, "operations.setTimePaused(") == 3 &&
			countText(editorSceneLoadControllerSource, "operations.setTimePaused(") == 2 &&
			countText(editorPlayCommandSource, "operations.requestSceneLoad(") == 2,
			"play command orchestration escaped its controller or returned to the Shell");
		Check(editorWindowHeader.find("m_ProjectSelector") == std::string::npos &&
			editorWindowHeader.find("m_ProjectLoaded") == std::string::npos &&
			editorWindowHeader.find("m_PendingProjectLoad") == std::string::npos &&
			countText(editorWindowSource, "m_ProjectSession.QueueOpen(") == 2 &&
			countText(editorWindowSource, "m_ProjectSession.QueueCreate(") == 1 &&
			editorProjectSessionSource.find("std::make_unique<Vans::VansProjectSelector>()") != std::string::npos,
			"project selector, loaded state, or pending requests escaped the project session");
		Check(countText(editorWindowSource, "VansEditorProjectSwitchController::Execute(") == 1 &&
			countText(editorProjectSwitchSource, "operations.unloadRuntimeScene()") == 1 &&
			countText(editorProjectSwitchSource, "operations.openProject(request)") == 1 &&
			countText(editorProjectSwitchSource, "operations.markProjectLoaded(") == 2,
			"project switch core orchestration escaped its controller");
		Check(editorWindowHeader.find("m_CurrentLoadedScenePath") == std::string::npos &&
			editorWindowHeader.find("m_PendingScenePath") == std::string::npos &&
			editorWindowHeader.find("m_PendingSceneLoadMode") == std::string::npos &&
			editorWindowSource.find("m_SceneLoadSession.Request(") != std::string::npos &&
			editorWindowSource.find("m_SceneLoadSession.ClearPending()") != std::string::npos,
			"Scene current path, pending request, or mode escaped the Scene load session");
		Check(editorWindowHeader.find("m_SceneDocument;") == std::string::npos &&
			editorWindowHeader.find("m_SceneEditService;") == std::string::npos &&
			editorWindowSource.find("std::make_unique<Vans::VansSceneEditService>") == std::string::npos &&
			editorPrefabSource.find("std::make_unique<Vans::VansSceneEditService>") == std::string::npos &&
			editorPrefabSource.find("previousDocument") == std::string::npos &&
			editorPrefabSource.find("previousEdits") == std::string::npos &&
			countText(editorSceneDocumentSessionSource,
				"std::make_unique<Vans::VansSceneEditService>") == 1,
			"Scene document/edit ownership or Prefab-stage swap escaped the paired session");
		Check(countText(editorWindowSource,
			"VansEditorSceneLoadController::Execute(") == 1 &&
			countText(editorSceneLoadControllerSource,
				"operations.loadRuntimeScene(context.mode)") == 1 &&
			countText(editorSceneLoadControllerSource,
				"operations.markLoaded(context.pendingScenePath)") == 1 &&
			countText(editorSceneLoadControllerSource,
				"operations.clearPendingRequest()") == 6,
			"Scene load orchestration escaped its controller or duplicated a publish path");
		Check(countText(editorWindowSource,
			"VansEditorAuthoringCommandController::Execute(") == 1 &&
			editorWindowSource.find(
				"Exit cancelled: save or undo current scene changes first") == std::string::npos &&
			countText(editorAuthoringCommandSource, "SaveScene(context, operations)") == 2 &&
			countText(editorAuthoringCommandSource,
				"operations.reloadCurrentSceneForEditing()") == 2,
			"save/exit authoring command routing escaped its controller");
		Check(editorWindowHeader.find("static bool m_WireframeMode") == std::string::npos &&
			editorWindowHeader.find("static bool m_VehicleDebugGizmos") == std::string::npos &&
			editorWindowHeader.find("static bool m_HiZCullDebugVisualization") == std::string::npos &&
			editorWindowHeader.find("static bool m_SkeletonDebugGizmos") == std::string::npos &&
			gizmoSource.find("VansEditorWindow::m_") == std::string::npos &&
			sceneWindowSource.find("VansEditorWindow::m_") == std::string::npos &&
			hiZWindowSource.find("VansEditorWindow::m_") == std::string::npos &&
			skeletonDebugWindowSource.find("VansEditorWindow::m_") == std::string::npos &&
			editorRuntimeHostSource.find("VansEditorWindow::m_") == std::string::npos &&
			countText(editorWindowSource, "m_WindowRegistry.Add<VansSceneWindow>(m_DebugViewState)") == 1 &&
			countText(editorWindowSource, "m_WindowRegistry.Add<VansHiZCullWindow>(m_DebugViewState)") == 1 &&
			countText(editorWindowSource, "m_WindowRegistry.Add<VansSkeletonDebugWindow>(m_DebugViewState)") == 1 &&
			editorRuntimeHostSource.find("EnableSkeletonDebugForAutomation()") != std::string::npos &&
			editorRuntimeHostSource.find("&VansEditorWindow::NativeWindow()") != std::string::npos,
			"debug-view or native-window state escaped explicit ownership and injection");
		Check(countText(editorWindowSource,
			"VansEditorShellCommandController::ResolveShortcut(") == 1 &&
			countText(editorWindowSource,
				"VansEditorShellCommandController::Execute(") == 2 &&
			editorWindowSource.find("switch (command.type)") == std::string::npos &&
			countText(editorShellCommandSource,
				"case VansEditorShellCommandType::") == 10 &&
			editorShellCommandSource.find("state.shiftDown && state.savePressed") != std::string::npos &&
			editorShellCommandSource.find("state.sceneDocumentReady && state.savePressed") != std::string::npos,
			"Shell shortcut priority or semantic command routing escaped its controller");
		Check(countText(editorWindowSource, "FORESTENGINE_AUTOPLAY_SECONDS") == 1 &&
			countText(editorWindowSource,
				"ExecutePlayCommand(VansEditorPlayCommand::Play)") == 1 &&
			editorWindowSource.find(
				"Automation triggering Play after Editor scene load") != std::string::npos &&
			editorWindowSource.find("Automation Play confirmed") != std::string::npos,
			"Editor Play automation bypassed the existing Play command or lost readiness evidence");
		const std::size_t drainBeforeGuiShutdown =
			editorRuntimeHostSource.find("DrainDeferredDeletesAfterDeviceIdle()");
		const std::size_t guiShutdown =
			editorRuntimeHostSource.find("m_GuiBackend->ShutdownBackEnd()");
		Check(drainBeforeGuiShutdown != std::string::npos &&
			guiShutdown != std::string::npos && drainBeforeGuiShutdown < guiShutdown,
			"Editor deferred GPU deletes are not drained before GUI backend teardown");
		VansDeferredDeleteQueue deferredDeletes;
		int deferredDeleteCalls = 0;
		deferredDeletes.Enqueue([&deferredDeletes, &deferredDeleteCalls]()
			{
				++deferredDeleteCalls;
				deferredDeletes.Enqueue([&deferredDeleteCalls]() { ++deferredDeleteCalls; });
			});
		deferredDeletes.Flush();
		Check(deferredDeleteCalls == 1 && deferredDeletes.Size() == 1,
			"Deferred delete flush executed or corrupted a re-entrant retirement");
		deferredDeletes.Flush();
		Check(deferredDeleteCalls == 2 && deferredDeletes.Empty(),
			"Deferred delete queue did not preserve the next retirement batch");
		Check(countText(editorWindowSource,
			"VansEditorHistoryAdapter::Compose(") == 1 &&
			countText(editorWindowSource, "editorHistory.AddSource(") == 0 &&
			countText(editorHistoryAdapterSource, "history.AddSource(") == 5 &&
			editorHistoryAdapterSource.find("GetRuntimeCommandHistory()") != std::string::npos &&
			editorHistoryAdapterSource.find("GetTerrainEditorSnapshot()") != std::string::npos &&
			editorHistoryAdapterSource.find("GetPcgSplineSnapshot()") != std::string::npos &&
			editorHistoryAdapterSource.find("ApplyRuntimeMaterialPreviewChange(") != std::string::npos &&
			editorHistoryAdapterSource.find("ApplyRuntimeEntityPreviewChange(") != std::string::npos &&
			editorHistoryAdapterSource.find("reload();") != std::string::npos,
			"History provider composition or runtime patch ownership escaped its adapter");
		Check(editorApiHeader.find("public IAudioEditorAPI") != std::string::npos &&
			countText(audioApiHeader, "virtual ") == 7 &&
			editorApiHeader.find("GetAudioBusDebugSnapshot()") == std::string::npos &&
			editorApiHeader.find("SetAudioBusGain(") == std::string::npos &&
			editorApiHeader.find("SetAudioBusMuted(") == std::string::npos &&
			editorApiHeader.find("SetAudioBusSoloed(") == std::string::npos &&
			editorApiHeader.find("SetAudioMaxActiveVoices(") == std::string::npos &&
			editorApiHeader.find("SetAudioSourceLimit(") == std::string::npos &&
			audioDebugWindowSource.find("IAudioEditorAPI& audioAPI = editorAPI") != std::string::npos &&
			audioDebugWindowSource.find("editorAPI.GetAudio") == std::string::npos &&
			audioDebugWindowSource.find("editorAPI.SetAudio") == std::string::npos,
			"Audio editor operations escaped their domain API facet");
		Check(editorApiHeader.find("public IAssetEditorAPI") != std::string::npos &&
			countText(assetApiHeader, "virtual ") == 4 &&
			editorApiHeader.find("QueryAssets(") == std::string::npos &&
			editorApiHeader.find("CreateAssetDragPayload(") == std::string::npos &&
			editorApiHeader.find("ResolveAssetGuid(") == std::string::npos &&
			gameplayActionEditorHeader.find("IAssetEditorAPI&") != std::string::npos &&
			gameplayActionEditorSource.find("IAssetEditorAPI& assetAPI = editorAPI") != std::string::npos &&
			gameplayActionEditorSource.find("editorAPI.QueryAssets(") == std::string::npos &&
			prefabEditServiceSource.find("Apply(EditorAPI::IAssetEditorAPI& assetAPI") != std::string::npos &&
			prefabEditServiceSource.find("api.QueryAssets(") == std::string::npos &&
			boneMaskEditorHeader.find("IAssetEditorAPI* m_AssetAPI") != std::string::npos &&
			boneMaskEditorSource.find("m_AssetAPI->QueryAssets(") != std::string::npos &&
			boneMaskEditorSource.find("m_ActiveAPI") == std::string::npos &&
			inspectorWindowSource.find("m_ActiveAPI->QueryAssets(") == std::string::npos &&
			inspectorWindowSource.find("m_ActiveAPI->ResolveAssetGuid(") == std::string::npos &&
			pcgConfigurationSource.find("Vans::EditorAPI::IAssetEditorAPI& assetAPI") != std::string::npos &&
			pcgConfigurationSource.find("api.QueryAssets(") == std::string::npos &&
			pcgConfigurationSource.find("api.ResolveAssetGuid(") == std::string::npos &&
			pcgSplinesSource.find("IAssetEditorAPI& assetAPI=api") != std::string::npos &&
			pcgSplinesSource.find("api.QueryAssets(") == std::string::npos &&
			sceneAnimationPreviewSource.find("IAssetEditorAPI& assetAPI = editorAPI") != std::string::npos &&
			sceneAnimationPreviewSource.find("editorAPI.QueryAssets(") == std::string::npos &&
			sceneAnimationPreviewSource.find("editorAPI.ResolveAssetGuid(") == std::string::npos &&
			projectWindowSource.find("assetAPI.CreateAssetDragPayload(") != std::string::npos &&
			projectWindowSource.find("editorAPI.CreateAssetDragPayload(") == std::string::npos,
			"Asset catalog operations escaped their domain API facet");
		Check(editorApiHeader.find("public IAssetAuthoringEditorAPI") != std::string::npos &&
			countText(assetAuthoringApiHeader, "virtual ") == 9 &&
			editorApiHeader.find("BuildModelLods(") == std::string::npos &&
			editorApiHeader.find("GetProjectBrowserRoot(") == std::string::npos &&
			editorApiHeader.find("GetShaderAuthoringSchema(") == std::string::npos &&
			editorApiHeader.find("GetLocalFogFieldPreview(") == std::string::npos &&
			editorApiHeader.find("CreateProjectAsset(") == std::string::npos &&
			editorApiHeader.find("QueryPrefabAsset(") == std::string::npos &&
			editorApiHeader.find("RefreshProjectAsset(") == std::string::npos &&
			editorApiHeader.find("PublishAssetWorkingCopy(") == std::string::npos &&
			editorApiHeader.find("GetAssetMeta(") == std::string::npos &&
			apiHeader.find("GetAssetMeta(") == std::string::npos &&
			engineDtosHeader.find("struct AssetMetaSnapshot") == std::string::npos &&
			projectWindowSource.find("IAssetAuthoringEditorAPI& assetAuthoringAPI = editorAPI") != std::string::npos &&
			projectWindowSource.find("editorAPI.CreateProjectAsset(") == std::string::npos &&
			projectWindowSource.find("editorAPI.RefreshProjectAsset(") == std::string::npos &&
			prefabEditServiceSource.find("Lookup(EditorAPI::IAssetAuthoringEditorAPI& api)") != std::string::npos &&
			prefabEditServiceSource.find("IAssetAuthoringEditorAPI& assetAuthoringAPI = api") != std::string::npos &&
			editorMaterialSchemaServiceSource.find("IAssetAuthoringEditorAPI& api") != std::string::npos &&
			editorAssetSaveServiceSource.find("IAssetAuthoringEditorAPI& assetAuthoringAPI = editorAPI") != std::string::npos &&
			inspectorWindowSource.find("IAssetAuthoringEditorAPI& assetAuthoringAPI = api") != std::string::npos &&
			inspectorWindowSource.find("api.BuildModelLods(") == std::string::npos &&
			inspectorWindowSource.find("api.GetLocalFogFieldPreview(") == std::string::npos &&
			clothProfileEditorHeader.find("IAssetAuthoringEditorAPI* m_AssetAuthoringAPI") != std::string::npos &&
			clothProfileEditorSource.find("m_ActiveAPI->CreateProjectAsset(") == std::string::npos &&
			clothProfileEditorSource.find("m_ActiveAPI->RefreshProjectAsset(") == std::string::npos &&
			editorWindowSource.find("IAssetAuthoringEditorAPI& assetAuthoringAPI = editorAPI") != std::string::npos &&
			editorWindowSource.find("editorAPI.GetProjectBrowserRoot(") == std::string::npos &&
			editorWindowSource.find("editorAPI.PublishAssetWorkingCopy(") == std::string::npos,
			"Asset authoring operations escaped their domain API facet");
		Check(editorApiHeader.find("public IProjectEditorAPI") != std::string::npos &&
			countText(projectApiHeader, "virtual ") == 14 &&
			editorApiHeader.find("GetRecentProjects(") == std::string::npos &&
			editorApiHeader.find("OpenProject(") == std::string::npos &&
			editorApiHeader.find("CloseProject(") == std::string::npos &&
			editorApiHeader.find("GetProjectConfigSnapshot(") == std::string::npos &&
			editorApiHeader.find("SetProjectDefaultScene(") == std::string::npos &&
			editorApiHeader.find("SetProjectPathField(") == std::string::npos &&
			editorApiHeader.find("SetProjectScriptSearchPaths(") == std::string::npos &&
			editorApiHeader.find("SetProjectAssetDirectory(") == std::string::npos &&
			editorApiHeader.find("SaveProjectDocuments(") == std::string::npos &&
			editorApiHeader.find("GetProjectPhysicsTiming(") == std::string::npos &&
			editorApiHeader.find("SetProjectPhysicsTiming(") == std::string::npos &&
			editorApiHeader.find("SetCurrentProjectScenePath(") == std::string::npos &&
			editorApiHeader.find("GetProjectRootPath(") == std::string::npos &&
			projectSelectorSource.find("IProjectEditorAPI& projectAPI") != std::string::npos &&
			projectSelectorSource.find("editorAPI.GetRecentProjects(") == std::string::npos &&
			projectSettingsWindowSource.find("IProjectEditorAPI& projectAPI = editorAPI") != std::string::npos &&
			projectSettingsWindowSource.find("editorAPI.GetProject") == std::string::npos &&
			projectSettingsWindowSource.find("editorAPI.SetProject") == std::string::npos &&
			projectSettingsWindowSource.find("editorAPI.SaveProjectDocuments(") == std::string::npos &&
			editorWindowSource.find("IProjectEditorAPI& projectAPI = editorAPI") != std::string::npos &&
			editorWindowSource.find("editorAPI.GetProjectConfigSnapshot(") == std::string::npos &&
			editorWindowSource.find("editorAPI.GetProjectRootPath(") == std::string::npos &&
			editorWindowSource.find("editorAPI.OpenProject(") == std::string::npos &&
			editorWindowSource.find("editorAPI.CloseProject(") == std::string::npos &&
			editorWindowSource.find("editorAPI.SaveProjectDocuments(") == std::string::npos &&
			scriptorWindowSource.find("IProjectEditorAPI& projectAPI = editorAPI") != std::string::npos &&
			scriptorWindowSource.find("editorAPI.GetProjectRootPath(") == std::string::npos &&
			clothProfileEditorSource.find("IProjectEditorAPI& projectAPI = api") != std::string::npos &&
			clothProfileEditorSource.find("api.GetProjectRootPath(") == std::string::npos,
			"Project lifecycle or configuration operations escaped their domain API facet");
		Check(editorApiHeader.find("public IUIEditorAPI") != std::string::npos &&
			countText(uiApiHeader, "virtual ") == 8 &&
			editorApiHeader.find("OpenUIDocument(") == std::string::npos &&
			editorApiHeader.find("CloseUIDocument(") == std::string::npos &&
			editorApiHeader.find("SetUIDocumentVisible(") == std::string::npos &&
			editorApiHeader.find("GetUIDocumentSnapshot(") == std::string::npos &&
			editorApiHeader.find("GetUIDiagnostics(") == std::string::npos &&
			editorApiHeader.find("RequestUIPreview(") == std::string::npos &&
			editorApiHeader.find("GetUIPreviewTexture(") == std::string::npos &&
			uiEditorWindowSource.find("IUIEditorAPI& uiAPI = api") != std::string::npos &&
			uiEditorWindowSource.find("DrawUIEditorContents(Vans::EditorAPI::IUIEditorAPI& api)") != std::string::npos &&
			uiEditorWindowSource.find("LoadPreview(Vans::EditorAPI::IEngineEditorAPI&") == std::string::npos &&
			uiEditorWindowSource.find("UnloadPreview(Vans::EditorAPI::IEngineEditorAPI&") == std::string::npos &&
			uiEditorWindowSource.find("DrawMetaPanel(Vans::EditorAPI::IEngineEditorAPI&") == std::string::npos &&
			uiEditorWindowSource.find("DrawPreviewViewport(Vans::EditorAPI::IEngineEditorAPI&") == std::string::npos,
			"UI document or preview operations escaped their domain API facet");
		Check(editorApiHeader.find("public IRenderEditorAPI") != std::string::npos &&
			countText(renderApiHeader, "virtual ") == 21 &&
			editorApiHeader.find("GetViewportPreview(") == std::string::npos &&
			editorApiHeader.find("GetUpscalerSettings(") == std::string::npos &&
			editorApiHeader.find("GetUpscalerCapabilities(") == std::string::npos &&
			editorApiHeader.find("ApplyUpscalerSettings(") == std::string::npos &&
			editorApiHeader.find("GetCommandRecordingSettings(") == std::string::npos &&
			editorApiHeader.find("SetCommandRecordingSettings(") == std::string::npos &&
			editorApiHeader.find("QueryRenderTexturePreviews(") == std::string::npos &&
			editorApiHeader.find("GetAmbientSkyCacheDebugMode(") == std::string::npos &&
			editorApiHeader.find("SetAmbientSkyCacheDebugMode(") == std::string::npos &&
			editorApiHeader.find("RequestPunctualShadowDebugPreview(") == std::string::npos &&
			editorApiHeader.find("GetPunctualShadowDebugSnapshot(") == std::string::npos &&
			editorApiHeader.find("ApplyPunctualScreenSpaceShadowSettings(") == std::string::npos &&
			editorApiHeader.find("GetRenderBackendDiagnostics(") == std::string::npos &&
			editorApiHeader.find("GetPipelineRegistryStats(") == std::string::npos &&
			editorApiHeader.find("GetRenderDocStatus(") == std::string::npos &&
			editorApiHeader.find("SetRenderDocAPIValidationEnabled(") == std::string::npos &&
			editorApiHeader.find("SetRenderDocReferenceAllResources(") == std::string::npos &&
			editorApiHeader.find("CaptureNextRenderDocFrame(") == std::string::npos &&
			editorApiHeader.find("OpenRenderDocUI(") == std::string::npos &&
			editorApiHeader.find("GetMainCameraHiZCullDebugSnapshot(") == std::string::npos &&
			editorApiHeader.find("GetViewportTexture(") == std::string::npos &&
			apiHeader.find("GetViewportTexture(") == std::string::npos &&
			apiSource.find("EngineAPIImpl::GetViewportTexture(") == std::string::npos &&
			gBufferWindowSource.find("IRenderEditorAPI& renderAPI = editorAPI") != std::string::npos &&
			gBufferWindowSource.find("editorAPI.QueryRenderTexturePreviews(") == std::string::npos &&
			renderDebugWindowSource.find("IRenderEditorAPI& renderAPI = editorAPI") != std::string::npos &&
			renderDebugWindowSource.find("DrawRenderBackendDiagnostics(Vans::EditorAPI::IRenderEditorAPI&") != std::string::npos &&
			shadowDebuggerWindowSource.find("IRenderEditorAPI& renderAPI = editorAPI") != std::string::npos &&
			shadowDebuggerWindowSource.find("editorAPI.RequestPunctualShadowDebugPreview(") == std::string::npos &&
			projectSettingsWindowSource.find("IRenderEditorAPI& renderAPI = editorAPI") != std::string::npos &&
			projectSettingsWindowSource.find("editorAPI.GetUpscalerSettings(") == std::string::npos &&
			projectSettingsWindowSource.find("editorAPI.SetCommandRecordingSettings(") == std::string::npos &&
			sceneWindowSource.find("IRenderEditorAPI& renderAPI = editorAPI") != std::string::npos &&
			sceneWindowSource.find("editorAPI.GetViewportPreview(") == std::string::npos &&
			pcgSplinesSource.find("IRenderEditorAPI& renderAPI=api") != std::string::npos &&
			pcgSplinesSource.find("api.QueryRenderTexturePreviews(") == std::string::npos &&
			reflectionProbeWindowSource.find("IRenderEditorAPI& renderAPI = editorAPI") != std::string::npos &&
			reflectionProbeWindowSource.find("editorAPI.QueryRenderTexturePreviews(") == std::string::npos &&
			waterWindowSource.find("IRenderEditorAPI& renderAPI = editorAPI") != std::string::npos &&
			waterWindowSource.find("Vans::EditorAPI::IRenderEditorAPI& editorAPI,") != std::string::npos &&
			hiZWindowSource.find("IRenderEditorAPI& renderAPI = editorAPI") != std::string::npos &&
			hiZWindowSource.find("editorAPI.GetMainCameraHiZCullDebugSnapshot(") == std::string::npos,
			"Render viewport settings or diagnostics escaped their domain API facet");
		Check(editorApiHeader.find("public IShaderEditorAPI") != std::string::npos &&
			editorApiHeader.find("public IReflectionProbeEditorAPI") != std::string::npos &&
			editorApiHeader.find("public IGIEditorAPI") != std::string::npos &&
			editorApiHeader.find("public IWaterEditorAPI") != std::string::npos &&
			countText(shaderApiHeader, "virtual ") == 3 &&
			countText(reflectionProbeApiHeader, "virtual ") == 10 &&
			countText(giApiHeader, "virtual ") == 7 &&
			countText(waterApiHeader, "virtual ") == 4 &&
			editorApiHeader.find("QueryShaderProgramSources(") == std::string::npos &&
			editorApiHeader.find("ApplyShaderCandidateAtRenderSafePoint(") == std::string::npos &&
			editorApiHeader.find("BakeQueuedReflectionProbesNow(") == std::string::npos &&
			editorApiHeader.find("GetReflectionProbeSettings(") == std::string::npos &&
			editorApiHeader.find("ApplyReflectionProbeSettings(") == std::string::npos &&
			editorApiHeader.find("GenerateAutoReflectionProbes(") == std::string::npos &&
			editorApiHeader.find("ClearAutoReflectionProbes(") == std::string::npos &&
			editorApiHeader.find("RequestReflectionProbeBakeAll(") == std::string::npos &&
			editorApiHeader.find("RequestReflectionProbeBake(") == std::string::npos &&
			editorApiHeader.find("SaveReflectionProbeConfiguration(") == std::string::npos &&
			editorApiHeader.find("ConvertReflectionProbeToManual(") == std::string::npos &&
			editorApiHeader.find("GetGISettings(") == std::string::npos &&
			editorApiHeader.find("ApplyGISettings(") == std::string::npos &&
			editorApiHeader.find("SaveGIConfiguration(") == std::string::npos &&
			editorApiHeader.find("SetGIProbeVisualization(") == std::string::npos &&
			editorApiHeader.find("GetGIProbeDebugSnapshot(") == std::string::npos &&
			editorApiHeader.find("RequestGIRTPreviews(") == std::string::npos &&
			editorApiHeader.find("GetWaterSettings(") == std::string::npos &&
			editorApiHeader.find("ApplyWaterSettings(") == std::string::npos &&
			editorApiHeader.find("GetWaterRuntimeStats(") == std::string::npos &&
			editorApiHeader.find("RebuildReflectionProbeResources(") == std::string::npos &&
			apiHeader.find("RebuildReflectionProbeResources(") == std::string::npos &&
			apiSource.find("EngineAPIImpl::RebuildReflectionProbeResources(") == std::string::npos &&
			shaderHotReloadHeader.find("IShaderEditorAPI& engineAPI") != std::string::npos &&
			shaderHotReloadHeader.find("IEngineEditorAPI") == std::string::npos &&
			shaderHotReloadSource.find("IShaderEditorAPI& engineAPI") != std::string::npos &&
			reflectionProbeWindowSource.find("IReflectionProbeEditorAPI& probeAPI = editorAPI") != std::string::npos &&
			reflectionProbeWindowSource.find("editorAPI.GetReflectionProbeSettings(") == std::string::npos &&
			giWindowSource.find("IGIEditorAPI& giAPI = editorAPI") != std::string::npos &&
			giWindowSource.find("editorAPI.GetGISettings(") == std::string::npos &&
			waterWindowSource.find("IWaterEditorAPI& waterAPI = editorAPI") != std::string::npos &&
			waterWindowSource.find("editorAPI.GetWaterSettings(") == std::string::npos &&
			gizmoSource.find("IGIEditorAPI& giAPI = api") != std::string::npos &&
			gizmoSource.find("IReflectionProbeEditorAPI& probeAPI = api") != std::string::npos &&
			gizmoSource.find("api.GetGISettings(") == std::string::npos &&
			gizmoSource.find("api.GetReflectionProbeSettings(") == std::string::npos,
			"Shader Probe GI or Water operations escaped their domain API facets");
		Check(editorApiHeader.find("public ISceneSettingsEditorAPI") != std::string::npos &&
			countText(sceneSettingsApiHeader, "virtual ") == 9 &&
			editorApiHeader.find("GetLightingSettings(") == std::string::npos &&
			editorApiHeader.find("ApplyLightingSettings(") == std::string::npos &&
			editorApiHeader.find("GetPostProcessSettings(") == std::string::npos &&
			editorApiHeader.find("ApplyPostProcessSettings(") == std::string::npos &&
			editorApiHeader.find("CommitPostProcessSettings(") == std::string::npos &&
			editorApiHeader.find("GetEnvironmentSettings(") == std::string::npos &&
			editorApiHeader.find("ApplyEnvironmentSettings(") == std::string::npos &&
			editorApiHeader.find("CommitEnvironmentSettings(") == std::string::npos &&
			editorApiHeader.find("ConsumeScenePropertyEdits(") == std::string::npos &&
			editorApiHeader.find("CommitLightingChanges(") == std::string::npos &&
			apiHeader.find("CommitLightingChanges(") == std::string::npos &&
			apiSource.find("EngineAPIImpl::CommitLightingChanges(") == std::string::npos &&
			lightWindowHeader.find("ISceneSettingsEditorAPI& editorAPI") != std::string::npos &&
			lightWindowSource.find("ISceneSettingsEditorAPI& sceneSettingsAPI = editorAPI") != std::string::npos &&
			lightWindowSource.find("sceneSettingsAPI.GetLightingSettings(") != std::string::npos &&
			postProcessWindowSource.find("ISceneSettingsEditorAPI& sceneSettingsAPI = editorAPI") != std::string::npos &&
			postProcessWindowSource.find("sceneSettingsAPI.GetPostProcessSettings(") != std::string::npos &&
			editorWindowSource.find("ISceneSettingsEditorAPI& sceneSettingsAPI = editorAPI") == std::string::npos &&
			editorWindowSource.find("ConsumeScenePropertyEdits(") == std::string::npos &&
			apiHeader.find("m_PendingScenePropertyEdits") == std::string::npos &&
			apiSource.find("m_PendingScenePropertyEdits") == std::string::npos &&
			apiSource.find("m_SceneAuthoringHost->SetSceneValue(") != std::string::npos &&
			pcgCreationSource.find("VansAssetMetaStorage::StageSave(") == std::string::npos &&
			enginePcgSplinesSource.find("VansAssetMetaStorage::StageSave(") == std::string::npos &&
			pcgCreationSource.find("VansAuthoringAssetCreationService::CreateBundle(") != std::string::npos &&
			enginePcgSplinesSource.find("VansAuthoringAssetCreationService::CreateBundle(") != std::string::npos &&
			authoringAssetCreationSource.find("VansAssetMetaStorage::StageSave(") != std::string::npos &&
			authoringAssetCreationSource.find("VansAssetObjectBootstrapper::Publish(") != std::string::npos &&
			renderSceneHeader.find("EditorAnimationPreview") == std::string::npos &&
			renderSceneHeader.find("m_EditorPreviewDrivenAnimationNodes") == std::string::npos &&
			renderSceneHeader.find("BeginExternalAnimationEvaluation(") != std::string::npos &&
			renderSceneHeader.find("EvaluateExternalAnimationStep(") != std::string::npos,
			"Lighting PostProcess Environment or Scene property edits escaped their domain API facet");
		Check(editorApiHeader.find("public IRuntimeSceneEditorAPI") != std::string::npos &&
			countText(runtimeSceneApiHeader, "virtual ") == 10 &&
			editorApiHeader.find("CreateRuntimeSceneEntities(") == std::string::npos &&
			editorApiHeader.find("PrepareModelAssetPlacement(") == std::string::npos &&
			editorApiHeader.find("DestroyRuntimeEntity(") == std::string::npos &&
			editorApiHeader.find("ReparentRuntimeEntity(") == std::string::npos &&
			editorApiHeader.find("IsRuntimeSceneReady(") == std::string::npos &&
			editorApiHeader.find("IsRuntimeSceneSwitching(") == std::string::npos &&
			editorApiHeader.find("LoadRuntimeScene(") == std::string::npos &&
			editorApiHeader.find("UnloadRuntimeScene(") == std::string::npos &&
			editorApiHeader.find("UnloadRuntimeProjectResources(") == std::string::npos &&
			apiHeader.find("ProjectMeshLoadResult EnsureProjectMeshLoaded(") != std::string::npos &&
			apiHeader.find("ProjectMeshSnapshot GetProjectMeshInfo(") != std::string::npos &&
			apiHeader.find("std::string GetDefaultMaterialAssetName() const;") != std::string::npos &&
			modelPlacementHeader.find("EngineAPIImpl& editorAPI") != std::string::npos &&
			modelPlacementHeader.find("IEngineEditorAPI") == std::string::npos &&
			modelPlacementSource.find("ProjectMeshLoadResult") != std::string::npos &&
			publicDtoHeader.find("struct MeshLoadRequest") == std::string::npos &&
			publicDtoHeader.find("struct ProjectMeshInfoSnapshot") == std::string::npos &&
			publicDtoHeader.find("struct ProjectMeshAliasRequest") == std::string::npos &&
			editorApiHeader.find("RegisterProjectMeshAlias(") == std::string::npos &&
			apiHeader.find("RegisterProjectMeshAlias(") == std::string::npos &&
			apiSource.find("EngineAPIImpl::RegisterProjectMeshAlias(") == std::string::npos &&
			editorApiHeader.find("MakeUniqueRuntimeEntityName(") == std::string::npos &&
			apiHeader.find("MakeUniqueRuntimeEntityName(") == std::string::npos &&
			apiSource.find("EngineAPIImpl::MakeUniqueRuntimeEntityName(") == std::string::npos &&
			editorApiHeader.find("AreRuntimeProjectResourcesLoaded(") == std::string::npos &&
			apiHeader.find("AreRuntimeProjectResourcesLoaded(") == std::string::npos &&
			apiSource.find("EngineAPIImpl::AreRuntimeProjectResourcesLoaded(") == std::string::npos &&
			sceneAssetPlacementHeader.find("IRuntimeSceneEditorAPI& editorAPI") != std::string::npos &&
			sceneEntityCreationHeader.find("IRuntimeSceneEditorAPI& editorAPI") != std::string::npos &&
			editorWindowSource.find("IRuntimeSceneEditorAPI& runtimeSceneAPI = editorAPI") != std::string::npos &&
			editorPrefabSource.find("IRuntimeSceneEditorAPI& runtimeSceneAPI = *api") != std::string::npos &&
			hierarchyWindowSource.find("IRuntimeSceneEditorAPI& runtimeSceneAPI = editorAPI") != std::string::npos &&
			inspectorWindowSource.find("IRuntimeSceneEditorAPI& runtimeSceneAPI = api") != std::string::npos &&
			sceneWindowSource.find("IRuntimeSceneEditorAPI& runtimeSceneAPI = editorAPI") != std::string::npos &&
			sceneAnimationPreviewSource.find("IRuntimeSceneEditorAPI& runtimeSceneAPI = editorAPI") != std::string::npos,
			"Runtime Scene operations escaped their facet or private placement helpers remained public");
		Check(editorApiHeader.find("public ISceneInteractionEditorAPI") != std::string::npos &&
			countText(sceneInteractionApiHeader, "virtual ") == 10 &&
			countText(editorApiHeader, "virtual ") == 0 &&
			editorApiHeader.find("CaptureEditorViewportCamera(") == std::string::npos &&
			editorApiHeader.find("RestoreEditorViewportCamera(") == std::string::npos &&
			editorApiHeader.find("ApplyRuntimeEntityPreviewChange(") == std::string::npos &&
			editorApiHeader.find("ApplyRuntimeMaterialPreviewChange(") == std::string::npos &&
			editorApiHeader.find("PickEditorScene(") == std::string::npos &&
			editorApiHeader.find("QueryEditorSceneBounds(") == std::string::npos &&
			editorApiHeader.find("GetRuntimeTransform(") == std::string::npos &&
			editorApiHeader.find("BuildRuntimeMultiMeshExpansionSnapshot(") == std::string::npos &&
			editorApiHeader.find("GetRuntimeCollisionLayerNames(") == std::string::npos &&
			editorApiHeader.find("ApplyRuntimeTransform(") == std::string::npos &&
			apiHeader.find("ApplyRuntimeTransform(") == std::string::npos &&
			apiSource.find("EngineAPIImpl::ApplyRuntimeTransform(") == std::string::npos &&
			publicDtoHeader.find("struct RuntimeTransformEditResult") == std::string::npos &&
			editorPrefabSource.find("ISceneInteractionEditorAPI& sceneInteractionAPI = *api") != std::string::npos &&
			editorWindowSource.find("ISceneInteractionEditorAPI& sceneInteractionAPI = editorAPI") != std::string::npos &&
			editorHistoryAdapterSource.find("ISceneInteractionEditorAPI& sceneInteractionAPI,") != std::string::npos &&
			scenePickingServiceHeader.find("ISceneInteractionEditorAPI& editorAPI") != std::string::npos &&
			scenePickingServiceHeader.find("IEngineEditorAPI") == std::string::npos &&
			scenePickingServiceSource.find("ISceneInteractionEditorAPI& editorAPI") != std::string::npos &&
			sceneViewCommandsSource.find("ISceneInteractionEditorAPI& sceneInteractionAPI = api") != std::string::npos &&
			gizmoSource.find("ISceneInteractionEditorAPI& sceneInteractionAPI = api") != std::string::npos &&
			inspectorWindowSource.find("ISceneInteractionEditorAPI& sceneInteractionAPI = api") != std::string::npos &&
			projectSettingsWindowSource.find("ISceneInteractionEditorAPI& sceneInteractionAPI = editorAPI") != std::string::npos &&
			sceneWindowSource.find("ISceneInteractionEditorAPI& sceneInteractionAPI = editorAPI") != std::string::npos,
			"Scene interaction or preview operations escaped their facet or dead transform command survived");
		Check(editorApiHeader.find("public IVehicleEditorAPI") != std::string::npos &&
			countText(vehicleApiHeader, "virtual ") == 2 &&
			editorApiHeader.find("GetVehicleDebugSnapshot(") == std::string::npos &&
			sceneWindowSource.find("IVehicleEditorAPI& vehicleAPI = editorAPI") != std::string::npos &&
			sceneWindowSource.find("vehicleAPI.GetVehicleDebugSnapshot(") != std::string::npos &&
			sceneWindowSource.find("editorAPI.GetVehicleDebugSnapshot(") == std::string::npos,
			"Vehicle diagnostics escaped their API facet");
		Check(editorApiHeader.find("public IPlayModeEditorAPI") != std::string::npos &&
			countText(playModeApiHeader, "virtual ") == 5 &&
			editorApiHeader.find("UpdateGameCursorViewport(") == std::string::npos &&
			editorApiHeader.find("IsGameCursorHidden(") == std::string::npos &&
			editorApiHeader.find("GetPlayState(") == std::string::npos &&
			editorApiHeader.find("SetPlayState(") == std::string::npos &&
			editorWindowSource.find("IPlayModeEditorAPI& playModeAPI") != std::string::npos &&
			editorPrefabSource.find("IPlayModeEditorAPI& playModeAPI = *api") != std::string::npos &&
			sceneViewCommandsSource.find("IPlayModeEditorAPI& playModeAPI = api") != std::string::npos &&
			gafDebuggerSource.find("IPlayModeEditorAPI& playModeAPI = editorAPI") != std::string::npos &&
			hierarchyWindowSource.find("IPlayModeEditorAPI& playModeAPI = editorAPI") != std::string::npos &&
			postProcessWindowSource.find("IPlayModeEditorAPI& playModeAPI = editorAPI") != std::string::npos &&
			sceneWindowSource.find("IPlayModeEditorAPI& playModeAPI = editorAPI") != std::string::npos &&
			timelineEditorHeader.find("IPlayModeEditorAPI* m_PlayModeAPI") != std::string::npos &&
			timelineEditorSource.find("m_PlayModeAPI->GetPlayState(") != std::string::npos,
			"Play state or game viewport cursor operations escaped their API facet");
		const std::size_t engineApiPrivateSection = apiHeader.find("\n\tprivate:");
		const std::size_t privateVehicleStep = apiHeader.find("void StepRuntimeVehicle(");
		Check(editorApiHeader.find("public IRuntimePhysicsEditorAPI") != std::string::npos &&
			countText(runtimePhysicsApiHeader, "virtual ") == 10 &&
			editorApiHeader.find("InstallRuntimeVehiclePhysicsStepCallback(") == std::string::npos &&
			editorApiHeader.find("ClearRuntimePhysicsStepCallback(") == std::string::npos &&
			editorApiHeader.find("IsRuntimePhysicsRunning(") == std::string::npos &&
			editorApiHeader.find("StartRuntimePhysicsIfNeeded(") == std::string::npos &&
			editorApiHeader.find("PauseRuntimePhysics(") == std::string::npos &&
			editorApiHeader.find("ResumeRuntimePhysics(") == std::string::npos &&
			editorApiHeader.find("SyncRuntimePhysicsTransforms(") == std::string::npos &&
			editorApiHeader.find("PrepareRuntimeCharacterLocomotion(") == std::string::npos &&
			editorApiHeader.find("FlushRuntimeCharacterControllerTransforms(") == std::string::npos &&
			editorApiHeader.find("StepRuntimeVehicle(") == std::string::npos &&
			editorApiHeader.find("SetRuntimeVehicleInput(") == std::string::npos &&
			apiHeader.find("SetRuntimeVehicleInput(") == std::string::npos &&
			apiSource.find("EngineAPIImpl::SetRuntimeVehicleInput(") == std::string::npos &&
			engineApiPrivateSection != std::string::npos && privateVehicleStep > engineApiPrivateSection &&
			editorWindowSource.find("IRuntimePhysicsEditorAPI& runtimePhysicsAPI") != std::string::npos &&
			editorWindowSource.find("IRuntimePhysicsEditorAPI& m_RuntimePhysicsAPI") != std::string::npos,
			"Runtime Physics operations escaped their facet or dead Vehicle input bridge survived");
		Check(editorApiHeader.find("public IRuntimeFrameEditorAPI") != std::string::npos &&
			countText(runtimeFrameApiHeader, "virtual ") == 14 &&
			editorApiHeader.find("UpdateRuntimeNonCameraScripts(") == std::string::npos &&
			editorApiHeader.find("AdvanceCameraRuntime(") == std::string::npos &&
			editorApiHeader.find("UpdateRuntimeActionsEarly(") == std::string::npos &&
			editorApiHeader.find("UpdateRuntimeAI(") == std::string::npos &&
			editorApiHeader.find("RunRuntimeActionLateContinuation(") == std::string::npos &&
			editorApiHeader.find("UpdateRuntimeTimelinesPostScript(") == std::string::npos &&
			editorApiHeader.find("BeginRuntimeCameraControlFrame(") == std::string::npos &&
			editorApiHeader.find("UpdateRuntimeCameraScripts(") == std::string::npos &&
			editorApiHeader.find("CaptureRuntimeCameraControlBase(") == std::string::npos &&
			editorApiHeader.find("UpdateRuntimeTimelinesCamera(") == std::string::npos &&
			editorApiHeader.find("UpdateTimelinePreviewsPostScript(") == std::string::npos &&
			editorApiHeader.find("UpdateTimelinePreviewsCamera(") == std::string::npos &&
			editorApiHeader.find("ResolveRuntimeCameraControlFrame(") == std::string::npos &&
			editorWindowSource.find("IRuntimeFrameEditorAPI& m_RuntimeFrameAPI") != std::string::npos &&
			editorWindowSource.find("m_EditorAPI.UpdateRuntime") == std::string::npos,
			"Runtime frame phase operations escaped their API facet");
		Check(editorApiHeader.find("public IScriptLifecycleEditorAPI") != std::string::npos &&
			countText(scriptLifecycleApiHeader, "virtual ") == 4 &&
			editorApiHeader.find("InitializeRuntimeScripts(") == std::string::npos &&
			editorApiHeader.find("SetupRuntimeScriptProjectVenv(") == std::string::npos &&
			editorApiHeader.find("ReloadRuntimeScripts(") == std::string::npos &&
			editorApiHeader.find("ReloadRuntimeScriptModule(") == std::string::npos &&
			apiHeader.find("ReloadRuntimeScriptModule(") == std::string::npos &&
			apiSource.find("EngineAPIImpl::ReloadRuntimeScriptModule(") == std::string::npos &&
			editorWindowSource.find("IScriptLifecycleEditorAPI& scriptLifecycleAPI") != std::string::npos &&
			editorWindowSource.find("editorAPI.SetupRuntimeScriptProjectVenv(") == std::string::npos &&
			editorWindowSource.find("startupEditorAPI.InitializeRuntimeScripts(") == std::string::npos &&
			scriptorWindowSource.find("IScriptLifecycleEditorAPI& scriptLifecycleAPI = editorAPI") != std::string::npos &&
			scriptorWindowSource.find("scriptLifecycleAPI.ReloadRuntimeScripts(") != std::string::npos &&
			scriptorWindowSource.find("editorAPI.ReloadRuntimeScripts(") == std::string::npos,
			"Script lifecycle operations escaped their facet or duplicate reload bridge survived");
		Check(editorApiHeader.find("public IRuntimeCommandHistoryEditorAPI") != std::string::npos &&
			countText(runtimeCommandHistoryApiHeader, "virtual ") == 5 &&
			editorApiHeader.find("BreakCommandMergeGroup(") == std::string::npos &&
			editorApiHeader.find("GetRuntimeCommandHistory(") == std::string::npos &&
			editorApiHeader.find("CanUndo(") == std::string::npos &&
			editorApiHeader.find("CanRedo(") == std::string::npos &&
			editorApiHeader.find("Undo(") == std::string::npos &&
			editorApiHeader.find("Redo(") == std::string::npos &&
			apiHeader.find("bool CanUndo() const override") == std::string::npos &&
			apiHeader.find("bool CanRedo() const override") == std::string::npos &&
			apiSource.find("EngineAPIImpl::CanUndo(") == std::string::npos &&
			apiSource.find("EngineAPIImpl::CanRedo(") == std::string::npos &&
			editorHistoryAdapterSource.find(
				"IRuntimeCommandHistoryEditorAPI& runtimeHistoryAPI,") != std::string::npos &&
			editorHistoryAdapterSource.find("IEngineEditorAPI") == std::string::npos &&
			editorHistoryAdapterSource.find("editorAPI.GetRuntimeCommandHistory(") == std::string::npos &&
			editorHistoryAdapterSource.find("[api = &editorAPI]() { api->Undo()") == std::string::npos &&
			editorWindowSource.find(
				"IRuntimeCommandHistoryEditorAPI& runtimeHistoryAPI = editorAPI") != std::string::npos &&
			lightWindowSource.find(
				"IRuntimeCommandHistoryEditorAPI& runtimeHistoryAPI = editorAPI") != std::string::npos &&
			lightWindowSource.find("editorAPI.BreakCommandMergeGroup(") == std::string::npos &&
			postProcessWindowSource.find(
				"IRuntimeCommandHistoryEditorAPI& runtimeHistoryAPI = editorAPI") != std::string::npos &&
			postProcessWindowSource.find("editorAPI.BreakCommandMergeGroup(") == std::string::npos,
			"Runtime command history escaped its facet or dead CanUndo CanRedo helpers survived");
		Check(editorApiHeader.find("public IAnimationEditorAPI") != std::string::npos &&
			countText(animationApiHeader, "virtual ") == 13 &&
			editorApiHeader.find("GetAnimationAssetBinding(") == std::string::npos &&
			editorApiHeader.find("GetSceneSkeletonHierarchy(") == std::string::npos &&
			editorApiHeader.find("GetSceneSkeletonNodePose(") == std::string::npos &&
			editorApiHeader.find("GetSkeletonDebugSnapshot(") == std::string::npos &&
			editorApiHeader.find("GetAssetSkeletonSnapshot(") == std::string::npos &&
			editorApiHeader.find("DecodeAnimatorDocument(") == std::string::npos &&
			editorApiHeader.find("EncodeAnimatorDocument(") == std::string::npos &&
			editorApiHeader.find("DecodeBoneMaskDocument(") == std::string::npos &&
			editorApiHeader.find("EncodeBoneMaskDocument(") == std::string::npos &&
			editorApiHeader.find("DecodeAnimationRigDocument(") == std::string::npos &&
			editorApiHeader.find("EncodeAnimationRigDocument(") == std::string::npos &&
			editorApiHeader.find("CompileBoneMaskDocument(") == std::string::npos &&
			editorWindowSource.find("IAnimationEditorAPI& animationAPI") != std::string::npos &&
			hierarchyWindowSource.find("IAnimationEditorAPI& animationAPI = editorAPI") != std::string::npos &&
			sceneViewCommandsSource.find("IAnimationEditorAPI& animationAPI = api") != std::string::npos &&
			gizmoSource.find("IAnimationEditorAPI&>(api).GetSkeletonDebugSnapshot") != std::string::npos &&
			skeletonDebugWindowSource.find("IAnimationEditorAPI& animationAPI = editorAPI") != std::string::npos &&
			animGraphEditorHeader.find("IAnimationEditorAPI* m_AnimationAPI") != std::string::npos &&
			animGraphEditorHeader.find("IAssetEditorAPI* m_AssetAPI") != std::string::npos &&
			animGraphEditorHeader.find("IEngineEditorAPI* m_ActiveAPI") == std::string::npos &&
			animGraphEditorSource.find("m_AnimationAPI->EncodeAnimatorDocument(") != std::string::npos &&
			animGraphEditorSource.find("m_AssetAPI->CreateAssetDragPayload(") != std::string::npos &&
			boneMaskEditorHeader.find("IAnimationEditorAPI* m_AnimationAPI") != std::string::npos &&
			boneMaskEditorHeader.find("IEngineEditorAPI* m_ActiveAPI") == std::string::npos &&
			boneMaskEditorSource.find("m_AnimationAPI->CompileBoneMaskDocument(") != std::string::npos &&
			sceneAnimationPreviewSource.find("IAnimationEditorAPI& animationAPI = editorAPI") != std::string::npos &&
			sceneAnimationPreviewSource.find("editorAPI.DecodeAnimatorDocument(") == std::string::npos &&
			sceneAnimationPreviewIKSource.find("IAnimationEditorAPI& animationAPI = api") != std::string::npos &&
			sceneAnimationPreviewIKSource.find("api.EncodeAnimatorDocument(") == std::string::npos &&
			sceneAnimationPreviewRigSource.find("IAnimationEditorAPI& animationAPI = api") != std::string::npos &&
			sceneAnimationPreviewRigSource.find("api.DecodeAnimationRigDocument(") == std::string::npos,
			"Animation tooling operations escaped their domain API facet");
		Check(editorApiHeader.find("public IAnimationPreviewEditorAPI") != std::string::npos &&
			countText(animationPreviewApiHeader, "virtual ") == 21 &&
			editorApiHeader.find("CreateAnimationPreview(") == std::string::npos &&
			editorApiHeader.find("UpdateAnimationPreviewDefinition(") == std::string::npos &&
			editorApiHeader.find("SetAnimationPreviewPlayback(") == std::string::npos &&
			editorApiHeader.find("SetAnimationPreviewParameter(") == std::string::npos &&
			editorApiHeader.find("SwitchAnimationPreviewGraphSet(") == std::string::npos &&
			editorApiHeader.find("TriggerAnimationPreviewSlot(") == std::string::npos &&
			editorApiHeader.find("TickAnimationPreview(") == std::string::npos &&
			editorApiHeader.find("GetAnimationPreviewSnapshot(") == std::string::npos &&
			editorApiHeader.find("GetAnimationPreviewRigSnapshot(") == std::string::npos &&
			editorApiHeader.find("QueryAnimationPreviewSceneEntities(") == std::string::npos &&
			editorApiHeader.find("GetAnimationPreviewWorkingRigDocument(") == std::string::npos &&
			editorApiHeader.find("SetAnimationPreviewRigDefinition(") == std::string::npos &&
			editorApiHeader.find("SetAnimationPreviewRigSocketTransform(") == std::string::npos &&
			editorApiHeader.find("SetAnimationPreviewRigAttachmentProfile(") == std::string::npos &&
			editorApiHeader.find("SetAnimationPreviewTargetBindings(") == std::string::npos &&
			editorApiHeader.find("AdoptAnimationPreviewSceneChanges(") == std::string::npos &&
			editorApiHeader.find("SetAnimationPreviewAttachmentTransform(") == std::string::npos &&
			editorApiHeader.find("SetAnimationPreviewAttachmentBinding(") == std::string::npos &&
			editorApiHeader.find("AdoptAnimationPreviewRig(") == std::string::npos &&
			editorApiHeader.find("DestroyAnimationPreview(") == std::string::npos &&
			sceneAnimationPreviewHeader.find(
				"IAnimationPreviewEditorAPI* m_ActivePreviewAPI") != std::string::npos &&
			sceneAnimationPreviewHeader.find("IEngineEditorAPI* m_ActiveAPI") == std::string::npos &&
			sceneAnimationPreviewSource.find(
				"IAnimationPreviewEditorAPI& previewAPI = editorAPI") != std::string::npos &&
			sceneAnimationPreviewSource.find("editorAPI.SetAnimationPreview") == std::string::npos &&
			sceneAnimationPreviewSource.find("editorAPI.GetAnimationPreview") == std::string::npos &&
			sceneAnimationPreviewIKSource.find(
				"IAnimationPreviewEditorAPI& previewAPI = api") != std::string::npos &&
			sceneAnimationPreviewIKSource.find("api.SetAnimationPreview") == std::string::npos &&
			sceneAnimationPreviewIKSource.find("api.GetAnimationPreview") == std::string::npos &&
			sceneAnimationPreviewRigSource.find(
				"IAnimationPreviewEditorAPI& previewAPI = api") != std::string::npos &&
			sceneAnimationPreviewRigSource.find("api.SetAnimationPreview") == std::string::npos &&
			sceneAnimationPreviewRigSource.find("api.GetAnimationPreview") == std::string::npos,
			"Animation Preview session operations escaped their domain API facet");
		Check(editorApiHeader.find("public IAIEditorAPI") != std::string::npos &&
			editorApiHeader.find("public IMotionMatchingEditorAPI") != std::string::npos &&
			editorApiHeader.find("public IParticleEditorAPI") != std::string::npos &&
			countText(aiApiHeader, "virtual ") == 2 &&
			countText(motionMatchingApiHeader, "virtual ") == 2 &&
			countText(particleApiHeader, "virtual ") == 3 &&
			editorApiHeader.find("GetAIDiagnosticsSnapshot()") == std::string::npos &&
			editorApiHeader.find("GetMotionMatchingDebugSnapshot()") == std::string::npos &&
			editorApiHeader.find("GetParticleAuthoringSchema()") == std::string::npos &&
			editorApiHeader.find("GetParticleDebugSnapshot()") == std::string::npos &&
			aiDebugWindowSource.find("IAIEditorAPI& aiAPI = editorAPI") != std::string::npos &&
			motionMatchingDebugWindowSource.find(
				"IMotionMatchingEditorAPI& motionMatchingAPI = editorAPI") != std::string::npos &&
			sceneWindowSource.find(
				"IMotionMatchingEditorAPI& motionMatchingAPI = editorAPI") != std::string::npos &&
			particleDebugWindowSource.find("IParticleEditorAPI& particleAPI = api") != std::string::npos &&
			inspectorWindowSource.find("IParticleEditorAPI& particleAPI = api") != std::string::npos,
			"AI, Motion Matching, or Particle editor operations escaped their domain API facets");
		const std::array<const char*, 27> pcgFacetMethods = {
			"BuildPcgPlantLods", "GetPcgSplineSnapshot", "CreatePcgSplineAsset",
			"BindPcgSplineAsset", "SelectPcgSpline", "ApplyPcgSplineEdit",
			"ExecutePcgSplineCommand", "AppendPcgSplinePoint", "GetPcgEditorSnapshot",
			"CreatePcgLayer", "RemovePcgLayer", "BindPcgRecipeToScene",
			"GetPcgPlantConfiguration", "GetPcgLayerConfiguration",
			"ApplyPcgPlantConfiguration", "ApplyPcgLayerConfiguration",
			"EditPcgConfiguration", "GetPcgMaskPreview", "GetPcgBrushSnapshot",
			"SelectPcgBrushTarget", "ConfigurePcgBrush", "ApplyPcgBrushInput",
			"EditPcgMaskDocument", "EditPcgMaskData", "CreatePcgExclusionMask",
			"GetPcgInstances", "EditPcgInstance"
		};
		Check(editorApiHeader.find("public IPcgEditorAPI") != std::string::npos &&
			countText(pcgApiHeader, "virtual ") == 28,
			"PCG editor facet inheritance or operation count changed");
		for (const char* method : pcgFacetMethods)
			Check(editorApiHeader.find(std::string(method) + "(") == std::string::npos,
				"PCG editor operation returned to the aggregate API body");
		Check(editorAssetSaveServiceSource.find("IPcgEditorAPI& pcgAPI = editorAPI") != std::string::npos &&
			editorAssetSaveServiceSource.find("editorAPI.BuildPcgPlantLods(") == std::string::npos &&
			editorHistoryAdapterSource.find("IPcgEditorAPI& pcgAPI,") != std::string::npos &&
			editorHistoryAdapterSource.find("editorAPI.GetPcgSplineSnapshot(") == std::string::npos &&
			sceneWindowSource.find("IPcgEditorAPI& pcgAPI = editorAPI") != std::string::npos &&
			sceneWindowHeader.find("FinishSplineGizmo(Vans::EditorAPI::IPcgEditorAPI&") != std::string::npos &&
			sceneWindowSplinesSource.find("IEngineEditorAPI") == std::string::npos &&
			pcgWindowSource.find("IPcgEditorAPI& pcgAPI = api") != std::string::npos &&
			pcgWindowSource.find("api.GetPcg") == std::string::npos &&
			pcgWindowSource.find("api.ApplyPcg") == std::string::npos &&
			pcgWindowHeader.find("ShowMaskCanvas(Vans::EditorAPI::IPcgEditorAPI&") != std::string::npos &&
			pcgWindowHeader.find("ShowInstances(Vans::EditorAPI::IPcgEditorAPI&") != std::string::npos &&
			pcgConfigurationSource.find("IPcgEditorAPI& api") != std::string::npos &&
			pcgMaskSource.find("IPcgEditorAPI& api") != std::string::npos &&
			pcgInstancesSource.find("IPcgEditorAPI& api") != std::string::npos &&
			pcgSplinesSource.find("IPcgEditorAPI& pcgAPI=api") != std::string::npos &&
			pcgSplinesSource.find("api.GetPcg") == std::string::npos &&
			pcgSplinesSource.find("api.ApplyPcg") == std::string::npos,
			"PCG editor consumers escaped their domain API facet");
		Check(editorApiHeader.find("GetParticleDiagnostics(") == std::string::npos &&
			apiHeader.find("GetParticleDiagnostics(") == std::string::npos &&
			apiSource.find("GetParticleDiagnostics(") == std::string::npos &&
			engineDtosHeader.find("ParticleDiagnosticsSnapshot") == std::string::npos &&
			engineDtosHeader.find("ParticleEffectSnapshot") == std::string::npos,
			"unused Particle diagnostics EditorAPI bridge or DTOs returned");
		Check(editorApiHeader.find("public ITerrainEditorAPI") != std::string::npos &&
			countText(terrainApiHeader, "virtual ") == 10 &&
			editorApiHeader.find("GetTerrainSettings()") == std::string::npos &&
			editorApiHeader.find("ApplyTerrainSettings(") == std::string::npos &&
			editorApiHeader.find("GetTerrainEditorSnapshot()") == std::string::npos &&
			editorApiHeader.find("ConfigureTerrainBrush(") == std::string::npos &&
			editorApiHeader.find("ApplyTerrainBrushInput(") == std::string::npos &&
			editorApiHeader.find("UndoTerrainEdit()") == std::string::npos &&
			editorApiHeader.find("RedoTerrainEdit()") == std::string::npos &&
			editorApiHeader.find("RevertTerrainEdits()") == std::string::npos &&
			editorApiHeader.find("SaveTerrainAsset()") == std::string::npos &&
			terrainWindowSource.find("ITerrainEditorAPI& terrainAPI = editorAPI") != std::string::npos &&
			sceneWindowSource.find("ITerrainEditorAPI& terrainAPI = editorAPI") != std::string::npos &&
			editorHistoryAdapterSource.find("ITerrainEditorAPI& terrainAPI,") != std::string::npos,
			"Terrain editor operations escaped their domain API facet");
		Check(editorApiHeader.find("public ITimelineEditorAPI") != std::string::npos &&
			countText(timelineApiHeader, "virtual ") == 8 &&
			editorApiHeader.find("StartTimelinePreview(") == std::string::npos &&
			editorApiHeader.find("ConfigureTimelinePreviewPlayback(") == std::string::npos &&
			editorApiHeader.find("PlayTimelinePreview(") == std::string::npos &&
			editorApiHeader.find("PauseTimelinePreview(") == std::string::npos &&
			editorApiHeader.find("SeekTimelinePreview(") == std::string::npos &&
			editorApiHeader.find("StopTimelinePreview(") == std::string::npos &&
			editorApiHeader.find("GetTimelinePreview(") == std::string::npos &&
			timelinePreviewSessionHeader.find("ITimelineEditorAPI* m_EditorAPI") != std::string::npos &&
			timelinePreviewSessionHeader.find("IEngineEditorAPI") == std::string::npos &&
			timelinePreviewSessionSource.find(
				"EditorAPI::ITimelineEditorAPI& editorAPI") != std::string::npos,
			"Timeline preview operations escaped their domain API facet");
		Check(editorApiHeader.find("public IGAFEditorAPI") != std::string::npos &&
			countText(gafApiHeader, "virtual ") == 20 &&
			editorApiHeader.find("OpenGAFAsset(") == std::string::npos &&
			editorApiHeader.find("SetGAFAssetField(") == std::string::npos &&
			editorApiHeader.find("ResetGAFAssetField(") == std::string::npos &&
			editorApiHeader.find("EditGAFAssetArray(") == std::string::npos &&
			editorApiHeader.find("GetGAFGraphNodeCatalog(") == std::string::npos &&
			editorApiHeader.find("EditGAFGraph(") == std::string::npos &&
			editorApiHeader.find("UndoGAFAsset(") == std::string::npos &&
			editorApiHeader.find("RedoGAFAsset(") == std::string::npos &&
			editorApiHeader.find("RevertGAFAsset(") == std::string::npos &&
			editorApiHeader.find("SaveGAFAsset(") == std::string::npos &&
			editorApiHeader.find("DiffGAFAsset(") == std::string::npos &&
			editorApiHeader.find("GetGAFProjectConfiguration(") == std::string::npos &&
			editorApiHeader.find("GetGAFTagCatalog(") == std::string::npos &&
			editorApiHeader.find("ApplyGAFProjectConfiguration(") == std::string::npos &&
			editorApiHeader.find("GetGAFRuntimeDebugSnapshot(") == std::string::npos &&
			editorApiHeader.find("GetGAFCombatDebugSnapshot(") == std::string::npos &&
			editorApiHeader.find("ControlGAFDebugger(") == std::string::npos &&
			editorApiHeader.find("ControlGAFTrace(") == std::string::npos &&
			editorApiHeader.find("SimulateGAFAction(") == std::string::npos &&
			countText(gameplayActionEditorHeader, "IEngineEditorAPI&") == 1 &&
			gameplayActionEditorHeader.find("IGAFEditorAPI&") != std::string::npos &&
			gameplayActionEditorSource.find("IGAFEditorAPI& gafAPI = editorAPI") != std::string::npos &&
			countText(gafDebuggerHeader, "IEngineEditorAPI&") == 1 &&
			gafDebuggerHeader.find("IGAFEditorAPI&") != std::string::npos &&
			gafDebuggerSource.find("IGAFEditorAPI& gafAPI = editorAPI") != std::string::npos &&
			countText(gafDebuggerSource, "GetPlayState()") == 1 &&
			gafDebuggerSource.find("DrawRuntimeDebugger(gafAPI, runtimePaused)") != std::string::npos &&
			countText(projectSettingsWindowHeader, "IEngineEditorAPI&") == 1 &&
			projectSettingsWindowHeader.find("IGAFEditorAPI&") != std::string::npos &&
			projectSettingsWindowSource.find("IGAFEditorAPI& gafAPI = editorAPI") != std::string::npos &&
			sceneWindowSource.find("IGAFEditorAPI& gafAPI = editorAPI") != std::string::npos &&
			sceneWindowSource.find("editorAPI.GetGAFCombatDebugSnapshot(") == std::string::npos,
			"GAF authoring, debugging, configuration, or combat overlay escaped its domain API facet");

        const glm::vec3 cameraPosition(4, 7, 15), target(-2, 1, 0);
        const glm::mat4 view = glm::lookAt(cameraPosition, target, glm::vec3(0, 1, 0));
        const auto projection = glm::perspective(glm::radians(53.f), 1.7f, .1f, 200.f);
        for (const auto uv : {glm::vec2(.5f), glm::vec2(.1f,.8f), glm::vec2(.9f,.2f)})
        {
            glm::vec3 rayOrigin, rayDirection; float length;
            Check(VansSceneViewMath::BuildRay(
                projection * view, uv, rayOrigin, rayDirection, length), "perspective unprojection");
            const auto point = projection * view * glm::vec4(rayOrigin + rayDirection * 5.f, 1);
            Check(glm::length(glm::vec2(point.x/point.w*.5f+.5f, .5f-point.y/point.w*.5f)-uv)<1e-4f, "ray/image reprojection mismatch");
        }
        glm::vec3 originA, directionA, originB, directionB; float length;
        const auto ortho = glm::ortho(-5.f,5.f,-3.f,3.f,.1f,100.f)*view;
        Check(VansSceneViewMath::BuildRay(ortho,{.1f,.3f},originA,directionA,length) &&
            VansSceneViewMath::BuildRay(ortho,{.9f,.7f},originB,directionB,length), "orthographic unprojection");
        Check(glm::length(directionA-directionB)<1e-5f && glm::length(originA-originB)>1, "orthographic ray origins");
        Check(!VansSceneViewMath::BuildRay(projection*view,{-1,.5f},originA,directionA,length), "letterbox click accepted");
        Check(!VansSceneViewMath::BuildRay(glm::mat4(0),{.5f,.5f},originA,directionA,length), "singular camera accepted");
        Check(VansSceneViewMath::IntersectBounds(
            {0,0,0}, {1,0,0}, {2,-1,-1}, {3,1,1}, 10), "view ray missed finite bounds");
        Check(!VansSceneViewMath::IntersectBounds(
            {0,0,0}, {1,0,0}, {2,-1,-1}, {3,1,1}, 1.9f), "view ray ignored distance bound");
        Check(!VansSceneViewMath::IntersectBounds(
            {0,2,0}, {1,0,0}, {2,-1,-1}, {3,1,1}, 10), "parallel view ray crossed bounds");

        EditorSceneBounds bounds{true, {-10,-2,-1}, {10,2,1}};
        for (float aspect : {.3f,1.f,3.f})
        {
            glm::vec3 position; float farClip;
            Check(VansSceneViewMath::CalculateFramePosition(
                bounds.available, Vec(bounds.minimum), Vec(bounds.maximum),
                {0,0,-1},45,aspect,.1f,position,farClip), "frame calculation");
            const auto vp=glm::perspective(glm::radians(45.f),aspect,.1f,farClip)*glm::lookAt(position,glm::vec3(0),glm::vec3(0,1,0));
            for (int i=0;i<8;++i)
            {
                const auto p=vp*glm::vec4(i&1?10.f:-10.f,i&2?2.f:-2.f,i&4?1.f:-1.f,1);
                Check(std::abs(p.x/p.w)<1 && std::abs(p.y/p.w)<1 && p.z/p.w>=0 && p.z/p.w<1, "framed corner clipped");
            }
        }
        CameraDevice device; VansCamera camera(&device); VansEditorCameraController control;
        const auto rotation=camera.CaptureView().pose.rotationDegrees;
        Check(control.Frame(&camera,bounds,camera.GetAspectRatio()), "camera rejected frame");
        VansEditorCameraInputState input; input.editMode=true; input.deltaTime=.05f;
        for(int i=0;i<5;++i) control.Update(&camera,input);
        const auto framed=camera.CaptureView();
        Check(glm::length(rotation-framed.pose.rotationDegrees)<1e-5f, "frame changed view orientation");
        bounds.minimum.x+=100; bounds.maximum.x+=100;
        Check(control.Frame(&camera,bounds,camera.GetAspectRatio()), "second frame");
        input.cancelFraming=true; control.Update(&camera,input);
        Check(glm::length(camera.CaptureView().pose.position-framed.pose.position)<1e-5f, "cancel did not stop focus");
        Check(control.Frame(&camera,bounds,camera.GetAspectRatio()), "third frame");
        input.cancelFraming=false; input.editMode=false; control.Update(&camera,input);
        Check(glm::length(camera.CaptureView().pose.position-framed.pose.position)<1e-5f, "focus leaked into play mode");

        auto& selection=VansEditorSelectionService::Get();
        EditorObjectHandle first,second;
        first.domain=second.domain=EditorObjectDomain::SceneEntity;
        first.guid=first.entityGuid="first"; second.guid=second.entityGuid="second";
        selection.Apply(EditorSelectionOperation::Replace,{first,second},second,"test");
        selection.Apply(EditorSelectionOperation::Toggle,{second},second,"test");
        Check(selection.Contains(first) && !selection.Contains(second) && selection.EntityGuid()=="first", "toggle resurrected active selection");
		selection.SelectScene("test");
		Check(selection.IsSceneSelected() && selection.EntityGuid().empty() && selection.AssetPath().empty(),
			"scene selection was not derived from the active snapshot");
		selection.SelectAsset("Assets/Test.vasset", "test");
		Check(!selection.IsSceneSelected() && selection.EntityGuid().empty() &&
			selection.AssetPath() == std::filesystem::path("Assets/Test.vasset"),
			"asset selection was not derived from the active snapshot");
        selection.Clear("test");
		VansEditorPrefabSession prefabSession;
		prefabSession.Queue({VansPrefabRequest::Kind::Open, {}, "Assets/Test.vprefab"});
		Check(prefabSession.HasPendingRequests(), "Prefab session lost a queued request");
		const auto prefabRequests = prefabSession.TakePendingRequests();
		Check(prefabRequests.size() == 1 &&
			prefabRequests.front().kind == VansPrefabRequest::Kind::Open &&
			prefabRequests.front().path == "Assets/Test.vprefab" &&
			!prefabSession.HasPendingRequests(),
			"Prefab session changed queue take semantics");
		prefabSession.Status() = "test";
		prefabSession.Reset();
		Check(prefabSession.Status().empty() && !prefabSession.HasStage() &&
			!prefabSession.HasPendingRequests(),
			"Prefab session reset did not clear its complete lifecycle state");
		Check(editorSelectionServiceHeader.find("m_ActiveEntityGuid") == std::string::npos &&
			editorSelectionServiceHeader.find("m_ActiveAssetPath") == std::string::npos &&
			editorSelectionServiceHeader.find("m_SceneSelected") == std::string::npos &&
			editorSelectionServiceSource.find("ReplaceFacadeStateFromActive") == std::string::npos &&
			!std::filesystem::exists(engineRoot / "Source" / "EngineCore" / "EditorCore" / "VansEditorSelection.h"),
			"selection mirror fields or the forwarding facade returned");
		Check(editorPrefabSessionHeader.find("class VansEditorPrefabSession final") != std::string::npos &&
			editorWindowHeader.find("static VansEditorPrefabSession m_PrefabSession") != std::string::npos &&
			editorPrefabSource.find("std::vector<PrefabRequest> requests") == std::string::npos &&
			editorPrefabSource.find("std::unique_ptr<PrefabSession> session") == std::string::npos &&
			editorPrefabSource.find("std::string status") == std::string::npos,
			"Prefab request, stage, or status state escaped its explicit owner");
		Check(editorAssetSaveServiceHeader.find("SaveSceneAndAssets(") != std::string::npos &&
			editorAssetSaveServiceSource.find("VansStagedFileTransaction transaction") != std::string::npos &&
			animationPreviewIKSource.find("VansEditorAssetSaveService::Get().SaveSceneAndAssets") != std::string::npos &&
			!std::filesystem::exists(engineRoot / "Source" / "EngineCore" / "EditorCore" /
				"Animation" / "VansSceneAnimationSaveService.cpp") &&
			!std::filesystem::exists(engineRoot / "Source" / "EngineCore" / "SceneCore" /
				"VansSceneSaveService.cpp"),
			"duplicate Scene or animation save transaction returned");
		Check(sceneRuntimeProjectionHeader.find("ProjectAuthoringEntity(") != std::string::npos &&
			sceneRuntimeProjectionHeader.find("ProjectAuthoringEntityFromSceneRoot(") != std::string::npos &&
			runtimePreviewProjectorSource.find("VansSceneRuntimeProjection::ProjectAuthoringEntity(") != std::string::npos &&
			runtimePreviewProjectorSource.find("VansSceneRuntimeProjection::ProjectAuthoringEntityFromSceneRoot(") != std::string::npos &&
			runtimePreviewProjectorSource.find("FindSerializedComponent") == std::string::npos &&
			runtimePreviewProjectorSource.find("BuildRuntimeTransformPreview") == std::string::npos &&
			runtimePreviewProjectorSource.find("AppendRuntimeMaterialOverridePreviews") == std::string::npos,
			"Editor runtime preview restored a parallel Scene component reader");
		Check(engineDtosHeader.find("struct ScenePropertyValue") == std::string::npos &&
			engineDtosHeader.find("std::vector<Vans::VansSerializedValue> sceneEntities") != std::string::npos &&
			editorObjectReferenceHeader.find("ValidateDocumentPropertyPointer(") != std::string::npos &&
			sceneEditServiceSource.find("ValidateDocumentPropertyPointer(") != std::string::npos &&
			assetDocumentEditSource.find("ValidateDocumentPropertyPointer(") != std::string::npos &&
			sceneEditServiceSource.find("ValidatePointer(") == std::string::npos &&
			assetDocumentEditSource.find("ValidatePointer(") == std::string::npos &&
			apiSource.find("ToSerializedValue(") == std::string::npos &&
			apiSource.find("FromSerializedValue(") == std::string::npos &&
			!std::filesystem::exists(engineRoot / "Source" / "EngineCore" / "EngineAPILayer" /
				"Private" / "ScenePropertyValueBuilders.h") &&
			!std::filesystem::exists(engineRoot / "Source" / "EngineCore" / "EditorCore" /
				"VansScenePropertyValueAdapter.cpp"),
			"parallel serialized-value conversion or JSON Pointer validation returned");
		Check(countText(sceneDocumentSource, "VansPrefabResolver::ResolveScene(") == 1 &&
			countText(sceneDocumentSource, "VansSceneSchema::ValidateSceneJson(") == 1 &&
			sceneDocumentSource.find("RebuildResolvedViewFromAuthoring(") != std::string::npos &&
			sceneSchemaHeader.find("ValidateEntityComponents(") != std::string::npos &&
			sceneSchemaSource.find("VansComponentTypeCatalog::IsSceneAuthoringType") != std::string::npos &&
			sceneRuntimeProjectionHeader.find("ValidateEntityComponentTypes") == std::string::npos &&
			sceneRuntimeProjectionSource.find("ValidateEntityComponentTypes") == std::string::npos,
			"parallel Scene resolved-view rebuild or component schema validation returned");
		Check(enginePathsHeader.find("FindEngineRoot(") != std::string::npos &&
			enginePathsHeader.find("DiscoverEngineRoot(") != std::string::npos &&
			enginePathsHeader.find("NormalizeEngineRoot(") != std::string::npos &&
			enginePathsSource.find("GetModuleFileNameW") != std::string::npos &&
			enginePathsSource.find("EngineAssets") != std::string::npos &&
			projectManagerHeader.find("ConfigureEngineRoot(") != std::string::npos &&
			projectManagerSource.find("VansEnginePaths::NormalizeEngineRoot") != std::string::npos &&
			configurationEditorRuntimeHostSource.find("VansEnginePaths::DiscoverEngineRoot") != std::string::npos &&
			configurationEditorRuntimeHostSource.find("ConfigureEngineRoot(engineRoot") != std::string::npos &&
			runtimeHostSource.find("ConfigureEngineRoot(contentRoot") != std::string::npos &&
			renderBootstrapHeader.find("inline constexpr VansRenderBootstrapSettings") != std::string::npos &&
			renderBootstrapHeader.find("cascadeCount = 4") != std::string::npos &&
			renderBootstrapHeader.find("punctualShadowAtlasWidth = 4096") != std::string::npos &&
			sourceManifest.find("ProjectSystem/VansEnginePaths.cpp") != std::string::npos &&
			sourceManifest.find("Configration/VansConfigration.cpp") == std::string::npos &&
			!std::filesystem::exists(engineRoot / "Source" / "EngineCore" / "Configration" /
				"VansConfigration.h") &&
			!std::filesystem::exists(engineRoot / "Source" / "EngineCore" / "Configration" /
				"VansConfigration.cpp"),
			"implicit engine paths or the mutable mixed configuration singleton returned");
		std::size_t untypedRuntimeWorldFlushCount = 0;
		const std::filesystem::path engineCoreRoot = engineRoot / "Source" / "EngineCore";
		for (const auto& entry : std::filesystem::recursive_directory_iterator(engineCoreRoot))
		{
			if (!entry.is_regular_file() ||
				(entry.path().extension() != ".cpp" && entry.path().extension() != ".h"))
				continue;
			untypedRuntimeWorldFlushCount += countText(readSource(entry.path()), "FlushCommands(");
		}
		Check(untypedRuntimeWorldFlushCount == 0 &&
			runtimeWorldHeader.find("enum class VansRuntimeCommandCommitPoint") != std::string::npos &&
			runtimeWorldHeader.find("CommitCommands(VansRuntimeCommandCommitPoint") != std::string::npos &&
			runtimeWorldSource.find("VANS_ASSERT_FRAME_PHASE(VansFramePhase::GameLogic)") != std::string::npos &&
			runtimeWorldSource.find("m_CommandCommitInProgress") != std::string::npos &&
			runtimeWorldSource.find("m_ComponentEffectiveEnabledDirty") != std::string::npos,
			"untyped runtime-world command flush or unguarded commit boundary returned");
		Check(sceneVehicleBuilderHeader.find("struct VansSceneVehicleBuildRequest") != std::string::npos &&
			sceneVehicleBuilderHeader.find("std::string ownerEntityGuid") != std::string::npos &&
			sceneVehicleBuilderHeader.find("AddVehiclePlaceholder") == std::string::npos &&
			sceneVehicleBuilderSource.find("FindObjectByGuid(request.ownerEntityGuid)") != std::string::npos &&
			sceneVehicleBuilderSource.find("objectIndex") == std::string::npos &&
			sceneObjectBuildSource.find("vehicleBuildRequests.push_back") != std::string::npos &&
			sceneObjectBuildSource.find("vehicleBuild.builtVehicles") != std::string::npos &&
			sceneObjectBuildSource.find("vehicleObjectConfigs") == std::string::npos &&
			sceneObjectBuildSource.find("vehicleComponents") == std::string::npos,
			"Vehicle SceneBuild restored positional joins or pre-runtime placeholder components");
		Check(materialLiveEditHeader.find("ApplyRendererMaterialOverrideBatch(") != std::string::npos &&
			materialLiveEditSource.find("class MaterialOverrideBatchTransaction") != std::string::npos &&
			previewSection.find("class MaterialOverrideTransaction") == std::string::npos &&
			previewSection.find("ApplyRendererMaterialOverrideBatch(") != std::string::npos,
			"EngineAPI regained ownership of the renderer material transaction");
		Check(cameraArbiterHeader.find("IsBaseCameraWriteWindowOpen()") != std::string::npos &&
			cameraArbiterHeader.find("enum class FrameStage") != std::string::npos &&
			cameraArbiterSource.find("FrameStage::AwaitingBaseCapture") != std::string::npos &&
			cameraArbiterSource.find("FrameStage::BaseCaptured") != std::string::npos &&
			apiSource.find("IsBaseCameraWriteWindowOpen()") != std::string::npos,
			"camera base-write and contribution phases became implicit again");
		std::cout<<"EDITOR_SCENE_INTERACTION_PASS window_catalog=24+single_state+configured_defaults editor_config=strict+fonts+layout+windows+platform window_registry=32+ordered+typed+owned debug_view_state=owned+injected+automation shell_menu=18_assets+command_sink+lazy_availability shell_commands=shortcut_priority+semantic_dispatch authoring_commands=save_all+reload+exit_gate package_session=dirty_order+request_forward+result_state prefab_session=owned+queued+stage play_toolbar=edit+play+pause+scene_gate play_commands=gate+timer+physics+state+reload_order project_session=selector+loaded+pending_request project_switch=dirty_gate+unload+open+publish_order scene_load_session=current+pending+mode scene_document_session=paired_owner+prefab_swap scene_load_controller=gate+prepare+publish+mode_order scene_load_request=encapsulated theme_owner=fonts+base_style+49_colors+toolbar history=chronological history_adapter=five_sources+runtime_patch selection_snapshot=derived+single_service save_transaction=single+scene_assets entity_preview_projection=scene_reader+dto_translation serialized_value=direct_dto+single_pointer_validator scene_document=rebuild+live_diagnostics+schema_validation authoring_boundary=instance_sessions+scene_host+asset_creation_transaction+runtime_neutral_preview engine_configuration=explicit_paths+project_owner+immutable_render_bootstrap runtime_command_commit=typed_points+game_logic_guard+batched_hierarchy_recompute scene_vehicle=guid_keyed+postbuild_publish material_override_transaction=service_owned camera_base_write=explicit_window+phase_assert api_facets=animation_12+animation_preview_20+asset_authoring_8+asset_catalog_3+audio_6+ai_1+gaf_19+gi_6+motion_matching_1+particle_2+pcg_27+play_mode_4+project_13+reflection_probe_9+render_20+runtime_command_history_4+runtime_frame_13+runtime_physics_9+runtime_scene_9+scene_interaction_9+scene_settings_9+script_lifecycle_3+shader_2+terrain_9+timeline_7+ui_7+vehicle_1+water_3 asset_meta_dead_bridge=removed lighting_commit_dead_bridge=removed particle_dead_bridge=removed project_mesh_alias_dead_bridge=removed reflection_probe_rebuild_dead_bridge=removed runtime_history_can_helpers_dead_bridge=removed runtime_resource_ready_dead_bridge=removed runtime_script_module_dead_bridge=removed runtime_transform_command_dead_bridge=removed runtime_unique_name_dead_bridge=removed runtime_vehicle_input_dead_bridge=removed runtime_vehicle_step=private viewport_texture_dead_bridge=removed transform=preview+single_scene_commit+undo_redo preview_ownership=instance_registry+session_owner+runtime_detach ray_projection=perspective+orthographic bounds=finite+limited+parallel frame=wide+tall cancel=pass play_isolation=pass selection_toggle=pass\n";
        return true;
    }
    catch(const std::exception& e) { std::cerr<<"EDITOR_SCENE_INTERACTION_FAIL "<<e.what()<<'\n'; return false; }
}

// 由已有 Vulkan fixture 调用，验证与编辑器相同的查询实现，而非另写一个测试用算法。
void TestEditorScenePickingGpu(VansGraphics::VansVKDevice& device, VansGraphics::VansMesh& imported)
{
    GeometryExecutor executor(device);
    VansRuntimeWorld world;
    const auto parent=world.CreateEntity({"parent","Parent"});
    const auto nearEntity=world.CreateEntity({"near","Near",parent});
    const auto farEntity=world.CreateEntity({"far","Far"});
    GeometryNode nearNode(device.GetLogicDevice()),farNode(device.GetLogicDevice());
    nearNode.m_Mesh=farNode.m_Mesh=&imported;
    nearNode.m_EntityGuid="near"; farNode.m_EntityGuid="far";
    farNode.SetTransformData({0,-4,0},{0,0,0},{1,1,1});
    const auto nearComponent=world.AddComponent(nearEntity,VansRuntimeComponentType_Render,VansRuntimeRenderComponent{&nearNode,{}});
    world.AddComponent(farEntity,VansRuntimeComponentType_Render,VansRuntimeRenderComponent{&farNode,{}});
    VansEditorSceneQuery query;
    EditorScenePickRequest request; request.ray={{-1,-10,-1},{0,-1,0}}; request.maxDistance=20; request.selectableEntities={"near","far"};
    auto result=query.Pick(world,request,&executor);
    Check(result.success && result.entityGuid=="near", "GPU mesh without CPU data was not picked");
    Check(executor.reads==1,"shared mesh was read more than once");
    Check(query.Pick(world,request,&executor).entityGuid=="near" && executor.reads==1,"warm pick performed GPU readback");
    request.ray.origin={1.5f,-10,1.5f};
    Check(query.Pick(world,request,&executor).entityGuid.empty(),"empty part of triangle bounds was picked");
    request.ray.origin={-1,-10,-1};
    world.SetComponentEnabled(nearComponent,false);
    Check(query.Pick(world,request,&executor).entityGuid=="far","disabled component intercepted pick");
    world.SetComponentEnabled(nearComponent,true);
    request.selectableEntities={"parent","far"};
    Check(query.Pick(world,request,&executor).entityGuid=="parent","runtime child did not resolve to document ancestor");
    request.selectableEntities={"far"};
    Check(query.Pick(world,request,&executor).entityGuid=="far","unmapped near node swallowed mapped hit");
    request.selectableEntities={"near","far"};
    nearNode.SetTransformData({8,2,-3},{25,70,35},{-2,.5f,3});
    const auto model=nearNode.GetTransformMatrix();
    const glm::vec3 point(model*glm::vec4(-1,-12,-1,1));
    const glm::vec3 normal=glm::normalize(glm::transpose(glm::inverse(glm::mat3(model)))*glm::vec3(0,1,0));
    request.ray.origin={point.x+normal.x*2,point.y+normal.y*2,point.z+normal.z*2};
    request.ray.direction={-normal.x,-normal.y,-normal.z};
    result=query.Pick(world,request,&executor);
    Check(result.success && result.entityGuid=="near","rotated offset mesh with negative/nonuniform scale missed");
    Check(executor.reads==1,"transform change rebuilt shared geometry");
    const auto bounds=query.Bounds(world,{"parent"},&executor);
    Check(bounds.available && point.x>=bounds.minimum.x && point.x<=bounds.maximum.x &&
        point.y>=bounds.minimum.y && point.y<=bounds.maximum.y && point.z>=bounds.minimum.z && point.z<=bounds.maximum.z,
        "parent focus excluded rotated child");
    const auto emptyEntity=world.CreateEntity({"empty","Empty"});
    const auto transformId=Vans::VansTransformStore::Allocate();
    Vans::VansTransform emptyTransform = Vans::VansTransformStore::Read(transformId);
    emptyTransform.m_Position={500,600,700};
    Vans::VansTransformStore::Write(transformId, emptyTransform);
    world.AddComponent(emptyEntity,VansRuntimeComponentType_Transform,VansRuntimeTransformComponent{transformId});
    const auto mixedBounds=query.Bounds(world,{"parent","empty"},&executor);
    Vans::VansTransformStore::Release(transformId);
    Check(mixedBounds.available && mixedBounds.maximum.x>=500 && mixedBounds.maximum.y>=600 &&
        mixedBounds.maximum.z>=700 && mixedBounds.minimum.x<=point.x,"mixed selection excluded empty object");
    request.ray.direction={0,0,0};
    Check(!query.Pick(world,request,&executor).success,"invalid ray mutated selection as a miss");
    world.DestroyEntity(nearEntity);
    request.ray={{-1,-10,-1},{0,-1,0}};
    Check(query.Pick(world,request,&executor).entityGuid=="far","deleted node survived in pick candidates");
    std::cout<<"EDITOR_SCENE_PICKING_GPU_PASS cpu_data=released triangles=precise shared_cache=1_read transforms=rotated+negative+nonuniform disabled=filtered owner_mapping=pass deletion=pass parent_bounds=pass\n";
}
