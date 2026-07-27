#pragma once

#include "../VansAssetMeta.h"
#include "VansStagedFileTransaction.h"

#include <filesystem>
#include <string>

namespace Vans
{
class VansAssetMetaStorage
{
public:
    static bool Load(const std::filesystem::path& metaPath, VansAssetMeta& result, std::string& error);
    static bool StageSave(
        const std::filesystem::path& metaPath,
        const VansAssetMeta& meta,
        VansStagedFile& stage,
        std::string& error);
    static bool SaveAtomic(const std::filesystem::path& metaPath, const VansAssetMeta& meta, std::string& error);
};
}
