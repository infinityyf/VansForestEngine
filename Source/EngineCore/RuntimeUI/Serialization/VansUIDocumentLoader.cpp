#include "VansUIDocumentLoader.h"

#include "../../AssetCore/Serialization/VansJsonDocumentCodec.h"
#include "../../AssetCore/Serialization/VansSerializedValueLegacyJsonAdapter.h"
#include "../../AssetCore/Storage/VansFileStorage.h"

#include <nlohmann/json.hpp>

namespace VansRuntime
{
	namespace
	{
		std::uint64_t HashBytes(const std::string& bytes)
		{
			constexpr std::uint64_t offset = 14695981039346656037ull;
			constexpr std::uint64_t prime = 1099511628211ull;
			std::uint64_t result = offset;
			for (const unsigned char value : bytes)
			{
				result ^= value;
				result *= prime;
			}
			return result;
		}
	}

	bool VansUIDocumentLoader::Load(
		const std::filesystem::path& path,
		VansUIAssetDocument& document,
		std::string& error)
	{
		std::string bytes;
		if (!Vans::VansFileStorage::ReadAllBytes(path, bytes, error))
			return false;

		Vans::VansJsonDocumentCodec::OrderedJson jsonRoot;
		if (!Vans::VansJsonDocumentCodec::Parse(bytes, jsonRoot, error))
			return false;

		document.root = Vans::DecodeSerializedValueLegacyJson(jsonRoot);
		document.sourcePath = path;
		document.contentHash = HashBytes(bytes);
		return true;
	}
}
