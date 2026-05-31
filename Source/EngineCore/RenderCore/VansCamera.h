#pragma once
#include "../ScriptCore/VansCommonUtils.h"
#include "../ScriptCore/VansTransform.h"
#include "VansGraphicsDevice.h"
#include "VulkanCore/VansVKDevice.h"
#include "VulkanCore/VansVKDescriptorManager.h"
#include <nlohmann/json.hpp>
#include <vector>
#include <climits>
using namespace VansGraphics;
using json = nlohmann::json;
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
        // 未经 jitter 偏移的 VP 矩阵，用于 MotionVector pass，保证静止时速度场精确为零
        glm::mat4x4 UnjitteredVPMatrix;
        glm::mat4x4 LastUnjitteredVPMatrix;
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

        bool m_IsRightMouseDown;

        // ── Transform 绑定 ──────────────────────────────────────────────────
        // UINT32_MAX 表示未绑定 Transform（降级路径，直接修改 m_Position/m_Rotation）
        uint32_t m_TransformID = UINT32_MAX;

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

        // ── 相机参数 Getter / Setter ─────────────────────────────────────────
        float GetFov()      const { return m_Fov; }
        float GetNearClip() const { return m_NearClip; }
        float GetFarClip()  const { return m_FarClip; }

        void SetFov(float fov)       { m_Fov      = fov; }
        void SetNearClip(float val)  { m_NearClip = val; }
        void SetFarClip(float val)   { m_FarClip  = val; }

        // ── Transform 绑定与同步 ─────────────────────────────────────────────
        // 绑定 camera object 的 transformID，之后 input 与渲染均通过 Transform 驱动
        void     SetTransformID(uint32_t id) { m_TransformID = id; }
        uint32_t GetTransformID()      const { return m_TransformID; }
        bool     HasTransform()        const { return m_TransformID != UINT32_MAX; }

        // 从绑定的 Transform 读取 position 和 rotation(pitch/yaw) 写入相机成员。
        // 每帧在 Rendering() 最前端调用，确保视图矩阵使用最新 transform 数据。
        void SyncFromTransform();

        glm::mat4 GetViewMatrix();

        glm::mat4 GetProjectiveMatrix();

        void ApplyCameraSettings(const json& cameraNode);

        void ResetToDefaults();

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

        VansCamera(VansGraphicsDevice* device);

        ~VansCamera();

        void Rendering();

        void Present();

        void* GetGraphicsDevice() {return m_RenderDevice;}

        float m_JitterX;
        float m_JitterY;
        // 像素空间抖动偏移（[-0.5, 0.5]），直接传给 FSR DispatchUpscale
        float m_JitterPixelX = 0.0f;
        float m_JitterPixelY = 0.0f;
	};
}
