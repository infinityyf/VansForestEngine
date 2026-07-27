#include "VansSceneFileStorage.h"

#include "../VansSceneSchema.h"
#include "../../AssetCore/Storage/VansJsonFileStorage.h"

#include <nlohmann/json.hpp>

namespace Vans
{
bool VansSceneFileStorage::ReadLegacySceneDocument(
	const std::filesystem::path& path,
	SceneJson& root,
	std::string& error)
{
	return VansJsonFileStorage::Read(path, root, error);
}

bool VansSceneFileStorage::ReadLegacySceneDocument(
	const std::filesystem::path& path,
	RuntimeSceneJson& root,
	std::string& error)
{
	return VansJsonFileStorage::Read(path, root, error);
}

bool VansSceneFileStorage::CreateEmptySceneDocument(const std::filesystem::path& path, std::string& error)
{
	VansSceneData sceneData;
	sceneData.sceneGuid = VansAssetGuid::New();
	const SceneJson root = VansSceneSchema::SerializeLegacyJson(sceneData);
	return WriteLegacySceneDocument(path, root, error);
}

bool VansSceneFileStorage::StageLegacySceneDocument(
	const std::filesystem::path& path,
	const SceneJson& root,
	VansStagedFile& stage,
	std::string& error)
{
	return VansJsonFileStorage::StageWrite(path, root, stage, error);
}

bool VansSceneFileStorage::WriteLegacySceneDocument(
	const std::filesystem::path& path,
	const SceneJson& root,
	std::string& error)
{
	return VansJsonFileStorage::WriteAtomic(path, root, error);
}
}
