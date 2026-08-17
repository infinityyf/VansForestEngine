#pragma once

#include "VansTimelineAsset.h"

#include <filesystem>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace Vans
{
class VansTimelineSerialization
{
public:
	using Json = nlohmann::ordered_json;

	static bool Decode(const Json& root, VansTimelineAsset& asset, std::string& error);
	static Json Encode(const VansTimelineAsset& asset);
	static bool Load(const std::filesystem::path& path, VansTimelineAsset& asset, std::string& error);
	static bool SaveAtomic(const std::filesystem::path& path, const VansTimelineAsset& asset, std::string& error);
	static VansSerializedValue EncodeSerialized(const VansTimelineAsset& asset);
	static bool DecodeSerialized(const VansSerializedValue& root, VansTimelineAsset& asset, std::string& error);
	static void Normalize(VansTimelineAsset& asset);

	static const char* ValueTypeName(VansTimelineValueType type);
	static bool TryParseValueType(const std::string& value, VansTimelineValueType& type);
	static const char* ChannelTypeName(VansTimelineChannelType type) { return ValueTypeName(type); }
	static bool TryParseChannelType(const std::string& value, VansTimelineChannelType& type)
	{
		return TryParseValueType(value, type);
	}
};
}
