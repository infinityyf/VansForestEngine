#include "VansAnimGraphEditorWindow.h"
#include "../../AnimationCore/VansAnimationNode.h"
#include "../../Util/VansLog.h"

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <algorithm>
#include <cstring>

using json = nlohmann::json;
using namespace VansGraphics;

namespace ne = ax::NodeEditor;

// ════════════════════════════════════════════════════════════════
//  构造 / 析构
// ════════════════════════════════════════════════════════════════

VansAnimGraphEditorWindow::VansAnimGraphEditorWindow()  = default;

VansAnimGraphEditorWindow::~VansAnimGraphEditorWindow()
{
	if (m_NodeEditorCtx)
	{
		ne::DestroyEditor(m_NodeEditorCtx);
		m_NodeEditorCtx = nullptr;
	}
}

// ════════════════════════════════════════════════════════════════
//  Open / Close
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::Open(VansAnimationController* controller,
                                      VansAnimationNode* node)
{
	m_TargetController = controller;
	m_TargetNode       = node;
	m_IsOpen           = true;

	// 获取 .vanimator 文件路径
	m_AnimatorFilePath = node->GetAnimatorFilePath();

	// 设置 v2 AnimGraph
	m_TargetGraph = controller->GetGraph();

	// v2 布局由 graph 节点自身存储，无需单独加载布局文件

	// 创建/重建 imgui-node-editor 上下文
	if (m_NodeEditorCtx)
		ne::DestroyEditor(m_NodeEditorCtx);

	ne::Config config;
	config.SettingsFile          = nullptr;  // 不使用文件持久化，手动管理布局
	config.NavigateButtonIndex   = 2;        // 中键平移
	config.ContextMenuButtonIndex = 1;       // 右键菜单
	m_NodeEditorCtx = ne::CreateEditor(&config);

	// 标记需要在首次绘制时应用节点位置
	m_EditState.needsInitialLayout = true;
	m_EditState.isDirty            = false;
}

void VansAnimGraphEditorWindow::Close()
{
	// 关闭时将当前节点位置写回 graph 节点
	if (m_NodeEditorCtx)
	{
		ne::SetCurrentEditor(m_NodeEditorCtx);
		ReadV2NodePositions();
		ne::SetCurrentEditor(nullptr);
		ne::DestroyEditor(m_NodeEditorCtx);
		m_NodeEditorCtx = nullptr;
	}

	m_TargetController = nullptr;
	m_TargetNode       = nullptr;
	m_TargetGraph      = nullptr;
	m_IsOpen           = false;
}

// ════════════════════════════════════════════════════════════════
//  ShowWindow — 主循环入口
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::ShowWindow(VansVKDevice& device)
{
	if (!m_IsOpen) return;

	// 检查 Controller 有效性
	if (!m_TargetController)
	{
		m_IsOpen = false;
		return;
	}

	ImGui::SetNextWindowSize(ImVec2(1200, 700), ImGuiCond_FirstUseEver);

	char title[256];
	snprintf(title, sizeof(title), "Animation Graph Editor - %s%s###AnimGraphEditor",
	         m_EditState.name.c_str(),
	         m_EditState.isDirty ? " *" : "");

	if (!ImGui::Begin(title, &m_IsOpen, ImGuiWindowFlags_MenuBar))
	{
		ImGui::End();
		return;
	}

	// 未保存关闭确认
	if (!m_IsOpen && m_EditState.isDirty)
	{
		m_IsOpen = true;  // 暂时重新打开
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

	// 主布局: 左侧面板 | 右侧画布
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

	// 快捷键（仅在 ImGui widget 层处理，不通过 VansInputManager）
	if (ImGui::IsWindowFocused(ImGuiFocusedFlags_ChildWindows))
	{
		if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
			Save();
		if (ImGui::IsKeyPressed(ImGuiKey_F))
			ne::NavigateToContent();
	}

	ImGui::End();
}

// ════════════════════════════════════════════════════════════════
//  DrawGraphCanvas — imgui-node-editor 即时模式渲染
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::DrawGraphCanvas()
{
	ne::SetCurrentEditor(m_NodeEditorCtx);

	ne::Begin("AnimGraphCanvas");

	// 首次打开时应用节点位置
	if (m_EditState.needsInitialLayout)
	{
		ApplyV2NodePositions();
		m_EditState.needsInitialLayout = false;
		ne::NavigateToContent(0.0f);
	}

	DrawV2GraphCanvas();

	ne::End();

	ne::SetCurrentEditor(nullptr);
}

// ════════════════════════════════════════════════════════════════
//  左侧面板
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::DrawLeftPanel()
{
	DrawParametersPanel();
	ImGui::Spacing();
	ImGui::Spacing();
	DrawClipsPanel();
	ImGui::Spacing();
	ImGui::Spacing();
	DrawV2PropertiesPanel();
}

void VansAnimGraphEditorWindow::DrawParametersPanel()
{
	ImGui::Text("Parameters");
	ImGui::Separator();

	for (int i = 0; i < (int)m_EditState.parameters.size(); ++i)
	{
		auto& param = m_EditState.parameters[i];
		ImGui::PushID(i);

		const char* typeNames[] = { "float", "bool", "int", "trigger" };
		ImGui::TextColored(ImVec4(0.5f, 0.7f, 1.0f, 1.0f), "[%s]",
		                   typeNames[(int)param.type]);
		ImGui::SameLine();

		// 名称（可编辑）
		char nameBuf[128];
		strncpy(nameBuf, param.name.c_str(), sizeof(nameBuf));
		nameBuf[sizeof(nameBuf) - 1] = '\0';
		ImGui::SetNextItemWidth(100.0f);
		if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf)))
		{
			param.name = nameBuf;
			m_EditState.isDirty = true;
		}

		// 默认值编辑
		ImGui::SameLine();
		switch (param.type)
		{
		case AnimatorParamType::Float:
			ImGui::SetNextItemWidth(60.0f);
			if (ImGui::DragFloat("##val", &param.floatVal, 0.01f))
				m_EditState.isDirty = true;
			break;
		case AnimatorParamType::Bool:
			if (ImGui::Checkbox("##val", &param.boolVal))
				m_EditState.isDirty = true;
			break;
		case AnimatorParamType::Int:
			ImGui::SetNextItemWidth(60.0f);
			if (ImGui::InputInt("##val", &param.intVal))
				m_EditState.isDirty = true;
			break;
		case AnimatorParamType::Trigger:
			ImGui::TextDisabled("(auto-reset)");
			break;
		}

		// 删除按钮
		ImGui::SameLine();
		if (ImGui::SmallButton("X"))
		{
			m_EditState.parameters.erase(m_EditState.parameters.begin() + i);
			m_EditState.isDirty = true;
			--i;
		}

		ImGui::PopID();
	}

	// 添加参数
	if (ImGui::Button("+ Add Parameter"))
		ImGui::OpenPopup("AddParamPopup");

	if (ImGui::BeginPopup("AddParamPopup"))
	{
		static char newParamName[128] = "";
		static int newParamType = 0;
		ImGui::InputText("Name", newParamName, sizeof(newParamName));
		ImGui::Combo("Type", &newParamType, "Float\0Bool\0Int\0Trigger\0");
		if (ImGui::Button("Add") && strlen(newParamName) > 0)
		{
			AnimatorParameter p;
			p.name = newParamName;
			p.type = (AnimatorParamType)newParamType;
			m_EditState.parameters.push_back(p);
			m_EditState.isDirty = true;
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

	for (int i = 0; i < (int)m_EditState.clipRefs.size(); ++i)
	{
		auto& clip = m_EditState.clipRefs[i];
		ImGui::PushID(i + 1000);  // 避免与参数面板 ID 冲突

		bool selected = false;
		ImGui::Selectable(clip.name.c_str(), &selected);

		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("%s", clip.path.c_str());

		ImGui::SameLine(ImGui::GetContentRegionAvail().x - 20);
		if (ImGui::SmallButton("X"))
		{
			m_EditState.clipRefs.erase(m_EditState.clipRefs.begin() + i);
			m_EditState.isDirty = true;
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
			m_EditState.clipRefs.push_back(ref);
			m_EditState.isDirty = true;
			newClipName[0] = '\0';
			newClipPath[0] = '\0';
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}
}

// ════════════════════════════════════════════════════════════════
//  菜单栏
// ════════════════════════════════════════════════════════════════

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
			if (ImGui::MenuItem("Fit All Nodes"))
				ne::NavigateToContent();
			if (ImGui::MenuItem("Fit Selected"))
				ne::NavigateToSelection(true);
			ImGui::EndMenu();
		}
		ImGui::EndMenuBar();
	}
}

// ════════════════════════════════════════════════════════════════
//  状态栏
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::DrawStatusBar()
{
	ImGui::Separator();

	if (m_EditState.isDirty)
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

// ════════════════════════════════════════════════════════════════
//  v2 AnimGraph 渲染
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::DrawV2GraphCanvas()
{
	if (!m_TargetGraph) return;

	// 1. 绘制所有图节点
	for (auto& [id, node] : m_TargetGraph->GetNodes())
		DrawV2Node(node.get());

	// 2. 绘制所有连线
	DrawV2Links();

	// 3. 同步选择
	SyncV2Selection();
}

// ─── 节点颜色映射 ─────────────────────────────────────────────────

static ImU32 GetV2NodeHeaderColor(AnimGraphNodeType type)
{
	switch (type)
	{
	case AnimGraphNodeType::Entry:         return IM_COL32(80,  200, 120, 255);  // 绿
	case AnimGraphNodeType::Output:        return IM_COL32(220, 80,  80,  255);  // 红
	case AnimGraphNodeType::Clip:          return IM_COL32(80,  140, 220, 255);  // 蓝
	case AnimGraphNodeType::Blend:         return IM_COL32(160, 100, 220, 255);  // 紫
	case AnimGraphNodeType::Blend1D:       return IM_COL32(140, 110, 200, 255);  // 紫偏淡
	case AnimGraphNodeType::IfCondition:   return IM_COL32(230, 160, 50,  255);  // 橙
	case AnimGraphNodeType::Switch:        return IM_COL32(210, 200, 60,  255);  // 黄
	case AnimGraphNodeType::AdditiveBlend: return IM_COL32(100, 180, 180, 255);  // 青
	case AnimGraphNodeType::SpeedScale:    return IM_COL32(180, 140, 100, 255);  // 棕
	case AnimGraphNodeType::StateMachine:  return IM_COL32(180, 180, 180, 255);  // 灰
	}
	return IM_COL32(150, 150, 150, 255);
}

static const char* GetV2NodeSubtitle(VansAnimGraphNode* node)
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
	default:
		return "";
	}
}

void VansAnimGraphEditorWindow::DrawV2Node(VansAnimGraphNode* node)
{
	if (!node) return;

	int nodeId = node->GetNodeId();
	ImU32 headerColor = GetV2NodeHeaderColor(node->GetType());

	ne::BeginNode(AnimGraphV2Ids::MakeNodeId(nodeId));

	// 标题行：类型标签 + 节点名称
	ImGui::PushStyleColor(ImGuiCol_Text, headerColor);
	ImGui::Text("[%s]", VansAnimGraphNode::TypeToString(node->GetType()));
	ImGui::PopStyleColor();

	ImGui::SameLine();
	ImGui::Text("%s", node->GetName().c_str());

	// 副标题（clip 名 / 参数名等）
	const char* subtitle = GetV2NodeSubtitle(node);
	if (subtitle && subtitle[0] != '\0')
	{
		ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f), "%s", subtitle);
	}

	ImGui::Dummy(ImVec2(100, 0));  // 最小宽度

	// 获取 pin 定义
	auto pins = node->GetPins();

	// 收集 Input / Output pin 列表
	std::vector<const AnimGraphPin*> inputPins, outputPins;
	for (auto& pin : pins)
	{
		if (pin.kind == AnimGraphPinKind::Input)
			inputPins.push_back(&pin);
		else
			outputPins.push_back(&pin);
	}

	// 绘制 Input pin（左侧）和 Output pin（右侧）
	// 取较大值来决定行数
	size_t rowCount = std::max(inputPins.size(), outputPins.size());

	for (size_t row = 0; row < rowCount; ++row)
	{
		// Input pin
		if (row < inputPins.size())
		{
			ne::BeginPin(AnimGraphV2Ids::MakeInputPin(nodeId, inputPins[row]->pinIndex),
			             ne::PinKind::Input);
			ImGui::Text(">> %s", inputPins[row]->name.c_str());
			ne::EndPin();
		}
		else
		{
			ImGui::Text("");  // 占位
		}

		// Output pin（同行右侧）
		if (row < outputPins.size())
		{
			ImGui::SameLine(120);
			ne::BeginPin(AnimGraphV2Ids::MakeOutputPin(nodeId, outputPins[row]->pinIndex),
			             ne::PinKind::Output);
			ImGui::Text("%s >>", outputPins[row]->name.c_str());
			ne::EndPin();
		}
	}

	ne::EndNode();
}

void VansAnimGraphEditorWindow::DrawV2Links()
{
	if (!m_TargetGraph) return;

	for (auto& link : m_TargetGraph->GetLinks())
	{
		ne::PinId startPin = AnimGraphV2Ids::MakeOutputPin(link.fromNodeId, link.fromPinIndex);
		ne::PinId endPin   = AnimGraphV2Ids::MakeInputPin(link.toNodeId, link.toPinIndex);
		ne::LinkId linkId  = AnimGraphV2Ids::MakeLinkId(link.linkId);

		ne::Link(linkId, startPin, endPin, ImVec4(0.9f, 0.9f, 0.9f, 1.0f), 2.0f);
	}
}

void VansAnimGraphEditorWindow::DrawV2PropertiesPanel()
{
	ImGui::Text("Properties (v2)");
	ImGui::Separator();

	if (!m_TargetGraph || m_EditState.selectedV2NodeId < 0)
	{
		ImGui::TextDisabled("Select a node to view properties.");
		return;
	}

	VansAnimGraphNode* node = m_TargetGraph->GetNode(m_EditState.selectedV2NodeId);
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
	default:
		ImGui::TextDisabled("No editable properties.");
		break;
	}
}

void VansAnimGraphEditorWindow::SyncV2Selection()
{
	if (!ne::HasSelectionChanged()) return;

	m_EditState.selectedV2NodeId = -1;
	m_EditState.selectedV2LinkId = -1;

	int selCount = ne::GetSelectedObjectCount();
	if (selCount > 0)
	{
		std::vector<ne::NodeId> selectedNodes(selCount);
		int count = ne::GetSelectedNodes(selectedNodes.data(), selCount);
		if (count > 0)
			m_EditState.selectedV2NodeId = (int)selectedNodes[0].Get();

		std::vector<ne::LinkId> selectedLinks(selCount);
		int linkCount = ne::GetSelectedLinks(selectedLinks.data(), selCount);
		if (linkCount > 0)
			m_EditState.selectedV2LinkId = (int)selectedLinks[0].Get() - 1000000;
	}
}

// ════════════════════════════════════════════════════════════════
//  v2 布局管理
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::ApplyV2NodePositions()
{
	if (!m_TargetGraph) return;

	for (auto& [id, node] : m_TargetGraph->GetNodes())
	{
		ne::SetNodePosition(
			AnimGraphV2Ids::MakeNodeId(id),
			ImVec2(node->m_EditorPosX, node->m_EditorPosY));
	}
}

void VansAnimGraphEditorWindow::ReadV2NodePositions()
{
	if (!m_TargetGraph) return;

	for (auto& [id, node] : m_TargetGraph->GetNodes())
	{
		ImVec2 pos = ne::GetNodePosition(AnimGraphV2Ids::MakeNodeId(id));
		node->m_EditorPosX = pos.x;
		node->m_EditorPosY = pos.y;
	}
}

// ════════════════════════════════════════════════════════════════
//  保存
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::Save()
{
	if (!m_EditState.isDirty) return;

	// 从 NodeEditor 读取最新节点位置
	if (m_NodeEditorCtx)
	{
		ne::SetCurrentEditor(m_NodeEditorCtx);
		ReadV2NodePositions();
		ne::SetCurrentEditor(nullptr);
	}

	// TODO: v2 图的完整序列化保存尚未实现
	m_EditState.isDirty = false;
}

// ════════════════════════════════════════════════════════════════
//  保存 editor 布局数据到 .vanimator 文件
// ════════════════════════════════════════════════════════════════

namespace
{
	// 在已有的 .vanimator JSON 中追加/更新 "editor" 字段
	void SaveEditorLayoutImpl(const std::string& filePath,
	                          const std::vector<AnimGraphNodeLayout>& layouts)
	{
		// 读取已有 JSON
		std::ifstream inFile(filePath);
		if (!inFile.is_open()) return;

		json root;
		try { root = json::parse(inFile); }
		catch (const json::parse_error&) { return; }
		inFile.close();

		// 构建 editor 字段
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

		// 写回文件
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
	SaveEditorLayoutImpl(filePath, m_EditState.nodeLayouts);
}
