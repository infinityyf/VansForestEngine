#pragma once

#include "../VansAudioDuckingRulesAsset.h"
#include "../../AssetCore/Storage/VansStagedFileTransaction.h"

#include <filesystem>
#include <string>

namespace Vans
{
class VansAudioDuckingRulesAssetStorage
{
public:
    static bool Load(
        const std::filesystem::path& path,
        VansAudioDuckingRulesAsset& asset,
        std::string& error);

    static bool StageWrite(
        const std::filesystem::path& path,
        const VansAudioDuckingRulesAsset& asset,
        VansStagedFile& stage,
        std::string& error);

    static bool SaveAtomic(
        const std::filesystem::path& path,
        const VansAudioDuckingRulesAsset& asset,
        std::string& error);
};
}
