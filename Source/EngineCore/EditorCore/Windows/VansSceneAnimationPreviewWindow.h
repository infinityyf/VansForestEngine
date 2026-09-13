#pragma once

#include "VansBaseWindowComponent.h"

#include <memory>
#include <array>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct ImVec2;

namespace VansGraphics
{
	class VansCamera;

	class VansSceneAnimationPreviewWindow final : public VansBaseWindowComponent
	{
	public:
		~VansSceneAnimationPreviewWindow() override;

		void SetOpen(bool open);
		bool IsOpen() const { return m_IsOpen; }
		void ShowWindow(Vans::EditorAPI::IEngineEditorAPI& editorAPI) override;

		// 返回true时场景视口的普通Entity Gizmo应让位给Socket/Attachment Gizmo。
		bool DrawSceneViewportHandle(
			Vans::EditorAPI::IEngineEditorAPI& editorAPI,
			VansCamera* camera,
			const ImVec2& viewportOrigin,
			const ImVec2& viewportSize);

	private:
		enum class TransformTarget { None, Socket, Attachment };
		enum class GizmoOperation { Translate, Rotate, Scale };
		struct NamePrefixFilter
		{
			std::array<char, 128> text{};
			bool Draw();
			bool Matches(std::string_view name) const;
		};
		bool DrawSceneEntityCombo(Vans::EditorAPI::IEngineEditorAPI& api,
			const char* label, std::string& selectedGuid, NamePrefixFilter& filter);

		void StartPreview(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void StopPreview();
		bool LoadAnimatorDocument(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void ResetParameterValues();
		void DrawSessionControls(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void DrawParameters(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void DrawGraphSetsAndSlots(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		bool DrawTransformIKEditor(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		bool ApplyAnimatorWorkingCopy(Vans::EditorAPI::IEngineEditorAPI& editorAPI, bool recordEdit);
		bool ApplyRigWorkingCopy(Vans::EditorAPI::IEngineEditorAPI& api,
			const Vans::EditorAPI::AnimationRigDocumentDTO& document);
		void DrawRigConstraintEditor(Vans::EditorAPI::IEngineEditorAPI& api);
		void ChangeRigHistory(Vans::EditorAPI::IEngineEditorAPI& api, bool redo);
		bool DrawBoneNameCombo(const char* label, std::string& name);
		void AddRotationDistributionNode();
		bool ApplyTargetBindingsToDocument(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		bool SaveAnimationSetup(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void ReloadSceneTargetEdits(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void DrawSocketAndAttachmentEditor(Vans::EditorAPI::IEngineEditorAPI& editorAPI, bool inlineAnchorVisible);
		void DrawSocketTransform(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		bool DrawAttachmentPoseEditor(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void DrawAttachmentTransform(Vans::EditorAPI::IEngineEditorAPI& editorAPI, bool showPose);
		void DrawSceneHandleControls();
		bool SaveRigChanges(
			Vans::EditorAPI::IEngineEditorAPI& editorAPI,
			const char* successMessage);
		void RefreshRigSnapshot(Vans::EditorAPI::IEngineEditorAPI& editorAPI);

		std::vector<Vans::EditorAPI::AnimationTargetBindingDTO> m_TargetBindings;
		std::string m_NewChainId;
		Vans::EditorAPI::AnimationRigChainDTO m_ChainDraft;
		Vans::EditorAPI::AnimationRigJointLimitDTO m_LimitDraft;
		Vans::EditorAPI::AnimationRigRotationDistributionDTO m_RotationDraft;
		std::string m_LimitOriginalBone, m_RotationOriginalId;
		bool m_LimitDraftDirty = false, m_RotationDraftDirty = false;
		NamePrefixFilter m_ConstraintBoneFilter;
		std::string m_CheckpointDraftId = "beforeIK";
		std::string m_CheckpointDraftBone;
		std::string m_NewBindingId = "handGrip";
		std::string m_BindPoseCheckpoint;
		std::vector<std::string> m_AppliedTargetTransforms;
		bool m_AnimatorDraftDirty = false;
		std::uint64_t m_AnimatorDocumentStateId = 0;
		std::uint64_t m_RigDocumentStateId = 0;
		bool m_IsOpen = false;
		Vans::EditorAPI::IEngineEditorAPI* m_ActiveAPI = nullptr;
		Vans::EditorAPI::AnimationPreviewSessionId m_SessionId = 0;
		std::string m_SelectedAnimatorGuid;
		std::string m_SelectedAnimatorLabel = "Choose Animator...";
		std::string m_SelectedEntityGuid;
		std::string m_SelectedAnimationComponentGuid;
		std::string m_SelectedRigLabel = "Choose Scene Skeleton...";
		std::unique_ptr<Vans::EditorAPI::AnimatorDocumentDTO> m_AnimatorDocument;
		Vans::EditorAPI::AnimationPreviewSnapshot m_Snapshot;
		Vans::EditorAPI::AnimationPreviewRigSnapshot m_RigSnapshot;
		std::vector<Vans::EditorAPI::AnimationPreviewSceneEntitySnapshot>
			m_SceneEntities;
		std::string m_SelectedSceneEntityGuid;
		NamePrefixFilter m_SceneObjectFilter;
		NamePrefixFilter m_TargetObjectFilter;
		NamePrefixFilter m_AttachmentFilter;
		NamePrefixFilter m_SocketFilter;
		std::string m_SelectedSocketGuid;
		std::string m_SelectedBindAnchorGuid;
		std::string m_SelectedBindAnchorLabel = "Choose Bone or Socket...";
		Vans::EditorAPI::RuntimeParentKind m_SelectedBindParentKind =
			Vans::EditorAPI::RuntimeParentKind::None;
		std::string m_SelectedAttachmentGuid;
		std::string m_SelectedGraphSetId;
		std::string m_SelectedSlotId;
		std::string m_SelectedClipName;
		std::string m_Message;
		bool m_Playing = true;
		float m_Speed = 1.0f;
		Vans::EditorAPI::RuntimeReparentTransformPolicy m_BindPolicy =
			Vans::EditorAPI::RuntimeReparentTransformPolicy::Snap;
		Vans::EditorAPI::RuntimeTransformSpace m_SocketEditSpace =
			Vans::EditorAPI::RuntimeTransformSpace::Local;
		Vans::EditorAPI::RuntimeTransformSpace m_AttachmentEditSpace =
			Vans::EditorAPI::RuntimeTransformSpace::Local;
		TransformTarget m_TransformTarget = TransformTarget::None;
		GizmoOperation m_GizmoOperation = GizmoOperation::Translate;
		bool m_GizmoWorldSpace = false;
		std::unordered_map<std::string, float> m_FloatParameters;
		std::unordered_map<std::string, bool> m_BoolParameters;
		std::unordered_map<std::string, int> m_IntParameters;
		std::unordered_map<std::string, Vans::EditorAPI::AnimationVector3DTO>
			m_VectorParameters;
		std::unordered_map<std::string, Vans::EditorAPI::AnimationQuaternionDTO>
			m_QuaternionParameters;
	};
}
