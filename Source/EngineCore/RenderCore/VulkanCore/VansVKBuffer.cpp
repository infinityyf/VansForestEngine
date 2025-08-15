
#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKBuffer.h"
#include "VansVKMemoryManager.h"
#include <iostream>

bool VansVulkan::VansVKBuffer::CreatVulkanBuffer(VkDevice& logical_device, VkDeviceSize size, VkFormat format, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties)
{
	m_BufferSize = size;
	m_BufferFormat = format;

	VkBufferCreateInfo buffer_create_info = 
	{
		 VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		 nullptr,
		 0,
		 size,
		 usage,
		 VK_SHARING_MODE_EXCLUSIVE,
		 0,
		 nullptr
	};

	//sharing mode defined wheather the buffer can be access by multi queues multi familis
	//exclude : only by queues from one family at a single time
	//concurrent : by queues from multiple families at the same time, performance low

	VkResult result = vkCreateBuffer(logical_device, &buffer_create_info, nullptr, &m_VansVKBuffer);
	if (VK_SUCCESS != result) 
	{
		std::cout << "Could not create a buffer." << std::endl;
		return false;
	}

	//allocate menory and bind to buffer
	VkMemoryRequirements memory_requirements;
	vkGetBufferMemoryRequirements(logical_device, m_VansVKBuffer, &memory_requirements);

	m_VansVKBufferMemory = VK_NULL_HANDLE;

	bool needBufferAddressable = usage & VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
	//决定了buffer的访问类型
	bool allocateResult = VansVKMemoryManager::GetInstance()->AllocateMemory(memory_requirements, m_VansVKBufferMemory, memory_properties, needBufferAddressable);
	if (!allocateResult)
	{
		return false;
	}
	VkDeviceSize memory_offset = 0;
	VkDeviceSize memory_range = size;

	//bind memory to buffer
	if (VK_NULL_HANDLE == m_VansVKBufferMemory) 
	{
		std::cout << "Could not allocate memory for a buffer." << std::endl;
		return false;
	}
	result = vkBindBufferMemory(logical_device, m_VansVKBuffer, m_VansVKBufferMemory, 0);
	if (VK_SUCCESS != result) 
	{
		std::cout << "Could not bind memory object to a buffer." << std::endl;
		return false;
	}

	if (!needBufferAddressable)
	{
		//create buffer view: how shader to view the buffer data
		//set the format
		VkBufferViewCreateInfo buffer_view_create_info =
		{
			 VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
			 nullptr,
			 0,
			 m_VansVKBuffer,
			 format,
			 memory_offset,
			 memory_range
		};

		result = vkCreateBufferView(logical_device, &buffer_view_create_info, nullptr, &m_VansVKBufferView);
		if (VK_SUCCESS != result)
		{
			std::cout << "Could not creat buffer view." << std::endl;
			return false;
		}
	}
	
	return true;
}

void VansVulkan::VansVKBuffer::DestroyVulkanBuffer(VkDevice& logical_device)
{
	if (VK_NULL_HANDLE != m_VansVKBufferView) 
	{
		vkDestroyBufferView(logical_device, m_VansVKBufferView, nullptr);
		m_VansVKBufferView = VK_NULL_HANDLE;
	}

	if (VK_NULL_HANDLE != m_VansVKBuffer) 
	{
		vkDestroyBuffer(logical_device, m_VansVKBuffer, nullptr);
		m_VansVKBuffer = VK_NULL_HANDLE;
	}

	VansVKMemoryManager::GetInstance()->FreeMemory(m_VansVKBufferMemory);
}

void VansVulkan::VansVKBuffer::AddTransitionBufferAccess(BufferTransition& transition)
{
	//creat barrier
	m_BufferMemoryBarriers.push_back(
		{
		 VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
		 nullptr,
		 transition.CurrentAccess,
		 transition.NewAccess,
		 transition.CurrentQueueFamily,
		 transition.NewQueueFamily,
		 transition.Buffer,
		 0,
		 VK_WHOLE_SIZE
		}
	);
}

void VansVulkan::VansVKBuffer::SetBufferMemoryBarrier(VkPipelineStageFlags generating_stages, VkPipelineStageFlags consuming_stages, BufferTransition bufferTransition)
{
	this->AddTransitionBufferAccess(bufferTransition);
	VansVKMemoryManager::GetInstance()->SetBufferMemoryBarrier(m_BufferMemoryBarriers, generating_stages, consuming_stages);
	m_BufferMemoryBarriers.clear();

}

bool VansVulkan::VansVKBuffer::SetBufferData(void* data, int offset, int size)
{
	return VansVKMemoryManager::GetInstance()->MapMemoryFromHost(m_VansVKBufferMemory, offset, size,data,true);
}


