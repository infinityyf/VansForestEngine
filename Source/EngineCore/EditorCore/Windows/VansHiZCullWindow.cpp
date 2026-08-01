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
	void DrawCount(const char* label, std::uint32_t value, const char* description)
	{
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0);
		ImGui::TextUnformatted(label);
		if (description != nullptr && ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", description);
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
	ImGui::TextDisabled("DC scope: main-camera draw calls evaluated by HiZ only");
	if (ImGui::IsItemHovered())
	{
		ImGui::SetTooltip(
			"Excludes draw calls that do not pass through main-camera HiZ, such as shadows, terrain, vegetation, particles, and post-process passes.");
	}

	if (ImGui::BeginTable("HiZCullStats", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg))
	{
		ImGui::TableSetupColumn("Metric");
		ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 96.0f);
		ImGui::TableHeadersRow();
		DrawCount("Candidate Nodes", snapshot.candidateCount,
			"Nodes with usable static bounds that passed CPU frustum culling and belong to a HiZ-enabled render class.");
		DrawCount("Frustum Visible Nodes", snapshot.frustumVisibleCount,
			"Bounded nodes inside the main-camera frustum, including render classes for which HiZ may be disabled.");
		DrawCount("HiZ Culled Nodes", snapshot.hizCulledCount,
			"Candidate nodes reported occluded by the latest completed HiZ GPU readback.");
		DrawCount("Forced Visible Nodes", snapshot.forcedVisibleCount,
			"Nodes kept visible because bounds are unavailable or their bounds changed recently.");
		DrawCount("DC Before HiZ", snapshot.preCullDrawCallCount,
			"Draw calls that reached the main-camera HiZ visibility gate this frame.");
		DrawCount("DC Culled by HiZ", snapshot.culledDrawCallCount,
			"Draw calls rejected by the main-camera HiZ visibility result.");
		DrawCount("DC Actually Drawn", snapshot.drawnDrawCallCount,
			"HiZ-processed draw calls allowed through to render-node command recording.");
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
