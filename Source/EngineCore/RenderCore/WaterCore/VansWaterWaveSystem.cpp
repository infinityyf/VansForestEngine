#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansWaterWaveSystem.h"
#include "VansWaterFFT.h"
#include "../../Util/VansLog.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../../Configration/VansConfigration.h"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace VansGraphics
{

bool VansWaterWaveSystem::AllocateBuffer(
    VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props,
    VkBuffer& outBuffer, VkDeviceMemory& outMemory)
{
    VkDevice device = m_Device->GetLogicDevice();
    VkBufferCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &ci, nullptr, &outBuffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, outBuffer, &memReq);
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_Device->GetPhysicalDevice(), &memProps);
    uint32_t memTypeIndex = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
    {
        if ((memReq.memoryTypeBits & (1u << i)) &&
            (memProps.memoryTypes[i].propertyFlags & props) == props)
        { memTypeIndex = i; break; }
    }
    if (memTypeIndex == UINT32_MAX) { vkDestroyBuffer(device, outBuffer, nullptr); outBuffer = VK_NULL_HANDLE; return false; }
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = memReq.size;
    ai.memoryTypeIndex = memTypeIndex;
    if (vkAllocateMemory(device, &ai, nullptr, &outMemory) != VK_SUCCESS)
    { vkDestroyBuffer(device, outBuffer, nullptr); outBuffer = VK_NULL_HANDLE; return false; }
    vkBindBufferMemory(device, outBuffer, outMemory, 0);
    return true;
}

void VansWaterWaveSystem::AddWave(const GerstnerWaveGPU& w)
{
    if (m_Waves.size() < MAX_WAVE_COUNT)
        m_Waves.push_back(w);
}

void VansWaterWaveSystem::RemoveWave(uint32_t index)
{
    if (index < m_Waves.size())
        m_Waves.erase(m_Waves.begin() + index);
}

void VansWaterWaveSystem::UpdateSSBO(VkDevice logicDevice)
{
    if (m_WaveSSBO == VK_NULL_HANDLE || m_Waves.empty())
        return;
    VkDeviceSize size = m_Waves.size() * sizeof(GerstnerWaveGPU);
    void* data = nullptr;
    vkMapMemory(logicDevice, m_WaveSSBOMemory, 0, size, 0, &data);
    std::memcpy(data, m_Waves.data(), static_cast<size_t>(size));
    vkUnmapMemory(logicDevice, m_WaveSSBOMemory);
}

void VansWaterWaveSystem::AutoGenerateWaves(int count, const glm::vec2& windDir,
                                              float swellAmplitude, float windSpeed)
{
    m_Waves.clear();
    m_Waves.reserve(count);
    glm::vec2 dir = glm::normalize(windDir);
    if (glm::length(dir) < 0.001f) dir = glm::vec2(0.7071f, 0.7071f);

    const float PI = 3.14159265358979323846f;
    const float GRAVITY = 9.81f;
    const float minWL = 0.5f;
    const float maxWL = 32.0f;

    for (int i = 0; i < count; ++i)
    {
        float t = (count <= 1) ? 0.0f : static_cast<float>(i) / static_cast<float>(count - 1);
        float wavelength = maxWL * std::powf(minWL / maxWL, t);
        float k = 2.0f * PI / wavelength;
        float omega = std::sqrtf(GRAVITY * k);
        float speed = omega / k;
        float baseAmp = swellAmplitude * std::powf(wavelength / maxWL, 0.75f);
        float angleSpread = (static_cast<float>((i * 7 + 3) % 17) / 17.0f - 0.5f) * 0.6f;
        float angle = std::atan2f(dir.y, dir.x) + angleSpread;

        GerstnerWaveGPU wave = {};
        wave.amplitude  = baseAmp;
        wave.wavelength = wavelength;
        wave.directionX = std::cosf(angle);
        wave.directionY = std::sinf(angle);
        wave.speed      = speed;
        wave.steepness  = 0.05f + 0.55f * (1.0f - t);
        m_Waves.push_back(wave);
    }
}

bool VansWaterWaveSystem::Initialize(VansVKDevice* device, const std::string& shaderRoot)
{
    m_Device = device;
    VkDevice logicDev = device->GetLogicDevice();

    // Compile wave compute shader
    m_WaveSimShader = new VansComputeShader();
    m_WaveSimShader->InitShader(logicDev, (shaderRoot + "EngineAssets/Shaders/Water/WaterWave").c_str());

    // Create displacement Texture2DArray (256² × 10, RGBA16F)
    m_WaveDisplacementImage.CreateVulkanImage(
        logicDev,
        { WAVE_TEXTURE_SIZE, WAVE_TEXTURE_SIZE, 1 },
        VK_FORMAT_R16G16B16A16_SFLOAT,
        1, MAX_LOD_COUNT,
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        VK_SAMPLE_COUNT_1_BIT,
        false, false, true);

    // Create SSBO (64 waves × 32B = 2KB)
    VkDeviceSize ssboSize = MAX_WAVE_COUNT * sizeof(GerstnerWaveGPU);
    AllocateBuffer(ssboSize,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_WaveSSBO, m_WaveSSBOMemory);

    // Auto-generate default waves
    AutoGenerateWaves(32, glm::vec2(0.7071f, 0.7071f), 1.5f, 12.0f);
    UpdateSSBO(logicDev);

    m_Initialized = true;
    VANS_LOG("[VansWaterWaveSystem] Initialize: " << m_Waves.size() << " waves, mode=Gerstner");
    return true;
}

void VansWaterWaveSystem::Shutdown(VkDevice logicDevice)
{
    if (m_WaveSSBO != VK_NULL_HANDLE) { vkDestroyBuffer(logicDevice, m_WaveSSBO, nullptr); m_WaveSSBO = VK_NULL_HANDLE; }
    if (m_WaveSSBOMemory != VK_NULL_HANDLE) { vkFreeMemory(logicDevice, m_WaveSSBOMemory, nullptr); m_WaveSSBOMemory = VK_NULL_HANDLE; }
    m_WaveDisplacementImage.DestroyVulkanImage(logicDevice);
    m_WaveDisplacementReady = false;
    delete m_WaveSimShader; m_WaveSimShader = nullptr;
    delete m_FFT; m_FFT = nullptr;
    m_Waves.clear();
    m_Initialized = false;
    m_Device = nullptr;
}

void VansWaterWaveSystem::UpdateWaveSimulation(VansVKCommandBuffer& cmd, float /*deltaTime*/, const glm::vec3& /*cameraPos*/)
{
    if (!m_Initialized || m_WaveSimShader == nullptr || m_WaveSimSet == VK_NULL_HANDLE)
    {
        if (m_Initialized && !m_WaveSimSet)
            VANS_LOG_WARN("[VansWaterWaveSystem] Descriptor sets not created — call SetupDescriptors before UpdateWaveSimulation");
        return;
    }

    VkImageMemoryBarrier beforeCompute = {};
    beforeCompute.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    beforeCompute.srcAccessMask = m_WaveDisplacementReady ? VK_ACCESS_SHADER_READ_BIT : 0;
    beforeCompute.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    beforeCompute.oldLayout = m_WaveDisplacementReady ? VK_IMAGE_LAYOUT_GENERAL : m_WaveDisplacementImage.GetImageLayout();
    beforeCompute.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    beforeCompute.image = m_WaveDisplacementImage.GetImage();
    beforeCompute.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, MAX_LOD_COUNT };
    cmd.PipelineBarrier(
        m_WaveDisplacementReady ? VK_PIPELINE_STAGE_VERTEX_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {}, {}, { beforeCompute });
    m_WaveDisplacementImage.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);

    cmd.EnsureComputeShader(*m_WaveSimShader, { m_WaveSimLayout });
    const uint32_t groups = (WAVE_TEXTURE_SIZE + 7u) / 8u;
    cmd.DispatchCompute(*m_WaveSimShader, groups, groups, MAX_LOD_COUNT, { m_WaveSimSet });

    VkImageMemoryBarrier afterCompute = {};
    afterCompute.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    afterCompute.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    afterCompute.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    afterCompute.oldLayout = VK_IMAGE_LAYOUT_GENERAL;
    afterCompute.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    afterCompute.image = m_WaveDisplacementImage.GetImage();
    afterCompute.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, MAX_LOD_COUNT };
    cmd.PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, {}, {}, { afterCompute });

    m_WaveDisplacementReady = true;
}

} // namespace VansGraphics
