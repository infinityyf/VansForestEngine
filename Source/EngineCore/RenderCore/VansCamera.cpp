#include "VansCamera.h"
#include "VansCameraControlArbiter.h"
#include "../ScriptCore/VansTransform.h"
#include "VulkanCore/VansDescriptorSetLayouts.h"
#include "../VansTimer.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <iostream>

namespace
{
    constexpr float kEditorCameraMoveSpeed = 12.0f;
    constexpr float kEditorCameraMaxMoveDeltaTime = 1.0f / 30.0f;
    constexpr float kDefaultCameraNearClip = 0.1f;
    constexpr float kCameraNearClipMinimum = 0.1f;
    constexpr float kCameraFarClipMinimumSeparation = 0.001f;
}

VansGraphics::VansCamera::VansCamera(VansGraphicsDevice* device)
    : m_RenderDevice(device)
{
    // Default camera parameters (overridden by ApplyCameraSettings if camera node exists in scene JSON)
    m_Position    = glm::vec3(0.0f, 1.0f, 5.0f);
    m_Rotation    = glm::vec3(0.0f, -90.0f, 0.0f);
    m_Fov         = 45.0f;
    m_NearClip    = kDefaultCameraNearClip;
    m_FarClip     = 10000.0f;
    m_AspectRatio = m_RenderDevice->GetAspectRatio();

    VkDescriptorSetLayoutBinding uniformBufferBinding =
    {
        GLOBAL_BINDING_CAMERA_UBO,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        1,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };
    VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(
        { uniformBufferBinding },
        m_CameraBufferLayout,
        m_CameraBufferDescriptorSets);

    // Create uniform buffer
    m_CameraDataBuffer.CreatVulkanBuffer(static_cast<VansVKDevice*>(device)->GetLogicDevice(), sizeof(m_CameraData), VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    m_RenderFrameIndex = 0;
    m_IsRightMouseDown = false;
}

void VansGraphics::VansCamera::ApplyCameraSettings(
    const Vans::VansSceneCameraSettingsConfig& cameraSettings)
{
    if (cameraSettings.position)
    {
        const auto& pos = *cameraSettings.position;
        m_Position = glm::vec3(pos[0], pos[1], pos[2]);
    }

    if (cameraSettings.rotation)
    {
        const auto& rot = *cameraSettings.rotation;
        m_Rotation = glm::vec3(rot[0], rot[1], rot[2]);
    }

    if (cameraSettings.fov) m_Fov = *cameraSettings.fov;
    if (cameraSettings.nearClip) SetNearClip(*cameraSettings.nearClip);
    if (cameraSettings.farClip) SetFarClip(*cameraSettings.farClip);

    VANS_LOG("[VansCamera] Camera settings applied: pos=("
        << m_Position.x << ", " << m_Position.y << ", " << m_Position.z
        << ") rot=(" << m_Rotation.x << ", " << m_Rotation.y << ", " << m_Rotation.z
        << ") fov=" << m_Fov);
}

VansGraphics::VansCameraControlPose VansGraphics::VansCamera::CaptureControlPose() const
{
	VansCameraControlPose pose;
	pose.position = m_Position;
	pose.rotationDegrees = m_Rotation;
	pose.fieldOfView = m_Fov;
	pose.nearClip = m_NearClip;
	pose.farClip = m_FarClip;
	return pose;
}

void VansGraphics::VansCamera::ApplyControlPose(const VansCameraControlPose& pose)
{
	m_Position = pose.position;
	m_Rotation = pose.rotationDegrees;
	m_Fov = pose.fieldOfView;
	SetNearClip(pose.nearClip);
	SetFarClip(pose.farClip);
	if (m_TransformID != UINT32_MAX)
	{
		VansTransform& transform = VansTransformStore::GetTransform(m_TransformID);
		transform.m_Position = pose.position;
		transform.m_Rotation = pose.rotationDegrees;
		VansTransformStore::TransformIDToTransformDirty[m_TransformID] = true;
	}
}

void VansGraphics::VansCamera::ApplyControlPoseChannels(
	const VansCameraControlPose& pose,
	std::uint32_t channels)
{
	if (channels & 0x01u) m_Position = pose.position;
	if (channels & 0x02u) m_Rotation = pose.rotationDegrees;
	if (channels & 0x04u) m_Fov = pose.fieldOfView;
	if (channels & 0x08u) SetNearClip(pose.nearClip);
	if (channels & 0x10u) SetFarClip(pose.farClip);
	if (m_TransformID != UINT32_MAX && (channels & 0x03u))
	{
		VansTransform& transform = VansTransformStore::GetTransform(m_TransformID);
		if (channels & 0x01u) transform.m_Position = pose.position;
		if (channels & 0x02u) transform.m_Rotation = pose.rotationDegrees;
		VansTransformStore::TransformIDToTransformDirty[m_TransformID] = true;
	}
}

void VansGraphics::VansCamera::ResetToDefaults()
{
    m_Position    = glm::vec3(0.0f, 1.0f, 5.0f);
    m_Rotation    = glm::vec3(0.0f, -90.0f, 0.0f);
    m_Fov         = 45.0f;
    m_NearClip    = kDefaultCameraNearClip;
    m_FarClip     = 10000.0f;

    VANS_LOG("[VansCamera] No camera node in scene JSON, using default parameters");
}

void VansGraphics::VansCamera::SetNearClip(float val)
{
    m_NearClip = std::max(val, kCameraNearClipMinimum);
    m_FarClip = std::max(m_FarClip, m_NearClip + kCameraFarClipMinimumSeparation);
}

void VansGraphics::VansCamera::SetFarClip(float val)
{
    m_FarClip = std::max(val, m_NearClip + kCameraFarClipMinimumSeparation);
}
void VansGraphics::VansCamera::SetRightMouseDown(bool down) 
{ 
    m_IsRightMouseDown = down; 
}

void VansGraphics::VansCamera::DetachTransformPreservingPose()
{
    SyncFromTransform();
    m_TransformID = UINT32_MAX;
}

// 从绑定的 Transform 同步 position 和 rotation(pitch/yaw) 到相机成员。
// roll(z) 不影响相机（GetViewMatrix 固定使用世界上方 (0,1,0)）。
void VansGraphics::VansCamera::SyncFromTransform()
{
    if (m_TransformID == UINT32_MAX)
        return;

    const VansGraphics::VansTransform& t =
        VansGraphics::VansTransformStore::GetTransform(m_TransformID);

    // position 完全同步
    m_Position   = t.m_Position;
    // 只同步 pitch(x) 和 yaw(y)；roll(z) 不同步
    m_Rotation.x = t.m_Rotation.x; // pitch
    m_Rotation.y = t.m_Rotation.y; // yaw
}
void VansGraphics::VansCamera::HandleMouseMovement(float deltaX, float deltaY)
{
    if (!m_IsRightMouseDown) return;

    const float sensitivity = 0.1f;
    float newYaw   = m_Rotation.y + deltaX * sensitivity;
    float newPitch = m_Rotation.x - deltaY * sensitivity;

    // 限制仰屰角，防止万向锁死
    newPitch = glm::clamp(newPitch, -89.0f, 89.0f);

    if (m_TransformID != UINT32_MAX)
    {
        // 目标路径：修改 Transform，帧开始前 SyncFromTransform 将新值拉回相机
        VansGraphics::VansTransform& t =
            VansGraphics::VansTransformStore::GetTransform(m_TransformID);
        t.m_Rotation.x = newPitch;
        t.m_Rotation.y = newYaw;
        VansGraphics::VansTransformStore::TransformIDToTransformDirty[m_TransformID] = true;
    }
    else
    {
        // 降级路径：无 transform 时直接修改相机成员
        m_Rotation.y = newYaw;
        m_Rotation.x = newPitch;
    }
}

void VansGraphics::VansCamera::HandleKeyboardInput(int key, int scancode, int action, int mods, float deltaTime)
{
    if (!m_IsRightMouseDown) return;

    float forwardAxis = 0.0f;
    float rightAxis = 0.0f;
    float upAxis = 0.0f;
    switch (key)
    {
    case GLFW_KEY_W: forwardAxis =  1.0f; break;
    case GLFW_KEY_S: forwardAxis = -1.0f; break;
    case GLFW_KEY_A: rightAxis   = -1.0f; break;
    case GLFW_KEY_D: rightAxis   =  1.0f; break;
    case GLFW_KEY_Q: upAxis      = -1.0f; break;
    case GLFW_KEY_E: upAxis      =  1.0f; break;
    default: return;
    }

    HandleKeyboardMovement(forwardAxis, rightAxis, upAxis, deltaTime);
}

void VansGraphics::VansCamera::HandleKeyboardMovement(float forwardAxis, float rightAxis, float upAxis, float deltaTime)
{
    if (!m_IsRightMouseDown) return;

    glm::vec3 localMove(rightAxis, upAxis, forwardAxis);
    if (glm::dot(localMove, localMove) <= 0.0f)
        return;

    localMove = glm::normalize(localMove);

    const float clampedDeltaTime = std::clamp(deltaTime, 0.0f, kEditorCameraMaxMoveDeltaTime);
    const float speed = kEditorCameraMoveSpeed * clampedDeltaTime;

    // 从当前 pitch/yaw 计算 front/right/up（与 GetViewMatrix 保持一致）
    glm::vec3 front;
    front.x = cos(glm::radians(m_Rotation.y)) * cos(glm::radians(m_Rotation.x));
    front.y = sin(glm::radians(m_Rotation.x));
    front.z = sin(glm::radians(m_Rotation.y)) * cos(glm::radians(m_Rotation.x));
    front = glm::normalize(front);

    const glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up    = glm::normalize(glm::cross(right, front));

    const glm::vec3 delta = (front * localMove.z + right * localMove.x + up * localMove.y) * speed;

    if (m_TransformID != UINT32_MAX)
    {
        // 目标路径：修改 Transform position
        VansGraphics::VansTransform& t =
            VansGraphics::VansTransformStore::GetTransform(m_TransformID);
        t.m_Position += delta;
        VansGraphics::VansTransformStore::TransformIDToTransformDirty[m_TransformID] = true;
    }
    else
    {
        // 降级路径：直接修改相机成员
        m_Position += delta;
    }
}

glm::vec4 VansGraphics::VansCamera::GetForward()
{
    // GLM matrices are column-major.  ViewMatrix[2] is not the camera's
    // world-space forward vector once the camera rotates.  The inverse-view
    // basis columns are the camera axes in world space; local -Z is forward.
    return glm::vec4(-glm::vec3(m_CameraData.InverseViewMatrix[2]), 0.0f);
}

glm::vec4 VansGraphics::VansCamera::GetRight()
{
    return glm::vec4(glm::vec3(m_CameraData.InverseViewMatrix[0]), 0.0f);
}

glm::vec4 VansGraphics::VansCamera::GetUp()
{
    return glm::vec4(glm::vec3(m_CameraData.InverseViewMatrix[1]), 0.0f);
}

void VansGraphics::VansCamera::SetCameraData(const glm::mat4& view_matrix, const glm::mat4& projective_matrix)
{
// Sub-pixel jitter (Halton 2,3) for TAA / upscale
    auto halton = [](uint32_t i, uint32_t b)->float {
        float f = 1.0f;
        float r = 0.0f;
        uint32_t x = i;
        while (x > 0) {
            f /= float(b);
            r += f * float(x % b);
            x /= b;
        }
        return r;
    };

    float width  = m_RenderDevice->GetNativeRenderWidth();
    float height = m_RenderDevice->GetNativeRenderHeight();

    uint32_t seqIndex = m_RenderFrameIndex & 1023u; // wrap to avoid precision drift
    float h2 = halton(seqIndex, 2);
    float h3 = halton(seqIndex, 3);

    // Centered jitter in [-0.5,0.5]
    float jitterPixelX = (h2 - 0.5f);
    float jitterPixelY = (h3 - 0.5f);

    // 优先使用 FSR 内置抖动序列（针对当前缩放比例优化），否则回退到 Halton(2,3)
    float fsrJx = 0.0f, fsrJy = 0.0f;
    if (m_RenderDevice->GetFSRJitterOffset(seqIndex, fsrJx, fsrJy))
    {
        jitterPixelX = fsrJx;
        jitterPixelY = fsrJy;
    }

    // 保存像素空间抖动值，供 FSR DispatchUpscale 直接使用（无需再除以分辨率）
    m_JitterPixelX = jitterPixelX;
    m_JitterPixelY = jitterPixelY;

    // Convert to clip space offsets (NDC) — multiply by 2 because clip x,y span [-1,1]
    m_JitterX =  (jitterPixelX / width) * 2.0f;
    m_JitterY =  (jitterPixelY / height) * 2.0f;

    glm::mat4 jitteredProj = projective_matrix;
    // GLM uses column-major; modify row 2 (Z) columns 0/1 to shift X/Y
    jitteredProj[2][0] += m_JitterX;
    jitteredProj[2][1] += m_JitterY;

    const glm::mat4 inverseView = glm::inverse(view_matrix);

    // Store camera data
    m_CameraData.CameraPosition   = glm::vec4(m_Position, 1.0f);
    m_CameraData.CameraDirection  = glm::vec4(-glm::vec3(inverseView[2]), 0.0f);
    m_CameraData.LastPrevViewMatrix = m_CameraData.LastViewMatrix;
    m_CameraData.LastPrevProjectionMatrix = m_CameraData.LastProjectionMatrix;
    m_CameraData.LastPrevVPMatrix = m_CameraData.LastVPMatrix;

    m_CameraData.LastViewMatrix = m_CameraData.ViewMatrix;
    m_CameraData.LastProjectionMatrix = m_CameraData.ProjectionMatrix;
    m_CameraData.LastVPMatrix = m_CameraData.VPMatrix;

    m_CameraData.ViewMatrix       = view_matrix;
    m_CameraData.ProjectionMatrix = jitteredProj;
    m_CameraData.VPMatrix = jitteredProj * view_matrix;

    // 保存未经 jitter 的 VP，用于 MotionVector pass，保证静止时速度场精确为零
    glm::mat4 unjitteredVP = projective_matrix * view_matrix;
    m_CameraData.LastUnjitteredVPMatrix = (m_RenderFrameIndex == 0) ? unjitteredVP : m_CameraData.UnjitteredVPMatrix;
    m_CameraData.UnjitteredVPMatrix     = unjitteredVP;

    m_CameraData.InverseViewMatrix       = inverseView;
    m_CameraData.InverseProjectionMatrix = glm::inverse(jitteredProj);
    m_CameraData.ScreenParams     = glm::vec4(width, height, 1.0f / width, 1.0f / height);

    float time = VansTimer::GetFrameTime();
    m_CameraData.FrameParams  = glm::vec4(
        m_RenderFrameIndex,
        time,
        m_RenderDevice->GetUpscaleMipBias(),
        0.0f);
    m_CameraData.CameraParams = glm::vec4(m_NearClip, m_FarClip, m_Fov, m_AspectRatio);

    m_CameraDataBuffer.SetBufferData(&m_CameraData, 0, sizeof(m_CameraData));

    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();
    descManager->WriteBufferDescriptor(
        m_CameraBufferDescriptorSets[0],
        GLOBAL_BINDING_CAMERA_UBO,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        {{
            m_CameraDataBuffer.GetNativeBuffer(),
            0,
            m_CameraDataBuffer.GetBufferSize()
        }});
    descManager->CommitDescriptorUpdates();
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

bool VansGraphics::VansCamera::ProjectWorldToViewport(
    const glm::vec3& worldPosition,
    glm::vec3& viewportPosition)
{
    // 脚本投影发生在 Rendering() 之前，因此这里主动读取最新绑定 Transform。
    SyncFromTransform();
    const glm::vec4 clip = GetProjectiveMatrix() * GetViewMatrix() * glm::vec4(worldPosition, 1.0f);
    if (clip.w <= 0.0001f)
        return false;

    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    viewportPosition = glm::vec3(
        ndc.x * 0.5f + 0.5f,
        0.5f - ndc.y * 0.5f,
        ndc.z);

    return ndc.x >= -1.0f && ndc.x <= 1.0f &&
           ndc.y >= -1.0f && ndc.y <= 1.0f &&
           ndc.z >= -1.0f && ndc.z <= 1.0f;
}

VansGraphics::VansCamera::~VansCamera()
{
    VansVKDescriptorManager::GetInstance()->DestroyDescriptorSetLayout(m_CameraBufferLayout);
    VansVKDescriptorManager::GetInstance()->DestroyDescriptorSet(m_CameraBufferDescriptorSets);

    m_CameraDataBuffer.DestroyVulkanBuffer(static_cast<VansVKDevice*>(m_RenderDevice)->GetLogicDevice());
}
void VansGraphics::VansCamera::Rendering()
{
	m_RenderDevice->PrepareRenderingFrame();
    // 若绑定了 Transform，先同步位置/旋转再构建矩阵，确保 GetViewMatrix 使用最新数据
    SyncFromTransform();

    SetCameraData(GetViewMatrix(), GetProjectiveMatrix());
    m_RenderDevice->Rendering();

    m_RenderFrameIndex++;
}

void VansGraphics::VansCamera::Present()
{
    m_RenderDevice->Present();
}
