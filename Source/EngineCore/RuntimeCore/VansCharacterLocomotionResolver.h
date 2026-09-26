#pragma once

#include "VansCharacterMotion.h"
#include "VansCharacterTrajectoryGenerator.h"

namespace Vans
{
	enum class VansLocomotionAuthorityMode
	{
		Capsule,
		RootMotion,
		Blend
	};

	// Scene composition selects exactly one per-frame locomotion authority after
	// animation evaluation. RuntimeCore only consumes the value; PhysicsCore does
	// not query AnimationCore or reinterpret Motion Matching state.
	struct VansLocomotionAuthority
	{
		VansLocomotionAuthorityMode mode = VansLocomotionAuthorityMode::Capsule;
		float rootMotionWeight = 0.0f;
	};

	VansLocomotionAuthority SelectLocomotionAuthority(
		const VansCharacterMotionSettings& settings,
		bool rootMotionValid,
		bool motionMatchingUsed,
		bool rootMotionPreferred);

	struct VansCharacterLocomotionResult
	{
		glm::vec3 displacementWorld{ 0.0f };
		float facingYaw = 0.0f;
		float deltaTime = 0.0f;
		bool hasMove = false;
	};

	// Pure character-motion state and math. It owns intent normalization,
	// trajectory planning, gravity/jump state, and one prepared frame. It has no
	// PhysX, Scene, TransformStore, AnimationCore, or rendering dependency.
	class VansCharacterLocomotionResolver
	{
	public:
		void Clear(const glm::vec3& positionWorld, float facingYaw);
		void ResetMotion(const glm::vec3& positionWorld, float facingYaw);

		void SetIntent(const VansCharacterMotionIntent& intent);
		bool HasIntent() const { return m_Intent.valid; }

		void Prepare(float deltaTime,
		             const VansCharacterMotionSettings& settings,
		             const glm::vec3& positionWorld,
		             float facingYaw,
		             bool grounded,
		             bool movementBlocked);

		VansCharacterLocomotionResult Resolve(
			const glm::vec3& animationRootDelta,
			const glm::quat& animationRootRotation,
			bool rootMotionValid,
			const VansLocomotionAuthority& authority,
			const VansCharacterMotionSettings& settings,
			const glm::vec3& animationToWorldScale,
			float currentFacingYaw);

		void RecordResolvedMotion(float deltaTime,
		                         const glm::vec3& positionWorld,
		                         const glm::vec3& actualVelocityWorld,
		                         const glm::vec3& requestedVelocityWorld);

		const VansCharacterTrajectory& GetTrajectory() const
		{
			return m_TrajectoryGenerator.GetTrajectory();
		}

	private:
		VansCharacterMotionIntent m_Intent;
		VansCharacterTrajectoryGenerator m_TrajectoryGenerator;
		float m_VerticalVelocity = 0.0f;
		float m_FrameDeltaTime = 0.0f;
		bool m_FramePrepared = false;
		bool m_FrameMovementBlocked = false;
	};
}
