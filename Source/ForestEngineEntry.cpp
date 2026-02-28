//#if defined FOREST_EDITOR
#include "EngineCore/EditorCore/VansEditorWindow.h"

#include "EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "EngineCore/RenderCore/VulkanCore/VansGUIVulkanBackEnd.h"

#include "EngineCore/RenderCore/VansCamera.h"
#include "EngineCore/RenderCore/VansScene.h"

#include "EngineCore/EditorCore/AssetsSystem/VansAssetsFileWatcher.h"
#include "EngineCore/Util/VansJobSystem.h"
#include "EngineCore/PhysicsCore/VansPhysics.h"

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
	std::cout << "[ForestEngine] Initializing core systems..." << std::endl;
	
	// Initialize Job System
	Vans::VansJobSystem::Get().Initialize();
	std::cout << "[ForestEngine]Job system initialized" << std::endl;

	// Initialize Physics System
	if (!InitializePhysicsSystem())
	{
		std::cerr << "[ForestEngine] Failed to initialize physics system!" << std::endl;
		return false;
	}

	return true;
}

bool InitializeGraphicsSystem()
{
	std::cout << "[ForestEngine] Initializing graphics system..." << std::endl;

	// Create window
	VansEditorWindow::CreateVansEditorWindow(1280 * 2, 720 * 2, VULKAN);
	std::cout << "[ForestEngine]Editor window created" << std::endl;

	// Setup vulkan backend
	m_GraphicsDevice = new VansVKDevice({ 1280, 720 });
	m_GUIBackEnd = new VansGraphicsGUIBackEnd();
	std::cout << "[ForestEngine]Vulkan backend initialized" << std::endl;

	// Initialize scene
	m_Scene = new VansScene();
	std::cout << "[ForestEngine]Scene system initialized" << std::endl;

	// Setup file watcher
	m_SceneFileWatcher = new VansAssetsFileWatcher();
	m_SceneFileWatcher->Start([](const std::string& folder, const std::string& filename) 
	{
		std::cout << "[FileWatcher] File changed: " << folder + "\\" + filename << std::endl;
	});
	std::cout << "[ForestEngine]Asset file watcher started" << std::endl;

	return true;
}

bool InitializePhysicsSystem()
{
	std::cout << "[ForestEngine] Initializing physics system..." << std::endl;

	VansPhysicsSystem& physics = VansPhysicsSystem::GetInstance();
	if (!physics.Initialize())
	{
		return false;
	}

	std::cout << "[ForestEngine]Physics system initialized successfully" << std::endl;
	return true;
}

void RunMainLoop(VansCamera& camera)
{
	std::cout << "[ForestEngine] Starting main engine loop..." << std::endl;

	// Inject camera into scene
	m_Scene->InjectCamera(&camera);

	// Prepare for rendering
	m_GraphicsDevice->BeforeRendering();

	// Start physics simulation thread
	VansPhysicsSystem::GetInstance().StartSimulation();
	std::cout << "[ForestEngine] Physics simulation started" << std::endl;

	// Run the editor main loop
	VansEditorWindow::StartEditorLoop(camera);

	// End rendering
	m_GraphicsDevice->AfterRendering();
	
	std::cout << "[ForestEngine] Main loop finished" << std::endl;
}

void ShutdownEngine()
{
	std::cout << "[ForestEngine] Shutting down engine systems..." << std::endl;

	// Shutdown physics simulation
	VansPhysicsSystem& physics = VansPhysicsSystem::GetInstance();
	physics.StopSimulation();
	physics.Shutdown();
	std::cout << "[ForestEngine]Physics system shutdown complete" << std::endl;

	// Unload scene
	if (m_Scene)
	{
		m_Scene->UnLoadScene();
		delete m_Scene;
		m_Scene = nullptr;
		std::cout << "[ForestEngine]Scene unloaded" << std::endl;
	}

	// Cleanup components
	if (m_SceneFileWatcher)
	{
		delete m_SceneFileWatcher;
		m_SceneFileWatcher = nullptr;
		std::cout << "[ForestEngine]File watcher stopped" << std::endl;
	}

	if (m_GraphicsDevice)
	{
		delete m_GraphicsDevice;
		m_GraphicsDevice = nullptr;
		std::cout << "[ForestEngine]Graphics device released" << std::endl;
	}

	if (m_GUIBackEnd)
	{
		delete m_GUIBackEnd;
		m_GUIBackEnd = nullptr;
		std::cout << "[ForestEngine]GUI backend released" << std::endl;
	}

	// Shutdown job system
	Vans::VansJobSystem::Get().Shutdown();
	std::cout << "[ForestEngine]Job system shutdown complete" << std::endl;
	
	std::cout << "[ForestEngine] Engine shutdown complete!" << std::endl;
}

int main()
{
	std::cout << "=== ForestEngine Starting ===" << std::endl;

	// Initialize core engine systems
	if (!InitializeEngineCore())
	{
		std::cerr << "[ForestEngine] Failed to initialize core systems!" << std::endl;
		ShutdownEngine();
		return -1;
	}

	// Initialize graphics systems
	if (!InitializeGraphicsSystem())
	{
		std::cerr << "[ForestEngine] Failed to initialize graphics systems!" << std::endl;
		ShutdownEngine();
		return -1;
	}

	// Create camera for the scene
	VansCamera camera(glm::vec3(1, 1, 1), glm::vec3(0, -90, 0), m_GraphicsDevice);

	// Run the main engine loop
	RunMainLoop(camera);

	// Shutdown all systems
	ShutdownEngine();

	std::cout << "=== ForestEngine Exited ===" << std::endl;
	return 0;
}