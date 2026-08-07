#include "VansRayTracing.h"
#include "../../RenderCore/VulkanCore/VansMesh.h"
#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansVKCommandBuffer.h"
#include "../../RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../../RenderCore/VulkanCore/VansDescriptorSetLayouts.h"
#include "../../RenderCore/VulkanCore/VansRenderPass.h"
#include "../../RenderCore/BRDFData/VansLight.h"
#include "../../RenderCore/VansScene.h"
#include "../../Configration/VansConfigration.h"
#include "../../RenderCore//VansMaterial.h"
#include "../../RenderCore/VansShaderManager.h"
#include "../../Util/VansLog.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace
{
    constexpr int kGIVisibilityOctaRes = 8;

    VansGraphics::RayTracingPushConstant BuildGIRegionPushConstant(
        const VansGraphics::GIResolvedRegion& region,
        float environmentIntensity,
        float maxIndirectRadiance,
        float maxSHL0)
    {
        VansGraphics::RayTracingPushConstant constants{};
        constants.dispatchParams = glm::vec4(
            glm::vec3(region.gridDimensions),
            static_cast<float>(region.raysPerProbe));
        constants.gridParams = glm::vec4(region.probeSpacingAxes, region.maxRayDistance);
        constants.frameParams = glm::vec4(
            0.0f,
            static_cast<float>(region.spatialUpdateDivisor),
            static_cast<float>(region.directionUpdateSlices),
            0.0f);
        constants.regionParams = glm::vec4(region.center, region.normalBias);
        constants.lightingParams = glm::vec4(
            std::max(environmentIntensity, 0.0f),
            std::max(maxIndirectRadiance, 0.0f),
            std::max(maxSHL0, 0.0f),
            0.0f);
        return constants;
    }

    uint32_t CeilDivide(uint32_t value, uint32_t divisor)
    {
        divisor = std::max(divisor, 1u);
        return value / divisor + (value % divisor != 0u ? 1u : 0u);
    }

    void DeleteTexture(VansGraphics::VansTexture*& texture)
    {
        delete texture;
        texture = nullptr;
    }
}

void VansGraphics::VansRayTracing::CleanupSceneResources(VkDevice device, VansMaterialManager* materialManager)
{
	if (!m_RTResourcesReady)
		return;

	if (materialManager != nullptr)
	{
		materialManager->UnregisterRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT);
		materialManager->UnregisterRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT);
		materialManager->UnregisterRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT);
		materialManager->UnregisterRuntimeRenderTexture(VansMaterialManager::RT_GI_VISIBILITY_ATLAS);
	}

	auto descMgr = VansVKDescriptorManager::GetInstance();

	// 释放 RT descriptor set 和 layout
	descMgr->DestroyDescriptorSet(m_RayTracingDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_RayTracingSetLayout);

	descMgr->DestroyDescriptorSet(m_GISamplePositionLightDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GISamplePositionLightSetLayout);

	descMgr->DestroyDescriptorSet(m_GIVisibilityUpdateDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GIVisibilityUpdateSetLayout);

	descMgr->DestroyDescriptorSet(m_GIRTPreviewDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GIRTPreviewSetLayout);

	// 释放 RT 相关 buffer
	m_BLASInstanceBuffer.DestroyVulkanBuffer(device);
	m_TLASInstanceTextureIndexBuffer.DestroyVulkanBuffer(device);

	for (GIRegionRuntime& region : m_GIRegions)
		DestroyRegionRuntime(device, region);
	m_GIRegions.clear();
	DeleteTexture(m_GIRTPreviewTexture);

	m_RayTracingPointLighting = nullptr;

	m_GIVisibilityUpdateShader = nullptr;

	m_GIRTPreviewShader = nullptr;

	// 重置 RT 着色器的 pipeline / SBT，下次 CreateRayTracingResource 将重建
	if (m_VansRayTracingShader)
		m_VansRayTracingShader->TriggerReCreateRayTracingPipeline();
	m_VansRayTracingShader = nullptr;

	// 标记脏以便下次 CreateRayTracingResource 重新绑定
	m_GIRTPreviewDescriptorSetIsDirty = true;
	m_GIRTPreviewRequestFrames = 0;
	m_GIRTPreviewBoundZSlice = 0xffffffffu;
	m_RTResourcesReady = false;

	VANS_LOG("[VansRayTracing] Scene RT resources cleaned up");
}

void VansGraphics::VansRayTracing::DestroyRegionRuntime(VkDevice device, GIRegionRuntime& region)
{
	region.hitPositionResult.DestroyVulkanBuffer(device);
	region.hitNormalResult.DestroyVulkanBuffer(device);
	region.hitAlbedoRoughnessResult.DestroyVulkanBuffer(device);
	region.hitRadianceBuffer.DestroyVulkanBuffer(device);
	region.hitDirectDiffuseBuffer.DestroyVulkanBuffer(device);
	DeleteTexture(region.rayTracingResult);
	DeleteTexture(region.shRResult);
	DeleteTexture(region.shGResult);
	DeleteTexture(region.shBResult);
	DeleteTexture(region.shFeedbackR);
	DeleteTexture(region.shFeedbackG);
	DeleteTexture(region.shFeedbackB);
	DeleteTexture(region.visibilityAtlas);
	region = GIRegionRuntime{};
}

VansGraphics::VansTexture* VansGraphics::VansRayTracing::GetGIRegionSHR(uint32_t regionIndex) const
{
	return regionIndex < m_GIRegions.size() ? m_GIRegions[regionIndex].shRResult : nullptr;
}

VansGraphics::VansTexture* VansGraphics::VansRayTracing::GetGIRegionSHG(uint32_t regionIndex) const
{
	return regionIndex < m_GIRegions.size() ? m_GIRegions[regionIndex].shGResult : nullptr;
}

VansGraphics::VansTexture* VansGraphics::VansRayTracing::GetGIRegionSHB(uint32_t regionIndex) const
{
	return regionIndex < m_GIRegions.size() ? m_GIRegions[regionIndex].shBResult : nullptr;
}

VansGraphics::VansTexture* VansGraphics::VansRayTracing::GetGIRegionVisibilityAtlas(uint32_t regionIndex) const
{
	return regionIndex < m_GIRegions.size() ? m_GIRegions[regionIndex].visibilityAtlas : nullptr;
}

void VansGraphics::VansRayTracing::RegisterPrimaryRegionRuntimeTextures(VansMaterialManager* materialManager)
{
	if (materialManager == nullptr || m_GIRegions.empty())
		return;

	GIRegionRuntime& primary = m_GIRegions[0];
	materialManager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT, primary.shRResult);
	materialManager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT, primary.shGResult);
	materialManager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT, primary.shBResult);
	materialManager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_GI_VISIBILITY_ATLAS, primary.visibilityAtlas);
}

void VansGraphics::VansRayTracing::SyncPrimaryCompatibilityState()
{
	if (m_GIRegions.empty())
		return;

	const GIRegionRuntime& primary = m_GIRegions[0];
	m_RayTracingGridDimensions = primary.resolved.gridDimensions;
	m_RayTracingProbeSpacing = primary.resolved.probeSpacingAxes;
	m_RayCountPerSample = static_cast<int>(primary.resolved.raysPerProbe);
	m_RayTracingConstant = primary.constants;
}

VansGraphics::VansRayTracing::GIRegionRuntime* VansGraphics::VansRayTracing::GetPreviewRegion()
{
	return m_GIRegions.empty() ? nullptr : &m_GIRegions[0];
}

const VansGraphics::VansRayTracing::GIRegionRuntime* VansGraphics::VansRayTracing::GetPreviewRegion() const
{
	return m_GIRegions.empty() ? nullptr : &m_GIRegions[0];
}

void VansGraphics::VansRayTracing::CreateRayTracingResource(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene)
{
    int blasMeshCount = scene->GetBLASVertexBuffers().size();
    std::vector<uint32_t>& instanceData = scene->GetTLASInstanceData();
	std::vector<uint32_t>& instanceTextureIndex = scene->GetTLASInstanceTextureIndex();

    // No RT geometry in the scene – nothing to set up.
    if (blasMeshCount == 0 || instanceData.empty())
    {
        VANS_LOG_WARN("[CreateRayTracingResource] No ray-tracing geometry found, skipping RT resource creation.");
        m_RTResourcesReady = false;
        return;
    }

    m_RTResourcesReady = true;

    VansGISettings gi = scene->GetGISettings();
    NormalizeGISettings(gi);

    std::vector<GIProbeRegionDesc> activeRegions;
    activeRegions.reserve(gi.regions.size());
    const uint32_t primaryIndex = std::min<uint32_t>(
        gi.selectedRegionIndex,
        static_cast<uint32_t>(gi.regions.size() - 1u));
    if (gi.regions[primaryIndex].enabled)
        activeRegions.push_back(gi.regions[primaryIndex]);
    for (uint32_t regionIndex = 0; regionIndex < gi.regions.size(); ++regionIndex)
    {
        if (regionIndex == primaryIndex || !gi.regions[regionIndex].enabled)
            continue;
        activeRegions.push_back(gi.regions[regionIndex]);
    }

    if (activeRegions.empty())
    {
        VANS_LOG_WARN("[CreateRayTracingResource] No enabled GI probe region found, skipping RT GI resource creation.");
        m_RTResourcesReady = false;
        return;
    }

    m_GIRegions.clear();
    m_GIRegions.reserve(activeRegions.size());
    m_BaseGIEnvironmentIntensity = std::max(gi.environmentIntensity, 0.0f);

    auto& shaderManager = VansShaderManager::Get();
    m_VansRayTracingShader = shaderManager.FindRayTracingShader("RayTracingTest");
    if (!m_VansRayTracingShader)
    {
        VANS_LOG_ERROR("[CreateRayTracingResource] Managed RayTracingTest shader is unavailable");
        m_RTResourcesReady = false;
        return;
    }

    for (const GIProbeRegionDesc& regionDesc : activeRegions)
    {
        m_GIRegions.emplace_back();
        GIRegionRuntime& regionRuntime = m_GIRegions.back();
        regionRuntime.resolved = ResolveGIRegion(regionDesc);
        regionRuntime.constants = BuildGIRegionPushConstant(
            regionRuntime.resolved,
            m_BaseGIEnvironmentIntensity,
            gi.maxIndirectRadiance,
            gi.maxSHL0);

        const glm::uvec3& grid = regionRuntime.resolved.gridDimensions;
        const uint64_t probeCount64 = regionRuntime.resolved.probeCount;
        const uint64_t rayCacheEntries64 = probeCount64 * regionRuntime.resolved.raysPerProbe;

        VANS_LOG("[CreateRayTracingResource] GI region '" << regionRuntime.resolved.name
            << "' grid=" << grid.x << "x" << grid.y << "x" << grid.z
            << " spacing=(" << regionRuntime.resolved.probeSpacingAxes.x << ","
            << regionRuntime.resolved.probeSpacingAxes.y << ","
            << regionRuntime.resolved.probeSpacingAxes.z << ")"
            << " probes=" << probeCount64
            << " rayCacheEntries=" << rayCacheEntries64);

        regionRuntime.rayTracingResult = new VansTexture();
        regionRuntime.rayTracingResult->InitTextureWithoutData(
            *commandBuffer,
            grid.x,
            grid.y,
            grid.z,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            false,
            false,
            true);

        auto createSHTexture = [&](VansTexture*& texture)
        {
            texture = new VansTexture();
            texture->InitTextureWithoutData(
                *commandBuffer,
                grid.x,
                grid.y,
                grid.z,
                VK_FORMAT_R32G32B32A32_SFLOAT,
                false,
                false,
                true);
        };
        createSHTexture(regionRuntime.shRResult);
        createSHTexture(regionRuntime.shGResult);
        createSHTexture(regionRuntime.shBResult);
        createSHTexture(regionRuntime.shFeedbackR);
        createSHTexture(regionRuntime.shFeedbackG);
        createSHTexture(regionRuntime.shFeedbackB);

        const int totalProbeCount = static_cast<int>(std::max<uint64_t>(probeCount64, 1u));
        const int probesPerAtlasRow = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(totalProbeCount)))));
        const int probesPerAtlasColumn = (totalProbeCount + probesPerAtlasRow - 1) / probesPerAtlasRow;

        regionRuntime.visibilityAtlas = new VansTexture();
        regionRuntime.visibilityAtlas->InitTextureWithoutData(
            *commandBuffer,
            probesPerAtlasRow * kGIVisibilityOctaRes,
            probesPerAtlasColumn * kGIVisibilityOctaRes,
            1,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            false,
            false,
            true,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

        const VkDeviceSize giHitBufferSize = static_cast<VkDeviceSize>(rayCacheEntries64) * sizeof(uint16_t) * 4;
        const VkDeviceSize giDirectCacheBufferSize = static_cast<VkDeviceSize>(rayCacheEntries64) * sizeof(uint32_t);

        regionRuntime.hitPositionResult.CreatVulkanBuffer(device->GetLogicDevice(),
            giHitBufferSize,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        regionRuntime.hitNormalResult.CreatVulkanBuffer(device->GetLogicDevice(),
            giHitBufferSize,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        regionRuntime.hitAlbedoRoughnessResult.CreatVulkanBuffer(device->GetLogicDevice(),
            giHitBufferSize,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        regionRuntime.hitRadianceBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
            giHitBufferSize,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        regionRuntime.hitDirectDiffuseBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
            giDirectCacheBufferSize,
            VK_FORMAT_R32_UINT,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }

    SyncPrimaryCompatibilityState();
    RegisterPrimaryRegionRuntimeTextures(scene->GetMaterialManager());

	m_GIRTPreviewTexture = new VansTexture();
	m_GIRTPreviewTexture->InitTextureWithoutData(
		*commandBuffer,
		static_cast<int>(m_RayTracingGridDimensions.x),
		static_cast<int>(m_RayTracingGridDimensions.y),
		1, VK_FORMAT_R32G32B32A32_SFLOAT, false, false, true,
		VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	m_GIRTPreviewStorageBufferAlignment = std::max<VkDeviceSize>(
		device->GetDeviceProperties().limits.minStorageBufferOffsetAlignment,
		1u);

    //提前生成pipeline
    CreateRayTraceDescriptorSets(device, blasMeshCount);
    m_VansRayTracingShader->GetRayTracingPipeline(device, { m_RayTracingSetLayout });

    //创建instance data的buffer
    m_BLASInstanceBuffer.CreatVulkanBuffer(device->GetLogicDevice(), 
        instanceData.size() * sizeof(uint32_t),
        VK_FORMAT_R32_UINT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    m_BLASInstanceBuffer.SetBufferData(instanceData.data(), 0, instanceData.size() * sizeof(uint32_t));

    //创建贴图索引
    m_TLASInstanceTextureIndexBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
        instanceTextureIndex.size() * sizeof(uint32_t),
        VK_FORMAT_R32_UINT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    m_TLASInstanceTextureIndexBuffer.SetBufferData(instanceTextureIndex.data(), 0, instanceTextureIndex.size() * sizeof(uint32_t));

    // GI 分帧更新会在冷启动阶段读取尚未轮到的方向 slice。
    // 显式清零所有反馈资源，保证未写入 slice 的 radiance 为 0，而不是未定义显存。
    if (commandBuffer->BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
    {
        VkClearColorValue clearSH{};
        for (GIRegionRuntime& regionRuntime : m_GIRegions)
        {
            commandBuffer->FillBuffer(regionRuntime.hitRadianceBuffer.GetNativeBuffer(), 0, regionRuntime.hitRadianceBuffer.GetBufferSize(), 0u);
            commandBuffer->FillBuffer(regionRuntime.hitDirectDiffuseBuffer.GetNativeBuffer(), 0, regionRuntime.hitDirectDiffuseBuffer.GetBufferSize(), 0u);
            commandBuffer->ClearColorImage(regionRuntime.shRResult->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
            commandBuffer->ClearColorImage(regionRuntime.shGResult->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
            commandBuffer->ClearColorImage(regionRuntime.shBResult->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
            commandBuffer->ClearColorImage(regionRuntime.visibilityAtlas->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
            commandBuffer->ClearColorImage(regionRuntime.shFeedbackR->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
            commandBuffer->ClearColorImage(regionRuntime.shFeedbackG->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
            commandBuffer->ClearColorImage(regionRuntime.shFeedbackB->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
            commandBuffer->ClearColorImage(regionRuntime.rayTracingResult->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
        }
		commandBuffer->ClearColorImage(m_GIRTPreviewTexture->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);

        VkMemoryBarrier transferToShader{};
        transferToShader.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        transferToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        transferToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        commandBuffer->PipelineBarrier(
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            { transferToShader });

        if (!commandBuffer->EndCommandBufferRecord() ||
            !VansVKCommandBuffer::SubmitCommands(
                device->GetGraphicsQueue(),
                device->GetLogicDevice(),
                { commandBuffer->GetVKCommandBuffer() },
                {}, {},
                commandBuffer->m_CommandBufferFinishSubmitFence) ||
            !commandBuffer->ResetCommandBuffer(false))
        {
            VANS_LOG_ERROR("[CreateRayTracingResource] Failed to clear initial GI resources.");
        }
    }
    else
    {
        VANS_LOG_ERROR("[CreateRayTracingResource] Failed to begin initial GI resource clear command buffer.");
    }

    m_RayTracingPointLighting = shaderManager.FindComputeShader("GIPointLight");
    if (!m_RayTracingPointLighting)
    {
        VANS_LOG_ERROR("[CreateRayTracingResource] Managed GIPointLight shader is unavailable");
        return;
    }


    //创建cs的set
    CreateGIPointLightDescriptorSets(device);

    m_GIVisibilityUpdateShader = shaderManager.FindComputeShader("GIVisibilityUpdate");
    if (!m_GIVisibilityUpdateShader)
    {
        VANS_LOG_ERROR("[CreateRayTracingResource] Managed GIVisibilityUpdate shader is unavailable");
        return;
    }

    CreateGIVisibilityUpdateDescriptorSets(device);

	m_GIRTPreviewShader = shaderManager.FindComputeShader("GIRTPreview");
	if (!m_GIRTPreviewShader)
	{
		VANS_LOG_ERROR("[CreateRayTracingResource] One or more managed GI compute shaders are unavailable");
		return;
	}
	CreateGIRTPreviewDescriptorSets(device);

    m_HasLastGIMainLight = false;
    m_GILightingResponseFramesRemaining = 0;
	m_GIRTPreviewRequestFrames = 0;
}

void VansGraphics::VansRayTracing::UpdateGISettings(const VansGISettings& settings)
{
    VansGISettings gi = settings;
    NormalizeGISettings(gi);

    std::vector<GIProbeRegionDesc> activeRegions;
    activeRegions.reserve(gi.regions.size());
    const uint32_t primaryIndex = std::min<uint32_t>(
        gi.selectedRegionIndex,
        static_cast<uint32_t>(gi.regions.size() - 1u));
    if (gi.regions[primaryIndex].enabled)
        activeRegions.push_back(gi.regions[primaryIndex]);
    for (uint32_t regionIndex = 0; regionIndex < gi.regions.size(); ++regionIndex)
    {
        if (regionIndex == primaryIndex || !gi.regions[regionIndex].enabled)
            continue;
        activeRegions.push_back(gi.regions[regionIndex]);
    }

    m_BaseGIEnvironmentIntensity = std::max(gi.environmentIntensity, 0.0f);
    const size_t updateCount = std::min(m_GIRegions.size(), activeRegions.size());
    for (size_t regionIndex = 0; regionIndex < updateCount; ++regionIndex)
    {
        GIRegionRuntime& region = m_GIRegions[regionIndex];
        const float previousFrameIndex = region.constants.frameParams.x;
        const float previousRefresh = region.constants.lightingParams.w;
        region.resolved = ResolveGIRegion(activeRegions[regionIndex]);
        region.constants = BuildGIRegionPushConstant(
            region.resolved,
            m_BaseGIEnvironmentIntensity,
            gi.maxIndirectRadiance,
            gi.maxSHL0);
        region.constants.frameParams.x = previousFrameIndex;
        region.constants.lightingParams.w = previousRefresh;
    }
    SyncPrimaryCompatibilityState();
}

void VansGraphics::VansRayTracing::RequestGIRTPreview(
    uint32_t mode,
    uint32_t zSlice,
    uint32_t rayIndex,
    float exposure,
    float positionScale)
{
    const GIRegionRuntime* previewRegion = GetPreviewRegion();
    if (!m_RTResourcesReady || previewRegion == nullptr)
        return;

    const uint32_t zCount = std::max(previewRegion->resolved.gridDimensions.z, 1u);
    const uint32_t rayCount = std::max(previewRegion->resolved.raysPerProbe, 1u);
    m_GIRTPreviewConstant.gridParams = glm::vec4(
        glm::vec3(previewRegion->resolved.gridDimensions),
        static_cast<float>(rayCount));
    const uint32_t safeZSlice = std::min(zSlice, zCount - 1u);
    if (m_GIRTPreviewBoundZSlice != safeZSlice)
        m_GIRTPreviewDescriptorSetIsDirty = true;
    m_GIRTPreviewConstant.selectionParams = glm::vec4(
        static_cast<float>(std::min(mode, 11u)),
        static_cast<float>(safeZSlice),
        static_cast<float>(std::min(rayIndex, rayCount - 1u)),
        std::max(exposure, 0.001f));
    m_GIRTPreviewConstant.displayParams = glm::vec4(
        std::max(positionScale, 0.0001f), 0.0f, 0.0f, 0.0f);

    // The editor refreshes this lease while the preview is visible. It avoids
    // paying for the gather pass after the window is closed.
    m_GIRTPreviewRequestFrames = 3;
}

bool VansGraphics::VansRayTracing::UpdateLightingResponseState(VansLightManager* lightManager)
{
    if (lightManager == nullptr)
        return false;

    glm::vec4 directionIntensity(0.0f);
    glm::vec4 lightColor(0.0f);

    auto& directionLights = lightManager->GetDirectionLights();
    m_RayTracingConstant.lightingParams.x = m_BaseGIEnvironmentIntensity;
    if (!directionLights.empty())
    {
        const VansCelestialLightingState celestialState =
            VansLightManager::ComputeCelestialLightingState(directionLights[0]);
        m_RayTracingConstant.lightingParams.x =
            m_BaseGIEnvironmentIntensity * std::max(celestialState.skyDiffuseScale, 0.0f);
        glm::vec3 direction = celestialState.direction;
        if (glm::dot(direction, direction) > 1e-6f)
            direction = glm::normalize(direction);
        else
            direction = glm::vec3(0.0f, 1.0f, 0.0f);

        directionIntensity = glm::vec4(direction, celestialState.intensity);
        lightColor = glm::vec4(celestialState.color, 0.0f);
    }

    const bool initializing = !m_HasLastGIMainLight;
    bool changed = initializing;
    if (!changed)
    {
        const float directionDelta = glm::length(glm::vec3(directionIntensity) - glm::vec3(m_LastGIMainLightDirectionIntensity));
        const float intensityDelta = std::abs(directionIntensity.w - m_LastGIMainLightDirectionIntensity.w);
        const float colorDelta = glm::length(glm::vec3(lightColor) - glm::vec3(m_LastGIMainLightColor));
        changed = directionDelta > 0.0025f || intensityDelta > 0.01f || colorDelta > 0.01f;
    }

    if (changed)
    {
        const uint32_t configuredDivisor = std::max(1u, static_cast<uint32_t>(m_RayTracingConstant.frameParams.y));
        const uint32_t responseDivisor = std::min(configuredDivisor, 2u);
        const uint32_t spatialPhaseCount = responseDivisor * responseDivisor * responseDivisor;

        // Cold start performs four full spatial sweeps so local static lights can
        // seed several diffuse bounces. Later main-light changes need two sweeps.
        const uint32_t sweepCount = initializing ? 4u : 2u;
        m_GILightingResponseFramesRemaining = std::max(
            m_GILightingResponseFramesRemaining,
            spatialPhaseCount * sweepCount);
    }

    m_LastGIMainLightDirectionIntensity = directionIntensity;
    m_LastGIMainLightColor = lightColor;
    m_HasLastGIMainLight = true;
    return changed;
}

void VansGraphics::VansRayTracing::UpdateGIProbe(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansLightManager* lightManager, VansMaterialManager* materialManager)
{
    if (!m_RTResourcesReady || m_GIRegions.empty())
        return;

    const bool lightingChanged = UpdateLightingResponseState(lightManager);
    if (lightingChanged && materialManager != nullptr)
    {
        materialManager->m_SSGITemporalFrame = 0;
    }

    if (lightingChanged)
    {
        for (GIRegionRuntime& region : m_GIRegions)
        {
            region.giLightingResponseFramesRemaining = std::max(
                region.giLightingResponseFramesRemaining,
                m_GILightingResponseFramesRemaining);
        }
    }

    commandBuffer->EnsureComputeShader(
        *m_RayTracingPointLighting,
        { m_Scene->GetGlobalDescriptorSetLayout(), m_GISamplePositionLightSetLayout });

    for (uint32_t regionIndex = 0; regionIndex < m_GIRegions.size(); ++regionIndex)
    {
        GIRegionRuntime& region = m_GIRegions[regionIndex];
        BindGIPointLightData(regionIndex);

        region.constants.frameParams.x = static_cast<float>(region.giUpdateFrameIndex++);
        region.constants.lightingParams.x = m_RayTracingConstant.lightingParams.x;

        const float configuredUpdateDivisor = region.constants.frameParams.y;
        const float configuredDirectionSlices = region.constants.frameParams.z;
        region.constants.lightingParams.w = region.giLightingResponseFramesRemaining > 0 ? 1.0f : 0.0f;
        if (region.giLightingResponseFramesRemaining > 0)
        {
            region.constants.frameParams.y = static_cast<float>(std::min(
                std::max(1u, static_cast<uint32_t>(configuredUpdateDivisor)),
                2u));
            region.constants.frameParams.z = 1.0f;
            --region.giLightingResponseFramesRemaining;
        }

        const uint32_t updateDivisor = std::max(1u, static_cast<uint32_t>(region.constants.frameParams.y));
        const glm::uvec3 probesPerAxis(
            CeilDivide(region.resolved.gridDimensions.x, updateDivisor),
            CeilDivide(region.resolved.gridDimensions.y, updateDivisor),
            CeilDivide(region.resolved.gridDimensions.z, updateDivisor));
        const glm::uvec3 groupCount(
            CeilDivide(probesPerAxis.x, 4u),
            CeilDivide(probesPerAxis.y, 4u),
            CeilDivide(probesPerAxis.z, 4u));

        commandBuffer->DispatchCompute(
            *m_RayTracingPointLighting,
            groupCount.x,
            groupCount.y,
            groupCount.z,
            { m_Scene->GetGlobalDescriptorSet(), m_GISamplePositionLightDescriptorSets[regionIndex] },
            &region.constants,
            sizeof(region.constants));

        CopyCurrentSHToFeedback(commandBuffer, materialManager, regionIndex);

        region.constants.frameParams.y = configuredUpdateDivisor;
        region.constants.frameParams.z = configuredDirectionSlices;
    }

	DispatchGIRTPreview(commandBuffer, materialManager);
    SyncPrimaryCompatibilityState();
}

void VansGraphics::VansRayTracing::CopyCurrentSHToFeedback(VansVKCommandBuffer* commandBuffer, VansMaterialManager* materialManager, uint32_t regionIndex)
{
    if (commandBuffer == nullptr || materialManager == nullptr || regionIndex >= m_GIRegions.size())
    {
        return;
    }

    GIRegionRuntime& region = m_GIRegions[regionIndex];
    if (region.shRResult == nullptr || region.shGResult == nullptr || region.shBResult == nullptr ||
        region.shFeedbackR == nullptr || region.shFeedbackG == nullptr || region.shFeedbackB == nullptr)
    {
        return;
    }

    auto transition = [commandBuffer](VansTexture* texture,
        VkAccessFlags srcAccess,
        VkAccessFlags dstAccess,
        VkImageLayout oldLayout,
        VkImageLayout newLayout,
        VkPipelineStageFlags srcStage,
        VkPipelineStageFlags dstStage)
    {
        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = srcAccess;
        barrier.dstAccessMask = dstAccess;
        barrier.oldLayout = oldLayout;
        barrier.newLayout = newLayout;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture->GetImage().GetImage();
        barrier.subresourceRange = { texture->GetImage().GetImageAspect(), 0, 1, 0, 1 };
        commandBuffer->PipelineBarrier(srcStage, dstStage, {}, {}, { barrier });
        texture->GetImage().SetTrackedImageLayout(newLayout);
    };

    auto copySH = [&](VansTexture* source, VansTexture* target)
    {
        transition(
            source,
            VK_ACCESS_SHADER_WRITE_BIT,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);
        transition(
            target,
            VK_ACCESS_SHADER_READ_BIT,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT);

        VkImageCopy copyRegion{};
        copyRegion.srcSubresource = { source->GetImage().GetImageAspect(), 0, 0, 1 };
        copyRegion.dstSubresource = { target->GetImage().GetImageAspect(), 0, 0, 1 };
        copyRegion.extent = {
            static_cast<uint32_t>(source->GetWidth()),
            static_cast<uint32_t>(source->GetHeight()),
            static_cast<uint32_t>(source->GetSlice())
        };
        commandBuffer->CopyImageRegions(
            source->GetImage(),
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            target->GetImage(),
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            { copyRegion });

        transition(
            source,
            VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        transition(
            target,
            VK_ACCESS_TRANSFER_WRITE_BIT,
            VK_ACCESS_SHADER_READ_BIT,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    };

    // 多次反弹反馈只读取上一帧完整 SH 快照，避免本帧刚写入的 probe 立即影响同一轮扩散。
    copySH(region.shRResult, region.shFeedbackR);
    copySH(region.shGResult, region.shFeedbackG);
    copySH(region.shBResult, region.shFeedbackB);
}

void VansGraphics::VansRayTracing::BindGIPointLightData(uint32_t regionIndex)
{
    if (regionIndex >= m_GIRegions.size() ||
        regionIndex >= m_GISamplePositionLightDescriptorSets.size())
    {
        return;
    }

    GIRegionRuntime& region = m_GIRegions[regionIndex];
    if (!region.giPointLightDescriptorSetIsDirty)
    {
        return;
    }

    VansMaterialManager* manager = m_Scene->GetMaterialManager();
    if (region.shFeedbackR == nullptr || region.shFeedbackG == nullptr || region.shFeedbackB == nullptr ||
        region.visibilityAtlas == nullptr || region.shRResult == nullptr ||
        region.shGResult == nullptr || region.shBResult == nullptr)
    {
        return;
    }
    VkDescriptorSet descriptorSet = m_GISamplePositionLightDescriptorSets[regionIndex];

    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();
    descManager->WriteBufferDescriptor(
        descriptorSet,
        GIPL_BINDING_HIT_POSITION,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitPositionResult.GetNativeBuffer(),
            0,
            region.hitPositionResult.GetBufferSize()
        }});
    descManager->WriteBufferDescriptor(
        descriptorSet,
        GIPL_BINDING_HIT_NORMAL,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitNormalResult.GetNativeBuffer(),
            0,
            region.hitNormalResult.GetBufferSize()
        }});

    descManager->WriteBufferDescriptor(
        descriptorSet,
        GIPL_BINDING_RADIANCE,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitRadianceBuffer.GetNativeBuffer(),
            0,
            region.hitRadianceBuffer.GetBufferSize()
        }});

    descManager->WriteBufferDescriptor(
        descriptorSet,
        GIPL_BINDING_PBR_DATA,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitAlbedoRoughnessResult.GetNativeBuffer(),
            0,
            region.hitAlbedoRoughnessResult.GetBufferSize()
        }});

    auto& skyImage = manager->m_PreConvDiffuse->GetImage();
    //设置天空盒
    descManager->WriteImageDescriptor(
        descriptorSet,
        GIPL_BINDING_ENVIRONMENT_MAP,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            skyImage.GetSampler(),
            skyImage.GetImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }});

    //设置球谐积分贴图
    descManager->WriteImageDescriptor(
            descriptorSet,
        GIPL_BINDING_SH_R,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            region.shFeedbackR->GetImage().GetSampler(),
            region.shFeedbackR->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});

    descManager->WriteBufferDescriptor(
        descriptorSet,
        GIPL_BINDING_DIRECT_CACHE,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitDirectDiffuseBuffer.GetNativeBuffer(),
            0,
            region.hitDirectDiffuseBuffer.GetBufferSize()
        }});
    descManager->WriteImageDescriptor(
        descriptorSet,
            GIPL_BINDING_SH_G,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{
            region.shFeedbackG->GetImage().GetSampler(),
            region.shFeedbackG->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->WriteImageDescriptor(
        descriptorSet,
            GIPL_BINDING_SH_B,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{
            region.shFeedbackB->GetImage().GetSampler(),
            region.shFeedbackB->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->WriteImageDescriptor(
        descriptorSet,
        GIPL_BINDING_SHADOW_MAP,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            VansRenderPassManager::GetInstance()->GetCascadeShadowSampler(),
            VansRenderPassManager::GetInstance()->GetCascadeShadowLayerView(1),  // matches RAYTRACING_CASCADE_INDEX in Common.glsl
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }});
    descManager->WriteImageDescriptor(
        descriptorSet,
        GIPL_BINDING_PUNCTUAL_SHADOW,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            VansRenderPassManager::GetInstance()->GetPunctualShadowMap().GetSampler(),
            VansRenderPassManager::GetInstance()->GetPunctualShadowMap().GetImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }});
    descManager->WriteImageDescriptor(
        descriptorSet,
        GIPL_BINDING_GI_VISIBILITY,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            region.visibilityAtlas->GetImage().GetSampler(),
            region.visibilityAtlas->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->WriteImageDescriptor(
        descriptorSet,
        GIPL_BINDING_RESULT_R,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{ region.shRResult->GetImage().GetSampler(), region.shRResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
    descManager->WriteImageDescriptor(
        descriptorSet,
        GIPL_BINDING_RESULT_G,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{ region.shGResult->GetImage().GetSampler(), region.shGResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
    descManager->WriteImageDescriptor(
        descriptorSet,
        GIPL_BINDING_RESULT_B,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{ region.shBResult->GetImage().GetSampler(), region.shBResult->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});

    descManager->CommitDescriptorUpdates();
    region.giPointLightDescriptorSetIsDirty = false;
}

void VansGraphics::VansRayTracing::BindGIVisibilityData(VansMaterialManager* materialManager, uint32_t regionIndex)
{
    if (regionIndex >= m_GIRegions.size() ||
        regionIndex >= m_GIVisibilityUpdateDescriptorSets.size())
    {
        return;
    }

    GIRegionRuntime& region = m_GIRegions[regionIndex];
    if (!region.giVisibilityDescriptorSetIsDirty)
    {
        return;
    }

    if (materialManager == nullptr || region.visibilityAtlas == nullptr)
    {
        return;
    }

    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();
    descManager->WriteBufferDescriptor(
        m_GIVisibilityUpdateDescriptorSets[regionIndex],
        GI_VISIBILITY_BINDING_HIT_POSITION,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitPositionResult.GetNativeBuffer(),
            0,
            region.hitPositionResult.GetBufferSize()
        }});
    descManager->WriteImageDescriptor(
        m_GIVisibilityUpdateDescriptorSets[regionIndex],
        GI_VISIBILITY_BINDING_RESULT,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{
            region.visibilityAtlas->GetImage().GetSampler(),
            region.visibilityAtlas->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->CommitDescriptorUpdates();
    region.giVisibilityDescriptorSetIsDirty = false;
}

void VansGraphics::VansRayTracing::BindGIRTPreviewData(VansMaterialManager* materialManager)
{
    GIRegionRuntime* previewRegion = GetPreviewRegion();
    if (!m_GIRTPreviewDescriptorSetIsDirty || materialManager == nullptr ||
        m_GIRTPreviewTexture == nullptr || previewRegion == nullptr ||
        previewRegion->rayTracingResult == nullptr)
    {
        return;
    }

    auto* shR = previewRegion->shRResult;
    auto* shG = previewRegion->shGResult;
    auto* shB = previewRegion->shBResult;
    if (shR == nullptr || shG == nullptr || shB == nullptr || m_GIRTPreviewDescriptorSets.empty())
        return;

    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();
    const VkDeviceSize elementBytes = sizeof(uint16_t) * 4u;
    const glm::uvec3& grid = previewRegion->resolved.gridDimensions;
    const VkDeviceSize sliceBytes = static_cast<VkDeviceSize>(grid.x) *
        grid.y * std::max(static_cast<int>(previewRegion->resolved.raysPerProbe), 1) * elementBytes;
    const uint32_t zSlice = std::min(
        static_cast<uint32_t>(m_GIRTPreviewConstant.selectionParams.y),
        std::max(grid.z, 1u) - 1u);
    const VkDeviceSize sliceOffset = static_cast<VkDeviceSize>(zSlice) * sliceBytes;
    const VkDeviceSize alignedOffset = sliceOffset - (sliceOffset % m_GIRTPreviewStorageBufferAlignment);
    const VkDeviceSize prefixBytes = sliceOffset - alignedOffset;
    m_GIRTPreviewConstant.displayParams.y = static_cast<float>(prefixBytes / elementBytes);

    auto writeBuffer = [&](uint32_t binding, VansVKBuffer& buffer)
    {
		const VkDeviceSize availableBytes = buffer.GetBufferSize() > alignedOffset
			? buffer.GetBufferSize() - alignedOffset
			: 0;
		const VkDeviceSize range = std::min(prefixBytes + sliceBytes, availableBytes);
        descManager->WriteBufferDescriptor(
            m_GIRTPreviewDescriptorSets[0], binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {{ buffer.GetNativeBuffer(), alignedOffset, range }});
    };
    auto writeImage = [&](uint32_t binding, VansTexture* texture)
    {
        descManager->WriteImageDescriptor(
            m_GIRTPreviewDescriptorSets[0], binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            {{ texture->GetImage().GetSampler(), texture->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
    };

    writeBuffer(GI_RT_PREVIEW_BINDING_HIT_POSITION, previewRegion->hitPositionResult);
    writeBuffer(GI_RT_PREVIEW_BINDING_HIT_NORMAL, previewRegion->hitNormalResult);
    writeBuffer(GI_RT_PREVIEW_BINDING_HIT_PBR, previewRegion->hitAlbedoRoughnessResult);
    writeBuffer(GI_RT_PREVIEW_BINDING_DIRECT_LIGHT, previewRegion->hitRadianceBuffer);
    writeImage(GI_RT_PREVIEW_BINDING_RAY_SUMMARY, previewRegion->rayTracingResult);
    writeImage(GI_RT_PREVIEW_BINDING_SH_R, shR);
    writeImage(GI_RT_PREVIEW_BINDING_SH_G, shG);
    writeImage(GI_RT_PREVIEW_BINDING_SH_B, shB);
    writeImage(GI_RT_PREVIEW_BINDING_OUTPUT, m_GIRTPreviewTexture);
    descManager->CommitDescriptorUpdates();
    m_GIRTPreviewDescriptorSetIsDirty = false;
	m_GIRTPreviewBoundZSlice = zSlice;
}

void VansGraphics::VansRayTracing::DispatchGIRTPreview(
    VansVKCommandBuffer* commandBuffer,
    VansMaterialManager* materialManager)
{
    if (m_GIRTPreviewRequestFrames == 0 || commandBuffer == nullptr ||
        m_GIRTPreviewShader == nullptr || m_GIRTPreviewTexture == nullptr)
    {
        return;
    }

    BindGIRTPreviewData(materialManager);
    if (m_GIRTPreviewDescriptorSetIsDirty || m_GIRTPreviewDescriptorSets.empty())
        return;

    VkMemoryBarrier sourceBarrier{};
    sourceBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    sourceBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    sourceBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    commandBuffer->PipelineBarrier(
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR |
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
            VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        { sourceBarrier });

    commandBuffer->EnsureComputeShader(*m_GIRTPreviewShader, { m_GIRTPreviewSetLayout });
    const GIRegionRuntime* previewRegion = GetPreviewRegion();
    if (previewRegion == nullptr)
        return;

    commandBuffer->DispatchCompute(
        *m_GIRTPreviewShader,
        (previewRegion->resolved.gridDimensions.x + 7u) / 8u,
        (previewRegion->resolved.gridDimensions.y + 7u) / 8u,
        1u,
        { m_GIRTPreviewDescriptorSets[0] },
        &m_GIRTPreviewConstant, sizeof(m_GIRTPreviewConstant));

    VkMemoryBarrier previewBarrier{};
    previewBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    previewBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    previewBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    commandBuffer->PipelineBarrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        { previewBarrier });
    --m_GIRTPreviewRequestFrames;
}

void VansGraphics::VansRayTracing::CreateGIVisibilityUpdateDescriptorSets(VansVKDevice* device)
{
    VansDescriptorSetLayoutFactory::CreateAndAllocate_GIVisibilityUpdate(
        m_GIVisibilityUpdateSetLayout,
        m_GIVisibilityUpdateDescriptorSets,
        static_cast<uint32_t>(std::max<size_t>(m_GIRegions.size(), 1u)));

    for (GIRegionRuntime& region : m_GIRegions)
        region.giVisibilityDescriptorSetIsDirty = true;
}

void VansGraphics::VansRayTracing::CreateGIRTPreviewDescriptorSets(VansVKDevice* device)
{
    VansDescriptorSetLayoutFactory::CreateAndAllocate_GIRTPreview(
        m_GIRTPreviewSetLayout,
        m_GIRTPreviewDescriptorSets);
    m_GIRTPreviewDescriptorSetIsDirty = true;
}

void VansGraphics::VansRayTracing::DispatchRayTracing(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene)
{
    if (!m_RTResourcesReady || m_GIRegions.empty())
    {
        return;
    }

    VansVKRayTracingPipeline* vansPipeline = m_VansRayTracingShader->GetRayTracingPipeline(device, { m_RayTracingSetLayout });

    // Make prior AS build/updates visible to RT stage (use a memory barrier)
    {
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
            VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        commandBuffer->PipelineBarrier(
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            { mb });
    }

    commandBuffer->BindRayTracingPipeline(*vansPipeline);
    for (uint32_t regionIndex = 0; regionIndex < m_GIRegions.size(); ++regionIndex)
    {
        GIRegionRuntime& region = m_GIRegions[regionIndex];
        if (region.hitPositionCalculateDone)
            continue;

        region.hitPositionCalculateDone = true;
        BindRayTracingData(device, scene, regionIndex);

        commandBuffer->BindRayTracingDescriptorSets(
            *vansPipeline,
            0,
            { m_RayTracingDescriptorSets[regionIndex] });
        commandBuffer->UpdateRayTracingPushConstants(
            *vansPipeline,
            VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR,
            0,
            m_VansRayTracingShader->GetPushConstantSize(),
            &region.constants);

        commandBuffer->TraceRays(
            *vansPipeline,
            region.resolved.gridDimensions.x,
            region.resolved.gridDimensions.y,
            region.resolved.gridDimensions.z);

        // Barrier: RT shader writes hit buffers before GI point-light compute reads them.
        {
            VkMemoryBarrier rtToComputeBarrier{};
            rtToComputeBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            rtToComputeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            rtToComputeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            commandBuffer->PipelineBarrier(
                VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                { rtToComputeBarrier });
        }

        if (!region.giVisibilityCalculateDone && m_GIVisibilityUpdateShader != nullptr)
        {
            BindGIVisibilityData(scene->GetMaterialManager(), regionIndex);

            const int totalProbeCount = static_cast<int>(std::max<uint64_t>(region.resolved.probeCount, 1u));
            const int probesPerAtlasRow = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(totalProbeCount)))));
            const int probesPerAtlasColumn = (totalProbeCount + probesPerAtlasRow - 1) / probesPerAtlasRow;
            const uint32_t atlasWidth = static_cast<uint32_t>(probesPerAtlasRow * kGIVisibilityOctaRes);
            const uint32_t atlasHeight = static_cast<uint32_t>(probesPerAtlasColumn * kGIVisibilityOctaRes);

            commandBuffer->EnsureComputeShader(*m_GIVisibilityUpdateShader, { m_GIVisibilityUpdateSetLayout });
            commandBuffer->DispatchCompute(
                *m_GIVisibilityUpdateShader,
                (atlasWidth + 7u) / 8u,
                (atlasHeight + 7u) / 8u,
                1,
                { m_GIVisibilityUpdateDescriptorSets[regionIndex] },
                &region.constants,
                sizeof(region.constants));

            region.giVisibilityCalculateDone = true;
        }

        VkMemoryBarrier visibilityToGIBarrier{};
        visibilityToGIBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        visibilityToGIBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        visibilityToGIBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        commandBuffer->PipelineBarrier(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            { visibilityToGIBarrier });
    }
}

void VansGraphics::VansRayTracing::CreateRayTraceDescriptorSets(VansVKDevice* device, int blasMeshCount)
{
    const uint32_t blasDescriptorCount = static_cast<uint32_t>(std::max(blasMeshCount, 1));

    VkDescriptorSetLayoutBinding tlasBinding =
    {
        RT_BINDING_TLAS,
        VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };
    VkDescriptorSetLayoutBinding resultBinding =
    {
        PassBinding::UAV_IMAGE_0,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };
    VkDescriptorSetLayoutBinding hitPositionResultBinding =
    {
        PassBinding::BUFFER_2,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };
    VkDescriptorSetLayoutBinding hitNormalResultBinding =
    {
        PassBinding::BUFFER_6,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };

    //blas data buffer
    VkDescriptorSetLayoutBinding vertexDataBuffer =
    {
        PassBinding::BUFFER_3,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        blasDescriptorCount,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };

    VkDescriptorSetLayoutBinding indexDataBuffer =
    {
        PassBinding::BUFFER_4,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        blasDescriptorCount,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };

    //instance data buffer
    VkDescriptorSetLayoutBinding instanceDataBuffer =
    {
        PassBinding::BUFFER_5,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };
    //instance texture index buffer
    VkDescriptorSetLayoutBinding instanceTextureIndexDataBuffer =
    {
        PassBinding::BUFFER_7,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };

    //instance texture index buffer
    VkDescriptorSetLayoutBinding hitPBRAlbedoRoughnessDataBuffer =
    {
        PassBinding::BUFFER_8,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };


	//bindless texture array
    VkDescriptorSetLayoutBinding bindlessTextureArrayBinding =
    {
        GLOBAL_BINDING_BINDLESS_TEXTURES,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        MAX_BINDLESS_TEXTURES,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR |
        VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR |
        VK_SHADER_STAGE_MISS_BIT_KHR |
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
	};
    

	VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(
        {
            tlasBinding,
            resultBinding,
            hitPositionResultBinding,
            vertexDataBuffer, 
            indexDataBuffer ,
            instanceDataBuffer,
            hitNormalResultBinding,
            instanceTextureIndexDataBuffer,
            bindlessTextureArrayBinding,
            hitPBRAlbedoRoughnessDataBuffer
        },
        m_RayTracingSetLayout,
        m_RayTracingDescriptorSets,
        static_cast<uint32_t>(std::max<size_t>(m_GIRegions.size(), 1u)));
    
    for (GIRegionRuntime& region : m_GIRegions)
        region.rayTracingDescriptorSetIsDirty = true;
}

void VansGraphics::VansRayTracing::CreateGIPointLightDescriptorSets(VansVKDevice* device)
{
    VansDescriptorSetLayoutFactory::CreateAndAllocate_GIPointLight(
        m_GISamplePositionLightSetLayout,
        m_GISamplePositionLightDescriptorSets,
        static_cast<uint32_t>(std::max<size_t>(m_GIRegions.size(), 1u)));

    for (GIRegionRuntime& region : m_GIRegions)
        region.giPointLightDescriptorSetIsDirty = true;
}

void VansGraphics::VansRayTracing::BindRayTracingData(VansVKDevice* device, VansScene* scene, uint32_t regionIndex)
{
    if (regionIndex >= m_GIRegions.size() ||
        regionIndex >= m_RayTracingDescriptorSets.size())
    {
        return;
    }

    GIRegionRuntime& region = m_GIRegions[regionIndex];
    if (!region.rayTracingDescriptorSetIsDirty)
    {
        return;
    }
    region.rayTracingDescriptorSetIsDirty = false;

    VkAccelerationStructureKHR& tlas = scene->GetTopAS();
    std::vector<VansVKBuffer>& vertexBuffers = scene->GetBLASVertexBuffers();
    std::vector<VansVKBuffer>& indexBuffers = scene->GetBLASIndexBuffers();
    int blasMeshCount = vertexBuffers.size();

    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();
    descManager->WriteBufferDescriptor(
        m_RayTracingDescriptorSets[regionIndex],
        PassBinding::BUFFER_2,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitPositionResult.GetNativeBuffer(),
            0,
            region.hitPositionResult.GetBufferSize()
        }});
    descManager->WriteBufferDescriptor(
        m_RayTracingDescriptorSets[regionIndex],
        PassBinding::BUFFER_6,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitNormalResult.GetNativeBuffer(),
            0,
            region.hitNormalResult.GetBufferSize()
        }});

    std::vector<VkDescriptorBufferInfo> blasVertexBufferInfos;
    for (int blasMeshIndex = 0; blasMeshIndex < blasMeshCount; blasMeshIndex++)
    {
        blasVertexBufferInfos.push_back(
            {
                vertexBuffers[blasMeshIndex].GetNativeBuffer(),
                0,
                vertexBuffers[blasMeshIndex].GetBufferSize()
            }
        );
    }
    descManager->WriteBufferDescriptor(
        m_RayTracingDescriptorSets[regionIndex],
        PassBinding::BUFFER_3,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        blasVertexBufferInfos);

    std::vector<VkDescriptorBufferInfo> blasIndexBufferInfos;
    for (int blasMeshIndex = 0; blasMeshIndex < blasMeshCount; blasMeshIndex++)
    {
        blasIndexBufferInfos.push_back(
            {
                indexBuffers[blasMeshIndex].GetNativeBuffer(),
                0,
                indexBuffers[blasMeshIndex].GetBufferSize()
            }
        );
    }
    descManager->WriteBufferDescriptor(
        m_RayTracingDescriptorSets[regionIndex],
        PassBinding::BUFFER_4,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        blasIndexBufferInfos);

    descManager->WriteBufferDescriptor(
        m_RayTracingDescriptorSets[regionIndex],
        PassBinding::BUFFER_5,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_BLASInstanceBuffer.GetNativeBuffer(),
            0,
            m_BLASInstanceBuffer.GetBufferSize()
        }});

    descManager->WriteBufferDescriptor(
        m_RayTracingDescriptorSets[regionIndex],
        PassBinding::BUFFER_7,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_TLASInstanceTextureIndexBuffer.GetNativeBuffer(),
            0,
            m_TLASInstanceTextureIndexBuffer.GetBufferSize()
        }});
    descManager->WriteBufferDescriptor(
        m_RayTracingDescriptorSets[regionIndex],
        PassBinding::BUFFER_8,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitAlbedoRoughnessResult.GetNativeBuffer(),
            0,
            region.hitAlbedoRoughnessResult.GetBufferSize()
        }});
    descManager->WriteAccelerationStructureDescriptor(
        m_RayTracingDescriptorSets[regionIndex],
        RT_BINDING_TLAS,
        tlas);
    descManager->WriteImageDescriptor(
        m_RayTracingDescriptorSets[regionIndex],
        PassBinding::UAV_IMAGE_0,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{
            region.rayTracingResult->GetImage().GetSampler(),
            region.rayTracingResult->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});

	//绑定bindless贴图数组
    auto& bindlessTextures = scene->GetTLASInstanceTextures();
	std::vector<VkDescriptorImageInfo> bindlessTextureInfos;
    for(size_t i = 0; i < bindlessTextures.size(); i++)
    {
        bindlessTextureInfos.push_back(
            {
                bindlessTextures[i].GetSampler(),
                bindlessTextures[i].GetImageView(),
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
            }
		);
    }
    descManager->WriteImageDescriptor(
        m_RayTracingDescriptorSets[regionIndex],
        GLOBAL_BINDING_BINDLESS_TEXTURES,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        bindlessTextureInfos);

    descManager->CommitDescriptorUpdates();
}

