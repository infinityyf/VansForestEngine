#pragma once

#if defined _WIN32
#define VK_USE_PLATFORM_WIN32_KHR
#elif defined __linux

#endif
#include "vulkan/vulkan.h"

// Forward-declare VMA opaque handles so this header does NOT pull in
// vk_mem_alloc.h (which is large and only needed in cpp files).
struct VmaAllocator_T;
struct VmaAllocation_T;
typedef struct VmaAllocator_T*  VmaAllocator;
typedef struct VmaAllocation_T* VmaAllocation;

namespace VansGraphics
{
	class VansVKDevice;

	// Resource usage intent. Translated to VMA flags inside the cpp.
	enum class VansMemoryUsage
	{
		GpuOnly,            // DEVICE_LOCAL only (vertex/index/SSBO/RT/depth ...)
		CpuToGpu,           // staging upload (HOST_VISIBLE, sequential write)
		GpuToCpu,           // readback (HOST_VISIBLE, random access)
		CpuOnly,            // host-only
		PersistentUpload    // HOST_VISIBLE + persistently mapped (per-frame UBO)
	};

	// Facade around AMD Vulkan Memory Allocator.
	// Owns the single VmaAllocator instance and is the only place that
	// includes vk_mem_alloc.h in the engine.
	class VansVKMemoryAllocator
	{
	public:
		static VansVKMemoryAllocator& Get();

		// Lifecycle ----------------------------------------------------
		bool Initialize(VansVKDevice& device, uint32_t apiVersion);
		void Shutdown();
		bool IsInitialized() const { return m_Allocator != nullptr; }

		// Buffer -------------------------------------------------------
		// On success, fills outBuffer + outAllocation. If needPersistentMap
		// is true and allocation is host-visible, outPersistentPtr (when
		// non-null) receives the persistently mapped pointer.
		bool CreateBuffer(
			const VkBufferCreateInfo& bufferInfo,
			VansMemoryUsage usage,
			bool needPersistentMap,
			VkBuffer&       outBuffer,
			VmaAllocation&  outAllocation,
			void**          outPersistentPtr);

		void DestroyBuffer(VkBuffer& buffer, VmaAllocation& allocation);

		// Image --------------------------------------------------------
		bool CreateImage(
			const VkImageCreateInfo& imageInfo,
			VansMemoryUsage usage,
			bool preferDedicated,
			VkImage&       outImage,
			VmaAllocation& outAllocation);

		void DestroyImage(VkImage& image, VmaAllocation& allocation);

		// Mapping ------------------------------------------------------
		// One-shot copy from host data into the allocation. Internally
		// flushes if memory is non-coherent. Safe to call on persistently
		// mapped allocations as well.
		bool WriteToAllocation(
			VmaAllocation allocation,
			VkDeviceSize  offset,
			VkDeviceSize  size,
			const void*   hostData);

		// Map / unmap on demand. Returns nullptr on failure.
		void* MapAllocation(VmaAllocation allocation);
		void  UnmapAllocation(VmaAllocation allocation);

		// Returns persistently mapped pointer (only valid for allocations
		// created with PersistentUpload + needPersistentMap == true).
		void* GetPersistentMappedPtr(VmaAllocation allocation) const;

		// Diagnostics --------------------------------------------------
		void LogMemoryStats() const;

		VmaAllocator GetNative() const { return m_Allocator; }

	private:
		VansVKMemoryAllocator() = default;
		~VansVKMemoryAllocator() = default;
		VansVKMemoryAllocator(const VansVKMemoryAllocator&) = delete;
		VansVKMemoryAllocator& operator=(const VansVKMemoryAllocator&) = delete;

	private:
		VmaAllocator  m_Allocator = nullptr;
		VansVKDevice* m_Device    = nullptr;
	};
}
