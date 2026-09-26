#pragma once

#include "../Util/VansFileFingerprint.h"

#include <cstdint>
#include <string>

namespace Vans
{
inline std::uint64_t PcgMix64(std::uint64_t value)
{
	value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
	value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
	return value ^ (value >> 31);
}

inline std::uint64_t PcgTextHash(const std::string& text)
{
	return ComputeMemoryFnv1a64(text.data(), text.size());
}

inline double PcgRandom01(std::uint64_t id, std::uint64_t channel)
{
	return static_cast<double>(PcgMix64(id ^ PcgMix64(channel + 0x9e3779b97f4a7c15ull)) >> 11) *
		(1.0 / 9007199254740992.0);
}

inline double PcgRadicalInverse(std::uint64_t index, std::uint32_t base)
{
	double result = 0;
	double scale = 1.0 / base;
	while (index)
	{
		result += (index % base) * scale;
		index /= base;
		scale /= base;
	}
	return result;
}
}
