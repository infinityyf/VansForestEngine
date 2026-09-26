#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <string>

namespace Vans
{
inline constexpr std::uint64_t VANS_FNV1A64_OFFSET_BASIS = 14695981039346656037ull;

struct VansFileFingerprint
{
	std::uint64_t size = 0;
	std::int64_t writeTime = 0;
	std::uint64_t contentHash = 0;
};

bool ComputeFileFingerprint(
	const std::filesystem::path& path,
	VansFileFingerprint& outFingerprint,
	std::string* outError = nullptr);

std::uint64_t ComputeMemoryFnv1a64(const void* data, std::size_t size);
std::uint64_t ContinueMemoryFnv1a64(std::uint64_t hash, const void* data, std::size_t size);
std::uint64_t ContinueUint64LittleEndianFnv1a64(std::uint64_t hash, std::uint64_t value);
}
