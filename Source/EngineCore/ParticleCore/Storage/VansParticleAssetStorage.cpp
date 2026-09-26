#include "VansParticleAssetStorage.h"

#include "../Serialization/VansParticleAssetJsonCodec.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"
#include "../../AssetCore/VansAssetDocument.h"
#include "../../AssetCore/VansAssetDocumentJson.h"

#include <nlohmann/json.hpp>

#include <exception>

namespace VansGraphics
{
bool VansParticleAssetStorage::Load(
    const std::filesystem::path& filePath,
    VansParticleAsset& asset,
    std::string& error)
{
    Vans::VansAssetDocument document;
    if (!document.Load(filePath, error))
        return false;
    return VansParticleAssetJsonCodec::Decode(
        document.SerializedRootSnapshot(), filePath, asset, error);
}

bool VansParticleAssetStorage::StageWrite(
    const std::filesystem::path& filePath,
    const VansParticleAsset& asset,
    Vans::VansStagedFile& stage,
    std::string& error)
{
    try
    {
        const Vans::AssetDocumentJson root =
            Vans::EncodeSerializedValueJson<Vans::AssetDocumentJson>(
                VansParticleAssetJsonCodec::Encode(asset));
        return Vans::VansJsonFileStorage::StageWrite(filePath, root, stage, error);
    }
    catch (const std::exception& exception)
    {
        error = "Invalid particle asset for staged save " + filePath.string() +
            ": " + exception.what();
        return false;
    }
}

bool VansParticleAssetStorage::SaveAtomic(
    const std::filesystem::path& filePath,
    const VansParticleAsset& asset,
    std::string& error)
{
    try
    {
        const Vans::AssetDocumentJson root =
            Vans::EncodeSerializedValueJson<Vans::AssetDocumentJson>(
                VansParticleAssetJsonCodec::Encode(asset));
        return Vans::VansJsonFileStorage::WriteAtomic(filePath, root, error);
    }
    catch (const std::exception& exception)
    {
        error = "Invalid particle asset for save " + filePath.string() + ": " + exception.what();
        return false;
    }
}
}
