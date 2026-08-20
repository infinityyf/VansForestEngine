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
#include "../../Util/VansProfiler.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    constexpr int kGIVisibilityOctaRes = 16;
    constexpr int kGIIrradianceOctaRes = 8;

    VansGraphics::RayTracingPushConstant BuildGIRegionPushConstant(
        const VansGraphics::GIResolvedRegion& region,
        float environmentIntensity,
        float maxIndirectRadiance,
        float maxProbeRadiance,
        float irradianceHysteresis,
        float distanceHysteresis,
        float distanceSharpness,
        float brightnessChangeThreshold)
    {
        VansGraphics::RayTracingPushConstant constants{};
        constants.dispatchParams = glm::vec4(
            glm::vec3(region.gridDimensions),
            static_cast<float>(region.raysPerProbe));
        constants.gridParams = glm::vec4(glm::vec3(region.probeSpacing), region.maxRayDistance);
        constants.frameParams = glm::vec4(
            0.0f,
            static_cast<float>(region.spatialUpdateDivisor),
            static_cast<float>(region.directionUpdateSlices),
            0.0f);
        constants.regionParams = glm::vec4(region.center, region.normalBias);
        constants.lightingParams = glm::vec4(
            std::max(environmentIntensity, 0.0f),
            std::max(maxIndirectRadiance, 0.0f),
            std::max(maxProbeRadiance, 0.0f),
            0.0f);
        constants.temporalParams = glm::vec4(
            std::clamp(irradianceHysteresis, 0.0f, 0.999f),
            std::clamp(distanceHysteresis, 0.0f, 0.999f),
            std::clamp(distanceSharpness, 8.0f, 16.0f),
            std::max(brightnessChangeThreshold, 0.001f));
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

    uint64_t MixGILightSignature(uint64_t seed, uint64_t value)
    {
        return seed ^ (value + 0x9e3779b97f4a7c15ull + (seed << 6u) + (seed >> 2u));
    }

    uint64_t QuantizeGILightFloat(float value, float scale = 1024.0f)
    {
        if (!std::isfinite(value))
            value = 0.0f;
        return static_cast<uint64_t>(static_cast<int64_t>(std::llround(value * scale)));
    }

    uint64_t MixGILightVec3(uint64_t seed, const glm::vec3& value, float scale = 1024.0f)
    {
        seed = MixGILightSignature(seed, QuantizeGILightFloat(value.x, scale));
        seed = MixGILightSignature(seed, QuantizeGILightFloat(value.y, scale));
        return MixGILightSignature(seed, QuantizeGILightFloat(value.z, scale));
    }

    uint64_t BuildGILightSignature(VansGraphics::VansLightManager* lightManager)
    {
        if (lightManager == nullptr)
            return 0ull;

        uint64_t signature = 1469598103934665603ull;
        const auto& directionLights = lightManager->GetDirectionLights();
        const auto& pointLights = lightManager->GetPointLights();
        const auto& spotLights = lightManager->GetSpotLight();
        const auto& rectLights = lightManager->GetRectLights();

        signature = MixGILightSignature(signature, static_cast<uint64_t>(directionLights.size()));
        signature = MixGILightSignature(signature, static_cast<uint64_t>(pointLights.size()));
        signature = MixGILightSignature(signature, static_cast<uint64_t>(spotLights.size()));
        signature = MixGILightSignature(signature, static_cast<uint64_t>(rectLights.size()));

        for (const VansGraphics::VansDirectionalLight& light : directionLights)
        {
            const VansGraphics::VansCelestialLightingState celestialState =
                VansGraphics::VansLightManager::ComputeCelestialLightingState(light);
            signature = MixGILightVec3(signature, celestialState.direction);
            signature = MixGILightVec3(signature, celestialState.color, 4096.0f);
            signature = MixGILightSignature(signature, QuantizeGILightFloat(celestialState.intensity, 4096.0f));
            signature = MixGILightSignature(signature, QuantizeGILightFloat(celestialState.skyDiffuseScale, 4096.0f));
        }
        for (const VansGraphics::VansPointLight& light : pointLights)
        {
            signature = MixGILightVec3(signature, light.m_Position);
            signature = MixGILightVec3(signature, light.m_Color, 4096.0f);
            signature = MixGILightSignature(signature, QuantizeGILightFloat(light.m_Intensity, 4096.0f));
            signature = MixGILightSignature(signature, QuantizeGILightFloat(light.m_Radius, 1024.0f));
        }
        for (const VansGraphics::VansSpotLight& light : spotLights)
        {
            signature = MixGILightVec3(signature, light.m_Position);
            signature = MixGILightVec3(signature, light.m_Direction);
            signature = MixGILightVec3(signature, light.m_Color, 4096.0f);
            signature = MixGILightSignature(signature, QuantizeGILightFloat(light.m_Intensity, 4096.0f));
            signature = MixGILightSignature(signature, QuantizeGILightFloat(light.m_Radius, 1024.0f));
            signature = MixGILightSignature(signature, QuantizeGILightFloat(light.m_InnerCutOff, 4096.0f));
            signature = MixGILightSignature(signature, QuantizeGILightFloat(light.m_OuterCutOff, 4096.0f));
        }
        for (const VansGraphics::VansRectLight& light : rectLights)
        {
            signature = MixGILightVec3(signature, light.m_Position);
            signature = MixGILightVec3(signature, light.m_Normal);
            signature = MixGILightVec3(signature, light.m_Right);
            signature = MixGILightVec3(signature, light.m_Up);
            signature = MixGILightVec3(signature, light.m_Color, 4096.0f);
            signature = MixGILightSignature(signature, QuantizeGILightFloat(light.m_Intensity, 4096.0f));
            signature = MixGILightSignature(signature, QuantizeGILightFloat(light.m_Range, 1024.0f));
            signature = MixGILightSignature(signature, QuantizeGILightFloat(light.m_HalfWidth, 1024.0f));
            signature = MixGILightSignature(signature, QuantizeGILightFloat(light.m_HalfHeight, 1024.0f));
        }
        return signature;
    }
}

void VansGraphics::VansRayTracing::CleanupSceneResources(VkDevice device, VansMaterialManager* materialManager)
{
	if (!m_RTResourcesReady)
		return;

	auto descMgr = VansVKDescriptorManager::GetInstance();

	// 释放 RT descriptor set 和 layout
	descMgr->DestroyDescriptorSet(m_RayTracingDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_RayTracingSetLayout);

	descMgr->DestroyDescriptorSet(m_GISamplePositionLightDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GISamplePositionLightSetLayout);

	descMgr->DestroyDescriptorSet(m_GIVisibilityUpdateDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GIVisibilityUpdateSetLayout);
	descMgr->DestroyDescriptorSet(m_GIProbeStateDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GIProbeStateSetLayout);

	descMgr->DestroyDescriptorSet(m_GIRTPreviewDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GIRTPreviewSetLayout);

	// 释放 RT 相关 buffer
	m_BLASInstanceBuffer.DestroyVulkanBuffer(device);
	m_TLASInstanceTextureIndexBuffer.DestroyVulkanBuffer(device);
	m_TLASInstanceGIEmissionBuffer.DestroyVulkanBuffer(device);

	for (GIRegionRuntime& region : m_GIRegions)
		DestroyRegionRuntime(device, region);
	m_GIRegions.clear();
	for (VansTexture*& previewTexture : m_GIRTPreviewTextures)
		DeleteTexture(previewTexture);

	m_RayTracingPointLighting = nullptr;

	m_GIVisibilityUpdateShader = nullptr;
	m_GIProbeStateShader = nullptr;

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
	region.hitEmissionResult.DestroyVulkanBuffer(device);
	region.hitRadianceBuffer.DestroyVulkanBuffer(device);
	region.probeStateBuffer.DestroyVulkanBuffer(device);
	DeleteTexture(region.rayTracingResult);
	DeleteTexture(region.irradianceAtlas);
	DeleteTexture(region.screenIrradianceAtlas);
	DeleteTexture(region.visibilityAtlas);
	region = GIRegionRuntime{};
}

VansGraphics::VansTexture* VansGraphics::VansRayTracing::GetGIRegionIrradianceAtlas(uint32_t regionIndex) const
{
	return regionIndex < m_GIRegions.size() ? m_GIRegions[regionIndex].irradianceAtlas : nullptr;
}

VansGraphics::VansTexture* VansGraphics::VansRayTracing::GetGIRegionScreenIrradianceAtlas(uint32_t regionIndex) const
{
	return regionIndex < m_GIRegions.size()
		? m_GIRegions[regionIndex].screenIrradianceAtlas
		: nullptr;
}

VansGraphics::VansTexture* VansGraphics::VansRayTracing::GetGIRegionVisibilityAtlas(uint32_t regionIndex) const
{
	return regionIndex < m_GIRegions.size() ? m_GIRegions[regionIndex].visibilityAtlas : nullptr;
}

const VansGraphics::VansVKBuffer* VansGraphics::VansRayTracing::GetGIRegionProbeStateBuffer(uint32_t regionIndex) const
{
	return regionIndex < m_GIRegions.size() ? &m_GIRegions[regionIndex].probeStateBuffer : nullptr;
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
	std::vector<glm::vec4>& instanceGIEmission = scene->GetTLASInstanceGIEmission();

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

    const std::vector<const GIProbeRegionDesc*> activeRegions = BuildActiveGIRegionOrder(gi);

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

    for (const GIProbeRegionDesc* regionDesc : activeRegions)
    {
        m_GIRegions.emplace_back();
        GIRegionRuntime& regionRuntime = m_GIRegions.back();
        regionRuntime.resolved = ResolveGIRegion(*regionDesc);
        regionRuntime.constants = BuildGIRegionPushConstant(
            regionRuntime.resolved,
            m_BaseGIEnvironmentIntensity,
            gi.maxIndirectRadiance,
            gi.maxProbeRadiance,
            gi.irradianceHysteresis,
            gi.distanceHysteresis,
            gi.distanceSharpness,
            gi.brightnessChangeThreshold);

        const glm::uvec3& grid = regionRuntime.resolved.gridDimensions;
        const uint64_t probeCount64 = regionRuntime.resolved.probeCount;
        // Working hit data is only the active spatial phase and direction
        // slice.  Retaining one entry per probe/ray turns every later update
        // into a read of stale geometry and is both incorrect and expensive.
        const GIProbeUpdateBatch maxBatch = BuildGIProbeUpdateBatch(regionRuntime.resolved, 0u);
        const uint64_t rayCacheEntries64 = std::max<uint64_t>(maxBatch.activeRayCount, 1u);

        VANS_LOG("[CreateRayTracingResource] GI region '" << regionRuntime.resolved.name
            << "' grid=" << grid.x << "x" << grid.y << "x" << grid.z
            << " spacing=" << regionRuntime.resolved.probeSpacing
            << " probes=" << probeCount64
            << " activeRayEntries=" << rayCacheEntries64
            << " fullUpdateCycleFrames=" << maxBatch.fullUpdateCycleFrameCount);

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

        const int totalProbeCount = static_cast<int>(std::max<uint64_t>(probeCount64, 1u));
        const int probesPerAtlasRow = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(totalProbeCount)))));
        const int probesPerAtlasColumn = (totalProbeCount + probesPerAtlasRow - 1) / probesPerAtlasRow;

        regionRuntime.irradianceAtlas = new VansTexture();
        regionRuntime.irradianceAtlas->InitTextureWithoutData(
            *commandBuffer,
            probesPerAtlasRow * kGIIrradianceOctaRes,
            probesPerAtlasColumn * kGIIrradianceOctaRes,
            1,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            false,
            false,
            true,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

		// 与原 irradiance atlas 保持相同 tile 布局。DDGI atlas 更新完成后在
		// 同一 workgroup 预合成轴向补偿，供屏幕查询每探针只采样一次。
		regionRuntime.screenIrradianceAtlas = new VansTexture();
		regionRuntime.screenIrradianceAtlas->InitTextureWithoutData(
			*commandBuffer,
			probesPerAtlasRow * kGIIrradianceOctaRes,
			probesPerAtlasColumn * kGIIrradianceOctaRes,
			1,
			VK_FORMAT_R16G16B16A16_SFLOAT,
			false,
			false,
			true,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

        regionRuntime.visibilityAtlas = new VansTexture();
        regionRuntime.visibilityAtlas->InitTextureWithoutData(
            *commandBuffer,
            probesPerAtlasRow * kGIVisibilityOctaRes,
            probesPerAtlasColumn * kGIVisibilityOctaRes,
            1,
            VK_FORMAT_R16G16_SFLOAT,
            false,
            false,
            true,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);

        const VkDeviceSize giHitBufferSize = static_cast<VkDeviceSize>(rayCacheEntries64) * sizeof(uint16_t) * 4;

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
		regionRuntime.hitEmissionResult.CreatVulkanBuffer(device->GetLogicDevice(),
			giHitBufferSize,
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        regionRuntime.hitRadianceBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
            giHitBufferSize,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        regionRuntime.probeStateBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
            static_cast<VkDeviceSize>(std::max<uint64_t>(probeCount64, 1u)) * 48u,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    }

	const GIRegionRuntime* previewRegion = GetPreviewRegion();
	if (previewRegion == nullptr)
	{
		VANS_LOG_ERROR("[CreateRayTracingResource] No enabled GI region is available for the preview target.");
		m_RTResourcesReady = false;
		return;
	}
	for (VansTexture*& previewTexture : m_GIRTPreviewTextures)
	{
		previewTexture = new VansTexture();
		previewTexture->InitTextureWithoutData(
			*commandBuffer,
			static_cast<int>(previewRegion->resolved.gridDimensions.x),
			static_cast<int>(previewRegion->resolved.gridDimensions.y),
			1, VK_FORMAT_R32G32B32A32_SFLOAT, false, false, true,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
	}
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

	if (instanceGIEmission.size() != instanceData.size())
	{
		VANS_LOG_ERROR("[CreateRayTracingResource] TLAS GI emissive payload does not match TLAS instance count.");
		m_RTResourcesReady = false;
		return;
	}
	m_TLASInstanceGIEmissionBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
		instanceGIEmission.size() * sizeof(glm::vec4),
		VK_FORMAT_R32G32B32A32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
	m_TLASInstanceGIEmissionBuffer.SetBufferData(instanceGIEmission.data(), 0,
		instanceGIEmission.size() * sizeof(glm::vec4));

    // GI 分帧更新会在冷启动阶段读取尚未轮到的方向 slice。
    // 显式清零所有反馈资源，保证未写入 slice 的 radiance 为 0，而不是未定义显存。
    if (commandBuffer->BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
    {
        VkClearColorValue clearSH{};
        for (GIRegionRuntime& regionRuntime : m_GIRegions)
        {
            commandBuffer->FillBuffer(regionRuntime.hitRadianceBuffer.GetNativeBuffer(), 0, regionRuntime.hitRadianceBuffer.GetBufferSize(), 0u);
			commandBuffer->FillBuffer(regionRuntime.probeStateBuffer.GetNativeBuffer(), 0, regionRuntime.probeStateBuffer.GetBufferSize(), 0u);
			commandBuffer->ClearColorImage(regionRuntime.visibilityAtlas->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
			commandBuffer->ClearColorImage(regionRuntime.irradianceAtlas->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
			commandBuffer->ClearColorImage(regionRuntime.screenIrradianceAtlas->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
            commandBuffer->ClearColorImage(regionRuntime.rayTracingResult->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
        }
		for (VansTexture* previewTexture : m_GIRTPreviewTextures)
			commandBuffer->ClearColorImage(previewTexture->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);

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

    m_GIProbeStateShader = shaderManager.FindComputeShader("GIProbeState");
    if (!m_GIProbeStateShader)
    {
        VANS_LOG_ERROR("[CreateRayTracingResource] Managed GIProbeState shader is unavailable");
        return;
    }
    CreateGIProbeStateDescriptorSets(device);

	m_GIRTPreviewShader = shaderManager.FindComputeShader("GIRTPreview");
	if (!m_GIRTPreviewShader)
	{
		VANS_LOG_ERROR("[CreateRayTracingResource] One or more managed GI compute shaders are unavailable");
		return;
	}
	CreateGIRTPreviewDescriptorSets(device);

    m_HasLastGIMainLight = false;
	m_GIRTPreviewRequestFrames = 0;
}

void VansGraphics::VansRayTracing::UpdateGISettings(const VansGISettings& settings)
{
    VansGISettings gi = settings;
    NormalizeGISettings(gi);

    const std::vector<const GIProbeRegionDesc*> activeRegions = BuildActiveGIRegionOrder(gi);

    m_BaseGIEnvironmentIntensity = std::max(gi.environmentIntensity, 0.0f);
    const size_t updateCount = std::min(m_GIRegions.size(), activeRegions.size());
    for (size_t regionIndex = 0; regionIndex < updateCount; ++regionIndex)
    {
        GIRegionRuntime& region = m_GIRegions[regionIndex];
        const float previousFrameIndex = region.constants.frameParams.x;
        region.resolved = ResolveGIRegion(*activeRegions[regionIndex]);
        region.constants = BuildGIRegionPushConstant(
            region.resolved,
            m_BaseGIEnvironmentIntensity,
            gi.maxIndirectRadiance,
            gi.maxProbeRadiance,
            gi.irradianceHysteresis,
            gi.distanceHysteresis,
            gi.distanceSharpness,
            gi.brightnessChangeThreshold);
        region.constants.frameParams.x = previousFrameIndex;
    }
}

void VansGraphics::VansRayTracing::RequestGIRTPreviews(
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
    const uint32_t directionSlices = std::max(previewRegion->resolved.directionUpdateSlices, 1u);
	const uint32_t raysPerActiveProbe = (rayCount + directionSlices - 1u) / directionSlices;
    const uint32_t updateFrame = static_cast<uint32_t>(previewRegion->constants.frameParams.x);
	const GIProbeUpdateBatch previewBatch = BuildGIProbeUpdateBatch(previewRegion->resolved, updateFrame);
    const uint32_t safeZSlice = zSlice == std::numeric_limits<uint32_t>::max()
        ? previewBatch.spatialOffset.z
        : std::min(zSlice, zCount - 1u);
    if (m_GIRTPreviewBoundZSlice != safeZSlice)
        m_GIRTPreviewDescriptorSetIsDirty = true;
    for (uint32_t mode = 0u; mode < GIRTPreviewModeCount; ++mode)
    {
        GIRTPreviewPushConstant& constants = m_GIRTPreviewConstants[mode];
        constants.gridParams = glm::vec4(
            glm::vec3(previewRegion->resolved.gridDimensions),
            static_cast<float>(rayCount));
        constants.selectionParams = glm::vec4(
            static_cast<float>(mode),
            static_cast<float>(safeZSlice),
            static_cast<float>(std::min(rayIndex, rayCount - 1u)),
            std::max(exposure, 0.001f));
        constants.displayParams = glm::vec4(
			std::max(positionScale, 0.0001f), 0.0f,
			std::max(previewRegion->resolved.maxRayDistance, 0.001f), 0.0f);
		constants.updateParams = glm::vec4(
			previewRegion->constants.frameParams.x,
			previewRegion->constants.frameParams.y,
			previewRegion->constants.frameParams.z,
			static_cast<float>(raysPerActiveProbe));
    }

    // The editor refreshes this lease while the preview is visible. It avoids
    // paying for the gather pass after the window is closed.
    m_GIRTPreviewRequestFrames = 3;
}

bool VansGraphics::VansRayTracing::UpdateLightingResponseState(VansLightManager* lightManager)
{
    if (lightManager == nullptr)
        return false;

    const uint64_t lightSignature = BuildGILightSignature(lightManager);
    glm::vec4 directionIntensity(0.0f);
    glm::vec4 lightColor(0.0f);

    auto& directionLights = lightManager->GetDirectionLights();
	float environmentIntensity = m_BaseGIEnvironmentIntensity;
	if (!directionLights.empty())
	{
		const VansCelestialLightingState celestialState =
			VansLightManager::ComputeCelestialLightingState(directionLights[0]);
		environmentIntensity = m_BaseGIEnvironmentIntensity * std::max(celestialState.skyDiffuseScale, 0.0f);
		glm::vec3 direction = celestialState.direction;
        if (glm::dot(direction, direction) > 1e-6f)
            direction = glm::normalize(direction);
        else
            direction = glm::vec3(0.0f, 1.0f, 0.0f);

        directionIntensity = glm::vec4(direction, celestialState.intensity);
        lightColor = glm::vec4(celestialState.color, 0.0f);
    }

	const bool hasPreviousLightingState = m_HasLastGIMainLight;
	bool changed = false;
	if (hasPreviousLightingState)
	{
        const float directionDelta = glm::length(glm::vec3(directionIntensity) - glm::vec3(m_LastGIMainLightDirectionIntensity));
        const float intensityDelta = std::abs(directionIntensity.w - m_LastGIMainLightDirectionIntensity.w);
        const float colorDelta = glm::length(glm::vec3(lightColor) - glm::vec3(m_LastGIMainLightColor));
        changed = directionDelta > 0.0025f || intensityDelta > 0.01f ||
            colorDelta > 0.01f || lightSignature != m_LastGILightSignature;
    }

	m_CurrentGIEnvironmentIntensity = environmentIntensity;
    m_LastGIMainLightDirectionIntensity = directionIntensity;
    m_LastGIMainLightColor = lightColor;
    m_LastGILightSignature = lightSignature;
    m_HasLastGIMainLight = true;
    return changed;
}

void VansGraphics::VansRayTracing::PrepareGIProbeUpdate(VansLightManager* lightManager, VansMaterialManager* materialManager)
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
            const GIProbeUpdateBatch resetBatch = BuildGIProbeUpdateBatch(region.resolved, 0u);
            region.giUpdateFrameIndex = 0u;
            region.lightingResetFramesRemaining =
                static_cast<uint32_t>(std::max<uint64_t>(resetBatch.fullUpdateCycleFrameCount, 1u));
        }
    }

    for (GIRegionRuntime& region : m_GIRegions)
    {
        region.constants.frameParams.x = static_cast<float>(region.giUpdateFrameIndex);
		region.constants.lightingParams.x = m_CurrentGIEnvironmentIntensity;
    }
}

void VansGraphics::VansRayTracing::UpdateGIProbe(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansLightManager* lightManager, VansMaterialManager* materialManager)
{
    (void)lightManager;
    if (!m_RTResourcesReady || m_GIRegions.empty())
        return;

    commandBuffer->EnsureComputeShader(
        *m_RayTracingPointLighting,
        { m_Scene->GetGlobalDescriptorSetLayout(), m_GISamplePositionLightSetLayout });
	const VkCommandBuffer nativeCommandBuffer = commandBuffer->GetVKCommandBuffer();
	const Vans::VansGpuQueueLane queueLane = device != nullptr && device->IsAsyncComputeEnabled()
		? Vans::VansGpuQueueLane::Compute
		: Vans::VansGpuQueueLane::Graphics;

    for (uint32_t regionIndex = 0; regionIndex < m_GIRegions.size(); ++regionIndex)
    {
        GIRegionRuntime& region = m_GIRegions[regionIndex];
        BindGIPointLightData(regionIndex);

        const uint32_t updateFrameIndex = region.giUpdateFrameIndex;
        region.constants.frameParams.x = static_cast<float>(updateFrameIndex);
		region.constants.lightingParams.x = m_CurrentGIEnvironmentIntensity;
        const bool lightingResetActive = region.lightingResetFramesRemaining > 0u;
        const glm::vec4 baseTemporalParams = region.constants.temporalParams;
        if (lightingResetActive)
        {
            // Negative irradiance hysteresis is an explicit shader-side reset
            // signal.  A zero value still means "blend this sparse direction
            // slice by 1 / directionSlices" once probe history is valid, which
            // leaves old lighting in the atlas for too long after light edits.
            region.constants.temporalParams.x = -1.0f;
        }

        const GIProbeUpdateBatch batch = BuildGIProbeUpdateBatch(
            region.resolved,
            updateFrameIndex);
        const glm::uvec3 groupCount(
            CeilDivide(batch.activeGridDimensions.x, 4u),
            CeilDivide(batch.activeGridDimensions.y, 4u),
            CeilDivide(batch.activeGridDimensions.z, 4u));

		{
			VANS_GPU_SCOPE_LANE(nativeCommandBuffer, "DDGI.RadianceShade", queueLane);
			commandBuffer->DispatchCompute(
				*m_RayTracingPointLighting,
				groupCount.x,
				groupCount.y,
				groupCount.z,
				{ m_Scene->GetGlobalDescriptorSet(), m_GISamplePositionLightDescriptorSets[regionIndex] },
				&region.constants,
				sizeof(region.constants));
		}

        // Shade writes the active-ray radiance first.  The atlas pass then
        // integrates exactly that same transient batch into irradiance and
        // distance moments; it must not observe a previous frame's buffer.
        VkMemoryBarrier shadeToAtlasBarrier{};
        shadeToAtlasBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        shadeToAtlasBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        shadeToAtlasBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        commandBuffer->PipelineBarrier(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            { shadeToAtlasBarrier });

        if (m_GIVisibilityUpdateShader != nullptr)
        {
            BindGIVisibilityData(materialManager, regionIndex);
            commandBuffer->EnsureComputeShader(*m_GIVisibilityUpdateShader, { m_GIVisibilityUpdateSetLayout });
			{
				VANS_GPU_SCOPE_LANE(nativeCommandBuffer, "DDGI.AtlasPrefilter", queueLane);
				commandBuffer->DispatchCompute(
					*m_GIVisibilityUpdateShader,
					batch.activeGridDimensions.x,
					batch.activeGridDimensions.y,
					batch.activeGridDimensions.z,
					{ m_GIVisibilityUpdateDescriptorSets[regionIndex] },
					&region.constants,
					sizeof(region.constants));
			}

            // The next scheduled batch shades hits by sampling the previous
            // DDGI atlas.  This explicit dependency is required even when a
            // probe is updated only once per spatial phase; otherwise a
            // compute read may observe a partially written irradiance or
            // distance tile and manifests as grid-aligned light patches.
            VkMemoryBarrier atlasToNextBatchBarrier{};
            atlasToNextBatchBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            atlasToNextBatchBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            atlasToNextBatchBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            commandBuffer->PipelineBarrier(
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                { atlasToNextBatchBarrier });
        }

        if (m_GIProbeStateShader != nullptr)
        {
            BindGIProbeStateData(regionIndex);
            commandBuffer->EnsureComputeShader(*m_GIProbeStateShader, { m_GIProbeStateSetLayout });
			{
				VANS_GPU_SCOPE_LANE(nativeCommandBuffer, "DDGI.ProbeState", queueLane);
				commandBuffer->DispatchCompute(
					*m_GIProbeStateShader,
					batch.activeGridDimensions.x,
					batch.activeGridDimensions.y,
					batch.activeGridDimensions.z,
					{ m_GIProbeStateDescriptorSets[regionIndex] },
					&region.constants,
					sizeof(region.constants));
			}

            // Relocation and classification are consumed by the following
            // ray-tracing dispatch.  Do not rely on submission ordering for a
            // shader-write -> ray-tracing-read hazard.
            VkMemoryBarrier stateToTraceBarrier{};
            stateToTraceBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            stateToTraceBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            stateToTraceBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
            commandBuffer->PipelineBarrier(
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
                { stateToTraceBarrier });
        }

        if (lightingResetActive)
        {
            --region.lightingResetFramesRemaining;
            region.constants.temporalParams = baseTemporalParams;
        }
        ++region.giUpdateFrameIndex;

    }

	DispatchGIRTPreview(commandBuffer, materialManager);

    // GI atlases are storage-image outputs during the probe update, then
    // sampled by SSGI and Deferred later in the frame.  The intra-update
    // barriers above only protect compute consumers inside the GI scheduler;
    // publish the final atlas contents to downstream screen-space and
    // fragment passes before the render graph samples them.
    VkMemoryBarrier atlasPublishBarrier{};
    atlasPublishBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    atlasPublishBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    atlasPublishBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    commandBuffer->PipelineBarrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        { atlasPublishBarrier });
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
	if (region.visibilityAtlas == nullptr || region.irradianceAtlas == nullptr ||
		manager == nullptr || manager->m_EnvironmentRadiance == nullptr)
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
			GIPL_BINDING_EMISSION,
			VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{
				region.hitEmissionResult.GetNativeBuffer(),
				0,
				region.hitEmissionResult.GetBufferSize()
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

	auto& skyImage = manager->m_EnvironmentRadiance->GetImage();
	// Probe miss shading needs raw environment radiance.  The diffuse
	// preconvolution is already an irradiance integral and must not be injected
	// into the Monte-Carlo ray integral a second time.
    descManager->WriteImageDescriptor(
        descriptorSet,
        GIPL_BINDING_ENVIRONMENT_MAP,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            skyImage.GetSampler(),
            skyImage.GetImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
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
            VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
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
        GIPL_BINDING_IRRADIANCE_ATLAS,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            region.irradianceAtlas->GetImage().GetSampler(),
            region.irradianceAtlas->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->WriteBufferDescriptor(
        descriptorSet,
        GIPL_BINDING_PROBE_STATE,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.probeStateBuffer.GetNativeBuffer(),
            0,
            region.probeStateBuffer.GetBufferSize()
        }});
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

    if (materialManager == nullptr || region.visibilityAtlas == nullptr ||
		region.irradianceAtlas == nullptr || region.screenIrradianceAtlas == nullptr)
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
    descManager->WriteBufferDescriptor(
        m_GIVisibilityUpdateDescriptorSets[regionIndex],
        GI_VISIBILITY_BINDING_RADIANCE,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitRadianceBuffer.GetNativeBuffer(),
            0,
            region.hitRadianceBuffer.GetBufferSize()
        }});
    descManager->WriteImageDescriptor(
        m_GIVisibilityUpdateDescriptorSets[regionIndex],
        GI_VISIBILITY_BINDING_IRRADIANCE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{
            region.irradianceAtlas->GetImage().GetSampler(),
            region.irradianceAtlas->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->WriteBufferDescriptor(
        m_GIVisibilityUpdateDescriptorSets[regionIndex],
        GI_VISIBILITY_BINDING_PROBE_STATE,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ region.probeStateBuffer.GetNativeBuffer(), 0, region.probeStateBuffer.GetBufferSize() }});
	descManager->WriteImageDescriptor(
		m_GIVisibilityUpdateDescriptorSets[regionIndex],
		GI_VISIBILITY_BINDING_SCREEN_IRRADIANCE,
		VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
		{{
			region.screenIrradianceAtlas->GetImage().GetSampler(),
			region.screenIrradianceAtlas->GetImage().GetImageView(),
			VK_IMAGE_LAYOUT_GENERAL
		}});
    descManager->CommitDescriptorUpdates();
    region.giVisibilityDescriptorSetIsDirty = false;
}

void VansGraphics::VansRayTracing::BindGIProbeStateData(uint32_t regionIndex)
{
    if (regionIndex >= m_GIRegions.size() || regionIndex >= m_GIProbeStateDescriptorSets.size())
        return;
    GIRegionRuntime& region = m_GIRegions[regionIndex];
    if (!region.giProbeStateDescriptorSetIsDirty)
        return;

    auto* descManager = VansVKDescriptorManager::GetInstance();
    const VkDescriptorSet descriptorSet = m_GIProbeStateDescriptorSets[regionIndex];
    descManager->BeginDescriptorUpdate();
    const auto writeBuffer = [descManager, descriptorSet](uint32_t binding, VansVKBuffer& buffer)
    {
        descManager->WriteBufferDescriptor(descriptorSet, binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {{ buffer.GetNativeBuffer(), 0, buffer.GetBufferSize() }});
    };
    writeBuffer(0u, region.hitPositionResult);
    writeBuffer(1u, region.hitNormalResult);
    writeBuffer(2u, region.probeStateBuffer);
    descManager->CommitDescriptorUpdates();
    region.giProbeStateDescriptorSetIsDirty = false;
}

void VansGraphics::VansRayTracing::BindGIRTPreviewData(VansMaterialManager* materialManager)
{
    GIRegionRuntime* previewRegion = GetPreviewRegion();
    if (!m_GIRTPreviewDescriptorSetIsDirty || materialManager == nullptr ||
        m_GIRTPreviewTextures[0] == nullptr || previewRegion == nullptr ||
        previewRegion->rayTracingResult == nullptr)
    {
        return;
    }

	auto* irradianceAtlas = previewRegion->irradianceAtlas;
	auto* visibilityAtlas = previewRegion->visibilityAtlas;
	if (irradianceAtlas == nullptr || visibilityAtlas == nullptr || m_GIRTPreviewDescriptorSets.empty())
        return;

    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();
    const glm::uvec3& grid = previewRegion->resolved.gridDimensions;
    const uint32_t zSlice = std::min(
        static_cast<uint32_t>(m_GIRTPreviewConstants[0].selectionParams.y),
        std::max(grid.z, 1u) - 1u);
    for (uint32_t mode = 0u; mode < GIRTPreviewModeCount; ++mode)
    {
        m_GIRTPreviewConstants[mode].displayParams.y = 0.0f;
        const VkDescriptorSet descriptorSet = m_GIRTPreviewDescriptorSets[mode];

	auto writeWholeBuffer = [&](uint32_t binding, VansVKBuffer& buffer)
	{
		descManager->WriteBufferDescriptor(
			descriptorSet, binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
			{{ buffer.GetNativeBuffer(), 0, buffer.GetBufferSize() }});
	};
	auto writeStorageImage = [&](uint32_t binding, VansTexture* texture)
	{
		descManager->WriteImageDescriptor(
			descriptorSet, binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
			{{ texture->GetImage().GetSampler(), texture->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	};
	auto writeSampledImage = [&](uint32_t binding, VansTexture* texture)
	{
		descManager->WriteImageDescriptor(
			descriptorSet, binding, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
			{{ texture->GetImage().GetSampler(), texture->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
	};

    writeWholeBuffer(GI_RT_PREVIEW_BINDING_HIT_POSITION, previewRegion->hitPositionResult);
    writeWholeBuffer(GI_RT_PREVIEW_BINDING_HIT_NORMAL, previewRegion->hitNormalResult);
    writeWholeBuffer(GI_RT_PREVIEW_BINDING_HIT_PBR, previewRegion->hitAlbedoRoughnessResult);
    writeWholeBuffer(GI_RT_PREVIEW_BINDING_DIRECT_LIGHT, previewRegion->hitRadianceBuffer);
	writeStorageImage(GI_RT_PREVIEW_BINDING_RAY_SUMMARY, previewRegion->rayTracingResult);
	writeSampledImage(GI_RT_PREVIEW_BINDING_SH_R, irradianceAtlas);
	writeSampledImage(GI_RT_PREVIEW_BINDING_SH_G, visibilityAtlas);
	writeWholeBuffer(GI_RT_PREVIEW_BINDING_SH_B, previewRegion->probeStateBuffer);
	writeStorageImage(GI_RT_PREVIEW_BINDING_OUTPUT, m_GIRTPreviewTextures[mode]);
    }
    descManager->CommitDescriptorUpdates();
    m_GIRTPreviewDescriptorSetIsDirty = false;
	m_GIRTPreviewBoundZSlice = zSlice;
}

void VansGraphics::VansRayTracing::DispatchGIRTPreview(
    VansVKCommandBuffer* commandBuffer,
    VansMaterialManager* materialManager)
{
    if (m_GIRTPreviewRequestFrames == 0 || commandBuffer == nullptr ||
        m_GIRTPreviewShader == nullptr || m_GIRTPreviewTextures[0] == nullptr)
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

    for (uint32_t mode = 0u; mode < GIRTPreviewModeCount; ++mode)
    {
        commandBuffer->DispatchCompute(
            *m_GIRTPreviewShader,
            (previewRegion->resolved.gridDimensions.x + 7u) / 8u,
            (previewRegion->resolved.gridDimensions.y + 7u) / 8u,
            1u,
            { m_GIRTPreviewDescriptorSets[mode] },
            &m_GIRTPreviewConstants[mode], sizeof(GIRTPreviewPushConstant));
    }

    VkMemoryBarrier previewBarrier{};
    previewBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    previewBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    previewBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    commandBuffer->PipelineBarrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
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

void VansGraphics::VansRayTracing::CreateGIProbeStateDescriptorSets(VansVKDevice* device)
{
    VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(
        {
            { 0u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        },
        m_GIProbeStateSetLayout,
        m_GIProbeStateDescriptorSets,
        static_cast<uint32_t>(std::max<size_t>(m_GIRegions.size(), 1u)));
    for (GIRegionRuntime& region : m_GIRegions)
        region.giProbeStateDescriptorSetIsDirty = true;
}

void VansGraphics::VansRayTracing::CreateGIRTPreviewDescriptorSets(VansVKDevice* device)
{
    VansDescriptorSetLayoutFactory::CreateAndAllocate_GIRTPreview(
        m_GIRTPreviewSetLayout,
        m_GIRTPreviewDescriptorSets,
        GIRTPreviewModeCount);
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
        const GIProbeUpdateBatch batch = BuildGIProbeUpdateBatch(
            region.resolved,
            region.giUpdateFrameIndex);
        if (batch.activeProbeCount == 0 || batch.raysPerActiveProbe == 0)
            continue;
        BindRayTracingData(device, scene, regionIndex);

        commandBuffer->BindRayTracingDescriptorSets(
            *vansPipeline,
            0,
            { m_RayTracingDescriptorSets[regionIndex] });
        commandBuffer->UpdateRayTracingPushConstants(
            *vansPipeline,
            VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
                VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR,
            0,
            m_VansRayTracingShader->GetPushConstantSize(),
            &region.constants);

        commandBuffer->TraceRays(
            *vansPipeline,
            batch.activeGridDimensions.x * batch.raysPerActiveProbe,
            batch.activeGridDimensions.y,
            batch.activeGridDimensions.z);

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

    VkDescriptorSetLayoutBinding probeStateDataBuffer =
    {
        9u,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR,
        nullptr
    };

	VkDescriptorSetLayoutBinding instanceGIEmissionDataBuffer =
	{
		10u,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		1,
		VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
		nullptr
	};

	VkDescriptorSetLayoutBinding hitEmissionDataBuffer =
	{
		11u,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		1,
		VK_SHADER_STAGE_RAYGEN_BIT_KHR,
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
            hitPBRAlbedoRoughnessDataBuffer,
			probeStateDataBuffer,
			instanceGIEmissionDataBuffer,
			hitEmissionDataBuffer
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
    descManager->WriteBufferDescriptor(
        m_RayTracingDescriptorSets[regionIndex],
        9u,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.probeStateBuffer.GetNativeBuffer(),
            0,
            region.probeStateBuffer.GetBufferSize()
        }});
	descManager->WriteBufferDescriptor(
		m_RayTracingDescriptorSets[regionIndex],
		10u,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{
			m_TLASInstanceGIEmissionBuffer.GetNativeBuffer(),
			0,
			m_TLASInstanceGIEmissionBuffer.GetBufferSize()
		}});
	descManager->WriteBufferDescriptor(
		m_RayTracingDescriptorSets[regionIndex],
		11u,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{
			region.hitEmissionResult.GetNativeBuffer(),
			0,
			region.hitEmissionResult.GetBufferSize()
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

