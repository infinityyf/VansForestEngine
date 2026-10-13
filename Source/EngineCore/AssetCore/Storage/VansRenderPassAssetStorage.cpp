#include "VansRenderPassAssetStorage.h"
#include "../VansAssetDocument.h"

namespace Vans
{
bool VansRenderPassAssetStorage::Load(const std::filesystem::path& path,
    VansRenderPassAsset& asset, std::string& error)
{
    VansAssetDocument document;
    return document.Load(path, error) &&
        ReadRenderPassAsset(document.SerializedRootSnapshot(), asset, error);
}
}
