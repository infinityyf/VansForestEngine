#include "VansAudioReverbPresetAssetStorage.h"

#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"
#include "../../AssetCore/VansAssetDocument.h"
#include "../../AssetCore/VansAssetDocumentJson.h"

#include <nlohmann/json.hpp>

namespace Vans
{
bool VansAudioReverbPresetAssetStorage::Load(
    const std::filesystem::path& path,
    VansAudioReverbPresetAsset& asset,
    std::string& error)
{
    VansAssetDocument document;
    if (!document.Load(path, error))
        return false;
    return ReadAudioReverbPresetAsset(document.SerializedRootSnapshot(), asset, error);
}

bool VansAudioReverbPresetAssetStorage::StageWrite(
    const std::filesystem::path& path,
    const VansAudioReverbPresetAsset& asset,
    VansStagedFile& stage,
    std::string& error)
{
    const AssetDocumentJson root =
        EncodeSerializedValueJson<AssetDocumentJson>(WriteAudioReverbPresetAssetRoot(asset));
    return VansJsonFileStorage::StageWrite(path, root, stage, error);
}

bool VansAudioReverbPresetAssetStorage::SaveAtomic(
    const std::filesystem::path& path,
    const VansAudioReverbPresetAsset& asset,
    std::string& error)
{
    const AssetDocumentJson root =
        EncodeSerializedValueJson<AssetDocumentJson>(WriteAudioReverbPresetAssetRoot(asset));
    return VansJsonFileStorage::WriteAtomic(path, root, error);
}
}
