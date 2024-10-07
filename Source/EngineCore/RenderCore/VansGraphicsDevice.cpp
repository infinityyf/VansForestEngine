#include "VansGraphicsDevice.h"


void VansGraphics::VansGraphicsDevice::SetCameraData(const glm::mat4& view_matrix, const glm::mat4& projective_matrix)
{
	m_CameraData.CameraPosition = glm::vec4(view_matrix[3]);
	m_CameraData.CameraDirection = glm::vec4(-view_matrix[2]);
	m_CameraData.ViewMatrix = view_matrix;
	m_CameraData.ProjectionMatrix = projective_matrix;
}

float VansGraphics::VansGraphicsDevice::GetAspectRatio()
{
	return (float)m_RenderWidth / (float)m_RenderHeight;
}

VansGraphics::VansGraphicsDevice* m_GraphicsDevice = nullptr;
VansGraphics::VansGUIBackEnd* m_GUIBackEnd = nullptr;

