#pragma once
#include "VansBaseWindowComponent.h"
#include "../VansGizmos.h"
#include "../VansEditorCameraController.h"
#include <string>
#include <vector>
namespace VansGraphics
{
	class VansCamera;

	class VansSceneWindow : public VansBaseWindowComponent
	{
	public:

		void RegistCamera(VansCamera* camera)
		{
			m_Camera = camera;
		}

	private:

		VansGraphics::VansCamera* m_Camera = nullptr;
		VansEditorCameraController m_CameraController;
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
		void FinishSplineGizmo(Vans::EditorAPI::IEngineEditorAPI&,bool cancel);
		void DrawSplineTools(Vans::EditorAPI::IEngineEditorAPI&,const Vans::EditorAPI::PcgSplineSnapshot&,
			glm::vec2 origin,glm::vec2 size,bool mouseInside);

		VansGizmos m_Gizmos;
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;
	};
}
