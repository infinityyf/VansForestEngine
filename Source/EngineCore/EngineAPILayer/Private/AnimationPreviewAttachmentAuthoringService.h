#pragma once

#include "AnimationPreviewWriteToken.h"
#include "../Public/EngineDTOs.h"

#include <memory>
#include <string>
#include <vector>

namespace VansGraphics { class VansScene; }

namespace Vans::EditorAPI
{
	class AnimationPreviewAttachmentAuthoringService final
	{
	public:
		AnimationPreviewAttachmentAuthoringService();
		~AnimationPreviewAttachmentAuthoringService();
		AnimationPreviewAttachmentAuthoringService(
			const AnimationPreviewAttachmentAuthoringService&) = delete;
		AnimationPreviewAttachmentAuthoringService& operator=(
			const AnimationPreviewAttachmentAuthoringService&) = delete;

		void BeginSession(AnimationPreviewWriteToken writeToken);
		std::vector<AnimationPreviewAttachmentSnapshot> GetSnapshots(
			AnimationPreviewWriteToken writeToken,
			VansGraphics::VansScene& scene,
			const std::string& entityGuid,
			const std::string& animationComponentGuid,
			std::uint64_t& revision);
		AnimationPreviewAttachmentEditResult SetTransform(
			AnimationPreviewWriteToken writeToken,
			const AnimationPreviewAttachmentTransformRequest& request,
			VansGraphics::VansScene& scene,
			const std::string& targetEntityGuid,
			const std::string& targetAnimationComponentGuid);
		AnimationPreviewAttachmentEditResult SetBinding(
			AnimationPreviewWriteToken writeToken,
			const AnimationPreviewAttachmentBindingRequest& request,
			VansGraphics::VansScene& scene,
			const std::string& targetEntityGuid,
			const std::string& targetAnimationComponentGuid);
		bool EndSession(
			AnimationPreviewSessionId sessionId,
			std::string& error);
		bool EndSession(
			AnimationPreviewSessionId sessionId,
			VansGraphics::VansScene& scene,
			std::string& error);

	private:
		struct Impl;
		friend class AnimationPreviewAdoptService;
		bool AdoptLocalTransforms(
			AnimationPreviewWriteToken writeToken,
			std::uint64_t expectedRevision,
			VansGraphics::VansScene& scene,
			const std::vector<std::string>& entities);
		std::unique_ptr<Impl> m_Impl;
	};
}
