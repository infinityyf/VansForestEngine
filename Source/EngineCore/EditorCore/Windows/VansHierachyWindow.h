#pragma once

#include "VansBaseWindowComponent.h"
#include "../../RenderCore/VansMaterial.h"
#include <string>
#include <vector>
namespace VansGraphics
{
	class VansHierachuWindow : public VansBaseWindowComponent
	{
	private:
		bool m_TestButton = true;

	private:
		void DrawRenderNodeList();

		void DrawRenderNodeDetail();

		void DrawTransformDetail(VansRenderNode& node);

		void DrawMaterialDetail(VansMaterial& material, int index = -1);

		void DrawPBRMaterialParameters(VansBasePBRParam& param, int id = 0);

		void DrawAtmosphereParameters(VansAtmospherePBRParam& param);

		void ShowWindow(VansVKDevice& device) override;
	};
}