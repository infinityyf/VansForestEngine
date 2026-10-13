#pragma once

#include "../VansNavigationMeshAsset.h"
#include <filesystem>

namespace Vans
{
struct VansStagedFile;
class VansNavigationMeshStorage
{
public:
    static bool Load(const std::filesystem::path& path,
        std::shared_ptr<const VansNavigationMeshAsset>& asset, std::string& error);
    static bool Save(const std::filesystem::path& path,
        const VansNavigationMeshAsset& asset, std::string& error);
    static bool StageSave(const std::filesystem::path& path,
        const VansNavigationMeshAsset& asset, VansStagedFile& stage, std::string& error);
};
}
