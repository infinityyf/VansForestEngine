#pragma once

#include "AnimationPreviewWriteToken.h"
#include "../Public/EngineDTOs.h"

#include <string>
#include <vector>

namespace VansGraphics
{
	class VansAnimationController;
	class VansScene;
}

namespace Vans::EditorAPI
{
	class AnimationPreviewAttachmentAuthoringService;
	class AnimationPreviewRigAuthoringService;

	class AnimationPreviewAdoptService final
	{
	public:
		static AnimationPreviewRigEditResult AdoptRig(
			AnimationPreviewRigAuthoringService& authoring,
			AnimationPreviewWriteToken writeToken,
			const AnimationPreviewRigAdoptRequest& request,
			VansGraphics::VansAnimationController& controller);
		static bool AdoptScene(
			AnimationPreviewAttachmentAuthoringService& authoring,
			AnimationPreviewWriteToken writeToken,
			std::uint64_t expectedAttachmentRevision,
			VansGraphics::VansScene& scene,
			const std::vector<std::string>& savedTransformEntities);
	};
}
