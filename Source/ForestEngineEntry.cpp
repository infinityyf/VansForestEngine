//#if defined FOREST_EDITOR
#include "EngineCore/EditorCore/VansEditorWindow.h"

#include "EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "EngineCore/RenderCore/VulkanCore/VansGUIVulkanBackEnd.h"

#include "EngineCore/RenderCore/VansCamera.h"
#include "EngineCore/RenderCore/VansScene.h"

#include "EngineCore/Configration/VansConfigration.h"
#include "EngineCore/EditorCore/AssetsSystem/VansAssetsFileWatcher.h"
#include "EngineCore/Util/VansJobSystem.h"
#include "EngineCore/PhysicsCore/VansPhysics.h"
#include "EngineCore/Util/VansLog.h"
#include "EngineCore/VansThreadContract.h"
#include "EngineCore/AudioCore/VansAudioSystem.h"
#include "EngineCore/RuntimeUI/Public/VansUISystem.h"
#include "EngineCore/RenderCore/VansShaderManager.h"

// Project System
#include "EngineCore/ProjectSystem/VansProjectManager.h"

using namespace VansGraphics;
using namespace VansEngine;

static VansAssetsFileWatcher* g_EditorAssetFileWatcher = nullptr;

#ifdef _DEBUG
std::thread::id g_MainThreadId;
#endif

// Forward declarations
bool InitializeEngineCore();
bool InitializeGraphicsSystem();
bool InitializePhysicsSystem();
void RunMainLoop(VansCamera& camera);
void ShutdownEngine();

bool InitializeEngineCore()
{
	VANS_LOG("[ForestEngine] Initializing core systems...");
	
	// Initialize Job System
	Vans::VansJobSystem::Get().Initialize();
	VANS_LOG("[ForestEngine] Job system initialized");

	// Initialize Physics System
	if (!InitializePhysicsSystem())
	{
		VANS_LOG_ERROR("[ForestEngine] Failed to initialize physics system!");
		return false;
	}

	// Initialize Audio System
	if (!VansEngine::VansAudioSystem::GetInstance().Initialize())
	{
		VANS_LOG_ERROR("[ForestEngine] Failed to initialize audio system (OpenAL)! 空间音效将无法播放");
		// 音频失敗不是致命错误，继续启动
	}

	return true;
}

bool InitializeGraphicsSystem()
{
	VANS_LOG("[ForestEngine] Initializing graphics system...");

	// Create window
	if (!VansEditorWindow::CreateVansEditorWindow(1280 * 2, 720 * 2, VULKAN))
	{
		VANS_LOG_ERROR("[ForestEngine] Failed to create editor window!");
		return false;
	}
	VANS_LOG("[ForestEngine] Editor window created");

	// Setup vulkan backend
	auto* vkDevice = new VansVKDevice({ 1280, 720 }, &VansEditorWindow::m_VansEditorWindow);
	if (!vkDevice->IsInitialized())
	{
		VANS_LOG_ERROR("[ForestEngine] Failed to initialize Vulkan device!");
		delete vkDevice;
		return false;
	}
	m_GraphicsDevice = vkDevice;
	m_GUIBackEnd = new VansGraphicsGUIBackEnd();
	VANS_LOG("[ForestEngine] Vulkan backend initialized");

	// Initialize scene
	m_Scene = new VansScene();
	VANS_LOG("[ForestEngine] Scene system initialized");

	// Setup file watcher
	g_EditorAssetFileWatcher = new VansAssetsFileWatcher();
	m_Scene->SetShaderHotReloadService(g_EditorAssetFileWatcher);
	g_EditorAssetFileWatcher->Start([](const std::string& folder, const std::string& filename)
	{
		VANS_LOG("[FileWatcher] File changed: " << folder + "\\" + filename);
	});
	VANS_LOG("[ForestEngine] Asset file watcher started");

	// Register and load all built-in engine shaders early, before
	// BeforeRendering() triggers PrepareRenderingData() which relies on
	// compute shaders (PreConDiffuseEnvironment, etc.) already being loaded.
	RegisterEngineShaders();
	m_Scene->LoadShadersFromRegistry(
	    VansConfigration::GetInstance()->GetProjectRootPath(),
	    vkDevice->GetLogicDevice());
	VANS_LOG("[ForestEngine] Engine shaders registered and loaded");

	return true;
}

bool InitializePhysicsSystem()
{
	VANS_LOG("[ForestEngine] Initializing physics system...");

	VansPhysicsSystem& physics = VansPhysicsSystem::GetInstance();
	if (!physics.Initialize())
	{
		return false;
	}

	VANS_LOG("[ForestEngine] Physics system initialized successfully");
	return true;
}

void RunMainLoop(VansCamera& camera)
{
	VANS_LOG("[ForestEngine] Starting main engine loop...");

	// Inject camera into scene
	m_Scene->InjectCamera(&camera);

	// Prepare for rendering
	m_GraphicsDevice->BeforeRendering();

	// Start physics simulation thread
	VansPhysicsSystem::GetInstance().StartSimulation();
	VANS_LOG("[ForestEngine] Physics simulation started");

	// Run the editor main loop
	VansEditorWindow::StartEditorLoop(camera);

	// End rendering
	m_GraphicsDevice->AfterRendering();
	
	VANS_LOG("[ForestEngine] Main loop finished");
}

void ShutdownEngine()
{
	VANS_LOG("[ForestEngine] Shutting down engine systems...");

	// ============================================================
	// 关闭顺序契约（见 重构-02）：
	// 1. 暂停物理，防止关闭期间继续模拟
	// 2. Vulkan device idle，确保 GPU 不再访问场景资源
	// 3. scene->UnLoadScene()，此时 PhysX/OpenAL/Vulkan 必须仍然存活
	// 4. scene->UnloadProjectResources()，完整实现见 重构-08
	// 5. RuntimeUI shutdown，Noesis RenderDevice 仍需要 Vulkan
	// 6. physics Stop/Shutdown，释放 PhysX
	// 7. AudioSystem shutdown，释放 OpenAL
	// 8. GUI backend shutdown，释放 ImGui Vulkan/GLFW backend 与 descriptor pool
	// 9. Editor window/panels shutdown，释放 ImGui context、GLFW 与编辑器工具对象
	// 10. ShaderManager shutdown，释放 shader module / pipeline（Vulkan device 仍存活）
	// 11. graphicsDevice 最后销毁
	// 重构-02 已按上述顺序执行，后续阶段只补完整项目资源释放实现。
	// ============================================================

	VansPhysicsSystem& physics = VansPhysicsSystem::GetInstance();
	physics.PauseSimulation();
	VANS_LOG("[ForestEngine] Physics simulation paused for shutdown");

	VansVKDevice* vkDevice = dynamic_cast<VansVKDevice*>(m_GraphicsDevice);
	if (vkDevice)
	{
		vkDevice->WaitForDevice();
		VANS_LOG("[ForestEngine] Vulkan device idle before scene teardown");
	}

	// Unload scene
	if (m_Scene)
	{
		m_Scene->UnLoadScene();
		m_Scene->UnloadProjectResources(vkDevice);
		delete m_Scene;
		m_Scene = nullptr;
		VANS_LOG("[ForestEngine] Scene unloaded");
	}

	VansRuntime::VansUISystem::Get().Shutdown();
	VANS_LOG("[ForestEngine] Runtime UI system shutdown complete");

	physics.StopSimulation();
	physics.Shutdown();
	VANS_LOG("[ForestEngine] Physics system shutdown complete");

	VansEngine::VansAudioSystem::GetInstance().Shutdown();
	VANS_LOG("[ForestEngine] Audio system shutdown complete");

	// Cleanup components
	if (g_EditorAssetFileWatcher)
	{
		delete g_EditorAssetFileWatcher;
		g_EditorAssetFileWatcher = nullptr;
		VANS_LOG("[ForestEngine] File watcher stopped");
	}

	if (m_GUIBackEnd)
	{
		m_GUIBackEnd->ShutdownBackEnd();
		VANS_LOG("[ForestEngine] GUI backend shutdown complete");
	}

	VansEditorWindow::DestroyVansEditorWindow();
	VANS_LOG("[ForestEngine] Editor window released");

	VansGraphics::VansShaderManager::Get().Clear();
	VANS_LOG("[ForestEngine] Shader manager released");

	if (m_GraphicsDevice)
	{
		delete m_GraphicsDevice;
		m_GraphicsDevice = nullptr;
		VANS_LOG("[ForestEngine] Graphics device released");
	}

	if (m_GUIBackEnd)
	{
		delete m_GUIBackEnd;
		m_GUIBackEnd = nullptr;
		VANS_LOG("[ForestEngine] GUI backend released");
	}

	// Shutdown job system
	Vans::VansJobSystem::Get().Shutdown();
	VANS_LOG("[ForestEngine] Job system shutdown complete");
	
	VANS_LOG("[ForestEngine] Engine shutdown complete!");
}

class EngineRuntime
{
public:
	~EngineRuntime()
	{
		Shutdown();
	}

	bool Initialize()
	{
		if (!InitializeEngineCore())
		{
			VANS_LOG_ERROR("[ForestEngine] Failed to initialize core systems!");
			return false;
		}

		if (!InitializeGraphicsSystem())
		{
			VANS_LOG_ERROR("[ForestEngine] Failed to initialize graphics systems!");
			return false;
		}

		m_Initialized = true;
		return true;
	}

	void Run()
	{
		if (!m_Initialized)
			return;

		// Create camera for the scene (parameters will be applied from Scene.json in LoadSceneContent)
		VansCamera camera(m_GraphicsDevice);
		RunMainLoop(camera);
	}

	void Shutdown()
	{
		if (m_Shutdown)
			return;

		ShutdownEngine();
		m_Shutdown = true;
	}

private:
	bool m_Initialized = false;
	bool m_Shutdown = false;
};

int main()
{
	VANS_INIT_MAIN_THREAD();

	VANS_LOG("=== ForestEngine Starting ===");

	EngineRuntime runtime;
	if (!runtime.Initialize())
	{
		return -1;
	}

	runtime.Run();
	runtime.Shutdown();

	VANS_LOG("=== ForestEngine Exited ===");
	return 0;
}
