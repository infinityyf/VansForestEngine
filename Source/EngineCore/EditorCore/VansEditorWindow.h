#pragma once
#include "../RenderCore/VansCamera.h"
#include "../RenderCore/VansGraphicsDevice.h"
#include "../../Application/VansBasicWindow.h"
#include "Windows/VansBaseWindowComponent.h"
#include <cstdint>
#include <memory>
#include <vector>

namespace Vans
{
    class VansSceneDocument;
    class VansSceneEditService;
    class VansSceneSaveService;
    class VansProjectSelector;
}

namespace Vans::EditorAPI
{
	class IEngineEditorAPI;
	enum class RuntimeSceneLoadMode;
}

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif

namespace VansGraphics
{
	class VansAnimGraphEditorWindow;
	class VansSceneAnimationPreviewWindow;
	class VansBoneMaskEditorWindow;
	class VansTimelineEditorWindow;
	class VansGameplayActionEditorWindow;
	class VansGAFDebuggerWindow;
	class VansUIEditorWindow;
	class VansHierachuWindow;
	class VansLightWindow;
	class VansProjectWindow;
	class VansProjectSettingsWindow;
	class VansSceneWindow;
	class VansInspectorWindow;
	class VansGBufferWindow;
	class VansRenderDebugWindow;
	class VansScriptorWindow;
	class VansConsoleWindow;
	class VansProfilerWindow;
	class VansClothProfileEditorWindow;
	class VansWaterWindow;
	class VansTerrainWindow;
	class VansReflectionProbeWindow;
	class VansGIWindow;
	class VansPostProcessWindow;
	class VansShadowDebuggerWindow;
	class VansPcgWindow;
	class VansHiZCullWindow;
	class VansAudioDebugWindow;
	class VansSkeletonDebugWindow;
	class VansMotionMatchingDebugWindow;
	class VansRenderSystem;

	//编辑器窗口
	class VansEditorWindow
	{
	public:

		static bool m_GBufferWindowOpen;
		static bool m_WaterGBufferWindowOpen;

		static bool m_RenderDebugWindowOpen;
		static bool m_HairDebugWindowOpen;

		static bool m_LightWindowOpen;
		static bool m_ScriptorWindowOpen;
		static bool m_ConsoleWindowOpen;
		static bool m_ProfilerWindowOpen;
		static bool m_UIEditorWindowOpen;
		static bool m_WaterWindowOpen;
		static bool m_TerrainWindowOpen;
		static bool m_ReflectionProbeWindowOpen;
		static bool m_GIWindowOpen;
		static bool m_PostProcessWindowOpen;
		static bool m_ShadowDebuggerWindowOpen;
		static bool m_PcgWindowOpen;
		static bool m_HiZCullWindowOpen;
		static bool m_ProjectSettingsWindowOpen;
		static bool m_AudioDebugWindowOpen;
		static bool m_GAFDebuggerWindowOpen;
		static bool m_SkeletonDebugWindowOpen;
		static bool m_MotionMatchingDebugWindowOpen;

		static bool m_WireframeMode;
		static bool m_VehicleDebugGizmos;
		static bool m_HiZCullDebugVisualization;
		static bool m_SkeletonDebugGizmos;
		static bool m_SkeletonDebugSelectedOnly;
		static bool m_SkeletonDebugShowNames;
		static bool m_SkeletonDebugShowRetargetSource;

	public: 
		static VansBasicWindow m_VansEditorWindow;
		
	public:
		static bool CreateVansEditorWindow(int width, int height, GRAPHICS_API api);

		static void StartEditorLoop(
			VansGraphics::VansCamera& camera,
			VansGraphics::VansRenderSystem& renderSystem);

		static std::unique_ptr<IVansRenderFrameOverlay> DrawEditorWindows(VansGraphicsDevice& device);

		static void DestroyVansEditorWindow();

		static Vans::VansSceneDocument* GetSceneDocument();
		static Vans::VansSceneEditService* GetSceneEditService();
		static Vans::EditorAPI::IEngineEditorAPI* GetEditorAPI();
		static void ReloadCurrentSceneForEditing();
		// Automation-only entry point. Normal editor startup is unchanged unless
		// the application explicitly queues a project path.
		static void QueueProjectOpenForAutomation(const std::string& projectPath);

	private:

		static void CreateWindowComponents();

		/// Setup ImGui fonts, style, and color theme
		static void SetupImGuiStyle();

		/// 处理延迟场景加载（从主循环中提取）
		static void ProcessPendingSceneLoad();
		static void ProcessRuntimeMultiMeshHierarchyExpansion();
		static void DetachEditorViewportCamerasFromSceneTransforms();

		/// 处理延迟项目加载，确保项目切换只发生在主循环安全点
		static void ProcessPendingProjectLoad();

		/// 绘制顶部运行控制工具栏（Play / Pause / Resume / Stop）
		static void DrawPlayControlToolbar();
		static void DrawBuildMenu();

		/// 运行控制动作
		static void OnPlay();
		static void OnPause();
		static void OnResume();
		static void OnStop();
		static void OpenSelectedAnimationGraph();

		/// 查询当前是否处于编辑模式（非 Playing / Paused）
		static bool IsEditing();

		static std::vector<VansGraphics::VansCamera*> m_Cameras;

	public:

		static std::vector<std::unique_ptr<VansBaseWindowComponent>> m_Windows;

		static VansHierachuWindow* m_HierachyWindow;

		static VansLightWindow* m_LightWindow;

		static VansProjectWindow* m_ProjectWindow;

		static VansProjectSettingsWindow* m_ProjectSettingsWindow;

		static VansSceneWindow* m_SceneWindow;

		static VansInspectorWindow* m_InspectorWindow;

		static VansGBufferWindow* m_GBufferWindow;

		static VansRenderDebugWindow* m_RenderDebugWindow;

		static VansScriptorWindow* m_ScriptorWindow;

		static VansConsoleWindow* m_ConsoleWindow;

		static VansProfilerWindow* m_ProfilerWindow;

		static VansAnimGraphEditorWindow* m_AnimGraphEditorWindow;
		static VansSceneAnimationPreviewWindow* m_SceneAnimationPreviewWindow;
		static VansBoneMaskEditorWindow* m_BoneMaskEditorWindow;
		static VansTimelineEditorWindow* m_TimelineEditorWindow;
		static VansGameplayActionEditorWindow* m_GameplayActionEditorWindow;
		static VansGAFDebuggerWindow* m_GAFDebuggerWindow;

		// Asset-document based animation authoring entry used by Project and
		// Inspector windows. Dispatch is by current canonical extension.
		static void OpenAnimationAsset(const std::string& sourcePath);
		static void OpenAssetForAuthoring(const std::string& sourcePath);
		static void OpenTimelineInstance(const std::string& sourcePath, const std::string& ownerEntityGuid);

		static VansUIEditorWindow* m_UIEditorWindow;

		static VansClothProfileEditorWindow* m_ClothProfileEditorWindow;

		static VansWaterWindow* m_WaterWindow;

		static VansTerrainWindow* m_TerrainWindow;

		static VansReflectionProbeWindow* m_ReflectionProbeWindow;

		static VansGIWindow* m_GIWindow;

		static VansPostProcessWindow* m_PostProcessWindow;

		static VansShadowDebuggerWindow* m_ShadowDebuggerWindow;

		static VansPcgWindow* m_PcgWindow;

		static VansHiZCullWindow* m_HiZCullWindow;

		static VansAudioDebugWindow* m_AudioDebugWindow;
		static VansSkeletonDebugWindow* m_SkeletonDebugWindow;
		static VansMotionMatchingDebugWindow* m_MotionMatchingDebugWindow;

	private:

		/// ImGui-based project selector overlay (shown until a project is loaded)
		static std::unique_ptr<Vans::VansProjectSelector> m_ProjectSelector;

		/// True once a project has been successfully opened/created
		static bool m_ProjectLoaded;

		/// 当前已加载场景的绝对路径（Stop 时用于重载）
		static std::string m_CurrentLoadedScenePath;

		/// 下一次延迟场景加载所使用的模式（Editor / Runtime）
		static Vans::EditorAPI::RuntimeSceneLoadMode m_PendingSceneLoadMode;

		struct VansPendingProjectLoad
		{
			bool m_Requested = false;
			bool m_CreateNew = false;
			std::string m_ProjectPath;
			std::string m_ProjectName;
		};

		static VansPendingProjectLoad m_PendingProjectLoad;

		static std::unique_ptr<Vans::VansSceneDocument> m_SceneDocument;
		static std::unique_ptr<Vans::VansSceneEditService> m_SceneEditService;
		static std::unique_ptr<Vans::VansSceneSaveService> m_SceneSaveService;
		static Vans::EditorAPI::IEngineEditorAPI* m_EditorAPI;
		static std::uint64_t m_RuntimeMultiMeshExpansionScannedStateId;

	public:
		/// Deferred scene load: set during ImGui frame, processed before next Rendering()
		static std::string m_PendingScenePath;

	};
}
