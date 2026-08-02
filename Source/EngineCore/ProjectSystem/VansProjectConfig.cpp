#include "VansProjectConfig.h"

#include "Storage/VansProjectConfigStorage.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <ShlObj.h>
#pragma comment(lib, "Shell32.lib")
#endif

namespace fs = std::filesystem;

namespace
{
std::string NowISO8601()
{
	auto now = std::chrono::system_clock::now();
	std::time_t time = std::chrono::system_clock::to_time_t(now);
	std::tm localTime{};
#ifdef _WIN32
	localtime_s(&localTime, &time);
#else
	localtime_r(&time, &localTime);
#endif
	std::ostringstream stream;
	stream << std::put_time(&localTime, "%Y-%m-%dT%H:%M:%S");
	return stream.str();
}
}

namespace Vans
{
void VansProjectConfig::SetDefaults(const std::string& name)
{
	projectName = name;
	engineVersion = "0.1.0";
	createdAt = NowISO8601();
	defaultScene = "Scenes/MainScene.json";
	assetsRoot = "Assets";
	importedArtifactRoot = "Library/Artifacts";
	metaExtension = ".meta";
	runtimeAssetBindings.clear();

	assetDirectories = {
		{ "models",    "Assets/Models" },
		{ "textures",  "Assets/Textures" },
		{ "materials", "Assets/Materials" },
		{ "scripts",   "Scripts" },
	};

	scriptSearchPaths = { "Scripts" };
	renderSettings = "ProjectSettings/RenderSettings.json";
	physicsSettings = "ProjectSettings/PhysicsSettings.json";
	audioSettings = "ProjectSettings/AudioMix.json";
	collisionLayerSettings = "ProjectSettings/PhysicsLayers.json";
}

bool VansProjectConfig::LoadFromFile(const std::string& filePath)
{
	std::string error;
	if (!VansProjectConfigStorage::LoadProjectConfig(filePath, *this, error))
	{
		VANS_LOG_ERROR("[ProjectConfig] " << error);
		return false;
	}

	VANS_LOG("[ProjectConfig] Loaded: " << filePath);
	return true;
}

bool VansProjectConfig::SaveToFile(const std::string& filePath) const
{
	std::string error;
	if (!VansProjectConfigStorage::SaveProjectConfig(filePath, *this, error))
	{
		VANS_LOG_ERROR("[ProjectConfig] " << error);
		return false;
	}

	VANS_LOG("[ProjectConfig] Saved: " << filePath);
	return true;
}

namespace RecentProjects
{
static constexpr int kMaxRecent = 20;

std::string GetRecentProjectsFilePath()
{
#ifdef _WIN32
	char appData[MAX_PATH]{};
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appData)))
	{
		fs::path directory = fs::path(appData) / "ForestEngine";
		fs::create_directories(directory);
		return (directory / "RecentProjects.json").string();
	}
#endif
	return "RecentProjects.json";
}

std::vector<RecentProjectEntry> Load()
{
	std::vector<RecentProjectEntry> entries;
	std::string error;
	VansProjectConfigStorage::LoadRecentProjects(GetRecentProjectsFilePath(), entries, error);
	return entries;
}

void Save(const std::vector<RecentProjectEntry>& entries)
{
	const std::string path = GetRecentProjectsFilePath();
	std::string error;
	if (!VansProjectConfigStorage::SaveRecentProjects(path, entries, kMaxRecent, error))
		VANS_LOG_ERROR("[ProjectConfig] " << error);
}

void AddOrUpdate(
	const std::string& name,
	const std::string& projectPath,
	const std::string& engineVersion)
{
	auto entries = Load();

	entries.erase(
		std::remove_if(entries.begin(), entries.end(),
			[&](const RecentProjectEntry& entry) { return entry.path == projectPath; }),
		entries.end());

	RecentProjectEntry newEntry;
	newEntry.name = name;
	newEntry.path = projectPath;
	newEntry.lastOpened = NowISO8601();
	newEntry.engineVersion = engineVersion;
	entries.insert(entries.begin(), std::move(newEntry));

	if (static_cast<int>(entries.size()) > kMaxRecent)
		entries.resize(kMaxRecent);

	Save(entries);
}
}
} // namespace Vans
