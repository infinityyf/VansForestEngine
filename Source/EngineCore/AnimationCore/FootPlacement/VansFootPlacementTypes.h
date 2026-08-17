#pragma once

#include "../VansAnimationTypes.h"
#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace VansGraphics
{
	// Foot Placement is deliberately pose-relative. The animated foot owns the
	// horizontal position; the procedural pass only supplies a displacement along
	// world up and a ground-aligned orientation.
	inline glm::vec3 FootPlacementBuildTarget(const glm::vec3& animatedFootWorld,
	                                          const glm::vec3& groundPointWorld,
	                                          float ankleHeight,
	                                          const glm::vec3& worldUp = glm::vec3(0.0f, 1.0f, 0.0f))
	{
		const float targetHeight = glm::dot(groundPointWorld, worldUp) + std::max(0.0f, ankleHeight);
		return animatedFootWorld + worldUp * (targetHeight - glm::dot(animatedFootWorld, worldUp));
	}

	inline float FootPlacementClearanceWeight(float soleClearance,
	                                          float fullContactHeight,
	                                          float fadeOutHeight)
	{
		const float fullHeight = std::max(0.0f, fullContactHeight);
		const float fadeHeight = std::max(fullHeight + 1e-4f, fadeOutHeight);
		const float t = glm::clamp((soleClearance - fullHeight) / (fadeHeight - fullHeight), 0.0f, 1.0f);
		const float smooth = t * t * (3.0f - 2.0f * t);
		return 1.0f - smooth;
	}

	inline bool FootPlacementSolveResultIsUsable(float finalPositionError,
	                                             float legLength,
	                                             bool finiteResult)
	{
		if (!finiteResult || !std::isfinite(finalPositionError) || !std::isfinite(legLength) || legLength <= 0.0f)
			return false;
		return finalPositionError <= std::max(legLength * 0.02f, 1e-4f);
	}

	struct FootPlacementBoneNames
	{
		std::string pelvis = "pelvis";
		std::string leftHip = "thigh_l";
		std::string leftKnee = "calf_l";
		std::string leftFoot = "foot_l";
		std::string rightHip = "thigh_r";
		std::string rightKnee = "calf_r";
		std::string rightFoot = "foot_r";
	};

	// 地形贴合与动画接触相位共用同一套 Foot Placement。动画相位存在时，
	// 已种下的脚在世界空间锁定；其他动画控制器仍保持纯姿态相对贴地。
	struct FootPlacementSettings
	{
		bool enabled = false;
		float probeOriginHeight = 0.45f;
		float probeLength = 1.10f;
		float footHalfLength = 0.18f;
		float footHalfWidth = 0.08f;
		float ankleHeight = 0.08f;
		float fullContactHeight = 0.08f;
		float contactFadeHeight = 0.28f;
		float maxStepUp = 0.35f;
		float maxStepDown = 0.55f;
		float maxSlopeDeg = 55.0f;
		float pelvisMaxDrop = 0.30f;
		float pelvisSmoothTime = 0.08f;
		float offsetSmoothTime = 0.04f;
		float normalSmoothTime = 0.06f;
		float weightSmoothTime = 0.04f;
		float globalWeightSmoothTime = 0.08f;
		float ikWeight = 1.0f;
		float rotationWeight = 0.70f;
		float maxLegExtensionRatio = 0.98f;
		float poleSmoothTime = 0.05f;
		bool footLockEnabled = false;
		float footLockEnterPlantWeight = 0.70f;
		float footLockExitPlantWeight = 0.25f;
		float footLockMaxDistance = 0.35f;
		float footLockSmoothTime = 0.035f;
		glm::vec3 kneePoleModelDir = glm::vec3(0.0f, 0.0f, 1.0f);
		float kneePoleModelWeight = 0.0f;
		bool debugVisualization = false;
		uint32_t collisionMask = 0xffffffffu;
		std::string airborneParameter = "IsAirborne";
		FootPlacementBoneNames bones;
	};

	struct FootPlacementRuntimeState
	{
		bool airborne = false;
		bool forceDisabled = false;
		float externalWeight = 1.0f;
		bool hasAnimationPlantWeights = false;
		float leftPlantWeight = 0.0f;
		float rightPlantWeight = 0.0f;
	};

	struct FootPlacementContact
	{
		bool valid = false;
		glm::vec3 groundPointWorld = glm::vec3(0.0f);
		glm::vec3 groundNormalWorld = glm::vec3(0.0f, 1.0f, 0.0f);
		float verticalOffset = 0.0f;
		float soleClearance = 0.0f;
		float slopeDeg = 0.0f;
		uint32_t layer = 0;
		uintptr_t actorId = 0;
		glm::mat4 actorWorldTransform = glm::mat4(1.0f);
		bool hasActorWorldTransform = false;
		std::string actorName;
	};

	struct FootPlacementFootState
	{
		bool initialized = false;
		float verticalOffset = 0.0f;
		float verticalVelocity = 0.0f;
		float weight = 0.0f;
		float weightVelocity = 0.0f;
		glm::vec3 groundNormalWorld = glm::vec3(0.0f, 1.0f, 0.0f);
		bool poleInitialized = false;
		glm::vec3 poleModelDir = glm::vec3(0.0f, 0.0f, 1.0f);
		bool planted = false;
		glm::vec3 lockedWorldPosition = glm::vec3(0.0f);
		uintptr_t lockedActorId = 0;
		glm::vec3 lockedActorLocalPosition = glm::vec3(0.0f);
		bool hasLockedActorLocalPosition = false;
		float lockWeight = 0.0f;
		float lockWeightVelocity = 0.0f;
	};

	struct FootPlacementDebugSample
	{
		glm::vec3 rayStart = glm::vec3(0.0f);
		glm::vec3 rayEnd = glm::vec3(0.0f);
		glm::vec3 hitPosition = glm::vec3(0.0f);
		glm::vec3 hitNormal = glm::vec3(0.0f, 1.0f, 0.0f);
		bool hasHit = false;
		bool accepted = false;
		uint32_t hitLayer = 0;
		std::string hitActorName;
		std::string status;
	};

	struct FootPlacementDebugLeg
	{
		glm::vec3 hip = glm::vec3(0.0f);
		glm::vec3 knee = glm::vec3(0.0f);
		glm::vec3 animatedFoot = glm::vec3(0.0f);
		glm::vec3 solvedFoot = glm::vec3(0.0f);
		glm::vec3 target = glm::vec3(0.0f);
		glm::vec3 contact = glm::vec3(0.0f);
		glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
		std::vector<FootPlacementDebugSample> samples;
		bool hasContact = false;
		bool hasTarget = false;
		float targetWeight = 0.0f;
		float verticalOffset = 0.0f;
		bool planted = false;
		float plantWeight = 0.0f;
		float horizontalLockError = 0.0f;
	};

	struct FootPlacementDebugData
	{
		bool enabled = false;
		float currentWeight = 0.0f;
		float pelvisOffset = 0.0f;
		FootPlacementDebugLeg left;
		FootPlacementDebugLeg right;
	};
}
