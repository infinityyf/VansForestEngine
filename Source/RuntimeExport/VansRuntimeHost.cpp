#include "VansRuntimeHost.h"

#include "../EngineCore/AssetCore/Importers/Shader/VansShaderArtifactCache.h"
#include "../EngineCore/AssetCore/VansAssetDatabase.h"
#include "../EngineCore/AudioCore/VansAudioMixConfig.h"
#include "../EngineCore/EventCore/VansEventBus.h"
#include "../EngineCore/PhysicsCore/VansPhysics.h"
#include "../EngineCore/PhysicsCore/VansPhysicsVehicle.h"
#include "../EngineCore/ProjectSystem/VansProjectManager.h"
#include "../EngineCore/RenderCore/SceneBuild/VansSceneProjectResourceBuilder.h"
#include "../EngineCore/RenderCore/VansCamera.h"
#include "../EngineCore/RenderCore/VansGraphicsDevice.h"
#include "../EngineCore/RenderCore/VansRenderSystem.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/RenderCore/VansShaderManager.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RuntimeCore/VansPackageManifest.h"
#include "../EngineCore/RuntimeCore/VansRuntimeFrameScheduler.h"
#include "../EngineCore/RuntimeCore/VansRuntimePhaseGuard.h"
#include "../EngineCore/RuntimeCore/VansRuntimeWindow.h"
#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include "../EngineCore/RuntimeUI/Public/VansUISystem.h"
#include "../EngineCore/SceneCore/VansAssetObjectBootstrapper.h"
#include "../EngineCore/SceneCore/VansPackagedResourcePlan.h"
#include "../EngineCore/SceneCore/VansSceneDocumentLoader.h"
#include "../EngineCore/ScriptCore/VansScriptContext.h"
#include "../EngineCore/Util/VansInputManager.h"
#include "../EngineCore/Util/VansJobSystem.h"
#include "../EngineCore/Util/VansLog.h"
#include "../EngineCore/VansTimer.h"

#include <mutex>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace Vans
{
namespace
{
std::mutex g_ActiveRuntimeMutex;
VansRuntimeHost* g_ActiveRuntime = nullptr;

bool ClaimActiveRuntime(VansRuntimeHost* runtime)
{
	std::lock_guard<std::mutex> lock(g_ActiveRuntimeMutex);
	if (g_ActiveRuntime != nullptr && g_ActiveRuntime != runtime)
		return false;
	g_ActiveRuntime = runtime;
	return true;
}

void ReleaseActiveRuntime(VansRuntimeHost* runtime)
{
	std::lock_guard<std::mutex> lock(g_ActiveRuntimeMutex);
	if (g_ActiveRuntime == runtime)
		g_ActiveRuntime = nullptr;
}

class VansRuntimeSceneFramePort final : public IVansRuntimeFramePort
{
  public:
	VansRuntimeSceneFramePort(VansGraphics::VansScene& scene, VansScriptContext* scriptContext)
		: m_Scene(scene), m_ScriptContext(scriptContext)
	{
	}

	void SyncPhysicsTransforms(const VansRuntimeFrameContext&) override
	{
		m_Scene.UpdatePhysicsTransforms();
	}

	void UpdateNonCameraScripts(const VansRuntimeFrameContext&) override
	{
		if (!m_ScriptContext)
			return;
		m_ScriptContext->SetScene(&m_Scene);
		m_ScriptContext->VansScriptUpdateNonCameraScripts();
	}

	void AdvanceCameraRuntime(const VansRuntimeFrameContext& context) override
	{
		m_Scene.AdvanceCameraRuntime(context.m_DeltaSeconds);
	}

	void UpdateActionsEarly(const VansRuntimeFrameContext& context) override
	{
		m_Scene.UpdateActionsEarly(context.m_DeltaSeconds);
	}

	void UpdateAI(const VansRuntimeFrameContext& context) override
	{
		m_Scene.UpdateAI(context.m_DeltaSeconds);
	}

	void PrepareCharacterLocomotion(const VansRuntimeFrameContext& context) override
	{
		m_Scene.PrepareCharacterLocomotion(static_cast<float>(context.m_DeltaSeconds));
	}

	void FlushCharacterControllerTransforms(const VansRuntimeFrameContext&) override
	{
		m_Scene.UpdateCharControllerTransforms();
	}

	void UpdateTimelinesPostScript(const VansRuntimeFrameContext& context) override
	{
		m_Scene.UpdateTimelinesPostScript(context.m_DeltaSeconds);
	}

	void RunActionLateContinuation(const VansRuntimeFrameContext&) override
	{
		m_Scene.RunActionLateContinuation();
	}

	void BeginCameraControlFrame(const VansRuntimeFrameContext&) override
	{
		m_Scene.BeginCameraControlFrame();
	}

	void UpdateCameraScripts(const VansRuntimeFrameContext&) override
	{
		if (m_ScriptContext)
			m_ScriptContext->VansScriptUpdateCameraScripts();
	}

	void CaptureCameraControlBase(const VansRuntimeFrameContext&) override
	{
		m_Scene.CaptureCameraControlBase();
	}

	void UpdateTimelinesCamera(const VansRuntimeFrameContext& context) override
	{
		m_Scene.UpdateTimelinesCamera(context.m_DeltaSeconds);
	}

	void ResolveCameraControlFrame(const VansRuntimeFrameContext&) override
	{
		m_Scene.ResolveCameraControlFrame();
	}

  private:
	VansGraphics::VansScene& m_Scene;
	VansScriptContext* m_ScriptContext = nullptr;
};

Vans::VansAssetArtifactFormat ArtifactFormatFromString(const std::string& value)
{
	if (value == "imported")
		return Vans::VansAssetArtifactFormat::Imported;
	if (value == "source")
		return Vans::VansAssetArtifactFormat::Source;
	if (value == "cooked" || value == "gaf-cooked")
		return Vans::VansAssetArtifactFormat::Cooked;
	return Vans::VansAssetArtifactFormat::None;
}

std::vector<Vans::VansAssetRecord> BuildRuntimeAssetRecords(
	const std::vector<Vans::VansPackagedAssetIndexRecord>& assetIndex)
{
	std::vector<Vans::VansAssetRecord> records;
	records.reserve(assetIndex.size());
	for (const Vans::VansPackagedAssetIndexRecord& indexRecord : assetIndex)
	{
		Vans::VansAssetGuid guid;
		if (!Vans::VansAssetGuid::TryParse(indexRecord.guid, guid))
			continue;

		Vans::VansAssetRecord record;
		record.guid = guid;
		record.type = Vans::VansAssetDatabase::ParseSerializedType(indexRecord.type);
		record.state = indexRecord.missing ? Vans::VansAssetState::Missing : Vans::VansAssetState::Discovered;
		record.sourcePath = indexRecord.sourcePath;
		record.authoringPath = indexRecord.authoringPath;
		record.artifactPath = indexRecord.artifactPath;
		record.metaPath = indexRecord.metaPath;
		record.artifactFormat = ArtifactFormatFromString(indexRecord.artifactFormat);
		if (record.sourcePath.empty() && record.artifactFormat == Vans::VansAssetArtifactFormat::Source)
			record.sourcePath = record.artifactPath;
		record.sourceHash = indexRecord.sourceHash;
		record.metaHash = indexRecord.metaHash;
		records.push_back(std::move(record));
	}
	return records;
}
} // namespace

VansRuntimeHost::VansRuntimeHost() = default;

VansRuntimeHost::~VansRuntimeHost()
{
	Shutdown();
}

bool VansRuntimeHost::IsProjectReady() const
{
	switch (m_State)
	{
	case VansRuntimeLifecycleState::ProjectReady:
	case VansRuntimeLifecycleState::GraphicsReady:
	case VansRuntimeLifecycleState::SceneReady:
	case VansRuntimeLifecycleState::Running:
	case VansRuntimeLifecycleState::Quiesced:
		return true;
	default:
		return false;
	}
}

bool VansRuntimeHost::IsGraphicsReady() const
{
	return m_State == VansRuntimeLifecycleState::GraphicsReady || m_State == VansRuntimeLifecycleState::SceneReady ||
		   m_State == VansRuntimeLifecycleState::Running || m_State == VansRuntimeLifecycleState::Quiesced;
}

void VansRuntimeHost::SetError(std::string message)
{
	m_LastError = std::move(message);
}

bool VansRuntimeHost::InitializeCore()
{
	if (m_Services.IsStarted())
		return true;

	VANS_INIT_MAIN_THREAD();
	std::string error;
	VansEngine::VansAudioMixConfig audioConfig;
	if (!VansProjectManager::Get().GetAudioMixConfig(audioConfig, error))
	{
		SetError("Cannot read project audio configuration: " + error);
		return false;
	}
	if (!m_Services.Start(audioConfig.device, error))
	{
		SetError(std::move(error));
		return false;
	}

	if (!m_Services.IsAudioAvailable())
		VANS_LOG_WARN("[ForestRuntime] Audio system initialization failed; continuing without audio");

	return true;
}

void VansRuntimeHost::RollbackSceneSession()
{
	VansEngine::VansPhysicsSystem::GetInstance().SetPreSimulateCallback(nullptr);
	if (m_Services.IsSimulationRunning())
	{
		m_Services.PauseSimulation();
		m_Services.StopSimulation();
	}

	if (m_RenderSystem)
		m_RenderSystem->WaitForIdle();
	else if (m_Device)
		m_Device->WaitForDevice();

	if (m_ScriptContext)
	{
		m_ScriptContext->ShutdownLua();
		m_ScriptContext->SetScene(nullptr);
		m_ScriptContext.reset();
	}
	if (m_Scene)
	{
		if (m_Scene->IsSceneReady() || m_Scene->IsSceneSwitching())
			m_Scene->UnloadScene(m_Device.get());
		m_Scene->UnloadProjectResources(m_Device.get());
	}
	if (m_Scene)
		m_Scene->InjectCamera(nullptr);
	m_Camera.reset();
	m_State = VansRuntimeLifecycleState::GraphicsReady;
}

void VansRuntimeHost::ShutdownGraphics()
{
	VansEngine::VansPhysicsSystem::GetInstance().SetPreSimulateCallback(nullptr);

	if (m_RenderSystem)
	{
		m_RenderSystem->WaitForIdle();
	}
	else if (m_Device)
	{
		m_Device->WaitForDevice();
	}

	// Noesis renderers are render-thread-affine and still reference the Views
	// owned by runtime UI documents. Stop frame execution while those documents
	// are intact; script teardown and scene unload may close their UI screens.
	// Joining here also makes the later pipeline-cache flush externally
	// synchronized with all render-thread child-cache users.
	if (m_RenderSystem && m_FrameExecutionStarted)
	{
		m_RenderSystem->ShutdownFrameExecution();
		m_FrameExecutionStarted = false;
	}
	// With all IRenderer instances stopped, release Views and UI callbacks while
	// the Lua VM is still alive. Destroying Noesis documents after lua_close can
	// run dependency-object callbacks against an invalid scripting state.
	VansRuntime::VansUISystem::Get().Shutdown();

	if (m_ScriptContext)
	{
		m_ScriptContext->ShutdownLua();
		m_ScriptContext->SetScene(nullptr);
		m_ScriptContext.reset();
	}
	if (m_Scene)
	{
		if (m_Scene->IsSceneReady() || m_Scene->IsSceneSwitching())
			m_Scene->UnloadScene(m_Device.get());
		m_Scene->UnloadProjectResources(m_Device.get());
		m_Scene->BindRenderThreadTransactionExecutor(nullptr);
	}
	m_Camera.reset();
	if (m_Device)
		m_Device->GetPipelineCacheService().Flush(VansGraphics::VansPipelineCacheFlushReason::Manual);
	if (m_Device)
		m_Device->GetPipelineCacheService().Flush(VansGraphics::VansPipelineCacheFlushReason::Shutdown);
	// The headless/package-load path never creates a runtime window or
	// initializes the input manager. Keep shutdown symmetric with the
	// window lifecycle so the generic headless API does not assert on an
	// uninitialized main-thread contract.
	if (m_Window)
		Vans::VansInputManager::Get().Shutdown();
	VansGraphics::VansShaderManager::Get().Clear();
	m_Scene.reset();
	::m_Scene = nullptr;
	m_RenderSystem.reset();
	m_Device.reset();
	::m_GraphicsDevice = nullptr;
	m_Window.reset();
	m_State = IsProjectReady() ? VansRuntimeLifecycleState::ProjectReady : VansRuntimeLifecycleState::Stopped;
}

bool VansRuntimeHost::LoadPackage(const char* manifestPath)
{
	if (IsProjectReady())
	{
		SetError("A package is already loaded by this runtime handle");
		return false;
	}
	if (!manifestPath || manifestPath[0] == '\0')
	{
		SetError("Package manifest path is empty");
		return false;
	}

	const fs::path manifest = fs::absolute(fs::path(manifestPath)).lexically_normal();
	std::error_code ec;
	if (!fs::exists(manifest, ec) || !fs::is_regular_file(manifest, ec))
	{
		SetError("Package manifest does not exist: " + manifest.string());
		return false;
	}

	const Vans::VansPackageManifestLoadResult manifestLoad = Vans::VansPackageManifestIO::Load(manifest);
	if (!manifestLoad)
	{
		SetError(manifestLoad.error);
		return false;
	}
	const Vans::VansPackageManifest& packageManifest = manifestLoad.manifest;
	const std::string& scene = packageManifest.scene;
	const fs::path contentRoot = manifest.parent_path();
	const fs::path shaderArtifactRoot = (contentRoot / packageManifest.shaderArtifacts).lexically_normal();
	if (!fs::exists(shaderArtifactRoot / "cooked-shader-manifest.json", ec) ||
		!fs::is_regular_file(shaderArtifactRoot / "cooked-shader-manifest.json", ec))
	{
		SetError("Packaged shader artifact index does not exist: " +
				 (shaderArtifactRoot / "cooked-shader-manifest.json").string());
		return false;
	}
	const fs::path projectConfigPath = contentRoot / "ForestProject.json";
	if (!fs::exists(projectConfigPath, ec) || !fs::is_regular_file(projectConfigPath, ec))
	{
		SetError("ForestProject.json does not exist in package content: " + projectConfigPath.string());
		return false;
	}

	const fs::path scenePath = (contentRoot / scene).lexically_normal();
	if (!fs::exists(scenePath, ec) || !fs::is_regular_file(scenePath, ec))
	{
		SetError("Packaged scene does not exist: " + scenePath.string());
		return false;
	}

	const std::string& resourcePlan = packageManifest.resourcePlan;
	fs::path resourcePlanPath;
	if (!resourcePlan.empty())
	{
		resourcePlanPath = (contentRoot / resourcePlan).lexically_normal();
		if (!fs::exists(resourcePlanPath, ec) || !fs::is_regular_file(resourcePlanPath, ec))
		{
			SetError("Packaged resource plan does not exist: " + resourcePlanPath.string());
			return false;
		}
	}

	if (!ClaimActiveRuntime(this))
	{
		SetError("ForestRuntime supports one active runtime handle per process");
		return false;
	}
	VansRuntimePhaseGuard packageGuard(
		[this]
		{
			VansShaderArtifactCache::ResetRuntimeConfiguration();
			ReleaseActiveRuntime(this);
		});
	Vans::VansShaderArtifactCache::ConfigureCookedRuntime(shaderArtifactRoot);

	Vans::VansProjectManager& projectManager = Vans::VansProjectManager::Get();
	std::string engineRootError;
	if (!projectManager.ConfigureEngineRoot(contentRoot, engineRootError))
	{
		SetError(engineRootError);
		return false;
	}
	VansProjectOpenRequest openRequest;
	openRequest.m_ProjectRootPath = contentRoot.string();
	openRequest.m_Options.m_UpdateLastOpenedAt = false;
	openRequest.m_Options.m_UpdateRecentProjects = false;
	openRequest.m_Options.m_LoadProjectSettings = true;
	openRequest.m_Options.m_ScanAssets = resourcePlan.empty();
	openRequest.m_Options.m_AssetPolicy = VansAssetOperationPolicy::ReadOnly();
	if (!projectManager.OpenProject(openRequest).m_Opened)
	{
		SetError("Cannot open packaged project: " + contentRoot.string());
		return false;
	}
	projectManager.GetSceneManager().SetCurrentScene(scene);

	m_State = VansRuntimeLifecycleState::ProjectReady;
	m_ContentRoot = contentRoot;
	m_LoadedScene = scene;
	m_ResourcePlan = resourcePlan;
	m_ProjectRoot = projectManager.GetProjectRootPath();
	m_LastError.clear();
	packageGuard.Commit();
	return true;
}

bool VansRuntimeHost::OpenWindow(int width, int height, const char* title)
{
	if (!IsProjectReady())
	{
		SetError("Runtime package is not loaded");
		return false;
	}
	if (IsGraphicsReady())
		return true;

	if (!InitializeCore())
		return false;
	VansRuntimePhaseGuard graphicsGuard([this] { ShutdownGraphics(); });

	m_Window = std::make_unique<Vans::VansRuntimeWindow>();
	std::string windowError;
	if (!m_Window->Create(width > 0 ? width : 1280, height > 0 ? height : 720, title, windowError))
	{
		SetError(windowError);
		return false;
	}
	Vans::VansInputManager::Get().Initialize(m_Window->GetGLFWWindow());
	Vans::VansInputManager::Get().SetCursorContext(Vans::VansCursorContext::Standalone);
	Vans::VansInputManager::Get().SetCursorMode(Vans::VansCursorMode::Captured);

	auto device =
		std::make_unique<VansGraphics::VansVKDevice>(VkExtent2D{static_cast<std::uint32_t>(width > 0 ? width : 1280),
																static_cast<std::uint32_t>(height > 0 ? height : 720)},
													 m_Window.get());
	if (!device->IsInitialized())
	{
		SetError("Vulkan device initialization failed");
		return false;
	}
	const VansGraphics::VansUpscalerSelectionChange upscalerSelection =
		device->ApplyRenderRuntimeConfig(
			Vans::VansProjectManager::Get().GetProjectSettings().GetRenderRuntimeConfig(),
			static_cast<std::uint32_t>(width > 0 ? width : 1280),
			static_cast<std::uint32_t>(height > 0 ? height : 720));
	if (!upscalerSelection.accepted)
	{
		SetError(upscalerSelection.error.empty()
			? "Runtime render settings application failed"
			: upscalerSelection.error);
		return false;
	}

	m_Device = std::move(device);
	m_Device->SetRuntimeSwapchainPresentationEnabled(true);
	::m_GraphicsDevice = m_Device.get();
	m_Scene = std::make_unique<VansGraphics::VansScene>();
	::m_Scene = m_Scene.get();
	m_RenderSystem = std::make_unique<VansGraphics::VansRenderSystem>(*m_Device, *m_Scene, true);

	::RegisterEngineShaders();
	if (!VansGraphics::VansSceneProjectResourceBuilder::LoadShadersFromRegistry(
			*m_Scene, Vans::VansProjectManager::Get().GetPathResolver().GetEngineRoot(),
			m_Device->GetLogicDevice()))
	{
		SetError("One or more required engine shaders failed to load");
		return false;
	}

	if (!m_RenderSystem->InitializeFrameExecution())
	{
		SetError("Render-system frame execution initialization failed");
		return false;
	}
	m_FrameExecutionStarted = true;
	m_Scene->BindRenderThreadTransactionExecutor(m_RenderSystem.get());
	VansRuntime::VansUIInitDesc uiDesc{};
	uiDesc.m_Width = static_cast<std::uint32_t>(width > 0 ? width : 1280);
	uiDesc.m_Height = static_cast<std::uint32_t>(height > 0 ? height : 720);
	if (!VansRuntime::VansUISystem::Get().InitializeWithDevice(uiDesc, m_Device.get()))
	{
		SetError("Runtime UI frontend initialization failed");
		return false;
	}
	m_State = VansRuntimeLifecycleState::GraphicsReady;
	m_LastError.clear();
	graphicsGuard.Commit();
	return true;
}

bool VansRuntimeHost::LoadCurrentScene()
{
	if (!IsGraphicsReady() || !m_Scene || !m_Device)
	{
		SetError("Runtime window/graphics are not initialized");
		return false;
	}
	if (m_LoadedScene.empty())
	{
		SetError("No packaged scene is loaded");
		return false;
	}
	if (!m_RenderSystem || !m_RenderSystem->WaitForIdle())
	{
		SetError("Render thread failed to drain before loading the runtime scene");
		return false;
	}

	Vans::VansProjectManager& projectManager = Vans::VansProjectManager::Get();
	const fs::path scenePath = (m_ContentRoot / m_LoadedScene).lexically_normal();
	// Prefab 解析依赖包内配置仓库，必须先发布索引中的内存资产。
	Vans::VansPackagedResourcePlan packagePlan;
	if (!m_ResourcePlan.empty())
	{
		const fs::path resourcePlanPath = (m_ContentRoot / m_ResourcePlan).lexically_normal();
		std::string planError;
		if (!Vans::VansPackagedResourcePlanIO::Load(resourcePlanPath, m_ContentRoot, packagePlan, planError))
		{
			SetError("Cannot load packaged resource plan: " + planError);
			return false;
		}
		std::vector<Vans::VansAssetRecord> packagedAssetRecords = BuildRuntimeAssetRecords(packagePlan.assetIndex);
		projectManager.SetPackagedAssetRecords(packagedAssetRecords);
		const Vans::VansAssetObjectBootstrapResult bootstrap =
			Vans::VansAssetObjectBootstrapper::Publish(packagedAssetRecords,
				projectManager.GetAssetObjectRepository(), {},
				projectManager.GetProjectSettings().GetNavigationSettings());
		if (!bootstrap)
		{
			SetError("Packaged configuration asset bootstrap failed: " + bootstrap.errors.front());
			return false;
		}
	}
	Vans::SceneDocumentLoadResult sceneDocumentLoad = Vans::VansSceneDocumentLoader::Load(
		scenePath, Vans::VansPrefabResolver::FromRepository(projectManager.GetAssetObjectRepository()));
	if (!sceneDocumentLoad)
	{
		std::string message = "Cannot load packaged scene document: " + scenePath.string();
		if (!sceneDocumentLoad.diagnostics.empty() && !sceneDocumentLoad.diagnostics.front().message.empty())
			message += " (" + sceneDocumentLoad.diagnostics.front().message + ")";
		SetError(message);
		return false;
	}
	const Vans::VansSerializedValue sceneDocument = sceneDocumentLoad.document->SerializedRootSnapshot();
	VansRuntimePhaseGuard sceneGuard([this] { RollbackSceneSession(); });
	if (!m_ResourcePlan.empty())
	{
		if (!m_Scene->LoadPackagedProjectAssets(packagePlan, m_Device.get()))
		{
			SetError("Packaged project asset loading failed for scene: " + scenePath.string());
			return false;
		}
	}
	else
	{
		Vans::VansAssetDatabase* database = projectManager.GetAssetDatabase();
		if (!database)
		{
			SetError("Packaged project has no asset database");
			return false;
		}
		if (!m_Scene->LoadProjectAssets(*database, sceneDocument, scenePath, m_Device.get()))
		{
			SetError("Project asset loading failed for scene: " + scenePath.string());
			return false;
		}
	}

	std::string uiThemeError;
	if (!VansRuntime::VansUISystem::Get().ApplyGlobalThemeFromMemory(uiThemeError))
	{
		SetError("Runtime UI global theme application failed: " + uiThemeError);
		return false;
	}

	m_Camera = std::make_unique<VansGraphics::VansCamera>(m_Device.get());
	m_Scene->InjectCamera(m_Camera.get());

	m_ScriptContext = std::make_unique<VansScriptContext>();
	m_ScriptContext->VansScriptSetup();
	VANS_LOG("[ForestRuntime] Runtime script context ready");

	const VansGraphics::VansSceneLoadResult sceneLoad = m_Scene->LoadSceneForRendering(
		sceneDocument, scenePath, m_Device.get(), VansGraphics::VansSceneLoadMode::Runtime);
	if (!sceneLoad.m_Loaded || !m_Scene->IsSceneReady())
	{
		std::string message = "Scene failed to become ready: " + scenePath.string();
		if (!sceneLoad.m_Error.empty())
			message += " (" + sceneLoad.m_Error + ")";
		SetError(std::move(message));
		return false;
	}

	m_ScriptContext->SetScene(m_Scene.get());
	const VansEngine::VansPhysicsTiming& physicsTiming =
		projectManager.GetProjectSettings().GetPhysicsTiming();
	if (!VansEngine::VansPhysicsSystem::GetInstance().SetTiming(physicsTiming))
	{
		SetError("Project physics timing application failed");
		return false;
	}
	VansGraphics::VansTimer::SetPhysicsDeltaTime(
		static_cast<double>(physicsTiming.fixedTimeStep));
	VansEngine::VansPhysicsSystem::GetInstance().SetPreSimulateCallback(
		[scene = m_Scene.get()](float deltaTimeSeconds)
		{
			if (scene && scene->GetVehicle())
				scene->GetVehicle()->Step(deltaTimeSeconds);
		});
	m_Services.StartSimulation();
	VANS_LOG("[ForestRuntime] Runtime play simulation started");
	VansGraphics::VansTimer::Reset();
	VansGraphics::VansTimer::SetTimePaused(false);
	m_State = VansRuntimeLifecycleState::SceneReady;
	m_LastError.clear();
	sceneGuard.Commit();
	return true;
}

bool VansRuntimeHost::Tick(float)
{
	if (!IsProjectReady())
	{
		SetError("Runtime package is not loaded");
		return false;
	}

	Vans::VansJobSystem::Get().ProcessMainThreadJobs();
	Vans::VansEventBus::Get().Flush(Vans::VansEventLane::MainThread);
	VansGraphics::VansTimer::Update();
	if (IsGraphicsReady() && m_Window)
	{
		Vans::VansInputManager::Get().Update();
		m_Window->PollEvents();
		Vans::VansInputManager::Get().RefreshPolledState();
		Vans::VansEventBus::Get().Flush(Vans::VansEventLane::Input);
	}
	if (IsSceneReady() && m_Scene && m_Scene->IsSceneReady())
	{
		VansRuntimeSceneFramePort framePort(*m_Scene, m_ScriptContext.get());
		const VansRuntimeFramePolicy framePolicy{ true, m_Services.IsSimulationRunning(), true, true };
		const VansRuntimeFrameContext frameContext{ VansGraphics::VansTimer::GetDeltaTime() };
		VansRuntimeFrameScheduler::RunGameplay(framePort, nullptr, framePolicy, frameContext);
		VansRuntime::VansUISystem::Get().Update(static_cast<float>(VansGraphics::VansTimer::GetDeltaTime()));
	}
	return true;
}

bool VansRuntimeHost::RenderFrame()
{
	if (!IsGraphicsReady() || !IsSceneReady() || !m_Window || !m_Device || !m_RenderSystem || !m_Camera)
	{
		SetError("Runtime scene is not ready to render");
		return false;
	}

	if (m_Window->ConsumeFramebufferResize())
	{
		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(m_Window->GetGLFWWindow(), &width, &height);
		if (width > 0 && height > 0)
			m_RenderSystem->RequestSurfaceResize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
	}

	Vans::VansEventBus::Get().Flush(Vans::VansEventLane::RenderPrep);
	m_RenderSystem->BeginFrame(*m_Camera);
	const VansGraphics::VansRenderFrameSubmitResult submitResult = m_RenderSystem->SubmitFrame();
	if (!submitResult)
	{
		SetError("Render-system frame submission failed");
		return false;
	}
	m_LastError.clear();
	return true;
}

void VansRuntimeHost::Shutdown()
{
	if (!IsProjectReady() && !m_Services.HasActiveResources())
	{
		Vans::VansShaderArtifactCache::ResetRuntimeConfiguration();
		ReleaseActiveRuntime(this);
		return;
	}

	if (m_Services.IsStarted())
	{
		m_Services.PauseSimulation();
		m_Services.StopSimulation();
	}

	ShutdownGraphics();
	m_LoadedScene.clear();
	m_ResourcePlan.clear();
	m_ProjectRoot.clear();
	m_LastError.clear();
	Vans::VansProjectManager::Get().CloseProject();
	m_State = VansRuntimeLifecycleState::Stopped;
	m_Services.ShutdownServices();
	m_Services.ShutdownJobs();
	Vans::VansShaderArtifactCache::ResetRuntimeConfiguration();
	ReleaseActiveRuntime(this);
}

bool VansRuntimeHost::ShouldClose() const
{
	return !m_Window || m_Window->ShouldClose();
}
} // namespace Vans
