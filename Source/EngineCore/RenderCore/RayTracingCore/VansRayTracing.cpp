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
}

void VansGraphics::VansRayTracing::CleanupSceneResources(VkDevice device)
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

	descMgr->DestroyDescriptorSet(m_GIRTPreviewDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GIRTPreviewSetLayout);

	// 释放 RT 相关 buffer
	m_RayTracingHitPositionResult.DestroyVulkanBuffer(device);
	m_RayTracingHitNormalResult.DestroyVulkanBuffer(device);
	m_RayTracingHitAlbedoRoughnessResult.DestroyVulkanBuffer(device);
	m_BLASInstanceBuffer.DestroyVulkanBuffer(device);
	m_TLASInstanceTextureIndexBuffer.DestroyVulkanBuffer(device);
	m_HitRadianceBuffer.DestroyVulkanBuffer(device);
	m_HitDirectDiffuseBuffer.DestroyVulkanBuffer(device);

	// 释放场景加载时 new 出来的 RT 渲染资源，防止切换场景时泄漏
	delete m_RayTracingResult;
	m_RayTracingResult = nullptr;

	delete m_SHFeedbackR;
	m_SHFeedbackR = nullptr;

	delete m_SHFeedbackG;
	m_SHFeedbackG = nullptr;

	delete m_SHFeedbackB;
	m_SHFeedbackB = nullptr;

	delete m_GIRTPreviewTexture;
	m_GIRTPreviewTexture = nullptr;

	m_RayTracingPointLighting = nullptr;

	m_GIVisibilityUpdateShader = nullptr;

	m_GIRTPreviewShader = nullptr;

	// 重置 RT 着色器的 pipeline / SBT，下次 CreateRayTracingResource 将重建
	if (m_VansRayTracingShader)
		m_VansRayTracingShader->TriggerReCreateRayTracingPipeline();
	m_VansRayTracingShader = nullptr;

	// 标记脏以便下次 CreateRayTracingResource 重新绑定
	m_RayTracingDescriptorSetIsDirty = true;
	m_GIPointLightDescriptorSetIsDirty = true;
	m_GIVisibilityDescriptorSetIsDirty = true;
	m_GIRTPreviewDescriptorSetIsDirty = true;
	m_GIRTPreviewRequestFrames = 0;
	m_GIRTPreviewBoundZSlice = 0xffffffffu;
	m_HitPositionCalculateDone = false;
	m_GIVisibilityCalculateDone = false;
	m_GIUpdateFrameIndex = 0;
	m_RTResourcesReady = false;

	VANS_LOG("[VansRayTracing] Scene RT resources cleaned up");
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

    const VansGISettings& gi = scene->GetGISettings();
    m_RayTracingGridDimensions = gi.gridDimensions;
    m_RayTracingProbeSpacing = gi.probeSpacingAxes;
    m_RayCountPerSample = static_cast<int>(gi.raysPerProbe);

    m_RayTracingConstant.dispatchParams = glm::vec4(
        glm::vec3(gi.gridDimensions), static_cast<float>(gi.raysPerProbe));
    m_RayTracingConstant.gridParams = glm::vec4(gi.probeSpacingAxes, gi.maxRayDistance);
    m_RayTracingConstant.frameParams = glm::vec4(
        0.0f, static_cast<float>(gi.spatialUpdateDivisor),
        static_cast<float>(gi.directionUpdateSlices), 0.0f);
    m_RayTracingConstant.regionParams = glm::vec4(gi.regionCenter, gi.normalBias);
    m_RayTracingConstant.lightingParams = glm::vec4(
        gi.environmentIntensity, gi.maxIndirectRadiance, gi.maxSHL0, 0.0f);

    VANS_LOG("[CreateRayTracingResource] GI grid="
        << gi.gridDimensions.x << "x"
        << gi.gridDimensions.y << "x"
        << gi.gridDimensions.z
        << " spacing=(" << gi.probeSpacingAxes.x << ","
        << gi.probeSpacingAxes.y << ","
        << gi.probeSpacingAxes.z << ")"
        << " probes=" << (gi.gridDimensions.x * gi.gridDimensions.y * gi.gridDimensions.z)
        << " pushConstants=" << sizeof(m_RayTracingConstant) << " bytes");

    auto& shaderManager = VansShaderManager::Get();
    m_VansRayTracingShader = shaderManager.FindRayTracingShader("RayTracingTest");
    if (!m_VansRayTracingShader)
    {
        VANS_LOG_ERROR("[CreateRayTracingResource] Managed RayTracingTest shader is unavailable");
        m_RTResourcesReady = false;
        return;
    }
    
    m_RayTracingResult = new VansTexture();
    m_RayTracingResult->InitTextureWithoutData(*commandBuffer, m_RayTracingGridDimensions.x, m_RayTracingGridDimensions.y, m_RayTracingGridDimensions.z, 4, false, false, true, HDR_PRES_16);
   
    VansMaterialManager* materialManager = scene->GetMaterialManager();
    VansTexture* shRResult = new VansTexture();
    shRResult->InitTextureWithoutData(*commandBuffer, m_RayTracingGridDimensions.x, m_RayTracingGridDimensions.y, m_RayTracingGridDimensions.z, 4, false, false, true, HIGH_PRES_32);
    materialManager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT, shRResult);

    VansTexture* shGResult = new VansTexture();
    shGResult->InitTextureWithoutData(*commandBuffer, m_RayTracingGridDimensions.x, m_RayTracingGridDimensions.y, m_RayTracingGridDimensions.z, 4, false, false, true, HIGH_PRES_32);
    materialManager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT, shGResult);

    VansTexture* shBResult = new VansTexture();
    shBResult->InitTextureWithoutData(*commandBuffer, m_RayTracingGridDimensions.x, m_RayTracingGridDimensions.y, m_RayTracingGridDimensions.z, 4, false, false, true, HIGH_PRES_32);
    materialManager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT, shBResult);

    const int totalProbeCount = static_cast<int>(m_RayTracingGridDimensions.x * m_RayTracingGridDimensions.y * m_RayTracingGridDimensions.z);
    const int probesPerAtlasRow = std::max(1, static_cast<int>(std::ceil(std::sqrt(static_cast<float>(totalProbeCount)))));
    const int probesPerAtlasColumn = (totalProbeCount + probesPerAtlasRow - 1) / probesPerAtlasRow;

    VansTexture* giVisibilityAtlas = new VansTexture();
    giVisibilityAtlas->InitTextureWithoutData(
        *commandBuffer,
        probesPerAtlasRow * kGIVisibilityOctaRes,
        probesPerAtlasColumn * kGIVisibilityOctaRes,
        1,
        4,
        false,
        false,
        true,
        HIGH_PRES_32,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE);
    materialManager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_GI_VISIBILITY_ATLAS, giVisibilityAtlas);

    m_SHFeedbackR = new VansTexture();
    m_SHFeedbackR->InitTextureWithoutData(*commandBuffer, m_RayTracingGridDimensions.x, m_RayTracingGridDimensions.y, m_RayTracingGridDimensions.z, 4, false, false, true, HIGH_PRES_32);

    m_SHFeedbackG = new VansTexture();
    m_SHFeedbackG->InitTextureWithoutData(*commandBuffer, m_RayTracingGridDimensions.x, m_RayTracingGridDimensions.y, m_RayTracingGridDimensions.z, 4, false, false, true, HIGH_PRES_32);

    m_SHFeedbackB = new VansTexture();
    m_SHFeedbackB->InitTextureWithoutData(*commandBuffer, m_RayTracingGridDimensions.x, m_RayTracingGridDimensions.y, m_RayTracingGridDimensions.z, 4, false, false, true, HIGH_PRES_32);

	m_GIRTPreviewTexture = new VansTexture();
	m_GIRTPreviewTexture->InitTextureWithoutData(
		*commandBuffer,
		static_cast<int>(m_RayTracingGridDimensions.x),
		static_cast<int>(m_RayTracingGridDimensions.y),
		1, 4, false, false, true, HIGH_PRES_32,
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

    const VkDeviceSize giHitBufferSize = static_cast<VkDeviceSize>(gi.raysPerProbe) *
        gi.gridDimensions.x * gi.gridDimensions.y * gi.gridDimensions.z * sizeof(uint16_t) * 4;
    const VkDeviceSize giDirectCacheBufferSize = static_cast<VkDeviceSize>(gi.raysPerProbe) *
        gi.gridDimensions.x * gi.gridDimensions.y * gi.gridDimensions.z * sizeof(uint32_t);

    //命中点法线和位置
    m_RayTracingHitPositionResult.CreatVulkanBuffer(device->GetLogicDevice(),
        giHitBufferSize,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
    m_RayTracingHitNormalResult.CreatVulkanBuffer(device->GetLogicDevice(),
        giHitBufferSize,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    //命中点pbr
    m_RayTracingHitAlbedoRoughnessResult.CreatVulkanBuffer(device->GetLogicDevice(),
        giHitBufferSize,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    m_HitRadianceBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
        giHitBufferSize,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    m_HitDirectDiffuseBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
        giDirectCacheBufferSize,
        VK_FORMAT_R32_UINT,
        VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    // GI 分帧更新会在冷启动阶段读取尚未轮到的方向 slice。
    // 显式清零所有反馈资源，保证未写入 slice 的 radiance 为 0，而不是未定义显存。
    if (commandBuffer->BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
    {
        commandBuffer->FillBuffer(m_HitRadianceBuffer.GetNativeBuffer(), 0, m_HitRadianceBuffer.GetBufferSize(), 0u);
        commandBuffer->FillBuffer(m_HitDirectDiffuseBuffer.GetNativeBuffer(), 0, m_HitDirectDiffuseBuffer.GetBufferSize(), 0u);

        VkClearColorValue clearSH{};
        commandBuffer->ClearColorImage(shRResult->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
        commandBuffer->ClearColorImage(shGResult->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
        commandBuffer->ClearColorImage(shBResult->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
        commandBuffer->ClearColorImage(giVisibilityAtlas->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
        commandBuffer->ClearColorImage(m_SHFeedbackR->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
        commandBuffer->ClearColorImage(m_SHFeedbackG->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
        commandBuffer->ClearColorImage(m_SHFeedbackB->GetImage(), VK_IMAGE_LAYOUT_GENERAL, clearSH);
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

    m_HitPositionCalculateDone = false;
    m_GIVisibilityCalculateDone = false;

    m_GIUpdateFrameIndex = 0;
    m_HasLastGIMainLight = false;
    m_GILightingResponseFramesRemaining = 0;
	m_GIRTPreviewRequestFrames = 0;
}

void VansGraphics::VansRayTracing::UpdateGISettings(const VansGISettings& settings)
{
    m_RayTracingGridDimensions = settings.gridDimensions;
    m_RayTracingProbeSpacing = settings.probeSpacingAxes;
    m_RayCountPerSample = static_cast<int>(settings.raysPerProbe);

    m_RayTracingConstant.dispatchParams = glm::vec4(
        glm::vec3(settings.gridDimensions),
        static_cast<float>(settings.raysPerProbe));
    m_RayTracingConstant.gridParams = glm::vec4(settings.probeSpacingAxes, settings.maxRayDistance);
    m_RayTracingConstant.frameParams.y = static_cast<float>(settings.spatialUpdateDivisor);
    m_RayTracingConstant.frameParams.z = static_cast<float>(settings.directionUpdateSlices);
    m_RayTracingConstant.regionParams = glm::vec4(settings.regionCenter, settings.normalBias);
    m_RayTracingConstant.lightingParams = glm::vec4(
        settings.environmentIntensity,
        settings.maxIndirectRadiance,
        settings.maxSHL0,
        0.0f);
}

void VansGraphics::VansRayTracing::RequestGIRTPreview(
    uint32_t mode,
    uint32_t zSlice,
    uint32_t rayIndex,
    float exposure,
    float positionScale)
{
    if (!m_RTResourcesReady)
        return;

    const uint32_t zCount = std::max(m_RayTracingGridDimensions.z, 1u);
    const uint32_t rayCount = std::max(static_cast<uint32_t>(std::max(m_RayCountPerSample, 1)), 1u);
    m_GIRTPreviewConstant.gridParams = glm::vec4(
        glm::vec3(m_RayTracingGridDimensions),
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
    if (!directionLights.empty())
    {
        const VansDirectionalLight& mainLight = directionLights[0];
        glm::vec3 direction = mainLight.m_Direction;
        if (glm::dot(direction, direction) > 1e-6f)
            direction = glm::normalize(direction);
        else
            direction = glm::vec3(0.0f, 1.0f, 0.0f);

        // 与上传到 LightsData.glsl 的主光颜色保持一致，让 GI 响应大气仰角衰减后的太阳颜色。
        glm::vec3 effectiveColor = VansLightManager::ComputeAtmosphereSunColor(direction, mainLight.m_Color);
        directionIntensity = glm::vec4(direction, mainLight.m_Intensity);
        lightColor = glm::vec4(effectiveColor, 0.0f);
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
    if (!m_RTResourcesReady)
        return;

    const bool lightingChanged = UpdateLightingResponseState(lightManager);
    if (lightingChanged && materialManager != nullptr)
    {
        materialManager->m_SSGITemporalFrame = 0;
    }

    BindGIPointLightData();

    m_RayTracingConstant.frameParams.x = static_cast<float>(m_GIUpdateFrameIndex++);

    const float configuredUpdateDivisor = m_RayTracingConstant.frameParams.y;
    const float configuredDirectionSlices = m_RayTracingConstant.frameParams.z;
    m_RayTracingConstant.lightingParams.w = m_GILightingResponseFramesRemaining > 0 ? 1.0f : 0.0f;
    if (m_GILightingResponseFramesRemaining > 0)
    {
        m_RayTracingConstant.frameParams.y = static_cast<float>(std::min(std::max(1u, static_cast<uint32_t>(configuredUpdateDivisor)), 2u));
        m_RayTracingConstant.frameParams.z = 1.0f;
        --m_GILightingResponseFramesRemaining;
    }

    const uint32_t updateDivisor = std::max(1u, static_cast<uint32_t>(m_RayTracingConstant.frameParams.y));
    const auto ceilDivide = [](uint32_t value, uint32_t divisor)
    {
        return value / divisor + (value % divisor != 0u ? 1u : 0u);
    };
    const glm::uvec3 probesPerAxis(
        ceilDivide(m_RayTracingGridDimensions.x, updateDivisor),
        ceilDivide(m_RayTracingGridDimensions.y, updateDivisor),
        ceilDivide(m_RayTracingGridDimensions.z, updateDivisor));
    const glm::uvec3 groupCount(
        ceilDivide(probesPerAxis.x, 4u),
        ceilDivide(probesPerAxis.y, 4u),
        ceilDivide(probesPerAxis.z, 4u));

    // GI point-light evaluation remains independent of reflection probes.
    commandBuffer->EnsureComputeShader(*m_RayTracingPointLighting, { m_Scene->GetGlobalDescriptorSetLayout(), m_GISamplePositionLightSetLayout});
    commandBuffer->DispatchCompute(
        *m_RayTracingPointLighting, 
        groupCount.x,
        groupCount.y,
        groupCount.z,
        { m_Scene->GetGlobalDescriptorSet(), m_GISamplePositionLightDescriptorSets[0]},
        &m_RayTracingConstant, sizeof(m_RayTracingConstant));

    CopyCurrentSHToFeedback(commandBuffer, materialManager);

	DispatchGIRTPreview(commandBuffer, materialManager);

    m_RayTracingConstant.frameParams.y = configuredUpdateDivisor;
    m_RayTracingConstant.frameParams.z = configuredDirectionSlices;
}

void VansGraphics::VansRayTracing::CopyCurrentSHToFeedback(VansVKCommandBuffer* commandBuffer, VansMaterialManager* materialManager)
{
    if (commandBuffer == nullptr || materialManager == nullptr ||
        m_SHFeedbackR == nullptr || m_SHFeedbackG == nullptr || m_SHFeedbackB == nullptr)
    {
        return;
    }

    auto* shR = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT);
    auto* shG = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT);
    auto* shB = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT);
    if (shR == nullptr || shG == nullptr || shB == nullptr)
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
    copySH(shR, m_SHFeedbackR);
    copySH(shG, m_SHFeedbackG);
    copySH(shB, m_SHFeedbackB);
}

void VansGraphics::VansRayTracing::BindGIPointLightData()
{
    if (!m_GIPointLightDescriptorSetIsDirty)
    {
        return;
    }

    VansMaterialManager* manager = m_Scene->GetMaterialManager();
    if (m_SHFeedbackR == nullptr || m_SHFeedbackG == nullptr || m_SHFeedbackB == nullptr)
    {
        return;
    }
    auto* visibilityAtlas = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_GI_VISIBILITY_ATLAS);
    auto* rCoeffTexture = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT);
    auto* gCoeffTexture = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT);
    auto* bCoeffTexture = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT);
    if (visibilityAtlas == nullptr || rCoeffTexture == nullptr ||
        gCoeffTexture == nullptr || bCoeffTexture == nullptr)
    {
        return;
    }

    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();
    descManager->WriteBufferDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
        GIPL_BINDING_HIT_POSITION,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_RayTracingHitPositionResult.GetNativeBuffer(),
            0,
            m_RayTracingHitPositionResult.GetBufferSize()
        }});
    descManager->WriteBufferDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
        GIPL_BINDING_HIT_NORMAL,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_RayTracingHitNormalResult.GetNativeBuffer(),
            0,
            m_RayTracingHitNormalResult.GetBufferSize()
        }});

    descManager->WriteBufferDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
        GIPL_BINDING_RADIANCE,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_HitRadianceBuffer.GetNativeBuffer(),
            0,
            m_HitRadianceBuffer.GetBufferSize()
        }});

    descManager->WriteBufferDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
        GIPL_BINDING_PBR_DATA,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_RayTracingHitAlbedoRoughnessResult.GetNativeBuffer(),
            0,
            m_RayTracingHitAlbedoRoughnessResult.GetBufferSize()
        }});

    auto& skyImage = manager->m_PreConvDiffuse->GetImage();
    //设置天空盒
    descManager->WriteImageDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
        GIPL_BINDING_ENVIRONMENT_MAP,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            skyImage.GetSampler(),
            skyImage.GetImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }});

    //设置球谐积分贴图
    descManager->WriteImageDescriptor(
            m_GISamplePositionLightDescriptorSets[0],
            GIPL_BINDING_SH_R,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{
            m_SHFeedbackR->GetImage().GetSampler(),
            m_SHFeedbackR->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});

    descManager->WriteBufferDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
        GIPL_BINDING_DIRECT_CACHE,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_HitDirectDiffuseBuffer.GetNativeBuffer(),
            0,
            m_HitDirectDiffuseBuffer.GetBufferSize()
        }});
    descManager->WriteImageDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
            GIPL_BINDING_SH_G,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{
            m_SHFeedbackG->GetImage().GetSampler(),
            m_SHFeedbackG->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->WriteImageDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
            GIPL_BINDING_SH_B,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {{
            m_SHFeedbackB->GetImage().GetSampler(),
            m_SHFeedbackB->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->WriteImageDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
        GIPL_BINDING_SHADOW_MAP,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            VansRenderPassManager::GetInstance()->GetCascadeShadowSampler(),
            VansRenderPassManager::GetInstance()->GetCascadeShadowLayerView(1),  // matches RAYTRACING_CASCADE_INDEX in Common.glsl
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }});
    descManager->WriteImageDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
        GIPL_BINDING_PUNCTUAL_SHADOW,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            VansRenderPassManager::GetInstance()->GetPunctualShadowMap().GetSampler(),
            VansRenderPassManager::GetInstance()->GetPunctualShadowMap().GetImageView(),
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        }});
    descManager->WriteImageDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
        GIPL_BINDING_GI_VISIBILITY,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            visibilityAtlas->GetImage().GetSampler(),
            visibilityAtlas->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->WriteImageDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
        GIPL_BINDING_RESULT_R,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{ rCoeffTexture->GetImage().GetSampler(), rCoeffTexture->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
    descManager->WriteImageDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
        GIPL_BINDING_RESULT_G,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{ gCoeffTexture->GetImage().GetSampler(), gCoeffTexture->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});
    descManager->WriteImageDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
        GIPL_BINDING_RESULT_B,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{ bCoeffTexture->GetImage().GetSampler(), bCoeffTexture->GetImage().GetImageView(), VK_IMAGE_LAYOUT_GENERAL }});

    descManager->CommitDescriptorUpdates();
    m_GIPointLightDescriptorSetIsDirty = false;
}

void VansGraphics::VansRayTracing::BindGIVisibilityData(VansMaterialManager* materialManager)
{
    if (!m_GIVisibilityDescriptorSetIsDirty)
    {
        return;
    }

    if (materialManager == nullptr)
    {
        return;
    }

    auto* visibilityAtlas = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_GI_VISIBILITY_ATLAS);
    if (visibilityAtlas == nullptr)
    {
        return;
    }

    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();
    descManager->WriteBufferDescriptor(
        m_GIVisibilityUpdateDescriptorSets[0],
        GI_VISIBILITY_BINDING_HIT_POSITION,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_RayTracingHitPositionResult.GetNativeBuffer(),
            0,
            m_RayTracingHitPositionResult.GetBufferSize()
        }});
    descManager->WriteImageDescriptor(
        m_GIVisibilityUpdateDescriptorSets[0],
        GI_VISIBILITY_BINDING_RESULT,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{
            visibilityAtlas->GetImage().GetSampler(),
            visibilityAtlas->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->CommitDescriptorUpdates();
    m_GIVisibilityDescriptorSetIsDirty = false;
}

void VansGraphics::VansRayTracing::BindGIRTPreviewData(VansMaterialManager* materialManager)
{
    if (!m_GIRTPreviewDescriptorSetIsDirty || materialManager == nullptr ||
        m_GIRTPreviewTexture == nullptr || m_RayTracingResult == nullptr)
    {
        return;
    }

    auto* shR = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT);
    auto* shG = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT);
    auto* shB = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT);
    if (shR == nullptr || shG == nullptr || shB == nullptr || m_GIRTPreviewDescriptorSets.empty())
        return;

    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();
    const VkDeviceSize elementBytes = sizeof(uint16_t) * 4u;
    const VkDeviceSize sliceBytes = static_cast<VkDeviceSize>(m_RayTracingGridDimensions.x) *
        m_RayTracingGridDimensions.y * std::max(m_RayCountPerSample, 1) * elementBytes;
    const uint32_t zSlice = std::min(
        static_cast<uint32_t>(m_GIRTPreviewConstant.selectionParams.y),
        std::max(m_RayTracingGridDimensions.z, 1u) - 1u);
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

    writeBuffer(GI_RT_PREVIEW_BINDING_HIT_POSITION, m_RayTracingHitPositionResult);
    writeBuffer(GI_RT_PREVIEW_BINDING_HIT_NORMAL, m_RayTracingHitNormalResult);
    writeBuffer(GI_RT_PREVIEW_BINDING_HIT_PBR, m_RayTracingHitAlbedoRoughnessResult);
    writeBuffer(GI_RT_PREVIEW_BINDING_DIRECT_LIGHT, m_HitRadianceBuffer);
    writeImage(GI_RT_PREVIEW_BINDING_RAY_SUMMARY, m_RayTracingResult);
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
    commandBuffer->DispatchCompute(
        *m_GIRTPreviewShader,
        (m_RayTracingGridDimensions.x + 7u) / 8u,
        (m_RayTracingGridDimensions.y + 7u) / 8u,
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
        m_GIVisibilityUpdateDescriptorSets);

    m_GIVisibilityDescriptorSetIsDirty = true;
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
    if (!m_RTResourcesReady || m_HitPositionCalculateDone)
    {
        return;
    }

    m_HitPositionCalculateDone = true;

    BindRayTracingData(device, scene);

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
    commandBuffer->BindRayTracingDescriptorSets(*vansPipeline, 0, m_RayTracingDescriptorSets);
    commandBuffer->UpdateRayTracingPushConstants(
        *vansPipeline,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR,
        0, 
        m_VansRayTracingShader->GetPushConstantSize(),
        &m_RayTracingConstant);

    commandBuffer->TraceRays(
        *vansPipeline,
        m_RayTracingGridDimensions.x, m_RayTracingGridDimensions.y, m_RayTracingGridDimensions.z);

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

    if (!m_GIVisibilityCalculateDone && m_GIVisibilityUpdateShader != nullptr)
    {
        BindGIVisibilityData(scene->GetMaterialManager());

        const int totalProbeCount = static_cast<int>(
            m_RayTracingGridDimensions.x * m_RayTracingGridDimensions.y * m_RayTracingGridDimensions.z);
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
            { m_GIVisibilityUpdateDescriptorSets[0] },
            &m_RayTracingConstant, sizeof(m_RayTracingConstant));

        m_GIVisibilityCalculateDone = true;

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
        blasMeshCount,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };

    VkDescriptorSetLayoutBinding indexDataBuffer =
    {
        PassBinding::BUFFER_4,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        blasMeshCount,
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
        m_RayTracingDescriptorSets);
    
    m_RayTracingDescriptorSetIsDirty = true;
}

void VansGraphics::VansRayTracing::CreateGIPointLightDescriptorSets(VansVKDevice* device)
{
    VansDescriptorSetLayoutFactory::CreateAndAllocate_GIPointLight(
        m_GISamplePositionLightSetLayout,
        m_GISamplePositionLightDescriptorSets);

    m_GIPointLightDescriptorSetIsDirty = true;
}

void VansGraphics::VansRayTracing::BindRayTracingData(VansVKDevice* device, VansScene* scene)
{
    if (!m_RayTracingDescriptorSetIsDirty)
    {
        return;
    }
    m_RayTracingDescriptorSetIsDirty = false;

    VkAccelerationStructureKHR& tlas = scene->GetTopAS();
    std::vector<VansVKBuffer>& vertexBuffers = scene->GetBLASVertexBuffers();
    std::vector<VansVKBuffer>& indexBuffers = scene->GetBLASIndexBuffers();
    int blasMeshCount = vertexBuffers.size();

    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();
    descManager->WriteBufferDescriptor(
        m_RayTracingDescriptorSets[0],
        PassBinding::BUFFER_2,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_RayTracingHitPositionResult.GetNativeBuffer(),
            0,
            m_RayTracingHitPositionResult.GetBufferSize()
        }});
    descManager->WriteBufferDescriptor(
        m_RayTracingDescriptorSets[0],
        PassBinding::BUFFER_6,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_RayTracingHitNormalResult.GetNativeBuffer(),
            0,
            m_RayTracingHitNormalResult.GetBufferSize()
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
        m_RayTracingDescriptorSets[0],
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
        m_RayTracingDescriptorSets[0],
        PassBinding::BUFFER_4,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        blasIndexBufferInfos);

    descManager->WriteBufferDescriptor(
        m_RayTracingDescriptorSets[0],
        PassBinding::BUFFER_5,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_BLASInstanceBuffer.GetNativeBuffer(),
            0,
            m_BLASInstanceBuffer.GetBufferSize()
        }});

    descManager->WriteBufferDescriptor(
        m_RayTracingDescriptorSets[0],
        PassBinding::BUFFER_7,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_TLASInstanceTextureIndexBuffer.GetNativeBuffer(),
            0,
            m_TLASInstanceTextureIndexBuffer.GetBufferSize()
        }});
    descManager->WriteBufferDescriptor(
        m_RayTracingDescriptorSets[0],
        PassBinding::BUFFER_8,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_RayTracingHitAlbedoRoughnessResult.GetNativeBuffer(),
            0,
            m_RayTracingHitAlbedoRoughnessResult.GetBufferSize()
        }});
    descManager->WriteAccelerationStructureDescriptor(
        m_RayTracingDescriptorSets[0],
        RT_BINDING_TLAS,
        tlas);
    descManager->WriteImageDescriptor(
        m_RayTracingDescriptorSets[0],
        PassBinding::UAV_IMAGE_0,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{
            m_RayTracingResult->GetImage().GetSampler(),
            m_RayTracingResult->GetImage().GetImageView(),
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
        m_RayTracingDescriptorSets[0],
        GLOBAL_BINDING_BINDLESS_TEXTURES,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        bindlessTextureInfos);

    descManager->CommitDescriptorUpdates();
}

