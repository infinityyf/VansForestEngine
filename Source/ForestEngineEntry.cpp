#include "EngineCore/RenderCore/VansGraphicsDevice.h"

//#if defined FOREST_EDITOR
#include "EngineCore/EditorCore/VansEditorWindow.h"

#include "EngineCore/RenderCore/VulkanCore/VansVKDevice.h"
#include "EngineCore/RenderCore/VulkanCore/VansGUIVulkanBackEnd.h"

#include "EngineCore/RenderCore/VansCamera.h"
#include "EngineCore/RenderCore/VansScene.h"

using namespace VansGraphics;
int main()
{
	//create window
	VansEditorWindow::CreateVansEditorWindow(1280,720,VULKAN);


	//setup vulkan backeend
	m_GraphicsDevice = new VansVulkan::VansVKDevice({ 1280,720 });
	m_GUIBackEnd = new VansVulkan::VansVulkanGUIBackEnd();

	//wrap back end with camera
	VansCamera camera(glm::vec3(1,1,5), glm::vec3(0,-90,0), m_GraphicsDevice);

	m_Scene = new VansScene();
	
	m_Scene->InjectCamera(&camera);

	m_GraphicsDevice->BeforeRendering();

	//run the editor
	VansEditorWindow::StartEditorLoop(camera);

	m_GraphicsDevice->AfterRendering();

	m_Scene->UnLoadScene();

	delete m_Scene;
	delete m_GraphicsDevice;
	delete m_GUIBackEnd;
}