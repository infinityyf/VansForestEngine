#include "VansCharacterLocomotionResolver.h"

#include <algorithm>
#include <cmath>

namespace Vans
{
	VansLocomotionAuthority SelectLocomotionAuthority(
		const VansCharacterMotionSettings& settings,
		bool rootMotionValid,
		bool motionMatchingUsed,
		bool rootMotionPreferred)
	{
		VansLocomotionAuthority authority;
		if (settings.driveMode == VansLocomotionDriveMode::RootMotion)
		{
			authority.mode = VansLocomotionAuthorityMode::RootMotion;
		}
		else if (settings.driveMode == VansLocomotionDriveMode::Hybrid &&
			rootMotionValid)
		{
			// A Motion Matching frame already selected and steered one authored
			// source. Capsule cannot receive a second share of the same frame.
			if (motionMatchingUsed)
			{
				authority.mode = VansLocomotionAuthorityMode::RootMotion;
			}
			else
			{
				authority.mode = VansLocomotionAuthorityMode::Blend;
				authority.rootMotionWeight = rootMotionPreferred
					? settings.transitionRootMotionWeight
					: settings.loopRootMotionWeight;
			}
		}
		return authority;
	}

	void VansCharacterLocomotionResolver::Clear(
		const glm::vec3& positionWorld, float facingYaw)
	{
		m_Intent = {};
		ResetMotion(positionWorld, facingYaw);
	}

	void VansCharacterLocomotionResolver::ResetMotion(
		const glm::vec3& positionWorld, float facingYaw)
	{
		m_TrajectoryGenerator.Reset(positionWorld, facingYaw);
		m_VerticalVelocity = 0.0f;
		m_FrameDeltaTime = 0.0f;
		m_FramePrepared = false;
		m_FrameMovementBlocked = false;
	}

	void VansCharacterLocomotionResolver::SetIntent(
		const VansCharacterMotionIntent& intent)
	{
		m_Intent = intent;
		const float inputLength = glm::length(m_Intent.moveInputLocal);
		if (inputLength > 1.0f)
			m_Intent.moveInputLocal /= inputLength;
		m_Intent.desiredSpeed = (std::max)(0.0f, m_Intent.desiredSpeed);
		m_Intent.valid = true;
	}

	void VansCharacterLocomotionResolver::Prepare(
		float deltaTime,
		const VansCharacterMotionSettings& settings,
		const glm::vec3& positionWorld,
		float facingYaw,
		bool grounded,
		bool movementBlocked)
	{
		m_FrameDeltaTime = (std::max)(deltaTime, 0.0f);
		m_FramePrepared = true;
		m_FrameMovementBlocked = movementBlocked;

		if (movementBlocked)
		{
			// Movement blocks suppress gameplay, AI, and animation motion through
			// the same resolver while preserving the CCT ground contact step.
			m_Intent = {};
			m_Intent.valid = true;
			m_Intent.movementReferenceYaw = facingYaw;
			m_Intent.desiredFacingYaw = facingYaw;
			m_Intent.hasFacing = true;
		}

		VansCharacterMotionIntent stationaryIntent;
		stationaryIntent.valid = true;
		stationaryIntent.movementReferenceYaw = facingYaw;
		stationaryIntent.desiredFacingYaw = facingYaw;
		m_TrajectoryGenerator.Update(
			m_FrameDeltaTime,
			m_Intent.valid ? m_Intent : stationaryIntent,
			settings,
			positionWorld,
			facingYaw,
			grounded && (!m_Intent.valid || !m_Intent.jumpRequested));

		// Root Motion without gameplay intent may move horizontally/rotate, but
		// it must not start a gravity state that gameplay never requested.
		if (!m_Intent.valid)
			return;
		if (grounded && m_VerticalVelocity < 0.0f)
			m_VerticalVelocity = -0.5f;
		if (m_Intent.jumpRequested && grounded)
			m_VerticalVelocity = m_Intent.jumpSpeed;
		else
			m_VerticalVelocity -=
				(std::max)(0.0f, m_Intent.gravity) * m_FrameDeltaTime;
	}

	VansCharacterLocomotionResult VansCharacterLocomotionResolver::Resolve(
		const glm::vec3& animationRootDelta,
		const glm::quat& animationRootRotation,
		bool rootMotionValid,
		const VansLocomotionAuthority& authority,
		const VansCharacterMotionSettings& settings,
		const glm::vec3& animationToWorldScale,
		float currentFacingYaw)
	{
		VansCharacterLocomotionResult result;
		result.facingYaw = currentFacingYaw;
		if (!m_FramePrepared)
			return result;

		const float deltaTime = m_FrameDeltaTime;
		const bool movementBlocked = m_FrameMovementBlocked;
		m_FrameDeltaTime = 0.0f;
		m_FramePrepared = false;
		m_FrameMovementBlocked = false;

		if (movementBlocked)
			rootMotionValid = false;
		if (!m_Intent.valid && !rootMotionValid)
			return result;

		const glm::vec3 capsuleDelta = m_Intent.valid
			? m_TrajectoryGenerator.GetPlannedVelocityWorld() * deltaTime
			: glm::vec3(0.0f);
		glm::vec3 rootWorldDelta(0.0f);
		float rootYawDelta = 0.0f;
		if (rootMotionValid)
		{
			const VansRootMotionOwnerDelta ownerDelta =
				ResolveAnimationRootMotionOwnerDelta(
					animationRootDelta,
					animationRootRotation,
					currentFacingYaw,
					animationToWorldScale);
			rootWorldDelta = ownerDelta.translationWorld;
			rootYawDelta = ownerDelta.yawDegrees;
		}

		float rootWeight = 0.0f;
		switch (authority.mode)
		{
		case VansLocomotionAuthorityMode::RootMotion:
			// RootMotion authority holds planar motion when the authored interval
			// is invalid; Capsule cannot silently take ownership for one frame.
			rootWeight = 1.0f;
			break;
		case VansLocomotionAuthorityMode::Blend:
			rootWeight = rootMotionValid
				? glm::clamp(authority.rootMotionWeight, 0.0f, 1.0f)
				: 0.0f;
			break;
		case VansLocomotionAuthorityMode::Capsule:
		default:
			break;
		}

		result.displacementWorld = glm::mix(
			capsuleDelta, rootWorldDelta, rootWeight);
		if (m_Intent.valid)
			result.displacementWorld.y = m_VerticalVelocity * deltaTime;
		result.deltaTime = deltaTime;
		result.hasMove = true;

		const float capsuleFacingDelta = m_Intent.valid
			? std::remainder(
				m_TrajectoryGenerator.GetPlannedFacingYaw() - currentFacingYaw,
				360.0f)
			: 0.0f;
		const float rootRotationWeight = glm::clamp(
			rootWeight * settings.rootRotationWeight, 0.0f, 1.0f);
		result.facingYaw += glm::mix(
			capsuleFacingDelta, rootYawDelta, rootRotationWeight);
		return result;
	}

	void VansCharacterLocomotionResolver::RecordResolvedMotion(
		float deltaTime,
		const glm::vec3& positionWorld,
		const glm::vec3& actualVelocityWorld,
		const glm::vec3& requestedVelocityWorld)
	{
		m_TrajectoryGenerator.RecordResolvedMotion(
			deltaTime, positionWorld, actualVelocityWorld, requestedVelocityWorld);
	}
}
