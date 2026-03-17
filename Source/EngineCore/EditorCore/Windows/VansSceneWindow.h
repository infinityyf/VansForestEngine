#pragma once
#include "VansBaseWindowComponent.h"
#include "../../RenderCore/VansCamera.h"
#include "../../RenderCore/VansScene.h"
#include "../VansGizmos.h"
#include <string>
#include <vector>
namespace VansGraphics
{
	class VansSceneWindow : public VansBaseWindowComponent
	{
	public:

		void RegistCamera(VansCamera* camera)
		{
			m_Camera = camera;
		}

		void RegistScene(VansScene* scene)
		{
			m_Scene = scene;
		}

	private:

		VansGraphics::VansCamera* m_Camera = nullptr;

		VansGraphics::VansScene*  m_Scene  = nullptr;

		VansGizmos m_Gizmos;

		void ShowWindow(VansVKDevice& device) override;
	};
}