#pragma once

#include "../VansPcgMaskAsset.h"
#include <filesystem>
#include <functional>
#include <optional>

namespace Vans
{
class VansPcgMaskAssetStorage
{
public:
	using PixelPathResolver = std::function<std::optional<std::filesystem::path>(VansAssetGuid)>;
	static bool Load(const std::filesystem::path& path, const PixelPathResolver& resolvePixels,
		VansPcgMaskAsset& asset, std::string& error);
};
}
