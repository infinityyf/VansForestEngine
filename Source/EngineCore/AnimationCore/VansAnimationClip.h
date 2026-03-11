#pragma once

#include "VansAnimationTypes.h"
#include <string>

namespace VansGraphics
{
	// ────────────────────────────────────────────────────────────────
	//  VansAnimationClipIO
	//
	//  Handles .vclip file serialization / deserialization.
	//  Format: binary header (magic + version + sizes) → JSON metadata → binary keyframes.
	// ────────────────────────────────────────────────────────────────

	class VansAnimationClipIO
	{
	public:
		// Save a clip to a .vclip file (overwrites if exists).
		static bool Save(const std::string& filePath, const VansAnimationClip& clip,
		                 const Skeleton& skeleton);

		// Load a clip + skeleton from a .vclip file.
		static bool Load(const std::string& filePath, VansAnimationClip& outClip,
		                 Skeleton& outSkeleton);

		// Read metadata only (no keyframe data). Useful for UI / quick validation.
		static bool Peek(const std::string& filePath, VansAnimationClipInfo& outInfo);
	};

}  // namespace VansGraphics
