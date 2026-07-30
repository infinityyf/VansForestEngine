#pragma once

#include <cstdint>

namespace VansRuntime
{
	using VansUIHandleId = std::uint64_t;
	using VansUISubscriptionToken = std::uint64_t;

	constexpr VansUIHandleId kInvalidUIHandle = 0;
	constexpr VansUISubscriptionToken kInvalidUISubscription = 0;
}
