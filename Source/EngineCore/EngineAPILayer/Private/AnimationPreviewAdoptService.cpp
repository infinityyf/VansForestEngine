#include "AnimationPreviewAdoptService.h"

#include "AnimationPreviewAttachmentAuthoringService.h"
#include "AnimationPreviewRigAuthoringService.h"

namespace Vans::EditorAPI
{
	AnimationPreviewRigEditResult AnimationPreviewAdoptService::AdoptRig(
		AnimationPreviewRigAuthoringService& authoring,
		AnimationPreviewWriteToken writeToken,
		const AnimationPreviewRigAdoptRequest& request,
		VansGraphics::VansAnimationController& controller)
	{
		return authoring.Adopt(
			writeToken, request, controller);
	}

	bool AnimationPreviewAdoptService::AdoptScene(
		AnimationPreviewAttachmentAuthoringService& authoring,
		AnimationPreviewWriteToken writeToken,
		std::uint64_t expectedAttachmentRevision,
		VansGraphics::VansScene& scene,
		const std::vector<std::string>& savedTransformEntities)
	{
		return authoring.AdoptLocalTransforms(
			writeToken, expectedAttachmentRevision, scene, savedTransformEntities);
	}
}
