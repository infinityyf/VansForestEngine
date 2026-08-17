#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace Vans
{
constexpr std::uint64_t VansStableHash64(std::string_view text)
{
	std::uint64_t value = 14695981039346656037ull;
	for (const char character : text)
	{
		value ^= static_cast<std::uint8_t>(character);
		value *= 1099511628211ull;
	}
	return value;
}

template <typename Tag>
struct VansStableId
{
	std::uint64_t value = 0;

	constexpr bool IsValid() const { return value != 0; }
	constexpr explicit operator bool() const { return IsValid(); }
	friend constexpr bool operator==(VansStableId left, VansStableId right) { return left.value == right.value; }
	friend constexpr bool operator!=(VansStableId left, VansStableId right) { return !(left == right); }
	friend constexpr bool operator<(VansStableId left, VansStableId right) { return left.value < right.value; }
};

template <typename Tag>
constexpr VansStableId<Tag> VansMakeStableId(std::string_view canonicalName)
{
	return { VansStableHash64(canonicalName) };
}

struct VansGenerationHandle
{
	std::uint32_t index = UINT32_MAX;
	std::uint32_t generation = 0;

	constexpr bool IsValid() const { return index != UINT32_MAX && generation != 0; }
	constexpr explicit operator bool() const { return IsValid(); }
	friend constexpr bool operator==(VansGenerationHandle left, VansGenerationHandle right)
	{
		return left.index == right.index && left.generation == right.generation;
	}
	friend constexpr bool operator!=(VansGenerationHandle left, VansGenerationHandle right) { return !(left == right); }
};
}

namespace std
{
template <typename Tag>
struct hash<Vans::VansStableId<Tag>>
{
	std::size_t operator()(Vans::VansStableId<Tag> id) const noexcept
	{
		return std::hash<std::uint64_t>{}(id.value);
	}
};
}
