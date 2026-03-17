#pragma once
#include "../ScriptCore/VansCommonUtils.h"
#include "VansGraphicsDevice.h"
#include "VulkanCore/VansVKDevice.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include <vector>
using namespace VansGraphics;
namespace VansGraphics
{
    struct alignas(16) CameraDataStruct
    {
        glm::vec4   CameraPosition;
        glm::vec4	CameraDirection;
        glm::mat4x4 ViewMatrix;
        glm::mat4x4 ProjectionMatrix;
        glm::mat4x4 VPMatrix;
        glm::mat4x4 LastViewMatrix;
        glm::mat4x4 LastProjectionMatrix;
        glm::mat4x4 LastVPMatrix;
        glm::mat4x4 LastPrevViewMatrix;
        glm::mat4x4 LastPrevProjectionMatrix;
        glm::mat4x4 LastPrevVPMatrix;
        glm::mat4x4 InverseViewMatrix;
        glm::mat4x4 InverseProjectionMatrix;
        //resolution, 1/resolution
        glm::vec4 ScreenParams;
        //frame index, time
        glm::vec4 FrameParams;
        //x: near, y: far, z: fov, w: aspect
        glm::vec4 CameraParams;
    };



	class VansCamera
	{
    private:

        //render backend引用
        VansGraphicsDevice* m_RenderDevice;

        CameraDataStruct m_CameraData;

        uint32_t m_RenderFrameIndex;

        bool  m_IsRightMouseDown;

    public:

        VkDescriptorSetLayout m_CameraBufferLayout;
        std::vector<VkDescriptorSet> m_CameraBufferDescriptorSets;

        //uniform buffer
        VansVKBuffer m_CameraDataBuffer;

    public:

        void SetRightMouseDown(bool down);

        void HandleMouseMovement(float deltaX, float deltaY);

        void HandleKeyboardInput(int key, int scancode, int action, int mods, float deltaTime);

        glm::vec4 GetPosition() { return glm::vec4(m_Position,1); }

        glm::vec4 GetForward();

        glm::vec4 GetRight();

        glm::vec4 GetUp();

        uint32_t GetFrameIndex()
        {
            return m_RenderFrameIndex;
        }

        void SetAspectRatio(float aspect) { m_AspectRatio = aspect; }

        glm::mat4 GetViewMatrix();

        glm::mat4 GetProjectiveMatrix();

    private:

        void SetCameraData(const glm::mat4& view_matrix, const glm::mat4& projective_matrix);

        glm::vec3 m_Position;
        
        glm::vec3 m_Rotation; // pitch, yaw, roll

        //support perspective projection
        float m_Fov = 45.0f;
        float m_AspectRatio = 1.0f;
        float m_NearClip = 0.01f;
        float m_FarClip = 10000.0f;
    public:

        VansCamera(glm::vec3 startPosition, glm::vec3 startRotation, VansGraphicsDevice* device)
            : m_Position(startPosition), m_Rotation(startRotation), m_RenderDevice(device)
        {
            m_AspectRatio = m_RenderDevice->GetAspectRatio();

            VkDescriptorSetLayoutBinding uniformBufferBinding =
            {
                GLOBAL_BINDING_CAMERA_UBO,
                VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                1,
                VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
                nullptr
            };
            VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ uniformBufferBinding }, m_CameraBufferLayout);
            VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_CameraBufferLayout }, m_CameraBufferDescriptorSets);

            //创建一个uniform buffer
            m_CameraDataBuffer.CreatVulkanBuffer(static_cast<VansVKDevice*>(device)->GetLogicDevice(), sizeof(m_CameraData), VK_FORMAT_R32_SFLOAT,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

            m_RenderFrameIndex = 0;

            m_IsRightMouseDown = false;
        }

        ~VansCamera();

        void Rendering();

        void Present();

        void* GetGraphicsDevice() {return m_RenderDevice;}

        float m_JitterX;
        float m_JitterY;
	};
}
