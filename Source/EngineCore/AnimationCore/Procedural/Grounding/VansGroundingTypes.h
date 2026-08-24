#pragma once

#include "../VansAnimationRig.h"

#include <string>
#include <vector>

namespace VansGraphics
{
	enum class VansPlantPivot { Heel, Ball, Ankle };

	struct VansGroundingQuerySettings
	{
		std::string profile;
		std::uint32_t collisionMask = 0;
		// Query distances and residuals are expressed in world-space units.
		float startDistanceAgainstApproach = 0.45f;
		float endDistanceAlongApproach = 0.65f;
		float maxStepUp = 0.35f;
		float maxStepDown = 0.55f;
		float maxSlopeDegrees = 55.0f;
		float maxPlaneResidual = 0.015f;
		float maxNormalDeviationDegrees = 20.0f;
	};

	struct VansGroundingPlantSettings
	{
		bool lockEnabled = true;
		float enterPhase = 0.70f;
		float exitPhase = 0.25f;
		// Plant/replant translation thresholds are expressed in world-space units.
		float unplantDistance = 0.30f;
		float replantDistance = 0.18f;
		float unplantAngleDegrees = 35.0f;
		float replantAngleDegrees = 18.0f;
		VansPlantPivot pivot = VansPlantPivot::Ball;
		float weightHalfLife = 0.04f;
	};

	struct VansGroundingAlignmentSettings
	{
		// Heights are measured in world-space units from the animated sole to the
		// queried support.  They fade pose-relative placement out during swing.
		float fullContactHeight = 0.08f;
		float contactFadeHeight = 0.28f;
		float normalHalfLife = 0.06f;
		float rotationWeight = 0.70f;
	};

	struct VansGroundingPelvisSettings
	{
		// Pelvis translation limits are expressed in world-space units.
		float maxUpOffset = 0.15f;
		float maxDownOffset = 0.32f;
		float maxHorizontalOffset = 0.10f;
		float halfLife = 0.07f;
	};

	struct VansGroundingSettings
	{
		std::vector<std::string> contacts;
		VansGroundingQuerySettings query;
		std::string plantSignal;
		VansGroundingPlantSettings plant;
		VansGroundingAlignmentSettings alignment;
		VansGroundingPelvisSettings pelvis;
		float weight = 1.0f;
	};

	struct VansCompiledGroundingSettings
	{
		std::vector<int> contactIndices;
		VansGroundingQuerySettings query;
		std::string plantSignal;
		VansGroundingPlantSettings plant;
		VansGroundingAlignmentSettings alignment;
		VansGroundingPelvisSettings pelvis;
		float weight = 1.0f;
	};

	bool VansCompileGroundingSettings(
		const VansGroundingSettings& settings,
		const VansCompiledAnimationRig& rig,
		VansCompiledGroundingSettings& outSettings,
		std::string& error);
}
