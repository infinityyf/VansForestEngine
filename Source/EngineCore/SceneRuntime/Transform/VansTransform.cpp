#include "VansTransform.h"

#include <glm/gtc/matrix_transform.hpp>

glm::mat4x4 Vans::VansTransform::GetModelMatrix() const
{
	glm::mat4 model(1.0f);
	model = glm::translate(model, m_Position);
	// 与 ComputeModelDataFromTransform 和 ImGuizmo 的分解约定保持一致。
	model = glm::rotate(model, glm::radians(m_Rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
	model = glm::rotate(model, glm::radians(m_Rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
	model = glm::rotate(model, glm::radians(m_Rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
	model = glm::scale(model, m_Scale);
	return model;
}
