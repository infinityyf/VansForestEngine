#include "VansShaderAuthoringAssetStorage.h"

#include "../VansAssetDocument.h"

namespace Vans
{
bool VansShaderAuthoringAssetStorage::Load(
    const std::filesystem::path& path,
    VansShaderAuthoringAsset& asset,
    std::string& error)
{
    VansAssetDocument document;
    if (!document.Load(path, error))
        return false;
    return ReadShaderAuthoringAsset(document.SerializedRootSnapshot(), asset, error);
}
}
