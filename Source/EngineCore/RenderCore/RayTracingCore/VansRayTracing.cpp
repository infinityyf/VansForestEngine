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
#include "../../Util/VansLog.h"

#include <iostream>
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

	descMgr->DestroyDescriptorSet(m_GISHUpdateDescriptorSets);
	descMgr->DestroyDescriptorSetLayout(m_GISHUpdateSetLayout);

	// 释放 RT 相关 buffer
	m_RayTracingHitPositionResult.DestroyVulkanBuffer(device);
	m_RayTracingHitNormalResult.DestroyVulkanBuffer(device);
	m_RayTracingHitAlbedoRoughnessResult.DestroyVulkanBuffer(device);
	m_BLASInstanceBuffer.DestroyVulkanBuffer(device);
	m_TLASInstanceTextureIndexBuffer.DestroyVulkanBuffer(device);
	m_ReSTIRBuffer.DestroyVulkanBuffer(device);
	m_HitPointLightBuffer.DestroyVulkanBuffer(device);

	// 释放场景加载时 new 出来的 RT 渲染资源，防止切换场景时泄漏
	delete m_RayTracingResult;
	m_RayTracingResult = nullptr;

	delete m_RayTracingPointLighting;
	m_RayTracingPointLighting = nullptr;

	delete m_GISHUpdateShader;
	m_GISHUpdateShader = nullptr;

	// 重置 RT 着色器的 pipeline / SBT，下次 CreateRayTracingResource 将重建
	m_VansRayTracingShader.CleanupPipeline();

	m_ReSTIRCPUData.clear();

	// 标记脏以便下次 CreateRayTracingResource 重新绑定
	m_RayTracingDescriptorSetIsDirty = true;
	m_GIPointLightDescriptorSetIsDirty = true;
	m_GISHUpdateDesctiproeSetIsDirty = true;
	m_HitPositionCalculateDone = false;
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
    m_RayTracingPositionCount = static_cast<int>(gi.gridSize);
    m_RayTracingPositionStride = gi.probeSpacing;
    m_RayCountPerSample = static_cast<int>(gi.raysPerProbe);

    m_RayTracingConstant.dispatchParams = glm::vec4(
        static_cast<float>(gi.gridSize), gi.probeSpacing,
        static_cast<float>(gi.raysPerProbe), gi.maxRayDistance);
    m_RayTracingConstant.frameParams = glm::vec4(
        0.0f, static_cast<float>(gi.spatialUpdateDivisor),
        static_cast<float>(gi.directionUpdateSlices), 0.0f);
    m_RayTracingConstant.regionParams = glm::vec4(gi.regionCenter, gi.normalBias);
    m_RayTracingConstant.lightingParams = glm::vec4(
        gi.environmentIntensity, gi.maxIndirectRadiance, gi.maxSHL0, gi.temporalBlend);

    auto vansConfigration = VansConfigration::GetInstance();
    std::string projectRoot = vansConfigration->GetProjectRootPath();
    m_VansRayTracingShader.InitRayTracingShader(device->GetLogicDevice(), (projectRoot + "EngineAssets/Shaders/RayTracingTest").c_str());
    m_VansRayTracingShader.SetPushConstant(sizeof(m_RayTracingConstant));
    m_VansRayTracingShader.SetPushConstantData(&(m_RayTracingConstant));
    
    m_RayTracingResult = new VansTexture();
    m_RayTracingResult->InitTextureWithoutData(*commandBuffer, m_RayTracingPositionCount, m_RayTracingPositionCount, m_RayTracingPositionCount, 4, false, false, true, MID_PRES_16);
   
    VansMaterialManager* materialManager = scene->GetMaterialManager();
    VansTexture* shRResult = new VansTexture();
    shRResult->InitTextureWithoutData(*commandBuffer, m_RayTracingPositionCount, m_RayTracingPositionCount, m_RayTracingPositionCount, 4, false, false, true, HIGH_PRES_32);
    materialManager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT, shRResult);

    VansTexture* shGResult = new VansTexture();
    shGResult->InitTextureWithoutData(*commandBuffer, m_RayTracingPositionCount, m_RayTracingPositionCount, m_RayTracingPositionCount, 4, false, false, true, HIGH_PRES_32);
    materialManager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT, shGResult);

    VansTexture* shBResult = new VansTexture();
    shBResult->InitTextureWithoutData(*commandBuffer, m_RayTracingPositionCount, m_RayTracingPositionCount, m_RayTracingPositionCount, 4, false, false, true, HIGH_PRES_32);
    materialManager->RegisterRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT, shBResult);

    //提前生成pipeline
    CreateRayTraceDescriptorSets(device, blasMeshCount);
    m_VansRayTracingShader.GetRayTracingPipeline(device, { m_RayTracingSetLayout });

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
        gi.gridSize * gi.gridSize * gi.gridSize * sizeof(uint16_t) * 4;

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

    m_HitPointLightBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
        giHitBufferSize,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    //缓存的有效索引
    /*m_ReSTIRBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
        (sizeof(ResTIRStruct)) * m_RayTracingPositionCount * m_RayTracingPositionCount * m_RayTracingPositionCount,
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
	);
    for (int i = 0; i < m_RayTracingPositionCount * m_RayTracingPositionCount * m_RayTracingPositionCount; i++)
    {
        auto& newdata = m_ReSTIRCPUData.emplace_back();
        newdata.state = glm::vec4(0, 0, 0, 0);
        newdata.radiance = glm::vec4(0, 0, 0, 0);
    }*/
    //m_ReSTIRBuffer.SetBufferData(m_ReSTIRCPUData.data(), 0, (sizeof(ResTIRStruct)) * m_RayTracingPositionCount * m_RayTracingPositionCount * m_RayTracingPositionCount);

    m_RayTracingPointLighting = new VansComputeShader();
    m_RayTracingPointLighting->InitShader(device->GetLogicDevice(), (projectRoot + "EngineAssets/Shaders/GIPointLight").c_str());
    m_RayTracingPointLighting->SetPushConstant(sizeof(m_RayTracingConstant));
    m_RayTracingPointLighting->SetPushConstantData(&(m_RayTracingConstant));


    //创建cs的set
    CreateGIPointLightDescriptorSets(device);

    m_GISHUpdateShader = new VansComputeShader();
    m_GISHUpdateShader->InitShader(device->GetLogicDevice(), (projectRoot + "EngineAssets/Shaders/GISHUpdate").c_str());
    m_GISHUpdateShader->SetPushConstant(sizeof(m_RayTracingConstant));
    m_GISHUpdateShader->SetPushConstantData(&(m_RayTracingConstant));

    CreateGISHUpdateDescriptorSets(device);

    m_HitPositionCalculateDone = false;

    m_GIUpdateFrameIndex = 0;
}

void VansGraphics::VansRayTracing::UpdateGIProbe(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansLightManager* lightManager, VansMaterialManager* materialManager)
{
    if (!m_RTResourcesReady)
        return;

    BindGIPointLightData();

    BindGISHData(materialManager);

    m_RayTracingConstant.frameParams.x = static_cast<float>(m_GIUpdateFrameIndex++);

    const uint32_t updateDivisor = static_cast<uint32_t>(m_RayTracingConstant.frameParams.y);
    const uint32_t probesPerAxis = (static_cast<uint32_t>(m_RayTracingPositionCount) + updateDivisor - 1u) / updateDivisor;
    const uint32_t groupCount = (probesPerAxis + 3u) / 4u;

    // GI point-light evaluation remains independent of reflection probes.
    commandBuffer->EnsureComputeShader(*m_RayTracingPointLighting, { m_Scene->GetGlobalDescriptorSetLayout(), m_GISamplePositionLightSetLayout});
    commandBuffer->DispatchCompute(
        *m_RayTracingPointLighting, 
        groupCount,
        groupCount,
        groupCount,
        { m_Scene->GetGlobalDescriptorSet(), m_GISamplePositionLightDescriptorSets[0]});

    // GIPointLight 写入 m_HitPointLightBuffer，GISHUpdate 将读取该缓冲区。
    // 插入 compute→compute 内存屏障防止 RAW 冒险。
    {
        VkMemoryBarrier computeBarrier{};
        computeBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        computeBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        computeBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        commandBuffer->PipelineBarrier(
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            { computeBarrier });
    }

    commandBuffer->EnsureComputeShader(*m_GISHUpdateShader, { m_Scene->GetGlobalDescriptorSetLayout(), m_GISHUpdateSetLayout});
    commandBuffer->DispatchCompute(
        *m_GISHUpdateShader,
        groupCount,
        groupCount,
        groupCount,
        { m_Scene->GetGlobalDescriptorSet(), m_GISHUpdateDescriptorSets[0] });
}

void VansGraphics::VansRayTracing::BindGIPointLightData()
{
    if (!m_GIPointLightDescriptorSetIsDirty)
    {
        return;
    }

    VansMaterialManager* manager = m_Scene->GetMaterialManager();
    auto* rCoeffTexture = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT);
    auto* gCoeffTexture = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT);
    auto* bCoeffTexture = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT);

    if (rCoeffTexture == nullptr || gCoeffTexture == nullptr || bCoeffTexture == nullptr)
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
        GIPL_BINDING_DIRECT_LIGHT,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_HitPointLightBuffer.GetNativeBuffer(),
            0,
            m_HitPointLightBuffer.GetBufferSize()
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
            rCoeffTexture->GetImage().GetSampler(),
            rCoeffTexture->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->WriteImageDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
        GIPL_BINDING_SH_G,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            gCoeffTexture->GetImage().GetSampler(),
            gCoeffTexture->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->WriteImageDescriptor(
        m_GISamplePositionLightDescriptorSets[0],
        GIPL_BINDING_SH_B,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        {{
            bCoeffTexture->GetImage().GetSampler(),
            bCoeffTexture->GetImage().GetImageView(),
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

    descManager->CommitDescriptorUpdates();
    m_GIPointLightDescriptorSetIsDirty = false;
}

void VansGraphics::VansRayTracing::BindGISHData(VansMaterialManager* materialManager)
{
    if (!m_GISHUpdateDesctiproeSetIsDirty)
    {
        return;
    }

    auto* rCoeffTexture = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT);
    auto* gCoeffTexture = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT);
    auto* bCoeffTexture = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT);
    if (rCoeffTexture == nullptr || gCoeffTexture == nullptr || bCoeffTexture == nullptr)
    {
        return;
    }

    auto* descManager = VansVKDescriptorManager::GetInstance();
    descManager->BeginDescriptorUpdate();
    descManager->WriteBufferDescriptor(
        m_GISHUpdateDescriptorSets[0],
        GISH_BINDING_DIRECT_LIGHT,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        {{
            m_HitPointLightBuffer.GetNativeBuffer(),
            0,
            m_HitPointLightBuffer.GetBufferSize()
        }});
    descManager->WriteImageDescriptor(
        m_GISHUpdateDescriptorSets[0],
        GISH_BINDING_RESULT_R,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{
            rCoeffTexture->GetImage().GetSampler(),
            rCoeffTexture->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->WriteImageDescriptor(
        m_GISHUpdateDescriptorSets[0],
        GISH_BINDING_RESULT_G,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{
            gCoeffTexture->GetImage().GetSampler(),
            gCoeffTexture->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->WriteImageDescriptor(
        m_GISHUpdateDescriptorSets[0],
        GISH_BINDING_RESULT_B,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        {{
            bCoeffTexture->GetImage().GetSampler(),
            bCoeffTexture->GetImage().GetImageView(),
            VK_IMAGE_LAYOUT_GENERAL
        }});
    descManager->CommitDescriptorUpdates();
    m_GISHUpdateDesctiproeSetIsDirty = false;
}

void VansGraphics::VansRayTracing::DispatchRayTracing(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene)
{
    if (!m_RTResourcesReady || m_HitPositionCalculateDone)
    {
        return;
    }

    m_HitPositionCalculateDone = true;

    BindRayTracingData(device, scene);

    VansVKRayTracingPipeline* vansPipeline = m_VansRayTracingShader.GetRayTracingPipeline(device, { m_RayTracingSetLayout });

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
        m_VansRayTracingShader.GetPushConstantSize(),
        m_VansRayTracingShader.GetPushConstantData());

    commandBuffer->TraceRays(
        *vansPipeline,
        m_RayTracingPositionCount, m_RayTracingPositionCount, m_RayTracingPositionCount);

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

void VansGraphics::VansRayTracing::CreateGISHUpdateDescriptorSets(VansVKDevice* device)
{
    VansDescriptorSetLayoutFactory::CreateAndAllocate_GISHUpdate(
        m_GISHUpdateSetLayout,
        m_GISHUpdateDescriptorSets);

    m_GISHUpdateDesctiproeSetIsDirty = true;
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

