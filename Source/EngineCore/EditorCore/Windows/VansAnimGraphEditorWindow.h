#pragma once

#include "VansBaseWindowComponent.h"
#include <imgui_node_editor.h>
#include "../../AnimationCore/VansAnimationController.h"
#include "../../AnimationCore/VansAnimatorIO.h"
#include "../../AnimationCore/VansAnimGraph.h"
#include <string>
#include <vector>

namespace ax { namespace NodeEditor { struct EditorContext; } }

namespace VansGraphics
{
	class VansAnimationNode;

	// ════════════════════════════════════════════════════════════════
	//  ID 编码辅助
	//
	//  imgui-node-editor 使用强类型 ID，底层为 uintptr_t。
	//  采用分段编码避免冲突：
	//    NodeId: 1=AnyState, 1000+stateIndex=各 State
	//    PinId:  nodeId*10+1=Input, nodeId*10+2=Output
	//    LinkId: 5000+transitionIndex
	// ════════════════════════════════════════════════════════════════

	namespace AnimGraphIds
	{
		constexpr int AnyStateNodeId = 1;
		constexpr int StateNodeBase  = 1000;
		constexpr int LinkIdBase     = 5000;

		inline ax::NodeEditor::NodeId MakeNodeId(int stateIndex)
		{
			return ax::NodeEditor::NodeId(StateNodeBase + stateIndex);
		}
		inline ax::NodeEditor::NodeId AnyStateNode()
		{
			return ax::NodeEditor::NodeId(AnyStateNodeId);
		}
		inline ax::NodeEditor::PinId MakeInputPin(int nodeIdRaw)
		{
			return ax::NodeEditor::PinId(nodeIdRaw * 10 + 1);
		}
		inline ax::NodeEditor::PinId MakeOutputPin(int nodeIdRaw)
		{
			return ax::NodeEditor::PinId(nodeIdRaw * 10 + 2);
		}
		inline ax::NodeEditor::LinkId MakeLinkId(int transIndex)
		{
			return ax::NodeEditor::LinkId(LinkIdBase + transIndex);
		}

		// 反向解析
		inline int NodeIdToStateIndex(int nodeIdRaw) { return nodeIdRaw - StateNodeBase; }
		inline int LinkIdToTransIndex(int linkIdRaw) { return linkIdRaw - LinkIdBase; }
		inline int PinIdToNodeIdRaw(int pinIdRaw)    { return pinIdRaw / 10; }
		inline bool IsInputPin(int pinIdRaw)         { return (pinIdRaw % 10) == 1; }
		inline bool IsOutputPin(int pinIdRaw)        { return (pinIdRaw % 10) == 2; }
	}

	// ════════════════════════════════════════════════════════════════
	//  v2 AnimGraph ID 编码辅助
	//
	//  v2 图节点直接使用图内 nodeId 作 NodeId。
	//  PinId  = nodeId * 1000 + (isOutput ? 500 : 0) + pinIndex + 1
	//  LinkId = linkId + 1000000
	// ════════════════════════════════════════════════════════════════

	namespace AnimGraphV2Ids
	{
		inline ax::NodeEditor::NodeId MakeNodeId(int nodeId)
		{
			return ax::NodeEditor::NodeId(nodeId);
		}
		inline ax::NodeEditor::PinId MakeInputPin(int nodeId, int pinIndex)
		{
			return ax::NodeEditor::PinId(nodeId * 1000 + pinIndex + 1);
		}
		inline ax::NodeEditor::PinId MakeOutputPin(int nodeId, int pinIndex)
		{
			return ax::NodeEditor::PinId(nodeId * 1000 + 500 + pinIndex + 1);
		}
		inline ax::NodeEditor::LinkId MakeLinkId(int linkId)
		{
			return ax::NodeEditor::LinkId(1000000 + linkId);
		}
	}

	// ════════════════════════════════════════════════════════════════
	//  编辑状态
	// ════════════════════════════════════════════════════════════════

	struct AnimGraphNodeLayout
	{
		std::string stateName;
		ImVec2      position = ImVec2(0, 0);
	};

	struct AnimGraphEditState
	{
		std::string                         name;
		std::vector<AnimatorParameter>      parameters;
		std::vector<AnimatorClipRef>        clipRefs;
		std::vector<AnimatorState>          states;
		std::vector<AnimatorTransition>     transitions;
		std::string                         defaultStateName;
		std::vector<AnimGraphNodeLayout>    nodeLayouts;

		bool isDirty            = false;
		int  selectedStateIndex = -1;   // -1=none, -2=AnyState
		int  selectedTransIndex = -1;
		bool needsInitialLayout = false;

		// v2 图模式下选中的节点 / 连线 ID（-1=无选中）
		int  selectedV2NodeId   = -1;
		int  selectedV2LinkId   = -1;
	};

	// ════════════════════════════════════════════════════════════════
	//  VansAnimGraphEditorWindow
	//
	//  基于 imgui-node-editor 的可视化动画状态机编辑窗口。
	//  从 Animation Inspector 通过按钮打开。
	// ════════════════════════════════════════════════════════════════

	class VansAnimGraphEditorWindow : public VansBaseWindowComponent
	{
	public:
		VansAnimGraphEditorWindow();
		~VansAnimGraphEditorWindow();

		// 打开编辑器，传入目标 Controller 和 AnimationNode
		void Open(VansAnimationController* controller, VansAnimationNode* node);
		void Close();
		bool IsOpen() const { return m_IsOpen; }

		// VansBaseWindowComponent 接口
		void ShowWindow(VansVKDevice& device) override;

	private:
		bool m_IsOpen = false;

		// 编辑目标
		VansAnimationController* m_TargetController = nullptr;
		VansAnimationNode*       m_TargetNode       = nullptr;
		std::string              m_AnimatorFilePath;

		// v2 图模式标记
		bool            m_IsV2Graph   = false;
		VansAnimGraph*  m_TargetGraph = nullptr;   // 指向 controller 中的图，不拥有

		// 编辑数据
		AnimGraphEditState       m_EditState;

		// imgui-node-editor 上下文
		ax::NodeEditor::EditorContext* m_NodeEditorCtx = nullptr;

		// 右键菜单上下文
		int m_ContextNodeId        = -1;
		int m_ContextLinkId        = -1;
		int m_CreateNewNodeFromPin = -1;  // 拖线到空白处时记录源 pin

		// ─── UI 绘制 ────────────────────────────────────────────────
		void DrawMenuBar();
		void DrawLeftPanel();
		void DrawParametersPanel();
		void DrawClipsPanel();
		void DrawPropertiesPanel();
		void DrawGraphCanvas();
		void DrawStatusBar();
		void DrawTransitionConditions(AnimatorTransition& trans);

		// ─── v1 节点/连线绘制（immediate mode）────────────────────
		void DrawAnyStateNode();
		void DrawStateNode(int stateIndex);
		void DrawLinks();

		// ─── v2 图绘制 ──────────────────────────────────────────────
		void DrawV2GraphCanvas();
		void DrawV2Node(VansAnimGraphNode* node);
		void DrawV2Links();
		void DrawV2PropertiesPanel();
		void SyncV2Selection();

		// ─── 交互处理 ────────────────────────────────────────────────
		void HandleCreateInteraction();
		void HandleDeleteInteraction();
		void HandleContextMenus();
		void SyncSelection();

		// ─── 编辑操作 ────────────────────────────────────────────────
		void AddState(const std::string& name, const std::string& clipName);
		void DeleteState(size_t stateIndex);
		void RenameState(const std::string& oldName, const std::string& newName);

		// ─── 数据同步 ────────────────────────────────────────────────
		void LoadEditStateFromController(VansAnimationController* ctrl);
		void SyncEditStateToController(VansAnimationController* ctrl);
		void AutoLayout();
		void ApplyNodePositions();    // v1: 将 nodeLayouts 应用到 NodeEditor
		void ReadNodePositions();     // v1: 从 NodeEditor 读取节点位置回 nodeLayouts
		bool LoadLayoutFromFile(const std::string& filePath);
		void ApplyV2NodePositions();  // v2: 从 graph 节点的 m_EditorPosX/Y 应用
		void ReadV2NodePositions();   // v2: 从 NodeEditor 读取位置回 graph 节点

		// ─── 辅助 ───────────────────────────────────────────────────
		int  GetStateIndex(const std::string& stateName) const;
		AnimatorParamType FindParamType(const std::string& paramName) const;

		// ─── 保存/加载 ──────────────────────────────────────────────
		void Save();
		void SaveEditorLayout(const std::string& filePath);
	};

}  // namespace VansGraphics
