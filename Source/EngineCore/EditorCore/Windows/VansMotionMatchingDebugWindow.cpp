#include "VansMotionMatchingDebugWindow.h"

#include "../VansEditorWindow.h"

#include <imgui.h>

#include <cmath>
#include <string>

namespace VansGraphics
{
	bool VansMotionMatchingDebugWindow::s_SceneOverlayEnabled = false;
	bool VansMotionMatchingDebugWindow::s_ShowHistory = true;
	bool VansMotionMatchingDebugWindow::s_ShowFutureTrajectory = true;
	bool VansMotionMatchingDebugWindow::s_ShowFutureVelocities = false;
	bool VansMotionMatchingDebugWindow::s_ShowActualVelocity = true;
	bool VansMotionMatchingDebugWindow::s_ShowPlannedVelocity = false;
	bool VansMotionMatchingDebugWindow::s_ShowDesiredVelocity = true;
	bool VansMotionMatchingDebugWindow::s_ShowActiveClipVelocity = true;
	bool VansMotionMatchingDebugWindow::s_ShowSelectedCandidateVelocity = false;
	bool VansMotionMatchingDebugWindow::s_ShowAppliedRootMotionVelocity = true;
	bool VansMotionMatchingDebugWindow::s_ShowPivot = true;
	bool VansMotionMatchingDebugWindow::s_ShowLabels = true;

	void VansMotionMatchingDebugWindow::ShowWindow(
		Vans::EditorAPI::IEngineEditorAPI& editorAPI)
	{
		if (!VansEditorWindow::m_MotionMatchingDebugWindowOpen)
			return;

		if (!ImGui::Begin("Motion Matching Debug", &VansEditorWindow::m_MotionMatchingDebugWindowOpen))
		{
			ImGui::End();
			return;
		}

		ImGui::TextUnformatted("Scene View Overlay");
		ImGui::Checkbox("Enabled##MMOverlay", &s_SceneOverlayEnabled);
		ImGui::BeginDisabled(!s_SceneOverlayEnabled);
		ImGui::Checkbox("Motion History", &s_ShowHistory);
		ImGui::Checkbox("Predicted Trajectory", &s_ShowFutureTrajectory);
		ImGui::Checkbox("Predicted Velocities", &s_ShowFutureVelocities);
		ImGui::Checkbox("Actual Velocity", &s_ShowActualVelocity);
		ImGui::SameLine(); ImGui::TextColored(ImVec4(0.0f, 0.86f, 1.0f, 1.0f), "cyan");
		ImGui::Checkbox("Planned Velocity", &s_ShowPlannedVelocity);
		ImGui::SameLine(); ImGui::TextColored(ImVec4(0.47f, 0.71f, 1.0f, 1.0f), "blue");
		ImGui::Checkbox("Desired Velocity", &s_ShowDesiredVelocity);
		ImGui::SameLine(); ImGui::TextColored(ImVec4(0.31f, 1.0f, 0.39f, 1.0f), "green");
		ImGui::Checkbox("Active Clip Velocity", &s_ShowActiveClipVelocity);
		ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.31f, 0.86f, 1.0f), "magenta");
		ImGui::Checkbox("Selected Candidate Velocity", &s_ShowSelectedCandidateVelocity);
		ImGui::SameLine(); ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.20f, 1.0f), "orange");
		ImGui::Checkbox("Applied Root Motion Velocity", &s_ShowAppliedRootMotionVelocity);
		ImGui::SameLine(); ImGui::TextColored(ImVec4(0.78f, 0.46f, 1.0f, 1.0f), "purple");
		ImGui::Checkbox("Pivot Marker", &s_ShowPivot);
		ImGui::Checkbox("Labels", &s_ShowLabels);
		ImGui::EndDisabled();

		ImGui::Separator();
		const auto snapshot = editorAPI.GetMotionMatchingDebugSnapshot();
		if (!snapshot.available)
		{
			ImGui::TextDisabled("No active Motion Matching runtime.");
			ImGui::End();
			return;
		}

		for (std::size_t visualIndex = 0; visualIndex < snapshot.visuals.size(); ++visualIndex)
		{
			const auto& visual = snapshot.visuals[visualIndex];
			const std::string displayName = visual.runtimeNodeName.empty()
				? "Motion Matching"
				: visual.runtimeNodeName;
			const std::string title = displayName +
				(visual.activeClip.empty() ? "" : " / " + visual.activeClip) +
				"##MM" + std::to_string(visualIndex);
			if (!ImGui::TreeNodeEx(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
				continue;

			ImGui::Text("Runtime: %s%s", displayName.c_str(),
				visual.retargetSource ? " (Retarget Source)" : "");
			if (!visual.entityGuid.empty())
				ImGui::Text("Entity: %s", visual.entityGuid.c_str());
			ImGui::Text("Active: %s", visual.activeClip.c_str());
			ImGui::Text("Selected: %s", visual.selectedClip.c_str());
			std::string databaseLabel;
			for (const std::string& database : visual.activeDatabases)
			{
				if (!databaseLabel.empty())
					databaseLabel += ", ";
				databaseLabel += database;
			}
			ImGui::Text("Databases: %s", databaseLabel.empty() ? "none" : databaseLabel.c_str());
			ImGui::Text("Playback: %.3f  Switches: %d", visual.playbackRate, visual.switches);
			ImGui::Text("Input speed: %.3f  direction: %.1f deg",
				visual.querySpeed, visual.queryDirectionDegrees);
			ImGui::Text("Local input: (%.2f, %.2f)  input change: %.1f deg",
				visual.moveInputLocal.x, visual.moveInputLocal.z,
				visual.inputDirectionChangeDegrees);
			ImGui::Text("Motion change: %.1f deg  Facing error: %.1f deg",
				visual.directionChangeDegrees, visual.facingDeltaDegrees);
			ImGui::Text("Facing current: %.1f  desired: %.1f  view rate: %.1f deg/s",
				visual.currentFacingYawDegrees,
				visual.desiredFacingYawDegrees,
				visual.desiredFacingYawRateDegreesPerSecond);
			ImGui::Text("Reference yaw: %.1f  rate: %.1f deg/s  planned facing: %.1f",
				visual.movementReferenceYaw,
				visual.movementReferenceYawRate,
				visual.plannedFacingYaw);
			ImGui::Text("Pivot: %s  data: %s  urgent: %s  ETA: %.3f s",
				visual.pivotRequested ? "yes" : "no",
				visual.pivotDatabaseAvailable ? "authored" : "loop fallback",
				visual.urgentDirectionChange ? "yes" : "no",
				visual.hasPredictedPivot ? visual.predictedPivotTime : -1.0f);
			ImGui::Text("Root motion consumed: %.2f%s",
				visual.motionConsumptionRatio,
				visual.movementBlocked ? "  (blocked)" : "");
			ImGui::Text("Move state: requested %d  effective %d%s",
				visual.requestedMoveState,
				visual.effectiveMoveState,
				visual.directionalStateFallback ? "  (directional fallback)" : "");
			ImGui::Text("Facing turn: %s  state: %s  gate: %s",
				visual.facingTurnRequested ? "yes" : "no",
				visual.facingTurnState.empty() ? "unknown" : visual.facingTurnState.c_str(),
				visual.facingTurnGateReason.empty() ? "unknown" : visual.facingTurnGateReason.c_str());
			ImGui::Text("Steering: %s%s  target %.1f  authored %.1f",
				visual.steeringActive ? "active" : "idle",
				visual.steeringLimited ? " (limited)" : "",
				visual.steeringTargetFacingDeltaDegrees,
				visual.steeringAuthoredFacingDeltaDegrees);
			ImGui::Text("Steering correction: requested %.1f  frame %.2f  rate %.1f deg/s",
				visual.steeringRequestedCorrectionDegrees,
				visual.steeringAppliedCorrectionDegrees,
				visual.steeringAppliedYawRateDegreesPerSecond);
			ImGui::Text("Turn warp: %s%s  gate: %s  replan: %s",
				visual.turnWarpActive ? "active" : "idle",
				visual.turnWarpLimited ? " (limited)" : "",
				visual.turnWarpDisableReason.empty()
					? "unknown" : visual.turnWarpDisableReason.c_str(),
				visual.turnWarpNeedsReplan
					? visual.turnWarpReplanReason.c_str() : "no");
			ImGui::Text("Turn endpoint: target %.1f  authored %.1f  scale %.3f  residual %.1f",
				visual.turnWarpTargetDeltaDegrees,
				visual.turnWarpAuthoredRemainingYawDegrees,
				visual.turnWarpScaleRatio,
				visual.turnWarpResidualDegrees);
			ImGui::Text("Turn correction: frame %.2f  additive total %.2f  cost %.3f  profile %d  motion end %.3f",
				visual.turnWarpAppliedFrameCorrectionDegrees,
				visual.turnWarpAccumulatedAdditiveDegrees,
				visual.turnWarpEndpointCost,
				visual.turnWarpProfileIndex,
				visual.turnWarpMotionEndTimeSeconds);
			ImGui::Text("Root transition: %s  target %.1f  reconciled %.1f deg/s",
				visual.rootMotionReconciliationActive ? "active" : "idle",
				visual.rootMotionTargetYawRateDegreesPerSecond,
				visual.rootMotionReconciledYawRateDegreesPerSecond);
			ImGui::Text("Root yaw frame: authored %.2f  applied %.2f deg",
				visual.authoredRootYawDeltaDegrees,
				visual.appliedRootYawDeltaDegrees);

			auto vectorRow = [](const char* label, const Vans::EditorAPI::Vec3& value)
			{
				ImGui::Text("%s  (%.3f, %.3f, %.3f)  speed %.3f",
					label, value.x, value.y, value.z,
					std::sqrt(value.x * value.x + value.z * value.z));
			};
			vectorRow("Actual ", visual.actualVelocity);
			vectorRow("Planned", visual.plannedVelocity);
			vectorRow("Desired", visual.desiredVelocity);
			vectorRow("Active clip", visual.activeClipVelocity);
			vectorRow("Candidate  ", visual.selectedCandidateVelocity);
			vectorRow("Applied RM ", visual.appliedRootMotionVelocity);
			vectorRow("RM target  ", visual.rootMotionTargetVelocity);
			vectorRow("RM blended ", visual.rootMotionReconciledVelocity);

			ImGui::Text("Cost total %.3f | trajectory %.3f | pose %.3f | contact %.3f",
				visual.currentCost, visual.trajectoryCost, visual.poseCost, visual.contactCost);
			if (ImGui::TreeNode("Top Candidates"))
			{
				if (ImGui::BeginTable("MMCandidates", 7,
					ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
				{
					ImGui::TableSetupColumn("Clip");
					ImGui::TableSetupColumn("Time");
					ImGui::TableSetupColumn("Total");
					ImGui::TableSetupColumn("Trajectory");
					ImGui::TableSetupColumn("Pose");
					ImGui::TableSetupColumn("Contact");
					ImGui::TableSetupColumn("Turn End");
					ImGui::TableHeadersRow();
					for (const auto& candidate : visual.topCandidates)
					{
						ImGui::TableNextRow();
						ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(candidate.clipName.c_str());
						ImGui::TableSetColumnIndex(1); ImGui::Text("%.3f", candidate.time);
						ImGui::TableSetColumnIndex(2); ImGui::Text("%.3f", candidate.totalCost);
						ImGui::TableSetColumnIndex(3); ImGui::Text("%.3f", candidate.trajectoryCost);
						ImGui::TableSetColumnIndex(4); ImGui::Text("%.3f", candidate.poseCost);
						ImGui::TableSetColumnIndex(5); ImGui::Text("%.3f", candidate.contactCost);
						ImGui::TableSetColumnIndex(6); ImGui::Text("%.3f", candidate.turnEndpointCost);
					}
					ImGui::EndTable();
				}
				ImGui::TreePop();
			}
			ImGui::TreePop();
		}

		ImGui::End();
	}
}
