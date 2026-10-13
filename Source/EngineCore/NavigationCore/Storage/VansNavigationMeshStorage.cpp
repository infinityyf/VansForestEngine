#include "VansNavigationMeshStorage.h"
#include "../Serialization/VansNavigationMeshCodec.h"
#include "../../AssetCore/Storage/VansFileStorage.h"
#include "../../AssetCore/Storage/VansStagedFileTransaction.h"
#include <new>

namespace Vans
{
bool VansNavigationMeshStorage::Load(const std::filesystem::path& path,
    std::shared_ptr<const VansNavigationMeshAsset>& asset, std::string& error)
try
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) { error = "Could not read navigation asset size: " + ec.message(); return false; }
    if (size == 0 || size > VansNavigationMeshCodec::maximumAssetBytes)
    { error = "Navigation asset exceeds byte capacity or is empty"; return false; }
    std::string bytes;
    return VansFileStorage::ReadByteRange(path,0,size,bytes,error) &&
        VansNavigationMeshCodec::Decode(bytes,asset,error);
}
catch(const std::bad_alloc&) { error="Navigation asset load allocation failed"; return false; }


bool VansNavigationMeshStorage::Save(const std::filesystem::path& path,
    const VansNavigationMeshAsset& asset, std::string& error)
{
    VansStagedFile stage;
    if (!StageSave(path, asset, stage, error)) return false;
    VansStagedFileTransaction transaction;
    transaction.Add(std::move(stage));
    return transaction.Publish(error);
}

bool VansNavigationMeshStorage::StageSave(const std::filesystem::path& path,
    const VansNavigationMeshAsset& asset, VansStagedFile& stage, std::string& error)
{
    std::string bytes;
    return VansNavigationMeshCodec::Encode(asset, bytes, error) &&
        VansFileStorage::StageWriteBytes(path, bytes, stage, error);
}
}
