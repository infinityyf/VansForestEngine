#pragma once
#include "../VansRenderPassAsset.h"
#include <filesystem>

namespace Vans
{
class VansRenderPassAssetStorage
{
public:
    static bool Load(const std::filesystem::path& path, VansRenderPassAsset& asset, std::string& error);
};
}
