#include "VansGAFDebuggerWindow.h"

#include "../VansEditorWindow.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <iterator>
#include <nlohmann/json.hpp>
#include <optional>

namespace VansGraphics
{
namespace
{
bool g_CombatOverlayEnabled = false;
bool g_ShowCombatSector = true;
bool g_ShowCombatWeaponPath = true;
bool g_ShowCombatHurtBodies = true;

void HelpMarker(const char* text)
{
	if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", text);
}
}

VansGAFDebuggerWindow::VansGAFDebuggerWindow()
{
	std::snprintf(m_TracePath.data(), m_TracePath.size(), "%s", "GAFTrace.gaftrace");
	std::snprintf(m_SimulationPayload.data(), m_SimulationPayload.size(), "%s", "{}");
}

void VansGAFDebuggerWindow::SetSimulationSourcePath(const std::string& sourcePath)
{
	std::snprintf(m_SimulationSourcePath.data(), m_SimulationSourcePath.size(),
		"%s", sourcePath.c_str());
	m_SimulationResult = {};
	m_SimulationStep = 0;
}

bool VansGAFDebuggerWindow::CombatOverlayEnabled() { return g_CombatOverlayEnabled; }
bool VansGAFDebuggerWindow::ShowCombatSector() { return g_ShowCombatSector; }
bool VansGAFDebuggerWindow::ShowCombatWeaponPath() { return g_ShowCombatWeaponPath; }
bool VansGAFDebuggerWindow::ShowCombatHurtBodies() { return g_ShowCombatHurtBodies; }

void VansGAFDebuggerWindow::ShowWindow(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	if (!VansEditorWindow::m_GAFDebuggerWindowOpen) return;

	ImGui::SetNextWindowSize(ImVec2(760.0f, 680.0f), ImGuiCond_FirstUseEver);
	if (!ImGui::Begin("GAF Debugger", &VansEditorWindow::m_GAFDebuggerWindowOpen))
	{
		ImGui::End();
		return;
	}

	if (ImGui::BeginTabBar("##gaf-debugger-tabs"))
	{
		if (ImGui::BeginTabItem("Runtime"))
		{
			DrawRuntimeDebugger(editorAPI);
			ImGui::EndTabItem();
		}
		if (ImGui::BeginTabItem("Simulator"))
		{
			ImGui::SeparatorText("Source Asset");
			ImGui::SetNextItemWidth(-1.0f);
			ImGui::InputTextWithHint("##simulation-source-path",
				"Action, Action Set, or Action Graph source path",
				m_SimulationSourcePath.data(), m_SimulationSourcePath.size());
			DrawSimulator(editorAPI);
			ImGui::EndTabItem();
		}
		ImGui::EndTabBar();
	}
	ImGui::End();
}

void VansGAFDebuggerWindow::DrawRuntimeDebugger(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	m_DebugSnapshot = editorAPI.GetGAFRuntimeDebugSnapshot();
	const Vans::EditorAPI::GAFCombatDebugSnapshot combatSnapshot =
		editorAPI.GetGAFCombatDebugSnapshot();
	ImGui::SeparatorText("Combat Window Visualization");
	ImGui::Checkbox("Scene Overlay", &g_CombatOverlayEnabled);
	ImGui::SameLine();
	ImGui::BeginDisabled(!g_CombatOverlayEnabled);
	ImGui::Checkbox("Sector", &g_ShowCombatSector);
	ImGui::SameLine();
	ImGui::Checkbox("Weapon Path", &g_ShowCombatWeaponPath);
	ImGui::SameLine();
	ImGui::Checkbox("Hurt Bodies", &g_ShowCombatHurtBodies);
	ImGui::EndDisabled();
	if (!combatSnapshot.available)
	{
		ImGui::TextDisabled("Combat service is not available in the current scene");
	}
	else
	{
		ImGui::TextDisabled("Windows %llu   Hurt bodies %llu",
			static_cast<unsigned long long>(combatSnapshot.windows.size()),
			static_cast<unsigned long long>(combatSnapshot.hurtBodies.size()));
		for (const auto& window : combatSnapshot.windows)
		{
			ImGui::BulletText("%s / %s  %s  hits %llu  range %.2f  angle +/-%.1f",
				window.owner.c_str(), window.window.c_str(),
				window.active ? "ACTIVE" : "inactive",
				static_cast<unsigned long long>(window.hitCount),
				window.range, window.halfAngleDegrees);
		}
	}
	Vans::EditorAPI::GAFDebugCommand queryBreakpoints;
	queryBreakpoints.kind = Vans::EditorAPI::GAFDebugCommandKind::Query;
	m_DebugBreakpoints = editorAPI.ControlGAFDebugger(queryBreakpoints).breakpoints;
	ImGui::SeparatorText("Breakpoints");
	const char* breakpointKinds[] = {
		"Action", "State", "Node", "Event", "Error", "Attribute", "Window"
	};
	ImGui::SetNextItemWidth(135.0f);
	if (ImGui::Combo("##breakpoint-kind", &m_DebugBreakpointKind,
		"Action\0State\0Node\0Event\0Error\0Attribute\0Window\0"))
		m_DebugBreakpointExpression.fill('\0');
	ImGui::SameLine();
	if (m_DebugBreakpointKind == static_cast<int>(Vans::EditorAPI::GAFDebugBreakpointKind::State))
	{
		const char* states[] = { "Created", "Queued", "Resolving", "BuildingContext",
			"Validating", "Preparing", "Committing", "Committed", "Running", "Waiting",
			"Transitioning", "Ending", "Ended" };
		int selected = 0;
		for (int index = 0; index < static_cast<int>(std::size(states)); ++index)
			if (m_DebugBreakpointExpression.data() == std::string(states[index])) selected = index;
		ImGui::SetNextItemWidth((std::max)(160.0f, ImGui::GetContentRegionAvail().x - 280.0f));
		if (ImGui::Combo("##breakpoint-expression", &selected,
			"Created\0Queued\0Resolving\0BuildingContext\0Validating\0Preparing\0Committing\0"
			"Committed\0Running\0Waiting\0Transitioning\0Ending\0Ended\0"))
			std::snprintf(m_DebugBreakpointExpression.data(), m_DebugBreakpointExpression.size(),
				"%s", states[selected]);
		if (m_DebugBreakpointExpression[0] == '\0')
			std::snprintf(m_DebugBreakpointExpression.data(), m_DebugBreakpointExpression.size(),
				"%s", states[selected]);
	}
	else if (m_DebugBreakpointKind == static_cast<int>(Vans::EditorAPI::GAFDebugBreakpointKind::Error))
	{
		const char* errors[] = { "InvalidDefinition", "Rejected", "Dependency", "Execution",
			"Timeout", "Cancelled", "Resource", "Budget", "Internal" };
		int selected = 0;
		for (int index = 0; index < static_cast<int>(std::size(errors)); ++index)
			if (m_DebugBreakpointExpression.data() == std::string(errors[index])) selected = index;
		ImGui::SetNextItemWidth((std::max)(160.0f, ImGui::GetContentRegionAvail().x - 280.0f));
		if (ImGui::Combo("##breakpoint-expression", &selected,
			"DefinitionMissing\0DefinitionInvalid\0NotGranted\0RequirementsFailed\0TargetInvalid\0"
			"CostUnavailable\0CooldownActive\0ConcurrencyBlocked\0ServiceMissing\0"
			"CommitFailed\0ExecutionFailed\0Cancelled\0TimedOut\0InternalInvariant\0InvalidState\0"
			"ConcurrencyRejected\0ConcurrencyQueueExpired\0BudgetExceeded\0"))
			std::snprintf(m_DebugBreakpointExpression.data(), m_DebugBreakpointExpression.size(),
				"%s", errors[selected]);
		if (m_DebugBreakpointExpression[0] == '\0')
			std::snprintf(m_DebugBreakpointExpression.data(), m_DebugBreakpointExpression.size(),
				"%s", errors[selected]);
	}
	else
	{
		const char* hints[] = { "Action id", "State", "Node name", "Event name", "ErrorCode",
			"Attribute id", "Window name" };
		ImGui::SetNextItemWidth((std::max)(160.0f, ImGui::GetContentRegionAvail().x - 280.0f));
		ImGui::InputTextWithHint("##breakpoint-expression", hints[m_DebugBreakpointKind],
			m_DebugBreakpointExpression.data(), m_DebugBreakpointExpression.size());
	}
	if (m_DebugBreakpointKind == static_cast<int>(Vans::EditorAPI::GAFDebugBreakpointKind::Attribute))
	{
		ImGui::SetNextItemWidth(120.0f);
		ImGui::Combo("Compare", &m_DebugBreakpointComparison,
			"Changed\0Equal\0Less\0Less or Equal\0Greater\0Greater or Equal\0");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		ImGui::InputDouble("Value", &m_DebugBreakpointValue, 0.0, 0.0, "%.6g");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100.0f);
		ImGui::InputDouble("Epsilon", &m_DebugBreakpointEpsilon, 0.0, 0.0, "%.6g");
	}
	if (ImGui::Button("Add Breakpoint"))
	{
		Vans::EditorAPI::GAFDebugCommand command;
		command.kind = Vans::EditorAPI::GAFDebugCommandKind::AddBreakpoint;
		command.breakpoint.kind =
			static_cast<Vans::EditorAPI::GAFDebugBreakpointKind>(m_DebugBreakpointKind);
		command.breakpoint.expression = m_DebugBreakpointExpression.data();
		command.breakpoint.comparison =
			static_cast<Vans::EditorAPI::GAFDebugBreakpointComparison>(m_DebugBreakpointComparison);
		command.breakpoint.value = m_DebugBreakpointValue;
		command.breakpoint.epsilon = m_DebugBreakpointEpsilon;
		const auto operation = editorAPI.ControlGAFDebugger(command);
		m_DebugBreakpoints = operation.breakpoints;
		m_DebugMessage = operation.message;
	}
	ImGui::SameLine();
	Vans::EditorAPI::GAFDebugCommand playback;
	if (editorAPI.GetPlayState() == Vans::EditorAPI::EnginePlayState::Pause)
	{
		if (ImGui::Button("Resume"))
		{
			playback.kind = Vans::EditorAPI::GAFDebugCommandKind::Resume;
			editorAPI.ControlGAFDebugger(playback);
		}
		ImGui::SameLine();
		if (ImGui::Button("Step"))
		{
			playback.kind = Vans::EditorAPI::GAFDebugCommandKind::Step;
			const auto operation = editorAPI.ControlGAFDebugger(playback);
			m_DebugMessage = operation.message;
		}
	}
	else if (ImGui::Button("Pause"))
	{
		playback.kind = Vans::EditorAPI::GAFDebugCommandKind::Pause;
		editorAPI.ControlGAFDebugger(playback);
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear Breakpoints"))
	{
		playback.kind = Vans::EditorAPI::GAFDebugCommandKind::ClearBreakpoints;
		m_DebugBreakpoints = editorAPI.ControlGAFDebugger(playback).breakpoints;
	}
	for (std::size_t index = 0; index < m_DebugBreakpoints.size(); ++index)
	{
		auto& breakpoint = m_DebugBreakpoints[index];
		ImGui::PushID(static_cast<int>(breakpoint.id));
		bool enabled = breakpoint.enabled;
		if (ImGui::Checkbox("##enabled", &enabled))
		{
			Vans::EditorAPI::GAFDebugCommand command;
			command.kind = Vans::EditorAPI::GAFDebugCommandKind::SetBreakpointEnabled;
			command.breakpointId = breakpoint.id;
			command.enabled = enabled;
			m_DebugBreakpoints = editorAPI.ControlGAFDebugger(command).breakpoints;
		}
		ImGui::SameLine();
		ImGui::Text("#%llu %s: %s", static_cast<unsigned long long>(breakpoint.id),
			breakpointKinds[static_cast<int>(breakpoint.kind)], breakpoint.expression.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("x"))
		{
			Vans::EditorAPI::GAFDebugCommand command;
			command.kind = Vans::EditorAPI::GAFDebugCommandKind::RemoveBreakpoint;
			command.breakpointId = breakpoint.id;
			m_DebugBreakpoints = editorAPI.ControlGAFDebugger(command).breakpoints;
			ImGui::PopID();
			break;
		}
		HelpMarker("Remove breakpoint");
		ImGui::PopID();
	}
	for (const std::string& hit : m_DebugSnapshot.breakpointHits)
		ImGui::TextColored(ImVec4(0.95f, 0.55f, 0.25f, 1.0f), "%s", hit.c_str());
	ImGui::SeparatorText("Trace");
	ImGui::SetNextItemWidth((std::max)(240.0f, ImGui::GetContentRegionAvail().x - 330.0f));
	ImGui::InputText("##trace-path", m_TracePath.data(), m_TracePath.size());
	ImGui::SameLine();
	if (!m_DebugSnapshot.recording)
	{
		if (ImGui::Button("Record"))
		{
			Vans::EditorAPI::GAFTraceCommand command;
			command.kind = Vans::EditorAPI::GAFTraceCommandKind::StartRecording;
			command.path = m_TracePath.data();
			const auto result = editorAPI.ControlGAFTrace(command);
			m_DebugMessage = result.message;
			m_DebugSnapshot = result.snapshot;
		}
	}
	else
	{
		if (ImGui::Button("Stop & Save"))
		{
			Vans::EditorAPI::GAFTraceCommand command;
			command.kind = Vans::EditorAPI::GAFTraceCommandKind::StopAndSave;
			command.path = m_TracePath.data();
			const auto result = editorAPI.ControlGAFTrace(command);
			m_DebugMessage = result.message;
			m_DebugSnapshot = result.snapshot;
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
		{
			Vans::EditorAPI::GAFTraceCommand command;
			command.kind = Vans::EditorAPI::GAFTraceCommandKind::CancelRecording;
			m_DebugSnapshot = editorAPI.ControlGAFTrace(command).snapshot;
		}
	}
	ImGui::SameLine();
	if (!m_DebugSnapshot.replay)
	{
		if (ImGui::Button("Open Trace"))
		{
			Vans::EditorAPI::GAFTraceCommand command;
			command.kind = Vans::EditorAPI::GAFTraceCommandKind::OpenReplay;
			command.path = m_TracePath.data();
			const auto result = editorAPI.ControlGAFTrace(command);
			m_DebugMessage = result.message;
			m_DebugSnapshot = result.snapshot;
		}
	}
	else
	{
		const auto step = [&](std::int32_t direction)
		{
			Vans::EditorAPI::GAFTraceCommand command;
			command.kind = Vans::EditorAPI::GAFTraceCommandKind::StepReplay;
			command.step = direction;
			const auto result = editorAPI.ControlGAFTrace(command);
			m_DebugMessage = result.message;
			m_DebugSnapshot = result.snapshot;
		};
		if (ImGui::Button("<")) step(-1);
		HelpMarker("Previous trace frame");
		ImGui::SameLine();
		if (ImGui::Button(">")) step(1);
		HelpMarker("Next trace frame");
		ImGui::SameLine();
		if (ImGui::Button("Close Trace"))
		{
			Vans::EditorAPI::GAFTraceCommand command;
			command.kind = Vans::EditorAPI::GAFTraceCommandKind::CloseReplay;
			m_DebugSnapshot = editorAPI.ControlGAFTrace(command).snapshot;
		}
	}
	if (!m_DebugMessage.empty())
		ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.40f, 1.0f), "%s", m_DebugMessage.c_str());
	if (!m_DebugSnapshot.available)
	{
		ImGui::TextDisabled("%s", m_DebugSnapshot.message.c_str());
		return;
	}
	ImGui::TextDisabled("Frame %llu   Time %.3f   Manifest %016llx%s",
		static_cast<unsigned long long>(m_DebugSnapshot.frame), m_DebugSnapshot.timeSeconds,
		static_cast<unsigned long long>(m_DebugSnapshot.contentManifestHash),
		m_DebugSnapshot.replay ? "   Replay" : "");
	if (m_DebugSnapshot.replay)
	{
		std::uint64_t frame = m_DebugSnapshot.replayFrame;
		const std::uint64_t minimum = 0;
		const std::uint64_t maximum = m_DebugSnapshot.replayFrameCount > 0
			? m_DebugSnapshot.replayFrameCount - 1 : 0;
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::SliderScalar("##replay-frame", ImGuiDataType_U64, &frame,
			&minimum, &maximum))
		{
			Vans::EditorAPI::GAFTraceCommand command;
			command.kind = Vans::EditorAPI::GAFTraceCommandKind::SeekReplay;
			command.frame = static_cast<std::size_t>(frame);
			m_DebugSnapshot = editorAPI.ControlGAFTrace(command).snapshot;
		}
	}
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##debug-filter", "Filter owner, action, state, node or event",
		m_DebugFilter.data(), m_DebugFilter.size());
	std::string filter = m_DebugFilter.data();
	std::transform(filter.begin(), filter.end(), filter.begin(),
		[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
	const auto matches = [&](const std::string& value)
	{
		if (filter.empty()) return true;
		std::string normalized = value;
		std::transform(normalized.begin(), normalized.end(), normalized.begin(),
			[](unsigned char character) { return static_cast<char>(std::tolower(character)); });
		return normalized.find(filter) != std::string::npos;
	};
	for (const auto& host : m_DebugSnapshot.hosts)
	{
		bool hostMatches = matches(host.owner);
		for (const auto& action : host.actions)
		{
			hostMatches = hostMatches || matches(action.actionId) || matches(action.state);
			for (const auto& node : action.activeNodes) hostMatches = hostMatches || matches(node);
			for (const auto& event : action.recentEvents) hostMatches = hostMatches || matches(event);
		}
		if (!hostMatches) continue;
		const std::string hostLabel = "Owner " + host.owner + "##" + host.owner;
		if (!ImGui::CollapsingHeader(hostLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) continue;
		ImGui::Text("%s   Cues %zu", host.enabled ? "Enabled" : "Disabled", host.activeCueCount);
		const auto drawValues = [](const char* label,
			const std::vector<Vans::EditorAPI::GAFDebugNamedValue>& values)
		{
			if (values.empty() || !ImGui::TreeNode(label)) return;
			if (ImGui::BeginTable(label, 2, ImGuiTableFlags_BordersInnerH |
				ImGuiTableFlags_SizingStretchProp))
			{
				for (const auto& value : values)
				{
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(value.name.c_str());
					ImGui::TableSetColumnIndex(1); ImGui::TextWrapped("%s", value.value.c_str());
				}
				ImGui::EndTable();
			}
			ImGui::TreePop();
		};
		drawValues("Tags", host.tags);
		drawValues("Attributes", host.attributes);
		drawValues("Effects", host.effects);
		drawValues("Granted Actions", host.grants);
		for (const auto& action : host.actions)
		{
			const std::string actionLabel = action.actionId + "  [" + action.state + "]##" + action.handle;
			if (!ImGui::TreeNodeEx(actionLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) continue;
			ImGui::Text("Handle %s   Time %.3f   Correlation %s",
				action.handle.c_str(), action.elapsedSeconds, action.correlationId.c_str());
			ImGui::Text("Executor %s   End %s   Error %s", action.executor.c_str(),
				action.endReason.c_str(), action.error.c_str());
			drawValues("Variables", action.variables);
			if (!action.activeNodes.empty())
				for (const auto& node : action.activeNodes) ImGui::BulletText("Active: %s", node.c_str());
			if (!action.waitingNodes.empty())
				for (const auto& node : action.waitingNodes) ImGui::BulletText("Waiting: %s", node.c_str());
			if (!action.tasks.empty() && ImGui::TreeNode("Tasks"))
			{
				for (const auto& task : action.tasks)
					ImGui::BulletText("%s [%s] %.3f / %.3f", task.name.c_str(), task.state.c_str(),
						task.elapsedSeconds, task.timeoutSeconds);
				ImGui::TreePop();
			}
			if (!action.resources.empty() && ImGui::TreeNode("Resource Ledger"))
			{
				for (const auto& resource : action.resources)
					ImGui::BulletText("%s %s", resource.type.c_str(), resource.name.c_str());
				ImGui::TreePop();
			}
			if (!action.recentEvents.empty() && ImGui::TreeNode("Events"))
			{
				for (const auto& event : action.recentEvents) ImGui::BulletText("%s", event.c_str());
				ImGui::TreePop();
			}
			if (!action.trace.empty() && ImGui::TreeNode("Trace"))
			{
				for (const auto& entry : action.trace) ImGui::TextWrapped("%s", entry.c_str());
				ImGui::TreePop();
			}
			ImGui::TreePop();
		}
	}
}

void VansGAFDebuggerWindow::DrawSimulator(
	Vans::EditorAPI::IEngineEditorAPI& editorAPI)
{
	using namespace Vans::EditorAPI;
	ImGui::SeparatorText("Action");
	ImGui::SetNextItemWidth(-1.0f);
	ImGui::InputTextWithHint("##simulation-action", "Current Action, GUID, or asset path",
		m_SimulationAction.data(), m_SimulationAction.size());
	int mode = m_SimulationRequest.mode == GAFSimulationMode::Execute ? 1 : 0;
	ImGui::SetNextItemWidth(180.0f);
	if (ImGui::Combo("Mode", &mode, "Can Activate\0Execute\0"))
		m_SimulationRequest.mode = mode == 0
			? GAFSimulationMode::CanActivate : GAFSimulationMode::Execute;
	if (ImGui::BeginTable("##simulation-entities", 4,
		ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV))
	{
		ImGui::TableSetupColumn("Owner index");
		ImGui::TableSetupColumn("Owner generation");
		ImGui::TableSetupColumn("Instigator index");
		ImGui::TableSetupColumn("Instigator generation");
		ImGui::TableHeadersRow();
		ImGui::TableNextRow();
		ImGui::TableSetColumnIndex(0); ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputScalar("##owner-index", ImGuiDataType_U32, &m_SimulationRequest.owner.index);
		ImGui::TableSetColumnIndex(1); ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputScalar("##owner-generation", ImGuiDataType_U32,
			&m_SimulationRequest.owner.generation);
		ImGui::TableSetColumnIndex(2); ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputScalar("##instigator-index", ImGuiDataType_U32,
			&m_SimulationRequest.instigator.index);
		ImGui::TableSetColumnIndex(3); ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputScalar("##instigator-generation", ImGuiDataType_U32,
			&m_SimulationRequest.instigator.generation);
		ImGui::EndTable();
	}

	ImGui::SeparatorText("Target");
	int targetKind = static_cast<int>(m_SimulationRequest.targetKind);
	ImGui::SetNextItemWidth(180.0f);
	if (ImGui::Combo("Type", &targetKind, "None\0Entity\0Location\0Ray\0Entity Set\0"))
		m_SimulationRequest.targetKind = static_cast<GAFSimulationTargetKind>(targetKind);
	if (m_SimulationRequest.targetKind == GAFSimulationTargetKind::Entity)
	{
		ImGui::SetNextItemWidth(150.0f);
		ImGui::InputScalar("Entity index", ImGuiDataType_U32,
			&m_SimulationRequest.primaryTarget.index);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(150.0f);
		ImGui::InputScalar("Generation", ImGuiDataType_U32,
			&m_SimulationRequest.primaryTarget.generation);
	}
	else if (m_SimulationRequest.targetKind == GAFSimulationTargetKind::Location ||
		m_SimulationRequest.targetKind == GAFSimulationTargetKind::Ray)
	{
		double origin[3]{ m_SimulationRequest.targetX,
			m_SimulationRequest.targetY, m_SimulationRequest.targetZ };
		if (ImGui::InputScalarN("Origin", ImGuiDataType_Double, origin, 3))
		{
			m_SimulationRequest.targetX = origin[0];
			m_SimulationRequest.targetY = origin[1];
			m_SimulationRequest.targetZ = origin[2];
		}
		if (m_SimulationRequest.targetKind == GAFSimulationTargetKind::Ray)
		{
			double direction[3]{ m_SimulationRequest.rayDirectionX,
				m_SimulationRequest.rayDirectionY, m_SimulationRequest.rayDirectionZ };
			if (ImGui::InputScalarN("Direction", ImGuiDataType_Double, direction, 3))
			{
				m_SimulationRequest.rayDirectionX = direction[0];
				m_SimulationRequest.rayDirectionY = direction[1];
				m_SimulationRequest.rayDirectionZ = direction[2];
			}
			ImGui::InputDouble("Length", &m_SimulationRequest.rayLength, 1.0, 10.0, "%.3f");
		}
	}
	else if (m_SimulationRequest.targetKind == GAFSimulationTargetKind::EntitySet)
	{
		std::optional<std::size_t> remove;
		for (std::size_t index = 0; index < m_SimulationRequest.targetEntities.size(); ++index)
		{
			ImGui::PushID(static_cast<int>(index));
			ImGui::SetNextItemWidth(130.0f);
			ImGui::InputScalar("##entity-index", ImGuiDataType_U32,
				&m_SimulationRequest.targetEntities[index].index);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(130.0f);
			ImGui::InputScalar("##entity-generation", ImGuiDataType_U32,
				&m_SimulationRequest.targetEntities[index].generation);
			ImGui::SameLine();
			if (ImGui::SmallButton("x")) remove = index;
			HelpMarker("Remove entity");
			ImGui::PopID();
		}
		if (remove) m_SimulationRequest.targetEntities.erase(
			m_SimulationRequest.targetEntities.begin() + static_cast<std::ptrdiff_t>(*remove));
		if (ImGui::Button("Add Entity"))
			m_SimulationRequest.targetEntities.push_back({
				static_cast<std::uint32_t>(m_SimulationRequest.targetEntities.size() + 2), 1 });
	}

	ImGui::SeparatorText("Initial State");
	for (std::size_t index = 0; index < m_SimulationRequest.initializers.size(); ++index)
	{
		ImGui::PushID(static_cast<int>(index));
		ImGui::TextWrapped("%s %s", m_SimulationRequest.initializers[index].type.c_str(),
			m_SimulationRequest.initializers[index].inputsJson.c_str());
		ImGui::SameLine();
		if (ImGui::SmallButton("x"))
		{
			m_SimulationRequest.initializers.erase(
				m_SimulationRequest.initializers.begin() + static_cast<std::ptrdiff_t>(index));
			ImGui::PopID();
			break;
		}
		HelpMarker("Remove initializer");
		ImGui::PopID();
	}
	ImGui::SetNextItemWidth((std::max)(180.0f, ImGui::GetContentRegionAvail().x - 190.0f));
	ImGui::InputTextWithHint("##new-simulation-tag", "Gameplay Tag",
		m_SimulationNewTag.data(), m_SimulationNewTag.size());
	ImGui::SameLine();
	ImGui::SetNextItemWidth(70.0f);
	ImGui::InputScalar("##new-tag-count", ImGuiDataType_U32, &m_SimulationNewTagCount);
	ImGui::SameLine();
	if (ImGui::Button("Add Tag") && m_SimulationNewTag[0] != '\0' && m_SimulationNewTagCount > 0)
	{
		nlohmann::ordered_json inputs{
			{ "tag", m_SimulationNewTag.data() }, { "count", m_SimulationNewTagCount } };
		m_SimulationRequest.initializers.push_back({
			"Gameplay.Tags.Initialize", inputs.dump() });
		m_SimulationNewTag.fill('\0');
	}
	ImGui::SetNextItemWidth((std::max)(180.0f, ImGui::GetContentRegionAvail().x - 240.0f));
	ImGui::InputTextWithHint("##new-simulation-attribute", "Attribute",
		m_SimulationNewAttribute.data(), m_SimulationNewAttribute.size());
	ImGui::SameLine();
	ImGui::SetNextItemWidth(100.0f);
	ImGui::InputDouble("##new-attribute-value", &m_SimulationNewAttributeValue,
		0.0, 0.0, "%.6g");
	ImGui::SameLine();
	if (ImGui::Button("Add Attribute") && m_SimulationNewAttribute[0] != '\0')
	{
		nlohmann::ordered_json inputs{
			{ "attribute", m_SimulationNewAttribute.data() },
			{ "value", m_SimulationNewAttributeValue } };
		m_SimulationRequest.initializers.push_back({
			"Gameplay.Attributes.Initialize", inputs.dump() });
		m_SimulationNewAttribute.fill('\0');
	}
	ImGui::InputTextMultiline("Payload", m_SimulationPayload.data(), m_SimulationPayload.size(),
		ImVec2(-1.0f, 90.0f), ImGuiInputTextFlags_AllowTabInput);

	if (m_SimulationRequest.mode == GAFSimulationMode::Execute)
	{
		ImGui::SetNextItemWidth(130.0f);
		ImGui::InputScalar("Ticks", ImGuiDataType_U32, &m_SimulationRequest.tickCount);
		ImGui::SameLine();
		ImGui::SetNextItemWidth(150.0f);
		ImGui::InputDouble("Delta seconds", &m_SimulationRequest.deltaSeconds,
			0.001, 0.01, "%.6f");
	}
	if (ImGui::Button("Run Simulation"))
	{
		m_SimulationRequest.sourcePath = m_SimulationSourcePath.data();
		m_SimulationRequest.actionReference = m_SimulationAction.data();
		m_SimulationRequest.payloadJson = m_SimulationPayload.data();
		m_SimulationResult = editorAPI.SimulateGAFAction(m_SimulationRequest);
		m_SimulationStep = 0;
	}

	if (!m_SimulationResult.success)
	{
		if (!m_SimulationResult.message.empty())
			ImGui::TextColored(ImVec4(0.95f, 0.30f, 0.28f, 1.0f), "%s",
				m_SimulationResult.message.c_str());
		return;
	}
	ImGui::SameLine();
	const ImVec4 outcomeColor = m_SimulationResult.canActivate
		? ImVec4(0.32f, 0.78f, 0.48f, 1.0f) : ImVec4(0.95f, 0.45f, 0.30f, 1.0f);
	ImGui::TextColored(outcomeColor, "%s  %s  %s", m_SimulationResult.actionReference.c_str(),
		m_SimulationResult.disposition.c_str(), m_SimulationResult.error.c_str());
	if (!m_SimulationResult.message.empty())
		ImGui::TextWrapped("%s", m_SimulationResult.message.c_str());
	if (!m_SimulationResult.steps.empty())
	{
		std::uint64_t step = static_cast<std::uint64_t>((std::min)(
			m_SimulationStep, m_SimulationResult.steps.size() - 1));
		const std::uint64_t minimum = 0;
		const std::uint64_t maximum = m_SimulationResult.steps.size() - 1;
		ImGui::SetNextItemWidth(-1.0f);
		if (ImGui::SliderScalar("Step", ImGuiDataType_U64, &step, &minimum, &maximum))
			m_SimulationStep = static_cast<std::size_t>(step);
		const auto& snapshot = m_SimulationResult.steps[m_SimulationStep];
		for (const auto& host : snapshot.hosts)
		{
			ImGui::Text("Owner %s", host.owner.c_str());
			for (const auto& attribute : host.attributes)
				ImGui::BulletText("%s = %s", attribute.name.c_str(), attribute.value.c_str());
			for (const auto& action : host.actions)
			{
				ImGui::SeparatorText(action.actionId.c_str());
				ImGui::Text("%s  %.3fs  %s", action.state.c_str(),
					action.elapsedSeconds, action.error.c_str());
				for (const auto& target : action.targets) ImGui::BulletText("Target: %s", target.c_str());
				for (const auto& node : action.activeNodes) ImGui::BulletText("Active: %s", node.c_str());
				for (const auto& node : action.waitingNodes) ImGui::BulletText("Waiting: %s", node.c_str());
				if (ImGui::TreeNode("Variables"))
				{
					for (const auto& variable : action.variables)
						ImGui::BulletText("%s = %s", variable.name.c_str(), variable.value.c_str());
					ImGui::TreePop();
				}
				if (ImGui::TreeNode("Resources"))
				{
					for (const auto& resource : action.resources)
						ImGui::BulletText("%s %s", resource.type.c_str(), resource.name.c_str());
					ImGui::TreePop();
				}
				if (ImGui::TreeNode("Trace"))
				{
					for (const auto& entry : action.trace) ImGui::TextWrapped("%s", entry.c_str());
					ImGui::TreePop();
				}
			}
		}
	}
	if (ImGui::TreeNode("Service Activity"))
	{
		for (const auto& service : m_SimulationResult.serviceActivity)
			ImGui::BulletText("%s  %s", service.name.c_str(), service.value.c_str());
		ImGui::TreePop();
	}
}
}
