#include "VansAIDebugWindow.h"

#include "../VansEditorWindow.h"
#include "../../EngineAPILayer/Public/IAIEditorAPI.h"

#include <imgui.h>

#include <string>

namespace VansGraphics
{
void VansAIDebugWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	Vans::EditorAPI::IAIEditorAPI& aiAPI = editorAPI;
	if (!VansEditorWindow::IsWindowOpen(VansEditorWindowId::AIDebug)) return;
	if (!ImGui::Begin("AI Debug", VansEditorWindow::WindowOpenState(VansEditorWindowId::AIDebug)))
	{
		ImGui::End();
		return;
	}

	const Vans::EditorAPI::VansAIDiagnosticsSnapshot snapshot =
		aiAPI.GetAIDiagnosticsSnapshot();
	if (!snapshot.available)
	{
		ImGui::TextDisabled("Runtime AI world is not available");
		ImGui::End();
		return;
	}

	ImGui::Text("Agents: %d%s", static_cast<int>(snapshot.totalAgents),
		snapshot.truncated ? " (view truncated)" : "");
	if (snapshot.agents.empty())
		ImGui::TextDisabled("No active AI agents");

	for (const Vans::EditorAPI::VansAIAgentDebugState& agent : snapshot.agents)
	{
		const std::string label = (agent.entityName.empty()
			? std::string("(unnamed agent)") : agent.entityName) + "##" + agent.entityGuid;
		if (!ImGui::TreeNode(label.c_str())) continue;

		ImGui::Text("Entity: %s", agent.entityGuid.empty() ? "(unknown)" : agent.entityGuid.c_str());
		ImGui::Text("Behavior: %s", agent.behaviorName.empty() ? "(none)" : agent.behaviorName.c_str());
		ImGui::Text("State: %s  Initialized: %s", agent.currentState.c_str(),
			agent.initialized ? "true" : "false");
		ImGui::Text("Target: %s", agent.targetName.empty() ? "(none)" : agent.targetName.c_str());
		if (!agent.targetGuid.empty()) ImGui::TextDisabled("%s", agent.targetGuid.c_str());
		ImGui::Text("Visibility: raw=%s retained=%s",
			agent.rawTargetVisible ? "true" : "false",
			agent.targetVisible ? "true" : "false");
		ImGui::Text("Movement: %s  commanded speed=%.3f",
			agent.movementBlocked ? "blocked" : "enabled", agent.commandedSpeed);
		ImGui::Text("Path: %s / %s  reason=%s",
			agent.pathStatus.c_str(), agent.pathFailure.c_str(),
			agent.lastPathRequestReason.c_str());
		ImGui::Text("Waypoints: %d  current=%d",
			static_cast<int>(agent.waypointCount), static_cast<int>(agent.waypointIndex));
		if (agent.hasPatrolDestination)
		{
			ImGui::Text("Patrol destination: %.2f, %.2f, %.2f",
				agent.patrolDestination.x, agent.patrolDestination.y,
				agent.patrolDestination.z);
		}
		if (!agent.pathDiagnostic.empty())
			ImGui::TextWrapped("Path detail: %s", agent.pathDiagnostic.c_str());
		if (agent.lineOfSightTested)
		{
			ImGui::Text("Line of sight: %s", agent.lineOfSightBlocked ? "blocked" : "clear");
			ImGui::Text("Ray: (%.2f, %.2f, %.2f) -> (%.2f, %.2f, %.2f)",
				agent.lineOfSightOrigin.x, agent.lineOfSightOrigin.y, agent.lineOfSightOrigin.z,
				agent.lineOfSightTarget.x, agent.lineOfSightTarget.y, agent.lineOfSightTarget.z);
			if (!agent.lineOfSightHit.empty())
				ImGui::Text("Hit: %s", agent.lineOfSightHit.c_str());
		}
		else
		{
			ImGui::TextDisabled("Line of sight ray not tested");
		}
		if (!agent.diagnostic.empty())
			ImGui::TextWrapped("Diagnostic: %s", agent.diagnostic.c_str());

		ImGui::SeparatorText("Blackboard");
		ImGui::Text("Entries: %d%s", static_cast<int>(agent.totalBlackboardEntries),
			agent.blackboardTruncated ? " (view truncated)" : "");
		if (ImGui::BeginTable("AIBlackboard", 4,
			ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_SizingStretchProp))
		{
			ImGui::TableSetupColumn("Name");
			ImGui::TableSetupColumn("Type");
			ImGui::TableSetupColumn("Value");
			ImGui::TableSetupColumn("Last Writer");
			ImGui::TableHeadersRow();
			for (const Vans::EditorAPI::VansAIBlackboardDebugEntry& entry : agent.blackboard)
			{
				ImGui::TableNextRow();
				ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(entry.name.c_str());
				ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(entry.type.c_str());
				ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(entry.value.c_str());
				ImGui::TableSetColumnIndex(3); ImGui::TextUnformatted(entry.lastWriter.c_str());
			}
			ImGui::EndTable();
		}
		ImGui::TreePop();
	}

	ImGui::End();
}
}
