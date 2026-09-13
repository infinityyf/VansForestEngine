#include "VansRayTracing.h"
#include "../../RenderCore/VulkanCore/VansMesh.h"
#include "../GICore/VansGIProbeLayout.h"
#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansVKCommandBuffer.h"
#include "../../RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../../RenderCore/VulkanCore/VansDescriptorSetLayouts.h"
#include "../../RenderCore/VulkanCore/VansRenderPass.h"
#include "../../RenderCore/BRDFData/VansLight.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore/VansRenderNode.h"
#include "../../Configration/VansConfigration.h"
#include "../../RenderCore//VansMaterial.h"
#include "../../RenderCore/VansShaderManager.h"
#include "../../Util/VansLog.h"
#include "../../Util/VansProfiler.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <cstring>
#include <cstdlib>

namespace
{
    constexpr int kGIVisibilityOctaRes = 16;
    constexpr int kGIIrradianceOctaRes = 8;

    VansGraphics::RayTracingPushConstant BuildGIRegionPushConstant(
        const VansGraphics::GIResolvedRegion& region,
        float maxIndirectRadiance,
        float maxProbeRadiance,
        float irradianceHysteresis,
        float distanceHysteresis,
        float distanceSharpness)
    {
        VansGraphics::RayTracingPushConstant constants{};
        constants.dispatchParams = glm::vec4(
            glm::vec3(region.gridDimensions),
            static_cast<float>(region.raysPerProbe));
        constants.gridParams = glm::vec4(glm::vec3(region.probeSpacing), region.maxRayDistance);
        constants.frameParams = glm::vec4(
            0.0f,
            0.0f,
            0.0f,
            0.0f);
        constants.regionParams = glm::vec4(region.center, region.normalBias);
        constants.lightingParams = glm::vec4(
            std::max(maxIndirectRadiance, 0.0f),
            std::max(maxProbeRadiance, 0.0f),
            0.0f, 0.0f);
        constants.temporalParams = glm::vec4(
            std::clamp(irradianceHysteresis, 0.0f, 0.999f),
            std::clamp(distanceHysteresis, 0.0f, 0.999f),
            std::clamp(distanceSharpness, 8.0f, 16.0f),
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

    uint64_t BuildGILightSignature(
		const VansGraphics::VansRenderLightFrameData& lightFrame)
    {
        uint64_t signature = 1469598103934665603ull;
		const auto& directionLights = lightFrame.directionalLights;
		const auto& pointLights = lightFrame.pointLights;
		const auto& spotLights = lightFrame.spotLights;
		const auto& rectLights = lightFrame.rectLights;

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

namespace
{
    void RequireGIResource(bool success, const char* operation)
    {
        if (!success) throw std::runtime_error(operation);
    }
}

void VansGraphics::VansRayTracing::CleanupSceneResources(VkDevice device)
{
	std::atomic_store(&m_PublishedGIProbeLayout, std::shared_ptr<const GIProbeLayoutSnapshot>{});
	ReleaseSceneResources(device, true);
	m_ResourceError.clear();
}

void VansGraphics::VansRayTracing::ReleaseSceneResources(VkDevice device, bool releaseSharedPipeline)
{

	auto descMgr = VansVKDescriptorManager::GetInstance();

	// 释放 RT descriptor set 和 layout
	descMgr->DestroyDescriptorSet(m_State->m_RayTracingDescriptorSets);
	descMgr->ReleaseDescriptorSetLayout(m_State->m_RayTracingSetLayout);

	descMgr->DestroyDescriptorSet(m_State->m_GISamplePositionLightDescriptorSets);
	descMgr->ReleaseDescriptorSetLayout(m_State->m_GISamplePositionLightSetLayout);

	descMgr->DestroyDescriptorSet(m_State->m_GIVisibilityUpdateDescriptorSets);
	descMgr->ReleaseDescriptorSetLayout(m_State->m_GIVisibilityUpdateSetLayout);
	descMgr->DestroyDescriptorSet(m_State->m_GIProbeStateDescriptorSets);
	descMgr->ReleaseDescriptorSetLayout(m_State->m_GIProbeStateSetLayout);

	descMgr->DestroyDescriptorSet(m_State->m_GIRTPreviewDescriptorSets);
	descMgr->ReleaseDescriptorSetLayout(m_State->m_GIRTPreviewSetLayout);

	// 释放 RT 相关 buffer
	m_State->m_ReceiverGeometryData.DestroyVulkanBuffer(device);
	m_State->m_BLASInstanceBuffer.DestroyVulkanBuffer(device);
	m_State->m_TLASInstanceMaterialBuffer.DestroyVulkanBuffer(device);
	m_State->m_TLASInstanceGIEmissionBuffer.DestroyVulkanBuffer(device);

	for (GIRegionRuntime& region : m_State->m_GIRegions)
		DestroyRegionRuntime(device, region);
	m_State->m_GIRegions.clear();
	m_State->m_WorkScheduler = {};
	m_State->m_AutomaticGIWork = false;
	for (VansTexture*& previewTexture : m_State->m_GIRTPreviewTextures)
		DeleteTexture(previewTexture);

	m_State->m_RayTracingPointLighting = nullptr;

	m_State->m_GIVisibilityUpdateShader = nullptr;
	m_State->m_GIProbeStateShader = nullptr;

	m_State->m_GIRTPreviewShader = nullptr;

	// 重置 RT 着色器的 pipeline / SBT，下次 CreateRayTracingResource 将重建
	if (releaseSharedPipeline && m_State->m_VansRayTracingShader)
		m_State->m_VansRayTracingShader->TriggerReCreateRayTracingPipeline();
	m_State->m_VansRayTracingShader = nullptr;
    if (releaseSharedPipeline && m_State->m_ReceiverVisibilityShader)
        m_State->m_ReceiverVisibilityShader->TriggerReCreateRayTracingPipeline();
    m_State->m_ReceiverVisibilityShader = nullptr;
    if (releaseSharedPipeline && m_State->m_ReceiverBiasShader)
        m_State->m_ReceiverBiasShader->TriggerReCreateRayTracingPipeline();
    m_State->m_ReceiverBiasShader = nullptr;


	// 标记脏以便下次 CreateRayTracingResource 重新绑定
	m_State->m_GIRTPreviewDescriptorSetIsDirty = true;
	m_State->m_GIRTPreviewRequestFrames = 0;
	m_State->m_GIRTPreviewBoundZSlice = 0xffffffffu;
	m_State->m_ProbeLayoutBuffer.DestroyVulkanBuffer(device);
	m_State->m_ProbeLayout = {};
	m_State->m_PendingLayoutRegionData.clear();
	m_State->m_RTResourcesReady = false;

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
    region.previousProbeStateBuffer.DestroyVulkanBuffer(device);
	region.workBuffer.DestroyVulkanBuffer(device);
    region.feedbackBuffer.DestroyVulkanBuffer(device);
    region.feedbackReadback.DestroyVulkanBuffer(device);
    region.stateAuditReadback.DestroyVulkanBuffer(device);
    m_State->m_GIFeedbackFence = VK_NULL_HANDLE;
	DeleteTexture(region.rayTracingResult);
	DeleteTexture(region.irradianceAtlas);
	DeleteTexture(region.visibilityAtlas);
	region = GIRegionRuntime{};
}

VansGraphics::VansTexture* VansGraphics::VansRayTracing::GetGIRegionIrradianceAtlas(uint32_t regionIndex) const
{
	return regionIndex < m_State->m_GIRegions.size() ? m_State->m_GIRegions[regionIndex].irradianceAtlas : nullptr;
}



VansGraphics::VansTexture* VansGraphics::VansRayTracing::GetGIRegionVisibilityAtlas(uint32_t regionIndex) const
{
	return regionIndex < m_State->m_GIRegions.size() ? m_State->m_GIRegions[regionIndex].visibilityAtlas : nullptr;
}

const VansGraphics::VansVKBuffer* VansGraphics::VansRayTracing::GetGIRegionProbeStateBuffer(uint32_t regionIndex) const
{
	return regionIndex < m_State->m_GIRegions.size() ? &m_State->m_GIRegions[regionIndex].probeStateBuffer : nullptr;
}

VansGraphics::VansRayTracing::GIRegionRuntime* VansGraphics::VansRayTracing::GetPreviewRegion()
{
	return m_State->m_GIRegions.empty() ? nullptr : &m_State->m_GIRegions[0];
}

const VansGraphics::VansRayTracing::GIRegionRuntime* VansGraphics::VansRayTracing::GetPreviewRegion() const
{
	return m_State->m_GIRegions.empty() ? nullptr : &m_State->m_GIRegions[0];
}

bool VansGraphics::VansRayTracing::CreateRayTracingResource(VansVKDevice* device,
    VansVKCommandBuffer* commandBuffer, VansScene* scene, const VansGISettings& settings)
{
    if (!device || !commandBuffer || !scene)
    {
        m_ResourceError = "GI resource creation requires a device, command buffer and scene";
        return false;
    }
    try
    {
        const VkDevice logicalDevice = device->GetLogicDevice();
        const bool retainPipeline = m_State->m_RTResourcesReady;
        auto pending = std::shared_ptr<VansRayTracing>(new VansRayTracing(),
            [logicalDevice, retainPipeline](VansRayTracing* resources)
            {
                resources->ReleaseSceneResources(logicalDevice, !retainPipeline);
                delete resources;
            });
        pending->InitializeSceneResources(device, commandBuffer, scene, settings);
        std::shared_ptr<const GIProbeLayoutSnapshot> publishedLayout;
        if (pending->IsReady() && pending->UsesSparseGI())
            publishedLayout = pending->m_State->m_ProbeLayout.CapturePositionSnapshot();
        // 先登记退役记录，再无异常地发布；旧预览可能仍被当前 GUI 帧引用。
        device->EnqueueDeferredDelete([pending]() {});
        m_State.swap(pending->m_State);
        std::atomic_store(&m_PublishedGIProbeLayout, std::move(publishedLayout));
        m_ResourceError.clear();
        return true;
    }
    catch (const std::exception& error)
    {
        m_ResourceError = error.what();
        VANS_LOG_ERROR("[GI] Resource preparation failed; previous resources retained: " << m_ResourceError);
        return false;
    }
}

void VansGraphics::VansRayTracing::InitializeSceneResources(VansVKDevice* device,
    VansVKCommandBuffer* commandBuffer, VansScene* scene, const VansGISettings& settings)
{
    m_State->settings = settings;
    NormalizeGISettings(m_State->settings);
    int blasMeshCount = scene->GetBLASVertexBuffers().size();
    std::vector<uint32_t>& instanceData = scene->GetTLASInstanceData();
	const auto& instanceMaterials = scene->GetTLASInstanceMaterials();
	std::vector<glm::vec4>& instanceGIEmission = scene->GetTLASInstanceGIEmission();

    // No RT geometry in the scene – nothing to set up.
    if (blasMeshCount == 0 || instanceData.empty())
    {
        VANS_LOG_WARN("[CreateRayTracingResource] No ray-tracing geometry found, skipping RT resource creation.");
        m_State->m_RTResourcesReady = false;
        return;
    }

    m_State->m_RTResourcesReady = false;

    const VansGISettings& gi = m_State->settings;

    const std::vector<const GIProbeRegionDesc*> activeRegions = BuildActiveGIRegionOrder(gi);

    if (activeRegions.empty())
    {
        VANS_LOG_WARN("[CreateRayTracingResource] No enabled GI probe region found, skipping RT GI resource creation.");
        m_State->m_RTResourcesReady = false;
        return;
    }

    m_State->m_GIRegions.clear();
    m_State->m_GIRegions.reserve(activeRegions.size());
    m_State->m_AutomaticGIWork = gi.placement.enabled;
    const char* audit = std::getenv("FORESTENGINE_GI_STATE_AUDIT");
    m_State->m_GIStateAuditEnabled = audit && std::strcmp(audit, "1") == 0;
    m_State->m_MaxComputeGroupsX = device->GetDeviceProperties().limits.maxComputeWorkGroupCount[0];
    m_State->m_WorkScheduler = {};
    m_State->m_ProbeLayout = {};
    if (activeRegions.size() > VANS_SSGI_MAX_GI_REGIONS)
        throw std::runtime_error("GI authoring region count exceeds the shared descriptor capacity");
    std::vector<GIResolvedRegion> resolvedRegions;
    for (const auto* description : activeRegions) resolvedRegions.push_back(ResolveGIRegion(*description));
    if (m_State->m_AutomaticGIWork)
    {
        VansSceneGeometrySnapshot geometry;
        std::string error;
        if (!VansSceneGeometrySnapshot::Capture(*scene, *device, geometry, error))
            throw std::runtime_error("GI geometry capture failed: " + error);
        if (!m_State->m_ProbeLayout.Build(resolvedRegions, gi.placement, geometry, error))
            throw std::runtime_error("GI sparse layout failed: " + error);
        const auto& stats = m_State->m_ProbeLayout.Stats();
        VANS_LOG("[GILayout] leaves=" << m_State->m_ProbeLayout.Leaves().size() << " nodes=" << m_State->m_ProbeLayout.Nodes().size()
            << " spacing=" << stats.finestSpacing << ".." << stats.coarsestSpacing
            << " constraints=" << stats.constrainedPositions << " budgetLimited=" << stats.budgetLimitedSplits
            << " parentCells=" << stats.parentCells << " parentProbes=" << stats.parentPositions
            << " parentMaxSize=" << gi.placement.parentProbeMaxSize << " coverageSplits=" << stats.coverageSplits);
    }
    {
        std::string error;
        std::vector<GIProbeWorkRegion> workRegions;
        for (uint32_t index = 0; index < resolvedRegions.size(); ++index)
        {
            const auto& resolved = resolvedRegions[index];
            GIProbeWorkRegion work;
            work.raysPerProbe = resolved.raysPerProbe;
            const uint32_t count = m_State->m_AutomaticGIWork ? m_State->m_ProbeLayout.Regions()[index].metadata.w : uint32_t(resolved.probeCount);
            work.probeIndices.reserve(count);
            for (uint32_t probe = 0; probe < count; ++probe) work.probeIndices.push_back(probe);
            VANS_LOG("[GILayout] Region '" << resolved.name << "' regular=" << resolved.probeCount
                << " physical=" << count);
            workRegions.push_back(std::move(work));
        }
        if (!m_State->m_WorkScheduler.Configure(std::move(workRegions), gi.placement.maxProbeUpdatesPerFrame,
            gi.placement.maxRaysPerFrame, error)) throw std::runtime_error(error);
    }
    const auto layoutData = BuildGIProbeLayoutGPUData(resolvedRegions, m_State->m_AutomaticGIWork ? &m_State->m_ProbeLayout : nullptr);
    const VkDeviceSize layoutBytes = layoutData.size() * sizeof(glm::uvec4);
    if (layoutBytes > device->GetDeviceProperties().limits.maxStorageBufferRange ||
        !m_State->m_ProbeLayoutBuffer.CreatVulkanBuffer(device->GetLogicDevice(), layoutBytes, VK_FORMAT_R32G32B32A32_UINT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        throw std::runtime_error("Failed to publish GI layout buffer");

    auto& shaderManager = VansShaderManager::Get();
    m_State->m_VansRayTracingShader = shaderManager.FindRayTracingShader("RayTracingTest");
    if (!m_State->m_VansRayTracingShader)
    {
        throw std::runtime_error("[CreateRayTracingResource] Managed RayTracingTest shader is unavailable");
    }

    for (const GIProbeRegionDesc* regionDesc : activeRegions)
    {
        m_State->m_GIRegions.emplace_back();
        GIRegionRuntime& regionRuntime = m_State->m_GIRegions.back();
        regionRuntime.resolved = ResolveGIRegion(*regionDesc);
        regionRuntime.constants = BuildGIRegionPushConstant(
            regionRuntime.resolved,
            gi.maxIndirectRadiance,
            gi.maxProbeRadiance,
            gi.irradianceHysteresis,
            gi.distanceHysteresis,
            gi.distanceSharpness);

        regionRuntime.physicalProbeCount = m_State->m_AutomaticGIWork
            ? m_State->m_ProbeLayout.Regions()[m_State->m_GIRegions.size() - 1u].metadata.w : uint32_t(regionRuntime.resolved.probeCount);
        regionRuntime.storageDimensions = regionRuntime.resolved.gridDimensions;
        if (m_State->m_AutomaticGIWork)
        {
            // 三维图像只负责预览和稳定射线散列，真实世界位置从只读布局表取得。
            const uint32_t side = std::max(1u, uint32_t(std::ceil(std::cbrt(double(regionRuntime.physicalProbeCount)))));
            regionRuntime.storageDimensions = {side, side,
                std::max(1u, CeilDivide(regionRuntime.physicalProbeCount, side * side))};
            regionRuntime.constants.dispatchParams = glm::vec4(glm::vec3(regionRuntime.storageDimensions),
                float(regionRuntime.resolved.raysPerProbe));
        }
        const glm::uvec3& grid = regionRuntime.storageDimensions;
        const uint64_t probeCount64 = regionRuntime.physicalProbeCount;
        // 临时缓存只容纳本帧预算；每个入选 probe 总是完整追踪。
        const uint32_t workCapacity = m_State->m_WorkScheduler.RegionCapacity(m_State->m_GIRegions.size() - 1u);
        const uint64_t rayCacheEntries64 = std::max<uint64_t>(
            m_State->m_WorkScheduler.RegionRayCapacity(m_State->m_GIRegions.size() - 1u), 1u);
        if (!regionRuntime.workBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
            sizeof(glm::uvec4) + uint64_t(std::max(workCapacity, 1u)) * sizeof(GIProbeWorkEntry), VK_FORMAT_R32_UINT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            throw std::runtime_error("Failed to allocate GI probe work buffer");
        const uint64_t feedbackBytes = uint64_t(std::max(workCapacity, 1u)) * sizeof(GIProbeFeedback);
        if (!regionRuntime.feedbackBuffer.CreatVulkanBuffer(device->GetLogicDevice(), feedbackBytes, VK_FORMAT_R32_UINT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            throw std::runtime_error("Failed to allocate GI completion feedback");
        if ((!regionRuntime.feedbackReadback.CreatVulkanBuffer(device->GetLogicDevice(),
            feedbackBytes, VK_FORMAT_R32_UINT, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) || !regionRuntime.feedbackReadback.PersistentMap()))
            throw std::runtime_error("Failed to allocate GI completion readback");
        regionRuntime.work.raysPerProbeUpdate = regionRuntime.resolved.raysPerProbe;

        VANS_LOG("[CreateRayTracingResource] GI region '" << regionRuntime.resolved.name
            << "' grid=" << grid.x << "x" << grid.y << "x" << grid.z
            << " spacing=" << regionRuntime.resolved.probeSpacing
            << " probes=" << probeCount64
            << " activeRayEntries=" << rayCacheEntries64
            << " completeRaysPerProbe=" << regionRuntime.resolved.raysPerProbe);

        regionRuntime.rayTracingResult = new VansTexture();
        RequireGIResource(regionRuntime.rayTracingResult->InitTextureWithoutData(
            *commandBuffer,
            grid.x,
            grid.y,
            grid.z,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            false,
            false,
            true), "Failed to initialize GI texture");

        const int totalProbeCount = static_cast<int>(std::max<uint64_t>(probeCount64, 1u));
        const int probesPerAtlasRow = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(totalProbeCount)))));
        const int probesPerAtlasColumn = (totalProbeCount + probesPerAtlasRow - 1) / probesPerAtlasRow;

        regionRuntime.irradianceAtlas = new VansTexture();
        RequireGIResource(regionRuntime.irradianceAtlas->InitTextureWithoutData(
            *commandBuffer,
            probesPerAtlasRow * kGIIrradianceOctaRes,
            probesPerAtlasColumn * kGIIrradianceOctaRes,
            1,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            false,
            false,
            true,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE), "Failed to initialize GI texture");

        regionRuntime.visibilityAtlas = new VansTexture();
        RequireGIResource(regionRuntime.visibilityAtlas->InitTextureWithoutData(
            *commandBuffer,
            probesPerAtlasRow * kGIVisibilityOctaRes,
            probesPerAtlasColumn * kGIVisibilityOctaRes,
            1,
            VK_FORMAT_R32G32_SFLOAT,
            false,
            false,
            true,
            VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE), "Failed to initialize GI texture");

        const VkDeviceSize giHitBufferSize = static_cast<VkDeviceSize>(rayCacheEntries64) * sizeof(uint16_t) * 4;

        RequireGIResource(regionRuntime.hitPositionResult.CreatVulkanBuffer(device->GetLogicDevice(),
            static_cast<VkDeviceSize>(rayCacheEntries64) * sizeof(float),
            VK_FORMAT_R32_SFLOAT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), "Failed to allocate GI buffer");
        RequireGIResource(regionRuntime.hitNormalResult.CreatVulkanBuffer(device->GetLogicDevice(),
            giHitBufferSize,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), "Failed to allocate GI buffer");
        RequireGIResource(regionRuntime.hitAlbedoRoughnessResult.CreatVulkanBuffer(device->GetLogicDevice(),
            giHitBufferSize,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), "Failed to allocate GI buffer");
		RequireGIResource(regionRuntime.hitEmissionResult.CreatVulkanBuffer(device->GetLogicDevice(),
			giHitBufferSize,
			VK_FORMAT_R16G16B16A16_SFLOAT,
			VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
			VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), "Failed to allocate GI buffer");
        RequireGIResource(regionRuntime.hitRadianceBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
            giHitBufferSize,
            VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), "Failed to allocate GI buffer");
        RequireGIResource(regionRuntime.probeStateBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
            static_cast<VkDeviceSize>(std::max<uint64_t>(probeCount64, 1u)) * 48u,
            VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), "Failed to allocate GI buffer");
        RequireGIResource(regionRuntime.previousProbeStateBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
            regionRuntime.probeStateBuffer.GetBufferSize(), VK_FORMAT_R32G32B32A32_SFLOAT,
            VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT), "Failed to allocate GI visibility origin history");
        if (m_State->m_GIStateAuditEnabled)
            RequireGIResource(regionRuntime.stateAuditReadback.CreatVulkanBuffer(device->GetLogicDevice(),
                static_cast<VkDeviceSize>(std::max<uint64_t>(probeCount64, 1u)) * 48u, VK_FORMAT_R32_UINT,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                && regionRuntime.stateAuditReadback.PersistentMap(), "Failed to allocate GI state diagnostic readback");
    }

	const GIRegionRuntime* previewRegion = GetPreviewRegion();
	if (previewRegion == nullptr)
	{
		throw std::runtime_error("[CreateRayTracingResource] No enabled GI region is available for the preview target.");
	}
	for (VansTexture*& previewTexture : m_State->m_GIRTPreviewTextures)
	{
		previewTexture = new VansTexture();
		RequireGIResource(previewTexture->InitTextureWithoutData(
			*commandBuffer,
			static_cast<int>(previewRegion->storageDimensions.x),
			static_cast<int>(previewRegion->storageDimensions.y),
			1, VK_FORMAT_R32G32B32A32_SFLOAT, false, false, true,
			VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE), "Failed to initialize GI texture");
	}
	m_State->m_GIRTPreviewStorageBufferAlignment = std::max<VkDeviceSize>(
		device->GetDeviceProperties().limits.minStorageBufferOffsetAlignment,
		1u);

    //提前生成pipeline
    CreateRayTraceDescriptorSets(device, blasMeshCount);

    //创建instance data的buffer
    RequireGIResource(m_State->m_BLASInstanceBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
        instanceData.size() * sizeof(uint32_t),
        VK_FORMAT_R32_UINT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    ), "Failed to allocate GI buffer");
    RequireGIResource(m_State->m_BLASInstanceBuffer.SetBufferData(instanceData.data(), 0, instanceData.size() * sizeof(uint32_t)), "Failed to upload GI instance data");

    // 接收点净空的透明裁剪按实际 BLAS 顶点布局读取 UV，不假定导入网格的步长。
    struct alignas(16) MeshLayout { glm::uvec4 offsets{0u, ~0u, ~0u, ~0u}; glm::uvec4 formats{0u}; };
    static_assert(sizeof(MeshLayout) == 32);
    std::vector<MeshLayout> meshLayouts(blasMeshCount);
    for (auto* node : scene->GetOpaqueRenderNodes())
    {
        auto* mesh = node ? node->m_Mesh : nullptr;
        if (!mesh || mesh->GetBLASIndex() < 0 || mesh->GetBLASIndex() >= blasMeshCount) continue;
        // 同一 mesh 的 BLAS 索引可能来自旧场景，必须核对当前绑定。
        const auto index = uint32_t(mesh->GetBLASIndex());
        if (mesh->GetBLASVertexBuffer().GetNativeBuffer() != scene->GetBLASVertexBuffers()[index].GetNativeBuffer()) continue;
        auto& layout = meshLayouts[index];
        layout.offsets.x = mesh->GetMeshVertexStride();
        layout.formats.y = mesh->GetIndexBufferParameter().IndexType == VK_INDEX_TYPE_UINT16 ? 1u : 0u;
        for (const auto& attribute : mesh->m_VertexInputAttributeDescriptions)
        {
            if (attribute.binding != 0u || attribute.location > 2u) continue;
            bool half = attribute.format == VK_FORMAT_R16G16_SFLOAT || attribute.format == VK_FORMAT_R16G16B16_SFLOAT || attribute.format == VK_FORMAT_R16G16B16A16_SFLOAT;
            bool full = attribute.format == VK_FORMAT_R32G32_SFLOAT || attribute.format == VK_FORMAT_R32G32B32_SFLOAT || attribute.format == VK_FORMAT_R32G32B32A32_SFLOAT;
            if (!half && !full) continue;
            layout.offsets[attribute.location + 1u] = attribute.offset;
            if (full) layout.formats.x |= 1u << attribute.location;
        }
    }
    RequireGIResource(m_State->m_ReceiverGeometryData.CreatVulkanBuffer(device->GetLogicDevice(),
        meshLayouts.size() * sizeof(MeshLayout), VK_FORMAT_UNDEFINED, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), "Failed to allocate receiver mesh layouts");
    RequireGIResource(m_State->m_ReceiverGeometryData.SetBufferData(meshLayouts.data(), 0,
        meshLayouts.size() * sizeof(MeshLayout)), "Failed to upload receiver mesh layouts");

    // 统一上传实例贴图身份与 Alpha Test 阈值。
    if (instanceMaterials.size() != instanceData.size())
        throw std::runtime_error("GI instance material count does not match TLAS instance count");
    RequireGIResource(m_State->m_TLASInstanceMaterialBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
        instanceMaterials.size() * sizeof(GIInstanceMaterialGPU),
        VK_FORMAT_UNDEFINED,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    ), "Failed to allocate GI buffer");
    RequireGIResource(m_State->m_TLASInstanceMaterialBuffer.SetBufferData(instanceMaterials.data(), 0, instanceMaterials.size() * sizeof(GIInstanceMaterialGPU)), "Failed to upload GI instance data");

	if (instanceGIEmission.size() != instanceData.size())
	{
		throw std::runtime_error("[CreateRayTracingResource] TLAS GI emissive payload does not match TLAS instance count.");
	}
	RequireGIResource(m_State->m_TLASInstanceGIEmissionBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
		instanceGIEmission.size() * sizeof(glm::vec4),
		VK_FORMAT_R32G32B32A32_SFLOAT,
		VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT), "Failed to allocate GI buffer");
	RequireGIResource(m_State->m_TLASInstanceGIEmissionBuffer.SetBufferData(instanceGIEmission.data(), 0,
		instanceGIEmission.size() * sizeof(glm::vec4)), "Failed to upload GI instance data");

    // 冷启动时部分 probe 尚未完成首次更新；显式清零资源并由发布状态控制读取。
    if (commandBuffer->BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
    {
        // 大布局驻留显存；复用既有初始化提交上传，不让像素查询依赖 host 内存。
        commandBuffer->UpdateBuffer(m_State->m_ProbeLayoutBuffer.GetNativeBuffer(), 0u, layoutBytes, layoutData.data());
        VkClearColorValue clearSH{};
        for (GIRegionRuntime& regionRuntime : m_State->m_GIRegions)
        {
            commandBuffer->FillBuffer(regionRuntime.hitRadianceBuffer.GetNativeBuffer(), 0, regionRuntime.hitRadianceBuffer.GetBufferSize(), 0u);
			commandBuffer->FillBuffer(regionRuntime.probeStateBuffer.GetNativeBuffer(), 0, regionRuntime.probeStateBuffer.GetBufferSize(), 0u);
			commandBuffer->ClearColorImage(regionRuntime.visibilityAtlas->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
			commandBuffer->ClearColorImage(regionRuntime.irradianceAtlas->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
            commandBuffer->ClearColorImage(regionRuntime.rayTracingResult->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
        }
		for (VansTexture* previewTexture : m_State->m_GIRTPreviewTextures)
			commandBuffer->ClearColorImage(previewTexture->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);

        VkMemoryBarrier transferToShader{};
        transferToShader.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        transferToShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        transferToShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

        commandBuffer->PipelineBarrier(
            VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR |
                VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
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
            throw std::runtime_error("Failed to initialize GI layout and lighting resources");
        }
    }
    else
    {
        throw std::runtime_error("Failed to begin GI resource initialization");
    }

    m_State->m_RayTracingPointLighting = shaderManager.FindComputeShader("GIPointLight");
    if (!m_State->m_RayTracingPointLighting)
    {
        throw std::runtime_error("[CreateRayTracingResource] Managed GIPointLight shader is unavailable");
    }


    //创建cs的set
    CreateGIPointLightDescriptorSets(device);

    m_State->m_GIVisibilityUpdateShader = shaderManager.FindComputeShader("GIVisibilityUpdate");
    if (!m_State->m_GIVisibilityUpdateShader)
    {
        throw std::runtime_error("[CreateRayTracingResource] Managed GIVisibilityUpdate shader is unavailable");
    }

    CreateGIVisibilityUpdateDescriptorSets(device);

    m_State->m_GIProbeStateShader = shaderManager.FindComputeShader("GIProbeState");
    if (!m_State->m_GIProbeStateShader)
    {
        throw std::runtime_error("[CreateRayTracingResource] Managed GIProbeState shader is unavailable");
    }
    CreateGIProbeStateDescriptorSets(device);

	m_State->m_GIRTPreviewShader = shaderManager.FindComputeShader("GIRTPreview");
	if (!m_State->m_GIRTPreviewShader)
	{
		throw std::runtime_error("[CreateRayTracingResource] One or more managed GI compute shaders are unavailable");
	}
	CreateGIRTPreviewDescriptorSets(device);

    RequireGIResource(m_State->m_VansRayTracingShader->GetRayTracingPipeline(
        device, { m_State->m_RayTracingSetLayout }) != nullptr, "Failed to create GI ray tracing pipeline");

    m_State->m_HasLastGIMainLight = false;
	m_State->m_GIRTPreviewRequestFrames = 0;
    m_State->m_RTResourcesReady = true;
}

void VansGraphics::VansRayTracing::UpdateGISettings(const VansGISettings& settings)
{
    VansGISettings gi = settings;
    NormalizeGISettings(gi);
    if (m_State->m_RTResourcesReady && !GISettingsResourceLayoutEquals(m_State->settings, gi))
        throw std::invalid_argument("GI layout changes require resource preparation");

    const std::vector<const GIProbeRegionDesc*> activeRegions = BuildActiveGIRegionOrder(gi);

    const size_t updateCount = std::min(m_State->m_GIRegions.size(), activeRegions.size());
    std::vector<GIResolvedRegion> resolved;
    std::vector<RayTracingPushConstant> constants;
    resolved.reserve(updateCount);
    constants.reserve(updateCount);
    for (size_t regionIndex = 0; regionIndex < updateCount; ++regionIndex)
    {
        const GIRegionRuntime& region = m_State->m_GIRegions[regionIndex];
        resolved.push_back(ResolveGIRegion(*activeRegions[regionIndex]));
        auto value = BuildGIRegionPushConstant(
            resolved.back(),
            gi.maxIndirectRadiance,
            gi.maxProbeRadiance,
            gi.irradianceHysteresis,
            gi.distanceHysteresis,
            gi.distanceSharpness);
        value.dispatchParams = glm::vec4(glm::vec3(region.storageDimensions), float(resolved.back().raysPerProbe));
        value.frameParams.x = region.constants.frameParams.x;
        constants.push_back(value);
    }
    // 参数变更在现有命令流中更新区域记录，不能由 host 改写在途查询缓冲。
    const auto records = BuildGIProbeLayoutGPUData(resolved, nullptr);
    std::vector<glm::uvec4> pendingRecords(records.begin() + 4, records.end());
    if (m_State->m_AutomaticGIWork)
    {
        for (size_t index = 0; index < m_State->m_GIRegions.size(); ++index)
        {
            const auto& region = m_State->m_ProbeLayout.Regions()[index];
            std::memcpy(pendingRecords.data() + index * 5u, &region, sizeof(region));
        }
    }
    // 所有可能分配的准备工作完成后再更新运行数据，保留现有光照历史。
    for (size_t index = 0; index < updateCount; ++index)
    {
        m_State->m_GIRegions[index].resolved = std::move(resolved[index]);
        m_State->m_GIRegions[index].constants = constants[index];
    }
    m_State->m_PendingLayoutRegionData = std::move(pendingRecords);
    m_State->settings = std::move(gi);
    m_ResourceError.clear();
}

void VansGraphics::VansRayTracing::RequestGIRTPreviews(
    uint32_t zSlice,
    uint32_t rayIndex,
    float exposure,
    float positionScale)
{
    const GIRegionRuntime* previewRegion = GetPreviewRegion();
    if (!m_State->m_RTResourcesReady || previewRegion == nullptr)
        return;

    const uint32_t zCount = std::max(previewRegion->storageDimensions.z, 1u);
    const uint32_t rayCount = std::max(previewRegion->resolved.raysPerProbe, 1u);
    const uint32_t raysPerActiveProbe = rayCount;
    const uint32_t safeZSlice = zSlice == std::numeric_limits<uint32_t>::max()
        ? (previewRegion->work.entries.empty() ? 0u : previewRegion->work.entries.front().probeIndex /
            (previewRegion->storageDimensions.x * previewRegion->storageDimensions.y))
        : std::min(zSlice, zCount - 1u);
    if (m_State->m_GIRTPreviewBoundZSlice != safeZSlice)
        m_State->m_GIRTPreviewDescriptorSetIsDirty = true;
    for (uint32_t mode = 0u; mode < GIRTPreviewModeCount; ++mode)
    {
        GIRTPreviewPushConstant& constants = m_State->m_GIRTPreviewConstants[mode];
        constants.gridParams = glm::vec4(
            glm::vec3(previewRegion->storageDimensions),
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
    m_State->m_GIRTPreviewRequestFrames = 3;
}

bool VansGraphics::VansRayTracing::UpdateLightingResponseState(
	const VansRenderLightFrameData& lightFrame)
{
	const uint64_t lightSignature = BuildGILightSignature(lightFrame);
    glm::vec4 directionIntensity(0.0f);
    glm::vec4 lightColor(0.0f);

	const auto& directionLights = lightFrame.directionalLights;
	if (!directionLights.empty())
	{
		const VansCelestialLightingState celestialState =
			VansLightManager::ComputeCelestialLightingState(directionLights[0]);
		glm::vec3 direction = celestialState.direction;
        if (glm::dot(direction, direction) > 1e-6f)
            direction = glm::normalize(direction);
        else
            direction = glm::vec3(0.0f, 1.0f, 0.0f);

        directionIntensity = glm::vec4(direction, celestialState.intensity);
        lightColor = glm::vec4(celestialState.color, 0.0f);
    }

	const bool hasPreviousLightingState = m_State->m_HasLastGIMainLight;
    // GI 接收的是最终静态天空亮度；角度系数或天空强度改变它时需重建光照历史。
    const bool skySourceChanged = hasPreviousLightingState &&
        lightFrame.skyLighting.radianceScale != m_State->m_LastSkyRadianceScale;
    if (skySourceChanged) m_State->m_WorkScheduler.ResetLighting();
    m_State->m_LastSkyRadianceScale = lightFrame.skyLighting.radianceScale;
	bool changed = false;
	if (hasPreviousLightingState)
	{
        const float directionDelta = glm::length(glm::vec3(directionIntensity) - glm::vec3(m_State->m_LastGIMainLightDirectionIntensity));
        const float intensityDelta = std::abs(directionIntensity.w - m_State->m_LastGIMainLightDirectionIntensity.w);
        const float colorDelta = glm::length(glm::vec3(lightColor) - glm::vec3(m_State->m_LastGIMainLightColor));
        changed = directionDelta > 0.0025f || intensityDelta > 0.01f ||
            colorDelta > 0.01f || lightSignature != m_State->m_LastGILightSignature || skySourceChanged;
    }

    m_State->m_LastGIMainLightDirectionIntensity = directionIntensity;
    m_State->m_LastGIMainLightColor = lightColor;
    m_State->m_LastGILightSignature = lightSignature;
    m_State->m_HasLastGIMainLight = true;
    return changed;
}

void VansGraphics::VansRayTracing::CompleteGIProbeUpdate(VkFence completionFence)
{
    if (completionFence == VK_NULL_HANDLE || completionFence != m_State->m_GIFeedbackFence) return;
    for (size_t index = 0; index < m_State->m_GIRegions.size(); ++index)
    {
        auto& region = m_State->m_GIRegions[index];
        if (region.work.entries.empty()) continue;
        const auto bytes = region.work.entries.size() * sizeof(GIProbeFeedback);
        region.feedbackReadback.InvalidateMappedRange(0u, bytes);
        const auto* records = static_cast<const GIProbeFeedback*>(region.feedbackReadback.GetMappedPtr());
        uint32_t rejectedFeedback = 0;
        for (size_t entry = 0; entry < region.work.entries.size(); ++entry)
            rejectedFeedback += !m_State->m_WorkScheduler.ApplyFeedback(index, region.work.entries[entry], records[entry]);
        if (rejectedFeedback)
            VANS_LOG_ERROR("[GIProbeUpdate] region=" << index << " rejected completion records=" << rejectedFeedback);
        if (region.stateAuditPending)
        {
            struct alignas(16) ProbeState { glm::vec4 targetAndConfidence, traceAndBackface; glm::uvec4 metadata; };
            static_assert(sizeof(ProbeState) == 48);
            const auto stateBytes = VkDeviceSize(region.physicalProbeCount) * sizeof(ProbeState);
            region.stateAuditReadback.InvalidateMappedRange(0u, stateBytes);
            const auto* states = static_cast<const ProbeState*>(region.stateAuditReadback.GetMappedPtr());
            uint32_t unseen = 0, updated = 0, published = 0, pending = 0, invalid = 0, oldestUpdateAge = 0;
            uint32_t parentTotal = 0, parentUpdated = 0;
            for (uint32_t probe = 0; probe < region.physicalProbeCount; ++probe)
            {
                const auto& state = states[probe];
                unseen += state.metadata.x == 0u; updated += state.metadata.x == 1u;
                if (m_State->m_AutomaticGIWork)
                {
                    const uint32_t first = m_State->m_ProbeLayout.Regions()[index].metadata.z;
                    if (m_State->m_ProbeLayout.Positions()[first + probe].metadata.z == 1u)
                    { ++parentTotal; parentUpdated += state.metadata.x == 1u; }
                }
                if (state.metadata.x == 1u) oldestUpdateAge = std::max(oldestUpdateAge, region.stateAuditFrame - state.metadata.y);
                const bool valid = state.metadata.x == 1u && state.targetAndConfidence.w > 0.0f;
                published += valid;
                pending += valid && glm::length(glm::vec3(state.targetAndConfidence) - glm::vec3(state.traceAndBackface)) > 1e-6f;
                invalid += glm::any(glm::isnan(state.targetAndConfidence)) || glm::any(glm::isinf(state.targetAndConfidence)) ||
                    glm::any(glm::isnan(state.traceAndBackface)) || glm::any(glm::isinf(state.traceAndBackface)) ||
                    state.targetAndConfidence.w < 0.0f || state.targetAndConfidence.w > 1.0f || state.metadata.x > 1u;
            }
            const auto coverage = m_State->m_WorkScheduler.RegionCoverage(index);
            VANS_LOG("[GIStateAudit] region=" << index << " frame=" << region.stateAuditFrame
                << " total=" << region.physicalProbeCount << " unseen=" << unseen << " updated=" << updated
                << " published=" << published << " pendingPublished=" << pending
                << " unpublishedUpdated=" << updated - published << " invalidFields=" << invalid
                << " completedProbes=" << coverage.updated << " minCompletedUpdates=" << coverage.minCompletedUpdates
                << " maxCompletedUpdates=" << coverage.maxCompletedUpdates << " oldestUpdateAge=" << oldestUpdateAge
                << " parentTotal=" << parentTotal << " parentUpdated=" << parentUpdated);
            region.stateAuditPending = false;
        }
    }
    m_State->m_GIFeedbackFence = VK_NULL_HANDLE;
}

void VansGraphics::VansRayTracing::DiscardGIProbeUpdate()
{
    if (m_State->m_GIFeedbackFence == VK_NULL_HANDLE) return;
    m_State->m_GIFeedbackFence = VK_NULL_HANDLE;
    // 被取消或失败的提交没有可靠回读；下一批从完整方向周期重建。
    for (auto& region : m_State->m_GIRegions)
        region.stateAuditPending = false;
    m_State->m_WorkScheduler.ResetLighting();
}

void VansGraphics::VansRayTracing::PrepareGIProbeUpdate(
	const VansRenderLightFrameData& lightFrame,
	VansMaterialManager* materialManager, VkFence completionFence)
{
    if (!m_State->m_RTResourcesReady || m_State->m_GIRegions.empty())
        return;

    if (m_State->m_GIFeedbackFence != VK_NULL_HANDLE || completionFence == VK_NULL_HANDLE)
        throw std::runtime_error("GI feedback reused before its frame completion");
    m_State->m_GIFeedbackFence = completionFence;

	const bool lightingChanged = UpdateLightingResponseState(lightFrame);
    if (lightingChanged && materialManager != nullptr)
    {
        materialManager->m_SSGITemporalFrame = 0;
    }
    // 连续天体运动保留采样序列和几何状态；更新间隔只记录到工作反馈，不改变采样历史权重。
    const auto now = std::chrono::steady_clock::now();
    const double elapsed = m_State->m_LastGIUpdateTime.time_since_epoch().count() == 0 ? 1.0 / 60.0 :
        std::chrono::duration<double>(now - m_State->m_LastGIUpdateTime).count();
    m_State->m_LastGIUpdateTime = now;
    const auto& scheduled = m_State->m_WorkScheduler.NextFrame(elapsed);
    for (size_t index = 0; index < m_State->m_GIRegions.size(); ++index)
    {
        GIRegionRuntime& region = m_State->m_GIRegions[index];
        region.constants.frameParams.x = static_cast<float>(region.giUpdateFrameIndex);
        region.work = scheduled[index];
    }
}

void VansGraphics::VansRayTracing::UpdateGIProbe(
	VansVKDevice* device,
	VansVKCommandBuffer* commandBuffer,
	VansMaterialManager* materialManager)
{
    if (!m_State->m_RTResourcesReady || m_State->m_GIRegions.empty())
        return;

    commandBuffer->EnsureComputeShader(
        *m_State->m_RayTracingPointLighting,
        { m_Scene->GetGlobalDescriptorSetLayout(), m_State->m_GISamplePositionLightSetLayout });
	const VkCommandBuffer nativeCommandBuffer = commandBuffer->GetVKCommandBuffer();
	const Vans::VansGpuQueueLane queueLane = device != nullptr && device->IsAsyncComputeEnabled()
		? Vans::VansGpuQueueLane::Compute
		: Vans::VansGpuQueueLane::Graphics;

    for (uint32_t regionIndex = 0; regionIndex < m_State->m_GIRegions.size(); ++regionIndex)
    {
        GIRegionRuntime& region = m_State->m_GIRegions[regionIndex];
        if (region.work.entries.empty()) continue;
        BindGIPointLightData(regionIndex);

        const uint32_t updateFrameIndex = region.giUpdateFrameIndex;
        region.constants.frameParams.x = static_cast<float>(updateFrameIndex);
        // 光照重置随每个工作项发布；分区被预算延迟后仍保留自己的完整采样序列。

        const uint32_t workCount = uint32_t(region.work.entries.size());
        const uint32_t shadeGroups = CeilDivide(workCount * region.work.raysPerProbeUpdate, 64u);
        const glm::uvec3 groupCount(std::min(shadeGroups, m_State->m_MaxComputeGroupsX),
            CeilDivide(shadeGroups, m_State->m_MaxComputeGroupsX), 1u);

		{
			VANS_GPU_SCOPE_LANE(nativeCommandBuffer, "DDGI.RadianceShade", queueLane);
			commandBuffer->DispatchCompute(
				*m_State->m_RayTracingPointLighting,
				groupCount.x,
				groupCount.y,
				groupCount.z,
				{ m_Scene->GetGlobalDescriptorSet(), m_State->m_GISamplePositionLightDescriptorSets[regionIndex] },
				&region.constants,
				sizeof(region.constants));
		}

    }
    for (uint32_t regionIndex = 0; regionIndex < m_State->m_GIRegions.size(); ++regionIndex)
    {
        GIRegionRuntime& region = m_State->m_GIRegions[regionIndex];
        if (region.work.entries.empty()) continue;
        const uint32_t workCount = uint32_t(region.work.entries.size());
        const glm::uvec3 probeGroups(std::min(workCount, m_State->m_MaxComputeGroupsX),
            CeilDivide(workCount, m_State->m_MaxComputeGroupsX), 1u);
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

        if (m_State->m_GIVisibilityUpdateShader != nullptr)
        {
            BindGIVisibilityData(materialManager, regionIndex);
            commandBuffer->EnsureComputeShader(*m_State->m_GIVisibilityUpdateShader, { m_State->m_GIVisibilityUpdateSetLayout });
			{
				VANS_GPU_SCOPE_LANE(nativeCommandBuffer, "DDGI.AtlasPrefilter", queueLane);
				commandBuffer->DispatchCompute(
					*m_State->m_GIVisibilityUpdateShader,
					probeGroups.x,
					probeGroups.y,
					probeGroups.z,
					{ m_State->m_GIVisibilityUpdateDescriptorSets[regionIndex] },
					&region.constants,
					sizeof(region.constants));
			}

            // The next scheduled batch shades hits by sampling the previous
            // DDGI atlas.  This explicit dependency is required even when a
            // probe is delayed by the shared frame budget; otherwise a
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

        if (m_State->m_GIProbeStateShader != nullptr)
        {
            BindGIProbeStateData(regionIndex);
            commandBuffer->EnsureComputeShader(*m_State->m_GIProbeStateShader, { m_State->m_GIProbeStateSetLayout });
			{
				VANS_GPU_SCOPE_LANE(nativeCommandBuffer, "DDGI.ProbeState", queueLane);
				commandBuffer->DispatchCompute(
					*m_State->m_GIProbeStateShader,
					probeGroups.x,
					probeGroups.y,
					probeGroups.z,
					{ m_State->m_GIProbeStateDescriptorSets[regionIndex] },
					&region.constants,
					sizeof(region.constants));
			}

            // Relocation and publication are consumed by the following
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

        ++region.giUpdateFrameIndex;

    }

    {
        VkMemoryBarrier toCopy{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        toCopy.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        toCopy.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        commandBuffer->PipelineBarrier(VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, {toCopy});
        for (auto& region : m_State->m_GIRegions)
        {
            if (region.work.entries.empty()) continue;
            commandBuffer->CopyBuffer(region.feedbackBuffer.GetNativeBuffer(),
                region.feedbackReadback.GetNativeBuffer(), 0u, 0u, region.work.entries.size() * sizeof(GIProbeFeedback));
            // 诊断每 128 次区域更新取样一次；正常运行无分配、无全表回读。
            if (m_State->m_GIStateAuditEnabled && region.giUpdateFrameIndex % 128u == 0u)
            {
                commandBuffer->CopyBuffer(region.probeStateBuffer.GetNativeBuffer(), region.stateAuditReadback.GetNativeBuffer(),
                    0u, 0u, VkDeviceSize(region.physicalProbeCount) * 48u);
                region.stateAuditFrame = region.giUpdateFrameIndex;
                region.stateAuditPending = true;
            }
        }
        VkMemoryBarrier toHost{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        toHost.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        commandBuffer->PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, {toHost});
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
    if (regionIndex >= m_State->m_GIRegions.size() ||
        regionIndex >= m_State->m_GISamplePositionLightDescriptorSets.size())
    {
        return;
    }

    GIRegionRuntime& region = m_State->m_GIRegions[regionIndex];
    if (!region.giPointLightDescriptorSetIsDirty)
    {
        return;
    }

    VansMaterialManager* manager = m_Scene->GetMaterialManager();
	if (region.visibilityAtlas == nullptr || region.irradianceAtlas == nullptr ||
		manager == nullptr || manager->m_SkyLighting.Radiance() == nullptr)
    {
        return;
    }
    VkDescriptorSet descriptorSet = m_State->m_GISamplePositionLightDescriptorSets[regionIndex];

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

	auto& skyImage = manager->m_SkyLighting.Radiance()->GetImage();
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
		VansRenderPassManager::GetInstance()->GetPunctualShadowDescriptorInfos());
    std::vector<VkDescriptorImageInfo> visibilityInfos, irradianceInfos;
    std::vector<VkDescriptorBufferInfo> stateInfos;
    for (uint32_t index = 0; index < VANS_SSGI_MAX_GI_REGIONS; ++index)
    {
        const auto& source = m_State->m_GIRegions[index < m_State->m_GIRegions.size() ? index : 0u];
        visibilityInfos.push_back({source.visibilityAtlas->GetImage().GetSampler(), source.visibilityAtlas->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL});
        irradianceInfos.push_back({source.irradianceAtlas->GetImage().GetSampler(), source.irradianceAtlas->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL});
        stateInfos.push_back({source.probeStateBuffer.GetNativeBuffer(), 0, source.probeStateBuffer.GetBufferSize()});
    }
    descManager->WriteImageDescriptor(descriptorSet, GIPL_BINDING_GI_VISIBILITY, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, visibilityInfos);
    descManager->WriteImageDescriptor(descriptorSet, GIPL_BINDING_IRRADIANCE_ATLAS, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, irradianceInfos);
    descManager->WriteBufferDescriptor(descriptorSet, GIPL_BINDING_PROBE_STATE, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, stateInfos);
    descManager->WriteBufferDescriptor(descriptorSet, 14u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ region.workBuffer.GetNativeBuffer(), 0, region.workBuffer.GetBufferSize() }});
    descManager->WriteBufferDescriptor(descriptorSet, 15u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ m_State->m_ProbeLayoutBuffer.GetNativeBuffer(), 0, m_State->m_ProbeLayoutBuffer.GetBufferSize() }});
    descManager->CommitDescriptorUpdates();
    region.giPointLightDescriptorSetIsDirty = false;
}

void VansGraphics::VansRayTracing::BindGIVisibilityData(VansMaterialManager* materialManager, uint32_t regionIndex)
{
    if (regionIndex >= m_State->m_GIRegions.size() ||
        regionIndex >= m_State->m_GIVisibilityUpdateDescriptorSets.size())
    {
        return;
    }

    GIRegionRuntime& region = m_State->m_GIRegions[regionIndex];
    if (!region.giVisibilityDescriptorSetIsDirty)
    {
        return;
    }

    if (materialManager == nullptr || region.visibilityAtlas == nullptr ||
		region.irradianceAtlas == nullptr)
    {
        return;
    }

    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();
    descManager->WriteBufferDescriptor(
        m_State->m_GIVisibilityUpdateDescriptorSets[regionIndex],
        GI_VISIBILITY_BINDING_HIT_POSITION,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitPositionResult.GetNativeBuffer(),
            0,
            region.hitPositionResult.GetBufferSize()
        }});
    descManager->WriteImageDescriptor(
        m_State->m_GIVisibilityUpdateDescriptorSets[regionIndex],
        GI_VISIBILITY_BINDING_RESULT,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{
            region.visibilityAtlas->GetImage().GetSampler(),
            region.visibilityAtlas->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->WriteBufferDescriptor(
        m_State->m_GIVisibilityUpdateDescriptorSets[regionIndex],
        GI_VISIBILITY_BINDING_RADIANCE,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitRadianceBuffer.GetNativeBuffer(),
            0,
            region.hitRadianceBuffer.GetBufferSize()
        }});
    descManager->WriteImageDescriptor(
        m_State->m_GIVisibilityUpdateDescriptorSets[regionIndex],
        GI_VISIBILITY_BINDING_IRRADIANCE,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{
            region.irradianceAtlas->GetImage().GetSampler(),
            region.irradianceAtlas->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->WriteBufferDescriptor(
        m_State->m_GIVisibilityUpdateDescriptorSets[regionIndex],
        GI_VISIBILITY_BINDING_PROBE_STATE,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ region.probeStateBuffer.GetNativeBuffer(), 0, region.probeStateBuffer.GetBufferSize() }});
    descManager->WriteBufferDescriptor(m_State->m_GIVisibilityUpdateDescriptorSets[regionIndex], GI_VISIBILITY_BINDING_WORK, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ region.workBuffer.GetNativeBuffer(), 0, region.workBuffer.GetBufferSize() }});
    descManager->WriteBufferDescriptor(m_State->m_GIVisibilityUpdateDescriptorSets[regionIndex], GI_VISIBILITY_BINDING_LAYOUT, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ m_State->m_ProbeLayoutBuffer.GetNativeBuffer(), 0, m_State->m_ProbeLayoutBuffer.GetBufferSize() }});
    descManager->CommitDescriptorUpdates();
    region.giVisibilityDescriptorSetIsDirty = false;
}

void VansGraphics::VansRayTracing::BindGIProbeStateData(uint32_t regionIndex)
{
    if (regionIndex >= m_State->m_GIRegions.size() || regionIndex >= m_State->m_GIProbeStateDescriptorSets.size())
        return;
    GIRegionRuntime& region = m_State->m_GIRegions[regionIndex];
    if (!region.giProbeStateDescriptorSetIsDirty)
        return;

    auto* descManager = VansVKDescriptorManager::GetInstance();
    const VkDescriptorSet descriptorSet = m_State->m_GIProbeStateDescriptorSets[regionIndex];
    descManager->BeginDescriptorUpdate();
    const auto writeBuffer = [descManager, descriptorSet](uint32_t binding, VansVKBuffer& buffer)
    {
        descManager->WriteBufferDescriptor(descriptorSet, binding, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {{ buffer.GetNativeBuffer(), 0, buffer.GetBufferSize() }});
    };
    writeBuffer(0u, region.hitPositionResult);
    writeBuffer(1u, region.hitNormalResult);
    writeBuffer(2u, region.probeStateBuffer);
    writeBuffer(3u, region.workBuffer);
    writeBuffer(4u, m_State->m_ProbeLayoutBuffer);
    writeBuffer(5u, region.feedbackBuffer);
    descManager->CommitDescriptorUpdates();
    region.giProbeStateDescriptorSetIsDirty = false;
}

void VansGraphics::VansRayTracing::BindGIRTPreviewData(VansMaterialManager* materialManager)
{
    GIRegionRuntime* previewRegion = GetPreviewRegion();
    if (!m_State->m_GIRTPreviewDescriptorSetIsDirty || materialManager == nullptr ||
        m_State->m_GIRTPreviewTextures[0] == nullptr || previewRegion == nullptr ||
        previewRegion->rayTracingResult == nullptr)
    {
        return;
    }

	auto* irradianceAtlas = previewRegion->irradianceAtlas;
	auto* visibilityAtlas = previewRegion->visibilityAtlas;
	if (irradianceAtlas == nullptr || visibilityAtlas == nullptr || m_State->m_GIRTPreviewDescriptorSets.empty())
        return;

    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();
    const glm::uvec3& grid = previewRegion->storageDimensions;
    const uint32_t zSlice = std::min(
        static_cast<uint32_t>(m_State->m_GIRTPreviewConstants[0].selectionParams.y),
        std::max(grid.z, 1u) - 1u);
    for (uint32_t mode = 0u; mode < GIRTPreviewModeCount; ++mode)
    {
        m_State->m_GIRTPreviewConstants[mode].displayParams.y = 0.0f;
        const VkDescriptorSet descriptorSet = m_State->m_GIRTPreviewDescriptorSets[mode];

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
	writeWholeBuffer(9u, previewRegion->workBuffer);
    writeWholeBuffer(10u, m_State->m_ProbeLayoutBuffer);
	writeStorageImage(GI_RT_PREVIEW_BINDING_OUTPUT, m_State->m_GIRTPreviewTextures[mode]);
    }
    descManager->CommitDescriptorUpdates();
    m_State->m_GIRTPreviewDescriptorSetIsDirty = false;
	m_State->m_GIRTPreviewBoundZSlice = zSlice;
}

void VansGraphics::VansRayTracing::DispatchGIRTPreview(
    VansVKCommandBuffer* commandBuffer,
    VansMaterialManager* materialManager)
{
    if (m_State->m_GIRTPreviewRequestFrames == 0 || commandBuffer == nullptr ||
        m_State->m_GIRTPreviewShader == nullptr || m_State->m_GIRTPreviewTextures[0] == nullptr)
    {
        return;
    }

    BindGIRTPreviewData(materialManager);
    if (m_State->m_GIRTPreviewDescriptorSetIsDirty || m_State->m_GIRTPreviewDescriptorSets.empty())
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

    commandBuffer->EnsureComputeShader(*m_State->m_GIRTPreviewShader, { m_State->m_GIRTPreviewSetLayout });
    const GIRegionRuntime* previewRegion = GetPreviewRegion();
    if (previewRegion == nullptr)
        return;

    for (uint32_t mode = 0u; mode < GIRTPreviewModeCount; ++mode)
    {
        commandBuffer->DispatchCompute(
            *m_State->m_GIRTPreviewShader,
            (previewRegion->storageDimensions.x + 7u) / 8u,
            (previewRegion->storageDimensions.y + 7u) / 8u,
            1u,
            { m_State->m_GIRTPreviewDescriptorSets[mode] },
            &m_State->m_GIRTPreviewConstants[mode], sizeof(GIRTPreviewPushConstant));
    }

    VkMemoryBarrier previewBarrier{};
    previewBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    previewBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    previewBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    commandBuffer->PipelineBarrier(
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        { previewBarrier });
    --m_State->m_GIRTPreviewRequestFrames;
}

void VansGraphics::VansRayTracing::CreateGIVisibilityUpdateDescriptorSets(VansVKDevice* device)
{
    RequireGIResource(VansDescriptorSetLayoutFactory::CreateAndAllocate_GIVisibilityUpdate(
        m_State->m_GIVisibilityUpdateSetLayout,
        m_State->m_GIVisibilityUpdateDescriptorSets,
        static_cast<uint32_t>(std::max<size_t>(m_State->m_GIRegions.size(), 1u))), "Failed to allocate GI descriptors");

    for (GIRegionRuntime& region : m_State->m_GIRegions)
        region.giVisibilityDescriptorSetIsDirty = true;
}

void VansGraphics::VansRayTracing::CreateGIProbeStateDescriptorSets(VansVKDevice* device)
{
    RequireGIResource(VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(
        {
            { 0u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 1u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 2u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 3u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 4u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
            { 5u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_COMPUTE_BIT, nullptr },
        },
        m_State->m_GIProbeStateSetLayout,
        m_State->m_GIProbeStateDescriptorSets,
        static_cast<uint32_t>(std::max<size_t>(m_State->m_GIRegions.size(), 1u))), "Failed to allocate GI descriptors");
    for (GIRegionRuntime& region : m_State->m_GIRegions)
        region.giProbeStateDescriptorSetIsDirty = true;
}

void VansGraphics::VansRayTracing::CreateGIRTPreviewDescriptorSets(VansVKDevice* device)
{
    RequireGIResource(VansDescriptorSetLayoutFactory::CreateAndAllocate_GIRTPreview(
        m_State->m_GIRTPreviewSetLayout,
        m_State->m_GIRTPreviewDescriptorSets,
        GIRTPreviewModeCount), "Failed to allocate GI descriptors");
    m_State->m_GIRTPreviewDescriptorSetIsDirty = true;
}

void VansGraphics::VansRayTracing::DispatchRayTracing(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene)
{
    if (!m_State->m_RTResourcesReady || m_State->m_GIRegions.empty())
    {
        return;
    }

    VansVKRayTracingPipeline* vansPipeline = m_State->m_VansRayTracingShader->GetRayTracingPipeline(device, { m_State->m_RayTracingSetLayout });

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
    // 与现有 RT -> GI 提交链共同排序，不在 CPU 修改仍可能被上一帧读取的 SSBO。
    VkMemoryBarrier workToTransfer{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    workToTransfer.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    workToTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    commandBuffer->PipelineBarrier(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, {workToTransfer});
    if (!m_State->m_PendingLayoutRegionData.empty())
    {
        commandBuffer->UpdateBuffer(m_State->m_ProbeLayoutBuffer.GetNativeBuffer(), 4u * sizeof(glm::uvec4),
            m_State->m_PendingLayoutRegionData.size() * sizeof(glm::uvec4), m_State->m_PendingLayoutRegionData.data());
        m_State->m_PendingLayoutRegionData.clear();
    }
    for (auto& region : m_State->m_GIRegions)
    {
        const glm::uvec4 header = {uint32_t(region.work.entries.size()), region.work.raysPerProbeUpdate,
            uint32_t(&region - m_State->m_GIRegions.data()), m_State->m_AutomaticGIWork ? 1u : 0u};
        commandBuffer->UpdateBuffer(region.workBuffer.GetNativeBuffer(), 0u, sizeof(header), &header);
        if (!region.work.entries.empty()) commandBuffer->UpdateBuffer(region.workBuffer.GetNativeBuffer(), sizeof(header),
            region.work.entries.size() * sizeof(GIProbeWorkEntry), region.work.entries.data());
    }
    VkMemoryBarrier workToRead{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    workToRead.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    workToRead.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    commandBuffer->PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, {workToRead});
    for (uint32_t regionIndex = 0; regionIndex < m_State->m_GIRegions.size(); ++regionIndex)
    {
        GIRegionRuntime& region = m_State->m_GIRegions[regionIndex];
        const auto& work = region.work;
        if (work.entries.empty() || !work.raysPerProbeUpdate)
            continue;
        BindRayTracingData(device, scene, regionIndex);

        commandBuffer->BindRayTracingDescriptorSets(
            *vansPipeline,
            0,
            { m_State->m_RayTracingDescriptorSets[regionIndex] });
        commandBuffer->UpdateRayTracingPushConstants(
            *vansPipeline,
            VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
                VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR,
            0,
            m_State->m_VansRayTracingShader->GetPushConstantSize(),
            &region.constants);

        commandBuffer->TraceRays(
            *vansPipeline,
            uint32_t(work.RayCount()), 1u, 1u);

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
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
        nullptr
    };

    VkDescriptorSetLayoutBinding indexDataBuffer =
    {
        PassBinding::BUFFER_4,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        blasDescriptorCount,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
        nullptr
    };

    //instance data buffer
    VkDescriptorSetLayoutBinding instanceDataBuffer =
    {
        PassBinding::BUFFER_5,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
        nullptr
    };
    //instance texture index buffer
    VkDescriptorSetLayoutBinding instanceMaterialDataBuffer =
    {
        PassBinding::BUFFER_7,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR,
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
        VK_SHADER_STAGE_ANY_HIT_BIT_KHR |
        VK_SHADER_STAGE_MISS_BIT_KHR |
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
	};
    

	RequireGIResource(VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(
        {
            tlasBinding,
            resultBinding,
            hitPositionResultBinding,
            vertexDataBuffer, 
            indexDataBuffer ,
            instanceDataBuffer,
            hitNormalResultBinding,
            instanceMaterialDataBuffer,
            bindlessTextureArrayBinding,
            hitPBRAlbedoRoughnessDataBuffer,
			probeStateDataBuffer,
			instanceGIEmissionDataBuffer,
			hitEmissionDataBuffer,
            {12u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr},
            {13u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_RAYGEN_BIT_KHR, nullptr},
            {14u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_ANY_HIT_BIT_KHR, nullptr}
        },
        m_State->m_RayTracingSetLayout,
        m_State->m_RayTracingDescriptorSets,
        static_cast<uint32_t>(std::max<size_t>(m_State->m_GIRegions.size(), 1u))), "Failed to allocate GI descriptors");
    
    for (GIRegionRuntime& region : m_State->m_GIRegions)
        region.rayTracingDescriptorSetIsDirty = true;
}

void VansGraphics::VansRayTracing::CreateGIPointLightDescriptorSets(VansVKDevice* device)
{
    RequireGIResource(VansDescriptorSetLayoutFactory::CreateAndAllocate_GIPointLight(
        m_State->m_GISamplePositionLightSetLayout,
        m_State->m_GISamplePositionLightDescriptorSets,
        static_cast<uint32_t>(std::max<size_t>(m_State->m_GIRegions.size(), 1u))), "Failed to allocate GI descriptors");

    for (GIRegionRuntime& region : m_State->m_GIRegions)
        region.giPointLightDescriptorSetIsDirty = true;
}

void VansGraphics::VansRayTracing::BindRayTracingData(VansVKDevice* device, VansScene* scene, uint32_t regionIndex)
{
    if (regionIndex >= m_State->m_GIRegions.size() ||
        regionIndex >= m_State->m_RayTracingDescriptorSets.size())
    {
        return;
    }

    GIRegionRuntime& region = m_State->m_GIRegions[regionIndex];
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
        m_State->m_RayTracingDescriptorSets[regionIndex],
        PassBinding::BUFFER_2,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitPositionResult.GetNativeBuffer(),
            0,
            region.hitPositionResult.GetBufferSize()
        }});
    descManager->WriteBufferDescriptor(
        m_State->m_RayTracingDescriptorSets[regionIndex],
        PassBinding::BUFFER_6,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitNormalResult.GetNativeBuffer(),
            0,
            region.hitNormalResult.GetBufferSize()
        }});

    descManager->WriteBufferDescriptor(m_State->m_RayTracingDescriptorSets[regionIndex], 14u,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, {{m_State->m_ReceiverGeometryData.GetNativeBuffer(), 0,
            m_State->m_ReceiverGeometryData.GetBufferSize()}});
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
        m_State->m_RayTracingDescriptorSets[regionIndex],
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
        m_State->m_RayTracingDescriptorSets[regionIndex],
        PassBinding::BUFFER_4,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        blasIndexBufferInfos);

    descManager->WriteBufferDescriptor(
        m_State->m_RayTracingDescriptorSets[regionIndex],
        PassBinding::BUFFER_5,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_State->m_BLASInstanceBuffer.GetNativeBuffer(),
            0,
            m_State->m_BLASInstanceBuffer.GetBufferSize()
        }});

    descManager->WriteBufferDescriptor(
        m_State->m_RayTracingDescriptorSets[regionIndex],
        PassBinding::BUFFER_7,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_State->m_TLASInstanceMaterialBuffer.GetNativeBuffer(),
            0,
            m_State->m_TLASInstanceMaterialBuffer.GetBufferSize()
        }});
    descManager->WriteBufferDescriptor(
        m_State->m_RayTracingDescriptorSets[regionIndex],
        PassBinding::BUFFER_8,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.hitAlbedoRoughnessResult.GetNativeBuffer(),
            0,
            region.hitAlbedoRoughnessResult.GetBufferSize()
        }});
    descManager->WriteBufferDescriptor(
        m_State->m_RayTracingDescriptorSets[regionIndex],
        9u,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            region.probeStateBuffer.GetNativeBuffer(),
            0,
            region.probeStateBuffer.GetBufferSize()
        }});
	descManager->WriteBufferDescriptor(
		m_State->m_RayTracingDescriptorSets[regionIndex],
		10u,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{
			m_State->m_TLASInstanceGIEmissionBuffer.GetNativeBuffer(),
			0,
			m_State->m_TLASInstanceGIEmissionBuffer.GetBufferSize()
		}});
	descManager->WriteBufferDescriptor(
		m_State->m_RayTracingDescriptorSets[regionIndex],
		11u,
		VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
		{{
			region.hitEmissionResult.GetNativeBuffer(),
			0,
			region.hitEmissionResult.GetBufferSize()
		}});
    descManager->WriteBufferDescriptor(m_State->m_RayTracingDescriptorSets[regionIndex], 12u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ region.workBuffer.GetNativeBuffer(), 0, region.workBuffer.GetBufferSize() }});
    descManager->WriteBufferDescriptor(m_State->m_RayTracingDescriptorSets[regionIndex], 13u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{ m_State->m_ProbeLayoutBuffer.GetNativeBuffer(), 0, m_State->m_ProbeLayoutBuffer.GetBufferSize() }});
    descManager->WriteAccelerationStructureDescriptor(
        m_State->m_RayTracingDescriptorSets[regionIndex],
        RT_BINDING_TLAS,
        tlas);
    descManager->WriteImageDescriptor(
        m_State->m_RayTracingDescriptorSets[regionIndex],
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
        m_State->m_RayTracingDescriptorSets[regionIndex],
        GLOBAL_BINDING_BINDLESS_TEXTURES,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        bindlessTextureInfos);

    descManager->CommitDescriptorUpdates();
}

const VansGraphics::VansVKBuffer* VansGraphics::VansRayTracing::GetGIRegionPreviousProbeStateBuffer(uint32_t regionIndex) const
{
    return regionIndex < m_State->m_GIRegions.size() ? &m_State->m_GIRegions[regionIndex].previousProbeStateBuffer : nullptr;
}

void VansGraphics::VansRayTracing::CopyPublishedProbeStateHistory(VansVKCommandBuffer& command)
{
    for (const auto& region : m_State->m_GIRegions)
        command.CopyBuffer(region.probeStateBuffer.GetNativeBuffer(), region.previousProbeStateBuffer.GetNativeBuffer(),
            0, 0, region.probeStateBuffer.GetBufferSize());
}

bool VansGraphics::VansRayTracing::DispatchReceiverVisibility(VansVKDevice* device, VansVKCommandBuffer& command,
    VansScene* scene, VkDescriptorSetLayout layout, VkDescriptorSet descriptor)
{
    if (!IsReady() || m_State->m_GIRegions.empty()) return false;
    if (!m_State->m_ReceiverVisibilityShader)
        m_State->m_ReceiverVisibilityShader = VansShaderManager::Get().FindRayTracingShader("GIReceiverVisibilityTrace");
    if (!m_State->m_ReceiverVisibilityShader) return false;
    auto* pipeline = m_State->m_ReceiverVisibilityShader->GetRayTracingPipeline(device, {m_State->m_RayTracingSetLayout, layout});
    if (!pipeline) return false;
    BindRayTracingData(device, scene, 0u);
    command.BindRayTracingPipeline(*pipeline);
    command.BindRayTracingDescriptorSets(*pipeline, 0u, {m_State->m_RayTracingDescriptorSets[0], descriptor});
    command.TraceRays(*pipeline, VansGIReceiverVisibility::RayBudget, 1u, 1u);
    return true;
}

bool VansGraphics::VansRayTracing::DispatchReceiverBias(VansVKDevice* device, VansVKCommandBuffer& command,
    VansScene* scene, VkDescriptorSetLayout layout, VkDescriptorSet descriptor, uint32_t width, uint32_t height)
{
    if (!IsReady() || m_State->m_GIRegions.empty()) return false;
    if (!m_State->m_ReceiverBiasShader)
        m_State->m_ReceiverBiasShader = VansShaderManager::Get().FindRayTracingShader("GIReceiverBias");
    if (!m_State->m_ReceiverBiasShader) return false;
    auto* pipeline = m_State->m_ReceiverBiasShader->GetRayTracingPipeline(device, {m_State->m_RayTracingSetLayout, layout});
    if (!pipeline) return false;
    BindRayTracingData(device, scene, 0u);
    command.BindRayTracingPipeline(*pipeline);
    command.BindRayTracingDescriptorSets(*pipeline, 0u, {m_State->m_RayTracingDescriptorSets[0], descriptor});
    command.TraceRays(*pipeline, width, height, 1u);
    return true;
}
