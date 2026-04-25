#include "VansCamera.h"
#include "../ScriptCore/VansTransform.h"
#include "../VansTimer.h"
#include "../Util/VansLog.h"

#include <iostream>

VansGraphics::VansCamera::VansCamera(VansGraphicsDevice* device)
    : m_RenderDevice(device)
{
    // Default camera parameters (overridden by ApplyCameraSettings if camera node exists in scene JSON)
    m_Position    = glm::vec3(0.0f, 1.0f, 5.0f);
    m_Rotation    = glm::vec3(0.0f, -90.0f, 0.0f);
    m_Fov         = 45.0f;
    m_NearClip    = 0.01f;
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
    VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ uniformBufferBinding }, m_CameraBufferLayout);
    VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_CameraBufferLayout }, m_CameraBufferDescriptorSets);

    // Create uniform buffer
    m_CameraDataBuffer.CreatVulkanBuffer(static_cast<VansVKDevice*>(device)->GetLogicDevice(), sizeof(m_CameraData), VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);

    m_RenderFrameIndex = 0;
    m_IsRightMouseDown = false;
}

void VansGraphics::VansCamera::ApplyCameraSettings(const json& cameraNode)
{
    if (cameraNode.contains("position"))
    {
        const auto& pos = cameraNode["position"];
        m_Position = glm::vec3(pos[0].get<float>(), pos[1].get<float>(), pos[2].get<float>());
    }

    if (cameraNode.contains("rotation"))
    {
        const auto& rot = cameraNode["rotation"];
        m_Rotation = glm::vec3(rot[0].get<float>(), rot[1].get<float>(), rot[2].get<float>());
    }

    if (cameraNode.contains("fov"))
        m_Fov = cameraNode["fov"].get<float>();

    if (cameraNode.contains("nearClip"))
        m_NearClip = cameraNode["nearClip"].get<float>();

    if (cameraNode.contains("farClip"))
        m_FarClip = cameraNode["farClip"].get<float>();

    VANS_LOG("[VansCamera] Camera settings applied from scene JSON: pos=("
        << m_Position.x << ", " << m_Position.y << ", " << m_Position.z
        << ") rot=(" << m_Rotation.x << ", " << m_Rotation.y << ", " << m_Rotation.z
        << ") fov=" << m_Fov);
}

void VansGraphics::VansCamera::ResetToDefaults()
{
    m_Position    = glm::vec3(0.0f, 1.0f, 5.0f);
    m_Rotation    = glm::vec3(0.0f, -90.0f, 0.0f);
    m_Fov         = 45.0f;
    m_NearClip    = 0.01f;
    m_FarClip     = 10000.0f;

    VANS_LOG("[VansCamera] No camera node in scene JSON, using default parameters");
}
void VansGraphics::VansCamera::SetRightMouseDown(bool down) 
{ 
    m_IsRightMouseDown = down; 
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

    const float speed = 200.0f * deltaTime;

    // 从当前 pitch/yaw 计算 front/right/up（与 GetViewMatrix 保持一致）
    glm::vec3 front;
    front.x = cos(glm::radians(m_Rotation.y)) * cos(glm::radians(m_Rotation.x));
    front.y = sin(glm::radians(m_Rotation.x));
    front.z = sin(glm::radians(m_Rotation.y)) * cos(glm::radians(m_Rotation.x));
    front = glm::normalize(front);

    const glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 up    = glm::normalize(glm::cross(right, front));

    glm::vec3 delta(0.0f);
    switch (key)
    {
    case GLFW_KEY_W: delta =  front * speed; break;
    case GLFW_KEY_S: delta = -front * speed; break;
    case GLFW_KEY_A: delta = -right * speed; break;
    case GLFW_KEY_D: delta =  right * speed; break;
    case GLFW_KEY_Q: delta = -up    * speed; break;
    case GLFW_KEY_E: delta =  up    * speed; break;
    default: return;
    }

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
    return m_CameraData.ViewMatrix[2];
}

glm::vec4 VansGraphics::VansCamera::GetRight()
{
    return m_CameraData.ViewMatrix[0];
}

glm::vec4 VansGraphics::VansCamera::GetUp()
{
    return m_CameraData.ViewMatrix[1];
}

void VansGraphics::VansCamera::SetCameraData(const glm::mat4& view_matrix, const glm::mat4& projective_matrix)
{
// Sub鈥憄ixel jitter (Halton 2,3) for TAA / upscale
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

    // Convert to clip space offsets (NDC) 鈥?multiply by 2 because clip x,y span [-1,1]
    m_JitterX =  (jitterPixelX / width) * 2.0f;
    m_JitterY =  (jitterPixelY / height) * 2.0f;

    glm::mat4 jitteredProj = projective_matrix;
    // GLM uses column-major; modify row 2 (Z) columns 0/1 to shift X/Y
    jitteredProj[2][0] += m_JitterX;
    jitteredProj[2][1] += m_JitterY;

    // Store camera data
    m_CameraData.CameraPosition   = glm::vec4(m_Position, 1.0f);
    m_CameraData.CameraDirection  = glm::vec4(-view_matrix[2]);
    m_CameraData.LastPrevViewMatrix = m_CameraData.LastViewMatrix;
    m_CameraData.LastPrevProjectionMatrix = m_CameraData.LastProjectionMatrix;
    m_CameraData.LastPrevVPMatrix = m_CameraData.LastVPMatrix;

    m_CameraData.LastViewMatrix = m_CameraData.ViewMatrix;
    m_CameraData.LastProjectionMatrix = m_CameraData.ProjectionMatrix;
    m_CameraData.LastVPMatrix = m_CameraData.VPMatrix;

    m_CameraData.ViewMatrix       = view_matrix;
    m_CameraData.ProjectionMatrix = jitteredProj;
    m_CameraData.VPMatrix = jitteredProj * view_matrix;

    m_CameraData.InverseViewMatrix       = glm::inverse(view_matrix);
    m_CameraData.InverseProjectionMatrix = glm::inverse(jitteredProj);
    m_CameraData.ScreenParams     = glm::vec4(width, height, 1.0f / width, 1.0f / height);

    float time = VansTimer::GetFrameTime();
    m_CameraData.FrameParams  = glm::vec4(m_RenderFrameIndex, time, 0, 0);
    m_CameraData.CameraParams = glm::vec4(m_NearClip, m_FarClip, m_Fov, m_AspectRatio);

    m_CameraDataBuffer.SetBufferData(&m_CameraData, 0, sizeof(m_CameraData));

    VansVKDescriptorManager::GetInstance()->ResetState();
    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_CameraBufferDescriptorSets[0],
            GLOBAL_BINDING_CAMERA_UBO,
            0,
            VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            {
                {
                    m_CameraDataBuffer.GetNativeBuffer(),
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
