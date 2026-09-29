#include "VansCharacterVelocityIntegrator.h"

#include <algorithm>
#include <cmath>

namespace
{
	constexpr float kMinTickTime = 1.e-6f;
	constexpr float kZeroSpeedSquared = 1.e-8f;

	glm::vec3 ClampSpeed(const glm::vec3& value, float speed)
	{
		if (speed <= 0.0f) return glm::vec3(0.0f);
		const float sizeSquared = glm::dot(value, value);
		return sizeSquared > speed * speed ? value * (speed / std::sqrt(sizeSquared)) : value;
	}

	bool IsOverSpeed(const glm::vec3& velocity, float speed)
	{
		// 在速度平方上留 1% 数值容差，不等同于将速度乘 1.01。
		return glm::dot(velocity, velocity) > speed * speed * 1.01f;
	}

	glm::vec3 Brake(glm::vec3 velocity, float dt, const Vans::VansCharacterVelocityDynamics& p)
	{
		const float friction = (std::max)(0.0f, p.friction * p.brakingFrictionFactor);
		const float deceleration = (std::max)(0.0f, p.brakingDeceleration);
		if (glm::dot(velocity, velocity) == 0.0f || (friction == 0.0f && deceleration == 0.0f))
			return velocity;
		const glm::vec3 oldVelocity = velocity;
		const glm::vec3 reverseAcceleration = deceleration == 0.0f
			? glm::vec3(0.0f) : -deceleration * glm::normalize(velocity);
		const float maxStep = (std::max)(kMinTickTime, p.brakingSubstep);
		float remaining = dt;
		while (remaining >= kMinTickTime)
		{
			const float step = remaining > maxStep && friction != 0.0f
				? (std::min)(maxStep, remaining * 0.5f) : remaining;
			remaining -= step;
			velocity += (-friction * velocity + reverseAcceleration) * step;
			if (glm::dot(velocity, oldVelocity) <= 0.0f) return glm::vec3(0.0f);
		}
		const float sizeSquared = glm::dot(velocity, velocity);
		if (sizeSquared <= kZeroSpeedSquared ||
			(deceleration != 0.0f && sizeSquared <= p.brakingStopSpeed * p.brakingStopSpeed))
			return glm::vec3(0.0f);
		return velocity;
	}
}

namespace Vans
{
	glm::vec3 ResolveCharacterInputAcceleration(const glm::vec3& inputWorld,
		const glm::vec3& planarVelocity, const VansCharacterVelocityDynamics& dynamics)
	{
		float scale = dynamics.accelerationScale;
		if (scale != 0.0f && dynamics.lowSpeedAccelerationBoost > 0.0f &&
			glm::dot(planarVelocity, planarVelocity) < dynamics.accelerationBoostSpeed * dynamics.accelerationBoostSpeed)
			scale = (std::min)(1.0f, scale * dynamics.lowSpeedAccelerationBoost);
		return inputWorld * dynamics.maxAcceleration * scale;
	}

	glm::vec3 IntegrateCharacterVelocity(const glm::vec3& inputVelocity,
		const glm::vec3& inputAcceleration, float maxInputSpeed, float deltaTime,
		const VansCharacterVelocityDynamics& dynamics)
	{
		glm::vec3 velocity(inputVelocity.x, 0.0f, inputVelocity.z);
		const glm::vec3 acceleration(inputAcceleration.x, 0.0f, inputAcceleration.z);
		if (!std::isfinite(deltaTime) || deltaTime < kMinTickTime) return velocity;
		maxInputSpeed = (std::max)(0.0f, maxInputSpeed);
		const bool hasAcceleration = glm::dot(acceleration, acceleration) != 0.0f;
		const bool overSpeed = IsOverSpeed(velocity, maxInputSpeed);
		if (!hasAcceleration || overSpeed)
		{
			const glm::vec3 oldVelocity = velocity;
			velocity = Brake(velocity, deltaTime, dynamics);
			if (overSpeed && glm::dot(velocity, velocity) < maxInputSpeed * maxInputSpeed &&
				glm::dot(acceleration, oldVelocity) > 0.0f)
				velocity = glm::normalize(oldVelocity) * maxInputSpeed;
		}
		else
		{
			const glm::vec3 direction = glm::normalize(acceleration);
			velocity -= (velocity - direction * glm::length(velocity)) *
				(std::min)(deltaTime * (std::max)(0.0f, dynamics.friction), 1.0f);
		}
		if (hasAcceleration)
		{
			const float cap = IsOverSpeed(velocity, maxInputSpeed) ? glm::length(velocity) : maxInputSpeed;
			velocity = ClampSpeed(velocity + acceleration * deltaTime, cap);
		}
		return velocity;
	}
}
