#pragma once

#include "../AnimationCore/FootPlacement/VansFootPlacementTypes.h"
#include "../AnimationCore/MotionMatching/VansMotionMatching.h"

#include <glm/glm.hpp>

#include <optional>
#include <string>
#include <vector>

namespace Vans
{
	struct VansSceneAnimationBoneBindingConfig
	{
		std::string boneName;
		std::string physicsObjectName;
		glm::vec3 offsetPosition = glm::vec3(0.0f);
		glm::vec3 offsetRotation = glm::vec3(0.0f);
		glm::vec3 offsetScale = glm::vec3(1.0f);
		bool syncRotation = true;
		bool syncScale = false;
		std::string layerName = "Default";
		bool isTrigger = false;
		bool enabled = true;
		bool autoCreateNode = false;
		glm::vec3 shapeExtents = glm::vec3(0.1f, 0.25f, 0.1f);
		std::string shapeType = "capsule";
	};

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
		std::string runtimeMode = "source_proxy";
		std::string cachePolicy = "read_or_build";
		bool debugDraw = false;
	};

	struct VansSceneAnimationComponentConfig
	{
		bool valid = false;
		bool enabled = true;
		std::string meshGroup;
		std::string animator;
		std::string externClips;
		bool rootMotion = false;
		bool autoPlay = true;
		bool loop = true;
		std::string rootBone;
		std::string name;
		std::optional<VansSceneAnimationRetargetConfig> retarget;
		std::optional<VansGraphics::MotionMatchingSettings> motionMatching;
		std::optional<VansGraphics::FootPlacementSettings> footPlacement;
		std::vector<VansSceneAnimationBoneBindingConfig> boneBindings;
		std::optional<VansSceneRagdollComponentConfig> ragdoll;
	};
}
