#pragma once

#include "../VansMaterialAuthoringAsset.h"
#include "VansStagedFileTransaction.h"

#include <filesystem>
#include <string>

namespace Vans
{
class VansMaterialAuthoringAssetStorage
{
public:
    static bool Load(
        const std::filesystem::path& path,
        VansMaterialAuthoringAsset& asset,
        std::string& error);

    static bool StageWrite(
        const std::filesystem::path& path,
        const VansMaterialAuthoringAsset& asset,
        VansStagedFile& stage,
        std::string& error);
};
}
