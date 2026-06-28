#pragma once

#include <../../GLM/glm.hpp>
#include <../../GLM/gtc/quaternion.hpp>

#include <cstdint>
#include <string>

namespace VansGraphics
{
	struct FootGroundHit
	{
		bool hasHit = false;
		glm::vec3 position = glm::vec3(0.0f);
		glm::vec3 normal = glm::vec3(0.0f, 1.0f, 0.0f);
		float distance = 0.0f;
		uint32_t layerIndex = 0;
		std::string actorName;
	};

	struct FootOverlapHit
	{
		bool hasHit = false;
		bool hasBounds = false;
		glm::vec3 boundsMin = glm::vec3(0.0f);
		glm::vec3 boundsMax = glm::vec3(0.0f);
		uint32_t layerIndex = 0;
		std::string actorName;
	};

	class VansFootGroundProbe
	{
	public:
		FootGroundHit Raycast(const glm::vec3& origin,
		                      const glm::vec3& direction,
		                      float distance,
		                      uint32_t collisionMask) const;

		FootGroundHit RaycastDown(const glm::vec3& origin,
		                          float distance,
		                          uint32_t collisionMask) const;

		FootOverlapHit OverlapBox(const glm::vec3& center,
		                          const glm::vec3& halfExtents,
		                          uint32_t collisionMask) const;

		FootOverlapHit OverlapBox(const glm::vec3& center,
		                          const glm::vec3& halfExtents,
		                          const glm::quat& rotation,
		                          uint32_t collisionMask) const;
	};
}
