#include "VansMaterialAuthoringAssetStorage.h"

#include "../Serialization/VansSerializedValueLegacyJsonAdapter.h"
#include "../VansAssetDocument.h"
#include "../VansAssetDocumentJson.h"
#include "VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

namespace Vans
{
bool VansMaterialAuthoringAssetStorage::Load(
    const std::filesystem::path& path,
    VansMaterialAuthoringAsset& asset,
    std::string& error)
{
    VansAssetDocument document;
    if (!document.Load(path, error))
        return false;
    return ReadMaterialAuthoringAsset(document.SerializedRootSnapshot(), asset, error);
}

bool VansMaterialAuthoringAssetStorage::StageWrite(
    const std::filesystem::path& path,
    const VansMaterialAuthoringAsset& asset,
    VansStagedFile& stage,
    std::string& error)
{
    const AssetDocumentJson root =
        EncodeSerializedValueLegacyJson<AssetDocumentJson>(WriteMaterialAuthoringAssetRoot(asset));
    return VansJsonFileStorage::StageWrite(path, root, stage, error);
}
}
