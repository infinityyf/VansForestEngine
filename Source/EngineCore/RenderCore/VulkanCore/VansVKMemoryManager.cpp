#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKMemoryManager.h"
#include "VansVKBuffer.h"
#include "VansVKImage.h"
#include "VansVKCommandBuffer.h"
#include "VansVKDevice.h"
#include <iostream>

VansVulkan::VansVKMemoryManager* VansVulkan::VansVKMemoryManager::instance = nullptr;

bool VansVulkan::VansVKMemoryManager::AllocateMemory(VkMemoryRequirements& requires, VkDeviceMemory& memory, VkMemoryPropertyFlags memory_properties)
{
	for (uint32_t type = 0; type < m_MemoryProperties.memoryTypeCount; ++type) 
	{
		if ((requires.memoryTypeBits & (1 << type)) &&
			((m_MemoryProperties.memoryTypes[type].propertyFlags & memory_properties) == memory_properties)) 
		{
			VkMemoryAllocateInfo memory_allocate_info = 
			{
				VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
				nullptr,
				requires.size,
				type
			};
			VkResult result = vkAllocateMemory(m_LogicalDevice, &memory_allocate_info, nullptr, &memory);
			if (VK_SUCCESS == result) 
			{
				return true;
			}
		}
	}
	return false;
}

void VansVulkan::VansVKMemoryManager::BindDevice(VkCommandBuffer& commandBuffer, VansVKDevice& device)
{
	m_PhysicalDevice = device.GetPhysicalDevice();
	m_LogicalDevice = device.GetLogicDevice();
	m_CommandBuffer = commandBuffer;
	m_DeviceProperties = device.GetDeviceProperties();
	vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &m_MemoryProperties);
}

void VansVulkan::VansVKMemoryManager::FreeMemory(VkDeviceMemory& memory)
{
	if (VK_NULL_HANDLE != memory)
	{
		vkFreeMemory(m_LogicalDevice, memory, nullptr);
		memory = VK_NULL_HANDLE;
	}
}

//内存barrier详解 https://zhuanlan.zhihu.com/p/161619652
//vkCmdpipelineBarrier只是提供了一个显式的依赖执行的约束，而memory的依赖需要memory barrier，传给vkCmdpipelineBarrier
//否则不能解决缓存可见性问题
//内存管理（layout比较直观的讲解）https://zhuanlan.zhihu.com/p/166387973
void VansVulkan::VansVKMemoryManager::SetBufferMemoryBarrier(
	std::vector<VkBufferMemoryBarrier>& bufferMemoryBarriers, 
	VkPipelineStageFlags generating_stages,
	VkPipelineStageFlags consuming_stages)
{
	if (bufferMemoryBarriers.size() > 0)
	{
		vkCmdPipelineBarrier(m_CommandBuffer, generating_stages,
			consuming_stages, 0, 0, nullptr,
			static_cast<uint32_t>(bufferMemoryBarriers.size()),
			&bufferMemoryBarriers[0], 0, nullptr);
	}
}

void VansVulkan::VansVKMemoryManager::SetImageMemoryBarrier(
	std::vector<VkImageMemoryBarrier>& imageMemoryBarriers,
	VkPipelineStageFlags generating_stages,
	VkPipelineStageFlags consuming_stages)
{
	if (imageMemoryBarriers.size() > 0)
	{
		vkCmdPipelineBarrier(m_CommandBuffer, generating_stages,
			consuming_stages, 0, 0, nullptr,
			0, nullptr,
			static_cast<uint32_t>(imageMemoryBarriers.size()),
			&imageMemoryBarriers[0]);
	}
}

bool VansVulkan::VansVKMemoryManager::MapMemoryFromHost(VkDeviceMemory& memory, VkDeviceSize offset, VkDeviceSize size, void* host_data, bool upmap_immediate)
{
	VkResult result;
	void* local_pointer;
	result = vkMapMemory(m_LogicalDevice, memory, offset, size, 0, &local_pointer);
	if (VK_SUCCESS != result) 
	{
		std::cout << "Could not map memory object." << std::endl;
		return false;
	}

	std::memcpy(local_pointer, host_data, size);

	//inform driver that memory has been modified
	//注意，如果是usageVK_MEMORY_PROPERTY_HOST_COHERENT_BIT,就可以不用flush
	//VkDeviceSize nonCoherentAtomSize = m_DeviceProperties.limits.nonCoherentAtomSize;
	//VkDeviceSize alignedSize = AlignMemorySizeTo(size, nonCoherentAtomSize);
	//std::vector<VkMappedMemoryRange> memory_ranges = 
	//{
	//	 {
	//		 VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE,
	//		 nullptr,
	//		 memory,
	//		 offset,
	//		 size
	//	 }
	//};
	//vkFlushMappedMemoryRanges(m_LogicalDevice, static_cast<uint32_t>(memory_ranges.size()), &memory_ranges[0]);
	if (VK_SUCCESS != result) 
	{
		std::cout << "Could not flush mapped memory." << std::endl;
		return false;
	}

	if (upmap_immediate)
	{
		vkUnmapMemory(m_LogicalDevice, memory);
	}

	return true;
}

VansVulkan::VansVKMemoryManager::VansVKMemoryManager()
{

}

void VansVulkan::VansVKMemoryManager::CopyBufferData(VansVKCommandBuffer& command_buffer, VansVKBuffer& source_buffer, VansVKBuffer& dest_buffer, const std::vector<VkBufferCopy>& regions)
{
	if (regions.size() > 0)
	{
		vkCmdCopyBuffer(command_buffer.m_VansVKCommandBuffer, source_buffer.m_VansVKBuffer, dest_buffer.m_VansVKBuffer, static_cast<uint32_t>(regions.size()), &regions[0]);
	}
}

void VansVulkan::VansVKMemoryManager::CopyBufferToImage(VansVKCommandBuffer& command_buffer, VansVKBuffer& source_buffer, VansVKImage& dest_image, VkImageLayout layout, const std::vector<VkBufferImageCopy>& regions)
{
	if (regions.size() > 0) 
	{
		vkCmdCopyBufferToImage(command_buffer.m_VansVKCommandBuffer, source_buffer.m_VansVKBuffer, dest_image.m_VansVKImage, layout, static_cast<uint32_t>(regions.size()), &regions[0]);
	}
}

void VansVulkan::VansVKMemoryManager::CopyImageToBuffer(VansVKCommandBuffer& command_buffer, VansVKImage& source_image, VansVKBuffer& dest_buffer, VkImageLayout layout, const std::vector<VkBufferImageCopy>& regions)
{
	if (regions.size() > 0) 
	{
		vkCmdCopyImageToBuffer(command_buffer.m_VansVKCommandBuffer, source_image.m_VansVKImage, layout, dest_buffer.m_VansVKBuffer, static_cast<uint32_t>(regions.size()), &regions[0]);
	}
}
