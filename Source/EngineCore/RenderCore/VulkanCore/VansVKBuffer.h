#pragma once
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"

#include "../VansGraphicsBuffer.h"
#include <vector>
using namespace VansGraphics;
namespace VansGraphics
{
	struct BufferTransition
	{
		VkBuffer Buffer;
		VkAccessFlags CurrentAccess;
		VkAccessFlags NewAccess;
		uint32_t CurrentQueueFamily;
		uint32_t NewQueueFamily;
	};
	//buffers and images in Vulkan don't have their own storage.They require us to specifically create and bind appropriate memory objects
	//buffer usage : uniform buffers, storage buffers, or texel buffers
	// stage buffer : intermediate resources for data transfer from the CPU to the GPU

	class VansVKBuffer : VansGraphicsBuffer
	{
		friend class VansVKMemoryManager;
		friend class VansVKDevice;
		friend class VansMesh;
	private:
		VkBuffer m_VansVKBuffer;

		VkDeviceMemory m_VansVKBufferMemory;

		VkDeviceSize m_BufferSize;

		VkFormat m_BufferFormat;

		uint32_t m_BufferStride;

		std::vector<VkBufferMemoryBarrier> m_BufferMemoryBarriers;

		// Persistent mapping: cached host pointer (nullptr when not mapped)
		void* m_MappedPtr = nullptr;

	private:
		VkBufferView m_VansVKBufferView;

	public:

		bool CreatVulkanBuffer(VkDevice& logical_device, VkDeviceSize size, VkFormat format, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		void DestroyVulkanBuffer(VkDevice& logical_device);

		//设置buffer memory barrier
		void SetBufferMemoryBarrier(VkPipelineStageFlags generating_stages, VkPipelineStageFlags consuming_stages, BufferTransition bufferTransition);

		bool SetBufferData(const void* data, int offset, int size);

		// ── Persistent mapping API ────────────────────────────────────
		// Map the whole buffer once and keep the pointer cached.
		// Subsequent writes go through UpdateMapped() with zero Vulkan overhead.
		bool PersistentMap();

		// Unmap a previously persistent-mapped buffer.
		void Unmap();

		// Write data into the persistently mapped region (no vkMapMemory call).
		// The buffer must have been PersistentMap()'d first.
		void UpdateMapped(const void* data, VkDeviceSize offset, VkDeviceSize size);

		// Returns true when the buffer is currently persistently mapped.
		bool IsMapped() const { return m_MappedPtr != nullptr; }

		void* GetMappedPtr() const { return m_MappedPtr; }

		VkBuffer GetNativeBuffer() const { return m_VansVKBuffer; }

		VkDeviceMemory GetNativeMemory() const { return m_VansVKBufferMemory; }

		VkDeviceSize GetBufferSize() const { return m_BufferSize; }
	private:
		//transition a buffer: can transition usage or used queue
	//add barrier to memory manager
	//this it access transition, not change usage, access must obey the usage
		void AddTransitionBufferAccess(BufferTransition& transition);

	};

}