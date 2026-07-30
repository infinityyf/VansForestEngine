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

void VansGraphics::VansVKMemoryManager::BindDevice(VansVKDevice& device)
{
	m_PhysicalDevice = device.GetPhysicalDevice();
	m_LogicalDevice = device.GetLogicDevice();
	m_DeviceProperties = device.GetDeviceProperties();
	m_Device = &device;
	VansGraphics::vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &m_MemoryProperties);
}

const std::vector<uint32_t>& VansGraphics::VansVKMemoryManager::GetSharingQueueFamilyIndices() const
{
	return m_Device->GetSharingQueueFamilyIndices();
}

VansGraphics::VansVKMemoryManager::VansVKMemoryManager()
{

}

void VansGraphics::VansVKMemoryManager::CopyBufferData(VansVKCommandBuffer& command_buffer, VansVKBuffer& source_buffer, VansVKBuffer& dest_buffer, const std::vector<VkBufferCopy>& regions)
{
	if (regions.size() > 0)
	{
		VansGraphics::vkCmdCopyBuffer(command_buffer.m_VansVKCommandBuffer, source_buffer.m_VansVKBuffer, dest_buffer.m_VansVKBuffer, static_cast<uint32_t>(regions.size()), &regions[0]);
	}
}

void VansGraphics::VansVKMemoryManager::CopyBufferToImage(VansVKCommandBuffer& command_buffer, VansVKBuffer& source_buffer, VansVKImage& dest_image, VkImageLayout layout, const std::vector<VkBufferImageCopy>& regions)
{
	if (regions.size() > 0) 
	{
		VansGraphics::vkCmdCopyBufferToImage(command_buffer.m_VansVKCommandBuffer, source_buffer.m_VansVKBuffer, dest_image.m_VansVKImage, layout, static_cast<uint32_t>(regions.size()), &regions[0]);
	}
}

void VansGraphics::VansVKMemoryManager::CopyImageToBuffer(VansVKCommandBuffer& command_buffer, VansVKImage& source_image, VansVKBuffer& dest_buffer, VkImageLayout layout, const std::vector<VkBufferImageCopy>& regions)
{
	if (regions.size() > 0) 
	{
		VansGraphics::vkCmdCopyImageToBuffer(command_buffer.m_VansVKCommandBuffer, source_image.m_VansVKImage, layout, dest_buffer.m_VansVKBuffer, static_cast<uint32_t>(regions.size()), &regions[0]);
	}
}
