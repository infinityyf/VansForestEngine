#pragma once

#include <atomic>
#include <cstdint>

namespace VansRuntime
{
	using VansUIHandleId = std::uint64_t;

	constexpr VansUIHandleId kInvalidUIHandle = 0;

	inline VansUIHandleId AllocateUIHandle()
	{
		static std::atomic<VansUIHandleId> nextHandle{ 1 };
		const VansUIHandleId handle = nextHandle.fetch_add(1, std::memory_order_relaxed);
		return handle == kInvalidUIHandle
			? nextHandle.fetch_add(1, std::memory_order_relaxed)
			: handle;
	}
}
