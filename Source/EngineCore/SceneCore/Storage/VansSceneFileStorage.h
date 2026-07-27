#pragma once

#include "../VansRuntimeSceneJson.h"
#include "../VansSceneJson.h"

#include <filesystem>
#include <string>

namespace Vans
{
struct VansStagedFile;

class VansSceneFileStorage
{
public:
	static bool ReadLegacySceneDocument(
		const std::filesystem::path& path,
		SceneJson& root,
		std::string& error);
	static bool ReadLegacySceneDocument(
		const std::filesystem::path& path,
		RuntimeSceneJson& root,
		std::string& error);
	static bool CreateEmptySceneDocument(const std::filesystem::path& path, std::string& error);
	static bool StageLegacySceneDocument(
		const std::filesystem::path& path,
		const SceneJson& root,
		VansStagedFile& stage,
		std::string& error);
	static bool WriteLegacySceneDocument(
		const std::filesystem::path& path,
		const SceneJson& root,
		std::string& error);
};
}
