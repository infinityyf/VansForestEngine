#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansRayTracing.h"
#include "../../RenderCore/VulkanCore/VansMesh.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansVKCommandBuffer.h"
#include "../../RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include "../../RenderCore/VulkanCore/VansRenderPass.h"
#include "../../RenderCore/BRDFData/VansLight.h"
#include "../../RenderCore/VansScene.h"
#include "../../RenderCore//VansMaterial.h"

#include <iostream>
//void VansVulkan::VansRayTracing::BuildBottomLevelAS(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansMesh* mesh)
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
//void VansVulkan::VansRayTracing::BuildTopLevelAS(VansVKDevice* device, VansVKCommandBuffer* commandBuffer)
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

void VansVulkan::VansRayTracing::CreateRayTracingResource(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene)
{
    int blasMeshCount = scene->GetBLASVertexBuffers().size();
    std::vector<uint32_t>& instanceData = scene->GetTLASInstanceData();

    //ray tracing参数
    m_RayTracingPositionCount = 80;
    m_RayTracingPositionStride = 1.0f;
    m_RayCountPerSample = 64;

    m_VansRayTracingShader.InitRayTracingShader(device->GetLogicDevice(), "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/RayTracingTest");
    m_VansRayTracingShader.SetPushConstant(sizeof(m_RayTracingConstant));
    m_VansRayTracingShader.SetPushConstantData(&(m_RayTracingConstant));
    
    m_RayTracingResult.InitTextureWithoutData(*commandBuffer, m_RayTracingPositionCount, m_RayTracingPositionCount, m_RayTracingPositionCount, 4, false, false, true, MID_PRES_16);
   
    VansMaterialManager* materialManager = scene->GetMaterialManager();
    materialManager->m_SHRResult = new VansTexture();
    materialManager->m_SHGResult = new VansTexture();
    materialManager->m_SHBResult = new VansTexture();
    materialManager->m_SHRResult->InitTextureWithoutData(*commandBuffer, m_RayTracingPositionCount, m_RayTracingPositionCount, m_RayTracingPositionCount, 4, false, false, true, HIGH_PRES_32);
    materialManager->m_SHGResult->InitTextureWithoutData(*commandBuffer, m_RayTracingPositionCount, m_RayTracingPositionCount, m_RayTracingPositionCount, 4, false, false, true, HIGH_PRES_32);
    materialManager->m_SHBResult->InitTextureWithoutData(*commandBuffer, m_RayTracingPositionCount, m_RayTracingPositionCount, m_RayTracingPositionCount, 4, false, false, true, HIGH_PRES_32);

    //提前生成pipeline
    CreateRayTraceDescriptorSets(device, blasMeshCount);
    m_VansRayTracingShader.GetRayTracingPipeline(device, { m_RayTracingSetLayout });

    //创建instance data的buffer
    m_BLASInstanceBuffer.CreatVulkanBuffer(device->GetLogicDevice(), 
        instanceData.size() * sizeof(uint32_t),
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );
    m_BLASInstanceBuffer.SetBufferData(instanceData.data(), 0, instanceData.size() * sizeof(uint32_t));

    //命中点法线和位置
    m_RayTracingHitPositionResult.CreatVulkanBuffer(device->GetLogicDevice(),
        m_RayCountPerSample * m_RayTracingPositionCount * m_RayTracingPositionCount * m_RayTracingPositionCount * sizeof(float) * 4,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );
    m_RayTracingHitNormalResult.CreatVulkanBuffer(device->GetLogicDevice(),
        m_RayCountPerSample * m_RayTracingPositionCount * m_RayTracingPositionCount * m_RayTracingPositionCount * sizeof(float) * 4,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    m_HitPointDirectLightBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
        m_RayCountPerSample * m_RayTracingPositionCount * m_RayTracingPositionCount * m_RayTracingPositionCount * sizeof(float) * 4,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    m_HitPointIndirectLightBuffer.CreatVulkanBuffer(device->GetLogicDevice(),
        m_RayCountPerSample * m_RayTracingPositionCount * m_RayTracingPositionCount * m_RayTracingPositionCount * sizeof(float) * 4,
        VK_FORMAT_R32G32B32A32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
    );

    m_RayTracingPointLighting = new VansComputeShader();
    m_RayTracingPointLighting->InitShader(device->GetLogicDevice(), "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/GIPointLight");
    m_RayTracingPointLighting->SetPushConstant(sizeof(m_RayTracingConstant));
    m_RayTracingPointLighting->SetPushConstantData(&(m_RayTracingConstant));


    //创建cs的set
    CreateGIPointLightDescriptorSets(device);

    m_GISHUpdateShader = new VansComputeShader();
    m_GISHUpdateShader->InitShader(device->GetLogicDevice(), "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/GISHUpdate");
    m_GISHUpdateShader->SetPushConstant(sizeof(m_RayTracingConstant));
    m_GISHUpdateShader->SetPushConstantData(&(m_RayTracingConstant));

    CreateGISHUpdateDescriptorSets(device);

}

void VansVulkan::VansRayTracing::UpdateGIProbe(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansLightManager* lightManager, VansMaterialManager* materialManager)
{
    BindGIPointLightData();

    BindGISHData(materialManager);

    commandBuffer->EnsureComputeShader(*m_RayTracingPointLighting, { lightManager->m_LightDataDescriptorSetLayout, m_GISamplePositionLightSetLayout});
    commandBuffer->DispatchCompute(
        *m_RayTracingPointLighting, 
        m_RayTracingPositionCount, 
        m_RayTracingPositionCount, 
        m_RayTracingPositionCount,
        { lightManager->m_LightDataDescriptorSets[0], m_GISamplePositionLightDescriptorSets[0]});

    commandBuffer->EnsureComputeShader(*m_GISHUpdateShader, {m_GISHUpdateSetLayout});
    commandBuffer->DispatchCompute(
        *m_GISHUpdateShader,
        m_RayTracingPositionCount,
        m_RayTracingPositionCount,
        m_RayTracingPositionCount,
        { m_GISHUpdateDescriptorSets[0] });
}

void VansVulkan::VansRayTracing::BindGIPointLightData()
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
            VansVKDescriptorManager::m_Buffer0SetBinding,
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
            VansVKDescriptorManager::m_Buffer1SetBinding,
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
            VansVKDescriptorManager::m_Buffer2SetBinding,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_HitPointDirectLightBuffer.GetNativeBuffer(),
                    0,
                    m_HitPointDirectLightBuffer.GetBufferSize()
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_GISamplePositionLightDescriptorSets[0],
            VansVKDescriptorManager::m_Buffer3SetBinding,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_HitPointIndirectLightBuffer.GetNativeBuffer(),
                    0,
                    m_HitPointIndirectLightBuffer.GetBufferSize()
                }
            }
        }
    );

    VansMaterialManager* manager = m_Scene->GetMaterialManager();
    auto& skyImage = manager->m_PreConvSpecular->GetImage();
    //设置天空盒
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_GISamplePositionLightDescriptorSets[0],
            VansVKDescriptorManager::m_SampleTexture4SetBinding,
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

    auto* rCoeffTexture = manager->m_SHRResult;
    auto* gCoeffTexture = manager->m_SHGResult;
    auto* bCoeffTexture = manager->m_SHBResult;

    //设置球谐积分贴图
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_GISamplePositionLightDescriptorSets[0],
            VansVKDescriptorManager::m_SampleTexture5SetBinding,
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
            VansVKDescriptorManager::m_SampleTexture6SetBinding,
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
            VansVKDescriptorManager::m_SampleTexture7SetBinding,
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
            VansVKDescriptorManager::m_SampleTexture8SetBinding,
            0,
            VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            {
                {
                    VansRenderPassManager::GetInstance()->GetShadowMap().GetSampler(),
                    VansRenderPassManager::GetInstance()->GetShadowMap().GetImageView(),
                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_GISamplePositionLightDescriptorSets[0],
            VansVKDescriptorManager::m_SampleTexture9SetBinding,
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

void VansVulkan::VansRayTracing::BindGISHData(VansMaterialManager* materialManager)
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
            VansVKDescriptorManager::m_Buffer0SetBinding,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_HitPointDirectLightBuffer.GetNativeBuffer(),
                    0,
                    m_HitPointDirectLightBuffer.GetBufferSize()
                }
            }
        }
    );
    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_GISHUpdateDescriptorSets[0],
            VansVKDescriptorManager::m_Buffer1SetBinding,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            {
                {
                    m_HitPointIndirectLightBuffer.GetNativeBuffer(),
                    0,
                    m_HitPointIndirectLightBuffer.GetBufferSize()
                }
            }
        }
    );

    auto* rCoeffTexture = materialManager->m_SHRResult;
    auto* gCoeffTexture = materialManager->m_SHGResult;
    auto* bCoeffTexture = materialManager->m_SHBResult;
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_GISHUpdateDescriptorSets[0],
            VansVKDescriptorManager::m_UAVTexture1SetBinding,
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
            VansVKDescriptorManager::m_UAVTexture2SetBinding,
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
            VansVKDescriptorManager::m_UAVTexture3SetBinding,
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

void VansVulkan::VansRayTracing::DispatchRayTracing(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansScene* scene)
{
    m_RayTracingConstant.dispatchParams = glm::vec4(
        m_RayTracingPositionCount,
        m_RayTracingPositionStride,
        m_RayCountPerSample,
        0
        );

    BindRayTracingData(device, scene);

    VansVKRayTracingPipeline* vansPipeline = m_VansRayTracingShader.GetRayTracingPipeline(device, { m_RayTracingSetLayout });


    vkCmdBindPipeline(commandBuffer->GetVKCommandBuffer(), VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, 
        vansPipeline->m_RayTracingPipeline);
    
    vkCmdBindDescriptorSets(commandBuffer->GetVKCommandBuffer(), 
        VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR, 
        vansPipeline->m_RayTracingLayout,
        0,
        1, m_RayTracingDescriptorSets.data(), 0, nullptr);

    vkCmdPushConstants(commandBuffer->GetVKCommandBuffer(), 
        vansPipeline->m_RayTracingLayout,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR | VK_SHADER_STAGE_MISS_BIT_KHR,
        0, 
        m_VansRayTracingShader.GetPushConstantSize(),
        m_VansRayTracingShader.GetPushConstantData());

    auto& rGenRegion = vansPipeline->m_RaygenShaderBindingTable;
    auto& rMissRegion = vansPipeline->m_MissShaderBindingTable;
    auto& rHitRegion = vansPipeline->m_HitShaderBindingTable;
    auto& rCallRegion = vansPipeline->m_CallableShaderBindingTable;

    vkCmdTraceRaysKHR(commandBuffer->GetVKCommandBuffer(), 
        &rGenRegion, &rMissRegion, &rHitRegion, &rCallRegion,
        m_RayTracingPositionCount, m_RayTracingPositionCount, m_RayTracingPositionCount);
}

void VansVulkan::VansRayTracing::CreateRayTraceDescriptorSets(VansVKDevice* device, int blasMeshCount)
{
    VkDescriptorSetLayoutBinding tlasBinding =
    {
        VansVKDescriptorManager::m_Tlas0Binding,
        VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };
    VkDescriptorSetLayoutBinding resultBinding =
    {
        VansVKDescriptorManager::m_UAVTexture0SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };
    VkDescriptorSetLayoutBinding hitPositionResultBinding =
    {
        VansVKDescriptorManager::m_Buffer2SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };
    VkDescriptorSetLayoutBinding hitNormalResultBinding =
    {
        VansVKDescriptorManager::m_Buffer6SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };

    //blas data buffer
    VkDescriptorSetLayoutBinding vertexDataBuffer =
    {
        VansVKDescriptorManager::m_Buffer3SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        blasMeshCount,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };

    VkDescriptorSetLayoutBinding indexDataBuffer =
    {
        VansVKDescriptorManager::m_Buffer4SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        blasMeshCount,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
        nullptr
    };

    //instance data buffer
    VkDescriptorSetLayoutBinding instanceDataBuffer =
    {
        VansVKDescriptorManager::m_Buffer5SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR | VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR,
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
            hitNormalResultBinding
        }, 
        m_RayTracingSetLayout);
    VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_RayTracingSetLayout }, m_RayTracingDescriptorSets);
    
    m_RayTracingDescriptorSetIsDirty = true;
}

void VansVulkan::VansRayTracing::CreateGIPointLightDescriptorSets(VansVKDevice* device)
{
    //光源信息在light manager里面已经创建过了

    //采样点位置ibuffer
    VkDescriptorSetLayoutBinding hitPosition =
    {
        VansVKDescriptorManager::m_Buffer0SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };
    //采样点法线
    VkDescriptorSetLayoutBinding hitNormal =
    {
        VansVKDescriptorManager::m_Buffer1SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //直接光buffer
    VkDescriptorSetLayoutBinding directResult =
    {
        VansVKDescriptorManager::m_Buffer2SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //间接光buffer
    VkDescriptorSetLayoutBinding indirectResult =
    {
        VansVKDescriptorManager::m_Buffer3SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //环境天空盒
    VkDescriptorSetLayoutBinding environmentMap =
    {
        VansVKDescriptorManager::m_SampleTexture4SetBinding,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //RGB SH
    VkDescriptorSetLayoutBinding SHRChannel =
    {
        VansVKDescriptorManager::m_SampleTexture5SetBinding,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };
    VkDescriptorSetLayoutBinding SHGChannel =
    {
        VansVKDescriptorManager::m_SampleTexture6SetBinding,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };
    VkDescriptorSetLayoutBinding SHBChannel =
    {
        VansVKDescriptorManager::m_SampleTexture7SetBinding,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };
    VkDescriptorSetLayoutBinding mainLightShadowMap =
    {
        VansVKDescriptorManager::m_SampleTexture8SetBinding,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };
    VkDescriptorSetLayoutBinding punctualLightShadowMap =
    {
        VansVKDescriptorManager::m_SampleTexture9SetBinding,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout(
        {
            hitPosition,
            hitNormal,
            directResult,
            indirectResult,
            environmentMap,
            SHRChannel,
            SHGChannel,
            SHBChannel,
            mainLightShadowMap,
            punctualLightShadowMap
        }, 
        m_GISamplePositionLightSetLayout);
    VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_GISamplePositionLightSetLayout }, m_GISamplePositionLightDescriptorSets);

    m_GIPointLightDescriptorSetIsDirty = true;
}

void VansVulkan::VansRayTracing::CreateGISHUpdateDescriptorSets(VansVKDevice* device)
{
    //直接光buffer
    VkDescriptorSetLayoutBinding directResult =
    {
        VansVKDescriptorManager::m_Buffer0SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //间接光buffer
    VkDescriptorSetLayoutBinding indirectResult =
    {
        VansVKDescriptorManager::m_Buffer1SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //R通道系数
    VkDescriptorSetLayoutBinding rSHResult =
    {
        VansVKDescriptorManager::m_UAVTexture1SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //g通道系数
    VkDescriptorSetLayoutBinding gSHResult =
    {
        VansVKDescriptorManager::m_UAVTexture2SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    //b通道系数
    VkDescriptorSetLayoutBinding bSHResult =
    {
        VansVKDescriptorManager::m_UAVTexture3SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        1,
        VK_SHADER_STAGE_COMPUTE_BIT,
        nullptr
    };

    VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ directResult,indirectResult,rSHResult,gSHResult,bSHResult }, m_GISHUpdateSetLayout);
    VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_GISHUpdateSetLayout }, m_GISHUpdateDescriptorSets);

    m_GISHUpdateDesctiproeSetIsDirty = true;
}

void VansVulkan::VansRayTracing::BindRayTracingData(VansVKDevice* device, VansScene* scene)
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
            VansVKDescriptorManager::m_Buffer2SetBinding,
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
            VansVKDescriptorManager::m_Buffer6SetBinding,
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
            VansVKDescriptorManager::m_Buffer3SetBinding,
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
            VansVKDescriptorManager::m_Buffer4SetBinding,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            blasIndexBufferInfos
        }
    );


    VansVKDescriptorManager::GetInstance()->m_BufferDescInfos.push_back(
        {
            m_RayTracingDescriptorSets[0],
            VansVKDescriptorManager::m_Buffer5SetBinding,
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


    VansVKDescriptorManager::GetInstance()->m_RayTraceASInfos.push_back(
        {
            m_RayTracingDescriptorSets[0],
            VansVKDescriptorManager::m_Tlas0Binding,
            0,
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            tlas
        }
    );
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.push_back(
        {
            m_RayTracingDescriptorSets[0],
            VansVKDescriptorManager::m_UAVTexture0SetBinding,
            0,
            VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            {
                {
                    m_RayTracingResult.GetImage().GetSampler(),
                    m_RayTracingResult.GetImage().GetImageView(),
                    VK_IMAGE_LAYOUT_GENERAL
                }
            }
        }
    );

    VansVKDescriptorManager::GetInstance()->UpdateDescriptorSets();
}
