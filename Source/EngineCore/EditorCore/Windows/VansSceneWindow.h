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

		VansGizmos m_Gizmos;
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;
	};
}
