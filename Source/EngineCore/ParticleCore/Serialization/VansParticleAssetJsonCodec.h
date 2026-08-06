#pragma once

#include "../VansParticleAsset.h"
#include "../VansParticleJson.h"

#include <filesystem>
#include <string>

namespace VansGraphics
{
class VansParticleAssetJsonCodec
{
public:
    static Vans::ParticleJson Encode(const VansParticleAsset& asset);
    static bool Decode(
        const Vans::ParticleJson& root,
        const std::filesystem::path& filePath,
        VansParticleAsset& asset,
        std::string& error);
};
}
