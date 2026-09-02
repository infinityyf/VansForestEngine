#pragma once

#include "../Public/EngineDTOs.h"

#include <../../GLM/mat4x4.hpp>

#include <cstdint>
#include <string>

namespace VansGraphics
{
	class VansAnimationController;
	struct Skeleton;
}

namespace Vans::EditorAPI
{
	struct AnimationPreviewRigContext
	{
		AnimationPreviewSessionId sessionId = 0;
		std::uint64_t sceneContentRevision = 0;
		std::string entityGuid;
		std::string animationComponentGuid;
		VansGraphics::VansAnimationController* controller = nullptr;
		const VansGraphics::Skeleton* skeleton = nullptr;
		glm::mat4 ownerWorld{ 1.0f };
		bool retargetEnabled = false;
		std::string retargetProfilePath;
		std::string retargetSourceModelPath;
		std::string retargetSourceAnimatorPath;
	};

	class AnimationPreviewRigAuthoringService final
	{
	public:
		static bool BeginSession(
			AnimationPreviewSessionId sessionId,
			VansGraphics::VansAnimationController& controller,
			std::string& error);
		static AnimationPreviewRigSnapshot GetSnapshot(
			const AnimationPreviewRigContext& context);
		static bool GetWorkingCanonicalJson(
			AnimationPreviewSessionId sessionId,
			std::string& canonicalJson,
			std::string& error);
		static AnimationPreviewRigEditResult SetSocketTransform(
			const AnimationPreviewRigContext& context,
			const AnimationPreviewRigSocketTransformRequest& request);
		static AnimationPreviewRigEditResult SetAttachmentProfile(
			const AnimationPreviewRigContext& context,
			const AnimationPreviewRigAttachmentProfileRequest& request);
		static AnimationPreviewRigEditResult Adopt(
			const AnimationPreviewRigAdoptRequest& request,
			VansGraphics::VansAnimationController& controller);
		static bool EndSession(
			AnimationPreviewSessionId sessionId,
			VansGraphics::VansAnimationController* controller,
			std::string& error);
	};
}
