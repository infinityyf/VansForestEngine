#pragma once
#include "../VansPcgSplineAsset.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"

namespace Vans
{
class VansPcgSplineAssetCodec
{
public:
    static bool Decode(const VansSerializedValue& root, VansPcgSplineAsset& asset, std::string& error);
    static bool Encode(const VansPcgSplineAsset& asset, VansSerializedValue& root, std::string& error);
    static std::uint64_t ContentHash(const VansPcgSplineAsset& asset);
};
}
