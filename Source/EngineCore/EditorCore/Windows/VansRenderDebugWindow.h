#pragma once
#include "VansBaseWindowComponent.h"
#include "../../RenderCore/VansScene.h"
#include <string>
#include <vector>
namespace VansGraphics
{
	class VansRenderDebugWindow : public VansBaseWindowComponent
	{
	public:
		void RegistScene(VansScene* scene) { m_Scene = scene; }

	private:
		VansScene* m_Scene = nullptr;
		void ShowWindow(VansVKDevice& device) override;
	};
}
