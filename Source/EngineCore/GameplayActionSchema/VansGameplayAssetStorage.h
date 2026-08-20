#pragma once

#include "VansGAFProjectConfiguration.h"
#include "VansGameplayAssetSchema.h"
#include "VansGameplayAssetMigration.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Vans
{
struct VansGameplayCookedAsset
{
	VansAssetType assetType = VansAssetType::Unknown;
	std::uint32_t schemaVersion = 1;
	std::uint64_t contentHash = 0;
	std::string cookPolicyFingerprint;
	std::vector<std::string> dependencies;
	VansSerializedValue runtimeDocument = VansSerializedValue::Object({});
};

struct VansGameplayCookResult
{
	VansGameplayCookedAsset asset;
	VansGameplayDiagnostics diagnostics;
	std::vector<VansGameplayMigrationRecord> migrations;
	std::string error;

	explicit operator bool() const { return error.empty(); }
};

class VansGameplayAssetStorage
{
public:
	static bool LoadSource(
		const std::filesystem::path& path,
		VansSerializedValue& root,
		std::string& error);
	static bool SaveSourceAtomic(
		const std::filesystem::path& path,
		const VansSerializedValue& root,
		std::string& error,
		const VansGAFProjectConfiguration* configuration = nullptr);
	static void AppendProjectDiagnostics(
		VansAssetType type,
		const VansSerializedValue& source,
		const VansGAFProjectConfiguration& configuration,
		VansGameplayDiagnostics& diagnostics);
	static VansGameplayCookResult Cook(
		VansAssetType type,
		const VansSerializedValue& source,
		const VansGameplayAssetSchemaRegistry& schemas = VansGameplayAssetSchemaRegistry::BuiltIns(),
		const VansGAFProjectConfiguration* configuration = nullptr);
	static bool SaveCookedAtomic(
		const std::filesystem::path& path,
		const VansGameplayCookedAsset& asset,
		std::string& error);
	static bool LoadCooked(
		const std::filesystem::path& path,
		VansGameplayCookedAsset& asset,
		std::string& error);
};
}
