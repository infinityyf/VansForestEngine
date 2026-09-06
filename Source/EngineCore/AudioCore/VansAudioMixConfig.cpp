#include "VansAudioMixConfig.h"

#include "Serialization/VansAudioMixConfigJsonCodec.h"
#include "../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

namespace VansEngine
{
bool VansAudioMixConfigStorage::Load(
	const std::filesystem::path& path,
	AudioMixConfig& config,
	std::string& error)
{
	nlohmann::json root;
	if (!Vans::VansJsonFileStorage::Read(path, root, error))
		return false;
	if (!VansAudioMixConfigJsonCodec::Decode(root, config, error))
	{
		error = "Cannot decode audio mix config '" + path.string() + "': " + error;
		return false;
	}
	return true;
}

bool VansAudioMixConfigStorage::SaveAtomic(
	const std::filesystem::path& path,
	const AudioMixConfig& config,
	std::string& error)
{
	return Vans::VansJsonFileStorage::WriteAtomic(
		path, VansAudioMixConfigJsonCodec::Encode(config), error);
}
}
