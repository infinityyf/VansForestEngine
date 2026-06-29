#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansWaterFFT.h"
#include "../../Util/VansLog.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

namespace VansGraphics
{

namespace
{
    enum FFTField
    {
        FIELD_HEIGHT = 0,
        FIELD_SLOPE_X = 1,
        FIELD_SLOPE_Z = 2,
        FIELD_DISP_X = 3,
        FIELD_DISP_Z = 4,
        FIELD_FOLD = 5,
        FIELD_COUNT_LOCAL = 6
    };

    struct alignas(16) FFTParamsGPU
    {
        glm::vec4 windDirection_Time;      // xy=wind dir, z=time, w=windSpeed
        glm::vec4 scaleParams;             // x=baseScale, y=detailBalance, z=amplitude, w=choppiness
        glm::vec4 dampingParams;           // x=smallWaveDamping, y=windDependency, z=depth, w=repeatPeriod
        glm::vec4 outputParams;            // x=maxWaveAmp, y=normalScale, z=foamSlopeScale, w=foamFoldScale
        glm::vec4 foldParams;              // x=foamFoldThreshold, y=randomSeed, z=resolution, w=lodCount
    };

    struct alignas(16) FFTIterPC
    {
        int stage;
        int direction;
        int inverse;
        int resolution;
        int normalize;
        int fieldCount;
        int lodCount;
        int pad0;
    };
}

bool VansWaterFFT::AllocateBuffer(VkDeviceSize size, VkBufferUsageFlags usage,
                                  VkMemoryPropertyFlags props,
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
        {
            memTypeIndex = i;
            break;
        }
    }

    if (memTypeIndex == UINT32_MAX)
    {
        vkDestroyBuffer(device, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }

    VkMemoryAllocateInfo ai = {};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = memReq.size;
    ai.memoryTypeIndex = memTypeIndex;
    if (vkAllocateMemory(device, &ai, nullptr, &outMemory) != VK_SUCCESS)
    {
        vkDestroyBuffer(device, outBuffer, nullptr);
        outBuffer = VK_NULL_HANDLE;
        return false;
    }

    vkBindBufferMemory(device, outBuffer, outMemory, 0);
    return true;
}

bool VansWaterFFT::Initialize(VansVKDevice* device, const std::string& shaderRoot,
                              VansVKImage* displacementImage,
                              VansVKImage* derivativeImage)
{
    m_Device = device;
    m_DisplacementImage = displacementImage;
    m_DerivativeImage = derivativeImage;

    VkDevice logicDev = device->GetLogicDevice();
    const uint32_t N = FFT_RESOLUTION;

    m_InitSpectrumShader = new VansComputeShader();
    if (!m_InitSpectrumShader->InitShader(logicDev, shaderRoot + "EngineAssets/Shaders/Water/FFT/Init"))
        return false;

    m_TimeEvolveShader = new VansComputeShader();
    if (!m_TimeEvolveShader->InitShader(logicDev, shaderRoot + "EngineAssets/Shaders/Water/FFT/Evolve"))
        return false;

    m_FFTIterShader = new VansComputeShader();
    if (!m_FFTIterShader->InitShader(logicDev, shaderRoot + "EngineAssets/Shaders/Water/FFT/Iter"))
        return false;
    m_FFTIterShader->SetPushConstant(sizeof(FFTIterPC));

    m_DisplacementExtractShader = new VansComputeShader();
    if (!m_DisplacementExtractShader->InitShader(logicDev, shaderRoot + "EngineAssets/Shaders/Water/FFT/Extract"))
        return false;

    auto createFFTImage = [&](VansVKImage& image, uint32_t layers)
    {
        image.CreateVulkanImage(logicDev, { N, N, 1 }, VK_FORMAT_R32G32B32A32_SFLOAT,
            1, layers, VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_SAMPLE_COUNT_1_BIT, false, false, true,
            VK_SAMPLER_ADDRESS_MODE_REPEAT);
    };

    createFFTImage(m_H0Spectrum, MAX_LOD_COUNT);
    createFFTImage(m_PingPong[0], MAX_LOD_COUNT * FIELD_COUNT);
    createFFTImage(m_PingPong[1], MAX_LOD_COUNT * FIELD_COUNT);

    if (!AllocateBuffer(sizeof(FFTParamsGPU),
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        m_ParamsBuffer, m_ParamsMemory))
    {
        return false;
    }

    if (!CreateDescriptors())
        return false;

    m_Params.resolution = FFT_RESOLUTION;
    m_Params.lodCount = MAX_LOD_COUNT;
    m_NeedsReinit = true;
    m_Initialized = true;

    VANS_LOG("[VansWaterFFT] Initialized Tessendorf FFT ocean, N=" << FFT_RESOLUTION);
    return true;
}

bool VansWaterFFT::CreateDescriptors()
{
    auto* descMgr = VansVKDescriptorManager::GetInstance();

    auto createLayoutAndSet = [&](const std::vector<VkDescriptorSetLayoutBinding>& bindings,
                                  VkDescriptorSetLayout& layout,
                                  VkDescriptorSet& set) -> bool
    {
        if (!descMgr->CreateDesciptorSetLayout(bindings, layout))
            return false;
        std::vector<VkDescriptorSet> sets;
        if (!descMgr->AllocateDescriptorSet({ layout }, sets) || sets.empty())
            return false;
        set = sets[0];
        return true;
    };

    if (!createLayoutAndSet({
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
    }, m_InitLayout, m_InitSet))
        return false;

    if (!createLayoutAndSet({
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
    }, m_EvolveLayout, m_EvolveSet))
        return false;

    if (!descMgr->CreateDesciptorSetLayout({
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
    }, m_IterLayout))
        return false;
    {
        std::vector<VkDescriptorSet> sets;
        if (!descMgr->AllocateDescriptorSet({ m_IterLayout, m_IterLayout }, sets) || sets.size() < 2)
            return false;
        m_IterSet[0] = sets[0];
        m_IterSet[1] = sets[1];
    }

    if (!descMgr->CreateDesciptorSetLayout({
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
    }, m_ExtractLayout))
        return false;
    {
        std::vector<VkDescriptorSet> sets;
        if (!descMgr->AllocateDescriptorSet({ m_ExtractLayout, m_ExtractLayout }, sets) || sets.size() < 2)
            return false;
        m_ExtractSet[0] = sets[0];
        m_ExtractSet[1] = sets[1];
    }

    const VkDescriptorBufferInfo paramsInfo = { m_ParamsBuffer, 0, sizeof(FFTParamsGPU) };
    auto storageInfo = [](VansVKImage& image)
    {
        return VkDescriptorImageInfo{ VK_NULL_HANDLE, image.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
    };

    descMgr->m_BufferDescInfos.push_back({ m_InitSet, 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { paramsInfo } });
    descMgr->m_ImageDescInfos.push_back({ m_InitSet, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_H0Spectrum) } });

    descMgr->m_BufferDescInfos.push_back({ m_EvolveSet, 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { paramsInfo } });
    descMgr->m_ImageDescInfos.push_back({ m_EvolveSet, 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_H0Spectrum) } });
    descMgr->m_ImageDescInfos.push_back({ m_EvolveSet, 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_PingPong[0]) } });

    descMgr->m_ImageDescInfos.push_back({ m_IterSet[0], 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_PingPong[0]) } });
    descMgr->m_ImageDescInfos.push_back({ m_IterSet[0], 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_PingPong[1]) } });
    descMgr->m_ImageDescInfos.push_back({ m_IterSet[1], 0, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_PingPong[1]) } });
    descMgr->m_ImageDescInfos.push_back({ m_IterSet[1], 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_PingPong[0]) } });

    for (uint32_t i = 0; i < 2; ++i)
    {
        descMgr->m_BufferDescInfos.push_back({ m_ExtractSet[i], 0, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { paramsInfo } });
        descMgr->m_ImageDescInfos.push_back({ m_ExtractSet[i], 1, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_PingPong[i]) } });
        descMgr->m_ImageDescInfos.push_back({ m_ExtractSet[i], 2, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(*m_DisplacementImage) } });
        descMgr->m_ImageDescInfos.push_back({ m_ExtractSet[i], 3, 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(*m_DerivativeImage) } });
    }

    descMgr->UpdateDescriptorSets();
    m_DescriptorsReady = true;
    return true;
}

void VansWaterFFT::Shutdown(VkDevice logicDevice)
{
    auto* descMgr = VansVKDescriptorManager::GetInstance();

    if (m_InitLayout != VK_NULL_HANDLE) { descMgr->DestroyDescriptorSetLayout(m_InitLayout); m_InitLayout = VK_NULL_HANDLE; }
    if (m_EvolveLayout != VK_NULL_HANDLE) { descMgr->DestroyDescriptorSetLayout(m_EvolveLayout); m_EvolveLayout = VK_NULL_HANDLE; }
    if (m_IterLayout != VK_NULL_HANDLE) { descMgr->DestroyDescriptorSetLayout(m_IterLayout); m_IterLayout = VK_NULL_HANDLE; }
    if (m_ExtractLayout != VK_NULL_HANDLE) { descMgr->DestroyDescriptorSetLayout(m_ExtractLayout); m_ExtractLayout = VK_NULL_HANDLE; }

    if (m_ParamsBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(logicDevice, m_ParamsBuffer, nullptr); m_ParamsBuffer = VK_NULL_HANDLE; }
    if (m_ParamsMemory != VK_NULL_HANDLE) { vkFreeMemory(logicDevice, m_ParamsMemory, nullptr); m_ParamsMemory = VK_NULL_HANDLE; }

    m_H0Spectrum.DestroyVulkanImage(logicDevice);
    m_PingPong[0].DestroyVulkanImage(logicDevice);
    m_PingPong[1].DestroyVulkanImage(logicDevice);

    delete m_InitSpectrumShader; m_InitSpectrumShader = nullptr;
    delete m_TimeEvolveShader; m_TimeEvolveShader = nullptr;
    delete m_FFTIterShader; m_FFTIterShader = nullptr;
    delete m_DisplacementExtractShader; m_DisplacementExtractShader = nullptr;

    m_InitSet = VK_NULL_HANDLE;
    m_EvolveSet = VK_NULL_HANDLE;
    m_IterSet[0] = m_IterSet[1] = VK_NULL_HANDLE;
    m_ExtractSet[0] = m_ExtractSet[1] = VK_NULL_HANDLE;
    m_DisplacementImage = nullptr;
    m_DerivativeImage = nullptr;
    m_Device = nullptr;
    m_Initialized = false;
    m_DescriptorsReady = false;
}

void VansWaterFFT::SetParams(const Params& params)
{
    Params next = params;
    next.resolution = FFT_RESOLUTION;
    next.lodCount = std::clamp(next.lodCount, 1u, MAX_LOD_COUNT);
    if (glm::length(next.windDirection) < 0.001f)
        next.windDirection = glm::vec2(0.7071f, 0.7071f);
    next.windDirection = glm::normalize(next.windDirection);
    next.detailBalance = std::max(next.detailBalance, 1.0f);
    next.baseScale = std::max(next.baseScale, 1.0f);

    const bool spectrumDirty =
        next.lodCount != m_Params.lodCount ||
        next.baseScale != m_Params.baseScale ||
        next.detailBalance != m_Params.detailBalance ||
        glm::length(next.windDirection - m_Params.windDirection) > 0.0001f ||
        next.windSpeed != m_Params.windSpeed ||
        next.spectrumAmplitude != m_Params.spectrumAmplitude ||
        next.smallWaveDamping != m_Params.smallWaveDamping ||
        next.windDependency != m_Params.windDependency ||
        next.depth != m_Params.depth ||
        next.randomSeed != m_Params.randomSeed;

    m_Params = next;
    if (spectrumDirty)
        m_NeedsReinit = true;
}

void VansWaterFFT::UpdateParamsBuffer(float time)
{
    if (m_Device == nullptr || m_ParamsMemory == VK_NULL_HANDLE)
        return;

    FFTParamsGPU gpu = {};
    gpu.windDirection_Time = glm::vec4(m_Params.windDirection, time, m_Params.windSpeed);
    gpu.scaleParams = glm::vec4(m_Params.baseScale, m_Params.detailBalance,
        m_Params.spectrumAmplitude, m_Params.choppiness);
    gpu.dampingParams = glm::vec4(m_Params.smallWaveDamping, m_Params.windDependency,
        m_Params.depth, m_Params.repeatPeriod);
    gpu.outputParams = glm::vec4(m_Params.maxWaveAmp, m_Params.normalScale,
        m_Params.foamSlopeScale, m_Params.foamFoldScale);
    gpu.foldParams = glm::vec4(m_Params.foamFoldThreshold, float(m_Params.randomSeed),
        float(m_Params.resolution), float(m_Params.lodCount));

    void* data = nullptr;
    vkMapMemory(m_Device->GetLogicDevice(), m_ParamsMemory, 0, sizeof(FFTParamsGPU), 0, &data);
    std::memcpy(data, &gpu, sizeof(FFTParamsGPU));
    vkUnmapMemory(m_Device->GetLogicDevice(), m_ParamsMemory);
}

void VansWaterFFT::BarrierImage(VansVKCommandBuffer& cmd, VansVKImage& image,
                                VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage,
                                uint32_t baseLayer, uint32_t layerCount)
{
    VkImageMemoryBarrier barrier = {};
    const VkImageLayout oldLayout = image.GetImageLayout();
    const bool fromUndefined = oldLayout == VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.srcAccessMask = fromUndefined ? 0 : srcAccess;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.GetImage();
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, baseLayer, layerCount };
    cmd.PipelineBarrier(fromUndefined ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT : srcStage, dstStage, {}, {}, { barrier });
    image.SetTrackedImageLayout(VK_IMAGE_LAYOUT_GENERAL);
}

void VansWaterFFT::UpdateFFT(VansVKCommandBuffer& cmd, float time)
{
    if (!IsReady() || m_DisplacementImage == nullptr || m_DerivativeImage == nullptr)
        return;

    UpdateParamsBuffer(time);

    const uint32_t N = FFT_RESOLUTION;
    const uint32_t groups = (N + 7u) / 8u;
    const int log2N = 8;
    const uint32_t fieldLayers = m_Params.lodCount * FIELD_COUNT;

    BarrierImage(cmd, m_PingPong[0], 0, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, fieldLayers);
    BarrierImage(cmd, m_PingPong[1], 0, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, fieldLayers);

    if (m_NeedsReinit)
    {
        BarrierImage(cmd, m_H0Spectrum, 0, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, m_Params.lodCount);
        cmd.EnsureComputeShader(*m_InitSpectrumShader, { m_InitLayout });
        cmd.DispatchCompute(*m_InitSpectrumShader, groups, groups, m_Params.lodCount, { m_InitSet });
        BarrierImage(cmd, m_H0Spectrum, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, m_Params.lodCount);
        m_NeedsReinit = false;
    }
    else
    {
        BarrierImage(cmd, m_H0Spectrum, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, m_Params.lodCount);
    }

    cmd.EnsureComputeShader(*m_TimeEvolveShader, { m_EvolveLayout });
    cmd.DispatchCompute(*m_TimeEvolveShader, groups, groups, m_Params.lodCount, { m_EvolveSet });
    BarrierImage(cmd, m_PingPong[0], VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, fieldLayers);

    int src = 0;
    int dst = 1;
    cmd.EnsureComputeShader(*m_FFTIterShader, { m_IterLayout });

    for (int stage = 0; stage < log2N; ++stage)
    {
        FFTIterPC pc = {};
        pc.stage = stage;
        pc.direction = 0;
        pc.inverse = 1;
        pc.resolution = int(N);
        pc.normalize = 0;
        pc.fieldCount = int(FIELD_COUNT);
        pc.lodCount = int(m_Params.lodCount);
        m_FFTIterShader->SetPushConstantData(&pc);
        cmd.DispatchCompute(*m_FFTIterShader, groups, groups, fieldLayers, { m_IterSet[src] });
        BarrierImage(cmd, m_PingPong[dst], VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, fieldLayers);
        std::swap(src, dst);
    }

    for (int stage = 0; stage < log2N; ++stage)
    {
        FFTIterPC pc = {};
        pc.stage = stage;
        pc.direction = 1;
        pc.inverse = 1;
        pc.resolution = int(N);
        pc.normalize = (stage == log2N - 1) ? 1 : 0;
        pc.fieldCount = int(FIELD_COUNT);
        pc.lodCount = int(m_Params.lodCount);
        m_FFTIterShader->SetPushConstantData(&pc);
        cmd.DispatchCompute(*m_FFTIterShader, groups, groups, fieldLayers, { m_IterSet[src] });
        BarrierImage(cmd, m_PingPong[dst], VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, fieldLayers);
        std::swap(src, dst);
    }

    BarrierImage(cmd, *m_DisplacementImage, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, m_Params.lodCount);
    BarrierImage(cmd, *m_DerivativeImage, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, m_Params.lodCount);

    cmd.EnsureComputeShader(*m_DisplacementExtractShader, { m_ExtractLayout });
    cmd.DispatchCompute(*m_DisplacementExtractShader, groups, groups, m_Params.lodCount, { m_ExtractSet[src] });

    BarrierImage(cmd, *m_DisplacementImage, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        0, m_Params.lodCount);
    BarrierImage(cmd, *m_DerivativeImage, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        0, m_Params.lodCount);
}

} // namespace VansGraphics
