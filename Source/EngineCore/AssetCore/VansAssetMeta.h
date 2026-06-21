#pragma once

#include "VansAssetGuid.h"

#include <filesystem>
#include <map>
#include <nlohmann/json.hpp>
#include <string>

namespace Vans
{
struct VansAssetMeta
{
    VansAssetGuid guid;
    std::string importer;
    std::uint32_t version = 1;
    nlohmann::ordered_json settings = nlohmann::ordered_json::object();
    std::map<std::string, VansSubAssetId> subAssets;

    static std::filesystem::path MetaPathFor(const std::filesystem::path& sourcePath);
    static bool Load(const std::filesystem::path& metaPath, VansAssetMeta& result, std::string& error);
    bool SaveAtomic(const std::filesystem::path& metaPath, std::string& error) const;
};
}
