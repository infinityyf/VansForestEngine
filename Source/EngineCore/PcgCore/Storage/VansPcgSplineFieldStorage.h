#pragma once
#include "../VansPcgSplineField.h"
#include <filesystem>

namespace Vans
{
class VansPcgSplineFieldStorage
{
public:
    static const char* CompilerFingerprint();
    static std::filesystem::path CachePath(const std::filesystem::path& projectRoot,VansAssetGuid guid);
    static bool Save(const std::filesystem::path& path,const VansPcgSplineFieldSnapshot& field,std::string& error);
    static std::shared_ptr<const VansPcgSplineFieldSnapshot> Load(const std::filesystem::path& path,
        const VansPcgSplineAsset& source,std::shared_ptr<const VansTerrainAsset> base,std::string& error);
};
}
