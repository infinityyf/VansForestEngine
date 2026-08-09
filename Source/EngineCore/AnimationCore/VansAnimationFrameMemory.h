#pragma once

#include <cstddef>
#include <memory_resource>
#include <initializer_list>
#include <utility>
#include <vector>

namespace VansGraphics
{
	// All transient animation payload containers obtain their allocator from this
	// thread-local scope. A controller owns the pool; definitions and public
	// outputs never retain pointers into a different controller's pool.
	class VansAnimationFrameMemory final
	{
	public:
		static std::pmr::memory_resource* CurrentResource() noexcept;

		class Scope final
		{
		public:
			explicit Scope(std::pmr::memory_resource& resource) noexcept;
			~Scope();
			Scope(const Scope&) = delete;
			Scope& operator=(const Scope&) = delete;

		private:
			std::pmr::memory_resource* m_Previous = nullptr;
		};

	private:
		static std::pmr::memory_resource*& ActiveResource() noexcept;
	};

	template<typename T>
	class VansAnimationFrameVector : public std::pmr::vector<T>
	{
		using Base = std::pmr::vector<T>;

	public:
		VansAnimationFrameVector()
			: Base(VansAnimationFrameMemory::CurrentResource()) {}
		explicit VansAnimationFrameVector(std::pmr::memory_resource* resource)
			: Base(resource) {}
		explicit VansAnimationFrameVector(std::size_t count)
			: Base(count, VansAnimationFrameMemory::CurrentResource()) {}
		VansAnimationFrameVector(std::size_t count, const T& value)
			: Base(count, value, VansAnimationFrameMemory::CurrentResource()) {}
		VansAnimationFrameVector(std::initializer_list<T> values)
			: Base(values, VansAnimationFrameMemory::CurrentResource()) {}
		VansAnimationFrameVector(const VansAnimationFrameVector& other)
			: Base(other.begin(), other.end(), VansAnimationFrameMemory::CurrentResource()) {}
		VansAnimationFrameVector(VansAnimationFrameVector&& other) noexcept
			: Base(std::move(other)) {}
		VansAnimationFrameVector& operator=(const VansAnimationFrameVector& other)
		{
			Base::assign(other.begin(), other.end());
			return *this;
		}
		VansAnimationFrameVector& operator=(VansAnimationFrameVector&& other)
		{
			Base::operator=(std::move(other));
			return *this;
		}
	};

	class VansAnimationFramePool final
	{
	public:
		VansAnimationFramePool();
		std::pmr::memory_resource& Resource() noexcept { return m_Pool; }
		void BeginFrame() noexcept;
		void EndFrame() noexcept;
		std::size_t GetLastFrameUpstreamAllocations() const noexcept { return m_LastFrameAllocations; }
		std::size_t GetLastFrameUpstreamBytes() const noexcept { return m_LastFrameBytes; }

	private:
		class CountingResource final : public std::pmr::memory_resource
		{
		public:
			std::size_t allocationCount = 0;
			std::size_t allocatedBytes = 0;

		private:
			void* do_allocate(std::size_t bytes, std::size_t alignment) override;
			void do_deallocate(void* pointer, std::size_t bytes, std::size_t alignment) override;
			bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override;
		};

		CountingResource m_Upstream;
		std::pmr::unsynchronized_pool_resource m_Pool;
		std::size_t m_FrameStartAllocations = 0;
		std::size_t m_FrameStartBytes = 0;
		std::size_t m_LastFrameAllocations = 0;
		std::size_t m_LastFrameBytes = 0;
	};
}
