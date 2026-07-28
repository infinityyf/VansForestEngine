#include "ForestRuntimeCAPI.h"

#include "../EngineCore/AudioCore/VansAudioSystem.h"
#include "../EngineCore/Configration/VansConfigration.h"
#include "../EngineCore/PhysicsCore/VansPhysics.h"
#include "../EngineCore/ProjectSystem/VansProjectManager.h"
#include "../EngineCore/RenderCore/SceneBuild/VansSceneProjectResourceBuilder.h"
#include "../EngineCore/RenderCore/VansCamera.h"
#include "../EngineCore/RenderCore/VansGraphicsDevice.h"
#include "../EngineCore/RenderCore/VansScene.h"
#include "../EngineCore/RenderCore/VansShaderManager.h"
#include "../EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "../EngineCore/RuntimeCore/VansRuntimeWindow.h"
#include "../EngineCore/RuntimeCore/VansFramePhase.h"
#include "../EngineCore/RuntimeCore/VansThreadContract.h"
#include "../EngineCore/ScriptCore/VansScriptContext.h"
#include "../EngineCore/Util/VansInputManager.h"
#include "../EngineCore/RuntimeUI/Public/VansUISystem.h"
#include "../EngineCore/Util/VansJobSystem.h"
#include "../EngineCore/Util/VansLog.h"
#include "../EngineCore/VansTimer.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

struct ForestRuntimeHandle
{
	bool packageLoaded = false;
	bool projectLoaded = false;
	fs::path manifestPath;
	fs::path contentRoot;
	std::string loadedScene;
	std::string startupMode = "play";
	std::string projectRoot;
	std::string lastError;
	std::unique_ptr<Vans::VansRuntimeWindow> window;
	std::unique_ptr<VansGraphics::VansVKDevice> device;
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
	std::string ReadTextFile(const fs::path& path)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
			return {};

		std::ostringstream stream;
		stream << file.rdbuf();
		return stream.str();
	}

	std::string ReadJsonStringField(const std::string& json, const std::string& fieldName)
	{
		const std::string key = "\"" + fieldName + "\"";
		const std::size_t keyPos = json.find(key);
		if (keyPos == std::string::npos)
			return {};

		const std::size_t colonPos = json.find(':', keyPos + key.size());
		if (colonPos == std::string::npos)
			return {};

		const std::size_t quoteStart = json.find('"', colonPos + 1);
		if (quoteStart == std::string::npos)
			return {};

		std::string value;
		bool escaping = false;
		for (std::size_t i = quoteStart + 1; i < json.size(); ++i)
		{
			const char c = json[i];
			if (escaping)
			{
				switch (c)
				{
				case 'n': value.push_back('\n'); break;
				case 'r': value.push_back('\r'); break;
				case 't': value.push_back('\t'); break;
				default: value.push_back(c); break;
				}
				escaping = false;
				continue;
			}
			if (c == '\\')
			{
				escaping = true;
				continue;
			}
			if (c == '"')
				break;
			value.push_back(c);
		}
		return value;
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

	if (runtime->device)
	{
		runtime->device->WaitForDevice();
		if (runtime->renderingPrepared)
		{
			runtime->device->AfterRendering();
			runtime->renderingPrepared = false;
			runtime->device->WaitForDevice();
		}
	}

	if (runtime->scene)
	{
		if (runtime->scene->IsSceneReady() || runtime->scene->IsSceneSwitching())
				runtime->scene->UnLoadScene();
			runtime->scene->UnloadProjectResources(runtime->device.get());
		}

		VansRuntime::VansUISystem::Get().Shutdown();
		Vans::VansInputManager::Get().Shutdown();
		VansGraphics::VansShaderManager::Get().Clear();

		if (runtime->scriptContext)
		{
			runtime->scriptContext->SetScene(nullptr);
			runtime->scriptContext->ShutdownLua();
			runtime->scriptContext.reset();
		}
		runtime->camera.reset();
		runtime->scene.reset();
		m_Scene = nullptr;
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

	const std::string manifestText = ReadTextFile(manifest);
	if (manifestText.empty())
	{
		SetError(runtime, "Package manifest is empty or unreadable: " + manifest.string());
		return 0;
	}

	const std::string format = ReadJsonStringField(manifestText, "format");
	if (format != "ForestGamePackage")
	{
		SetError(runtime, "Unsupported package manifest format: " + format);
		return 0;
	}

	const std::string platform = ReadJsonStringField(manifestText, "platform");
	if (platform != "Windows")
	{
		SetError(runtime, "Unsupported package platform: " + platform);
		return 0;
	}

	const std::string scene = ReadJsonStringField(manifestText, "scene");
	if (scene.empty())
	{
		SetError(runtime, "Package manifest does not specify a scene");
		return 0;
	}

	std::string startupMode = ReadJsonStringField(manifestText, "startupMode");
	if (startupMode.empty())
		startupMode = "play";
	if (startupMode != "play")
	{
		SetError(runtime, "Unsupported package startup mode: " + startupMode);
		return 0;
	}

	const fs::path contentRoot = manifest.parent_path();
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

	if (VansConfigration* configuration = VansConfigration::GetInstance())
		configuration->SetProjectRootPath(contentRoot.string());

	Vans::VansProjectManager& projectManager = Vans::VansProjectManager::Get();
	Vans::VansProjectOpenOptions openOptions;
	openOptions.updateLastOpenedAt = false;
	openOptions.updateRecentProjects = false;
	openOptions.loadProjectSettings = false;
	openOptions.scanAssets = true;
	if (!projectManager.OpenProject(contentRoot.string(), openOptions))
	{
		SetError(runtime, "Cannot open packaged project: " + contentRoot.string());
		return 0;
	}
	projectManager.GetSceneManager().SetCurrentScene(scene);

	runtime->packageLoaded = true;
	runtime->projectLoaded = true;
	runtime->manifestPath = manifest;
	runtime->contentRoot = contentRoot;
	runtime->loadedScene = scene;
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

	runtime->device = std::move(device);
	runtime->device->SetRuntimeSwapchainPresentationEnabled(true);
	m_GraphicsDevice = runtime->device.get();

	runtime->scene = std::make_unique<VansGraphics::VansScene>();
	m_Scene = runtime->scene.get();

	RegisterEngineShaders();
	VansGraphics::VansSceneProjectResourceBuilder::LoadShadersFromRegistry(
		*runtime->scene,
		VansConfigration::GetInstance()->GetProjectRootPath(),
		runtime->device->GetLogicDevice());

	runtime->device->BeforeRendering();
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

	Vans::VansProjectManager& projectManager = Vans::VansProjectManager::Get();
	Vans::VansAssetDatabase* database = projectManager.GetAssetDatabase();
	if (!database)
	{
		SetError(runtime, "Packaged project has no asset database");
		return 0;
	}

	const fs::path scenePath = (runtime->contentRoot / runtime->loadedScene).lexically_normal();
	if (!runtime->scene->LoadProjectAssets(*database, scenePath, runtime->device.get()))
	{
		SetError(runtime, "Project asset loading failed for scene: " + scenePath.string());
		return 0;
	}

	runtime->camera = std::make_unique<VansGraphics::VansCamera>(runtime->device.get());
	runtime->scene->InjectCamera(runtime->camera.get());

	runtime->scriptContext = std::make_unique<VansScriptContext>();
	runtime->scriptContext->VansScriptSetup();
	VANS_LOG("[ForestRuntime] Runtime script context ready");

	runtime->scene->LoadSceneForRendering(scenePath.string().c_str(), runtime->device.get(), VansGraphics::VansSceneLoadMode::Runtime);
	if (!runtime->scene->IsSceneReady())
	{
		SetError(runtime, "Scene failed to become ready: " + scenePath.string());
		return 0;
	}

	runtime->scriptContext->SetScene(runtime->scene.get());
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
	VansGraphics::VansTimer::Update();
	if (runtime->graphicsInitialized && runtime->window)
	{
		Vans::VansInputManager::Get().Update();
		runtime->window->PollEvents();
		Vans::VansInputManager::Get().RefreshPolledState();
	}
	if (runtime->sceneLoaded && runtime->scene && runtime->scene->IsSceneReady())
	{
		VANS_SET_FRAME_PHASE(VansFramePhase::GameLogic);

		if (VansEngine::VansPhysicsSystem::GetInstance().IsSimulationRunning())
			runtime->scene->UpdatePhysicsTransforms();

		if (runtime->scriptContext)
		{
			runtime->scriptContext->SetScene(runtime->scene.get());
			runtime->scriptContext->VansScriptUpdateNonCameraScripts();
		}

		if (VansEngine::VansPhysicsSystem::GetInstance().IsSimulationRunning())
			runtime->scene->UpdateCharControllerTransforms();

		if (runtime->scriptContext)
			runtime->scriptContext->VansScriptUpdateCameraScripts();
	}
	return 1;
}

FOREST_RUNTIME_API int ForestRuntime_RenderFrame(ForestRuntimeHandle* runtime)
{
	if (!runtime)
		return 0;
	if (!runtime->graphicsInitialized || !runtime->sceneLoaded || !runtime->window || !runtime->device || !runtime->camera)
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
			runtime->device->OnWindowResize(static_cast<std::uint32_t>(width), static_cast<std::uint32_t>(height));
	}

	runtime->camera->Rendering();
	runtime->camera->Present();
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
		return;

	ShutdownGraphics(runtime);
	runtime->packageLoaded = false;
	runtime->projectLoaded = false;
	runtime->loadedScene.clear();
	runtime->projectRoot.clear();
	runtime->lastError.clear();
	Vans::VansProjectManager::Get().CloseProject();
	ShutdownRuntimeCore(runtime);
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
