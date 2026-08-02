#pragma once

#include "../VansAudioReverbPresetAsset.h"
#include "../../AssetCore/Storage/VansStagedFileTransaction.h"

#include <filesystem>
#include <string>

namespace Vans
{
class VansAudioReverbPresetAssetStorage
{
public:
    static bool Load(
        const std::filesystem::path& path,
        VansAudioReverbPresetAsset& asset,
        std::string& error);

    static bool StageWrite(
        const std::filesystem::path& path,
        const VansAudioReverbPresetAsset& asset,
        VansStagedFile& stage,
        std::string& error);

    static bool SaveAtomic(
        const std::filesystem::path& path,
        const VansAudioReverbPresetAsset& asset,
        std::string& error);
};
}
