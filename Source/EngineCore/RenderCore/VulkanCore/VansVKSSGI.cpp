#include "VansVKSSGI.h"
#include "../GICore/VansGIMemoryPublication.h"
#include "VansVKDevice.h"
#include "VansVKDescriptorManager.h"
#include "VansDescriptorSetLayouts.h"
#include "../VansRuntimeRenderTextureManager.h"
#include "../VansShaderManager.h"

namespace
{
    using namespace VansGraphics;
    struct TextureDescription
    {
        const char* name;
        VkFormat format;
        bool probeCache;
    };
    // 创建和释放共用一份清单；顺序与原创建路径一致。
    constexpr TextureDescription kTextures[] =
    {
        {VansVKSSGI::TextureResult, VK_FORMAT_R16G16B16A16_SFLOAT, false},
        {VansVKSSGI::TextureProbeCacheRadiance, VK_FORMAT_R16G16B16A16_SFLOAT, true},
        {VansVKSSGI::TextureProbeCacheSurface, VK_FORMAT_R16G16B16A16_SFLOAT, true},
        {VansVKSSGI::TextureFilterResult, VK_FORMAT_R16G16B16A16_SFLOAT, false},
        {VansVKSSGI::TextureTemporalA, VK_FORMAT_R16G16B16A16_SFLOAT, false},
        {VansVKSSGI::TextureTemporalB, VK_FORMAT_R16G16B16A16_SFLOAT, false},
        {VansVKSSGI::TextureMomentsA, VK_FORMAT_R16G16_SFLOAT, false},
        {VansVKSSGI::TextureMomentsB, VK_FORMAT_R16G16_SFLOAT, false},
        {VansVKSSGI::TextureSurfaceHistoryA, VK_FORMAT_R32G32B32A32_SFLOAT, false},
        {VansVKSSGI::TextureSurfaceHistoryB, VK_FORMAT_R32G32B32A32_SFLOAT, false},
        {VansVKSSGI::TextureAtrousA, VK_FORMAT_R16G16B16A16_SFLOAT, false},
    };
}

void VansGraphics::VansVKSSGI::Initialize(VansVKDevice& device, VansVKCommandBuffer& command,
    VansRuntimeRenderTextureManager& textures, const VansGISettings& gi, uint32_t width, uint32_t height)
{
    m_MemoryPublication = device.GetGIMemoryPublication();
    auto& ssgi = *this;
    VkDevice logicalDevice = device.GetLogicDevice();
    for (const auto& description : kTextures)
    {
        constexpr uint32_t tileSize = VansSSGIParameters::ProbeCacheTileSize;
        const uint32_t textureWidth = description.probeCache ? (width + tileSize - 1u) / tileSize : width;
        const uint32_t textureHeight = description.probeCache ? (height + tileSize - 1u) / tileSize : height;
        auto* texture = new VansTexture();
        texture->InitTextureWithoutData(command, textureWidth, textureHeight, 1,
            description.format, VK_IMAGE_VIEW_TYPE_2D, false, true,
            description.probeCache ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE : VK_SAMPLER_ADDRESS_MODE_REPEAT);
        textures.Add(description.name, texture);
    }
    history.Reset();
    ssgi.shader = VansGraphics::VansShaderManager::Get().FindComputeShader("SSGI");
    ssgi.probeCacheShader =
        VansGraphics::VansShaderManager::Get().FindComputeShader("SSGIProbeCache");

    ssgi.temporalShader = VansGraphics::VansShaderManager::Get().FindComputeShader("SSGITemporal");
    ssgi.atrousShader = VansGraphics::VansShaderManager::Get().FindComputeShader("SSGIAtrous");

    const VansSSGIParameters data = BuildSSGIParameters(
        gi, width, height);
    ssgi.params.CreatVulkanBuffer(
        logicalDevice, sizeof(data), VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    ssgi.params.SetBufferData(&data, 0, sizeof(data));

    VansSSGIHistoryParameters temporalData{};
    temporalData.screenSize = data.screenSize;
    temporalData.frameParams = glm::vec4(0.0f);
    for (VansVKBuffer& buffer : ssgi.temporalParams)
    {
        buffer.CreatVulkanBuffer(
            logicalDevice, sizeof(temporalData), VK_FORMAT_R32_SFLOAT,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
        buffer.SetBufferData(&temporalData, 0, sizeof(temporalData));
    }

    VansDescriptorSetLayoutFactory::CreateAndAllocate_SSGI(
        ssgi.layout, ssgi.sets,
        VansCommandRecordingConfig::MaximumFramesInFlight);
    VansDescriptorSetLayoutFactory::CreateAndAllocate_SSGIProbeCache(
        ssgi.probeCacheLayout,
        ssgi.probeCacheSets,
        2u * VansCommandRecordingConfig::MaximumFramesInFlight);
    VansDescriptorSetLayoutFactory::CreateAndAllocate_SSGITemporal(ssgi.temporalLayout,
        ssgi.temporalSets, 2u * VansCommandRecordingConfig::MaximumFramesInFlight);
    VansDescriptorSetLayoutFactory::CreateAndAllocate_SSGIAtrous(ssgi.atrousLayout,
        ssgi.atrousSets, 2u * VansCommandRecordingConfig::MaximumFramesInFlight);
    RefreshMemory(textures);
}

void VansGraphics::VansVKSSGI::Destroy(VkDevice device, VansRuntimeRenderTextureManager& textures)
{
    for (const auto& description : kTextures)
        textures.Remove(description.name);
    for (auto& buffer : temporalParams)
        buffer.DestroyVulkanBuffer(device);
    params.DestroyVulkanBuffer(device);
    auto* descriptors = VansVKDescriptorManager::GetInstance();
    descriptors->DestroyDescriptorSet(sets);
    descriptors->ReleaseDescriptorSetLayout(layout);
    descriptors->DestroyDescriptorSet(probeCacheSets);
    descriptors->ReleaseDescriptorSetLayout(probeCacheLayout);
    descriptors->DestroyDescriptorSet(temporalSets);
    descriptors->ReleaseDescriptorSetLayout(temporalLayout);
    descriptors->DestroyDescriptorSet(atrousSets);
    descriptors->ReleaseDescriptorSetLayout(atrousLayout);
    shader = probeCacheShader = temporalShader = atrousShader = nullptr;
    history.Reset();
    RefreshMemory(textures);
    m_MemoryPublication.reset();
}

void VansGraphics::VansVKSSGI::RefreshMemory(const VansRuntimeRenderTextureManager& textures) const
{
    if (!m_MemoryPublication) return;
    VansGIMemoryUsage usage;
    for (const auto& description : kTextures)
        if (auto* texture = textures.Get(description.name))
            usage.imageAllocationBytes += texture->GetImage().GetAllocationSize();
    usage.bufferCapacityBytes = params.GetBufferSize();
    usage.bufferAllocationBytes = params.GetAllocationSize();
    for (const auto& buffer : temporalParams)
    {
        usage.bufferCapacityBytes += buffer.GetBufferSize();
        usage.bufferAllocationBytes += buffer.GetAllocationSize();
    }
    m_MemoryPublication->PublishConsumer(&VansGIMemorySnapshot::ssgi, usage);
}
