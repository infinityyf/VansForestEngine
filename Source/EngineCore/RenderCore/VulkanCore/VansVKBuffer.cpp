#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKBuffer.h"
#include "VansVKMemoryManager.h"
#include "VansVKMemoryAllocator.h"
#include "../../Util/VansLog.h"
#include <iostream>

namespace
{
	// Translate the legacy VkMemoryPropertyFlags + buffer usage combo (which
	// is how callers historically expressed their intent) into a VansMemoryUsage
	// suitable for VMA. This bridge keeps the 30+ existing call sites unchanged.
	VansGraphics::VansMemoryUsage TranslateLegacyProps(
		VkMemoryPropertyFlags props, VkBufferUsageFlags usage)
	{
		const bool deviceLocal = (props & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) != 0;
		const bool hostVisible = (props & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0;

		if (hostVisible)
		{
			// Frequently-updated small buffers (UBO/SSBO) read by the GPU each frame
			// are best served by a persistent mapping.
			const bool persistentCandidate =
				(usage & (VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
				        | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
				        | VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT
				        | VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT)) != 0
				&& (usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) == 0;

			return persistentCandidate ? VansGraphics::VansMemoryUsage::PersistentUpload
			                           : VansGraphics::VansMemoryUsage::CpuToGpu;
		}
		(void)deviceLocal;
		return VansGraphics::VansMemoryUsage::GpuOnly;
	}
}

bool VansGraphics::VansVKBuffer::CreatVulkanBuffer(VkDevice& logical_device, VkDeviceSize size, VkFormat format, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties)
{
	m_BufferSize = size;
	m_BufferFormat = format;

	const auto& sharingFamilies = VansVKMemoryManager::GetInstance()->GetSharingQueueFamilyIndices();

	VkBufferCreateInfo buffer_create_info =
	{
		 VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		 nullptr,
		 0,
		 size,
		 usage,
		 VK_SHARING_MODE_CONCURRENT,
		 static_cast<uint32_t>(sharingFamilies.size()),
		 sharingFamilies.data()
	};

	const VansMemoryUsage vu = TranslateLegacyProps(memory_properties, usage);
	const bool wantPersistent = (vu == VansMemoryUsage::PersistentUpload);

	if (!VansVKMemoryAllocator::Get().CreateBuffer(
			buffer_create_info, vu, wantPersistent,
			m_VansVKBuffer, m_VansVKBufferAllocation, &m_MappedPtr))
	{
		VANS_LOG_ERROR("Could not create a buffer (VMA).");
		return false;
	}

	{
		// Buffer view: how shaders interpret the buffer data.
		VkBufferViewCreateInfo buffer_view_create_info =
		{
			 VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
			 nullptr,
			 0,
			 m_VansVKBuffer,
			 format,
			 0,
			 size
		};

		VkResult result = vkCreateBufferView(logical_device, &buffer_view_create_info, nullptr, &m_VansVKBufferView);
		if (VK_SUCCESS != result)
		{
			VANS_LOG_ERROR("Could not creat buffer view.");
			return false;
		}
	}

	return true;
}

void VansGraphics::VansVKBuffer::DestroyVulkanBuffer(VkDevice& logical_device)
{
	// Persistent mapping (if any) is released automatically by vmaDestroyBuffer.
	m_MappedPtr = nullptr;

	if (VK_NULL_HANDLE != m_VansVKBufferView)
	{
		vkDestroyBufferView(logical_device, m_VansVKBufferView, nullptr);
		m_VansVKBufferView = VK_NULL_HANDLE;
	}

	VansVKMemoryAllocator::Get().DestroyBuffer(m_VansVKBuffer, m_VansVKBufferAllocation);
}

void VansGraphics::VansVKBuffer::AddTransitionBufferAccess(BufferTransition& transition)
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

void VansGraphics::VansVKBuffer::SetBufferMemoryBarrier(VkPipelineStageFlags generating_stages, VkPipelineStageFlags consuming_stages, BufferTransition bufferTransition)
{
	this->AddTransitionBufferAccess(bufferTransition);
	VansVKMemoryManager::GetInstance()->SetBufferMemoryBarrier(m_BufferMemoryBarriers, generating_stages, consuming_stages);
	m_BufferMemoryBarriers.clear();

}

bool VansGraphics::VansVKBuffer::SetBufferData(const void* data, int offset, int size)
{
	// Fast path: persistent mapping was set up at creation time.
	if (m_MappedPtr)
	{
		std::memcpy(static_cast<char*>(m_MappedPtr) + offset, data, size);
		return true;
	}
	return VansVKMemoryAllocator::Get().WriteToAllocation(
		m_VansVKBufferAllocation, offset, size, data);
}

bool VansGraphics::VansVKBuffer::PersistentMap()
{
	if (m_MappedPtr)
		return true; // Already mapped (either persistent at creation or mapped on demand)

	m_MappedPtr = VansVKMemoryAllocator::Get().MapAllocation(m_VansVKBufferAllocation);
	if (!m_MappedPtr)
	{
		VANS_LOG_ERROR("PersistentMap: vmaMapMemory failed.");
		return false;
	}
	return true;
}

void VansGraphics::VansVKBuffer::Unmap()
{
	if (!m_MappedPtr)
		return;

	// If this allocation is persistently mapped (PersistentUpload), the pointer
	// remains valid for the allocation's lifetime; vmaUnmapMemory is a no-op
	// in that case but we still clear the cache so SetBufferData falls back
	// to the copy path consistently.
	VansVKMemoryAllocator::Get().UnmapAllocation(m_VansVKBufferAllocation);
	m_MappedPtr = nullptr;
}

void VansGraphics::VansVKBuffer::UpdateMapped(const void* data, VkDeviceSize offset, VkDeviceSize size)
{
	if (!m_MappedPtr)
		return;
	std::memcpy(static_cast<char*>(m_MappedPtr) + offset, data, size);
}
