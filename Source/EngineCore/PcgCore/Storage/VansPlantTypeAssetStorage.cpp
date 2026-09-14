#include "VansPlantTypeAssetStorage.h"
#include "../Serialization/VansPlantTypeAssetCodec.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

namespace Vans
{
bool VansPlantTypeAssetStorage::Load(const std::filesystem::path& path, VansPlantTypeAsset& asset, std::string& error)
{
	nlohmann::ordered_json root;
	return VansJsonFileStorage::Read(path, root, error) &&
		VansPlantTypeAssetCodec::Decode(DecodeSerializedValueJson(root), asset, error);
}

bool VansPlantTypeAssetStorage::SaveAtomic(const std::filesystem::path& path, const VansPlantTypeAsset& asset, std::string& error)
{
	VansSerializedValue root;
	return VansPlantTypeAssetCodec::Encode(asset, root, error) &&
		VansJsonFileStorage::WriteAtomic(path, EncodeSerializedValueJson<nlohmann::ordered_json>(root), error);
}
}
