#pragma once

#include "VansGAFProjectConfiguration.h"
#include "VansGameplayAssetSchema.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Vans
{
class VansGAFSchemaRegistry;

struct VansGameplayCookedAsset
{
	VansAssetType assetType = VansAssetType::Unknown;
	std::uint64_t contentHash = 0;
	std::string cookPolicyFingerprint;
	std::vector<std::string> dependencies;
	VansSerializedValue runtimeDocument = VansSerializedValue::Object({});
};

struct VansGameplayCookResult
{
	VansGameplayCookedAsset asset;
	VansGameplayDiagnostics diagnostics;
	std::string error;

	explicit operator bool() const { return error.empty(); }
};

// 项目/包边界解码后的 GAF 资产对象。运行时只消费该对象，不再根据索引路径回读源文件。
struct VansGameplayAssetMemoryObject
{
	std::filesystem::path sourcePath;
	VansSerializedValue sourceDocument = VansSerializedValue::Object({});
	VansGameplayCookedAsset cookedAsset;
	bool hasCookedAsset = false;
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
	static void AppendExtensionDiagnostics(
		VansAssetType type,
		const VansSerializedValue& source,
		const VansGAFSchemaRegistry& schemas,
		VansGameplayDiagnostics& diagnostics);
	static VansGameplayCookResult Cook(
		VansAssetType type,
		const VansSerializedValue& source,
		const VansGameplayAssetSchemaRegistry& schemas = VansGameplayAssetSchemaRegistry::BuiltIns(),
		const VansGAFProjectConfiguration* configuration = nullptr,
		const VansGAFSchemaRegistry* extensionSchemas = nullptr);
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
