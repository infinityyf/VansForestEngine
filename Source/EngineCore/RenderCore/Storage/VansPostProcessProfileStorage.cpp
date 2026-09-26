#include "VansPostProcessProfileStorage.h"

#include "../Serialization/VansPostProcessProfileJsonCodec.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"

#include <nlohmann/json.hpp>

namespace VansGraphics
{
bool VansPostProcessProfileStorage::Load(
	const std::filesystem::path& filePath,
	VansPostProcessProfile& profile,
	std::string& error)
{
	nlohmann::ordered_json root;
	if (!Vans::VansJsonFileStorage::Read(filePath, root, error))
		return false;
	return VansPostProcessProfileJsonCodec::Decode(
		Vans::DecodeSerializedValueJson(root), filePath, profile, error);
}

bool VansPostProcessProfileStorage::SaveAtomic(
	const std::filesystem::path& filePath,
	const VansPostProcessProfile& profile,
	std::string& error)
{
	return Vans::VansJsonFileStorage::WriteAtomic(
		filePath,
		Vans::EncodeSerializedValueJson<nlohmann::ordered_json>(
			VansPostProcessProfileJsonCodec::Encode(profile)),
		error);
}
}
