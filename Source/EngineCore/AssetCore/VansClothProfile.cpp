#include "VansClothProfile.h"

#include <GLM/gtc/matrix_transform.hpp>

namespace VansEngine
{
	void VansClothProfile::ResetToDefaults()
	{
		*this = VansClothProfile{};
	}

	glm::mat4 VansClothProfile::GetSkeletonOffsetMatrix() const
	{
		const glm::mat4 translation = glm::translate(
			glm::mat4(1.0f), m_SkeletonOffset.m_Position);
		glm::mat4 rotation(1.0f);
		rotation = glm::rotate(rotation, glm::radians(m_SkeletonOffset.m_Rotation.x),
			glm::vec3(1.0f, 0.0f, 0.0f));
		rotation = glm::rotate(rotation, glm::radians(m_SkeletonOffset.m_Rotation.y),
			glm::vec3(0.0f, 1.0f, 0.0f));
		rotation = glm::rotate(rotation, glm::radians(m_SkeletonOffset.m_Rotation.z),
			glm::vec3(0.0f, 0.0f, 1.0f));
		const glm::mat4 scale = glm::scale(glm::mat4(1.0f), m_SkeletonOffset.m_Scale);
		return translation * rotation * scale;
	}
}
