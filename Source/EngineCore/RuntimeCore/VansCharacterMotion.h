#pragma once

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>

#include <array>
#include <algorithm>
#include <cmath>

namespace Vans
{
	enum class VansLocomotionDriveMode
	{
		Capsule,
		RootMotion,
		Hybrid
	};

	struct VansCharacterMotionSettings
	{
		VansLocomotionDriveMode driveMode = VansLocomotionDriveMode::Hybrid;
		float velocityHalfLife = 0.14f;
		float facingHalfLife = 0.10f;
		float facingVelocityHalfLife = 0.08f;
		float movementReferenceYawRateHalfLife = 0.18f;
		float maxFacingYawRate = 720.0f;
		float maxAcceleration = 18.0f;
		float maxDeceleration = 24.0f;
		float actualVelocityFeedbackHalfLife = 0.45f;
		float predictionStep = 1.0f / 60.0f;
		float rootMotionToWorldScale = 0.01f;
		float loopRootMotionWeight = 0.0f;
		float transitionRootMotionWeight = 1.0f;
		float rootRotationWeight = 1.0f;
	};

	struct VansCharacterMotionIntent
	{
		// 输入始终保留在移动参考系中：X 为右，Y 为前。相机旋转只改变
		// movementReferenceYaw，不会被误判成玩家主动改变 W/A/S/D 方向。
		glm::vec2 moveInputLocal{ 0.0f };
		float movementReferenceYaw = 0.0f;
		float desiredSpeed = 0.0f;
		float desiredFacingYaw = 0.0f;
		bool hasFacing = false;
		bool jumpRequested = false;
		float jumpSpeed = 5.5f;
		float gravity = 16.0f;
		bool valid = false;
	};

	struct VansCharacterTrajectorySample
	{
		float time = 0.0f;
		glm::vec3 positionWorld{ 0.0f };
		glm::vec3 velocityWorld{ 0.0f };
		float facingYaw = 0.0f;
	};

	struct VansCharacterTrajectory
	{
		glm::vec3 originWorld{ 0.0f };
		// currentVelocityWorld 必须来自上一帧碰撞结算后的真实 CCT 位移。
		// plannedVelocityWorld 是本帧运动模型给 capsule-drive 路径的命令速度，
		// 二者分离后 Root Motion 偏差和碰撞阻挡才能反馈给下一帧 Pose Search。
		glm::vec3 currentVelocityWorld{ 0.0f };
		glm::vec3 requestedVelocityWorld{ 0.0f };
		glm::vec3 plannedVelocityWorld{ 0.0f };
		glm::vec3 desiredVelocityWorld{ 0.0f };
		glm::vec2 moveInputLocal{ 0.0f };
		float movementReferenceYaw = 0.0f;
		float movementReferenceYawRate = 0.0f;
		float currentFacingYaw = 0.0f;
		float plannedFacingYaw = 0.0f;
		float desiredFacingYaw = 0.0f;
		float desiredFacingYawRate = 0.0f;
		std::array<VansCharacterTrajectorySample, 2> history{};
		std::array<VansCharacterTrajectorySample, 3> future{};
		float directionChangeDegrees = 0.0f;
		float inputDirectionChangeDegrees = 0.0f;
		glm::vec3 predictedPivotPositionWorld{ 0.0f };
		float predictedPivotTime = 0.0f;
		float motionConsumptionRatio = 1.0f;
		bool hasDirectionChange = false;
		bool hasPredictedPivot = false;
		bool movementBlocked = false;
		bool hasFacing = false;
		bool grounded = true;
		bool hasGrounding = false;
		bool valid = false;
	};

	// Imported locomotion clips use X/Y as the ground plane and -Y as forward;
	// the engine uses X/Z as the ground plane and +Z as forward. Keep this
	// conversion explicit and shared by trajectory queries and CCT root motion.
	inline glm::vec3 EngineLocalToAnimationPlanar(const glm::vec3& engineLocal)
	{
		return glm::vec3(engineLocal.x, -engineLocal.z, 0.0f);
	}

	inline glm::vec3 AnimationToEngineLocalPlanar(const glm::vec3& animationLocal)
	{
		return glm::vec3(animationLocal.x, 0.0f, -animationLocal.y);
	}

	// Locomotion lives on the engine X/Z ground plane. Model import correction
	// (pitch/roll/scale) is visual state and must never participate in movement
	// direction queries. Facing yaw is the explicit world-from-locomotion basis.
	inline glm::vec3 WorldToLocomotionLocalPlanar(
		const glm::vec3& worldVector, float facingYawDegrees)
	{
		const glm::quat worldFromLocomotion = glm::angleAxis(
			glm::radians(facingYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::vec3 local = glm::conjugate(worldFromLocomotion) * worldVector;
		return glm::vec3(local.x, 0.0f, local.z);
	}

	inline glm::vec3 WorldToAnimationPlanar(
		const glm::vec3& worldVector, float facingYawDegrees)
	{
		return EngineLocalToAnimationPlanar(
			WorldToLocomotionLocalPlanar(worldVector, facingYawDegrees));
	}

	inline glm::vec3 LocomotionLocalToWorldPlanar(
		const glm::vec3& localVector, float facingYawDegrees)
	{
		const glm::quat worldFromLocomotion = glm::angleAxis(
			glm::radians(facingYawDegrees), glm::vec3(0.0f, 1.0f, 0.0f));
		const glm::vec3 world = worldFromLocomotion * localVector;
		return glm::vec3(world.x, 0.0f, world.z);
	}

	inline glm::vec3 AnimationToWorldPlanar(
		const glm::vec3& animationVector, float facingYawDegrees)
	{
		return LocomotionLocalToWorldPlanar(
			AnimationToEngineLocalPlanar(animationVector), facingYawDegrees);
	}

	// Converts a desired world-space visual-forward yaw into the owner Transform
	// yaw required by a model whose authored forward axis may not be engine +Z.
	// Locomotion keeps its own movementReferenceYaw, so this correction never
	// reverses path following or CCT movement.
	inline float ResolveModelOwnerFacingYaw(
		float desiredWorldForwardYawDegrees,
		const glm::vec3& modelForwardLocal)
	{
		const glm::vec2 planarForward(modelForwardLocal.x, modelForwardLocal.z);
		const float planarLength = glm::length(planarForward);
		if (!std::isfinite(desiredWorldForwardYawDegrees) ||
			!std::isfinite(planarLength) || planarLength <= 1.0e-6f)
		{
			return desiredWorldForwardYawDegrees;
		}
		const float authoredForwardYaw = glm::degrees(std::atan2(
			planarForward.x, planarForward.y));
		return std::remainder(
			desiredWorldForwardYawDegrees - authoredForwardYaw, 360.0f);
	}

	struct VansRootMotionOwnerDelta
	{
		glm::vec3 translationWorld{ 0.0f };
		float yawDegrees = 0.0f;
	};

	// Root Motion 始终是相对拥有者当前 Transform 的局部增量，不能被解释成
	// 动画文件中的绝对世界 Transform。动画空间以 -Y 为前、Z 为旋转轴；
	// CCT/场景空间以 +Z 为前、Y 为旋转轴。
	inline VansRootMotionOwnerDelta ResolveAnimationRootMotionOwnerDelta(
		const glm::vec3& animationTranslation,
		const glm::quat& animationRotation,
		float ownerFacingYawDegrees,
		const glm::vec3& animationToWorldScale)
	{
		VansRootMotionOwnerDelta result;
		const glm::vec3 scaledTranslation = animationTranslation * animationToWorldScale;
		// 动画空间(X, Y, Z-up)转换到引擎空间(X, Y-up, Z)，再以拥有者当前
		// 朝向旋转到世界空间。保留垂直分量供非 CCT 角色使用；CCT 会继续按
		// 自身重力/落地规则接管 world Y。
		const glm::vec3 engineLocal(
			scaledTranslation.x,
			scaledTranslation.z,
			-scaledTranslation.y);
		const float ownerYawRadians = glm::radians(ownerFacingYawDegrees);
		result.translationWorld = glm::mat3(glm::rotate(
			glm::mat4(1.0f), ownerYawRadians, glm::vec3(0.0f, 1.0f, 0.0f))) *
			engineLocal;

		const float rotationLength = glm::length(animationRotation);
		if (!std::isfinite(rotationLength) || rotationLength <= 1.0e-6f)
			return result;
		const glm::vec3 rotatedForward = glm::normalize(animationRotation)
			* glm::vec3(0.0f, -1.0f, 0.0f);
		const float planarLength = glm::length(glm::vec2(rotatedForward.x, rotatedForward.y));
		if (planarLength > 1.0e-6f)
			result.yawDegrees = glm::degrees(std::atan2(
				rotatedForward.x, -rotatedForward.y));
		return result;
	}

	inline VansRootMotionOwnerDelta ResolveAnimationRootMotionOwnerDelta(
		const glm::vec3& animationTranslation,
		const glm::quat& animationRotation,
		float ownerFacingYawDegrees,
		float animationToWorldScale)
	{
		return ResolveAnimationRootMotionOwnerDelta(
			animationTranslation, animationRotation, ownerFacingYawDegrees,
			glm::vec3(animationToWorldScale));
	}

	// First-order facing response to a target yaw moving at a constant angular
	// velocity. This lets Pose Search see where a rotating camera is heading,
	// instead of assuming the camera stops on every individual frame.
	inline float PredictFacingYaw(float currentYawDegrees,
	                              float desiredYawDegrees,
	                              float desiredYawRateDegreesPerSecond,
	                              float horizonSeconds,
	                              float facingHalfLifeSeconds)
	{
		const float horizon = (std::max)(0.0f, horizonSeconds);
		const float halfLife = (std::max)(0.0001f, facingHalfLifeSeconds);
		const float responseRate = 0.6931471805599453f / halfLife;
		const float response = 1.0f - std::exp(-responseRate * horizon);
		const float error = std::remainder(desiredYawDegrees - currentYawDegrees, 360.0f);
		const float predictedDelta = desiredYawRateDegreesPerSecond * horizon +
			(error - desiredYawRateDegreesPerSecond / responseRate) * response;
		return currentYawDegrees + predictedDelta;
	}
}
