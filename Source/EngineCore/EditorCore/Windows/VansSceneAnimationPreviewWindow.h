#pragma once

#include "VansBaseWindowComponent.h"

#include <memory>
#include <string>
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

		void StartPreview(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void StopPreview();
		bool LoadAnimatorDocument(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void ResetParameterValues();
		void DrawSessionControls(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void DrawParameters(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void DrawGraphSetsAndSlots(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void DrawSocketAndAttachmentEditor(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void DrawSocketTransform(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		void DrawAttachmentTransform(Vans::EditorAPI::IEngineEditorAPI& editorAPI);
		bool SaveRigChanges(
			Vans::EditorAPI::IEngineEditorAPI& editorAPI,
			const char* successMessage);
		void RefreshRigSnapshot(Vans::EditorAPI::IEngineEditorAPI& editorAPI);

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
