#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansRayTracing.h"
#include "../../RenderCore/VulkanCore/VansMesh.h"
#include "../../RenderCore/VulkanCore/VansVKDevice.h"
#include "../../RenderCore/VulkanCore/VansVKCommandBuffer.h"
#include "../../RenderCore/VulkanCore/VansVKDescriptorManager.h"
#include <iostream>
void VansVulkan::VansRayTracing::BuildBottomLevelAS(VansVKDevice* device, VansVKCommandBuffer* commandBuffer, VansMesh* mesh)
{
    // 获取顶点缓冲区地址
    VkBufferDeviceAddressInfo addressInfo{};
    addressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    addressInfo.buffer = mesh->m_VertexPositionBuffer.GetNativeBuffer();
    addressInfo.pNext = nullptr;
    VkDeviceAddress vertexBufferAddress = vkGetBufferDeviceAddressKHR(device->GetLogicDevice(), &addressInfo);
    
    addressInfo.buffer = mesh->m_IndexBuffer.GetNativeBuffer();
    VkDeviceAddress indexBufferAddress = vkGetBufferDeviceAddressKHR(device->GetLogicDevice(), &addressInfo);
    
    // 定义几何数据
    VkAccelerationStructureGeometryTrianglesDataKHR triangles{};
    triangles.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_TRIANGLES_DATA_KHR;
    triangles.vertexFormat = VK_FORMAT_R32G32B32_SFLOAT;
    triangles.vertexData.deviceAddress = vertexBufferAddress;
    triangles.vertexStride = sizeof(float) * 3;
    triangles.maxVertex = mesh->GetMeshVertexCount() - 1;
    triangles.indexType = VK_INDEX_TYPE_UINT32;
    triangles.indexData.deviceAddress = indexBufferAddress;
    triangles.transformData.deviceAddress = 0;

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_TRIANGLES_KHR;
    geometry.geometry.triangles = triangles;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    

    // 计算构建大小
    VkAccelerationStructureBuildGeometryInfoKHR buildGeometryInfo{};
    buildGeometryInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildGeometryInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;
    buildGeometryInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildGeometryInfo.geometryCount = 1;
    buildGeometryInfo.pGeometries = &geometry;

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.primitiveCount = mesh->GetIndexCount() / 3;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.transformOffset = 0;

    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{};
    buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(device->GetLogicDevice(),
        VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR, &buildGeometryInfo, &buildGeometryInfo.geometryCount, &buildSizesInfo);

    //给blas创建buffer
    m_BottomLevelASBuffer.CreatVulkanBuffer(
        device->GetLogicDevice(),
        buildSizesInfo.accelerationStructureSize,
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    VkAccelerationStructureCreateInfoKHR accelCreateInfo = {};
    accelCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelCreateInfo.buffer = m_BottomLevelASBuffer.GetNativeBuffer();
    accelCreateInfo.size = buildSizesInfo.accelerationStructureSize;
    accelCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL_KHR;

    vkCreateAccelerationStructureKHR(device->GetLogicDevice(), &accelCreateInfo, nullptr, &m_BottomLevelAS);

    buildGeometryInfo.dstAccelerationStructure = m_BottomLevelAS;

    VansVKBuffer scratchBuffer;
    scratchBuffer.CreatVulkanBuffer(
        device->GetLogicDevice(),
        buildSizesInfo.buildScratchSize,
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    
    VkBufferDeviceAddressInfo bufferAddressInfo;
    bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferAddressInfo.buffer = scratchBuffer.GetNativeBuffer();
    bufferAddressInfo.pNext = nullptr;
    buildGeometryInfo.scratchData.deviceAddress = vkGetBufferDeviceAddressKHR(device->GetLogicDevice(), &bufferAddressInfo);

    //创建加速结构
    const VkAccelerationStructureBuildRangeInfoKHR* pRangeInfo = &buildRangeInfo;
    vkCmdBuildAccelerationStructuresKHR(commandBuffer->GetVKCommandBuffer(), 1, &buildGeometryInfo, &pRangeInfo);

}

void VansVulkan::VansRayTracing::BuildTopLevelAS(VansVKDevice* device, VansVKCommandBuffer* commandBuffer)
{
    // 创建实例缓冲区
    VkAccelerationStructureInstanceKHR instance{};
    instance.transform = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };
    instance.instanceCustomIndex = 0;
    instance.mask = 0xFF;
    instance.instanceShaderBindingTableRecordOffset = 0;
    instance.flags = VK_GEOMETRY_INSTANCE_TRIANGLE_FACING_CULL_DISABLE_BIT_KHR;

    // 获取BLAS地址
    VkAccelerationStructureDeviceAddressInfoKHR asAddressInfo{};
    asAddressInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_DEVICE_ADDRESS_INFO_KHR;
    asAddressInfo.accelerationStructure = m_BottomLevelAS;
    instance.accelerationStructureReference = vkGetAccelerationStructureDeviceAddressKHR(device->GetLogicDevice(), &asAddressInfo);

    // 创建实例缓冲区
    m_InstanceBuffer.CreatVulkanBuffer(
        device->GetLogicDevice(),
        sizeof(VkAccelerationStructureInstanceKHR),
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_BUILD_INPUT_READ_ONLY_BIT_KHR |
        VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    // 复制实例数据
    m_InstanceBuffer.SetBufferData(&instance, 0, m_InstanceBuffer.GetBufferSize());

    // 关联每个instance的geometry
    VkAccelerationStructureGeometryInstancesDataKHR instancesData{};
    instancesData.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_INSTANCES_DATA_KHR;
    instancesData.arrayOfPointers = VK_FALSE;

    VkBufferDeviceAddressInfo bufferAddressInfo;
    bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferAddressInfo.buffer = m_InstanceBuffer.GetNativeBuffer();
    bufferAddressInfo.pNext = nullptr;
    instancesData.data.deviceAddress = vkGetBufferDeviceAddressKHR(device->GetLogicDevice(), &bufferAddressInfo);

    VkAccelerationStructureGeometryKHR geometry{};
    geometry.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_GEOMETRY_KHR;
    geometry.geometryType = VK_GEOMETRY_TYPE_INSTANCES_KHR;
    geometry.geometry.instances = instancesData;
    geometry.flags = VK_GEOMETRY_OPAQUE_BIT_KHR;

    VkAccelerationStructureBuildGeometryInfoKHR buildInfo{};
    buildInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_GEOMETRY_INFO_KHR;
    buildInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;
    buildInfo.flags = VK_BUILD_ACCELERATION_STRUCTURE_PREFER_FAST_TRACE_BIT_KHR;
    buildInfo.geometryCount = 1;
    buildInfo.pGeometries = &geometry;
    buildInfo.mode = VK_BUILD_ACCELERATION_STRUCTURE_MODE_BUILD_KHR;

    VkAccelerationStructureBuildRangeInfoKHR buildRangeInfo{};
    buildRangeInfo.primitiveCount = 1;
    buildRangeInfo.primitiveOffset = 0;
    buildRangeInfo.firstVertex = 0;
    buildRangeInfo.transformOffset = 0;
    const VkAccelerationStructureBuildRangeInfoKHR* pBuildRangeInfos = &buildRangeInfo;

    VkAccelerationStructureBuildSizesInfoKHR buildSizesInfo{};
    buildSizesInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_BUILD_SIZES_INFO_KHR;
    vkGetAccelerationStructureBuildSizesKHR(device->GetLogicDevice(), VK_ACCELERATION_STRUCTURE_BUILD_TYPE_DEVICE_KHR,
        &buildInfo, &buildInfo.geometryCount, &buildSizesInfo);
    
    
    // 创建缓冲区
    m_TopLevelASBuffer.CreatVulkanBuffer(
        device->GetLogicDevice(),
        buildSizesInfo.accelerationStructureSize,
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT | VK_BUFFER_USAGE_ACCELERATION_STRUCTURE_STORAGE_BIT_KHR,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    // 构建TLAS
    VkAccelerationStructureCreateInfoKHR accelCreateInfo = {};
    accelCreateInfo.sType = VK_STRUCTURE_TYPE_ACCELERATION_STRUCTURE_CREATE_INFO_KHR;
    accelCreateInfo.buffer = m_TopLevelASBuffer.GetNativeBuffer();
    accelCreateInfo.size = buildSizesInfo.accelerationStructureSize;
    accelCreateInfo.type = VK_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL_KHR;

    vkCreateAccelerationStructureKHR(device->GetLogicDevice(), &accelCreateInfo, nullptr, &m_TopLevelAS);

    buildInfo.dstAccelerationStructure = m_TopLevelAS;

    VansVKBuffer scratchBuffer;
    scratchBuffer.CreatVulkanBuffer(
        device->GetLogicDevice(),
        buildSizesInfo.buildScratchSize,
        VK_FORMAT_R32_SFLOAT,
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    bufferAddressInfo.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    bufferAddressInfo.buffer = scratchBuffer.GetNativeBuffer();
    bufferAddressInfo.pNext = nullptr;
    buildInfo.scratchData.deviceAddress = vkGetBufferDeviceAddressKHR(device->GetLogicDevice(), &bufferAddressInfo);

    vkCmdBuildAccelerationStructuresKHR(commandBuffer->GetVKCommandBuffer(), 1, &buildInfo, &pBuildRangeInfos);
}

void VansVulkan::VansRayTracing::CreateRayTracingResource(VansVKDevice* device, VansVKCommandBuffer* commandBuffer)
{
    m_VansRayTracingShader.InitRayTracingShader(device->GetLogicDevice(), "C:/Users/infinityyf/Projects/ForestEngine/ForestEngine/ForestEngine/EngineAssets/Shaders/RayTracingTest");
    m_VansRayTracingShader.SetPushConstant(sizeof(m_RayTracingConstant));
    m_VansRayTracingShader.SetPushConstantData(&(m_RayTracingConstant));
    
    m_RayTracingResult.InitTextureWithoutData(*commandBuffer, 500, 500, 4, false, false, true, MID_PRES_16);
    
    //提前生成pipeline
    CreateDescriptorSets(device);
    m_VansRayTracingShader.GetRayTracingPipeline(device, { m_RayTracingSetLayout });
}

void VansVulkan::VansRayTracing::DispatchRayTracing(VansVKDevice* device, VansVKCommandBuffer* commandBuffer)
{
    BindRayTracingData(device);

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
        500, 500, 1);
}

void VansVulkan::VansRayTracing::CreateDescriptorSets(VansVKDevice* device)
{
    VkDescriptorSetLayoutBinding tlasBinding =
    {
        VansVKDescriptorManager::m_Tlas0Binding,
        VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR,
        nullptr
    };
    VkDescriptorSetLayoutBinding resultBinding =
    {
        VansVKDescriptorManager::m_UAVTexture0SetBinding,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        1,
        VK_SHADER_STAGE_RAYGEN_BIT_KHR,
        nullptr
    };

    VansVKDescriptorManager::GetInstance()->CreateDesciptorSetLayout({ tlasBinding,resultBinding }, m_RayTracingSetLayout);
    VansVKDescriptorManager::GetInstance()->AllocateDescriptorSet({ m_RayTracingSetLayout }, m_RayTracingDescriptorSets);
}

void VansVulkan::VansRayTracing::BindRayTracingData(VansVKDevice* device)
{
    if (!m_DescriptorSetIsDirty)
    {
        return;
    }
    m_DescriptorSetIsDirty = false;
    VansVKDescriptorManager::GetInstance()->ResetState();
    VansVKDescriptorManager::GetInstance()->m_RayTraceASInfos.push_back(
        {
            m_RayTracingDescriptorSets[0],
            VansVKDescriptorManager::m_Tlas0Binding,
            0,
            VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR,
            m_TopLevelAS
        }
    );
    VansVKDescriptorManager::GetInstance()->m_ImageDescInfos.clear();
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
