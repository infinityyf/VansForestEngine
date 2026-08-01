#pragma once

#include "VansSceneResourcePlan.h"

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace Vans
{
struct VansPackagedAssetIndexRecord
{
	std::string guid;
	std::string type;
	std::string sourcePath;
	std::string authoringPath;
	std::string artifactPath;
	std::string artifactFormat;
	std::uint64_t sourceHash = 0;
	std::uint64_t metaHash = 0;
	bool missing = false;
};

struct VansPackagedResourcePlan
{
	VansSceneResourceBuildPlan resourcePlan;
	std::map<std::string, std::string> runtimeAssetBindings;
	std::vector<VansPackagedAssetIndexRecord> assetIndex;
};

class VansPackagedResourcePlanIO
{
public:
	static const char* DefaultRelativePath();

	static bool Save(
		const std::filesystem::path& path,
		const VansPackagedResourcePlan& plan,
		const std::filesystem::path& sourceContentRoot,
		std::string& error);

	static bool Load(
		const std::filesystem::path& path,
		const std::filesystem::path& packageContentRoot,
		VansPackagedResourcePlan& outPlan,
		std::string& error);
};
}
