#pragma once
#include "../VansNode.h"
#include "../ScriptCore/VansCommonUtils.h"
#include "../ScriptCore/VansTransform.h"
#include "VansGraphicsDevice.h"
#include "VansRenderFrame.h"
#include "../SceneCore/VansSceneCameraSettingsConfig.h"
#include <climits>
using namespace VansGraphics;
namespace VansGraphics
{
	struct VansCameraControlPose;
	class VansCamera : public VansNode
	{
    private:
        //render backend引用
        VansGraphicsDevice* m_RenderDevice;

        bool m_IsRightMouseDown;

        // ── Transform 绑定 ──────────────────────────────────────────────────
        // UINT32_MAX 表示未绑定 Transform（降级路径，直接修改 m_Position/m_Rotation）
        uint32_t m_TransformID = UINT32_MAX;

    public:

        void SetRightMouseDown(bool down);

        void HandleMouseMovement(float deltaX, float deltaY);

        void HandleKeyboardInput(int key, int scancode, int action, int mods, float deltaTime);

        void HandleKeyboardMovement(float forwardAxis, float rightAxis, float upAxis, float deltaTime);

        glm::vec4 GetPosition() { return glm::vec4(m_Position,1); }
		VansCameraControlPose CaptureControlPose() const;
		void ApplyControlPose(const VansCameraControlPose& pose);
		void ApplyControlPoseChannels(const VansCameraControlPose& pose, std::uint32_t channels);

        glm::vec4 GetForward();

        glm::vec4 GetRight();

        glm::vec4 GetUp();

        void SetAspectRatio(float aspect) { m_AspectRatio = aspect; }

        // ── 相机参数 Getter / Setter ─────────────────────────────────────────
        float GetFov()      const { return m_Fov; }
        float GetNearClip() const { return m_NearClip; }
        float GetFarClip()  const { return m_FarClip; }
        float GetAspectRatio() const { return m_AspectRatio; }

        void SetFov(float fov)       { m_Fov      = fov; }
        void SetNearClip(float val);
        void SetFarClip(float val);

        // ── Transform 绑定与同步 ─────────────────────────────────────────────
        // 绑定 camera object 的 transformID，之后 input 与渲染均通过 Transform 驱动
        void     SetTransformID(uint32_t id) { m_TransformID = id; }
        uint32_t GetTransformID()      const { return m_TransformID; }
        bool     HasTransform()        const { return m_TransformID != UINT32_MAX; }
        void     DetachTransformPreservingPose();

        // 从绑定的 Transform 读取 position 和 rotation(pitch/yaw) 写入相机成员。
        // 每帧在构建 render view snapshot 前调用，确保只发布已 resolve 的主线程数据。
        void SyncFromTransform();

        glm::mat4 GetViewMatrix();

        glm::mat4 GetProjectiveMatrix();

		VansRenderViewSnapshot BuildRenderViewSnapshot(
			std::uint32_t viewportWidth,
			std::uint32_t viewportHeight);

        // 将世界坐标投影到左上角为原点的归一化视口坐标。
        // 返回 false 表示点位于相机后方或视锥之外。
        bool ProjectWorldToViewport(const glm::vec3& worldPosition, glm::vec3& viewportPosition);

        void ApplyCameraSettings(const Vans::VansSceneCameraSettingsConfig& cameraSettings);

        void ResetToDefaults();

    private:
        glm::vec3 m_Position;
        
        glm::vec3 m_Rotation; // pitch, yaw, roll

        //support perspective projection
        float m_Fov = 45.0f;
        float m_AspectRatio = 1.0f;
        float m_NearClip = 0.1f;
        float m_FarClip = 10000.0f;
    public:

        VansCamera(VansGraphicsDevice* device);

        ~VansCamera();

        void* GetGraphicsDevice() {return m_RenderDevice;}

	};
}
