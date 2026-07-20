#include "VansAnimGraphEditorWindow.h"
#include "../../AnimationCore/VansAnimationController.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include "../../AnimationCore/VansAnimatorIO.h"
#include "../../AnimationCore/VansAnimGraph.h"
#include "../../AnimationCore/MotionMatching/VansMotionMatching.h"
#include "../../Util/VansLog.h"
#include <imgui.h>
#include <../../GLM/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cstring>
using json = nlohmann::json;
using namespace VansGraphics;
namespace ne = ax::NodeEditor;
namespace VansGraphics
{
	struct AnimGraphNodeLayout
	{
		std::string stateName;
		ImVec2 position = ImVec2(0, 0);
	};
	struct AnimGraphEditState
	{
		std::string name;
		std::vector<AnimatorParameter> parameters;
		std::vector<AnimatorClipRef> clipRefs;
		std::vector<AnimGraphNodeLayout> nodeLayouts;
		bool isDirty = false;
		bool needsInitialLayout = false;
		int selectedNodeId = -1;
		int selectedLinkId = -1;
	};
}
// ============================================================================
// ============================================================================
VansAnimGraphEditorWindow::VansAnimGraphEditorWindow()
	: m_EditState(std::make_unique<AnimGraphEditState>())
{
}
VansAnimGraphEditorWindow::~VansAnimGraphEditorWindow()
{
	if (m_NodeEditorCtx)
	{
		ne::DestroyEditor(m_NodeEditorCtx);
		m_NodeEditorCtx = nullptr;
	}
}
// ============================================================================
//  Open / Close
// ============================================================================
void VansAnimGraphEditorWindow::Open(VansAnimationController* controller,
                                      VansAnimationNode* node)
{
	m_TargetController = controller;
	m_TargetNode       = node;
	m_IsOpen           = true;
	m_AnimatorFilePath = node->GetAnimatorFilePath();
	// 璁剧疆 AnimGraph
	m_TargetGraph = controller->GetGraph();
	if (m_NodeEditorCtx)
		ne::DestroyEditor(m_NodeEditorCtx);
	ne::Config config;
	config.SettingsFile          = nullptr;
	config.NavigateButtonIndex   = 2;
	config.ContextMenuButtonIndex = 1;       // 鍙抽敭鑿滃崟
	m_NodeEditorCtx = ne::CreateEditor(&config);
	m_EditState->needsInitialLayout = true;
	m_EditState->isDirty            = false;
	m_NavigationStack.clear();
}
void VansAnimGraphEditorWindow::Close()
{
    // 关闭时将当前节点位置写回 graph 节点
	if (m_NodeEditorCtx)
	{
		ne::SetCurrentEditor(m_NodeEditorCtx);
		ReadNodePositions();
		ne::SetCurrentEditor(nullptr);
		ne::DestroyEditor(m_NodeEditorCtx);
		m_NodeEditorCtx = nullptr;
	}
	m_TargetController = nullptr;
	m_TargetNode       = nullptr;
	m_TargetGraph      = nullptr;
	m_IsOpen           = false;
}
// ============================================================================
// ============================================================================
void VansAnimGraphEditorWindow::ShowWindow(Vans::EditorAPI::IEngineEditorAPI&)
{
	if (!m_IsOpen) return;
	if (!m_TargetController)
	{
		m_IsOpen = false;
		return;
	}
	ImGui::SetNextWindowSize(ImVec2(1200, 700), ImGuiCond_FirstUseEver);
	char title[256];
	snprintf(title, sizeof(title), "Animation Graph Editor - %s%s###AnimGraphEditor",
	         m_EditState->name.c_str(),
	         m_EditState->isDirty ? " *" : "");
	if (!ImGui::Begin(title, &m_IsOpen, ImGuiWindowFlags_MenuBar))
	{
		ImGui::End();
		return;
	}
	// ============================================================================
	// ?????????????????????
	if (!m_IsOpen && m_EditState->isDirty)
	{
		m_IsOpen = true;  // ??????
		ImGui::OpenPopup("UnsavedChanges");
	}
	if (ImGui::BeginPopupModal("UnsavedChanges", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("You have unsaved changes. Save before closing?");
		if (ImGui::Button("Save"))   { Save(); m_IsOpen = false; ImGui::CloseCurrentPopup(); }
		ImGui::SameLine();
		if (ImGui::Button("Discard")){ m_IsOpen = false; ImGui::CloseCurrentPopup(); }
		ImGui::SameLine();
		if (ImGui::Button("Cancel")) { ImGui::CloseCurrentPopup(); }
		ImGui::EndPopup();
	}
	DrawMenuBar();
	DrawNavigationBar();
	float leftPanelWidth = 220.0f;
	ImGui::BeginChild("LeftPanel", ImVec2(leftPanelWidth, -ImGui::GetFrameHeightWithSpacing()),
	                   ImGuiChildFlags_Borders);
	DrawLeftPanel();
	ImGui::EndChild();
	ImGui::SameLine();
    // 画布区域
	ImGui::BeginChild("GraphCanvas", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
	DrawGraphCanvas();
	ImGui::EndChild();
	DrawStatusBar();
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
	{
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
			Save();
		if (IsRootGraphView() && ImGui::IsKeyPressed(ImGuiKey_F))
			ne::NavigateToContent();
	}
	ImGui::End();
}
// ============================================================================
// DrawGraphCanvas - imgui-node-editor 即时模式渲染
// ============================================================================
void VansAnimGraphEditorWindow::DrawGraphCanvas()
{
	if (!IsRootGraphView())
	{
		DrawSubgraphPreviewCanvas();
		return;
	}
	ne::SetCurrentEditor(m_NodeEditorCtx);
	ne::Begin("AnimGraphCanvas");
	if (m_EditState->needsInitialLayout)
	{
		ApplyNodePositions();
		m_EditState->needsInitialLayout = false;
		ne::NavigateToContent(0.0f);
	}
	DrawGraphEditorCanvas();
	ne::End();
	ne::SetCurrentEditor(nullptr);
}
// ============================================================================
//  宸︿晶闈㈡澘
// ============================================================================
void VansAnimGraphEditorWindow::DrawLeftPanel()
{
	DrawParametersPanel();
	ImGui::Spacing();
	ImGui::Spacing();
	DrawClipsPanel();
	ImGui::Spacing();
	ImGui::Spacing();
	DrawPropertiesPanel();
}
void VansAnimGraphEditorWindow::DrawParametersPanel()
{
	ImGui::Text("Parameters");
	ImGui::Separator();
	for (int i = 0; i < (int)m_EditState->parameters.size(); ++i)
	{
		auto& param = m_EditState->parameters[i];
		ImGui::PushID(i);
		const char* typeNames[] = { "float", "bool", "int", "trigger" };
		ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "[%s]",
		                   typeNames[(int)param.type]);
		ImGui::SameLine();
		char nameBuf[128];
		strncpy(nameBuf, param.name.c_str(), sizeof(nameBuf));
		nameBuf[sizeof(nameBuf) - 1] = '\0';
		ImGui::SetNextItemWidth(100.0f);
		if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
		{
			param.name = nameBuf;
			m_EditState->isDirty = true;
		}
		// ============================================================================
		// ============================================================================
		// ?????
		switch (param.type)
		{
		case AnimatorParamType::Float:
			ImGui::SetNextItemWidth(60.0f);
			if (ImGui::DragFloat("##val", &param.floatVal, 0.01f))
				m_EditState->isDirty = true;
			break;
		case AnimatorParamType::Bool:
			if (ImGui::Checkbox("##val", &param.boolVal))
				m_EditState->isDirty = true;
			break;
		case AnimatorParamType::Int:
			ImGui::SetNextItemWidth(60.0f);
			if (ImGui::InputInt("##val", &param.intVal))
				m_EditState->isDirty = true;
			break;
		case AnimatorParamType::Trigger:
			ImGui::TextDisabled("(auto-reset)");
			break;
		case AnimatorParamType::Vector3:
			ImGui::SetNextItemWidth(150.0f);
			if (ImGui::DragFloat3("##val", &param.vec3Val.x, 0.01f))
				m_EditState->isDirty = true;
			break;
		case AnimatorParamType::Quaternion:
		{
			float q[4] = { param.quatVal.x, param.quatVal.y, param.quatVal.z, param.quatVal.w };
			ImGui::SetNextItemWidth(190.0f);
			if (ImGui::DragFloat4("##val", q, 0.01f))
			{
				param.quatVal = glm::normalize(glm::quat(q[3], q[0], q[1], q[2]));
				m_EditState->isDirty = true;
			}
			break;
		}
		}
		// 鍒犻櫎鎸夐挳
		ImGui::SameLine();
		if (ImGui::SmallButton("X"))
		{
			m_EditState->parameters.erase(m_EditState->parameters.begin() + i);
			m_EditState->isDirty = true;
			--i;
		}
		ImGui::PopID();
	}
	// ============================================================================
	// ????
	if (ImGui::Button("+ Add Parameter"))
		ImGui::OpenPopup("AddParamPopup");
	if (ImGui::BeginPopup("AddParamPopup"))
	{
		static char newParamName[128] = "";
		static int newParamType = 0;
		ImGui::InputText("Name", newParamName, sizeof(newParamName));
		ImGui::Combo("Type", &newParamType, "Float\0Bool\0Int\0Trigger\0Vector3\0Quaternion\0");
		if (ImGui::Button("Add") && strlen(newParamName) > 0)
		{
			AnimatorParameter p;
			p.name = newParamName;
			p.type = (AnimatorParamType)newParamType;
			m_EditState->parameters.push_back(p);
			m_EditState->isDirty = true;
			newParamName[0] = '\0';
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}
void VansAnimGraphEditorWindow::DrawClipsPanel()
{
	ImGui::Text("Clips");
	ImGui::Separator();
	for (int i = 0; i < (int)m_EditState->clipRefs.size(); ++i)
	{
		auto& clip = m_EditState->clipRefs[i];
		ImGui::PushID(i + 1000);
		bool selected = false;
		ImGui::Selectable(clip.name.c_str(), &selected);
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", clip.path.c_str());
		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
		if (ImGui::SmallButton("X"))
		{
			m_EditState->clipRefs.erase(m_EditState->clipRefs.begin() + i);
			m_EditState->isDirty = true;
			--i;
		}
		ImGui::PopID();
	}
	if (ImGui::Button("+ Add Clip"))
		ImGui::OpenPopup("AddClipPopup");
	if (ImGui::BeginPopup("AddClipPopup"))
	{
		static char newClipName[128] = "";
		static char newClipPath[256] = "";
		ImGui::InputText("Name", newClipName, sizeof(newClipName));
		ImGui::InputText("Path (.vclip)", newClipPath, sizeof(newClipPath));
		if (ImGui::Button("Add") && strlen(newClipName) > 0 && strlen(newClipPath) > 0)
		{
			AnimatorClipRef ref;
			ref.name = newClipName;
			ref.path = newClipPath;
			m_EditState->clipRefs.push_back(ref);
			m_EditState->isDirty = true;
			newClipName[0] = '\0';
			newClipPath[0] = '\0';
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}
// ============================================================================
// ============================================================================
// ============================================================================
void VansAnimGraphEditorWindow::DrawMenuBar()
{
	if (ImGui::BeginMenuBar())
	{
		if (ImGui::BeginMenu("File"))
		{
			if (ImGui::MenuItem("Save", "Ctrl+S"))
				Save();
			ImGui::Separator();
			if (ImGui::MenuItem("Close"))
				Close();
			ImGui::EndMenu();
		}
		if (ImGui::BeginMenu("View"))
		{
			if (!IsRootGraphView())
				ImGui::BeginDisabled();
			if (ImGui::MenuItem("Fit All Nodes"))
				ne::NavigateToContent();
			if (ImGui::MenuItem("Fit Selected"))
				ne::NavigateToSelection(true);
			if (!IsRootGraphView())
				ImGui::EndDisabled();
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
}
// ============================================================================
// ============================================================================
void VansAnimGraphEditorWindow::DrawStatusBar()
{
	ImGui::Separator();
	if (m_EditState->isDirty)
		ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.3f, 1.0f), "Modified");
	else
		ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f), "Saved");
	ImGui::SameLine(200);
	if (m_TargetGraph)
	{
		ImGui::Text("Nodes: %d  Links: %d",
		            (int)m_TargetGraph->GetNodes().size(),
		            (int)m_TargetGraph->GetLinks().size());
	}
}
// ============================================================================
//  AnimGraph 娓叉煋
// ============================================================================
void VansAnimGraphEditorWindow::DrawNavigationBar()
{
    ImGui::Separator();
    if (IsRootGraphView())
        ImGui::BeginDisabled();
    if (ImGui::Button("< Back"))
        NavigateBack();
    if (IsRootGraphView())
        ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextUnformatted("Graph");
    for (const auto& frame : m_NavigationStack)
    {
        ImGui::SameLine();
        ImGui::TextDisabled(">");
        ImGui::SameLine();
        ImGui::TextUnformatted(frame.title.c_str());
    }
    ImGui::Separator();
}
bool VansAnimGraphEditorWindow::CanOpenNodeSubgraph(VansAnimGraphNode* node) const
{
    if (!node)
        return false;
    return node->GetType() == AnimGraphNodeType::StateMachine ||
           node->GetType() == AnimGraphNodeType::MotionMatching;
}
void VansAnimGraphEditorWindow::OpenNodeSubgraph(int nodeId)
{
    if (!m_TargetGraph)
        return;
    VansAnimGraphNode* node = m_TargetGraph->GetNode(nodeId);
    if (!CanOpenNodeSubgraph(node))
        return;
    NavigationFrame frame;
    frame.nodeId = nodeId;
    frame.title = std::string(VansAnimGraphNode::TypeToString(node->GetType())) + ": " + node->GetName();
    m_NavigationStack.push_back(frame);
    m_EditState->selectedNodeId = nodeId;
    m_EditState->selectedLinkId = -1;
}
void VansAnimGraphEditorWindow::NavigateBack()
{
    if (!m_NavigationStack.empty())
        m_NavigationStack.pop_back();
}
void VansAnimGraphEditorWindow::DrawSubgraphPreviewCanvas()
{
    if (m_NavigationStack.empty() || !m_TargetGraph)
        return;
    VansAnimGraphNode* node = m_TargetGraph->GetNode(m_NavigationStack.back().nodeId);
    if (!node)
    {
        ImGui::TextDisabled("Missing node.");
        return;
    }
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "[%s] %s", VansAnimGraphNode::TypeToString(node->GetType()), node->GetName().c_str());
    ImGui::Separator();
    if (node->GetType() == AnimGraphNodeType::StateMachine)
        DrawStateMachineSubgraphPreview(node);
    else if (node->GetType() == AnimGraphNodeType::MotionMatching)
        DrawMotionMatchingSubgraphPreview(node);
}
void VansAnimGraphEditorWindow::DrawStateMachineSubgraphPreview(VansAnimGraphNode* node)
{
    auto* sm = static_cast<AnimGraphStateMachineNode*>(node);
    ImGui::Text("Default State: %s", sm->m_DefaultStateName.empty() ? "(none)" : sm->m_DefaultStateName.c_str());
    ImGui::Text("Current State: %s", sm->m_CurrentStateName.empty() ? "(none)" : sm->m_CurrentStateName.c_str());
    ImGui::Text("Previous State: %s", sm->m_PrevStateName.empty() ? "(none)" : sm->m_PrevStateName.c_str());
    ImGui::Text("Blend: %.2f / %.2f", sm->m_BlendAlpha, sm->m_BlendDuration);
    ImGui::Separator();
    ImGui::Text("States");
    for (const auto& state : sm->m_States)
    {
        ImGui::BulletText("%s  Clip=%s  Speed=%.2f  Loop=%s  RootMotion=%s", state.name.c_str(), state.clipName.c_str(), state.speed, state.loop ? "true" : "false", state.rootMotion ? "true" : "false");
        ImGui::TextDisabled("    Time %.2f  Range %.2f -> %.2f", state.currentTime, state.startTime, state.endTime);
    }
    ImGui::Separator();
    ImGui::Text("Transitions");
    for (const auto& transition : sm->m_Transitions)
    {
        ImGui::BulletText("%s -> %s  Blend=%.2f  Exit=%s %.2f  Conditions=%d", transition.fromState.c_str(), transition.toState.c_str(), transition.blendDuration, transition.hasExitTime ? "true" : "false", transition.exitTime, (int)transition.conditions.size());
    }
}
void VansAnimGraphEditorWindow::DrawMotionMatchingSubgraphPreview(VansAnimGraphNode* node)
{
    auto* mm = static_cast<AnimGraphMotionMatchingNode*>(node);
    const MotionMatchingDebugData* debug = m_TargetController ? m_TargetController->GetMotionMatchingDebugData() : nullptr;
    ImGui::Text("Fallback Input: %s", mm->m_EnableFallbackInput ? "enabled" : "disabled");
    ImGui::Text("Runtime Param: UseMotionMatching");
    ImGui::Separator();
    ImGui::Text("Runtime");
    if (debug)
    {
        ImGui::Text("Enabled: %s  Used: %s  Database: %s  Rig: %s", debug->enabled ? "true" : "false", debug->usedThisFrame ? "true" : "false", debug->databaseReady ? "ready" : "not ready", debug->rigReady ? "ready" : "not ready");
        ImGui::Text("Active: %s @ %.2f", debug->activeClip.empty() ? "(none)" : debug->activeClip.c_str(), debug->activeTime);
        ImGui::Text("Selected: %s @ %.2f", debug->selectedClip.empty() ? "(none)" : debug->selectedClip.c_str(), debug->selectedTime);
        ImGui::Text("Cost: %.3f  Trajectory %.3f  Pose %.3f  Bias %.3f", debug->currentCost, debug->trajectoryCost, debug->poseCost, debug->biasCost);
        ImGui::Text("Query: Speed %.2f  Direction %.2f  Clips %d  Samples %d  Switches %d", debug->querySpeed, debug->queryDirection, debug->clipCount, debug->sampleCount, debug->switches);
        if (!debug->rigStatus.empty())
            ImGui::TextDisabled("%s", debug->rigStatus.c_str());
        ImGui::Separator();
        ImGui::Text("Top Candidates");
        for (const auto& candidate : debug->topCandidates)
            ImGui::BulletText("%s @ %.2f  Cost %.3f  Traj %.3f  Pose %.3f  Bias %.3f", candidate.clipName.c_str(), candidate.time, candidate.totalCost, candidate.trajectoryCost, candidate.poseCost, candidate.biasCost);
    }
    else
        ImGui::TextDisabled("No active runtime data.");
    ImGui::Separator();
    ImGui::Text("Pipeline");
    ImGui::BulletText("Query Parameters");
    ImGui::BulletText("Trajectory And Pose Feature Extraction");
    ImGui::BulletText("Database Search");
    ImGui::BulletText("Candidate Ranking");
    ImGui::BulletText("Pose Output");
    ImGui::BulletText("Fallback Pose");
}
void VansAnimGraphEditorWindow::DrawGraphEditorCanvas()
{
	if (!m_TargetGraph) return;
	for (auto& [id, node] : m_TargetGraph->GetNodes())
		DrawGraphNode(node.get());
	DrawGraphLinks();
	ne::NodeId doubleClickedNode = ne::GetDoubleClickedNode();
	if (doubleClickedNode)
		OpenNodeSubgraph(static_cast<int>(doubleClickedNode.Get()));
	// ============================================================================
	// ????
	SyncSelection();
}
static ImU32 GetNodeHeaderColor(AnimGraphNodeType type)
{
	switch (type)
	{
    case AnimGraphNodeType::Entry:         return IM_COL32(80,  200, 120, 255);  // 绿
    case AnimGraphNodeType::Output:        return IM_COL32(220, 80,  80,  255);  // 红
    case AnimGraphNodeType::Clip:          return IM_COL32(80,  140, 220, 255);  // 蓝
    case AnimGraphNodeType::Blend:         return IM_COL32(160, 100, 220, 255);  // 紫
	case AnimGraphNodeType::Blend1D:       return IM_COL32(140, 110, 200, 255);
	case AnimGraphNodeType::IfCondition:   return IM_COL32(230, 160, 50,  255);  // ?
	case AnimGraphNodeType::Switch:        return IM_COL32(210, 200, 60,  255);  // ?
	case AnimGraphNodeType::AdditiveBlend: return IM_COL32(100, 180, 180, 255);  // ?
	case AnimGraphNodeType::SpeedScale:    return IM_COL32(180, 140, 100, 255);  // ?
	case AnimGraphNodeType::StateMachine:  return IM_COL32(180, 180, 180, 255);  // ?
	case AnimGraphNodeType::MotionMatching:return IM_COL32(90,  190, 150, 255);
	}
	return IM_COL32(150, 150, 150, 255);
}
static const char* GetNodeSubtitle(VansAnimGraphNode* node)
{
	switch (node->GetType())
	{
	case AnimGraphNodeType::Clip:
	{
		auto* n = static_cast<AnimGraphClipNode*>(node);
		return n->m_ClipName.c_str();
	}
	case AnimGraphNodeType::Blend:
	{
		auto* n = static_cast<AnimGraphBlendNode*>(node);
		return n->m_UseParam ? n->m_ParamName.c_str() : "(fixed)";
	}
	case AnimGraphNodeType::Blend1D:
	{
		auto* n = static_cast<AnimGraphBlend1DNode*>(node);
		return n->m_ParamName.c_str();
	}
	case AnimGraphNodeType::IfCondition:
	{
		auto* n = static_cast<AnimGraphIfConditionNode*>(node);
		return n->m_ParamName.c_str();
	}
	case AnimGraphNodeType::Switch:
	{
		auto* n = static_cast<AnimGraphSwitchNode*>(node);
		return n->m_ParamName.c_str();
	}
	case AnimGraphNodeType::MotionMatching:
		return "UseMotionMatching";
	default:
		return "";
	}
}
void VansAnimGraphEditorWindow::DrawGraphNode(VansAnimGraphNode* node)
{
	if (!node) return;
	int nodeId = node->GetNodeId();
	ImU32 headerColor = GetNodeHeaderColor(node->GetType());
	ne::BeginNode(AnimGraphIds::MakeNodeId(nodeId));
	// ============================================================================
	// ???????? + ????
	ImGui::PushStyleColor(ImGuiCol_Text, headerColor);
	ImGui::Text("[%s]", VansAnimGraphNode::TypeToString(node->GetType()));
	ImGui::PopStyleColor();
	ImGui::SameLine();
	ImGui::Text("%s", node->GetName().c_str());
	const char* subtitle = GetNodeSubtitle(node);
	if (subtitle && subtitle[0] != '\0')
	{
		ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "%s", subtitle);
	}
	ImGui::Dummy(ImVec2(100, 0));
	auto pins = node->GetPins();
	// 鏀堕泦 Input / Output pin 鍒楄〃
	std::vector<const AnimGraphPin*> inputPins, outputPins;
	for (auto& pin : pins)
	{
		if (pin.kind == AnimGraphPinKind::Input)
			inputPins.push_back(&pin);
		else
			outputPins.push_back(&pin);
	}
	size_t rowCount = (std::max)(inputPins.size(), outputPins.size());
	for (size_t row = 0; row < rowCount; ++row)
	{
		// Input pin
		if (row < inputPins.size())
		{
			ne::BeginPin(AnimGraphIds::MakeInputPin(nodeId, inputPins[row]->pinIndex),
			             ne::PinKind::Input);
			ImGui::Text(">> %s", inputPins[row]->name.c_str());
			ne::EndPin();
		}
		else
		{
			ImGui::Text("");  // 鍗犱綅
		}
		if (row < outputPins.size())
		{
			ImGui::SameLine(120);
			ne::BeginPin(AnimGraphIds::MakeOutputPin(nodeId, outputPins[row]->pinIndex),
			             ne::PinKind::Output);
			ImGui::Text("%s >>", outputPins[row]->name.c_str());
			ne::EndPin();
		}
	}
	ne::EndNode();
}
void VansAnimGraphEditorWindow::DrawGraphLinks()
{
	if (!m_TargetGraph) return;
	for (auto& link : m_TargetGraph->GetLinks())
	{
		ne::PinId startPin = AnimGraphIds::MakeOutputPin(link.fromNodeId, link.fromPinIndex);
		ne::PinId endPin   = AnimGraphIds::MakeInputPin(link.toNodeId, link.toPinIndex);
		ne::LinkId linkId  = AnimGraphIds::MakeLinkId(link.linkId);
		ne::Link(linkId, startPin, endPin, ImVec4(0.9f, 0.9f, 0.9f, 1.0f), 2.0f);
	}
}
void VansAnimGraphEditorWindow::DrawPropertiesPanel()
{
	ImGui::Text("Properties");
	ImGui::Separator();
	if (!m_TargetGraph || m_EditState->selectedNodeId < 0)
	{
		ImGui::TextDisabled("Select a node to view properties.");
		return;
	}
	VansAnimGraphNode* node = m_TargetGraph->GetNode(m_EditState->selectedNodeId);
	if (!node) return;
	ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "[%s]",
	                   VansAnimGraphNode::TypeToString(node->GetType()));
	ImGui::Text("Name: %s", node->GetName().c_str());
	ImGui::Text("ID:   %d", node->GetNodeId());
	ImGui::Separator();
	switch (node->GetType())
	{
	case AnimGraphNodeType::Clip:
	{
		auto* n = static_cast<AnimGraphClipNode*>(node);
		ImGui::Text("Clip:  %s", n->m_ClipName.c_str());
		ImGui::Text("Speed: %.2f", n->m_Speed);
		ImGui::Text("Loop:  %s", n->m_Loop ? "true" : "false");
		break;
	}
	case AnimGraphNodeType::Blend:
	{
		auto* n = static_cast<AnimGraphBlendNode*>(node);
		ImGui::Text("Param: %s", n->m_ParamName.c_str());
		ImGui::Text("Fixed Alpha: %.2f", n->m_FixedAlpha);
		ImGui::Text("Use Param:   %s", n->m_UseParam ? "true" : "false");
		break;
	}
	case AnimGraphNodeType::Blend1D:
	{
		auto* n = static_cast<AnimGraphBlend1DNode*>(node);
		ImGui::Text("Param: %s", n->m_ParamName.c_str());
		ImGui::Text("Thresholds: %d", (int)n->m_Thresholds.size());
		break;
	}
	case AnimGraphNodeType::IfCondition:
	{
		auto* n = static_cast<AnimGraphIfConditionNode*>(node);
		const char* opStr[] = { ">", "<", "==", "!=", ">=", "<=" };
		ImGui::Text("Param: %s", n->m_ParamName.c_str());
		ImGui::Text("Op:    %s", opStr[(int)n->m_CompareOp]);
		ImGui::Text("Float: %.2f  Int: %d  Bool: %s",
		            n->m_FloatVal, n->m_IntVal, n->m_BoolVal ? "true" : "false");
		break;
	}
	case AnimGraphNodeType::Switch:
	{
		auto* n = static_cast<AnimGraphSwitchNode*>(node);
		ImGui::Text("Param:      %s", n->m_ParamName.c_str());
		ImGui::Text("Case Count: %d", n->m_CaseCount);
		break;
	}
	case AnimGraphNodeType::AdditiveBlend:
	{
		auto* n = static_cast<AnimGraphAdditiveBlendNode*>(node);
		ImGui::Text("Param:  %s", n->m_ParamName.c_str());
		ImGui::Text("Weight: %.2f", n->m_FixedWeight);
		ImGui::Text("Use Param: %s", n->m_UseParam ? "true" : "false");
		break;
	}
	case AnimGraphNodeType::SpeedScale:
	{
		auto* n = static_cast<AnimGraphSpeedScaleNode*>(node);
		ImGui::Text("Param: %s", n->m_ParamName.c_str());
		ImGui::Text("Speed: %.2f", n->m_FixedSpeed);
		ImGui::Text("Use Param: %s", n->m_UseParam ? "true" : "false");
		break;
	}
	case AnimGraphNodeType::MotionMatching:
	{
		auto* n = static_cast<AnimGraphMotionMatchingNode*>(node);
		ImGui::Text("Fallback Input: %s", n->m_EnableFallbackInput ? "true" : "false");
		ImGui::Text("Runtime Param: UseMotionMatching");
		break;
	}
	case AnimGraphNodeType::IK:
	{
		auto* n = static_cast<AnimGraphIKNode*>(node);
		ImGui::Text("Chain: %s", n->m_Chain.chainName.c_str());
		ImGui::Text("Bones: %d", (int)n->m_Chain.bones.size());
		ImGui::Text("Target Pos: %s", n->m_TargetPosParamName.c_str());
		ImGui::Text("Target Rot: %s", n->m_TargetRotParamName.c_str());
		ImGui::Text("Weight: %s", n->m_WeightParamName.c_str());
		ImGui::Text("Rotation Target: %s %.2f",
		            n->m_Chain.enableRotationTarget ? "true" : "false",
		            n->m_Chain.rotationWeight);
		break;
	}
	case AnimGraphNodeType::TwoBoneIK:
	{
		auto* n = static_cast<AnimGraphTwoBoneIKNode*>(node);
		ImGui::Text("Profile: %s %s",
		            n->m_UseLegProfile ? "Leg" : "Arm",
		            n->m_IsRightSide ? "Right" : "Left");
		ImGui::Text("Root/Mid/Tip: %s / %s / %s",
		            n->m_RootBoneName.c_str(),
		            n->m_MidBoneName.c_str(),
		            n->m_TipBoneName.c_str());
		ImGui::Text("Target Pos: %s", n->m_TargetPosParamName.c_str());
		ImGui::Text("Target Rot: %s", n->m_TargetRotParamName.c_str());
		ImGui::Text("Weight: %s", n->m_WeightParamName.c_str());
		ImGui::Text("Rotation Target: %s %.2f",
		            n->m_EnableRotationTarget ? "true" : "false",
		            n->m_RotationWeight);
		break;
	}
	default:
		ImGui::TextDisabled("No editable properties.");
		break;
	}
}
void VansAnimGraphEditorWindow::SyncSelection()
{
	if (!ne::HasSelectionChanged()) return;
	m_EditState->selectedNodeId = -1;
	m_EditState->selectedLinkId = -1;
	int selCount = ne::GetSelectedObjectCount();
	if (selCount > 0)
	{
		std::vector<ne::NodeId> selectedNodes(selCount);
		int count = ne::GetSelectedNodes(selectedNodes.data(), selCount);
		if (count > 0)
			m_EditState->selectedNodeId = (int)selectedNodes[0].Get();
		std::vector<ne::LinkId> selectedLinks(selCount);
		int linkCount = ne::GetSelectedLinks(selectedLinks.data(), selCount);
		if (linkCount > 0)
			m_EditState->selectedLinkId = (int)selectedLinks[0].Get() - 1000000;
	}
}
// ============================================================================
//  AnimGraph layout
// ============================================================================
void VansAnimGraphEditorWindow::ApplyNodePositions()
{
	if (!m_TargetGraph) return;
	for (auto& [id, node] : m_TargetGraph->GetNodes())
	{
		ne::SetNodePosition(
			AnimGraphIds::MakeNodeId(id),
			ImVec2(node->m_EditorPosX, node->m_EditorPosY));
	}
}
void VansAnimGraphEditorWindow::ReadNodePositions()
{
	if (!m_TargetGraph) return;
	for (auto& [id, node] : m_TargetGraph->GetNodes())
	{
		ImVec2 pos = ne::GetNodePosition(AnimGraphIds::MakeNodeId(id));
		node->m_EditorPosX = pos.x;
		node->m_EditorPosY = pos.y;
	}
}
// ============================================================================
// ============================================================================
// ============================================================================
void VansAnimGraphEditorWindow::Save()
{
	if (!m_EditState->isDirty) return;
	if (m_NodeEditorCtx)
	{
		ne::SetCurrentEditor(m_NodeEditorCtx);
		ReadNodePositions();
		ne::SetCurrentEditor(nullptr);
	}
	m_EditState->isDirty = false;
}
// ============================================================================
// ============================================================================
namespace
{
	void SaveEditorLayoutImpl(const std::string& filePath,
	                          const std::vector<AnimGraphNodeLayout>& layouts)
	{
		// ============================================================================
		// ???? JSON
		std::ifstream inFile(filePath);
		if (!inFile.is_open()) return;
		json root;
		try { root = json::parse(inFile); }
		catch (const json::parse_error&) { return; }
		inFile.close();
		json editorObj;
		json layoutArray = json::array();
		for (auto& layout : layouts)
		{
			json item;
			item["state"] = layout.stateName;
			item["x"]     = layout.position.x;
			item["y"]     = layout.position.y;
			layoutArray.push_back(item);
		}
		editorObj["nodeLayouts"] = layoutArray;
		root["editor"] = editorObj;
		std::ofstream outFile(filePath);
		if (outFile.is_open())
		{
			outFile << root.dump(4);
			outFile.close();
		}
	}
}
void VansAnimGraphEditorWindow::SaveEditorLayout(const std::string& filePath)
{
	SaveEditorLayoutImpl(filePath, m_EditState->nodeLayouts);
}
