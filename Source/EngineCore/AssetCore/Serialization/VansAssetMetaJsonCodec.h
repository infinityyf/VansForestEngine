#pragma once

#include "../VansAssetMeta.h"
#include "VansSerializedValue.h"

#include <filesystem>
#include <string>

namespace Vans
{
class VansAssetMetaJsonCodec
{
public:
    static bool Encode(
        const VansAssetMeta& meta,
        VansSerializedValue& root,
        std::string& error);
    static bool Decode(
        const VansSerializedValue& root,
        const std::filesystem::path& metaPath,
        VansAssetMeta& result,
        std::string& error);
};
}
