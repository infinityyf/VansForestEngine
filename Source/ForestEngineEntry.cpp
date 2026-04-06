//#if defined FOREST_EDITOR
#include "EngineCore/EditorCore/VansEditorWindow.h"

#include "EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "EngineCore/RenderCore/VulkanCore/VansGUIVulkanBackEnd.h"

#include "EngineCore/RenderCore/VansCamera.h"
#include "EngineCore/RenderCore/VansScene.h"

#include "EngineCore/EditorCore/AssetsSystem/VansAssetsFileWatcher.h"
#include "EngineCore/Util/VansJobSystem.h"
#include "EngineCore/PhysicsCore/VansPhysics.h"
#include "EngineCore/Util/VansLog.h"

// Project System
#include "EngineCore/ProjectSystem/VansProjectManager.h"

using namespace VansGraphics;
using namespace VansEngine;

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

	return true;
}

bool InitializeGraphicsSystem()
{
	VANS_LOG("[ForestEngine] Initializing graphics system...");

	// Create window
	VansEditorWindow::CreateVansEditorWindow(1280 * 2, 720 * 2, VULKAN);
	VANS_LOG("[ForestEngine] Editor window created");

	// Setup vulkan backend
	m_GraphicsDevice = new VansVKDevice({ 1280, 720 });
	m_GUIBackEnd = new VansGraphicsGUIBackEnd();
	VANS_LOG("[ForestEngine] Vulkan backend initialized");

	// Initialize scene
	m_Scene = new VansScene();
	VANS_LOG("[ForestEngine] Scene system initialized");

	// Setup file watcher
	m_SceneFileWatcher = new VansAssetsFileWatcher();
	m_SceneFileWatcher->Start([](const std::string& folder, const std::string& filename) 
	{
		VANS_LOG("[FileWatcher] File changed: " << folder + "\\" + filename);
	});
	VANS_LOG("[ForestEngine] Asset file watcher started");

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

	// Shutdown physics simulation
	VansPhysicsSystem& physics = VansPhysicsSystem::GetInstance();
	physics.StopSimulation();
	physics.Shutdown();
	VANS_LOG("[ForestEngine] Physics system shutdown complete");

	// Unload scene
	if (m_Scene)
	{
		m_Scene->UnLoadScene();
		delete m_Scene;
		m_Scene = nullptr;
		VANS_LOG("[ForestEngine] Scene unloaded");
	}

	// Cleanup components
	if (m_SceneFileWatcher)
	{
		delete m_SceneFileWatcher;
		m_SceneFileWatcher = nullptr;
		VANS_LOG("[ForestEngine] File watcher stopped");
	}

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

int main()
{
	VANS_LOG("=== ForestEngine Starting ===");

	// Initialize core engine systems
	if (!InitializeEngineCore())
	{
		VANS_LOG_ERROR("[ForestEngine] Failed to initialize core systems!");
		ShutdownEngine();
		return -1;
	}

	// Initialize graphics systems
	if (!InitializeGraphicsSystem())
	{
		VANS_LOG_ERROR("[ForestEngine] Failed to initialize graphics systems!");
		ShutdownEngine();
		return -1;
	}

	// Create camera for the scene
	VansCamera camera(glm::vec3(1, 1, 1), glm::vec3(0, -90, 0), m_GraphicsDevice);

	// Run the main engine loop
	RunMainLoop(camera);

	// Shutdown all systems
	ShutdownEngine();

	VANS_LOG("=== ForestEngine Exited ===");
	return 0;
}