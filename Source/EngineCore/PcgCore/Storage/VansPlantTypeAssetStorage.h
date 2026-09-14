#pragma once

#include "../VansPlantTypeAsset.h"
#include <filesystem>

namespace Vans
{
class VansPlantTypeAssetStorage
{
public:
	static bool Load(const std::filesystem::path& path, VansPlantTypeAsset& asset, std::string& error);
	static bool SaveAtomic(const std::filesystem::path& path, const VansPlantTypeAsset& asset, std::string& error);
};
}
