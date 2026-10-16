#include "VansVKGIReceiverVisibility.h"
#include "../GICore/VansGIMemoryPublication.h"
#include "../GICore/VansGISettings.h"
#include "VansVKDevice.h"
#include "VansVKDescriptorManager.h"
#include "../VansShaderManager.h"
#include "../../Util/VansLog.h"
#include <cstdlib>
#include <stdexcept>

void VansGraphics::VansVKGIReceiverVisibility::Initialize(VansVKDevice& device, VansVKCommandBuffer& command, uint32_t width, uint32_t height)
{
    m_MemoryPublication = device.GetGIMemoryPublication();
    auto& receiver = *this;
    VkDevice logicalDevice = device.GetLogicDevice();
    const auto usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    const VkDeviceSize bytes = 16 + VkDeviceSize(width) * height * sizeof(VansGIReceiverVisibility::Record);
    if (!command.BeginCommandBufferRecord(VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
        throw std::runtime_error("Failed to begin GI receiver visibility initialization");
    for (auto* buffer : {&receiver.current, &receiver.history, &receiver.world, &receiver.bias})
    {
        const VkDeviceSize allocation = buffer == &receiver.bias ? 16 + VkDeviceSize(width) * height * 32 :
            (buffer == &receiver.world ? VansGIReceiverVisibility::WorldBytes : bytes);
        if (!buffer->CreatVulkanBuffer(logicalDevice, allocation, VK_FORMAT_R32_UINT, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
            throw std::runtime_error("Failed to allocate GI receiver visibility history");
        command.FillBuffer(buffer->GetNativeBuffer(), 0, allocation, 0u);
    }
    VkMemoryBarrier initialized{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    initialized.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    initialized.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    command.PipelineBarrier(VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_RAY_TRACING_SHADER_BIT_KHR,
        {initialized});
    if (!command.EndCommandBufferRecord() ||
        !VansVKCommandBuffer::SubmitCommands(device.GetGraphicsQueue(), logicalDevice,
            {command.GetVKCommandBuffer()}, {}, {}, command.m_CommandBufferFinishSubmitFence) ||
        !command.ResetCommandBuffer(false))
        throw std::runtime_error("Failed to initialize GI receiver visibility history");
    if (!receiver.work.CreatVulkanBuffer(logicalDevice, VansGIReceiverVisibility::WorkBytes, VK_FORMAT_R32_UINT,
        usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) throw std::runtime_error("Failed to allocate GI receiver visibility work");
    if (!receiver.anchors.CreatVulkanBuffer(logicalDevice, VkDeviceSize(width) * height * 4, VK_FORMAT_R32_UINT,
        usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) ||
        !receiver.worldClaims.CreatVulkanBuffer(logicalDevice, VkDeviceSize(VansGIReceiverVisibility::WorldCapacity) * 4,
            VK_FORMAT_R32_UINT, usage, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        throw std::runtime_error("Failed to allocate GI receiver visibility cache indices");
    const VkShaderStageFlags stages = VK_SHADER_STAGE_COMPUTE_BIT | VK_SHADER_STAGE_RAYGEN_BIT_KHR;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
    for (uint32_t i = 0; i < 4; ++i) bindings.push_back({i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, stages, nullptr});
    bindings.push_back({4, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, stages, nullptr});
    for (uint32_t i : {6u, 7u, 8u, 9u, 10u, 11u, 12u, 13u, 14u, 15u})
        bindings.push_back({i, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, (i == 7u || i == 8u) ? kGIMaximumRegionCount : 1u, stages, nullptr});
	if (!VansDescriptorSetLayoutFactory::CreateAndAllocate_Custom(
		bindings, receiver.layout, receiver.sets, VansCommandRecordingConfig::MaximumFramesInFlight))
        throw std::runtime_error("Failed to create GI receiver visibility descriptors");
    receiver.prepareShader = VansShaderManager::Get().FindComputeShader("GIReceiverVisibilityPrepare");
    receiver.reprojectShader = VansShaderManager::Get().FindComputeShader("GIReceiverVisibilityReproject");
    receiver.worldShader = VansShaderManager::Get().FindComputeShader("GIReceiverVisibilityWorldCache");
    if (!receiver.prepareShader || !receiver.reprojectShader || !receiver.worldShader)
        throw std::runtime_error("GI receiver visibility shaders are unavailable");
    receiver.state.frame = 0;
    receiver.enabled = std::getenv("FOREST_DISABLE_GI_RECEIVER_VISIBILITY") == nullptr;
    VANS_LOG("[GIReceiverVisibility] cache=" << width << "x" << height << ", rayBudget=" << VansGIReceiverVisibility::RayBudget
        << ", historyBytes=" << bytes * 2 << ", worldBytes=" << VansGIReceiverVisibility::WorldBytes
        << ", anchorBytes=" << receiver.anchors.GetBufferSize() << ", enabled=" << receiver.enabled);
    RefreshMemory();
}

void VansGraphics::VansVKGIReceiverVisibility::Destroy(VkDevice device)
{
    auto* descriptors = VansVKDescriptorManager::GetInstance();
    descriptors->DestroyDescriptorSet(sets);
    descriptors->ReleaseDescriptorSetLayout(layout);
    for (auto* buffer : {&bias, &current, &history, &work, &anchors, &world, &worldClaims})
        buffer->DestroyVulkanBuffer(device);
    state.frame = 0;
    RefreshMemory();
    m_MemoryPublication.reset();
}

void VansGraphics::VansVKGIReceiverVisibility::RefreshMemory() const
{
    if (!m_MemoryPublication) return;
    VansGIMemoryUsage usage;
    for (const auto* buffer : {&bias, &current, &history, &work, &anchors, &world, &worldClaims})
    {
        usage.bufferCapacityBytes += buffer->GetBufferSize();
        usage.bufferAllocationBytes += buffer->GetAllocationSize();
    }
    m_MemoryPublication->PublishConsumer(&VansGIMemorySnapshot::receiver, usage);
}
