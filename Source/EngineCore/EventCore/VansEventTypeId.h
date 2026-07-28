#pragma once

#include <cstdint>

namespace Vans
{
	using VansEventTypeId = std::uint32_t;

	VansEventTypeId AllocateVansEventTypeId();

	template <typename EventT>
	VansEventTypeId GetVansEventTypeId()
	{
		static const VansEventTypeId id = AllocateVansEventTypeId();
		return id;
	}
}
