#include "VansHiZCullWindow.h"

#include "../VansEditorWindow.h"
#include "../../EngineAPILayer/Public/IEngineEditorAPI.h"

#include "imgui.h"

#include <algorithm>
#include <cstdint>

namespace VansGraphics
{
namespace
{
	void DrawCount(const char* label, std::uint32_t value)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(label);
		ImGui::TableSetColumnIndex(1);
		ImGui::Text("%u", value);
	}
}

void VansHiZCullWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!VansEditorWindow::m_HiZCullWindowOpen)
		return;

	if (!ImGui::Begin("HiZ Occlusion Culling", &VansEditorWindow::m_HiZCullWindowOpen))
	{
		ImGui::End();
		return;
	}

	const Vans::EditorAPI::MainCameraHiZCullDebugSnapshot snapshot =
		editorAPI.GetMainCameraHiZCullDebugSnapshot();
	if (!snapshot.available)
	{
		ImGui::TextDisabled("No runtime scene");
		ImGui::End();
		return;
	}

	ImGui::Checkbox("Visualize Culled Bounds", &VansEditorWindow::m_HiZCullDebugVisualization);
	ImGui::Separator();

	ImGui::Text("Enabled: %s", snapshot.enabled ? "Yes" : "No");
	ImGui::SameLine();
	ImGui::Text("History: %s", snapshot.historyValid ? "Valid" : "Invalid");

	if (ImGui::BeginTable("HiZCullStats", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
	{
		ImGui::TableSetupColumn("Metric");
		ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 96.0f);
		ImGui::TableHeadersRow();
		DrawCount("Candidates", snapshot.candidateCount);
		DrawCount("Frustum Visible", snapshot.frustumVisibleCount);
		DrawCount("HiZ Culled", snapshot.hizCulledCount);
		DrawCount("Forced Visible", snapshot.forcedVisibleCount);
		ImGui::EndTable();
	}

	ImGui::Spacing();
	ImGui::Text("Culled Nodes");
	const ImVec2 tableSize(0.0f, std::max(180.0f, ImGui::GetContentRegionAvail().y));
	if (ImGui::BeginTable("HiZCulledNodes", 3,
		ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY,
		tableSize))
	{
		ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 48.0f);
		ImGui::TableSetupColumn("Class", ImGuiTableColumnFlags_WidthFixed, 130.0f);
		ImGui::TableSetupColumn("Node");
		ImGui::TableHeadersRow();

		const std::size_t count = snapshot.culledNodes.size();
		for (std::size_t i = 0; i < count; ++i)
		{
			const auto& node = snapshot.culledNodes[i];
			ImGui::TableNextRow();
			ImGui::TableSetColumnIndex(0);
			ImGui::Text("%zu", i);
			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(node.cullClass.c_str());
			ImGui::TableSetColumnIndex(2);
			ImGui::TextUnformatted(node.name.c_str());
		}

		ImGui::EndTable();
	}

	ImGui::End();
}
}
