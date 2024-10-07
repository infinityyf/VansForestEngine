#pragma once
#include "../RenderCore/VansCamera.h"
#include "../RenderCore/VansGraphicsDevice.h"
#include "../../VansBasicWindow.h"
#include "Windows/VansBaseWindowComponent.h"
#include <vector>

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif

namespace VansGraphics
{
	//±à¼­Æ÷´°¿Ú
	class VansEditorWindow
	{
	public: 
		static VansBasicWindow m_VansEditorWindow;
		
	public:
		static bool CreateVansEditorWindow(int width, int height, GRAPHICS_API api);

		static void StartEditorLoop(VansGraphics::VansCamera& camera);

		static void DrawEditorWindows(VansVKDevice* device);

		static void DestroyVansEditorWindow();

	private:

		static void CreateWindowComponents();

		static void KeyBoardInputCallBack(GLFWwindow* window, int key, int scancode, int action, int mods);

		static void MouseInputCallBack(GLFWwindow* window, double xpos, double ypos);

		static std::vector<VansGraphics::VansCamera*> m_Cameras;

	public:

		static std::vector<VansBaseWindowComponent*> m_Windows;
	};
}