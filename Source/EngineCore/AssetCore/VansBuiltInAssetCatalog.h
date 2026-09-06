#pragma once

#include "VansAssetDatabase.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace Vans
{
struct VansBuiltInAssetEntry
{
	const char* runtimeAlias = nullptr;
	const char* guid = nullptr;
	const char* sourcePath = nullptr;
	VansAssetType type = VansAssetType::Unknown;
};

class VansBuiltInAssetCatalog
{
public:
	static constexpr std::string_view BaseThemeGuid =
		"18dcb044-c0d3-4bc6-a868-8b38785b82aa";
	static const std::vector<VansBuiltInAssetEntry>& Entries();
	static bool IsReservedRuntimeAlias(const std::string& alias);
	static bool RegisterAssets(
		VansAssetDatabase& database,
		const std::filesystem::path& engineRoot,
		const VansAssetOperationPolicy& policy,
		std::vector<std::string>& errors);
};
}
