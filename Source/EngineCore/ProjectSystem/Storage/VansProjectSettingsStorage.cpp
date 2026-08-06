#include "VansProjectSettingsStorage.h"

#include "../Serialization/VansProjectSettingsJsonCodec.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

namespace Vans
{
bool VansProjectSettingsStorage::LoadRenderSettings(
	const std::string& filePath,
	VansProjectRenderSettingsData& settings,
	std::vector<std::string>& warnings,
	std::string& error)
{
	nlohmann::json root;
	if (!VansJsonFileStorage::Read(filePath, root, error))
		return false;
	return VansProjectSettingsJsonCodec::DecodeRenderSettings(root, settings, warnings, error);
}

bool VansProjectSettingsStorage::SaveRenderSettings(
	const std::string& filePath,
	const VansProjectRenderSettingsData& settings,
	std::string& error)
{
	const nlohmann::json root = VansProjectSettingsJsonCodec::EncodeRenderSettings(settings);
	return VansJsonFileStorage::WriteAtomic(filePath, root, error);
}

bool VansProjectSettingsStorage::LoadPhysicsSettings(
	const std::string& filePath,
	VansProjectPhysicsSettingsData& settings,
	std::string& error)
{
	nlohmann::json root;
	if (!VansJsonFileStorage::Read(filePath, root, error))
		return false;
	return VansProjectSettingsJsonCodec::DecodePhysicsSettings(root, settings, error);
}

bool VansProjectSettingsStorage::SavePhysicsSettings(
	const std::string& filePath,
	const VansProjectPhysicsSettingsData& settings,
	std::string& error)
{
	const nlohmann::json root = VansProjectSettingsJsonCodec::EncodePhysicsSettings(settings);
	return VansJsonFileStorage::WriteAtomic(filePath, root, error);
}
}
