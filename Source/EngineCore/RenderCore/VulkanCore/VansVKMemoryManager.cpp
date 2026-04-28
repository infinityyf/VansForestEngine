#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKMemoryManager.h"
#include "VansVKBuffer.h"
#include "VansVKImage.h"
#include "VansVKCommandBuffer.h"
#include "VansVKDevice.h"
#include "../../Util/VansLog.h"
#include <algorithm>
#include <iostream>

VansGraphics::VansVKMemoryManager* VansGraphics::VansVKMemoryManager::instance = nullptr;

void VansGraphics::VansVKMemoryManager::BindDevice(VkCommandBuffer& commandBuffer, VansVKDevice& device)
{
	m_PhysicalDevice = device.GetPhysicalDevice();
	m_LogicalDevice = device.GetLogicDevice();
	m_CommandBuffer = commandBuffer;
	m_DeviceProperties = device.GetDeviceProperties();
	m_Device = &device;
	vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &m_MemoryProperties);
}

const std::vector<uint32_t>& VansGraphics::VansVKMemoryManager::GetSharingQueueFamilyIndices() const
{
	return m_Device->GetSharingQueueFamilyIndices();
}

//内存barrier详解 https://zhuanlan.zhihu.com/p/161619652
//vkCmdpipelineBarrier只是提供了一个显式的依赖执行的约束，而memory的依赖需要memory barrier，传给vkCmdpipelineBarrier
//否则不能解决缓存可见性问题
//内存管理（layout比较直观的讲解）https://zhuanlan.zhihu.com/p/166387973
void VansGraphics::VansVKMemoryManager::SetBufferMemoryBarrier(
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

void VansGraphics::VansVKMemoryManager::SetImageMemoryBarrier(
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

VansGraphics::VansVKMemoryManager::VansVKMemoryManager()
{

}

void VansGraphics::VansVKMemoryManager::CopyBufferData(VansVKCommandBuffer& command_buffer, VansVKBuffer& source_buffer, VansVKBuffer& dest_buffer, const std::vector<VkBufferCopy>& regions)
{
	if (regions.size() > 0)
	{
		vkCmdCopyBuffer(command_buffer.m_VansVKCommandBuffer, source_buffer.m_VansVKBuffer, dest_buffer.m_VansVKBuffer, static_cast<uint32_t>(regions.size()), &regions[0]);
	}
}

void VansGraphics::VansVKMemoryManager::CopyBufferToImage(VansVKCommandBuffer& command_buffer, VansVKBuffer& source_buffer, VansVKImage& dest_image, VkImageLayout layout, const std::vector<VkBufferImageCopy>& regions)
{
	if (regions.size() > 0) 
	{
		vkCmdCopyBufferToImage(command_buffer.m_VansVKCommandBuffer, source_buffer.m_VansVKBuffer, dest_image.m_VansVKImage, layout, static_cast<uint32_t>(regions.size()), &regions[0]);
	}
}

void VansGraphics::VansVKMemoryManager::CopyImageToBuffer(VansVKCommandBuffer& command_buffer, VansVKImage& source_image, VansVKBuffer& dest_buffer, VkImageLayout layout, const std::vector<VkBufferImageCopy>& regions)
{
	if (regions.size() > 0) 
	{
		vkCmdCopyImageToBuffer(command_buffer.m_VansVKCommandBuffer, source_image.m_VansVKImage, layout, dest_buffer.m_VansVKBuffer, static_cast<uint32_t>(regions.size()), &regions[0]);
	}
}
