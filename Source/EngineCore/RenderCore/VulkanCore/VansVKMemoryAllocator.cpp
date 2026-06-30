#include "../../../Graphics/Vulkan/VansVKFunctions.h"
#include "VansVKDevice.h"
#include "../../Util/VansLog.h"

// VMA implementation lives ONLY in this translation unit.
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS  0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_VULKAN_VERSION           1002000  // Vulkan 1.2

// Disable VMA's default assert / debug log noise; route to our logger.
#ifdef _DEBUG
	#define VMA_DEBUG_LOG_FORMAT(format, ...) do { } while (0)
#endif

#include "vk_mem_alloc.h"

#include "VansVKMemoryAllocator.h"

namespace VansGraphics
{
	VansVKMemoryAllocator& VansVKMemoryAllocator::Get()
	{
		static VansVKMemoryAllocator instance;
		return instance;
	}

	bool VansVKMemoryAllocator::Initialize(VansVKDevice& device, uint32_t apiVersion)
	{
		if (m_Allocator != nullptr)
		{
			VANS_LOG_ERROR("[VMA] Initialize called twice.");
			return false;
		}
		m_Device = &device;

		// Provide the two entry-point loaders. VMA will resolve every other
		// Vulkan function on its own (DYNAMIC_VULKAN_FUNCTIONS = 1).
		// NOTE: the project loads Vulkan dynamically; the function pointers
		// live in the VansGraphics namespace, not at global scope.
		VmaVulkanFunctions vkFns{};
		vkFns.vkGetInstanceProcAddr = VansGraphics::vkGetInstanceProcAddr;
		vkFns.vkGetDeviceProcAddr   = VansGraphics::vkGetDeviceProcAddr;

		VmaAllocatorCreateInfo info{};
		info.vulkanApiVersion = apiVersion;
		info.physicalDevice   = device.GetPhysicalDevice();
		info.device           = device.GetLogicDevice();
		info.instance         = device.GetInstance();
		info.pVulkanFunctions = &vkFns;
		info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT
		           | VMA_ALLOCATOR_CREATE_KHR_DEDICATED_ALLOCATION_BIT;

		VkResult result = vmaCreateAllocator(&info, &m_Allocator);
		if (result != VK_SUCCESS)
		{
			VANS_LOG_ERROR("[VMA] vmaCreateAllocator failed: " << result);
			m_Allocator = nullptr;
			return false;
		}
		VANS_LOG("[VMA] allocator initialized (apiVersion=1.2, BDA + dedicated allocation enabled)");
		return true;
	}

	void VansVKMemoryAllocator::Shutdown()
	{
		if (m_Allocator == nullptr)
		{
			return;
		}
		LogMemoryStats();
		vmaDestroyAllocator(m_Allocator);
		m_Allocator = nullptr;
		m_Device    = nullptr;
	}

	// Translates VansMemoryUsage to a (VmaMemoryUsage, VmaAllocationCreateFlags) pair.
	static void TranslateUsage(VansMemoryUsage usage,
	                           bool needPersistentMap,
	                           VmaMemoryUsage&            outUsage,
	                           VmaAllocationCreateFlags&  outFlags,
	                           VkMemoryPropertyFlags&     outRequired)
	{
		outUsage    = VMA_MEMORY_USAGE_AUTO;
		outFlags    = 0;
		outRequired = 0;

		switch (usage)
		{
		case VansMemoryUsage::GpuOnly:
			outUsage    = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			outRequired = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
			break;

		case VansMemoryUsage::CpuToGpu:
			outUsage = VMA_MEMORY_USAGE_AUTO;
			outFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
			break;

		case VansMemoryUsage::GpuToCpu:
			outUsage = VMA_MEMORY_USAGE_AUTO;
			outFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			break;

		case VansMemoryUsage::CpuOnly:
			outUsage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
			outFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
			break;

		case VansMemoryUsage::PersistentUpload:
			outUsage = VMA_MEMORY_USAGE_AUTO;
			outFlags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT
			         | VMA_ALLOCATION_CREATE_MAPPED_BIT;
			break;
		}

		if (needPersistentMap)
		{
			outFlags |= VMA_ALLOCATION_CREATE_MAPPED_BIT;
		}
	}

	bool VansVKMemoryAllocator::CreateBuffer(
		const VkBufferCreateInfo& bufferInfo,
		VansMemoryUsage usage,
		bool needPersistentMap,
		VkBuffer&       outBuffer,
		VmaAllocation&  outAllocation,
		void**          outPersistentPtr)
	{
		if (m_Allocator == nullptr)
		{
			VANS_LOG_ERROR("[VMA] CreateBuffer called before Initialize.");
			return false;
		}

		VmaAllocationCreateInfo allocInfo{};
		TranslateUsage(usage, needPersistentMap,
		               allocInfo.usage, allocInfo.flags, allocInfo.requiredFlags);

		VmaAllocationInfo outInfo{};
		VkResult result = vmaCreateBuffer(
			m_Allocator, &bufferInfo, &allocInfo,
			&outBuffer, &outAllocation, &outInfo);
		if (result != VK_SUCCESS)
		{
			VANS_LOG_ERROR("[VMA] vmaCreateBuffer failed: " << result
			               << " size=" << bufferInfo.size);
			outBuffer     = VK_NULL_HANDLE;
			outAllocation = nullptr;
			return false;
		}

		if (outPersistentPtr != nullptr)
		{
			*outPersistentPtr = outInfo.pMappedData; // may be nullptr if not mapped
		}
		return true;
	}

	void VansVKMemoryAllocator::DestroyBuffer(VkBuffer& buffer, VmaAllocation& allocation)
	{
		if (m_Allocator == nullptr)
		{
			return;
		}
		if (buffer != VK_NULL_HANDLE || allocation != nullptr)
		{
			vmaDestroyBuffer(m_Allocator, buffer, allocation);
		}
		buffer     = VK_NULL_HANDLE;
		allocation = nullptr;
	}

	bool VansVKMemoryAllocator::CreateImage(
		const VkImageCreateInfo& imageInfo,
		VansMemoryUsage usage,
		bool preferDedicated,
		VkImage&       outImage,
		VmaAllocation& outAllocation)
	{
		if (m_Allocator == nullptr)
		{
			VANS_LOG_ERROR("[VMA] CreateImage called before Initialize.");
			return false;
		}

		VmaAllocationCreateInfo allocInfo{};
		TranslateUsage(usage, /*needPersistentMap*/ false,
		               allocInfo.usage, allocInfo.flags, allocInfo.requiredFlags);
		if (preferDedicated)
		{
			allocInfo.flags |= VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT;
		}

		VkResult result = vmaCreateImage(
			m_Allocator, &imageInfo, &allocInfo,
			&outImage, &outAllocation, nullptr);
		if (result != VK_SUCCESS)
		{
			VANS_LOG_ERROR("[VMA] vmaCreateImage failed: " << result
			               << " extent=" << imageInfo.extent.width
			               << "x" << imageInfo.extent.height);
			outImage      = VK_NULL_HANDLE;
			outAllocation = nullptr;
			return false;
		}
		return true;
	}

	void VansVKMemoryAllocator::DestroyImage(VkImage& image, VmaAllocation& allocation)
	{
		if (m_Allocator == nullptr)
		{
			return;
		}
		if (image != VK_NULL_HANDLE || allocation != nullptr)
		{
			vmaDestroyImage(m_Allocator, image, allocation);
		}
		image      = VK_NULL_HANDLE;
		allocation = nullptr;
	}

	bool VansVKMemoryAllocator::WriteToAllocation(
		VmaAllocation allocation,
		VkDeviceSize  offset,
		VkDeviceSize  size,
		const void*   hostData)
	{
		if (m_Allocator == nullptr || allocation == nullptr || hostData == nullptr)
		{
			return false;
		}
		// vmaCopyMemoryToAllocation handles map/unmap and flush if non-coherent.
		VkResult result = vmaCopyMemoryToAllocation(
			m_Allocator, hostData, allocation, offset, size);
		if (result != VK_SUCCESS)
		{
			VANS_LOG_ERROR("[VMA] vmaCopyMemoryToAllocation failed: " << result);
			return false;
		}
		return true;
	}

	void* VansVKMemoryAllocator::MapAllocation(VmaAllocation allocation)
	{
		if (m_Allocator == nullptr || allocation == nullptr)
		{
			return nullptr;
		}
		void* p = nullptr;
		VkResult result = vmaMapMemory(m_Allocator, allocation, &p);
		if (result != VK_SUCCESS)
		{
			VANS_LOG_ERROR("[VMA] vmaMapMemory failed: " << result);
			return nullptr;
		}
		return p;
	}

	void VansVKMemoryAllocator::UnmapAllocation(VmaAllocation allocation)
	{
		if (m_Allocator == nullptr || allocation == nullptr)
		{
			return;
		}
		vmaUnmapMemory(m_Allocator, allocation);
	}

	void VansVKMemoryAllocator::FlushAllocation(VmaAllocation allocation, VkDeviceSize offset, VkDeviceSize size)
	{
		if (m_Allocator == nullptr || allocation == nullptr || size == 0)
		{
			return;
		}
		VkResult result = vmaFlushAllocation(m_Allocator, allocation, offset, size);
		if (result != VK_SUCCESS)
		{
			VANS_LOG_ERROR("[VMA] vmaFlushAllocation failed: " << result);
		}
	}

	void* VansVKMemoryAllocator::GetPersistentMappedPtr(VmaAllocation allocation) const
	{
		if (m_Allocator == nullptr || allocation == nullptr)
		{
			return nullptr;
		}
		VmaAllocationInfo info{};
		vmaGetAllocationInfo(m_Allocator, allocation, &info);
		return info.pMappedData;
	}

	void VansVKMemoryAllocator::LogMemoryStats() const
	{
		if (m_Allocator == nullptr)
		{
			return;
		}
		VmaTotalStatistics stats{};
		vmaCalculateStatistics(m_Allocator, &stats);
		VANS_LOG("[VMA] stats: blocks=" << stats.total.statistics.blockCount
		         << " allocations=" << stats.total.statistics.allocationCount
		         << " allocBytes=" << stats.total.statistics.allocationBytes
		         << " blockBytes=" << stats.total.statistics.blockBytes);
	}
}
