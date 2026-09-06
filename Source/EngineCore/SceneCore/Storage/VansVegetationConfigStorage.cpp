#include "VansVegetationConfigStorage.h"

#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

namespace Vans
{
bool VansVegetationConfigStorage::Load(
	const std::filesystem::path& path,
	VansVegetationConfigAsset& asset,
	std::string& error)
{
	nlohmann::ordered_json root;
	if (!VansJsonFileStorage::Read(path, root, error))
	{
		error = "Cannot read vegetation configuration " + path.string() + ": " + error;
		return false;
	}
	if (!VansVegetationConfigCodec::Decode(
		DecodeSerializedValueJson(root), asset, error))
	{
		error = "Invalid vegetation configuration " + path.string() + ": " + error;
		return false;
	}
	return true;
}

bool VansVegetationConfigStorage::SaveAtomic(
	const std::filesystem::path& path,
	const VansSceneVegetationNodeConfig& config,
	std::string& error)
{
	VansSerializedValue serialized;
	if (!VansVegetationConfigCodec::Encode(config, serialized, error))
		return false;
	const nlohmann::ordered_json root =
		EncodeSerializedValueJson<nlohmann::ordered_json>(serialized);
	return VansJsonFileStorage::WriteAtomic(path, root, error);
}
}
