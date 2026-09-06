#include "VansAIBehaviorAsset.h"

#include "Serialization/VansAIBehaviorJsonCodec.h"
#include "../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

namespace Vans
{
const VansAIStateDefinition* VansAIBehaviorAsset::FindState(const std::string& id) const
{
	for (const VansAIStateDefinition& state : states)
		if (state.id == id) return &state;
	return nullptr;
}

bool VansAIBehaviorAssetStorage::Load(
	const std::filesystem::path& path,
	VansAIBehaviorAsset& outAsset,
	std::string& error)
{
	nlohmann::json root;
	return VansJsonFileStorage::Read(path, root, error)
		&& VansAIBehaviorJsonCodec::Decode(root, outAsset, error);
}

bool VansAIBehaviorAssetStorage::SaveAtomic(
	const std::filesystem::path& path,
	const VansAIBehaviorAsset& asset,
	std::string& error)
{
	nlohmann::json root;
	return VansAIBehaviorJsonCodec::Encode(asset, root, error)
		&& VansJsonFileStorage::WriteAtomic(path, root, error);
}
}
