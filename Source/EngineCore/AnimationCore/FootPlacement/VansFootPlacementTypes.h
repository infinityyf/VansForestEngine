#pragma once

#include "../VansAnimationTypes.h"
#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace VansGraphics
{
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

	struct FootPlacementSettings
	{
		bool enabled = false;

		float probeHeightAbove = 0.55f;
		float probeDistanceBelow = 0.85f;
		float probeFootRadius = 0.12f;
		float probeFootForwardExtent = 0.0f;
		float probeFootBackwardExtent = 0.0f;
		float probeFootSideExtent = 0.0f;
		float footGroundOffset = 0.02f;
		float maxSurfaceAngleDeg = 60.0f;
		float maxVerticalCorrectionUp = 0.35f;
		float maxVerticalCorrectionDown = 0.45f;
		float maxHorizontalFootError = 0.35f;
		float minContactQuality = 0.15f;

		float pelvisMaxDown = 0.25f;
		float pelvisMaxUp = 0.08f;
		float pelvisInterpSpeed = 12.0f;

		float ikWeight = 1.0f;
		float ikWeightSpeed = 8.0f;
		float crouchWeightScale = 0.35f;
		float stanceChangeSuppressionTime = 0.20f;
		float footLockInterpSpeed = 16.0f;
		float normalInterpSpeed = 12.0f;
		float groundHeightInterpSpeed = 24.0f;
		float footPlantFullHeight = 0.05f;
		float footPlantFadeHeight = 0.18f;
		float poleInterpSpeed = 24.0f;
		bool enableFootRotation = false;
		float footRotationWeight = 0.75f;
		float ankleHeightOffset = 0.08f;
		bool debugVisualization = false;

		uint32_t collisionMask = 0xffffffffu;
		FootPlacementBoneNames bones;
	};

	struct FootPlacementRuntimeState
	{
		bool airborne = false;
		bool forceDisabled = false;
		bool crouching = false;
		bool stanceChanging = false;
		float externalWeight = 1.0f;
	};

	struct SurfaceContact
	{
		bool hasHit = false;
		bool hasOverlap = false;
		glm::vec3 worldPosition = glm::vec3(0.0f);
		glm::vec3 worldNormal = glm::vec3(0.0f, 1.0f, 0.0f);
		float verticalDelta = 0.0f;
		float horizontalError = 0.0f;
		float surfaceAngleDeg = 0.0f;
		float quality = 0.0f;
	};

	struct FootPlacementFootState
	{
		bool initialized = false;
		glm::vec3 smoothedWorldPosition = glm::vec3(0.0f);
		glm::vec3 smoothedWorldNormal = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 smoothedPoleModelDir = glm::vec3(0.0f, 0.0f, 1.0f);
		float smoothedGroundHeight = 0.0f;
		float smoothedWeight = 0.0f;
		bool groundHeightInitialized = false;
		bool poleInitialized = false;
	};

	struct FootPlacementDebugSample
	{
		glm::vec3 rayStart = glm::vec3(0.0f);
		glm::vec3 rayEnd = glm::vec3(0.0f);
		glm::vec3 hitPosition = glm::vec3(0.0f);
		glm::vec3 hitNormal = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 rawHitPosition = glm::vec3(0.0f);
		bool hasHit = false;
		bool hasRawHit = false;
		bool accepted = false;
		float quality = 0.0f;
		uint32_t hitLayer = 0;
		uint32_t rawHitLayer = 0;
		std::string hitActorName;
		std::string rawHitActorName;
		std::string status;
	};

	struct FootPlacementDebugLeg
	{
		glm::vec3 hip = glm::vec3(0.0f);
		glm::vec3 knee = glm::vec3(0.0f);
		glm::vec3 foot = glm::vec3(0.0f);
		glm::vec3 target = glm::vec3(0.0f);
		glm::vec3 contact = glm::vec3(0.0f);
		glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
		glm::vec3 overlapCenter = glm::vec3(0.0f);
		glm::vec3 overlapHalfExtents = glm::vec3(0.0f);
		std::vector<FootPlacementDebugSample> samples;
		bool hasContact = false;
		bool hasTarget = false;
		bool hasOverlap = false;
		float targetWeight = 0.0f;
		uint32_t overlapLayer = 0;
		std::string overlapActorName;
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
