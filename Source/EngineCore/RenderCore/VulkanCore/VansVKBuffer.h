#pragma once
#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"

#include "../VansGraphicsBuffer.h"
#include <vector>

// Forward declare VMA opaque allocation handle to keep this header light.
struct VmaAllocation_T;
typedef struct VmaAllocation_T* VmaAllocation;

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
		VkBuffer m_VansVKBuffer = VK_NULL_HANDLE;

		// VMA-managed allocation backing the buffer (replaces raw VkDeviceMemory).
		VmaAllocation m_VansVKBufferAllocation = nullptr;

		VkDeviceSize m_BufferSize = 0;

		VkFormat m_BufferFormat;

		uint32_t m_BufferStride;

		std::vector<VkBufferMemoryBarrier> m_BufferMemoryBarriers;

		// Persistent mapping: cached host pointer (nullptr when not mapped)
		void* m_MappedPtr = nullptr;

		// 是否通过显式 vmaMapMemory 映射；为 true 时销毁前必须 vmaUnmapMemory，
		// 否则 VMA 断言：Unmapping allocation not previously mapped。
		// 若 m_MappedPtr 来自 VMA_ALLOCATION_CREATE_MAPPED_BIT 持久映射，则保持 false。
		bool m_ExplicitlyMapped = false;

	private:
		VkBufferView m_VansVKBufferView = VK_NULL_HANDLE;

	public:

		bool CreatVulkanBuffer(VkDevice& logical_device, VkDeviceSize size, VkFormat format, VkBufferUsageFlags usage, VkMemoryPropertyFlags memory_properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

		void DestroyVulkanBuffer(VkDevice& logical_device);

		//设置buffer memory barrier
		void SetBufferMemoryBarrier(VkPipelineStageFlags generating_stages, VkPipelineStageFlags consuming_stages, BufferTransition bufferTransition);

		bool SetBufferData(const void* data, VkDeviceSize offset, VkDeviceSize size);

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

		VmaAllocation GetNativeAllocation() const { return m_VansVKBufferAllocation; }

		VkDeviceSize GetBufferSize() const { return m_BufferSize; }
	private:
		//transition a buffer: can transition usage or used queue
	//add barrier to memory manager
	//this it access transition, not change usage, access must obey the usage
		void AddTransitionBufferAccess(BufferTransition& transition);

	};

}