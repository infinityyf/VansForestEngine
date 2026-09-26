#pragma once
#include "../RenderCore/VansCamera.h"
#include "../RenderCore/VansGraphicsDevice.h"
#include "../../Application/VansBasicWindow.h"
#include "Windows/VansBaseWindowComponent.h"
#include "VansEditorWindowCatalog.h"
#include "VansEditorWindowRegistry.h"
#include "VansEditorPackageSession.h"
#include "VansEditorPrefabSession.h"
#include "VansEditorProjectSession.h"
#include "VansEditorSceneDocumentSession.h"
#include "VansEditorSceneLoadSession.h"
#include "VansEditorDebugViewState.h"
#include <glm/mat4x4.hpp>
#include <cstdint>
#include <memory>
#include <vector>

struct ImVec2;

namespace Vans
{
    class IVansEditorAPIHost;
    class VansSceneDocument;
    class VansSceneEditService;
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
	class VansParticleDebugWindow;
	class VansMotionMatchingDebugWindow;
	class VansAIDebugWindow;
	class VansRenderSystem;
	struct VansEditorConfiguration;
	enum class VansEditorPlayCommand;

	//编辑器窗口
	class VansEditorWindow
	{
	public:
		static bool CreateVansEditorWindow(int width, int height, GRAPHICS_API api);
		static VansBasicWindow& NativeWindow();
		static void EnableSkeletonDebugForAutomation();
		static void AttachEditorAPIHost(Vans::IVansEditorAPIHost& host);
		static void DetachEditorAPIHost(Vans::IVansEditorAPIHost& host);

		static void StartEditorLoop(
			VansGraphics::VansCamera& camera,
			VansGraphics::VansRenderSystem& renderSystem);

		static std::unique_ptr<IVansRenderFrameOverlay> DrawEditorWindows(VansGraphicsDevice& device);

		static void DestroyVansEditorWindow();

		static Vans::VansSceneDocument* GetSceneDocument();
		static Vans::VansSceneEditService* GetSceneEditService();
		static Vans::EditorAPI::IEngineEditorAPI* GetEditorAPI();
		static bool IsWindowOpen(VansEditorWindowId id);
		static bool* WindowOpenState(VansEditorWindowId id);
		static bool DrawSceneAnimationPreviewViewportHandle(
			Vans::EditorAPI::IEngineEditorAPI& editorAPI,
			VansCamera* camera,
			const ImVec2& viewportOrigin,
			const ImVec2& viewportSize);
		static void DrawParticleDebugSceneOverlay(
			Vans::EditorAPI::IEngineEditorAPI& editorAPI,
			const glm::mat4& viewProjection,
			const ImVec2& origin,
			const ImVec2& size);
		static void RequestSceneLoad(const std::string& scenePath);
		static void ReloadCurrentSceneForEditing();
        static void QueuePrefabCreation(std::string entity, std::string directory, std::string documentToken);
        static void QueuePrefabPlacement(std::string asset, std::string parent, float x, float y, float z);
        static void QueuePrefabOpen(std::string path);
        static void QueuePrefabDuplicate(std::string root);
        static void QueuePrefabDelete(std::string root);
        static void ProcessPrefabRequests();
        static void DrawPrefabToolbar();
        static bool SavePrefabSession();
        static bool HasPrefabSession();
        static bool HasPendingPrefabRequests();
        static bool RefreshActiveScenePreview();
        static std::string ActiveDocumentToken();
		// Automation-only entry point. Normal editor startup is unchanged unless
		// the application explicitly queues a project path.
		static void QueueProjectOpenForAutomation(const std::string& projectPath);

	private:

		static void CreateWindowComponents();

		/// 处理延迟场景加载（从主循环中提取）
		static void ProcessPendingSceneLoad();
		static void ProcessRuntimeMultiMeshHierarchyExpansion();
		static void DetachEditorViewportCamerasFromSceneTransforms();

		/// 处理延迟项目加载，确保项目切换只发生在主循环安全点
		static void ProcessPendingProjectLoad();

		static void DrawBuildMenu();

		static void ExecutePlayCommand(VansEditorPlayCommand command);
		static void OpenSelectedAnimationGraph();

		/// 查询当前是否处于编辑模式（非 Playing / Paused）
		static bool IsEditing();

		template <typename T>
		static T* Window()
		{
			return m_WindowRegistry.Find<T>();
		}

		static std::vector<VansGraphics::VansCamera*> m_Cameras;

	public:

		// Asset-document based animation authoring entry used by Project and
		// Inspector windows. Dispatch is by current canonical extension.
		static void OpenAnimationAsset(const std::string& sourcePath);
		static void OpenAssetForAuthoring(const std::string& sourcePath);
		static void OpenTimelineInstance(const std::string& sourcePath, const std::string& ownerEntityGuid);

	private:

		static Vans::IVansEditorAPIHost* m_EditorAPIHost;
		static VansEditorWindowCatalog m_WindowCatalog;
		static VansEditorWindowRegistry m_WindowRegistry;
		static VansEditorPackageSession m_PackageSession;
		static VansEditorPrefabSession m_PrefabSession;
		static VansEditorProjectSession m_ProjectSession;
		static VansEditorSceneDocumentSession m_SceneDocumentSession;
		static VansEditorSceneLoadSession m_SceneLoadSession;
		static VansEditorDebugViewState m_DebugViewState;
		static std::unique_ptr<VansEditorConfiguration> m_EditorConfiguration;
		static VansBasicWindow m_VansEditorWindow;
		static std::uint64_t m_RuntimeMultiMeshExpansionScannedStateId;

	};
}
