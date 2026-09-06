#pragma once
#include "VansBaseWindowComponent.h"
#include "../../EngineAPILayer/Public/EngineDTOs.h"
#include "../../EngineAPILayer/Public/AnimationClipEventAuthoring.h"
#include <imgui_node_editor.h>
#include <memory>
#include <cstdint>
#include <string>
#include <vector>
namespace ax { namespace NodeEditor { struct EditorContext; } }
namespace Vans { struct VansOpenAssetDocument; }
namespace Vans::EditorAPI { class IEngineEditorAPI; }
namespace VansGraphics
{
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
		void Open(const std::string& animatorFilePath);
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
		bool m_CloseRequested = false;
		bool m_NeedsDecode = false;
		std::string              m_AnimatorFilePath;
		std::string              m_LastError;
		Vans::EditorAPI::AnimationClipEventDocument m_ClipEvents;
		bool m_ClipEventsDirty = false;
		std::unique_ptr<Vans::EditorAPI::AnimatorDocumentDTO> m_AssetData;
		std::shared_ptr<Vans::VansOpenAssetDocument> m_Document;
		std::uint64_t m_DocumentStateId = 0;
		Vans::EditorAPI::IEngineEditorAPI* m_ActiveAPI = nullptr;
		Vans::EditorAPI::AnimationGraphDTO*  m_TargetGraph = nullptr;
		std::string m_ActiveGraphId;
		std::unique_ptr<AnimGraphEditState> m_EditState;
		ax::NodeEditor::EditorContext* m_NodeEditorCtx = nullptr;
		int m_ContextNodeId        = -1;
		int m_ContextLinkId        = -1;
		int m_CreateNewNodeFromPin = -1;
		int m_SelectedStateIndex = -1;
		int m_SelectedTransitionIndex = -1;
		int m_SelectedConditionIndex = -1;
		std::vector<NavigationFrame> m_NavigationStack;
		void DrawMenuBar();
		void DrawLeftPanel();
		void DrawLayersPanel();
		void DrawSlotsPanel();
		bool IsEditingTargetPostProcessGraph() const;
		void DrawParametersPanel();
		void DrawClipsPanel();
		void DrawGraphCanvas();
		void DrawStatusBar();
		void DrawNavigationBar();
		void DrawGraphEditorCanvas();
		void DrawSubgraphPreviewCanvas();
		void DrawStateMachineSubgraphPreview(Vans::EditorAPI::AnimationNodeDTO* node);
		void DrawMotionMatchingSubgraphPreview(Vans::EditorAPI::AnimationNodeDTO* node);
		void DrawGraphNode(Vans::EditorAPI::AnimationNodeDTO* node);
		void DrawGraphLinks();
		void DrawPropertiesPanel();
		void SyncSelection();
		bool IsRootGraphView() const { return m_NavigationStack.empty(); }
		bool CanOpenNodeSubgraph(Vans::EditorAPI::AnimationNodeDTO* node) const;
		void OpenNodeSubgraph(int nodeId);
		void NavigateBack();
		void ApplyNodePositions();
		void ReadNodePositions();
		bool CommitWorkingCopyToDocument();
		bool ReloadWorkingCopyFromDocument();
		bool Undo();
		bool Redo();
		bool Save();
		void CloseImmediately();
	};
}  // namespace VansGraphics
