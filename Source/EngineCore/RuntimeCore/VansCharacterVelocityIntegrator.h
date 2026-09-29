#pragma once

#include <glm/glm.hpp>
#include "VansCharacterFallingIntegrator.h"

namespace Vans
{
	// SI 单位的输入加速度运动模型。参数由 gameplay 提供，不持有项目曲线/状态。
	struct VansCharacterVelocityDynamics
	{
		float maxAcceleration = 0.0f;
		float friction = 0.0f;
		float brakingDeceleration = 0.0f;
		float brakingFrictionFactor = 0.0f;
		float brakingSubstep = 1.0f / 60.0f;
		float brakingStopSpeed = 0.0f;
		float minAnalogSpeed = 0.0f;
		float accelerationScale = 1.0f;
		float lowSpeedAccelerationBoost = 1.0f;
		float accelerationBoostSpeed = 0.0f;
	};

	struct VansCharacterAccelerationModel
	{
		VansCharacterVelocityDynamics grounded;
		VansCharacterVelocityDynamics airborne;
		VansCharacterFallingDynamics falling;
	};

	// 只积分平面速度，不负责重力、碰撞或世界位置；调用方提供碰撞后的初速度。
	glm::vec3 ResolveCharacterInputAcceleration(const glm::vec3& inputWorld,
		const glm::vec3& planarVelocity, const VansCharacterVelocityDynamics& dynamics);
	glm::vec3 IntegrateCharacterVelocity(const glm::vec3& velocity,
		const glm::vec3& acceleration, float maxInputSpeed, float deltaTime,
		const VansCharacterVelocityDynamics& dynamics);
}
