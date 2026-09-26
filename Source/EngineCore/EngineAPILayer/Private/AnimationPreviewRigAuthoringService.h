#pragma once

#include "AnimationPreviewWriteToken.h"
#include "../Public/EngineDTOs.h"

#include <../../GLM/mat4x4.hpp>

#include <cstdint>
#include <memory>
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
		AnimationPreviewWriteToken writeToken;
		std::string entityGuid;
		std::string animationComponentGuid;
		glm::mat4 ownerWorld{ 1.0f };
		bool retargetEnabled = false;
		std::string retargetProfilePath;
		std::string retargetSourceModelPath;
		std::string retargetSourceAnimatorPath;
	};

	class AnimationPreviewRigAuthoringService final
	{
	public:
		AnimationPreviewRigAuthoringService();
		~AnimationPreviewRigAuthoringService();
		AnimationPreviewRigAuthoringService(
			const AnimationPreviewRigAuthoringService&) = delete;
		AnimationPreviewRigAuthoringService& operator=(
			const AnimationPreviewRigAuthoringService&) = delete;

		bool BeginSession(
			AnimationPreviewWriteToken writeToken,
			VansGraphics::VansAnimationController& controller,
			std::string& error);
		AnimationPreviewRigSnapshot GetSnapshot(
			const AnimationPreviewRigContext& context,
			const VansGraphics::VansAnimationController& controller,
			const VansGraphics::Skeleton& skeleton);
		bool GetWorkingCanonicalJson(
			AnimationPreviewSessionId sessionId,
			std::string& canonicalJson,
			std::string& error);
		AnimationPreviewRigEditResult SetDefinition(
			const AnimationPreviewRigContext& context,
			VansGraphics::VansAnimationController& controller,
			const VansGraphics::Skeleton& skeleton,
			std::uint64_t expectedRevision,
			const std::string& canonicalJson);
		AnimationPreviewRigEditResult SetSocketTransform(
			const AnimationPreviewRigContext& context,
			VansGraphics::VansAnimationController& controller,
			const VansGraphics::Skeleton& skeleton,
			const AnimationPreviewRigSocketTransformRequest& request);
		AnimationPreviewRigEditResult SetAttachmentProfile(
			const AnimationPreviewRigContext& context,
			VansGraphics::VansAnimationController& controller,
			const VansGraphics::Skeleton& skeleton,
			const AnimationPreviewRigAttachmentProfileRequest& request);
		bool EndSession(
			AnimationPreviewSessionId sessionId,
			std::string& error);
		bool EndSession(
			AnimationPreviewSessionId sessionId,
			VansGraphics::VansAnimationController& controller,
			std::string& error);

	private:
		struct Impl;
		friend class AnimationPreviewAdoptService;
		AnimationPreviewRigEditResult Adopt(
			AnimationPreviewWriteToken writeToken,
			const AnimationPreviewRigAdoptRequest& request,
			VansGraphics::VansAnimationController& controller);
		std::unique_ptr<Impl> m_Impl;
	};
}
