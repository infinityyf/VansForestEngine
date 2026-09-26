#pragma once

#include "../Public/EngineDTOs.h"

#include <cstdint>

namespace Vans::EditorAPI
{
	struct AnimationPreviewWriteToken
	{
		AnimationPreviewSessionId sessionId = 0;
		std::uint64_t sceneContentRevision = 0;
	};
}
