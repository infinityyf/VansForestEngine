#pragma once

#include "../VansAssetMeta.h"
#include "../VansModelAsset.h"

#include <cstddef>
#include <string>

namespace Vans
{
struct VansSkeletonSubAssetRefreshResult
{
    bool succeeded = false;
    bool hasSkeleton = false;
    std::size_t boneSubAssetCount = 0;
    std::string error;
};

class VansModelImporter
{
public:
    static constexpr std::uint32_t Version = 1;

    VansSkeletonSubAssetRefreshResult RefreshSkeletonSubAssets(
        const std::filesystem::path& sourcePath,
        VansAssetMeta& meta) const;
    VansModelImportResult Import(const std::filesystem::path& sourcePath,
        VansAssetMeta& meta, const VansModelImportSettings& settings) const;
};
}
