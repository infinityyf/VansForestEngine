#pragma once
#include "VansBaseWindowComponent.h"
#include <imgui_node_editor.h>
#include <memory>
#include <string>
#include <vector>
namespace ax { namespace NodeEditor { struct EditorContext; } }
namespace VansGraphics
{
	class VansAnimationController;
	class VansAnimationNode;
	class VansAnimGraph;
	class VansAnimGraphNode;
	struct AnimGraphEditState;
    // ============================================================================
    // AnimGraph 图节点 ID 映射：
    // NodeId 直接使用图内 nodeId。
	// ============================================================================
	//  LinkId = linkId + 1000000
    // ============================================================================
	namespace AnimGraphIds
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
	// ============================================================================
	// ============================================================================
// ============================================================================
	//  VansAnimGraphEditorWindow
	//
	// ============================================================================
	class VansAnimGraphEditorWindow : public VansBaseWindowComponent
	{
	public:
		VansAnimGraphEditorWindow();
		~VansAnimGraphEditorWindow();
		void Open(VansAnimationController* controller, VansAnimationNode* node);
		void Close();
		bool IsOpen() const { return m_IsOpen; }
		// VansBaseWindowComponent 鎺ュ彛
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI&) override;
	private:
		struct NavigationFrame
		{
			int nodeId = -1;
			std::string title;
		};
		bool m_IsOpen = false;
		VansAnimationController* m_TargetController = nullptr;
		VansAnimationNode*       m_TargetNode       = nullptr;
		std::string              m_AnimatorFilePath;
		VansAnimGraph*  m_TargetGraph = nullptr;
		std::unique_ptr<AnimGraphEditState> m_EditState;
		ax::NodeEditor::EditorContext* m_NodeEditorCtx = nullptr;
		int m_ContextNodeId        = -1;
		int m_ContextLinkId        = -1;
		int m_CreateNewNodeFromPin = -1;
		std::vector<NavigationFrame> m_NavigationStack;
		void DrawMenuBar();
		void DrawLeftPanel();
		void DrawParametersPanel();
		void DrawClipsPanel();
		void DrawGraphCanvas();
		void DrawStatusBar();
		void DrawNavigationBar();
		void DrawGraphEditorCanvas();
		void DrawSubgraphPreviewCanvas();
		void DrawStateMachineSubgraphPreview(VansAnimGraphNode* node);
		void DrawMotionMatchingSubgraphPreview(VansAnimGraphNode* node);
		void DrawGraphNode(VansAnimGraphNode* node);
		void DrawGraphLinks();
		void DrawPropertiesPanel();
		void SyncSelection();
		bool IsRootGraphView() const { return m_NavigationStack.empty(); }
		bool CanOpenNodeSubgraph(VansAnimGraphNode* node) const;
		void OpenNodeSubgraph(int nodeId);
		void NavigateBack();
		void ApplyNodePositions();
		void ReadNodePositions();
		void Save();
		void SaveEditorLayout(const std::string& filePath);
	};
}  // namespace VansGraphics
