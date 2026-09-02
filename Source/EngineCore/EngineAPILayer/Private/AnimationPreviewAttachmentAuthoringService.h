#pragma once

#include "../Public/EngineDTOs.h"

#include <string>
#include <vector>

namespace VansGraphics { class VansScene; }

namespace Vans::EditorAPI
{
	class AnimationPreviewAttachmentAuthoringService final
	{
	public:
		static void BeginSession(AnimationPreviewSessionId sessionId);
		static std::vector<AnimationPreviewAttachmentSnapshot> GetSnapshots(
			AnimationPreviewSessionId sessionId,
			VansGraphics::VansScene& scene,
			const std::string& entityGuid,
			const std::string& animationComponentGuid,
			std::uint64_t& revision);
		static AnimationPreviewAttachmentEditResult SetTransform(
			const AnimationPreviewAttachmentTransformRequest& request,
			VansGraphics::VansScene& scene,
			const std::string& targetEntityGuid,
			const std::string& targetAnimationComponentGuid);
		static AnimationPreviewAttachmentEditResult SetBinding(
			const AnimationPreviewAttachmentBindingRequest& request,
			VansGraphics::VansScene& scene,
			const std::string& targetEntityGuid,
			const std::string& targetAnimationComponentGuid);
		static bool EndSession(
			AnimationPreviewSessionId sessionId,
			VansGraphics::VansScene* scene,
			std::string& error);
	};
}
