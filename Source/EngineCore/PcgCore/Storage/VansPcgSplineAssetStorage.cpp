#include "VansPcgSplineAssetStorage.h"
#include "../Serialization/VansPcgSplineAssetCodec.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"
#include <nlohmann/json.hpp>

namespace Vans
{
bool VansPcgSplineAssetStorage::Load(const std::filesystem::path& path, VansPcgSplineAsset& asset, std::string& error)
{
    nlohmann::ordered_json root;
    return VansJsonFileStorage::Read(path,root,error) && VansPcgSplineAssetCodec::Decode(DecodeSerializedValueJson(root),asset,error);
}
bool VansPcgSplineAssetStorage::SaveAtomic(const std::filesystem::path& path, const VansPcgSplineAsset& asset, std::string& error)
{
    VansSerializedValue root;
    return VansPcgSplineAssetCodec::Encode(asset,root,error) &&
        VansJsonFileStorage::WriteAtomic(path,EncodeSerializedValueJson<nlohmann::ordered_json>(root),error);
}
}
