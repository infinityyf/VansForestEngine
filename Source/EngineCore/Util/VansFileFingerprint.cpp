#include "VansFileFingerprint.h"

#include <array>
#include <fstream>

namespace Vans
{
namespace
{
	constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
	constexpr std::uint64_t kFnvPrime = 1099511628211ull;

	void SetError(std::string* outError, const std::string& message)
	{
		if (outError)
			*outError = message;
	}
}

std::uint64_t ComputeMemoryFnv1a64(const void* data, std::size_t size)
{
	const auto* bytes = static_cast<const unsigned char*>(data);
	std::uint64_t hash = kFnvOffsetBasis;
	for (std::size_t i = 0; i < size; ++i)
	{
		hash ^= static_cast<std::uint64_t>(bytes[i]);
		hash *= kFnvPrime;
	}
	return hash;
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
	std::uint64_t hash = kFnvOffsetBasis;
	while (file)
	{
		file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
		const std::streamsize count = file.gcount();
		for (std::streamsize i = 0; i < count; ++i)
		{
			hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(buffer[static_cast<std::size_t>(i)]));
			hash *= kFnvPrime;
		}
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
