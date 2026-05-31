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
	class VansAnimGraphEditorWindow;
	class VansUIEditorWindow;
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
	class VansClothProfileEditorWindow;

	// 前置声明（定义在 VansScene.h）
	enum class VansSceneLoadMode;

	/// 编辑器运行控制状态
	enum class VansEditorPlayState
	{
		Editing,  // 默认：场景已加载，时间不推进
		Playing,  // 运行中：时间推进，物理与脚本均激活
		Paused,   // 暂停：时间冻结，物理与脚本停止
	};

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

		/// 处理延迟场景加载（从主循环中提取）
		static void ProcessPendingSceneLoad();

		/// 绘制顶部运行控制工具栏（Play / Pause / Resume / Stop）
		static void DrawPlayControlToolbar();

		/// 运行控制动作
		static void OnPlay();
		static void OnPause();
		static void OnResume();
		static void OnStop();

		/// 查询当前是否处于编辑模式（非 Playing / Paused）
		static bool IsEditing() { return m_PlayState == VansEditorPlayState::Editing; }

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

		static VansAnimGraphEditorWindow* m_AnimGraphEditorWindow;

		static VansUIEditorWindow* m_UIEditorWindow;

		static VansClothProfileEditorWindow* m_ClothProfileEditorWindow;

	private:

		static VansScriptContext m_ScriptContext;

		/// ImGui-based project selector overlay (shown until a project is loaded)
		static Vans::VansProjectSelector* m_ProjectSelector;

		/// True once a project has been successfully opened/created
		static bool m_ProjectLoaded;

		/// 当前编辑器运行状态
		static VansEditorPlayState m_PlayState;

		/// 当前已加载场景的绝对路径（Stop 时用于重载）
		static std::string m_CurrentLoadedScenePath;

		/// 下一次延迟场景加载所使用的模式（Editor / Runtime）
		static VansGraphics::VansSceneLoadMode m_PendingSceneLoadMode;

	public:
		/// Deferred scene load: set during ImGui frame, processed before next Rendering()
		static std::string m_PendingScenePath;

		/// Deferred resource load: set during ImGui frame, processed before scene load
		static std::string m_PendingResourcePath;
	};
}