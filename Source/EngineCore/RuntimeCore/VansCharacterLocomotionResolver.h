#pragma once

#include "VansCharacterMotion.h"
#include "VansCharacterTrajectoryGenerator.h"
#include "VansCharacterMotionStepper.h"

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
		bool rootMotionPreferred,
		bool activeSlotRootMotion = false);

	struct VansCharacterLocomotionResult
	{
		glm::vec3 displacementWorld{ 0.0f };
		float facingYaw = 0.0f;
		float deltaTime = 0.0f;
		bool hasMove = false;
		bool substepped = false;
	};

	// Pure character-motion state and math. It owns intent normalization,
	// trajectory planning, gravity/jump state, and one prepared frame. It has no
	// PhysX, Scene, TransformStore, AnimationCore, or rendering dependency.
	class VansCharacterLocomotionResolver
	{
	public:
		void Clear(const glm::vec3& positionWorld, float facingYaw);
		void ResetMotion(const glm::vec3& positionWorld, float facingYaw);
		void SeedVelocity(const glm::vec3& velocityWorld);
		void InitializeGroundVelocity();

		void SetIntent(const VansCharacterMotionIntent& intent);
		void SetDynamics(const VansCharacterAccelerationModel& model)
		{
			if (m_Intent.accelerationModel) m_Intent.accelerationModel = model;
			m_MotionStepper.SetDynamics(model);
		}
		bool HasIntent() const { return m_Intent.valid; }
		std::optional<float> ResolveVerticalContact(bool grounded, bool ceiling);
		bool NextMotionStep(VansCharacterMotionStep& step) { return m_MotionStepper.Next(step); }
		glm::vec3 GetSimulatedVelocity() const { return m_MotionStepper.GetVelocity(); }
		glm::vec3 GetResolvedVelocity() const { return m_TrajectoryGenerator.GetIntegrationVelocity(); }
		void ResolveSweptMotionStep(const glm::vec3& velocity, bool grounded, float unusedTime,
			bool stopSimulation = false)
		{
			m_MotionStepper.ApplySweptCollision(velocity,grounded,unusedTime,stopSimulation);
			m_VerticalVelocity = m_MotionStepper.GetVelocity().y;
		}
		int GetSimulationSteps() const { return m_MotionStepper.GetStepCount(); }

		// Returns true only when this frame consumes and accepts a jump request.
		bool Prepare(float deltaTime,
		             const VansCharacterMotionSettings& settings,
		             const glm::vec3& positionWorld,
		             float facingYaw,
		             bool grounded,
		             bool movementBlocked,
		             const glm::vec3& leavingBaseVelocity = glm::vec3(0));

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
		VansCharacterMotionStepper m_MotionStepper;
		glm::vec3 m_FrameInitialVelocity{0.0f};
		bool m_FrameGrounded = false;
		float m_VerticalVelocity = 0.0f;
		float m_VerticalDisplacement = 0.0f;
		float m_FrameDeltaTime = 0.0f;
		bool m_FramePrepared = false;
		bool m_FrameMovementBlocked = false;
	};
}
