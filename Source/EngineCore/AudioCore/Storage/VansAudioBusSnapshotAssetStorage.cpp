#include "VansAudioBusSnapshotAssetStorage.h"

#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"
#include "../../AssetCore/VansAssetDocument.h"
#include "../../AssetCore/VansAssetDocumentJson.h"

#include <nlohmann/json.hpp>

namespace Vans
{
bool VansAudioBusSnapshotAssetStorage::Load(
    const std::filesystem::path& path,
    VansAudioBusSnapshotAsset& asset,
    std::string& error)
{
    VansAssetDocument document;
    if (!document.Load(path, error))
        return false;
    return ReadAudioBusSnapshotAsset(document.SerializedRootSnapshot(), asset, error);
}

bool VansAudioBusSnapshotAssetStorage::StageWrite(
    const std::filesystem::path& path,
    const VansAudioBusSnapshotAsset& asset,
    VansStagedFile& stage,
    std::string& error)
{
    const AssetDocumentJson root =
        EncodeSerializedValueJson<AssetDocumentJson>(WriteAudioBusSnapshotAssetRoot(asset));
    return VansJsonFileStorage::StageWrite(path, root, stage, error);
}

bool VansAudioBusSnapshotAssetStorage::SaveAtomic(
    const std::filesystem::path& path,
    const VansAudioBusSnapshotAsset& asset,
    std::string& error)
{
    const AssetDocumentJson root =
        EncodeSerializedValueJson<AssetDocumentJson>(WriteAudioBusSnapshotAssetRoot(asset));
    return VansJsonFileStorage::WriteAtomic(path, root, error);
}
}
