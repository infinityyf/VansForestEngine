#pragma once

#include "../VansAudioBusSnapshotAsset.h"
#include "../../AssetCore/Storage/VansStagedFileTransaction.h"

#include <filesystem>
#include <string>

namespace Vans
{
class VansAudioBusSnapshotAssetStorage
{
public:
    static bool Load(
        const std::filesystem::path& path,
        VansAudioBusSnapshotAsset& asset,
        std::string& error);

    static bool StageWrite(
        const std::filesystem::path& path,
        const VansAudioBusSnapshotAsset& asset,
        VansStagedFile& stage,
        std::string& error);

    static bool SaveAtomic(
        const std::filesystem::path& path,
        const VansAudioBusSnapshotAsset& asset,
        std::string& error);
};
}
