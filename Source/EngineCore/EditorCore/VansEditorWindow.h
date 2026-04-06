#pragma once
#include "../RenderCore/VansCamera.h"
#include "../RenderCore/VansGraphicsDevice.h"
#include "../../VansBasicWindow.h"
#include "Windows/VansBaseWindowComponent.h"
#include "../ScriptCore/VansScriptContext.h"
#include "../ProjectSystem/VansProjectSelector.h"
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
	class VansGBufferWindow;
	class VansRenderDebugWindow;
	class VansScriptorWindow;
	class VansConsoleWindow;
	class VansProfilerWindow;

	//编辑器窗口
	class VansEditorWindow
	{
	public:

		static bool m_GBufferWindowOpen;

		static bool m_RenderDebugWindowOpen;

		static bool m_WireframeMode;

	public: 
		static VansBasicWindow m_VansEditorWindow;
		
	public:
		static bool CreateVansEditorWindow(int width, int height, GRAPHICS_API api);

		static void StartEditorLoop(VansGraphics::VansCamera& camera);

		static void DrawEditorWindows(VansVKDevice* device);

		static void DestroyVansEditorWindow();

	private:

		static void CreateWindowComponents();

		/// Setup ImGui fonts, style, and color theme
		static void SetupImGuiStyle();

		/// Register camera input listeners with VansInputManager
		static void RegisterCameraInputListeners();

		/// Unregister camera input listeners
		static void UnregisterCameraInputListeners();

		static std::vector<VansGraphics::VansCamera*> m_Cameras;

	public:

		static std::vector<VansBaseWindowComponent*> m_Windows;

		static VansHierachuWindow* m_HierachyWindow;

		static VansLightWindow* m_LightWindow;

		static VansProjectWindow* m_ProjectWindow;

		static VansSceneWindow* m_SceneWindow;

		static VansInspectorWindow* m_InspectorWindow;

		static VansGBufferWindow* m_GBufferWindow;

		static VansRenderDebugWindow* m_RenderDebugWindow;

		static VansScriptorWindow* m_ScriptorWindow;

		static VansConsoleWindow* m_ConsoleWindow;

		static VansProfilerWindow* m_ProfilerWindow;

	private:

		static VansScriptContext m_ScriptContext;

		/// ImGui-based project selector overlay (shown until a project is loaded)
		static Vans::VansProjectSelector* m_ProjectSelector;

		/// True once a project has been successfully opened/created
		static bool m_ProjectLoaded;

	public:
		/// Deferred scene load: set during ImGui frame, processed before next Rendering()
		static std::string m_PendingScenePath;

		/// Deferred resource load: set during ImGui frame, processed before scene load
		static std::string m_PendingResourcePath;
	};
}