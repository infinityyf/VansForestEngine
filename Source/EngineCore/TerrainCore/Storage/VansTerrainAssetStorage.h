#pragma once

#include "../VansTerrainAsset.h"

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace Vans
{
class VansTerrainAssetStorage
{
public:
	using TexturePathResolver =
		std::function<std::optional<std::filesystem::path>(VansAssetGuid)>;

	static bool Load(
		const std::filesystem::path& sourcePath,
		const TexturePathResolver& resolveTexturePath,
		VansTerrainAsset& asset,
		std::string& error);
};
}
