#pragma once
#include <optional>

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace VansEngine
{
	// Main-thread locomotion acceptance, published before animation evaluation.
	// This is not a collision result or a guarantee of unobstructed upward travel.
	struct VansCharacterJumpEvent
	{
		std::uint32_t transformID = UINT32_MAX;
	};

	// 在落地位置发布、清除下落速度和续算地面运动之前，由 Scene 在物理锁外投递。
	struct VansCharacterLandedEvent
	{
		std::uint32_t transformID = UINT32_MAX;
		glm::vec3 position{0.0f};
		glm::vec3 velocity{0.0f};
		glm::vec3 normal{0.0f};
	};

	// Collision-resolved CCT motion. Scene publishes after releasing Physics locks.
	struct VansCharacterMovementUpdatedEvent
	{
		std::uint32_t transformID = UINT32_MAX;
		float deltaTime = 0.0f;
		glm::vec3 position{ 0.0f };
		glm::vec3 velocity{ 0.0f };
		bool grounded = false;
		// 运动模型经碰撞约束后的帧末速度；velocity 保留碰撞位移/dt。
		std::optional<glm::vec3> motionVelocity;
		int simulationSteps = 1;
	};

	struct VansCharacterMotionFlushResult
	{
		std::optional<VansCharacterMovementUpdatedEvent> movement;
		std::optional<VansCharacterLandedEvent> landed;
	};

	enum class VansPhysicsContactEventType
	{
		CollisionEnter,
		CollisionExit,
		TriggerEnter,
		TriggerExit
	};

	struct VansPhysicsContactEvent
	{
		VansPhysicsContactEventType type = VansPhysicsContactEventType::CollisionEnter;

		std::uint32_t transformID_A = 0;
		std::uint32_t transformID_B = 0;

		glm::vec3 contactPoint = glm::vec3(0.0f);
		glm::vec3 contactNormal = glm::vec3(0.0f);
		float impulse = 0.0f;

		std::string nameA;
		std::string nameB;
	};
}
