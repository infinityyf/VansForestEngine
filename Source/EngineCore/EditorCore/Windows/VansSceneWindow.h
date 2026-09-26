#pragma once
#include "VansBaseWindowComponent.h"
#include "../VansGizmos.h"
#include "../VansEditorCameraController.h"
#include <string>
#include <vector>

namespace Vans
{
	class VansSceneEditService;
}
namespace Vans::EditorAPI { class IPcgEditorAPI; }

namespace VansGraphics
{
	class VansCamera;
	struct VansEditorDebugViewState;

	class VansSceneWindow : public VansBaseWindowComponent
	{
	public:
		explicit VansSceneWindow(VansEditorDebugViewState& debugViewState)
			: m_DebugViewState(debugViewState)
		{
		}

        bool IsGameCursorViewportInteractive() const { return m_GameCursorViewportInteractive; }
		void SetSceneEditService(Vans::VansSceneEditService* sceneEdits)
		{
			m_SceneEdits = sceneEdits;
		}

		void RegistCamera(VansCamera* camera)
		{
			m_Camera = camera;
		}

	private:
		VansEditorDebugViewState& m_DebugViewState;

        bool m_GameCursorViewportInteractive = false;
		VansGraphics::VansCamera* m_Camera = nullptr;
		Vans::VansSceneEditService* m_SceneEdits = nullptr;
		VansEditorCameraController m_CameraController;
		bool m_ObjectPickPressed = false;
		ImVec2 m_ObjectPickStart;
		bool m_TerrainBrushDragging = false;
		bool m_TerrainBrushHit = false;
		Vans::EditorAPI::Vec3 m_TerrainBrushWorldPosition;
		bool m_PcgBrushDragging = false;
		Vans::EditorAPI::PcgBrushTarget m_PcgDragTarget;
		std::string m_PcgBrushMessage;
		bool m_SplineGizmoDragging=false;
		int m_SplineHandle=0;
		Vans::EditorAPI::PcgSplineEditRequest m_SplineGizmoEdit;
		std::string m_SplineGizmoPoint;
		void FinishSplineGizmo(Vans::EditorAPI::IPcgEditorAPI&,bool cancel);
		void DrawSplineTools(Vans::EditorAPI::IPcgEditorAPI&,const Vans::EditorAPI::PcgSplineSnapshot&,
			glm::vec2 origin,glm::vec2 size,bool mouseInside);

		VansGizmos m_Gizmos;
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;
	};
}
