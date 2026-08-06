#pragma once

#include "../VansSceneJson.h"

#include <filesystem>
#include <string>

namespace Vans
{
struct VansStagedFile;

class VansSceneFileStorage
{
public:
	static bool ReadSceneDocument(
		const std::filesystem::path& path,
		SceneJson& root,
		std::string& error);
	static bool CreateEmptySceneDocument(const std::filesystem::path& path, std::string& error);
	static bool StageSceneDocument(
		const std::filesystem::path& path,
		const SceneJson& root,
		VansStagedFile& stage,
		std::string& error);
	static bool WriteSceneDocument(
		const std::filesystem::path& path,
		const SceneJson& root,
		std::string& error);
};
}
