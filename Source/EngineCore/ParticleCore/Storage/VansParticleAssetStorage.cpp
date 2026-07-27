#include "VansParticleAssetStorage.h"

#include "../Serialization/VansParticleAssetLegacyJsonCodec.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

namespace VansGraphics
{
bool VansParticleAssetStorage::Load(
    const std::filesystem::path& filePath,
    VansParticleAsset& asset,
    std::string& error)
{
    Vans::ParticleJson root;
    if (!Vans::VansJsonFileStorage::Read(filePath, root, error))
        return false;
    return VansParticleAssetLegacyJsonCodec::Decode(root, filePath, asset, error);
}

bool VansParticleAssetStorage::SaveAtomic(
    const std::filesystem::path& filePath,
    const VansParticleAsset& asset,
    std::string& error)
{
    return Vans::VansJsonFileStorage::WriteAtomic(
        filePath,
        VansParticleAssetLegacyJsonCodec::Encode(asset),
        error);
}
}
