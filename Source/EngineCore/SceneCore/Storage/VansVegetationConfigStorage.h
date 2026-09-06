#pragma once

#include "../Serialization/VansVegetationConfigCodec.h"

#include <filesystem>
#include <string>

namespace Vans
{
class VansVegetationConfigStorage
{
public:
	static bool Load(
		const std::filesystem::path& path,
		VansVegetationConfigAsset& asset,
		std::string& error);
	static bool SaveAtomic(
		const std::filesystem::path& path,
		const VansSceneVegetationNodeConfig& config,
		std::string& error);
};
}
