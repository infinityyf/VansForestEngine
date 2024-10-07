#pragma once

#include "VansCommonUtils.h"

namespace VansGraphics
{
	class VansTransform
	{
	public:

		glm::vec3 m_Position;
		glm::vec3 m_Rotation;
		glm::vec3 m_Scale;

	public:
		glm::mat4x4 GetModelMatrix();
	};
}
