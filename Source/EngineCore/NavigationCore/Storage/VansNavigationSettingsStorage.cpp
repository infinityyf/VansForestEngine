#include "VansNavigationSettingsStorage.h"

#include "../Serialization/VansNavigationSettingsJsonCodec.h"
#include "../../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

namespace Vans
{
bool VansNavigationSettingsStorage::Load(const std::filesystem::path& path,
	VansNavigationSettings& settings, std::string& error)
{
	nlohmann::json root;
	if (!VansJsonFileStorage::Read(path, root, error)) return false;
	return VansNavigationSettingsJsonCodec::Decode(DecodeSerializedValueJson(root), settings, error);
}

bool VansNavigationSettingsStorage::Save(const std::filesystem::path& path,
	const VansNavigationSettings& settings, std::string& error)
{
	if (!ValidateNavigationSettings(settings, error)) return false;
	return VansJsonFileStorage::WriteAtomic(path,
		EncodeSerializedValueJson<nlohmann::json>(VansNavigationSettingsJsonCodec::Encode(settings)), error);
}
}
