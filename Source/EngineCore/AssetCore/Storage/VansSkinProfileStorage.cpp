#include "VansSkinProfileStorage.h"

#include "../Serialization/VansSkinProfileJsonCodec.h"
#include "VansJsonFileStorage.h"

namespace Vans
{
bool VansSkinProfileStorage::Load(
	const std::filesystem::path& filePath,
	VansSkinProfile& profile,
	std::string& error)
{
	SkinProfileJson root;
	if (!VansJsonFileStorage::Read(filePath, root, error))
		return false;
	return VansSkinProfileJsonCodec::Decode(root, filePath, profile, error);
}

bool VansSkinProfileStorage::SaveAtomic(
	const std::filesystem::path& filePath,
	const VansSkinProfile& profile,
	std::string& error)
{
	return VansJsonFileStorage::WriteAtomic(
		filePath,
		VansSkinProfileJsonCodec::Encode(profile),
		error);
}
}
