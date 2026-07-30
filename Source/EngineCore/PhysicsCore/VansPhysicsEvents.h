#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

namespace VansEngine
{
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
