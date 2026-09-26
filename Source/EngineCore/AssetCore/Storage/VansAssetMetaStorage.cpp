#include "VansAssetMetaStorage.h"

#include "../Serialization/VansAssetMetaJsonCodec.h"
#include "../Serialization/VansSerializedValueJsonAdapter.h"
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
        return VansAssetMetaJsonCodec::Decode(
            DecodeSerializedValueJson(root), metaPath, result, error);
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
    VansSerializedValue serialized;
    if (!VansAssetMetaJsonCodec::Encode(meta, serialized, error))
        return false;

    const nlohmann::ordered_json root =
        EncodeSerializedValueJson<nlohmann::ordered_json>(serialized);

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
    VansSerializedValue serialized;
    if (!VansAssetMetaJsonCodec::Encode(meta, serialized, error))
        return false;
    const nlohmann::ordered_json root =
        EncodeSerializedValueJson<nlohmann::ordered_json>(serialized);
    return VansJsonFileStorage::StageWrite(metaPath, root, stage, error);
}
}
