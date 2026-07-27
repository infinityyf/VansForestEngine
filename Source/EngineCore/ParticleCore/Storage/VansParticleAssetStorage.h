#pragma once

#include "../VansParticleAsset.h"

#include <filesystem>
#include <string>

namespace VansGraphics
{
class VansParticleAssetStorage
{
public:
    static bool Load(const std::filesystem::path& filePath, VansParticleAsset& asset, std::string& error);
    static bool SaveAtomic(const std::filesystem::path& filePath, const VansParticleAsset& asset, std::string& error);
};
}
