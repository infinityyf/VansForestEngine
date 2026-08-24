#pragma once

#include "../AnimationCore/MotionMatching/VansMotionMatching.h"

#include <optional>
#include <string>
#include <vector>

namespace Vans
{
	struct VansSceneRagdollComponentConfig
	{
		std::string profile;
		std::string driveMode = "animation";
		float blendWeight = 0.0f;
	};

	struct VansSceneAnimationRetargetConfig
	{
		bool enabled = false;
		std::string profile;
		std::string sourceModel;
		std::string sourceAnimator;
		bool debugDraw = false;
	};

	struct VansSceneAnimationComponentConfig
	{
		bool valid = false;
		bool enabled = true;
		std::string meshGroup;
		std::string animator;
		std::string rig;
		std::string externClips;
		bool rootMotion = false;
		bool autoPlay = true;
		bool loop = true;
		std::string rootBone;
		std::string name;
		std::optional<VansSceneAnimationRetargetConfig> retarget;
		std::optional<VansGraphics::MotionMatchingSettings> motionMatching;
		std::optional<VansSceneRagdollComponentConfig> ragdoll;
	};
}
