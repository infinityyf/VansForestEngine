#include "VansAnimationFrameMemory.h"

namespace VansGraphics
{
	std::pmr::memory_resource*& VansAnimationFrameMemory::ActiveResource() noexcept
	{
		thread_local std::pmr::memory_resource* resource = std::pmr::get_default_resource();
		return resource;
	}

	std::pmr::memory_resource* VansAnimationFrameMemory::CurrentResource() noexcept
	{
		return ActiveResource();
	}

	VansAnimationFrameMemory::Scope::Scope(std::pmr::memory_resource& resource) noexcept
		: m_Previous(ActiveResource())
	{
		ActiveResource() = &resource;
	}

	VansAnimationFrameMemory::Scope::~Scope()
	{
		ActiveResource() = m_Previous;
	}

	VansAnimationFramePool::VansAnimationFramePool()
		: m_Pool(&m_Upstream)
	{
	}

	void VansAnimationFramePool::BeginFrame() noexcept
	{
		m_FrameStartAllocations = m_Upstream.allocationCount;
		m_FrameStartBytes = m_Upstream.allocatedBytes;
	}

	void VansAnimationFramePool::EndFrame() noexcept
	{
		m_LastFrameAllocations = m_Upstream.allocationCount - m_FrameStartAllocations;
		m_LastFrameBytes = m_Upstream.allocatedBytes - m_FrameStartBytes;
	}

	void* VansAnimationFramePool::CountingResource::do_allocate(
		std::size_t bytes, std::size_t alignment)
	{
		++allocationCount;
		allocatedBytes += bytes;
		return std::pmr::new_delete_resource()->allocate(bytes, alignment);
	}

	void VansAnimationFramePool::CountingResource::do_deallocate(
		void* pointer, std::size_t bytes, std::size_t alignment)
	{
		std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
	}

	bool VansAnimationFramePool::CountingResource::do_is_equal(
		const std::pmr::memory_resource& other) const noexcept
	{
		return this == &other;
	}
}
