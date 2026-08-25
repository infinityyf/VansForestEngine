#pragma once
#include "VansBaseWindowComponent.h"
#include "../../EngineAPILayer/Public/EngineDTOs.h"
#include <imgui_node_editor.h>
#include <memory>
#include <cstdint>
#include <array>
#include <string>
#include <unordered_map>
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
		void DrawPreviewPanel();
		void EnsurePreviewSession();
		void QueuePreviewCompile();
		void UpdatePreviewDefinition();
		void DestroyPreviewSession();
		void ResetPreviewParameters();
		void ReconcilePreviewParameters();
		void ApplyPreviewParameters();
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

		Vans::EditorAPI::AnimationPreviewSessionId m_PreviewSessionId = 0;
		std::string m_PreviewSessionTargetKey;
		Vans::EditorAPI::AnimationPreviewTargetKind m_PreviewTargetKind =
			Vans::EditorAPI::AnimationPreviewTargetKind::IsolatedModel;
		std::string m_PreviewSceneEntityGuid;
		std::string m_PreviewSceneAnimationComponentGuid;
		std::string m_PreviewSelectedSlotId;
		std::string m_PreviewSelectedClipName;
		std::uint64_t m_PreviewRevision = 0;
		std::uint64_t m_PreviewDocumentStateId = 0;
		bool m_PreviewCompilePending = false;
		double m_PreviewCompileQueuedAt = 0.0;
		bool m_PreviewPlaying = true;
		float m_PreviewSpeed = 1.0f;
		float m_PreviewPanelHeight = 285.0f;
		float m_PreviewYaw = 0.35f;
		float m_PreviewPitch = -0.15f;
		float m_PreviewZoom = 0.9f;
		int m_PreviewVisualizedLayer = -1;
		Vans::EditorAPI::AnimationPreviewPlaybackRequest::RootMotionMode m_PreviewRootMotionMode =
			Vans::EditorAPI::AnimationPreviewPlaybackRequest::RootMotionMode::InPlace;
		std::unordered_map<std::string, float> m_PreviewFloats;
		std::unordered_map<std::string, bool> m_PreviewBools;
		std::unordered_map<std::string, int> m_PreviewInts;
		std::unordered_map<std::string, std::array<float, 4>> m_PreviewVectors;
		std::unordered_map<std::string, Vans::EditorAPI::AnimatorParamType> m_PreviewParameterTypes;
	};
}  // namespace VansGraphics
