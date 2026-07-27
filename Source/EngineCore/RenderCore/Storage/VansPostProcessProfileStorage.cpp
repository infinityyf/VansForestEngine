#include "VansPostProcessProfileStorage.h"

#include "../Serialization/VansPostProcessProfileLegacyJsonCodec.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

namespace VansGraphics
{
bool VansPostProcessProfileStorage::Load(
	const std::filesystem::path& filePath,
	VansPostProcessProfile& profile,
	std::string& error)
{
	PostProcessProfileJson root;
	if (!Vans::VansJsonFileStorage::Read(filePath, root, error))
		return false;
	return VansPostProcessProfileLegacyJsonCodec::Decode(root, filePath, profile, error);
}

bool VansPostProcessProfileStorage::SaveAtomic(
	const std::filesystem::path& filePath,
	const VansPostProcessProfile& profile,
	std::string& error)
{
	return Vans::VansJsonFileStorage::WriteAtomic(
		filePath,
		VansPostProcessProfileLegacyJsonCodec::Encode(profile),
		error);
}
}
