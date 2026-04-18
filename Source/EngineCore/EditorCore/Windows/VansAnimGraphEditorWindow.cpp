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

	// 从 Controller 加载编辑数据
	LoadEditStateFromController(controller);

	// 检测 v2 图模式
	m_IsV2Graph   = controller->HasGraph();
	m_TargetGraph = controller->GetGraph();

	// 尝试从 .vanimator 加载布局数据（仅 v1 使用）
	if (!m_IsV2Graph)
	{
		if (!LoadLayoutFromFile(m_AnimatorFilePath))
			AutoLayout();
	}

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
	// 保存当前节点位置到 nodeLayouts
	if (m_NodeEditorCtx)
	{
		ne::SetCurrentEditor(m_NodeEditorCtx);
		if (m_IsV2Graph)
			ReadV2NodePositions();
		else
			ReadNodePositions();
		ne::SetCurrentEditor(nullptr);
		ne::DestroyEditor(m_NodeEditorCtx);
		m_NodeEditorCtx = nullptr;
	}

	m_TargetController = nullptr;
	m_TargetNode       = nullptr;
	m_TargetGraph      = nullptr;
	m_IsV2Graph        = false;
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
		if (m_IsV2Graph)
			ApplyV2NodePositions();
		else
			ApplyNodePositions();
		m_EditState.needsInitialLayout = false;
		ne::NavigateToContent(0.0f);
	}

	if (m_IsV2Graph)
	{
		DrawV2GraphCanvas();
	}
	else
	{
		// v1 FSM 模式
		DrawAnyStateNode();
		for (int i = 0; i < (int)m_EditState.states.size(); ++i)
			DrawStateNode(i);
		DrawLinks();

		HandleCreateInteraction();
		HandleDeleteInteraction();
		HandleContextMenus();
		SyncSelection();
	}

	ne::End();

	ne::SetCurrentEditor(nullptr);
}

// ════════════════════════════════════════════════════════════════
//  节点绘制
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::DrawAnyStateNode()
{
	ne::NodeId nodeId(AnimGraphIds::AnyStateNodeId);

	ne::BeginNode(nodeId);

		ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 200, 100, 255));
		ImGui::Text("* Any State");
		ImGui::PopStyleColor();

		ImGui::Dummy(ImVec2(120, 0));

		// AnyState 只有 Output Pin
		ne::BeginPin(
			AnimGraphIds::MakeOutputPin(AnimGraphIds::AnyStateNodeId),
			ne::PinKind::Output);
			ImGui::Text("Out >>");
		ne::EndPin();

	ne::EndNode();
}

void VansAnimGraphEditorWindow::DrawStateNode(int stateIndex)
{
	auto& state = m_EditState.states[stateIndex];
	int nodeIdRaw = AnimGraphIds::StateNodeBase + stateIndex;
	ne::NodeId nodeId(nodeIdRaw);

	bool isDefault = (state.name == m_EditState.defaultStateName);
	bool isActive  = m_TargetController &&
	                 (state.name == m_TargetController->GetCurrentStateName());

	// 节点绘制
	ne::BeginNode(nodeId);

		// Header（通过不同文字颜色区分类型）
		ImU32 headerColor = IM_COL32(48, 112, 176, 255);  // 普通蓝
		if (isDefault)
			headerColor = IM_COL32(48, 176, 112, 255);    // 默认绿

		ImGui::PushStyleColor(ImGuiCol_Text, headerColor);
		ImGui::Text("%s", state.name.c_str());
		ImGui::PopStyleColor();

		ImGui::Dummy(ImVec2(140, 0));  // 最小宽度

		// Clip 信息
		ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Clip: %s", state.clipName.c_str());

		// 属性行
		char info[64];
		snprintf(info, sizeof(info), "%.1fx  %s  %s",
		         state.speed,
		         state.loop ? "loop" : "once",
		         state.rootMotion ? "RM" : "");
		ImGui::TextColored(ImVec4(0.55f, 0.55f, 0.55f, 0.8f), "%s", info);

		// 运行时进度条
		if (isActive)
		{
			float progress = m_TargetController->GetNormalizedTime();
			ImGui::ProgressBar(progress, ImVec2(140, 4), "");
		}

		// Input Pin
		ne::BeginPin(AnimGraphIds::MakeInputPin(nodeIdRaw), ne::PinKind::Input);
			ImGui::Text(">> In");
		ne::EndPin();

		ImGui::SameLine(120);

		// Output Pin
		ne::BeginPin(AnimGraphIds::MakeOutputPin(nodeIdRaw), ne::PinKind::Output);
			ImGui::Text("Out >>");
		ne::EndPin();

	ne::EndNode();

	// 活跃状态额外高亮边框
	if (isActive)
	{
		ImDrawList* dl = ne::GetNodeBackgroundDrawList(nodeId);
		if (dl)
		{
			ImVec2 nodePos  = ne::GetNodePosition(nodeId);
			ImVec2 nodeSize = ne::GetNodeSize(nodeId);
			dl->AddRect(nodePos, ImVec2(nodePos.x + nodeSize.x, nodePos.y + nodeSize.y),
			            IM_COL32(100, 255, 100, 200), 6.0f, 0, 3.0f);
		}
	}
}

// ════════════════════════════════════════════════════════════════
//  连线绘制
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::DrawLinks()
{
	for (int i = 0; i < (int)m_EditState.transitions.size(); ++i)
	{
		auto& trans = m_EditState.transitions[i];

		int fromNodeIdRaw = (trans.fromState == "*")
			? AnimGraphIds::AnyStateNodeId
			: AnimGraphIds::StateNodeBase + GetStateIndex(trans.fromState);
		int toNodeIdRaw = AnimGraphIds::StateNodeBase + GetStateIndex(trans.toState);

		ne::PinId startPin = AnimGraphIds::MakeOutputPin(fromNodeIdRaw);
		ne::PinId endPin   = AnimGraphIds::MakeInputPin(toNodeIdRaw);
		ne::LinkId linkId  = AnimGraphIds::MakeLinkId(i);

		// 颜色: 条件为空 = 灰色，有条件 = 白色，exitTime = 黄色
		ImVec4 linkColor = trans.conditions.empty()
			? (trans.hasExitTime ? ImVec4(1.0f, 0.8f, 0.3f, 1.0f) : ImVec4(0.6f, 0.6f, 0.6f, 1.0f))
			: ImVec4(1.0f, 1.0f, 1.0f, 1.0f);

		ne::Link(linkId, startPin, endPin, linkColor, 2.5f);

		// 正在过渡时显示 Flow 动画
		if (m_TargetController &&
			m_TargetController->GetPlaybackState() == AnimationState::Blending)
		{
			std::string curState = m_TargetController->GetCurrentStateName();
			if (trans.toState == curState)
			{
				ne::Flow(linkId, ne::FlowDirection::Forward);
			}
		}
	}
}

// ════════════════════════════════════════════════════════════════
//  连线创建交互
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::HandleCreateInteraction()
{
	if (ne::BeginCreate())
	{
		ne::PinId startPinId, endPinId;
		if (ne::QueryNewLink(&startPinId, &endPinId))
		{
			if (startPinId && endPinId)
			{
				int startPinRaw = (int)startPinId.Get();
				int endPinRaw   = (int)endPinId.Get();

				bool startIsOutput = AnimGraphIds::IsOutputPin(startPinRaw);
				bool endIsInput    = AnimGraphIds::IsInputPin(endPinRaw);

				// 反向拖拽也允许，交换
				if (!startIsOutput || !endIsInput)
				{
					if (AnimGraphIds::IsOutputPin(endPinRaw) &&
						AnimGraphIds::IsInputPin(startPinRaw))
					{
						std::swap(startPinRaw, endPinRaw);
						startIsOutput = true;
						endIsInput = true;
					}
					else
					{
						ne::RejectNewItem(ImVec4(1, 0, 0, 1), 2.0f);
					}
				}

				if (startIsOutput && endIsInput)
				{
					int fromNodeRaw = AnimGraphIds::PinIdToNodeIdRaw(startPinRaw);
					int toNodeRaw   = AnimGraphIds::PinIdToNodeIdRaw(endPinRaw);

					// 不允许自连接
					if (fromNodeRaw == toNodeRaw)
					{
						ne::RejectNewItem(ImVec4(1, 0, 0, 1), 2.0f);
					}
					// 不允许连到 AnyState
					else if (toNodeRaw == AnimGraphIds::AnyStateNodeId)
					{
						ne::RejectNewItem(ImVec4(1, 0, 0, 1), 2.0f);
					}
					else if (ne::AcceptNewItem(ImVec4(0, 1, 0, 1), 2.0f))
					{
						std::string fromState = (fromNodeRaw == AnimGraphIds::AnyStateNodeId)
							? "*"
							: m_EditState.states[AnimGraphIds::NodeIdToStateIndex(fromNodeRaw)].name;
						std::string toState =
							m_EditState.states[AnimGraphIds::NodeIdToStateIndex(toNodeRaw)].name;

						// 检查重复
						bool exists = false;
						for (auto& t : m_EditState.transitions)
						{
							if (t.fromState == fromState && t.toState == toState)
							{ exists = true; break; }
						}

						if (!exists)
						{
							AnimatorTransition newTrans;
							newTrans.fromState     = fromState;
							newTrans.toState       = toState;
							newTrans.blendDuration = 0.2f;
							m_EditState.transitions.push_back(newTrans);
							m_EditState.isDirty = true;
						}
					}
				}
			}
		}

		// 从 pin 拖出到空白处 → 弹出添加新节点菜单
		ne::PinId pinId;
		if (ne::QueryNewNode(&pinId))
		{
			if (ne::AcceptNewItem())
			{
				m_CreateNewNodeFromPin = (int)pinId.Get();
				ne::Suspend();
				ImGui::OpenPopup("CreateNewStateFromPin");
				ne::Resume();
			}
		}
	}
	ne::EndCreate();
}

// ════════════════════════════════════════════════════════════════
//  删除交互
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::HandleDeleteInteraction()
{
	if (ne::BeginDelete())
	{
		// 查询要删除的连线
		ne::LinkId deletedLinkId;
		while (ne::QueryDeletedLink(&deletedLinkId))
		{
			if (ne::AcceptDeletedItem())
			{
				int transIndex = AnimGraphIds::LinkIdToTransIndex((int)deletedLinkId.Get());
				if (transIndex >= 0 && transIndex < (int)m_EditState.transitions.size())
				{
					m_EditState.transitions.erase(
						m_EditState.transitions.begin() + transIndex);
					m_EditState.isDirty = true;
				}
			}
		}

		// 查询要删除的节点
		ne::NodeId deletedNodeId;
		while (ne::QueryDeletedNode(&deletedNodeId))
		{
			int nodeIdRaw = (int)deletedNodeId.Get();

			// AnyState 不可删除
			if (nodeIdRaw == AnimGraphIds::AnyStateNodeId)
			{
				ne::RejectDeletedItem();
				continue;
			}

			if (ne::AcceptDeletedItem())
			{
				int stateIdx = AnimGraphIds::NodeIdToStateIndex(nodeIdRaw);
				if (stateIdx >= 0 && stateIdx < (int)m_EditState.states.size())
				{
					DeleteState(stateIdx);
				}
			}
		}
	}
	ne::EndDelete();
}

// ════════════════════════════════════════════════════════════════
//  右键菜单
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::HandleContextMenus()
{
	// 需要 Suspend 才能使用 ImGui popup
	ne::Suspend();

	ne::NodeId contextNodeId;
	ne::LinkId contextLinkId;

	if (ne::ShowNodeContextMenu(&contextNodeId))
	{
		m_ContextNodeId = (int)contextNodeId.Get();
		ImGui::OpenPopup("NodeContextMenu");
	}
	else if (ne::ShowLinkContextMenu(&contextLinkId))
	{
		m_ContextLinkId = (int)contextLinkId.Get();
		ImGui::OpenPopup("LinkContextMenu");
	}
	else if (ne::ShowBackgroundContextMenu())
	{
		ImGui::OpenPopup("BackgroundContextMenu");
	}

	// 节点右键菜单
	if (ImGui::BeginPopup("NodeContextMenu"))
	{
		int nodeIdRaw = m_ContextNodeId;

		if (nodeIdRaw != AnimGraphIds::AnyStateNodeId)
		{
			int stateIdx = AnimGraphIds::NodeIdToStateIndex(nodeIdRaw);
			if (stateIdx >= 0 && stateIdx < (int)m_EditState.states.size())
			{
				auto& state = m_EditState.states[stateIdx];

				if (ImGui::MenuItem("Set as Default State"))
				{
					m_EditState.defaultStateName = state.name;
					m_EditState.isDirty = true;
				}
				if (ImGui::MenuItem("Delete State"))
					DeleteState(stateIdx);
			}
		}
		else
		{
			ImGui::TextDisabled("Any State (cannot delete)");
		}
		ImGui::EndPopup();
	}

	// 连线右键菜单
	if (ImGui::BeginPopup("LinkContextMenu"))
	{
		int transIdx = AnimGraphIds::LinkIdToTransIndex(m_ContextLinkId);
		if (transIdx >= 0 && transIdx < (int)m_EditState.transitions.size())
		{
			auto& trans = m_EditState.transitions[transIdx];
			ImGui::Text("%s -> %s", trans.fromState.c_str(), trans.toState.c_str());
			ImGui::Separator();
			if (ImGui::MenuItem("Delete Transition"))
			{
				m_EditState.transitions.erase(m_EditState.transitions.begin() + transIdx);
				m_EditState.isDirty = true;
			}
		}
		ImGui::EndPopup();
	}

	// 画布空白右键菜单
	if (ImGui::BeginPopup("BackgroundContextMenu"))
	{
		if (ImGui::MenuItem("Add State"))
			ImGui::OpenPopup("AddStatePopup");
		if (ImGui::MenuItem("Navigate to Content"))
			ne::NavigateToContent();
		ImGui::EndPopup();
	}

	// 从 pin 拖到空白处创建新 state 弹窗
	if (ImGui::BeginPopup("CreateNewStateFromPin"))
	{
		static char newStateName[128] = "";
		ImGui::InputText("State Name", newStateName, sizeof(newStateName));

		if (ImGui::Button("Create") && strlen(newStateName) > 0)
		{
			AddState(newStateName, "");

			// 如果是从 output pin 拖出，自动创建连线
			if (m_CreateNewNodeFromPin > 0 && AnimGraphIds::IsOutputPin(m_CreateNewNodeFromPin))
			{
				int fromNodeRaw = AnimGraphIds::PinIdToNodeIdRaw(m_CreateNewNodeFromPin);
				std::string fromState = (fromNodeRaw == AnimGraphIds::AnyStateNodeId)
					? "*"
					: m_EditState.states[AnimGraphIds::NodeIdToStateIndex(fromNodeRaw)].name;

				AnimatorTransition newTrans;
				newTrans.fromState     = fromState;
				newTrans.toState       = newStateName;
				newTrans.blendDuration = 0.2f;
				m_EditState.transitions.push_back(newTrans);
			}

			m_EditState.isDirty = true;
			newStateName[0] = '\0';
			m_CreateNewNodeFromPin = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::EndPopup();
	}

	ne::Resume();
}

// ════════════════════════════════════════════════════════════════
//  选择同步
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::SyncSelection()
{
	if (!ne::HasSelectionChanged()) return;

	m_EditState.selectedStateIndex = -1;
	m_EditState.selectedTransIndex = -1;

	// 查询选中的节点
	int selCount = ne::GetSelectedObjectCount();
	if (selCount > 0)
	{
		std::vector<ne::NodeId> selectedNodes(selCount);
		int count = ne::GetSelectedNodes(selectedNodes.data(), selCount);
		if (count > 0)
		{
			int nodeIdRaw = (int)selectedNodes[0].Get();
			if (nodeIdRaw == AnimGraphIds::AnyStateNodeId)
				m_EditState.selectedStateIndex = -2;  // AnyState
			else
				m_EditState.selectedStateIndex = AnimGraphIds::NodeIdToStateIndex(nodeIdRaw);
		}

		// 查询选中的连线
		std::vector<ne::LinkId> selectedLinks(selCount);
		int linkCount = ne::GetSelectedLinks(selectedLinks.data(), selCount);
		if (linkCount > 0)
		{
			m_EditState.selectedTransIndex = AnimGraphIds::LinkIdToTransIndex(
				(int)selectedLinks[0].Get());
		}
	}
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
	if (m_IsV2Graph)
		DrawV2PropertiesPanel();
	else
		DrawPropertiesPanel();
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

void VansAnimGraphEditorWindow::DrawPropertiesPanel()
{
	ImGui::Text("Properties");
	ImGui::Separator();

	if (m_EditState.selectedStateIndex >= 0)
	{
		// 选中了一个 State 节点
		auto& state = m_EditState.states[m_EditState.selectedStateIndex];

		char nameBuf[128];
		strncpy(nameBuf, state.name.c_str(), sizeof(nameBuf));
		nameBuf[sizeof(nameBuf) - 1] = '\0';
		if (ImGui::InputText("State Name", nameBuf, sizeof(nameBuf)))
		{
			RenameState(state.name, nameBuf);
			state.name = nameBuf;
			m_EditState.isDirty = true;
		}

		// Clip 下拉
		if (ImGui::BeginCombo("Clip", state.clipName.c_str()))
		{
			for (auto& ref : m_EditState.clipRefs)
			{
				bool sel = (state.clipName == ref.name);
				if (ImGui::Selectable(ref.name.c_str(), sel))
				{
					state.clipName = ref.name;
					m_EditState.isDirty = true;
				}
			}
			ImGui::EndCombo();
		}

		if (ImGui::DragFloat("Speed", &state.speed, 0.01f, 0.0f, 10.0f))
			m_EditState.isDirty = true;
		if (ImGui::Checkbox("Loop", &state.loop))
			m_EditState.isDirty = true;
		if (ImGui::Checkbox("Root Motion", &state.rootMotion))
			m_EditState.isDirty = true;
	}
	else if (m_EditState.selectedStateIndex == -2)
	{
		ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "* Any State");
		ImGui::TextDisabled("Global transition source. Cannot be edited or deleted.");
	}
	else if (m_EditState.selectedTransIndex >= 0)
	{
		// 选中了一条 Transition 连线
		auto& trans = m_EditState.transitions[m_EditState.selectedTransIndex];

		ImGui::Text("%s -> %s", trans.fromState.c_str(), trans.toState.c_str());
		if (ImGui::DragFloat("Blend Duration", &trans.blendDuration, 0.01f, 0.0f, 5.0f))
			m_EditState.isDirty = true;
		if (ImGui::Checkbox("Has Exit Time", &trans.hasExitTime))
			m_EditState.isDirty = true;
		if (trans.hasExitTime)
		{
			if (ImGui::DragFloat("Exit Time", &trans.exitTime, 0.01f, 0.0f, 1.0f))
				m_EditState.isDirty = true;
		}

		ImGui::Separator();
		ImGui::Text("Conditions (AND):");
		DrawTransitionConditions(trans);
	}
	else
	{
		ImGui::TextDisabled("Select a node or link to edit properties.");
	}
}

// ════════════════════════════════════════════════════════════════
//  Transition Conditions 编辑
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::DrawTransitionConditions(AnimatorTransition& trans)
{
	for (int i = 0; i < (int)trans.conditions.size(); ++i)
	{
		auto& cond = trans.conditions[i];
		ImGui::PushID(i + 2000);

		// Parameter 选择下拉
		ImGui::SetNextItemWidth(100.0f);
		if (ImGui::BeginCombo("##param", cond.paramName.c_str()))
		{
			for (auto& param : m_EditState.parameters)
			{
				if (ImGui::Selectable(param.name.c_str(), cond.paramName == param.name))
				{
					cond.paramName = param.name;
					m_EditState.isDirty = true;
				}
			}
			ImGui::EndCombo();
		}

		// 操作符选择
		ImGui::SameLine();
		const char* opLabels[] = { ">", "<", "==", "!=", ">=", "<=" };
		ImGui::SetNextItemWidth(50.0f);
		int opIdx = (int)cond.op;
		if (ImGui::Combo("##op", &opIdx, opLabels, IM_ARRAYSIZE(opLabels)))
		{
			cond.op = (CompareOp)opIdx;
			m_EditState.isDirty = true;
		}

		// 值编辑（根据参数类型）
		ImGui::SameLine();
		AnimatorParamType paramType = FindParamType(cond.paramName);
		ImGui::SetNextItemWidth(80.0f);
		switch (paramType)
		{
		case AnimatorParamType::Float:
			if (ImGui::DragFloat("##val", &cond.floatVal, 0.01f))
				m_EditState.isDirty = true;
			break;
		case AnimatorParamType::Bool:
		case AnimatorParamType::Trigger:
			if (ImGui::Checkbox("##val", &cond.boolVal))
				m_EditState.isDirty = true;
			break;
		case AnimatorParamType::Int:
			if (ImGui::InputInt("##val", &cond.intVal))
				m_EditState.isDirty = true;
			break;
		}

		// 删除条件
		ImGui::SameLine();
		if (ImGui::SmallButton("X"))
		{
			trans.conditions.erase(trans.conditions.begin() + i);
			m_EditState.isDirty = true;
			--i;
		}

		ImGui::PopID();
	}

	if (ImGui::SmallButton("+ Add Condition"))
	{
		TransitionCondition cond;
		if (!m_EditState.parameters.empty())
			cond.paramName = m_EditState.parameters[0].name;
		trans.conditions.push_back(cond);
		m_EditState.isDirty = true;
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
		if (ImGui::BeginMenu("Edit"))
		{
			if (ImGui::MenuItem("Add State"))
				ImGui::OpenPopup("AddStateFromMenu");
			if (ImGui::MenuItem("Auto Layout"))
			{
				AutoLayout();
				ApplyNodePositions();
				ne::NavigateToContent();
			}
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

	// 默认状态选择
	ImGui::Text("Default State:");
	ImGui::SameLine();
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::BeginCombo("##defState", m_EditState.defaultStateName.c_str()))
	{
		for (auto& s : m_EditState.states)
		{
			if (ImGui::Selectable(s.name.c_str(), s.name == m_EditState.defaultStateName))
			{
				m_EditState.defaultStateName = s.name;
				m_EditState.isDirty = true;
			}
		}
		ImGui::EndCombo();
	}

	ImGui::SameLine(500);
	if (m_IsV2Graph && m_TargetGraph)
	{
		ImGui::Text("Nodes: %d  Links: %d  [v2 Graph]",
		            (int)m_TargetGraph->GetNodes().size(),
		            (int)m_TargetGraph->GetLinks().size());
	}
	else
	{
		ImGui::Text("States: %d  Transitions: %d",
		            (int)m_EditState.states.size(),
		            (int)m_EditState.transitions.size());
	}
}

// ════════════════════════════════════════════════════════════════
//  编辑操作
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::AddState(const std::string& name, const std::string& clipName)
{
	// 检查重名
	for (auto& s : m_EditState.states)
	{
		if (s.name == name)
		{
			VANS_LOG_WARN("[AnimGraphEditor] State already exists: " << name);
			return;
		}
	}

	AnimatorState state;
	state.name     = name;
	state.clipName = clipName;
	state.speed    = 1.0f;
	state.loop     = true;
	m_EditState.states.push_back(state);

	// 添加布局信息
	AnimGraphNodeLayout layout;
	layout.stateName = name;
	layout.position  = ImVec2(300.0f, 300.0f);
	m_EditState.nodeLayouts.push_back(layout);

	// 如果是第一个 state，自动设为默认
	if (m_EditState.states.size() == 1)
		m_EditState.defaultStateName = name;

	m_EditState.isDirty = true;
}

void VansAnimGraphEditorWindow::DeleteState(size_t stateIndex)
{
	if (stateIndex >= m_EditState.states.size()) return;

	std::string stateName = m_EditState.states[stateIndex].name;

	// 删除关联的 transitions
	auto& transitions = m_EditState.transitions;
	transitions.erase(
		std::remove_if(transitions.begin(), transitions.end(),
			[&](const AnimatorTransition& t) {
				return t.fromState == stateName || t.toState == stateName;
			}),
		transitions.end());

	// 删除布局
	auto& layouts = m_EditState.nodeLayouts;
	layouts.erase(
		std::remove_if(layouts.begin(), layouts.end(),
			[&](const AnimGraphNodeLayout& l) { return l.stateName == stateName; }),
		layouts.end());

	// 删除 state
	m_EditState.states.erase(m_EditState.states.begin() + stateIndex);

	// 如果删除的是默认状态，重置
	if (m_EditState.defaultStateName == stateName)
	{
		m_EditState.defaultStateName = m_EditState.states.empty() ? "" : m_EditState.states[0].name;
	}

	m_EditState.isDirty = true;
	m_EditState.selectedStateIndex = -1;
}

void VansAnimGraphEditorWindow::RenameState(const std::string& oldName, const std::string& newName)
{
	if (oldName == newName) return;

	// 更新 transitions 中的引用
	for (auto& t : m_EditState.transitions)
	{
		if (t.fromState == oldName) t.fromState = newName;
		if (t.toState == oldName)   t.toState   = newName;
	}

	// 更新 nodeLayouts
	for (auto& l : m_EditState.nodeLayouts)
	{
		if (l.stateName == oldName) l.stateName = newName;
	}

	// 更新默认状态
	if (m_EditState.defaultStateName == oldName)
		m_EditState.defaultStateName = newName;
}

// ════════════════════════════════════════════════════════════════
//  数据同步
// ════════════════════════════════════════════════════════════════

void VansAnimGraphEditorWindow::LoadEditStateFromController(VansAnimationController* ctrl)
{
	m_EditState = {};
	m_EditState.name             = ctrl->GetName();
	m_EditState.defaultStateName = ctrl->GetDefaultStateName();

	// 拷贝参数
	for (auto& [name, param] : ctrl->GetParameters())
		m_EditState.parameters.push_back(param);

	// 拷贝状态
	for (auto& stateName : ctrl->GetStateNames())
	{
		const AnimatorState* state = ctrl->GetState(stateName);
		if (state)
			m_EditState.states.push_back(*state);
	}

	// 拷贝 transitions
	m_EditState.transitions = ctrl->GetTransitions();

	// 构建 clip refs（从已有的 clipRefs 恢复；若无则仅用名称填充）
	for (auto& [clipName, clip] : ctrl->GetClipsMap())
	{
		AnimatorClipRef ref;
		ref.name = clipName;
		ref.path = "";  // 路径需由 .vanimator 文件加载时提供，此处仅做占位
		m_EditState.clipRefs.push_back(ref);
	}
}

void VansAnimGraphEditorWindow::SyncEditStateToController(VansAnimationController* ctrl)
{
	if (!ctrl) return;

	// 注意: 这里简单地清空并重建 Controller 数据
	// 保留运行时 clips 不变（clip 数据由场景加载器管理）

	// 重建参数
	// 先获取旧参数名列表再删除，避免迭代中修改
	auto oldParams = ctrl->GetParameters();
	for (auto& [name, param] : oldParams)
		ctrl->RemoveParameter(name);

	for (auto& p : m_EditState.parameters)
	{
		ctrl->AddParameter(p.name, p.type);
		switch (p.type)
		{
		case AnimatorParamType::Float:   ctrl->SetFloat(p.name, p.floatVal); break;
		case AnimatorParamType::Bool:    ctrl->SetBool(p.name, p.boolVal);   break;
		case AnimatorParamType::Int:     ctrl->SetInt(p.name, p.intVal);     break;
		case AnimatorParamType::Trigger: break;
		}
	}

	// 重建 states
	auto oldStateNames = ctrl->GetStateNames();
	for (auto& name : oldStateNames)
		ctrl->RemoveState(name);

	for (auto& s : m_EditState.states)
		ctrl->AddState(s);

	// 重建 transitions（先清空）
	// Controller 没有 ClearTransitions, 逐个删除
	auto oldTrans = ctrl->GetTransitions();
	for (auto& t : oldTrans)
		ctrl->RemoveTransition(t.fromState, t.toState);

	for (auto& t : m_EditState.transitions)
		ctrl->AddTransition(t);

	ctrl->SetDefaultState(m_EditState.defaultStateName);
	ctrl->SetName(m_EditState.name);
	ctrl->BindStateClips();
}

void VansAnimGraphEditorWindow::AutoLayout()
{
	float x = 100.0f, y = 100.0f;
	float stepX = 220.0f, stepY = 120.0f;
	int cols = 4;

	m_EditState.nodeLayouts.clear();

	// AnyState 放在最上方
	m_EditState.nodeLayouts.push_back({ "*", ImVec2(x, y - 80.0f) });

	// 各 State
	int idx = 0;
	for (auto& state : m_EditState.states)
	{
		float px = x + (idx % cols) * stepX;
		float py = y + (idx / cols) * stepY;
		m_EditState.nodeLayouts.push_back({ state.name, ImVec2(px, py) });
		++idx;
	}
}

void VansAnimGraphEditorWindow::ApplyNodePositions()
{
	for (auto& layout : m_EditState.nodeLayouts)
	{
		int nodeIdRaw = (layout.stateName == "*")
			? AnimGraphIds::AnyStateNodeId
			: AnimGraphIds::StateNodeBase + GetStateIndex(layout.stateName);

		if (nodeIdRaw >= 0)
			ne::SetNodePosition(ne::NodeId(nodeIdRaw), layout.position);
	}
}

void VansAnimGraphEditorWindow::ReadNodePositions()
{
	m_EditState.nodeLayouts.clear();

	// AnyState
	{
		ImVec2 pos = ne::GetNodePosition(ne::NodeId(AnimGraphIds::AnyStateNodeId));
		m_EditState.nodeLayouts.push_back({ "*", pos });
	}

	// 各 State
	for (int i = 0; i < (int)m_EditState.states.size(); ++i)
	{
		ImVec2 pos = ne::GetNodePosition(AnimGraphIds::MakeNodeId(i));
		m_EditState.nodeLayouts.push_back({ m_EditState.states[i].name, pos });
	}
}

bool VansAnimGraphEditorWindow::LoadLayoutFromFile(const std::string& filePath)
{
	if (filePath.empty()) return false;

	std::ifstream inFile(filePath);
	if (!inFile.is_open()) return false;

	json root;
	try
	{
		root = json::parse(inFile);
	}
	catch (const json::parse_error&)
	{
		return false;
	}

	if (!root.contains("editor")) return false;

	auto& editor = root["editor"];
	if (!editor.contains("nodeLayouts") || !editor["nodeLayouts"].is_array())
		return false;

	m_EditState.nodeLayouts.clear();
	for (auto& item : editor["nodeLayouts"])
	{
		AnimGraphNodeLayout layout;
		layout.stateName = item.value("state", "");
		layout.position  = ImVec2(item.value("x", 0.0f), item.value("y", 0.0f));
		m_EditState.nodeLayouts.push_back(layout);
	}

	return true;
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
//  辅助方法
// ════════════════════════════════════════════════════════════════

int VansAnimGraphEditorWindow::GetStateIndex(const std::string& stateName) const
{
	for (int i = 0; i < (int)m_EditState.states.size(); ++i)
	{
		if (m_EditState.states[i].name == stateName)
			return i;
	}
	return -1;
}

AnimatorParamType VansAnimGraphEditorWindow::FindParamType(const std::string& paramName) const
{
	for (auto& p : m_EditState.parameters)
	{
		if (p.name == paramName)
			return p.type;
	}
	return AnimatorParamType::Float;
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
		if (m_IsV2Graph)
			ReadV2NodePositions();
		else
			ReadNodePositions();
		ne::SetCurrentEditor(nullptr);
	}

	if (m_IsV2Graph)
	{
		// v2 模式：暂不支持编辑保存，仅读取查看
		VANS_LOG_WARN("[AnimGraphEditor] v2 graph save not yet implemented.");
		m_EditState.isDirty = false;
		return;
	}

	// v1 模式：构建临时 Controller 用于序列化
	VansAnimationController tempCtrl;
	tempCtrl.SetName(m_EditState.name);

	for (auto& p : m_EditState.parameters)
		tempCtrl.AddParameter(p.name, p.type);

	for (auto& s : m_EditState.states)
		tempCtrl.AddState(s);

	for (auto& t : m_EditState.transitions)
		tempCtrl.AddTransition(t);

	tempCtrl.SetDefaultState(m_EditState.defaultStateName);

	// 保存到文件（带 editor 布局数据）
	if (!m_AnimatorFilePath.empty())
	{
		if (!VansAnimatorIO::Save(m_AnimatorFilePath, tempCtrl, m_EditState.clipRefs))
		{
			VANS_LOG_WARN("[AnimGraphEditor] Failed to save: " << m_AnimatorFilePath);
			return;
		}

		// 追加写入 editor 布局字段到已保存的 JSON
		SaveEditorLayout(m_AnimatorFilePath);
	}

	// 同步回运行时 Controller
	if (m_TargetController)
		SyncEditStateToController(m_TargetController);

	m_EditState.isDirty = false;
	VANS_LOG("[AnimGraphEditor] Saved: " << m_AnimatorFilePath);
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
