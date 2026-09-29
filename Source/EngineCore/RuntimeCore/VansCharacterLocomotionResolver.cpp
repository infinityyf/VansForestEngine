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
		m_VerticalDisplacement = 0.0f;
		m_FrameDeltaTime = 0.0f;
		m_FramePrepared = false;
		m_FrameMovementBlocked = false;
		m_MotionStepper = {};
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

	bool VansCharacterLocomotionResolver::Prepare(
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
		const bool jumpAccepted = m_FrameDeltaTime > 0.0f && m_Intent.valid &&
			m_Intent.jumpRequested && grounded && !movementBlocked &&
			std::isfinite(m_Intent.jumpSpeed) && m_Intent.jumpSpeed > 0.0f;
		stationaryIntent.valid = true;
		stationaryIntent.movementReferenceYaw = facingYaw;
		stationaryIntent.desiredFacingYaw = facingYaw;
		m_FrameInitialVelocity = m_TrajectoryGenerator.GetIntegrationVelocity();
		m_FrameInitialVelocity.y = grounded ? 0.0f : m_VerticalVelocity;
		if (jumpAccepted) m_FrameInitialVelocity.y = (std::max)(m_FrameInitialVelocity.y,m_Intent.jumpSpeed);
		m_FrameGrounded = grounded && !jumpAccepted;
		m_TrajectoryGenerator.Update(
			m_FrameDeltaTime,
			m_Intent.valid ? m_Intent : stationaryIntent,
			settings,
			positionWorld,
			facingYaw,
			grounded && !jumpAccepted);

		// Root Motion without gameplay intent may move horizontally/rotate, but
		// it must not start a gravity state that gameplay never requested.
		if (!m_Intent.valid)
			return false;
		// A submitted request belongs to one advancing frame, including rejection.
		// A paused frame neither accepts nor consumes it.
		if (m_FrameDeltaTime > 0.0f)
			m_Intent.jumpRequested = false;
		if (m_Intent.accelerationModel)
		{
			if (grounded) m_VerticalVelocity = 0.0f;
			if (jumpAccepted) m_VerticalVelocity = (std::max)(m_VerticalVelocity, m_Intent.jumpSpeed);
			if (!grounded || jumpAccepted)
			{
				const auto falling = IntegrateCharacterFalling(m_VerticalVelocity,
					m_Intent.gravity, m_FrameDeltaTime, m_Intent.accelerationModel->falling);
				m_VerticalVelocity = falling.velocity;
				m_VerticalDisplacement = falling.displacement;
			}
			else
				// 当前 CCT 仍靠下压保持接地；完整地面检测/坡面流程另行迁移。
				m_VerticalDisplacement = -0.5f * m_FrameDeltaTime;
			return jumpAccepted;
		}
		if (grounded && m_VerticalVelocity < 0.0f)
			m_VerticalVelocity = -0.5f;
		if (jumpAccepted)
			m_VerticalVelocity = m_Intent.jumpSpeed;
		else
			m_VerticalVelocity -=
				(std::max)(0.0f, m_Intent.gravity) * m_FrameDeltaTime;
		m_VerticalDisplacement = m_VerticalVelocity * m_FrameDeltaTime;
		return jumpAccepted;
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
			result.displacementWorld.y = m_VerticalDisplacement;
		result.deltaTime = deltaTime;
		result.hasMove = true;
		result.substepped = m_Intent.valid && m_Intent.accelerationModel && !movementBlocked &&
			authority.mode == VansLocomotionAuthorityMode::Capsule;
		if (result.substepped)
			m_MotionStepper.Begin(m_Intent,m_FrameInitialVelocity,m_FrameGrounded,deltaTime);

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

	std::optional<float> VansCharacterLocomotionResolver::ResolveVerticalContact(bool grounded, bool ceiling)
	{
		if (!m_Intent.valid || !m_Intent.accelerationModel) return std::nullopt;
		if ((grounded && m_VerticalVelocity < 0.0f) || (ceiling && m_VerticalVelocity > 0.0f))
			m_VerticalVelocity = 0.0f;
		return m_VerticalVelocity;
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
