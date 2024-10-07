#pragma once
#include "VansCommonUtils.h"
#include "VansGraphicsDevice.h"
namespace VansGraphics
{
	class VansCamera
	{
    private:

        //render backendÒýÓÃ
        VansGraphicsDevice* m_RenderDevice;

    public:

        void HandleMouseMovement(float deltaX, float deltaY);

        void HandleKeyboardInput(int key, int scancode, int action, int mods, float deltaTime);

    private:

        glm::mat4 GetViewMatrix();

        glm::mat4 GetProjectiveMatrix();

        glm::vec3 m_Position;
        
        glm::vec3 m_Rotation; // pitch, yaw, roll

        //support perspective projection
        float m_Fov = 45.0f;
        float m_AspectRatio = 1.0f;
        float m_NearClip = 0.1f;
        float m_FarClip = 100.0f;

    public:

        VansCamera(glm::vec3 startPosition, glm::vec3 startRotation, VansGraphicsDevice* device)
            : m_Position(startPosition), m_Rotation(startRotation), m_RenderDevice(device)
        {
            m_AspectRatio = m_RenderDevice->GetAspectRatio();
        }

        void Rendering();

        void Present();

        void* GetGraphicsDevice() {return m_RenderDevice;}
	};
}
