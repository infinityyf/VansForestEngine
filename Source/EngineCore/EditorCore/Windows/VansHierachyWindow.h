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

		void DrawMaterialDetail(VansMaterial& material);

		void DrawPBRMaterialParameters(VansBasePBRParam& param);

		void DrawAtmosphereParameters(VansAtmospherePBRParam& param);

		void ShowWindow(VansVKDevice& device) override;
	};
}
