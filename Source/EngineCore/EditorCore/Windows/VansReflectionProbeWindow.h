#pragma once

#include "VansBaseWindowComponent.h"
#include "../../RenderCore/VansScene.h"

namespace VansGraphics
{
	class VansReflectionProbeWindow : public VansBaseWindowComponent
	{
	public:
		void RegistScene(VansScene* scene) { m_Scene = scene; }

	private:
		VansScene* m_Scene = nullptr;
		void ShowWindow(VansVKDevice& device) override;
	};
}
