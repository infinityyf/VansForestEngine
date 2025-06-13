#include "VansCamera.h"
#include "../VansTimer.h"

#include <iostream>

void VansGraphics::VansCamera::HandleMouseMovement(float deltaX, float deltaY)
{
    const float sensitivity = 0.1f;
    m_Rotation.y += deltaX * sensitivity;
    m_Rotation.x -= deltaY * sensitivity;

    // Constrain the pitch
    if (m_Rotation.x > 89.0f) m_Rotation.x = 89.0f;
    if (m_Rotation.x < -89.0f) m_Rotation.x = -89.0f;
}

void VansGraphics::VansCamera::HandleKeyboardInput(int key, int scancode, int action, int mods, float deltaTime)
{
    const float speed = 20.0f * deltaTime;
    glm::vec3 front;
    front.x = cos(glm::radians(m_Rotation.y)) * cos(glm::radians(m_Rotation.x));
    front.y = sin(glm::radians(m_Rotation.x));
    front.z = sin(glm::radians(m_Rotation.y)) * cos(glm::radians(m_Rotation.x));
    front = glm::normalize(front);

    glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
    glm::vec3 up = glm::normalize(glm::cross(right, front));

    switch (key) 
    {
    case GLFW_KEY_W:
        m_Position = m_Position + front * speed;
        // Handle forward movement
        break;
    case GLFW_KEY_S:
        m_Position = m_Position - front * speed;
        // Handle backward movement
        break;
    case GLFW_KEY_A:
        m_Position = m_Position - right * speed;
        // Handle left movement
        break;
    case GLFW_KEY_D:
        m_Position = m_Position + right * speed;
        // Handle right movement
        break;
    case GLFW_KEY_Q:
        m_Position = m_Position - up * speed;
        // Handle left movement
        break;
    case GLFW_KEY_E:
        m_Position = m_Position + up * speed;
        // Handle right movement
        break;
    default:
        break;
    }
}

void VansGraphics::VansCamera::SetCameraData(const glm::mat4& view_matrix, const glm::mat4& projective_matrix)
{
    m_CameraData.CameraPosition = glm::vec4(m_Position.x, m_Position.y, m_Position.z,1.0f);
    m_CameraData.CameraDirection = glm::vec4(-view_matrix[2]);
    m_CameraData.ViewMatrix = view_matrix;
    m_CameraData.ProjectionMatrix = projective_matrix;

	float width = m_RenderDevice->GetNativeRenderWidth();
	float height = m_RenderDevice->GetNativeRenderHeight();
	m_CameraData.ScreenParams = glm::vec4(width, height, 1 / width, 1 / height);

    float time = VansTimer::GetFrameTime();
    m_CameraData.FrameParams = glm::vec4(m_RenderFrameIndex, time, 0, 0);
	m_CameraData.CameraParams = glm::vec4(m_NearClip, m_FarClip, m_Fov, m_AspectRatio);

    m_CameraDataBuffer.SetBufferData(&m_CameraData, 0, sizeof(m_CameraData));

    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.clear();
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();
    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_CameraBufferDescriptorSets[0],
            VansVKDescriptorManager::m_CameraBufferSetBinding,
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            {
                {
                    m_CameraDataBuffer.GetMativeBuffer(),
                    0,
                    m_CameraDataBuffer.GetBufferSize()
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

glm::mat4 VansGraphics::VansCamera::GetViewMatrix()
{
    glm::vec3 front;
    front.x = cos(glm::radians(m_Rotation.y)) * cos(glm::radians(m_Rotation.x));
    front.y = sin(glm::radians(m_Rotation.x));
    front.z = sin(glm::radians(m_Rotation.y)) * cos(glm::radians(m_Rotation.x));
    front = glm::normalize(front);

    return glm::lookAt(m_Position, m_Position + front, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 VansGraphics::VansCamera::GetProjectiveMatrix()
{
    //calculate projective matrix
    return glm::perspective(glm::radians(m_Fov), m_AspectRatio, m_NearClip, m_FarClip);
}

VansGraphics::VansCamera::~VansCamera()
{
    VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(m_CameraBufferLayout);
    VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(m_CameraBufferDescriptorSets);

    m_CameraDataBuffer.DestroyVulkanBuffer(static_cast<VansVKDevice*>(m_RenderDevice)->GetLogicDevice());
}
void VansGraphics::VansCamera::Rendering()
{
    SetCameraData(GetViewMatrix(), GetProjectiveMatrix());
    m_RenderDevice->Rendering();

    m_RenderFrameIndex++;
}

void VansGraphics::VansCamera::Present()
{
    m_RenderDevice->Present();
}
