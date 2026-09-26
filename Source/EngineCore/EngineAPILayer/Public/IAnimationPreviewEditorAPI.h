#pragma once

#include "EngineDTOs.h"

#include <vector>

namespace Vans::EditorAPI
{
	class IAnimationPreviewEditorAPI
	{
	public:
		virtual ~IAnimationPreviewEditorAPI() = default;
		virtual AnimationPreviewCreateResult CreateAnimationPreview(
			const AnimationPreviewCreateRequest& request) = 0;
		virtual AnimationPreviewUpdateResult UpdateAnimationPreviewDefinition(
			const AnimationPreviewDefinitionUpdate& update) = 0;
		virtual bool SetAnimationPreviewPlayback(
			const AnimationPreviewPlaybackRequest& request) = 0;
		virtual bool SetAnimationPreviewParameter(
			const AnimationPreviewParameterValue& value) = 0;
		virtual bool SwitchAnimationPreviewGraphSet(
			const AnimationPreviewGraphSetRequest& request) = 0;
		virtual bool TriggerAnimationPreviewSlot(
			const AnimationPreviewSlotRequest& request) = 0;
		virtual void TickAnimationPreview(AnimationPreviewSessionId sessionId, float deltaTime) = 0;
		virtual AnimationPreviewSnapshot GetAnimationPreviewSnapshot(
			AnimationPreviewSessionId sessionId) const = 0;
		virtual AnimationPreviewRigSnapshot GetAnimationPreviewRigSnapshot(
			AnimationPreviewSessionId sessionId) const = 0;
		virtual std::vector<AnimationPreviewSceneEntitySnapshot>
			QueryAnimationPreviewSceneEntities(AnimationPreviewSessionId sessionId) const = 0;
		virtual AnimationRigDocumentDecodeResult GetAnimationPreviewWorkingRigDocument(
			AnimationPreviewSessionId sessionId) const = 0;
		virtual AnimationPreviewRigEditResult SetAnimationPreviewRigDefinition(
			const AnimationPreviewRigDefinitionRequest& request) = 0;
		virtual AnimationPreviewRigEditResult SetAnimationPreviewRigSocketTransform(
			const AnimationPreviewRigSocketTransformRequest& request) = 0;
		virtual AnimationPreviewRigEditResult SetAnimationPreviewRigAttachmentProfile(
			const AnimationPreviewRigAttachmentProfileRequest& request) = 0;
		virtual AnimationPreviewRigEditResult SetAnimationPreviewTargetBindings(
			const AnimationPreviewTargetBindingsRequest& request) = 0;
		virtual bool AdoptAnimationPreviewSceneChanges(
			const AnimationPreviewSceneAdoptRequest& request) = 0;
		virtual AnimationPreviewAttachmentEditResult SetAnimationPreviewAttachmentTransform(
			const AnimationPreviewAttachmentTransformRequest& request) = 0;
		virtual AnimationPreviewAttachmentEditResult SetAnimationPreviewAttachmentBinding(
			const AnimationPreviewAttachmentBindingRequest& request) = 0;
		virtual AnimationPreviewRigEditResult AdoptAnimationPreviewRig(
			const AnimationPreviewRigAdoptRequest& request) = 0;
		virtual void DestroyAnimationPreview(AnimationPreviewSessionId sessionId) = 0;
	};
}
