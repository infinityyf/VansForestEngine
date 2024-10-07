#pragma once

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"
#include <vector>


namespace VansVulkan
{
	class VansVKCommandBuffer;
	class VansVKBuffer;
	class VansVKImage;
	class VansVKDevice;

	class VansVKMemoryManager
	{
	private:
		static VansVKMemoryManager* instance;

		VansVKMemoryManager();

	public:
		static VansVKMemoryManager* GetInstance()
		{
			if (instance == nullptr)
			{
				instance = new VansVKMemoryManager();
			}
			return instance;
		}

		void BindDevice(VkCommandBuffer& commandBuffer, VansVKDevice& device);

		VkDeviceSize AlignMemorySizeTo(VkDeviceSize value, VkDeviceSize alignment) 
		{
			return (value + alignment - 1) & ~(alignment - 1);
		}

		bool AllocateMemory(VkMemoryRequirements& requires, VkDeviceMemory& memory, VkMemoryPropertyFlags memory_properties);
		
		void FreeMemory(VkDeviceMemory& memory);

		void SetBufferMemoryBarrier(std::vector<VkBufferMemoryBarrier>& bufferMemoryBarriers,
			VkPipelineStageFlags generating_stages,
			VkPipelineStageFlags consuming_stages);

		void SetImageMemoryBarrier(std::vector<VkImageMemoryBarrier>& imageMemoryBarriers,
			VkPipelineStageFlags generating_stages,
			VkPipelineStageFlags consuming_stages);

		//map memory from host to device
		bool MapMemoryFromHost(VkDeviceMemory& memory, VkDeviceSize offset, VkDeviceSize size, void* host_data, bool upmap_immediate = true);

	public:
		//copy date between buffers
		static void CopyBufferData(VansVKCommandBuffer& command_buffer, VansVKBuffer& source_buffer, VansVKBuffer& dest_buffer, const std::vector<VkBufferCopy>& regions);

		//copy deate from buffer to image
		//copyinfo定义了buffer中的imge长宽，layout为了正确的upload image
		static void CopyBufferToImage(VansVKCommandBuffer& command_buffer, VansVKBuffer& source_buffer, VansVKImage& dest_image, VkImageLayout layout, const std::vector<VkBufferImageCopy>& regions);

		static void CopyImageToBuffer(VansVKCommandBuffer& command_buffer, VansVKImage& source_image, VansVKBuffer& dest_buffer, VkImageLayout layout, const std::vector<VkBufferImageCopy>& regions);

	private:
		//device info
		VkPhysicalDevice m_PhysicalDevice;

		VkDevice m_LogicalDevice;

		VkCommandBuffer m_CommandBuffer;

		VkPhysicalDeviceProperties m_DeviceProperties;

	private :
		VkPhysicalDeviceMemoryProperties m_MemoryProperties;


	};
}
