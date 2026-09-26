#include "VansFileFingerprint.h"

#include <array>
#include <fstream>

namespace Vans
{
namespace
{
	constexpr std::uint64_t kFnvPrime = 1099511628211ull;

	void SetError(std::string* outError, const std::string& message)
	{
		if (outError)
			*outError = message;
	}
}

std::uint64_t ComputeMemoryFnv1a64(const void* data, std::size_t size)
{
	return ContinueMemoryFnv1a64(VANS_FNV1A64_OFFSET_BASIS, data, size);
}

std::uint64_t ContinueMemoryFnv1a64(std::uint64_t hash, const void* data, std::size_t size)
{
	const auto* bytes = static_cast<const unsigned char*>(data);
	for (std::size_t i = 0; i < size; ++i)
	{
		hash ^= static_cast<std::uint64_t>(bytes[i]);
		hash *= kFnvPrime;
	}
	return hash;
}

std::uint64_t ContinueUint64LittleEndianFnv1a64(std::uint64_t hash, std::uint64_t value)
{
	std::array<unsigned char, sizeof(value)> bytes{};
	for (std::size_t index = 0; index < bytes.size(); ++index)
		bytes[index] = static_cast<unsigned char>(value >> (index * 8));
	return ContinueMemoryFnv1a64(hash, bytes.data(), bytes.size());
}

bool ComputeFileFingerprint(
	const std::filesystem::path& path,
	VansFileFingerprint& outFingerprint,
	std::string* outError)
{
	std::error_code ec;
	const auto size = std::filesystem::file_size(path, ec);
	if (ec)
	{
		SetError(outError, "Cannot query file size: " + path.string() + " (" + ec.message() + ")");
		return false;
	}
	const auto writeTime = std::filesystem::last_write_time(path, ec);
	if (ec)
	{
		SetError(outError, "Cannot query file timestamp: " + path.string() + " (" + ec.message() + ")");
		return false;
	}

	std::ifstream file(path, std::ios::binary);
	if (!file.is_open())
	{
		SetError(outError, "Cannot open file for hashing: " + path.string());
		return false;
	}

	std::array<char, 64 * 1024> buffer{};
	std::uint64_t hash = VANS_FNV1A64_OFFSET_BASIS;
	while (file)
	{
		file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const std::streamsize count = file.gcount();
		hash = ContinueMemoryFnv1a64(hash, buffer.data(), static_cast<std::size_t>(count));
	}

	if (!file.eof())
	{
		SetError(outError, "Cannot read file for hashing: " + path.string());
		return false;
	}

	outFingerprint.size = static_cast<std::uint64_t>(size);
	outFingerprint.writeTime = static_cast<std::int64_t>(writeTime.time_since_epoch().count());
	outFingerprint.contentHash = hash;
	return true;
}
}
