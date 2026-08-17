#include "VansWaterFFT.h"
#include "../../Util/VansLog.h"
#include "../VulkanCore/VansVKDevice.h"
#include "../VulkanCore/VansVKCommandBuffer.h"
#include "../VulkanCore/VansShader.h"
#include "../VulkanCore/VansVKDescriptorManager.h"
#include "../VulkanCore/VansDescriptorSetLayouts.h"
#include "../VansShaderManager.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace VansGraphics
{

namespace
{
    enum FFTField
    {
        FIELD_HEIGHT = 0,
        FIELD_DISP_X = 1,
        FIELD_DISP_Z = 2,
        FIELD_SLOPE_X = 3,
        FIELD_SLOPE_Z = 4,
        FIELD_COUNT_LOCAL = 5
    };

    struct alignas(16) FFTParamsGPU
    {
        glm::vec4 windDirection_Time;      // xy=wind dir, z=time, w=windSpeed
        glm::vec4 spectrumParams;          // x=amplitude, y=choppiness
        glm::vec4 dampingParams;           // x=smallWaveDamping, y=windDependency, z=depth, w=repeatPeriod
        glm::vec4 spectrumMeta;             // x=randomSeed, y=resolution, z=cascadeCount, w=outputMode
        glm::vec4 domainCoverage;
        glm::vec4 minWavelength;
        glm::vec4 maxWavelength;
        glm::vec4 spectralOptions;          // x=capillary coefficient
    };

}

bool VansWaterFFT::Initialize(VansVKDevice* device, const std::string& shaderRoot,
                              VansVKImage* displacementImage,
                              VansVKImage* derivativeImage,
                              OutputMode outputMode)
{
    m_Device = device;
    m_DisplacementImage = displacementImage;
    m_DerivativeImage = derivativeImage;
    m_OutputMode = outputMode;

    if (m_DisplacementImage == nullptr ||
        (m_OutputMode == OutputMode::Displacement && m_DerivativeImage == nullptr))
        return false;

    VkDevice logicDev = device->GetLogicDevice();
    const uint32_t N = FFT_RESOLUTION;

    (void)shaderRoot;
    auto& shaderManager = VansShaderManager::Get();
    m_InitSpectrumShader = shaderManager.FindComputeShader("WaterFFTInit");
    m_TimeEvolveShader = shaderManager.FindComputeShader("WaterFFTEvolve");
    m_FFTIterShader = shaderManager.FindComputeShader("WaterFFTIter");
    m_DisplacementExtractShader = shaderManager.FindComputeShader(
        m_OutputMode == OutputMode::SpectralSlope ? "WaterFFTExtractSlope" : "WaterFFTExtract");
    if (!m_InitSpectrumShader || !m_TimeEvolveShader || !m_FFTIterShader || !m_DisplacementExtractShader)
    {
        VANS_LOG_ERROR("[VansWaterFFT] One or more managed FFT shaders are unavailable");
        return false;
    }

    auto createFFTImage = [&](VansVKImage& image, uint32_t layers)
    {
        image.CreateVulkanImage(logicDev, { N, N, 1 }, VK_FORMAT_R32G32B32A32_SFLOAT,
            1, layers, VK_IMAGE_TYPE_2D,
            VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            VK_SAMPLE_COUNT_1_BIT, false, false, true,
            VK_SAMPLER_ADDRESS_MODE_REPEAT);
    };

    createFFTImage(m_H0Spectrum, MAX_CASCADE_COUNT);
    createFFTImage(m_PingPong[0], MAX_CASCADE_COUNT * FIELD_COUNT);
    createFFTImage(m_PingPong[1], MAX_CASCADE_COUNT * FIELD_COUNT);

    m_ParamsBufferCreated = m_ParamsBuffer.CreatVulkanBuffer(logicDev,
        sizeof(FFTParamsGPU),
        VK_FORMAT_UNDEFINED,
        VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (!m_ParamsBufferCreated)
    {
        return false;
    }

    if (!CreateDescriptors())
        return false;

    m_Params.resolution = FFT_RESOLUTION;
    m_Params.cascadeCount = MAX_CASCADE_COUNT;
    m_NeedsReinit = true;
    m_Initialized = true;

    VANS_LOG("[VansWaterFFT] Initialized "
        << (m_OutputMode == OutputMode::SpectralSlope ? "spectral slope" : "displacement")
        << " FFT, N=" << FFT_RESOLUTION);
    return true;
}

bool VansWaterFFT::CreateDescriptors()
{
    auto* descMgr = VansVKDescriptorManager::GetInstance();

    auto createLayoutAndSet = [&](const std::vector<VkDescriptorSetLayoutBinding>& bindings,
                                  VkDescriptorSetLayout& layout,
                                  VkDescriptorSet& set) -> bool
    {
        std::vector<VkDescriptorSet> sets;
        if (!VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(bindings, layout, sets) || sets.empty())
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

    std::vector<VkDescriptorSet> sets;
    if (!VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom({
        { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
    }, m_IterLayout, sets, 2) || sets.size() < 2)
        return false;
    m_IterSet[0] = sets[0];
    m_IterSet[1] = sets[1];

    sets.clear();
    std::vector<VkDescriptorSetLayoutBinding> extractBindings = {
        { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        { 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
    };
    if (m_OutputMode == OutputMode::Displacement)
        extractBindings.push_back({ 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
    if (!VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(
        extractBindings, m_ExtractLayout, sets, 2) || sets.size() < 2)
        return false;
    m_ExtractSet[0] = sets[0];
    m_ExtractSet[1] = sets[1];

    const VkDescriptorBufferInfo paramsInfo = { m_ParamsBuffer.GetNativeBuffer(), 0, sizeof(FFTParamsGPU) };
    auto storageInfo = [](VansVKImage& image)
    {
        return VkDescriptorImageInfo{ VK_NULL_HANDLE, image.GetImageView(), VK_IMAGE_LAYOUT_GENERAL };
    };

    descMgr->BeginDescriptorUpdate();
    descMgr->WriteBufferDescriptor(m_InitSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { paramsInfo });
    descMgr->WriteImageDescriptor(m_InitSet, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_H0Spectrum) });

    descMgr->WriteBufferDescriptor(m_EvolveSet, 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { paramsInfo });
    descMgr->WriteImageDescriptor(m_EvolveSet, 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_H0Spectrum) });
    descMgr->WriteImageDescriptor(m_EvolveSet, 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_PingPong[0]) });

    descMgr->WriteImageDescriptor(m_IterSet[0], 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_PingPong[0]) });
    descMgr->WriteImageDescriptor(m_IterSet[0], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_PingPong[1]) });
    descMgr->WriteImageDescriptor(m_IterSet[1], 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_PingPong[1]) });
    descMgr->WriteImageDescriptor(m_IterSet[1], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_PingPong[0]) });

    for (uint32_t i = 0; i < 2; ++i)
    {
        descMgr->WriteBufferDescriptor(m_ExtractSet[i], 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, { paramsInfo });
        descMgr->WriteImageDescriptor(m_ExtractSet[i], 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(m_PingPong[i]) });
        descMgr->WriteImageDescriptor(m_ExtractSet[i], 2, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(*m_DisplacementImage) });
        if (m_OutputMode == OutputMode::Displacement)
            descMgr->WriteImageDescriptor(m_ExtractSet[i], 3, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, { storageInfo(*m_DerivativeImage) });
    }

    descMgr->CommitDescriptorUpdates();
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

    if (m_ParamsBufferCreated)
    {
        m_ParamsBuffer.DestroyVulkanBuffer(logicDevice);
        m_ParamsBufferCreated = false;
    }

    m_H0Spectrum.DestroyVulkanImage(logicDevice);
    m_PingPong[0].DestroyVulkanImage(logicDevice);
    m_PingPong[1].DestroyVulkanImage(logicDevice);

    m_InitSpectrumShader = nullptr;
    m_TimeEvolveShader = nullptr;
    m_FFTIterShader = nullptr;
    m_DisplacementExtractShader = nullptr;

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
    next.cascadeCount = std::clamp(next.cascadeCount, 1u, MAX_CASCADE_COUNT);
    if (glm::length(next.windDirection) < 0.001f)
        next.windDirection = glm::vec2(0.7071f, 0.7071f);
    next.windDirection = glm::normalize(next.windDirection);
    next.spectrumAmplitude = std::clamp(next.spectrumAmplitude, 0.0f, 0.02f);
    next.choppiness = std::clamp(next.choppiness, 0.0f, 3.0f);
    next.smallWaveDamping = std::clamp(next.smallWaveDamping, 0.0f, 0.1f);
    next.windDependency = std::clamp(next.windDependency, 0.0f, 1.0f);
    next.depth = (std::max)(next.depth, 0.1f);
    next.repeatPeriod = (std::max)(next.repeatPeriod, 0.0f);
    next.capillaryCoefficient = (std::max)(next.capillaryCoefficient, 0.0f);
    for (uint32_t i = 0; i < MAX_CASCADE_COUNT; ++i)
    {
        next.domainCoverage[i] = (std::max)(next.domainCoverage[i], 1.0f);
        next.minWavelength[i] = (std::max)(next.minWavelength[i],
            4.0f * next.domainCoverage[i] / float(FFT_RESOLUTION));
        next.maxWavelength[i] = (std::max)(next.maxWavelength[i], next.minWavelength[i]);
    }

    const bool spectrumDirty =
        next.cascadeCount != m_Params.cascadeCount ||
        glm::length(next.windDirection - m_Params.windDirection) > 0.0001f ||
        next.windSpeed != m_Params.windSpeed ||
        next.spectrumAmplitude != m_Params.spectrumAmplitude ||
        next.smallWaveDamping != m_Params.smallWaveDamping ||
        next.windDependency != m_Params.windDependency ||
        next.depth != m_Params.depth ||
        next.randomSeed != m_Params.randomSeed ||
        next.capillaryCoefficient != m_Params.capillaryCoefficient ||
        next.domainCoverage != m_Params.domainCoverage ||
        next.minWavelength != m_Params.minWavelength ||
        next.maxWavelength != m_Params.maxWavelength;

    m_Params = next;
    if (spectrumDirty)
        m_NeedsReinit = true;
}

void VansWaterFFT::UpdateParamsBuffer(float time)
{
    if (m_Device == nullptr || !m_ParamsBufferCreated)
        return;

    FFTParamsGPU gpu = {};
    gpu.windDirection_Time = glm::vec4(m_Params.windDirection, time, m_Params.windSpeed);
    gpu.spectrumParams = glm::vec4(
        m_Params.spectrumAmplitude, m_Params.choppiness, 0.0f, 0.0f);
    gpu.dampingParams = glm::vec4(m_Params.smallWaveDamping, m_Params.windDependency,
        m_Params.depth, m_Params.repeatPeriod);
    gpu.spectrumMeta = glm::vec4(float(m_Params.randomSeed), float(m_Params.resolution),
        float(m_Params.cascadeCount), float(m_OutputMode));
    gpu.domainCoverage = glm::vec4(
        m_Params.domainCoverage[0], m_Params.domainCoverage[1],
        m_Params.domainCoverage[2], m_Params.domainCoverage[3]);
    gpu.minWavelength = glm::vec4(
        m_Params.minWavelength[0], m_Params.minWavelength[1],
        m_Params.minWavelength[2], m_Params.minWavelength[3]);
    gpu.maxWavelength = glm::vec4(
        m_Params.maxWavelength[0], m_Params.maxWavelength[1],
        m_Params.maxWavelength[2], m_Params.maxWavelength[3]);
    gpu.spectralOptions = glm::vec4(m_Params.capillaryCoefficient, 0.0f, 0.0f, 0.0f);

    m_ParamsBuffer.SetBufferData(&gpu, 0, sizeof(FFTParamsGPU));
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
    if (!IsReady() || m_DisplacementImage == nullptr ||
        (m_OutputMode == OutputMode::Displacement && m_DerivativeImage == nullptr))
        return;

    UpdateParamsBuffer(time);

    const uint32_t N = FFT_RESOLUTION;
    const uint32_t groups = (N + 7u) / 8u;
    const int log2N = 8;
    const uint32_t activeFieldCount = m_OutputMode == OutputMode::SpectralSlope ? 2u : FIELD_COUNT;
    const uint32_t fieldLayers = MAX_CASCADE_COUNT * FIELD_COUNT;
    const uint32_t activeFieldLayers = m_Params.cascadeCount * activeFieldCount;

    BarrierImage(cmd, m_PingPong[0], VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, fieldLayers);
    BarrierImage(cmd, m_PingPong[1], VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
        VK_ACCESS_SHADER_WRITE_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, fieldLayers);

    if (m_NeedsReinit)
    {
        BarrierImage(cmd, m_H0Spectrum, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, MAX_CASCADE_COUNT);
        cmd.EnsureComputeShader(*m_InitSpectrumShader, { m_InitLayout });
        cmd.DispatchCompute(*m_InitSpectrumShader, groups, groups, m_Params.cascadeCount, { m_InitSet });
        BarrierImage(cmd, m_H0Spectrum, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, MAX_CASCADE_COUNT);
        m_NeedsReinit = false;
    }
    else
    {
        BarrierImage(cmd, m_H0Spectrum, VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, MAX_CASCADE_COUNT);
    }

    cmd.EnsureComputeShader(*m_TimeEvolveShader, { m_EvolveLayout });
    cmd.DispatchCompute(*m_TimeEvolveShader, groups, groups, m_Params.cascadeCount, { m_EvolveSet });
    BarrierImage(cmd, m_PingPong[0], VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, fieldLayers);

    int src = 0;
    int dst = 1;
    cmd.EnsureComputeShader(*m_FFTIterShader, { m_IterLayout });

    for (int stage = 0; stage < log2N; ++stage)
    {
        BarrierImage(cmd, m_PingPong[dst], VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, fieldLayers);

        IterPushConstants pc = {};
        pc.stage = stage;
        pc.direction = 0;
        pc.inverse = 1;
        pc.resolution = int(N);
        pc.normalize = 0;
        pc.fieldCount = int(activeFieldCount);
        pc.cascadeCount = int(m_Params.cascadeCount);
        cmd.DispatchCompute(*m_FFTIterShader, groups, groups, activeFieldLayers,
            { m_IterSet[src] }, &pc, sizeof(pc));
        BarrierImage(cmd, m_PingPong[dst], VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, fieldLayers);
        std::swap(src, dst);
    }

    for (int stage = 0; stage < log2N; ++stage)
    {
        BarrierImage(cmd, m_PingPong[dst], VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, fieldLayers);

        IterPushConstants pc = {};
        pc.stage = stage;
        pc.direction = 1;
        pc.inverse = 1;
        pc.resolution = int(N);
        pc.normalize = 0; // spectral coefficients use the unnormalised inverse DFT sum
        pc.fieldCount = int(activeFieldCount);
        pc.cascadeCount = int(m_Params.cascadeCount);
        cmd.DispatchCompute(*m_FFTIterShader, groups, groups, activeFieldLayers,
            { m_IterSet[src] }, &pc, sizeof(pc));
        BarrierImage(cmd, m_PingPong[dst], VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, fieldLayers);
        std::swap(src, dst);
    }

    const VkPipelineStageFlags outputReadStage = m_OutputMode == OutputMode::SpectralSlope
        ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT
        : VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
    BarrierImage(cmd, *m_DisplacementImage, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
        outputReadStage, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0, MAX_CASCADE_COUNT);
    if (m_OutputMode == OutputMode::Displacement)
        BarrierImage(cmd, *m_DerivativeImage, VK_ACCESS_SHADER_READ_BIT, VK_ACCESS_SHADER_WRITE_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0, MAX_CASCADE_COUNT * 2);

    cmd.EnsureComputeShader(*m_DisplacementExtractShader, { m_ExtractLayout });
    cmd.DispatchCompute(*m_DisplacementExtractShader, groups, groups, m_Params.cascadeCount, { m_ExtractSet[src] });

    BarrierImage(cmd, *m_DisplacementImage, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, outputReadStage,
        0, MAX_CASCADE_COUNT);
    if (m_OutputMode == OutputMode::Displacement)
        BarrierImage(cmd, *m_DerivativeImage, VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, MAX_CASCADE_COUNT * 2);
}

} // namespace VansGraphics
