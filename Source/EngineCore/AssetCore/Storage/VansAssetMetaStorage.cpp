#include "VansAssetMetaStorage.h"

#include "../Serialization/VansAssetMetaLegacyJsonCodec.h"
#include "VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

#include <exception>

namespace Vans
{
bool VansAssetMetaStorage::Load(
    const std::filesystem::path& metaPath,
    VansAssetMeta& result,
    std::string& error)
{
    try
    {
        nlohmann::ordered_json root;
        if (!VansJsonFileStorage::Read(metaPath, root, error))
            return false;
        return VansAssetMetaLegacyJsonCodec::Decode(root, metaPath, result, error);
    }
    catch (const std::exception& exception)
    {
        error = exception.what();
        return false;
    }
}

bool VansAssetMetaStorage::SaveAtomic(
    const std::filesystem::path& metaPath,
    const VansAssetMeta& meta,
    std::string& error)
{
    nlohmann::ordered_json root;
    if (!VansAssetMetaLegacyJsonCodec::Encode(meta, root, error))
        return false;

    if (!VansJsonFileStorage::WriteAtomic(metaPath, root, error))
        return false;

    VansAssetMeta verification;
    if (!Load(metaPath, verification, error) || verification.guid != meta.guid)
    {
        if (error.empty())
            error = "Published asset meta verification failed";
        return false;
    }
    return true;
}

bool VansAssetMetaStorage::StageSave(
    const std::filesystem::path& metaPath,
    const VansAssetMeta& meta,
    VansStagedFile& stage,
    std::string& error)
{
    nlohmann::ordered_json root;
    if (!VansAssetMetaLegacyJsonCodec::Encode(meta, root, error))
        return false;
    return VansJsonFileStorage::StageWrite(metaPath, root, stage, error);
}
}
