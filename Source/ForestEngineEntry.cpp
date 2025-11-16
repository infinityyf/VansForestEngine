//#if defined FOREST_EDITOR
#include "EngineCore/EditorCore/VansEditorWindow.h"

#include "EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "EngineCore/RenderCore/VulkanCore/VansGUIVulkanBackEnd.h"

#include "EngineCore/RenderCore/VansCamera.h"
#include "EngineCore/RenderCore/VansScene.h"

#include "EngineCore/EditorCore/AssetsSystem/VansAssetsFileWatcher.h"

using namespace VansGraphics;
int main()
{
	//create window
	VansEditorWindow::CreateVansEditorWindow(1280 * 2,720 * 2,VULKAN);


	//setup vulkan backeend
	m_GraphicsDevice = new VansVKDevice({ 1280,720 });
	m_GUIBackEnd = new VansGraphicsGUIBackEnd();

	//wrap back end with camera
	VansCamera camera(glm::vec3(1,1,5), glm::vec3(0,-90,0), m_GraphicsDevice);

	m_Scene = new VansScene();
	m_Scene->InjectCamera(&camera);

	m_SceneFileWatcher = new VansAssetsFileWatcher();
	m_SceneFileWatcher->Start([](const std::string& folder, const std::string& filename) 
		{
			std::cout << "File changed: " << folder + "\\" + filename << std::endl;
		});

	m_GraphicsDevice->BeforeRendering();

	//run the editor
	VansEditorWindow::StartEditorLoop(camera);

	m_GraphicsDevice->AfterRendering();

	m_Scene->UnLoadScene();

	delete m_SceneFileWatcher;
	delete m_Scene;
	delete m_GraphicsDevice;
	delete m_GUIBackEnd;
}