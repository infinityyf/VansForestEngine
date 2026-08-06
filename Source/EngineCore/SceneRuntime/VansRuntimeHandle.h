#pragma once

#include <cstdint>
#include <limits>

namespace Vans
{
constexpr std::uint32_t VansInvalidRuntimeIndex = std::numeric_limits<std::uint32_t>::max();
constexpr std::uint16_t VansInvalidComponentTypeId = 0;

struct VansEntityHandle
{
	std::uint32_t index = VansInvalidRuntimeIndex;
	std::uint32_t generation = 0;

	bool IsValid() const { return index != VansInvalidRuntimeIndex; }
};

inline bool operator==(const VansEntityHandle& lhs, const VansEntityHandle& rhs)
{
	return lhs.index == rhs.index && lhs.generation == rhs.generation;
}

inline bool operator!=(const VansEntityHandle& lhs, const VansEntityHandle& rhs)
{
	return !(lhs == rhs);
}

struct VansComponentHandle
{
	std::uint16_t typeId = VansInvalidComponentTypeId;
	std::uint32_t index = VansInvalidRuntimeIndex;
	std::uint32_t generation = 0;

	bool IsValid() const { return typeId != VansInvalidComponentTypeId && index != VansInvalidRuntimeIndex; }
};

inline bool operator==(const VansComponentHandle& lhs, const VansComponentHandle& rhs)
{
	return lhs.typeId == rhs.typeId &&
		lhs.index == rhs.index &&
		lhs.generation == rhs.generation;
}

inline bool operator!=(const VansComponentHandle& lhs, const VansComponentHandle& rhs)
{
	return !(lhs == rhs);
}
}
