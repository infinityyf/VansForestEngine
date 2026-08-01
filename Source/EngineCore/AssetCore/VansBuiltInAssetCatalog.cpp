#include "VansBuiltInAssetCatalog.h"

namespace Vans
{
const std::vector<VansBuiltInAssetEntry>& VansBuiltInAssetCatalog::Entries()
{
	static const std::vector<VansBuiltInAssetEntry> entries = {
		{
			"fullScreenQuad",
			"2c86c128-f3f0-4dbd-9e4e-0f0f0a61c9d1",
			"EngineAssets/Models/fullscreen.obj",
			VansAssetType::Model
		},
		{
			"plane",
			"92eb85ad-49ac-4f90-a8f9-9e30251c584b",
			"EngineAssets/Models/plane.obj",
			VansAssetType::Model
		}
	};
	return entries;
}

bool VansBuiltInAssetCatalog::IsReservedRuntimeAlias(const std::string& alias)
{
	for (const VansBuiltInAssetEntry& entry : Entries())
	{
		if (entry.runtimeAlias != nullptr && alias == entry.runtimeAlias)
			return true;
	}
	return false;
}

bool VansBuiltInAssetCatalog::RegisterAssets(
	VansAssetDatabase& database,
	const std::filesystem::path& engineRoot,
	const VansAssetOperationPolicy& policy,
	std::vector<std::string>& errors)
{
	bool success = true;
	for (const VansBuiltInAssetEntry& entry : Entries())
	{
		std::string error;
		if (!database.RegisterOrRefresh(engineRoot / entry.sourcePath, policy, error))
		{
			success = false;
			errors.push_back(error.empty()
				? std::string("Cannot register built-in asset: ") + entry.sourcePath
				: std::move(error));
		}
	}
	return success;
}
}
