#pragma once

#include "VansBaseWindowComponent.h"
#include "../../RenderCore/VansMaterial.h"
#include <string>
#include <vector>
#include <set>
namespace VansGraphics
{
	class VansHierachuWindow : public VansBaseWindowComponent
	{
	private:
		bool m_TestButton = true;

	private:
		void DrawRenderNodeList();

		void DrawRenderNodeDetail();

		// Draw a list of render nodes, grouping multi-mesh children under a collapsible parent tree node.
		void DrawNodeListWithGroups(const std::vector<VansRenderNode*>& nodes);

		void DrawTransformDetail(VansRenderNode& node);

		void DrawMaterialDetail(VansMaterial& material, int index = -1);

		void DrawPBRMaterialParameters(VansBasePBRParam& param, int id = 0);

		void DrawAtmosphereParameters(VansAtmospherePBRParam& param);

		void ShowWindow(VansVKDevice& device) override;
	};
}