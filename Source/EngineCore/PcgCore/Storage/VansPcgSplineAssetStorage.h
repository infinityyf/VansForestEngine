#pragma once
#include "../VansPcgSplineAsset.h"
#include <filesystem>

namespace Vans
{
class VansPcgSplineAssetStorage
{
public:
    static bool Load(const std::filesystem::path& path, VansPcgSplineAsset& asset, std::string& error);
    static bool SaveAtomic(const std::filesystem::path& path, const VansPcgSplineAsset& asset, std::string& error);
};
}
