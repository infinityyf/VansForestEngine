#pragma once

#include "../AssetCore/VansAssetDatabase.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Vans
{
struct VansGAFProjectConfiguration;

struct VansGameplayPackagedAssetRecord
{
	std::string guid;
	VansAssetType assetType = VansAssetType::Unknown;
	std::filesystem::path sourcePath;
	std::filesystem::path artifactPath;
	std::uint64_t contentHash = 0;
	std::vector<std::string> dependencies;
};

struct VansGameplayPackageCookResult
{
	bool success = false;
	std::vector<VansGameplayPackagedAssetRecord> assets;
	std::vector<std::string> requiredAssetGuids;
	std::vector<std::string> errors;

	explicit operator bool() const { return success; }
};

class VansGameplayAssetPackageCooker
{
public:
	static VansGameplayPackageCookResult CookClosure(
		const std::filesystem::path& projectRoot,
		VansAssetDatabase& projectDatabase,
		VansAssetDatabase* builtInDatabase,
		const std::vector<std::string>& seedAssetGuids,
		const VansGAFProjectConfiguration* configuration = nullptr);
};
}
