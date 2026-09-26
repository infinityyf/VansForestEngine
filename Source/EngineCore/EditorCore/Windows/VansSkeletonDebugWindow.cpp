#include "VansSkeletonDebugWindow.h"

#include "../VansEditorSelectionService.h"
#include "../VansEditorDebugViewState.h"
#include "../VansEditorWindow.h"
#include "../../EngineAPILayer/Public/IAnimationEditorAPI.h"

#include <imgui.h>

namespace VansGraphics
{
	void VansSkeletonDebugWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		if (!VansEditorWindow::IsWindowOpen(VansEditorWindowId::SkeletonDebug))
			return;

		if (!ImGui::Begin("Skeleton Debug", VansEditorWindow::WindowOpenState(VansEditorWindowId::SkeletonDebug)))
		{
			ImGui::End();
			return;
		}

		ImGui::Checkbox("Scene View Overlay", &m_DebugViewState.skeletonDebugGizmos);
		ImGui::Checkbox("Selected Entity Only", &m_DebugViewState.skeletonDebugSelectedOnly);
		ImGui::Checkbox("Bone Names", &m_DebugViewState.skeletonDebugShowNames);
		ImGui::Checkbox("Retarget Source", &m_DebugViewState.skeletonDebugShowRetargetSource);

		const std::string selectedGuid = m_DebugViewState.skeletonDebugSelectedOnly
			? Vans::VansEditorSelectionService::Get().EntityGuid()
			: std::string();
		Vans::EditorAPI::IAnimationEditorAPI& animationAPI = editorAPI;
		const auto snapshot = animationAPI.GetSkeletonDebugSnapshot(selectedGuid);

		ImGui::Separator();
		if (!snapshot.available)
		{
			ImGui::TextDisabled("No runtime skeletons.");
			ImGui::End();
			return;
		}

		ImGui::Text("Rigs: %d", static_cast<int>(snapshot.rigs.size()));
		for (const auto& rig : snapshot.rigs)
		{
			if (rig.retargetSource && !m_DebugViewState.skeletonDebugShowRetargetSource)
				continue;
			if (ImGui::TreeNode(rig.nodeName.empty() ? "(unnamed rig)" : rig.nodeName.c_str()))
			{
				ImGui::Text("Role: %s", rig.role.empty() ? "(none)" : rig.role.c_str());
				ImGui::Text("State: %s", rig.currentState.empty() ? "(none)" : rig.currentState.c_str());
				ImGui::Text("Active: %s", rig.activeClip.empty() ? "(none)" : rig.activeClip.c_str());
				ImGui::Text("Selected: %s", rig.selectedClip.empty() ? "(none)" : rig.selectedClip.c_str());
				ImGui::Text("Time: %.3f  Normalized: %.3f", rig.currentTime, rig.normalizedTime);
				ImGui::Text("Playing: %s", rig.playing ? "true" : "false");
				ImGui::Text("Bones: %d", static_cast<int>(rig.bones.size()));
				ImGui::TreePop();
			}
		}

		ImGui::End();
	}
}
