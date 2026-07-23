#pragma once

#include <../../GLM/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

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
		uintptr_t actorId = 0;
	};

	struct FootGroundRayRequest
	{
		glm::vec3 origin = glm::vec3(0.0f);
		glm::vec3 direction = glm::vec3(0.0f, -1.0f, 0.0f);
		float distance = 0.0f;
	};

	class VansFootGroundProbe
	{
	public:
		std::vector<FootGroundHit> RaycastBatch(const std::vector<FootGroundRayRequest>& requests,
		                                        uint32_t collisionMask) const;
	};
}
