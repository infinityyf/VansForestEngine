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
		std::vector<AnimGraphNodeLayout>    nodeLayouts;

		bool isDirty            = false;
		bool needsInitialLayout = false;

		// v2 图选中的节点 / 连线 ID（-1=无选中）
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
		void DrawGraphCanvas();
		void DrawStatusBar();

		// ─── v2 图绘制 ──────────────────────────────────────────────
		void DrawV2GraphCanvas();
		void DrawV2Node(VansAnimGraphNode* node);
		void DrawV2Links();
		void DrawV2PropertiesPanel();
		void SyncV2Selection();

		// ─── 数据管理 ────────────────────────────────────────────────
		void ApplyV2NodePositions();  // v2: 从 graph 节点的 m_EditorPosX/Y 应用
		void ReadV2NodePositions();   // v2: 从 NodeEditor 读取位置回 graph 节点

		// ─── 辅助 ───────────────────────────────────────────────────

		// ─── 保存/加载 ──────────────────────────────────────────────
		void Save();
		void SaveEditorLayout(const std::string& filePath);
	};

}  // namespace VansGraphics
