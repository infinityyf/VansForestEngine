#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansWaterFFT.h"
#include "../../Util/VansLog.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include <cstring>

namespace VansGraphics
{

namespace
{
    struct alignas(16) FFTSpectrumParams
    {
        float baseScale;
        glm::vec2 windDir;
        float windSpeed;
        float amplitude;
        float pad[3];
    };

    struct alignas(16) FFTIterPC
    {
        int iteration;
        int direction;  // 0=horiz, 1=vert
        int N;
        int pad0;
    };

    struct alignas(16) FFTEvolveParams
    {
        float time;
        float baseScale;
        float pad[2];
    };

    struct alignas(16) FFTExtractParams
    {
        float baseScale;
        float amplitude;
        float lodScale;
        int   lodIndex;
        float maxWaveAmp;
        float pad[3];
    };
}

bool VansWaterFFT::AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                   VkMemoryPropertyFlags props,
                                   VkBuffer& outBuffer, VkDeviceMemory& outMemory)
{
    VkDevice device = m_Device->GetLogicDevice();
    VkBufferCreateInfo ci = {};
    ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    ci.size = size; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &ci, nullptr, &outBuffer) != VK_SUCCESS)
        return false;
    VkMemoryRequirements memReq;
    vkGetBufferMemoryRequirements(device, outBuffer, &memReq);
    VkPhysicalDeviceMemoryProperties memProps;
    vkGetPhysicalDeviceMemoryProperties(m_Device->GetPhysicalDevice(), &memProps);
    uint32_t mtIdx = UINT32_MAX;
    for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i)
        if ((memReq.memoryTypeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & props) == props)
        { mtIdx = i; break; }
    if (mtIdx == UINT32_MAX) { vkDestroyBuffer(device, outBuffer, nullptr); outBuffer = VK_NULL_HANDLE; return false; }
    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; ai.allocationSize = memReq.size; ai.memoryTypeIndex = mtIdx;
    if (vkAllocateMemory(device, &ai, nullptr, &outMemory) != VK_SUCCESS)
    { vkDestroyBuffer(device, outBuffer, nullptr); outBuffer = VK_NULL_HANDLE; return false; }
    vkBindBufferMemory(device, outBuffer, outMemory, 0);
    return true;
}

bool VansWaterFFT::Initialize(VansVKDevice* device, const std::string& shaderRoot,
                               VansVKImage* displacementImage)
{
    m_Device = device;
    m_DisplacementImage = displacementImage;
    VkDevice logicDev = device->GetLogicDevice();
    const uint32_t N = FFT_RESOLUTION;

    // Compile FFT shaders
    m_InitSpectrumShader = new VansComputeShader();
    m_InitSpectrumShader->InitShader(logicDev, (shaderRoot + "EngineAssets/Shaders/Water/FFT").c_str());

    m_FFTIterShader = new VansComputeShader();
    m_FFTIterShader->InitShader(logicDev, (shaderRoot + "EngineAssets/Shaders/Water/FFT").c_str());
    m_FFTIterShader->SetPushConstant(sizeof(FFTIterPC));

    m_TimeEvolveShader = new VansComputeShader();
    m_TimeEvolveShader->InitShader(logicDev, (shaderRoot + "EngineAssets/Shaders/Water/FFT").c_str());

    m_DisplacementExtractShader = new VansComputeShader();
    m_DisplacementExtractShader->InitShader(logicDev, (shaderRoot + "EngineAssets/Shaders/Water/FFT").c_str());

    // Create FFT textures
    auto createFFTImage = [&](VansVKImage& img, uint32_t layers)
    {
        img.CreateVulkanImage(logicDev, {N, N, 1}, VK_FORMAT_R32G32B32A32_SFLOAT,
            1, layers, VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_SAMPLE_COUNT_1_BIT, false, false, true);
    };
    createFFTImage(m_H0Spectrum, 2);       // layer 0=real, 1=imag
    createFFTImage(m_HtSpectrum, 4);       // layer 0-3: Hx_real, Hx_imag, Hy, Hz...
    createFFTImage(m_PingPong[0], 2);
    createFFTImage(m_PingPong[1], 2);

    // Create params UBO
    AllocateBuffer(256,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_ParamsBuffer, m_ParamsMemory);

    m_NeedsReinit = true;
    m_Initialized = true;
    VANS_LOG("[VansWaterFFT] Initialize: N=" << N);
    return true;
}

void VansWaterFFT::Shutdown(VkDevice logicDevice)
{
    if (m_ParamsBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(logicDevice, m_ParamsBuffer, nullptr); m_ParamsBuffer = VK_NULL_HANDLE; }
    if (m_ParamsMemory  != VK_NULL_HANDLE) { vkFreeMemory(logicDevice, m_ParamsMemory, nullptr);  m_ParamsMemory  = VK_NULL_HANDLE; }
    m_H0Spectrum.DestroyVulkanImage(logicDevice);
    m_HtSpectrum.DestroyVulkanImage(logicDevice);
    m_PingPong[0].DestroyVulkanImage(logicDevice);
    m_PingPong[1].DestroyVulkanImage(logicDevice);
    delete m_InitSpectrumShader;       m_InitSpectrumShader       = nullptr;
    delete m_FFTIterShader;            m_FFTIterShader            = nullptr;
    delete m_TimeEvolveShader;         m_TimeEvolveShader         = nullptr;
    delete m_DisplacementExtractShader; m_DisplacementExtractShader = nullptr;
    m_Initialized = false;
    m_Device = nullptr;
}

void VansWaterFFT::SetWindParams(glm::vec2 direction, float speed, float amplitude)
{
    if (m_WindDirection != direction || m_WindSpeed != speed || m_Amplitude != amplitude)
    {
        m_WindDirection = direction;
        m_WindSpeed     = speed;
        m_Amplitude     = amplitude;
        m_NeedsReinit   = true;
    }
}

void VansWaterFFT::UpdateFFT(VansVKCommandBuffer& cmd, float time)
{
    if (!m_Initialized || m_Device == nullptr || m_DisplacementImage == nullptr)
        return;

    m_Time = time;
    VkDevice logicDev = m_Device->GetLogicDevice();
    const uint32_t N = FFT_RESOLUTION;
    const uint32_t groups = (N + 7u) / 8u;
    const int log2N = 8;  // N=256

    // ── Step 1: Init Spectrum (only when wind params change) ──
    if (m_NeedsReinit)
    {
        FFTSpectrumParams initParams = {};
        initParams.baseScale  = m_BaseScale;
        initParams.windDir    = glm::normalize(m_WindDirection);
        initParams.windSpeed  = m_WindSpeed;
        initParams.amplitude  = m_Amplitude;
        void* data = nullptr;
        vkMapMemory(logicDev, m_ParamsMemory, 0, sizeof(FFTSpectrumParams), 0, &data);
        std::memcpy(data, &initParams, sizeof(FFTSpectrumParams));
        vkUnmapMemory(logicDev, m_ParamsMemory);

        cmd.EnsureComputeShader(*m_InitSpectrumShader, {});
        cmd.DispatchCompute(*m_InitSpectrumShader, groups, groups, 1, { m_InitSet });

        m_NeedsReinit = false;
    }

    // ── Step 2: Time Evolve ───────────────────────────────────
    {
        FFTEvolveParams evoParams = {};
        evoParams.time      = m_Time;
        evoParams.baseScale = m_BaseScale;
        void* data = nullptr;
        vkMapMemory(logicDev, m_ParamsMemory, 0, sizeof(FFTEvolveParams), 0, &data);
        std::memcpy(data, &evoParams, sizeof(FFTEvolveParams));
        vkUnmapMemory(logicDev, m_ParamsMemory);

        cmd.EnsureComputeShader(*m_TimeEvolveShader, {});
        cmd.DispatchCompute(*m_TimeEvolveShader, groups, groups, 1, { m_EvolveSet });
    }

    // ── Step 3: IFFT — 16 iterations (8 horizontal + 8 vertical) ──
    {
        int pingPong = 0;
        for (int n = 0; n < log2N; ++n)
        {
            FFTIterPC pc = {};
            pc.iteration = n;
            pc.direction = 0;  // horizontal
            pc.N = static_cast<int>(N);

            cmd.EnsureComputeShader(*m_FFTIterShader, {});
            // Bind ping-pong: src=pingPong[n%2], dst=pingPong[(n+1)%2]
            cmd.DispatchCompute(*m_FFTIterShader, groups, groups, 1, {});
            pingPong = 1 - pingPong;
        }
        for (int n = 0; n < log2N; ++n)
        {
            FFTIterPC pc = {};
            pc.iteration = n;
            pc.direction = 1;  // vertical

            cmd.EnsureComputeShader(*m_FFTIterShader, {});
            cmd.DispatchCompute(*m_FFTIterShader, groups, groups, 1, {});
            pingPong = 1 - pingPong;
        }
    }

    // ── Step 4: Extract displacement per LOD ──────────────────
    {
        for (int lod = 0; lod < static_cast<int>(MAX_LOD_COUNT); ++lod)
        {
            float lodScaleFactor = m_BaseScale * std::powf(2.0f, static_cast<float>(lod));

            FFTExtractParams extParams = {};
            extParams.baseScale  = m_BaseScale;
            extParams.amplitude  = m_Amplitude;
            extParams.lodScale   = lodScaleFactor;
            extParams.lodIndex   = lod;
            extParams.maxWaveAmp = 5.0f;
            void* data = nullptr;
            vkMapMemory(logicDev, m_ParamsMemory, 0, sizeof(FFTExtractParams), 0, &data);
            std::memcpy(data, &extParams, sizeof(FFTExtractParams));
            vkUnmapMemory(logicDev, m_ParamsMemory);

            cmd.EnsureComputeShader(*m_DisplacementExtractShader, {});
            cmd.DispatchCompute(*m_DisplacementExtractShader, groups, groups, 1, { m_ExtractSet });
        }
    }
}

} // namespace VansGraphics
