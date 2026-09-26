#pragma once

#include "../RuntimeCore/VansRuntimeComponentTypeId.h"

#include <cstdint>
#include <limits>

namespace Vans
{
constexpr std::uint32_t VansInvalidRuntimeIndex = std::numeric_limits<std::uint32_t>::max();

inline std::uint32_t NextRuntimeGeneration(std::uint32_t generation)
{
	++generation;
	return generation == 0 ? 1 : generation;
}

struct VansEntityHandle
{
	std::uint32_t index = VansInvalidRuntimeIndex;
	std::uint32_t generation = 0;

	bool IsValid() const { return index != VansInvalidRuntimeIndex && generation != 0; }
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

	bool IsValid() const
	{
		return typeId != VansInvalidComponentTypeId &&
			index != VansInvalidRuntimeIndex && generation != 0;
	}
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
