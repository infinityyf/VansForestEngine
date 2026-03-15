#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansRayTracing.h"
#include "../../RenderCore/VulkanCore/VansMesh.h"
#include "../../RenderCore/VulkanCore/VansTexture.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansVKCommandBuffer.h"
#include "../../RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../../RenderCore/VulkanCore/VansRenderPass.h"
#include "../../RenderCore/BRDFData/VansLight.h"
#include "../../RenderCore/VansScene.h"
#include "../../Configration/VansConfigration.h"
#include "../../RenderCore//VansMaterial.h"
#include "../../Util/VansLog.h"

#include <iostream>
//void VansGraphics::VansRayTracing::BuildBottomLevelAS(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansMesh* mesh)
//{
//    // 获取顶点缓冲区地址
//    VkBufferDeviceAddressInfo addressInfo{};
//    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
//    addressInfo.buffer = mesh->m_VertexPositionBuffer.GetNativeBuffer();
//    addressInfo.pNext = nullptr;
//    VkDeviceAddress vertexBufferAddress = vkGetBufferDeviceAddressKHR(device->GetLogicDevice(), &addressInfo);
//    
//    addressInfo.buffer = mesh->m_IndexBuffer.GetNativeBuffer();
//    VkDeviceAddress indexBufferAddress = vkGetBufferDeviceAddressKHR(device->GetLogicDevice(), &addressInfo);
//    
//    // 定义几何数据
//    VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
//    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
//    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
//    triangles.vertexData.deviceAddress = vertexBufferAddress;
//    triangles.vertexStride = sizeof(float) * 3;
//    triangles.maxVertex = mesh->GetMeshVertexCount() - 1;
//    triangles.indexType = VK_INDEX_TYPE_UINT32;
//    triangles.indexData.deviceAddress = indexBufferAddress;
//    triangles.transformData.deviceAddress = 0;
//
//    VkAccelerationStructureGeometryKHR geometry{};
//    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
//    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
//    geometry.geometry.triangles = triangles;
//    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
//
//    
//
//    // 计算构建大小
//    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{};
//    buildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
//    buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
//    buildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
//    buildGeometryInfo.geometryCount = 1;
//    buildGeometryInfo.pGeometries = &geometry;
//
//    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
//    buildRangeInfo.firstVertex = 0;
//    buildRangeInfo.primitiveCount = mesh->GetIndexCount() / 3;
//    buildRangeInfo.primitiveOffset = 0;
//    buildRangeInfo.transformOffset = 0;
//
//    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{};
//    buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
//    vkGetAccelerationStructureBuildSizesKHR(device->GetLogicDevice(),
//        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildGeometryInfo, &buildGeometryInfo.geometryCount, &buildSizesInfo);
//
//    //给blas创建buffer
//    m_BottomLevelASBuffer.CreatVulkanBuffer(
//        device->GetLogicDevice(),
//        buildSizesInfo.accelerationStructureSize,
//        VK_FORMAT_R32_SFLOAT,
//        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
//        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
//    
//    VkAccelerationStructureCreateInfoKHR accelCreateInfo = {};
//    accelCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
//    accelCreateInfo.buffer = m_BottomLevelASBuffer.GetNativeBuffer();
//    accelCreateInfo.size = buildSizesInfo.accelerationStructureSize;
//    accelCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
//
//    vkCreateAccelerationStructureKHR(device->GetLogicDevice(), &accelCreateInfo, nullptr, &m_BottomLevelAS);
//
//    buildGeometryInfo.dstAccelerationStructure = m_BottomLevelAS;
//
//    VansVKBuffer scratchBuffer;
//    scratchBuffer.CreatVulkanBuffer(
//        device->GetLogicDevice(),
//        buildSizesInfo.buildScratchSize,
//        VK_FORMAT_R32_SFLOAT,
//        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
//        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
//    
//    VkBufferDeviceAddressInfo bufferAddressInfo;
//    bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
//    bufferAddressInfo.buffer = scratchBuffer.GetNativeBuffer();
//    bufferAddressInfo.pNext = nullptr;
//    buildGeometryInfo.scratchData.deviceAddress = vkGetBufferDeviceAddressKHR(device->GetLogicDevice(), &bufferAddressInfo);
//
//    //创建加速结构
//    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &buildRangeInfo;
//    vkCmdBuildAccelerationStructuresKHR(commandBuffer->GetVKCommandBuffer(), 1, &buildGeometryInfo, &pRangeInfo);
//
//}
//
//void VansGraphics::VansRayTracing::BuildTopLevelAS(VansVKDevice* device, VansVKCommandBuffer* commandBuffer)
//{
//    // 创建实例缓冲区
//    VkAccelerationStructureInstanceKHR instance{};
//    instance.transform = {
//        1.0f, 0.0f, 0.0f, 0.0f,
//        0.0f, 1.0f, 0.0f, 0.0f,
//        0.0f, 0.0f, 1.0f, 0.0f
//    };
//    instance.instanceCustomIndex = 0;
//    instance.mask = 0xFF;
//    instance.instanceShaderBindingTableRecordOffset = 0;
//    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;
//
//    // 获取BLAS地址
//    VkAccelerationStructureDeviceAddressInfoKHR asAddressInfo{};
//    asAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
//    asAddressInfo.accelerationStructure = m_BottomLevelAS;
//    instance.accelerationStructureReference = vkGetAccelerationStructureDeviceAddressKHR(device->GetLogicDevice(), &asAddressInfo);
//
//    // 创建实例缓冲区
//    m_InstanceBuffer.CreatVulkanBuffer(
//        device->GetLogicDevice(),
//        sizeof(VkAccelerationStructureInstanceKHR),
//        VK_FORMAT_R32_SFLOAT,
//        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
//        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
//        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
//
//    // 复制实例数据
//    m_InstanceBuffer.SetBufferData(&instance, 0, m_InstanceBuffer.GetBufferSize());
//
//    // 关联每个instance的geometry
//    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
//    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
//    instancesData.arrayOfPointers = VK_FALSE;
//
//    VkBufferDeviceAddressInfo bufferAddressInfo;
//    bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
//    bufferAddressInfo.buffer = m_InstanceBuffer.GetNativeBuffer();
//    bufferAddressInfo.pNext = nullptr;
//    instancesData.data.deviceAddress = vkGetBufferDeviceAddressKHR(device->GetLogicDevice(), &bufferAddressInfo);
//
//    VkAccelerationStructureGeometryKHR geometry{};
//    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
//    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
//    geometry.geometry.instances = instancesData;
//    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;
//
//    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
//    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
//    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
//    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
//    buildInfo.geometryCount = 1;
//    buildInfo.pGeometries = &geometry;
//    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;
//
//    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
//    buildRangeInfo.primitiveCount = 1;
//    buildRangeInfo.primitiveOffset = 0;
//    buildRangeInfo.firstVertex = 0;
//    buildRangeInfo.transformOffset = 0;
//    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfos = &buildRangeInfo;
//
//    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{};
//    buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
//    vkGetAccelerationStructureBuildSizesKHR(device->GetLogicDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
//        &buildInfo, &buildInfo.geometryCount, &buildSizesInfo);
//    
//    
//    // 创建缓冲区
//    m_TopLevelASBuffer.CreatVulkanBuffer(
//        device->GetLogicDevice(),
//        buildSizesInfo.accelerationStructureSize,
//        VK_FORMAT_R32_SFLOAT,
//        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
//        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
//
//    // 构建TLAS
//    VkAccelerationStructureCreateInfoKHR accelCreateInfo = {};
//    accelCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
//    accelCreateInfo.buffer = m_TopLevelASBuffer.GetNativeBuffer();
//    accelCreateInfo.size = buildSizesInfo.accelerationStructureSize;
//    accelCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
//
//    vkCreateAccelerationStructureKHR(device->GetLogicDevice(), &accelCreateInfo, nullptr, &m_TopLevelAS);
//
//    buildInfo.dstAccelerationStructure = m_TopLevelAS;
//
//    VansVKBuffer scratchBuffer;
//    scratchBuffer.CreatVulkanBuffer(
//        device->GetLogicDevice(),
//        buildSizesInfo.buildScratchSize,
//        VK_FORMAT_R32_SFLOAT,
//        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
//        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
//
//    bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
//    bufferAddressInfo.buffer = scratchBuffer.GetNativeBuffer();
//    bufferAddressInfo.pNext = nullptr;
//    buildInfo.scratchData.deviceAddress = vkGetBufferDeviceAddressKHR(device->GetLogicDevice(), &bufferAddressInfo);
//
//    vkCmdBuildAccelerationStructuresKHR(commandBuffer->GetVKCommandBuffer(), 1, &buildInfo, &pBuildRangeInfos);
//}

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

    //ray tracing参数
    m_RayTracingPositionCount = 80;
    m_RayTracingPositionStride = 0.5f;
    m_RayCountPerSample = 256;

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

    //命中点法线和位置
    m_RayTracingHitPositionResult.CreatVulkanBuffer(device->GetLogicDevice(),
        m_RayCountPerSample * m_RayTracingPositionCount * m_RayTracingPositionCount * m_RayTracingPositionCount * sizeof(float) * 2,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
    m_RayTracingHitNormalResult.CreatVulkanBuffer(device->GetLogicDevice(),
        m_RayCountPerSample * m_RayTracingPositionCount * m_RayTracingPositionCount * m_RayTracingPositionCount * sizeof(float) * 2,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );
    
    //命中点pbr
    m_RayTracingHitAlbedoRoughnessResult.CreatVulkanBuffer(device->GetLogicDevice(),
        m_RayCountPerSample * m_RayTracingPositionCount * m_RayTracingPositionCount * m_RayTracingPositionCount * sizeof(float) * 2,
        VK_FORMAT_R16G16B16A16_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    m_HitPointLightBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
        m_RayCountPerSample * m_RayTracingPositionCount * m_RayTracingPositionCount * m_RayTracingPositionCount * sizeof(float) * 2,
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

    m_RayTracingConstant.frameParams = glm::vec4(
        m_GIUpdateFrameIndex++,
        0,
        0,
        0
    );

    commandBuffer->EnsureComputeShader(*m_RayTracingPointLighting, { m_Scene->m_GlobalDescriptorSetLayout, m_GISamplePositionLightSetLayout});
    commandBuffer->DispatchCompute(
        *m_RayTracingPointLighting, 
        m_RayTracingPositionCount / 8, 
        m_RayTracingPositionCount / 8,
        m_RayTracingPositionCount / 8,
        { m_Scene->m_GlobalDescriptorSet, m_GISamplePositionLightDescriptorSets[0]});

    commandBuffer->EnsureComputeShader(*m_GISHUpdateShader, { m_Scene->m_GlobalDescriptorSetLayout, m_GISHUpdateSetLayout});
    commandBuffer->DispatchCompute(
        *m_GISHUpdateShader,
        m_RayTracingPositionCount / 8,
        m_RayTracingPositionCount / 8,
        m_RayTracingPositionCount / 8,
        { m_Scene->m_GlobalDescriptorSet, m_GISHUpdateDescriptorSets[0] });
}

void VansGraphics::VansRayTracing::BindGIPointLightData()
{
    if (!m_GIPointLightDescriptorSetIsDirty)
    {
        return;
    }
    m_GIPointLightDescriptorSetIsDirty = false;

    VansVKDescriptorManager::GetInstance()->ResetState();
    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_GISamplePositionLightDescriptorSets[0],
            PassBinding::BUFFER_0,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_RayTracingHitPositionResult.GetNativeBuffer(),
                    0,
                    m_RayTracingHitPositionResult.GetBufferSize()
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_GISamplePositionLightDescriptorSets[0],
            PassBinding::BUFFER_1,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_RayTracingHitNormalResult.GetNativeBuffer(),
                    0,
                    m_RayTracingHitNormalResult.GetBufferSize()
                }
            }
        }
    );

    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_GISamplePositionLightDescriptorSets[0],
            PassBinding::BUFFER_2,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_HitPointLightBuffer.GetNativeBuffer(),
                    0,
                    m_HitPointLightBuffer.GetBufferSize()
                }
            }
        }
    );

    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_GISamplePositionLightDescriptorSets[0],
            PassBinding::BUFFER_10,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_RayTracingHitAlbedoRoughnessResult.GetNativeBuffer(),
                    0,
                    m_RayTracingHitAlbedoRoughnessResult.GetBufferSize()
                }
            }
        }
    );

    VansMaterialManager* manager = m_Scene->GetMaterialManager();
    auto& skyImage = manager->m_PreConvDiffuse->GetImage();
    //设置天空盒
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_GISamplePositionLightDescriptorSets[0],
            PassBinding::TEXTURE_4,
            0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {
                {
                    skyImage.GetSampler(),
                    skyImage.GetImageView(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                }
            }
        }
    );

    auto* rCoeffTexture = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT);
    auto* gCoeffTexture = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT);
    auto* bCoeffTexture = manager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT);

    if (rCoeffTexture == nullptr || gCoeffTexture == nullptr || bCoeffTexture == nullptr)
    {
        return;
    }

    //设置球谐积分贴图
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_GISamplePositionLightDescriptorSets[0],
            PassBinding::TEXTURE_5,
            0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {
                {
                    rCoeffTexture->GetImage().GetSampler(),
                    rCoeffTexture->GetImage().GetImageView(),
                    VK_IMAGE_LAYOUT_GENERAL
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_GISamplePositionLightDescriptorSets[0],
            PassBinding::TEXTURE_6,
            0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {
                {
                    gCoeffTexture->GetImage().GetSampler(),
                    gCoeffTexture->GetImage().GetImageView(),
                    VK_IMAGE_LAYOUT_GENERAL
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_GISamplePositionLightDescriptorSets[0],
            PassBinding::TEXTURE_7,
            0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {
                {
                    bCoeffTexture->GetImage().GetSampler(),
                    bCoeffTexture->GetImage().GetImageView(),
                    VK_IMAGE_LAYOUT_GENERAL
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_GISamplePositionLightDescriptorSets[0],
            PassBinding::TEXTURE_8,
            0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {
                {
                    VansRenderPassManager::GetInstance()->GetCascadeShadowSampler(),
                    VansRenderPassManager::GetInstance()->GetCascadeShadowLayerView(1),  // matches RAYTRACING_CASCADE_INDEX in Common.glsl
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_GISamplePositionLightDescriptorSets[0],
            PassBinding::TEXTURE_9,
            0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {
                {
                    VansRenderPassManager::GetInstance()->GetPunctualShadowMap().GetSampler(),
                    VansRenderPassManager::GetInstance()->GetPunctualShadowMap().GetImageView(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                }
            }
        }
    );

    VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

void VansGraphics::VansRayTracing::BindGISHData(VansMaterialManager* materialManager)
{
    if (!m_GISHUpdateDesctiproeSetIsDirty)
    {
        return;
    }
    m_GISHUpdateDesctiproeSetIsDirty = false;

    VansVKDescriptorManager::GetInstance()->ResetState();
    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_GISHUpdateDescriptorSets[0],
            PassBinding::BUFFER_0,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_HitPointLightBuffer.GetNativeBuffer(),
                    0,
                    m_HitPointLightBuffer.GetBufferSize()
                }
            }
        }
    );

    auto* rCoeffTexture = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_R_RESULT);
    auto* gCoeffTexture = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_G_RESULT);
    auto* bCoeffTexture = materialManager->GetRuntimeRenderTexture(VansMaterialManager::RT_SH_B_RESULT);
    if (rCoeffTexture == nullptr || gCoeffTexture == nullptr || bCoeffTexture == nullptr)
    {
        return;
    }
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_GISHUpdateDescriptorSets[0],
            PassBinding::UAV_IMAGE_1,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            {
                {
                    rCoeffTexture->GetImage().GetSampler(),
                    rCoeffTexture->GetImage().GetImageView(),
                    VK_IMAGE_LAYOUT_GENERAL
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_GISHUpdateDescriptorSets[0],
            PassBinding::UAV_IMAGE_2,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            {
                {
                    gCoeffTexture->GetImage().GetSampler(),
                    gCoeffTexture->GetImage().GetImageView(),
                    VK_IMAGE_LAYOUT_GENERAL
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_GISHUpdateDescriptorSets[0],
            PassBinding::UAV_IMAGE_3,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            {
                {
                    bCoeffTexture->GetImage().GetSampler(),
                    bCoeffTexture->GetImage().GetImageView(),
                    VK_IMAGE_LAYOUT_GENERAL
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}

void VansGraphics::VansRayTracing::DispatchRayTracing(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene)
{
    if (!m_RTResourcesReady || m_HitPositionCalculateDone)
    {
        return;
    }

    m_HitPositionCalculateDone = true;

    m_RayTracingConstant.dispatchParams = glm::vec4(
        m_RayTracingPositionCount,
        m_RayTracingPositionStride,
        m_RayCountPerSample,
        0
        );

    BindRayTracingData(device, scene);

    VansVKRayTracingPipeline* vansPipeline = m_VansRayTracingShader.GetRayTracingPipeline(device, { m_RayTracingSetLayout });

    // --- Add explicit barriers before tracing ---
    VkCommandBuffer cmd = commandBuffer->GetVKCommandBuffer();

    // Make prior AS build/updates visible to RT stage (use a memory barrier)
    {
        VkMemoryBarrier mb{};
        mb.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        mb.srcAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_WRITE_BIT_KHR | VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR |
            VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT;
        mb.dstAccessMask = VK_ACCESS_ACCELERATION_STRUCTURE_READ_BIT_KHR | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_ACCELERATION_STRUCTURE_BUILD_BIT_KHR | VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
            VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
            0, 1, &mb, 0, nullptr, 0, nullptr);
    }

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR,
        vansPipeline->m_RayTracingPipeline);
    
    vkCmdBindDescriptorSets(cmd,
        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, 
        vansPipeline->m_RayTracingLayout,
        0,
        1, m_RayTracingDescriptorSets.data(), 0, nullptr);

    vkCmdPushConstants(cmd,
        vansPipeline->m_RayTracingLayout,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR,
        0, 
        m_VansRayTracingShader.GetPushConstantSize(),
        m_VansRayTracingShader.GetPushConstantData());

    auto& rGenRegion = vansPipeline->m_RaygenShaderBindingTable;
    auto& rMissRegion = vansPipeline->m_MissShaderBindingTable;
    auto& rHitRegion = vansPipeline->m_HitShaderBindingTable;
    auto& rCallRegion = vansPipeline->m_CallableShaderBindingTable;

    vkCmdTraceRaysKHR(cmd,
        &rGenRegion, &rMissRegion, &rHitRegion, &rCallRegion,
        m_RayTracingPositionCount, m_RayTracingPositionCount, m_RayTracingPositionCount);
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
    

	VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout(
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
        m_RayTracingSetLayout);
    VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_RayTracingSetLayout }, m_RayTracingDescriptorSets);
    
    m_RayTracingDescriptorSetIsDirty = true;
}

void VansGraphics::VansRayTracing::CreateGIPointLightDescriptorSets(VansVKDevice* device)
{
    //光源信息在light manager里面已经创建过了

    //采样点位置ibuffer
    VkDescriptorSetLayoutBinding hitPosition =
    {
        PassBinding::BUFFER_0,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };
    //采样点法线
    VkDescriptorSetLayoutBinding hitNormal =
    {
        PassBinding::BUFFER_1,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //直接光buffer
    VkDescriptorSetLayoutBinding directResult =
    {
        PassBinding::BUFFER_2,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //环境天空盒
    VkDescriptorSetLayoutBinding environmentMap =
    {
        PassBinding::TEXTURE_4,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //RGB SH
    VkDescriptorSetLayoutBinding SHRChannel =
    {
        PassBinding::TEXTURE_5,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };
    VkDescriptorSetLayoutBinding SHGChannel =
    {
        PassBinding::TEXTURE_6,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };
    VkDescriptorSetLayoutBinding SHBChannel =
    {
        PassBinding::TEXTURE_7,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };
    VkDescriptorSetLayoutBinding mainLightShadowMap =
    {
        PassBinding::TEXTURE_8,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };
    VkDescriptorSetLayoutBinding punctualLightShadowMap =
    {
        PassBinding::TEXTURE_9,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //命中点pbrbuffer
    VkDescriptorSetLayoutBinding pbrDataResult =
    {
        PassBinding::BUFFER_10,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

	//ReSTIR缓存buffer
    VkDescriptorSetLayoutBinding RESTIRDataResult =
    {
        PassBinding::BUFFER_11,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };


    VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout(
        {
            hitPosition,
            hitNormal,
            directResult,
            environmentMap,
            SHRChannel,
            SHGChannel,
            SHBChannel,
            mainLightShadowMap,
            punctualLightShadowMap,
            pbrDataResult,
        }, 
        m_GISamplePositionLightSetLayout);
    VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_GISamplePositionLightSetLayout }, m_GISamplePositionLightDescriptorSets);

    m_GIPointLightDescriptorSetIsDirty = true;
}

void VansGraphics::VansRayTracing::CreateGISHUpdateDescriptorSets(VansVKDevice* device)
{
    //直接光buffer
    VkDescriptorSetLayoutBinding directResult =
    {
        PassBinding::BUFFER_0,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //蓄水池buffer
    VkDescriptorSetLayoutBinding RESTIRDataResult =
    {
        PassBinding::BUFFER_5,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //R通道系数
    VkDescriptorSetLayoutBinding rSHResult =
    {
        PassBinding::UAV_IMAGE_1,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //g通道系数
    VkDescriptorSetLayoutBinding gSHResult =
    {
        PassBinding::UAV_IMAGE_2,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //b通道系数
    VkDescriptorSetLayoutBinding bSHResult =
    {
        PassBinding::UAV_IMAGE_3,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ directResult,rSHResult,gSHResult,bSHResult }, m_GISHUpdateSetLayout);
    VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_GISHUpdateSetLayout }, m_GISHUpdateDescriptorSets);

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

    VansVKDescriptorManager::GetInstance()->ResetState();
    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_RayTracingDescriptorSets[0],
            PassBinding::BUFFER_2,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_RayTracingHitPositionResult.GetNativeBuffer(),
                    0,
                    m_RayTracingHitPositionResult.GetBufferSize()
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_RayTracingDescriptorSets[0],
            PassBinding::BUFFER_6,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_RayTracingHitNormalResult.GetNativeBuffer(),
                    0,
                    m_RayTracingHitNormalResult.GetBufferSize()
                }
            }
        }
    );

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
    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_RayTracingDescriptorSets[0],
            PassBinding::BUFFER_3,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            blasVertexBufferInfos
        }
    );

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
    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_RayTracingDescriptorSets[0],
            PassBinding::BUFFER_4,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            blasIndexBufferInfos
        }
    );


    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_RayTracingDescriptorSets[0],
            PassBinding::BUFFER_5,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_BLASInstanceBuffer.GetNativeBuffer(),
                    0,
                    m_BLASInstanceBuffer.GetBufferSize()
                }
            }
        }
    );

    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_RayTracingDescriptorSets[0],
            PassBinding::BUFFER_7,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_TLASInstanceTextureIndexBuffer.GetNativeBuffer(),
                    0,
                    m_TLASInstanceTextureIndexBuffer.GetBufferSize()
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_RayTracingDescriptorSets[0],
            PassBinding::BUFFER_8,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_RayTracingHitAlbedoRoughnessResult.GetNativeBuffer(),
                    0,
                    m_RayTracingHitAlbedoRoughnessResult.GetBufferSize()
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->m_RayTraceASInfos.push_back(
        {
            m_RayTracingDescriptorSets[0],
            RT_BINDING_TLAS,
            0,
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            tlas
        }
    );
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_RayTracingDescriptorSets[0],
            PassBinding::UAV_IMAGE_0,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            {
                {
                    m_RayTracingResult->GetImage().GetSampler(),
                    m_RayTracingResult->GetImage().GetImageView(),
                    VK_IMAGE_LAYOUT_GENERAL
                }
            }
        }
    );

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
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_RayTracingDescriptorSets[0],
            GLOBAL_BINDING_BINDLESS_TEXTURES,
            0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            bindlessTextureInfos
        }
    );

    VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}
