#pragma once

#include "../VansAssetMeta.h"

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <string>

namespace Vans
{
class VansAssetMetaJsonCodec
{
public:
    static bool Encode(const VansAssetMeta& meta, nlohmann::ordered_json& root, std::string& error);
    static bool Decode(
        const nlohmann::ordered_json& root,
        const std::filesystem::path& metaPath,
        VansAssetMeta& result,
        std::string& error);
};
}
