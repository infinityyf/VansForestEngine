#pragma once

#include <glm/glm.hpp>

namespace Vans
{
class VansTransform
{
public:
	glm::vec3 m_Position;
	glm::vec3 m_Rotation;
	glm::vec3 m_Scale;

	glm::mat4x4 GetModelMatrix() const;
};
}
