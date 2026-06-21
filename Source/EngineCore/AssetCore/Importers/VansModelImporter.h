#pragma once

#include "../VansAssetMeta.h"
#include "../VansModelAsset.h"

namespace Vans
{
class VansModelImporter
{
public:
    static constexpr std::uint32_t Version = 1;

    VansModelImportResult Import(const std::filesystem::path& sourcePath,
        VansAssetMeta& meta, const VansModelImportSettings& settings) const;
};
}
