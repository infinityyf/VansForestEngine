#include "VansRetargetProfileStorage.h"

#include "../Serialization/VansRetargetProfileJsonCodec.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

namespace VansGraphics
{
bool VansRetargetProfileStorage::Load(
	const std::filesystem::path& path,
	VansRetargetProfileAsset& asset,
	std::string& error)
{
	nlohmann::json root;
	return Vans::VansJsonFileStorage::Read(path, root, error)
		&& VansRetargetProfileJsonCodec::Decode(root, asset, error);
}

bool VansRetargetProfileStorage::SaveAtomic(
	const std::filesystem::path& path,
	const VansRetargetProfileAsset& asset,
	std::string& error)
{
	nlohmann::json root;
	return VansRetargetProfileJsonCodec::Encode(asset, root, error)
		&& Vans::VansJsonFileStorage::WriteAtomic(path, root, error);
}
}
