#pragma once

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/VansAssetObjectRepository.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace Vans
{
enum class VansAuthoringAssetPayloadKind
{
	SerializedJson,
	Bytes
};

struct VansAuthoringAssetCreateItem
{
	std::filesystem::path sourcePath;
	VansAssetGuid guid;
	VansAssetType type = VansAssetType::Unknown;
	VansAuthoringAssetPayloadKind payloadKind =
		VansAuthoringAssetPayloadKind::SerializedJson;
	VansSerializedValue serializedRoot;
	std::string bytes;
	std::optional<VansSerializedValue> metaSettings;
};

struct VansAuthoringAssetCreationResult
{
	bool success = false;
	std::string message;
	std::vector<VansAssetRecord> records;

	explicit operator bool() const { return success; }
};

class VansAuthoringAssetCreationService final
{
public:
	using FinalizeCallback = std::function<bool(std::string&)>;

	static VansAuthoringAssetCreationResult CreateBundle(
		VansAssetDatabase& database,
		VansAssetObjectRepository& repository,
		std::filesystem::path bundleDirectory,
		std::vector<VansAuthoringAssetCreateItem> items,
		std::string ioOperation,
		FinalizeCallback finalize = {},
		bool requireNewDirectory = true);
};
} // namespace Vans
