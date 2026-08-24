#include "VansCamera.h"
#include "VansCameraControlArbiter.h"
#include "../ScriptCore/VansTransform.h"
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
	const glm::mat4 inverseView = glm::inverse(GetViewMatrix());
    return glm::vec4(-glm::vec3(inverseView[2]), 0.0f);
}

glm::vec4 VansGraphics::VansCamera::GetRight()
{
	const glm::mat4 inverseView = glm::inverse(GetViewMatrix());
    return glm::vec4(glm::vec3(inverseView[0]), 0.0f);
}

glm::vec4 VansGraphics::VansCamera::GetUp()
{
	const glm::mat4 inverseView = glm::inverse(GetViewMatrix());
    return glm::vec4(glm::vec3(inverseView[1]), 0.0f);
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

VansGraphics::VansRenderViewSnapshot
VansGraphics::VansCamera::BuildRenderViewSnapshot(
	std::uint32_t viewportWidth,
	std::uint32_t viewportHeight)
{
	SyncFromTransform();

	VansRenderViewSnapshot snapshot;
	snapshot.cameraIdentity = static_cast<std::uint64_t>(
		reinterpret_cast<std::uintptr_t>(this));
	snapshot.view = GetViewMatrix();
	snapshot.projection = GetProjectiveMatrix();
	snapshot.position = m_Position;
	const glm::mat4 inverseView = glm::inverse(snapshot.view);
	snapshot.forward = glm::normalize(-glm::vec3(inverseView[2]));
	snapshot.up = glm::normalize(glm::vec3(inverseView[1]));
	snapshot.right = glm::normalize(glm::vec3(inverseView[0]));
	snapshot.nearClip = m_NearClip;
	snapshot.farClip = m_FarClip;
	snapshot.viewportWidth = viewportWidth;
	snapshot.viewportHeight = viewportHeight;
	snapshot.fieldOfViewRadians = glm::radians(m_Fov);
	snapshot.aspectRatio = m_AspectRatio;
	return snapshot;
}

bool VansGraphics::VansCamera::ProjectWorldToViewport(
    const glm::vec3& worldPosition,
    glm::vec3& viewportPosition)
{
    // 脚本投影可能发生在 render frame snapshot 之前，因此主动读取最新绑定 Transform。
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
}
