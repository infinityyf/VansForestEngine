#pragma once

#include "../VansNavigationMeshAsset.h"
#include <string_view>

namespace Vans
{
class VansNavigationMeshCodec
{
public:
    static constexpr std::size_t maximumAssetBytes = 256u * 1024u * 1024u;
    static bool Encode(const VansNavigationMeshAsset& asset, std::string& bytes, std::string& error);
    static bool Decode(std::string_view bytes, std::shared_ptr<const VansNavigationMeshAsset>& asset,
        std::string& error);
};
}
