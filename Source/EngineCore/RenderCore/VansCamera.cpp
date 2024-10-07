#include "VansCamera.h"

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

void VansGraphics::VansCamera::Rendering()
{
    m_RenderDevice->SetCameraData(GetViewMatrix(), GetProjectiveMatrix());
    m_RenderDevice->Rendering();
}

void VansGraphics::VansCamera::Present()
{
    m_RenderDevice->Present();
}
