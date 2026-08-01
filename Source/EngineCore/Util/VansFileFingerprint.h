#pragma once

#include <cstdint>
#include <cstddef>
#include <filesystem>
#include <string>

namespace Vans
{
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
}
