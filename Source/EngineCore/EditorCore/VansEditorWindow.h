#pragma once
#include "../RenderCore/VansCamera.h"
#include "../RenderCore/VansGraphicsDevice.h"
#include "../../VansBasicWindow.h"
#include "Windows/VansBaseWindowComponent.h"
#include "../ScriptCore/VansScriptContext.h"
#include <vector>

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif

namespace VansGraphics
{
	class VansHierachuWindow;
	class VansLightWindow;
	class VansProjectWindow;
	class VansSceneWindow;
	class VansInspectorWindow;

	//编辑器窗口
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

		static void MouseClickCallBack(GLFWwindow* window, int button, int action, int mods);

		static std::vector<VansGraphics::VansCamera*> m_Cameras;

	public:

		static std::vector<VansBaseWindowComponent*> m_Windows;

		static VansHierachuWindow* m_HierachyWindow;

		static VansLightWindow* m_LightWindow;

		static VansProjectWindow* m_ProjectWindow;

		static VansSceneWindow* m_SceneWindow;

		static VansInspectorWindow* m_InspectorWindow;

	private:

		static VansScriptContext m_ScriptContext;
	};
}