#pragma once

#include "../VansShaderAuthoringAsset.h"

#include <filesystem>
#include <string>

namespace Vans
{
class VansShaderAuthoringAssetStorage
{
public:
    static bool Load(
        const std::filesystem::path& path,
        VansShaderAuthoringAsset& asset,
        std::string& error);
};
}
