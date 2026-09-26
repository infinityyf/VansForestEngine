#include "VansCamera.h"
#include "../SceneRuntime/Transform/VansTransformStore.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <iostream>

namespace
{
    constexpr float kEditorCameraMoveSpeed = 12.0f;
    constexpr float kEditorCameraMaxMoveDeltaTime = 1.0f / 30.0f;
}

VansGraphics::VansCamera::VansCamera(VansGraphicsDevice* device)
    : m_RenderDevice(device)
{
    // 编辑器初始视图；运行场景的 Camera component 会在发布时覆盖姿势与镜头。
    m_Position    = glm::vec3(0.0f, 1.0f, 5.0f);
    m_Rotation    = glm::vec3(0.0f, -90.0f, 0.0f);
    m_Fov         = 45.0f;
    m_NearClip    = 0.1f;
    m_FarClip     = 10000.0f;
    m_AspectRatio = m_RenderDevice->GetAspectRatio();

    m_IsRightMouseDown = false;
}

Vans::VansCameraViewSnapshot VansGraphics::VansCamera::CaptureView() const
{
	Vans::VansCameraViewSnapshot view;
	view.pose.position = m_Position;
	view.pose.rotationDegrees = m_Rotation;
	view.lens.fieldOfView = m_Fov;
	view.lens.nearClip = m_NearClip;
	view.lens.farClip = m_FarClip;
	return view;
}

void VansGraphics::VansCamera::ApplyView(const Vans::VansCameraViewSnapshot& view)
{
	Vans::VansCameraViewSnapshot applied = view;
	std::string diagnostic;
	if (!Vans::VansClampCameraView(applied, m_LensLimits, diagnostic))
	{
		VANS_LOG_ERROR("[Camera] Rejected view: " << diagnostic);
		return;
	}
	if (!diagnostic.empty())
		VANS_LOG_WARN("[Camera] ApplyView: " << diagnostic);
	m_Position = applied.pose.position;
	m_Rotation = applied.pose.rotationDegrees;
	m_Fov = applied.lens.fieldOfView;
	m_NearClip = applied.lens.nearClip;
	m_FarClip = applied.lens.farClip;
	if (m_TransformID != UINT32_MAX)
	{
		Vans::VansTransform transform = Vans::VansTransformStore::Read(m_TransformID);
		transform.m_Position = applied.pose.position;
		transform.m_Rotation = applied.pose.rotationDegrees;
		Vans::VansTransformStore::Write(m_TransformID, transform);
		Vans::VansTransformStore::MarkDirty(m_TransformID);
	}
}

bool VansGraphics::VansCamera::SetLensLimits(
	Vans::VansCameraLensLimits limits,
	std::string& error)
{
	if (!Vans::VansValidateCameraLensLimits(limits, error)) return false;
	m_LensLimits = limits;
	if (!ApplyLens(CaptureView().lens, "SetLensLimits"))
	{
		error = "Current camera lens could not be normalized to the configured limits";
		return false;
	}
	error.clear();
	return true;
}

bool VansGraphics::VansCamera::ApplyLens(
	Vans::VansCameraLens lens,
	const char* source)
{
	Vans::VansCameraViewSnapshot view = CaptureView();
	view.lens = lens;
	std::string diagnostic;
	if (!Vans::VansClampCameraView(view, m_LensLimits, diagnostic))
	{
		VANS_LOG_ERROR("[Camera] " << source << " rejected: " << diagnostic);
		return false;
	}
	if (!diagnostic.empty())
		VANS_LOG_WARN("[Camera] " << source << ": " << diagnostic);
	m_Fov = view.lens.fieldOfView;
	m_NearClip = view.lens.nearClip;
	m_FarClip = view.lens.farClip;
	return true;
}

void VansGraphics::VansCamera::SetFov(float value)
{
	Vans::VansCameraLens lens = CaptureView().lens;
	lens.fieldOfView = value;
	ApplyLens(lens, "SetFov");
}

void VansGraphics::VansCamera::SetNearClip(float val)
{
	Vans::VansCameraLens lens = CaptureView().lens;
	lens.nearClip = val;
	ApplyLens(lens, "SetNearClip");
}

void VansGraphics::VansCamera::SetFarClip(float val)
{
	Vans::VansCameraLens lens = CaptureView().lens;
	lens.farClip = val;
	ApplyLens(lens, "SetFarClip");
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

    const Vans::VansTransform& t =
        Vans::VansTransformStore::Read(m_TransformID);

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
        Vans::VansTransform t =
            Vans::VansTransformStore::Read(m_TransformID);
        t.m_Rotation.x = newPitch;
        t.m_Rotation.y = newYaw;
		Vans::VansTransformStore::Write(m_TransformID, t);
        Vans::VansTransformStore::MarkDirty(m_TransformID);
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
        Vans::VansTransform t =
            Vans::VansTransformStore::Read(m_TransformID);
        t.m_Position += delta;
		Vans::VansTransformStore::Write(m_TransformID, t);
        Vans::VansTransformStore::MarkDirty(m_TransformID);
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
