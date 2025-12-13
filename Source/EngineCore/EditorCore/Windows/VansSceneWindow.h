#pragma once
#include "VansBaseWindowComponent.h"
#include "../../RenderCore/VansCamera.h"
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

	private:

		VansGraphics::VansCamera* m_Camera;

		void ShowWindow(VansVKDevice& device) override;
	};
}