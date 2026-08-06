#include "VansAudioDuckingRulesAssetStorage.h"

#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"
#include "../../AssetCore/VansAssetDocument.h"
#include "../../AssetCore/VansAssetDocumentJson.h"

#include <nlohmann/json.hpp>

namespace Vans
{
bool VansAudioDuckingRulesAssetStorage::Load(
    const std::filesystem::path& path,
    VansAudioDuckingRulesAsset& asset,
    std::string& error)
{
    VansAssetDocument document;
    if (!document.Load(path, error))
        return false;
    return ReadAudioDuckingRulesAsset(document.SerializedRootSnapshot(), asset, error);
}

bool VansAudioDuckingRulesAssetStorage::StageWrite(
    const std::filesystem::path& path,
    const VansAudioDuckingRulesAsset& asset,
    VansStagedFile& stage,
    std::string& error)
{
    const AssetDocumentJson root =
        EncodeSerializedValueJson<AssetDocumentJson>(WriteAudioDuckingRulesAssetRoot(asset));
    return VansJsonFileStorage::StageWrite(path, root, stage, error);
}

bool VansAudioDuckingRulesAssetStorage::SaveAtomic(
    const std::filesystem::path& path,
    const VansAudioDuckingRulesAsset& asset,
    std::string& error)
{
    const AssetDocumentJson root =
        EncodeSerializedValueJson<AssetDocumentJson>(WriteAudioDuckingRulesAssetRoot(asset));
    return VansJsonFileStorage::WriteAtomic(path, root, error);
}
}
