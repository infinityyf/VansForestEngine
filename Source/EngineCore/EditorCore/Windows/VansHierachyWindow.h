#pragma once

#include "VansBaseWindowComponent.h"
#include "../../RenderCore/VansMaterial.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include <string>
#include <vector>
#include <set>
namespace VansGraphics
{
	class VansHierachuWindow : public VansBaseWindowComponent
	{
	private:
		bool m_TestButton = true;
		VansAnimationNode* m_SelectedAnimationNode = nullptr;
		int m_SelectedBone = -1;  // selected bone index in the hierarchy tree

	private:
		// ── Render Node tab ──
		void DrawRenderNodeList();
		void DrawRenderNodeDetail();
		void DrawNodeListWithGroups(const std::vector<VansRenderNode*>& nodes);
		void DrawTransformDetail(VansRenderNode& node);
		void DrawMaterialDetail(VansMaterial& material, int index = -1);
		void DrawPBRMaterialParameters(VansBasePBRParam& param, int id = 0);
		void DrawAtmosphereParameters(VansAtmospherePBRParam& param);

		// ── Objects tab ──
		void DrawObjectList();
		void DrawObjectDetail();

		// ── Animation tab ──
		void DrawAnimationList();
		void DrawAnimationNodeDetail();
		void DrawAnimBoneTree(const Skeleton& skeleton, const VansAnimationClip* clip, int boneIndex, float time);

		void ShowWindow(VansVKDevice& device) override;
	};
}