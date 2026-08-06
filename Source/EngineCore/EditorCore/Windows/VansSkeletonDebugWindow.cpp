#include "VansSkeletonDebugWindow.h"

#include "../VansEditorSelection.h"
#include "../VansEditorWindow.h"

#include <imgui.h>

namespace VansGraphics
{
	void VansSkeletonDebugWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		if (!VansEditorWindow::m_SkeletonDebugWindowOpen)
			return;

		if (!ImGui::Begin("Skeleton Debug", &VansEditorWindow::m_SkeletonDebugWindowOpen))
		{
			ImGui::End();
			return;
		}

		ImGui::Checkbox("Scene View Overlay", &VansEditorWindow::m_SkeletonDebugGizmos);
		ImGui::Checkbox("Selected Entity Only", &VansEditorWindow::m_SkeletonDebugSelectedOnly);
		ImGui::Checkbox("Bone Names", &VansEditorWindow::m_SkeletonDebugShowNames);
		ImGui::Checkbox("Retarget Source", &VansEditorWindow::m_SkeletonDebugShowRetargetSource);

		const std::string selectedGuid = VansEditorWindow::m_SkeletonDebugSelectedOnly
			? Vans::VansEditorSelection::EntityGuid()
			: std::string();
		const auto snapshot = editorAPI.GetSkeletonDebugSnapshot(selectedGuid);

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
			if (rig.retargetSource && !VansEditorWindow::m_SkeletonDebugShowRetargetSource)
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
