#pragma once

#include "../VansParticleAsset.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"

#include <filesystem>
#include <string>

namespace VansGraphics
{
class VansParticleAssetJsonCodec
{
public:
    static Vans::VansSerializedValue Encode(const VansParticleAsset& asset);
    static bool Decode(
        const Vans::VansSerializedValue& root,
        const std::filesystem::path& filePath,
        VansParticleAsset& asset,
        std::string& error);
};
}
