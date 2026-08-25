#include "ForestRuntimeCAPI.h"

#include "../EngineCore/AudioCore/VansAudioSystem.h"
#include "../EngineCore/AssetCore/VansAssetDatabase.h"
#include "../EngineCore/AssetCore/Importers/Shader/VansShaderArtifactCache.h"
#include "../EngineCore/Configration/VansConfigration.h"
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
#include "../EngineCore/RuntimeCore/VansRuntimeWindow.h"
#include "../EngineCore/RuntimeCore/VansPackageManifest.h"
#include "../EngineCore/RuntimeCore/VansRuntimeFrameScheduler.h"
#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include "../EngineCore/SceneCore/VansPackagedResourcePlan.h"
#include "../EngineCore/ScriptCore/VansScriptContext.h"
#include "../EngineCore/Util/VansInputManager.h"
#include "../EngineCore/RuntimeUI/Public/VansUISystem.h"
#include "../EngineCore/Util/VansJobSystem.h"
#include "../EngineCore/Util/VansLog.h"
#include "../EngineCore/VansTimer.h"

#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct ForestRuntimeHandle
{
	bool packageLoaded = false;
	bool projectLoaded = false;
	fs::path manifestPath;
	fs::path contentRoot;
	std::string loadedScene;
	std::string resourcePlan;
	std::string startupMode = "play";
	std::string projectRoot;
	std::string lastError;
	std::unique_ptr<Vans::VansRuntimeWindow> window;
	std::unique_ptr<VansGraphics::VansVKDevice> device;
	std::unique_ptr<VansGraphics::VansRenderSystem> renderSystem;
	std::unique_ptr<VansGraphics::VansScene> scene;
	std::unique_ptr<VansGraphics::VansCamera> camera;
	std::unique_ptr<VansScriptContext> scriptContext;
	bool coreInitialized = false;
	bool graphicsInitialized = false;
	bool renderingPrepared = false;
	bool sceneLoaded = false;
};

namespace
{
	std::mutex g_ActiveRuntimeMutex;
	ForestRuntimeHandle* g_ActiveRuntime = nullptr;

	bool ClaimActiveRuntime(ForestRuntimeHandle* runtime)
	{
		std::lock_guard<std::mutex> lock(g_ActiveRuntimeMutex);
		if (g_ActiveRuntime != nullptr && g_ActiveRuntime != runtime)
			return false;
		g_ActiveRuntime = runtime;
		return true;
	}

	void ReleaseActiveRuntime(ForestRuntimeHandle* runtime)
	{
		std::lock_guard<std::mutex> lock(g_ActiveRuntimeMutex);
		if (g_ActiveRuntime == runtime)
			g_ActiveRuntime = nullptr;
	}

	Vans::VansAssetArtifactFormat ArtifactFormatFromString(const std::string& value)
	{
		if (value == "imported") return Vans::VansAssetArtifactFormat::Imported;
		if (value == "source") return Vans::VansAssetArtifactFormat::Source;
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
			record.state = indexRecord.missing
				? Vans::VansAssetState::Missing
				: Vans::VansAssetState::Discovered;
			record.sourcePath = indexRecord.sourcePath;
			record.authoringPath = indexRecord.authoringPath;
			record.artifactPath = indexRecord.artifactPath;
			record.artifactFormat = ArtifactFormatFromString(indexRecord.artifactFormat);
			record.sourceHash = indexRecord.sourceHash;
			record.metaHash = indexRecord.metaHash;
			records.push_back(std::move(record));
		}
		return records;
	}

	void SetError(ForestRuntimeHandle* runtime, std::string message)
	{
		if (runtime)
			runtime->lastError = std::move(message);
	}

	bool InitializeRuntimeCore(ForestRuntimeHandle* runtime)
	{
		if (runtime->coreInitialized)
			return true;

		VANS_INIT_MAIN_THREAD();

		Vans::VansJobSystem::Get().Initialize();

		if (!VansEngine::VansPhysicsSystem::GetInstance().Initialize())
		{
			SetError(runtime, "Physics system initialization failed");
			return false;
		}

		if (!VansEngine::VansAudioSystem::GetInstance().Initialize())
			VANS_LOG_WARN("[ForestRuntime] Audio system initialization failed; continuing without audio");

		runtime->coreInitialized = true;
		return true;
	}

	void ShutdownGraphics(ForestRuntimeHandle* runtime)
	{
		if (!runtime)
		return;

	VansEngine::VansPhysicsSystem::GetInstance().SetPreSimulateCallback(nullptr);

	if (runtime->renderSystem)
	{
		runtime->renderSystem->WaitForIdle();
	}
	else if (runtime->device)
	{
		runtime->device->WaitForDevice();
	}

		if (runtime->scene)
		{
			if (runtime->scene->IsSceneReady() || runtime->scene->IsSceneSwitching())
				runtime->scene->UnLoadScene();
			runtime->scene->UnloadProjectResources(runtime->device.get());
			runtime->scene->BindRenderThreadTransactionExecutor(nullptr);
		}

		if (runtime->device)
			runtime->device->GetPipelineCacheService().Flush(VansGraphics::VansPipelineCacheFlushReason::Manual);

		if (runtime->scriptContext)
		{
			runtime->scriptContext->SetScene(nullptr);
			runtime->scriptContext->ShutdownLua();
			runtime->scriptContext.reset();
		}
		runtime->camera.reset();
		if (runtime->renderSystem && runtime->renderingPrepared)
		{
			runtime->renderSystem->ShutdownFrameExecution();
			runtime->renderingPrepared = false;
		}
		VansRuntime::VansUISystem::Get().Shutdown();
		if (runtime->device)
			runtime->device->GetPipelineCacheService().Flush(VansGraphics::VansPipelineCacheFlushReason::Shutdown);
		Vans::VansInputManager::Get().Shutdown();
		VansGraphics::VansShaderManager::Get().Clear();
		runtime->scene.reset();
		m_Scene = nullptr;
		runtime->renderSystem.reset();
		runtime->device.reset();
		m_GraphicsDevice = nullptr;
		runtime->window.reset();
		runtime->graphicsInitialized = false;
		runtime->sceneLoaded = false;
	}

	void ShutdownRuntimeCore(ForestRuntimeHandle* runtime)
	{
		if (!runtime || !runtime->coreInitialized)
			return;

		VansEngine::VansPhysicsSystem::GetInstance().PauseSimulation();
		VansEngine::VansPhysicsSystem::GetInstance().StopSimulation();
		VansEngine::VansPhysicsSystem::GetInstance().Shutdown();
		VansEngine::VansAudioSystem::GetInstance().Shutdown();
		Vans::VansJobSystem::Get().Shutdown();
		runtime->coreInitialized = false;
	}
}

FOREST_RUNTIME_API int ForestRuntime_GetAbiVersion()
{
	return 1;
}

FOREST_RUNTIME_API ForestRuntimeHandle* ForestRuntime_Create()
{
	return new ForestRuntimeHandle();
}

FOREST_RUNTIME_API void ForestRuntime_Destroy(ForestRuntimeHandle* runtime)
{
	ForestRuntime_Shutdown(runtime);
	delete runtime;
}

FOREST_RUNTIME_API int ForestRuntime_LoadPackage(ForestRuntimeHandle* runtime, const char* manifestPath)
{
	if (!runtime)
		return 0;
	if (runtime->packageLoaded)
	{
		SetError(runtime, "A package is already loaded by this runtime handle");
		return 0;
	}
	if (!manifestPath || manifestPath[0] == '\0')
	{
		SetError(runtime, "Package manifest path is empty");
		return 0;
	}

	const fs::path manifest = fs::absolute(fs::path(manifestPath)).lexically_normal();
	std::error_code ec;
	if (!fs::exists(manifest, ec) || !fs::is_regular_file(manifest, ec))
	{
		SetError(runtime, "Package manifest does not exist: " + manifest.string());
		return 0;
	}

	const Vans::VansPackageManifestLoadResult manifestLoad =
		Vans::VansPackageManifestIO::Load(manifest);
	if (!manifestLoad)
	{
		SetError(runtime, manifestLoad.error);
		return 0;
	}
	const Vans::VansPackageManifest& packageManifest = manifestLoad.manifest;
	const std::string& scene = packageManifest.scene;
	const std::string& startupMode = packageManifest.startupMode;

	const fs::path contentRoot = manifest.parent_path();
	const fs::path shaderArtifactRoot =
		(contentRoot / packageManifest.shaderArtifacts).lexically_normal();
	if (!fs::exists(shaderArtifactRoot / "cooked-shader-manifest.json", ec)
		|| !fs::is_regular_file(shaderArtifactRoot / "cooked-shader-manifest.json", ec))
	{
		SetError(runtime, "Packaged shader artifact index does not exist: "
			+ (shaderArtifactRoot / "cooked-shader-manifest.json").string());
		return 0;
	}
	const fs::path projectConfigPath = contentRoot / "ForestProject.json";
	if (!fs::exists(projectConfigPath, ec) || !fs::is_regular_file(projectConfigPath, ec))
	{
		SetError(runtime, "ForestProject.json does not exist in package content: " + projectConfigPath.string());
		return 0;
	}

	const fs::path scenePath = (contentRoot / scene).lexically_normal();
	if (!fs::exists(scenePath, ec) || !fs::is_regular_file(scenePath, ec))
	{
		SetError(runtime, "Packaged scene does not exist: " + scenePath.string());
		return 0;
	}

	const std::string& resourcePlan = packageManifest.resourcePlan;
	fs::path resourcePlanPath;
	if (!resourcePlan.empty())
	{
		resourcePlanPath = (contentRoot / resourcePlan).lexically_normal();
		if (!fs::exists(resourcePlanPath, ec) || !fs::is_regular_file(resourcePlanPath, ec))
		{
			SetError(runtime, "Packaged resource plan does not exist: " + resourcePlanPath.string());
			return 0;
		}
	}

	if (!ClaimActiveRuntime(runtime))
	{
		SetError(runtime, "ForestRuntime supports one active runtime handle per process");
		return 0;
	}
	Vans::VansShaderArtifactCache::ConfigureCookedRuntime(shaderArtifactRoot);

	if (VansConfigration* configuration = VansConfigration::GetInstance())
		configuration->SetProjectRootPath(contentRoot.string());

	Vans::VansProjectManager& projectManager = Vans::VansProjectManager::Get();
	Vans::VansProjectOpenOptions openOptions;
	openOptions.updateLastOpenedAt = false;
	openOptions.updateRecentProjects = false;
	openOptions.loadProjectSettings = true;
	openOptions.scanAssets = resourcePlan.empty();
	openOptions.assetPolicy = Vans::VansAssetOperationPolicy::ReadOnly();
	if (!projectManager.OpenProject(contentRoot.string(), openOptions))
	{
		SetError(runtime, "Cannot open packaged project: " + contentRoot.string());
		Vans::VansShaderArtifactCache::ResetRuntimeConfiguration();
		ReleaseActiveRuntime(runtime);
		return 0;
	}
	projectManager.GetSceneManager().SetCurrentScene(scene);

	runtime->packageLoaded = true;
	runtime->projectLoaded = true;
	runtime->manifestPath = manifest;
	runtime->contentRoot = contentRoot;
	runtime->loadedScene = scene;
	runtime->resourcePlan = resourcePlan;
	runtime->startupMode = startupMode;
	runtime->projectRoot = projectManager.GetProjectRootPath();
	runtime->lastError.clear();
	return 1;
}

FOREST_RUNTIME_API int ForestRuntime_CreateWindow(ForestRuntimeHandle* runtime, int width, int height, const char* title)
{
	if (!runtime)
		return 0;
	if (!runtime->packageLoaded)
	{
		SetError(runtime, "Runtime package is not loaded");
		return 0;
	}
	if (runtime->graphicsInitialized)
		return 1;

	if (!InitializeRuntimeCore(runtime))
		return 0;

	runtime->window = std::make_unique<Vans::VansRuntimeWindow>();
	std::string windowError;
	if (!runtime->window->Create(width > 0 ? width : 1280, height > 0 ? height : 720, title, windowError))
	{
		SetError(runtime, windowError);
		ShutdownGraphics(runtime);
		return 0;
	}
	Vans::VansInputManager::Get().Initialize(runtime->window->GetGLFWWindow());
	Vans::VansInputManager::Get().SetCursorCaptureAllowed(true);
	Vans::VansInputManager::Get().SetCursorCaptureEnabled(true);

	auto device = std::make_unique<VansGraphics::VansVKDevice>(
		VkExtent2D{
			static_cast<std::uint32_t>(width > 0 ? width : 1280),
			static_cast<std::uint32_t>(height > 0 ? height : 720)
		},
		runtime->window.get());
	if (!device->IsInitialized())
	{
		SetError(runtime, "Vulkan device initialization failed");
		ShutdownGraphics(runtime);
		return 0;
	}
	device->ApplyRenderRuntimeConfig(
		Vans::VansProjectManager::Get().GetProjectSettings().GetRenderRuntimeConfig(),
		static_cast<std::uint32_t>(width > 0 ? width : 1280),
		static_cast<std::uint32_t>(height > 0 ? height : 720));

	runtime->device = std::move(device);
	runtime->device->SetRuntimeSwapchainPresentationEnabled(true);
	m_GraphicsDevice = runtime->device.get();
	runtime->scene = std::make_unique<VansGraphics::VansScene>();
	m_Scene = runtime->scene.get();
	runtime->renderSystem = std::make_unique<VansGraphics::VansRenderSystem>(
		*runtime->device,
		*runtime->scene,
		true);

	RegisterEngineShaders();
	VansGraphics::VansSceneProjectResourceBuilder::LoadShadersFromRegistry(
		*runtime->scene,
		VansConfigration::GetInstance()->GetProjectRootPath(),
		runtime->device->GetLogicDevice());

	if (!runtime->renderSystem->InitializeFrameExecution())
	{
		SetError(runtime, "Render-system frame execution initialization failed");
		ShutdownGraphics(runtime);
		return 0;
	}
	runtime->scene->BindRenderThreadTransactionExecutor(runtime->renderSystem.get());
	VansRuntime::VansUIInitDesc uiDesc{};
	uiDesc.m_Width = static_cast<std::uint32_t>(width > 0 ? width : 1280);
	uiDesc.m_Height = static_cast<std::uint32_t>(height > 0 ? height : 720);
	if (!VansRuntime::VansUISystem::Get().InitializeWithDevice(uiDesc, runtime->device.get()))
	{
		SetError(runtime, "Runtime UI frontend initialization failed");
		ShutdownGraphics(runtime);
		return 0;
	}
	runtime->renderingPrepared = true;
	runtime->graphicsInitialized = true;
	runtime->lastError.clear();
	return 1;
}

FOREST_RUNTIME_API int ForestRuntime_LoadCurrentScene(ForestRuntimeHandle* runtime)
{
	if (!runtime)
		return 0;
	if (!runtime->graphicsInitialized || !runtime->scene || !runtime->device)
	{
		SetError(runtime, "Runtime window/graphics are not initialized");
		return 0;
	}
	if (runtime->loadedScene.empty())
	{
		SetError(runtime, "No packaged scene is loaded");
		return 0;
	}
	if (!runtime->renderSystem || !runtime->renderSystem->WaitForIdle())
	{
		SetError(runtime, "Render thread failed to drain before loading the runtime scene");
		return 0;
	}

	Vans::VansProjectManager& projectManager = Vans::VansProjectManager::Get();
	const fs::path scenePath = (runtime->contentRoot / runtime->loadedScene).lexically_normal();
	if (!runtime->resourcePlan.empty())
	{
		const fs::path resourcePlanPath = (runtime->contentRoot / runtime->resourcePlan).lexically_normal();
		Vans::VansPackagedResourcePlan packagePlan;
		std::string planError;
		if (!Vans::VansPackagedResourcePlanIO::Load(resourcePlanPath, runtime->contentRoot, packagePlan, planError))
		{
			SetError(runtime, "Cannot load packaged resource plan: " + planError);
			return 0;
		}
		projectManager.SetPackagedAssetRecords(BuildRuntimeAssetRecords(packagePlan.assetIndex));
		if (!runtime->scene->LoadPackagedProjectAssets(packagePlan, runtime->device.get()))
		{
			SetError(runtime, "Packaged project asset loading failed for scene: " + scenePath.string());
			return 0;
		}
	}
	else
	{
		Vans::VansAssetDatabase* database = projectManager.GetAssetDatabase();
		if (!database)
		{
			SetError(runtime, "Packaged project has no asset database");
			return 0;
		}
		if (!runtime->scene->LoadProjectAssets(*database, scenePath, runtime->device.get()))
		{
			SetError(runtime, "Project asset loading failed for scene: " + scenePath.string());
			return 0;
		}
	}

	runtime->camera = std::make_unique<VansGraphics::VansCamera>(runtime->device.get());
	runtime->scene->InjectCamera(runtime->camera.get());

	runtime->scriptContext = std::make_unique<VansScriptContext>();
	runtime->scriptContext->VansScriptSetup();
	VANS_LOG("[ForestRuntime] Runtime script context ready");

	if (!runtime->scene->LoadSceneForRendering(
			scenePath.string().c_str(),
			runtime->device.get(),
			VansGraphics::VansSceneLoadMode::Runtime) ||
		!runtime->scene->IsSceneReady())
	{
		SetError(runtime, "Scene failed to become ready: " + scenePath.string());
		return 0;
	}

	runtime->scriptContext->SetScene(runtime->scene.get());
	VansEngine::VansPhysicsSystem::GetInstance().SetPreSimulateCallback([scene = runtime->scene.get()](float deltaTimeSeconds)
	{
		if (scene && scene->GetVehicle())
			scene->GetVehicle()->Step(deltaTimeSeconds);
	});
	VansEngine::VansPhysicsSystem::GetInstance().StartSimulation();
	VANS_LOG("[ForestRuntime] Runtime play simulation started");
	VansGraphics::VansTimer::Reset();
	VansGraphics::VansTimer::SetTimePaused(false);
	runtime->sceneLoaded = true;
	runtime->lastError.clear();
	return 1;
}

FOREST_RUNTIME_API int ForestRuntime_Tick(ForestRuntimeHandle* runtime, float)
{
	if (!runtime)
		return 0;
	if (!runtime->packageLoaded)
	{
		SetError(runtime, "Runtime package is not loaded");
		return 0;
	}

	Vans::VansJobSystem::Get().ProcessMainThreadJobs();
	Vans::VansEventBus::Get().Flush(Vans::VansEventLane::MainThread);
	VansGraphics::VansTimer::Update();
	if (runtime->graphicsInitialized && runtime->window)
	{
		Vans::VansInputManager::Get().Update();
		runtime->window->PollEvents();
		Vans::VansInputManager::Get().RefreshPolledState();
		Vans::VansEventBus::Get().Flush(Vans::VansEventLane::Input);
	}
	if (runtime->sceneLoaded && runtime->scene && runtime->scene->IsSceneReady())
	{
		Vans::VansRuntimeGameplayFrame frame;
		frame.sceneReady = true;
		frame.simulationRunning = VansEngine::VansPhysicsSystem::GetInstance().IsSimulationRunning();
		frame.gameplayActive = true;
		frame.cameraControlActive = true;
		frame.deltaSeconds = VansGraphics::VansTimer::GetDeltaTime();
		frame.syncPhysicsTransforms = [&] { runtime->scene->UpdatePhysicsTransforms(); };
		frame.updateNonCameraScripts = [&]
		{
			if (runtime->scriptContext)
			{
				runtime->scriptContext->SetScene(runtime->scene.get());
				runtime->scriptContext->VansScriptUpdateNonCameraScripts();
			}
		};
		frame.updateActionsEarly = [&](double deltaSeconds)
		{
			runtime->scene->UpdateActionsEarly(deltaSeconds);
		};
		frame.prepareCharacterLocomotion = [&](double deltaSeconds)
		{
			runtime->scene->PrepareCharacterLocomotion(static_cast<float>(deltaSeconds));
		};
		frame.flushCharacterControllerTransforms = [&] { runtime->scene->UpdateCharControllerTransforms(); };
		frame.updateTimelinesPostScript = [&](double deltaSeconds) { runtime->scene->UpdateTimelinesPostScript(deltaSeconds); };
		frame.runActionLateContinuation = [&] { runtime->scene->RunActionLateContinuation(); };
		frame.beginCameraControlFrame = [&] { runtime->scene->BeginCameraControlFrame(); };
		frame.updateCameraScripts = [&]
		{
			if (runtime->scriptContext)
				runtime->scriptContext->VansScriptUpdateCameraScripts();
		};
		frame.captureCameraControlBase = [&] { runtime->scene->CaptureCameraControlBase(); };
		frame.updateTimelinesCamera = [&](double deltaSeconds) { runtime->scene->UpdateTimelinesCamera(deltaSeconds); };
		frame.resolveCameraControlFrame = [&] { runtime->scene->ResolveCameraControlFrame(); };
		Vans::VansRuntimeFrameScheduler::RunGameplay(frame);
		VansRuntime::VansUISystem::Get().Update(
			static_cast<float>(VansGraphics::VansTimer::GetDeltaTime()));
	}
	return 1;
}

FOREST_RUNTIME_API int ForestRuntime_RenderFrame(ForestRuntimeHandle* runtime)
{
	if (!runtime)
		return 0;
	if (!runtime->graphicsInitialized || !runtime->sceneLoaded || !runtime->window ||
		!runtime->device || !runtime->renderSystem || !runtime->camera)
	{
		SetError(runtime, "Runtime scene is not ready to render");
		return 0;
	}

	if (runtime->window->ConsumeFramebufferResize())
	{
		int width = 0;
		int height = 0;
		glfwGetFramebufferSize(runtime->window->GetGLFWWindow(), &width, &height);
		if (width > 0 && height > 0)
			runtime->renderSystem->RequestSurfaceResize(
				static_cast<std::uint32_t>(width),
				static_cast<std::uint32_t>(height));
	}

	Vans::VansEventBus::Get().Flush(Vans::VansEventLane::RenderPrep);
	runtime->renderSystem->BeginFrame(*runtime->camera);
	const VansGraphics::VansRenderFrameSubmitResult submitResult =
		runtime->renderSystem->SubmitFrame();
	if (!submitResult)
	{
		SetError(runtime, "Render-system frame submission failed");
		return 0;
	}
	runtime->lastError.clear();
	return 1;
}

FOREST_RUNTIME_API int ForestRuntime_ShouldClose(ForestRuntimeHandle* runtime)
{
	if (!runtime || !runtime->window)
		return 1;
	return runtime->window->ShouldClose() ? 1 : 0;
}

FOREST_RUNTIME_API void ForestRuntime_Shutdown(ForestRuntimeHandle* runtime)
{
	if (!runtime)
		return;
	if (!runtime->packageLoaded && !runtime->projectLoaded && !runtime->coreInitialized &&
		!runtime->graphicsInitialized && !runtime->sceneLoaded)
	{
		Vans::VansShaderArtifactCache::ResetRuntimeConfiguration();
		ReleaseActiveRuntime(runtime);
		return;
	}

	if (runtime->coreInitialized)
	{
		VansEngine::VansPhysicsSystem::GetInstance().PauseSimulation();
		VansEngine::VansPhysicsSystem::GetInstance().StopSimulation();
	}

	ShutdownGraphics(runtime);
	runtime->packageLoaded = false;
	runtime->projectLoaded = false;
	runtime->loadedScene.clear();
	runtime->resourcePlan.clear();
	runtime->projectRoot.clear();
	runtime->lastError.clear();
	Vans::VansProjectManager::Get().CloseProject();
	ShutdownRuntimeCore(runtime);
	Vans::VansShaderArtifactCache::ResetRuntimeConfiguration();
	ReleaseActiveRuntime(runtime);
}

FOREST_RUNTIME_API const char* ForestRuntime_GetLastError(ForestRuntimeHandle* runtime)
{
	if (!runtime)
		return "Runtime handle is null";
	return runtime->lastError.c_str();
}

FOREST_RUNTIME_API const char* ForestRuntime_GetLoadedScene(ForestRuntimeHandle* runtime)
{
	if (!runtime)
		return "";
	return runtime->loadedScene.c_str();
}

FOREST_RUNTIME_API int ForestRuntime_IsProjectLoaded(ForestRuntimeHandle* runtime)
{
	if (!runtime)
		return 0;
	return runtime->projectLoaded ? 1 : 0;
}

FOREST_RUNTIME_API const char* ForestRuntime_GetProjectRoot(ForestRuntimeHandle* runtime)
{
	if (!runtime)
		return "";
	return runtime->projectRoot.c_str();
}
