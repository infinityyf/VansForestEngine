#include "VansProjectConfig.h"
#include "../Util/VansLog.h"

#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <sstream>

#ifdef _WIN32
#include <ShlObj.h>   // SHGetFolderPathA
#pragma comment(lib, "Shell32.lib")
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

// -----------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------
static std::string NowISO8601()
{
	auto now = std::chrono::system_clock::now();
	std::time_t t = std::chrono::system_clock::to_time_t(now);
	std::tm tm{};
#ifdef _WIN32
	localtime_s(&tm, &t);
#else
	localtime_r(&t, &tm);
#endif
	std::ostringstream oss;
	oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
	return oss.str();
}

// -----------------------------------------------------------------------
// VansProjectConfig
// -----------------------------------------------------------------------
namespace Vans {

void VansProjectConfig::SetDefaults(const std::string& name)
{
	projectName = name;
	engineVersion = "0.1.0";
	createdAt = NowISO8601();
	lastOpenedAt = createdAt;
	defaultScene = "Scenes/MainScene.json";
	sceneSchemaVersion = 2;
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
	collisionLayerSettings = "ProjectSettings/PhysicsLayers.json";
}

bool VansProjectConfig::LoadFromFile(const std::string& filePath)
{
	std::ifstream ifs(filePath);
	if (!ifs.is_open())
	{
		VANS_LOG_ERROR("[ProjectConfig] Cannot open: " << filePath);
		return false;
	}

	try
	{
		json j = json::parse(ifs);

		projectName    = j.value("projectName", "");
		engineVersion  = j.value("engineVersion", "0.1.0");
		createdAt      = j.value("createdAt", "");
		lastOpenedAt   = j.value("lastOpenedAt", "");
		defaultScene   = j.value("defaultScene", "Scenes/MainScene.json");
		sceneSchemaVersion = j.value("sceneSchemaVersion", 2u);
		if (sceneSchemaVersion != 2u)
		{
			VANS_LOG_ERROR("[ProjectConfig] Only Scene schema version 2 projects are supported");
			return false;
		}
		assetsRoot = "Assets";
		importedArtifactRoot = "Library/Artifacts";
		metaExtension = ".meta";
		runtimeAssetBindings.clear();
		if (j.contains("assetDatabase") && j["assetDatabase"].is_object())
		{
			const json& database = j["assetDatabase"];
			assetsRoot = database.value("assetsRoot", "Assets");
			importedArtifactRoot = database.value("importedArtifactRoot", "Library/Artifacts");
			metaExtension = database.value("metaExtension", ".meta");
		}
		if (j.contains("runtimeAssetBindings") && j["runtimeAssetBindings"].is_object())
		{
			for (const auto& [name, value] : j["runtimeAssetBindings"].items())
				if (value.is_string()) runtimeAssetBindings[name] = value.get<std::string>();
		}
		renderSettings = j.value("renderSettings", "ProjectSettings/RenderSettings.json");
		physicsSettings = j.value("physicsSettings", "ProjectSettings/PhysicsSettings.json");
		collisionLayerSettings = j.value("collisionLayerSettings", "ProjectSettings/PhysicsLayers.json");

		assetDirectories.clear();
		if (j.contains("assetDirectories") && j["assetDirectories"].is_object())
		{
			for (auto& [key, val] : j["assetDirectories"].items())
				assetDirectories[key] = val.get<std::string>();
		}

		scriptSearchPaths.clear();
		if (j.contains("scriptSearchPaths") && j["scriptSearchPaths"].is_array())
		{
			for (auto& v : j["scriptSearchPaths"])
				scriptSearchPaths.push_back(v.get<std::string>());
		}
	}
	catch (const json::exception& e)
	{
		VANS_LOG_ERROR("[ProjectConfig] JSON parse error: " << e.what());
		return false;
	}

	VANS_LOG("[ProjectConfig] Loaded: " << filePath);
	return true;
}

bool VansProjectConfig::SaveToFile(const std::string& filePath) const
{
	json j;
	j["projectName"]    = projectName;
	j["engineVersion"]  = engineVersion;
	j["createdAt"]      = createdAt;
	j["lastOpenedAt"]   = lastOpenedAt;
	j["defaultScene"]   = defaultScene;
	j["sceneSchemaVersion"] = 2;
	j["assetDatabase"] = {
		{ "assetsRoot", assetsRoot },
		{ "importedArtifactRoot", importedArtifactRoot },
		{ "metaExtension", metaExtension }
	};
	j["runtimeAssetBindings"] = runtimeAssetBindings;
	j["renderSettings"] = renderSettings;
	j["physicsSettings"] = physicsSettings;
	j["collisionLayerSettings"] = collisionLayerSettings;

	json dirs = json::object();
	for (auto& [k, v] : assetDirectories)
		dirs[k] = v;
	j["assetDirectories"] = dirs;

	j["scriptSearchPaths"] = scriptSearchPaths;

	// Ensure parent directory exists
	fs::path p(filePath);
	if (p.has_parent_path())
		fs::create_directories(p.parent_path());

	std::ofstream ofs(filePath);
	if (!ofs.is_open())
	{
		VANS_LOG_ERROR("[ProjectConfig] Cannot write: " << filePath);
		return false;
	}

	ofs << j.dump(4);
	VANS_LOG("[ProjectConfig] Saved: " << filePath);
	return true;
}

// -----------------------------------------------------------------------
// RecentProjects  (global, per-user)
// -----------------------------------------------------------------------
namespace RecentProjects {

static constexpr int kMaxRecent = 20;

std::string GetRecentProjectsFilePath()
{
#ifdef _WIN32
	char appData[MAX_PATH]{};
	if (SUCCEEDED(SHGetFolderPathA(NULL, CSIDL_APPDATA, NULL, 0, appData)))
	{
		fs::path dir = fs::path(appData) / "ForestEngine";
		fs::create_directories(dir);
		return (dir / "RecentProjects.json").string();
	}
#endif
	// Fallback: next to executable
	return "RecentProjects.json";
}

std::vector<RecentProjectEntry> Load()
{
	std::vector<RecentProjectEntry> result;
	std::string path = GetRecentProjectsFilePath();
	std::ifstream ifs(path);
	if (!ifs.is_open())
		return result;

	try
	{
		json j = json::parse(ifs);
		if (j.contains("recentProjects") && j["recentProjects"].is_array())
		{
			for (auto& item : j["recentProjects"])
			{
				RecentProjectEntry e;
				e.name          = item.value("name", "");
				e.path          = item.value("path", "");
				e.lastOpened    = item.value("lastOpened", "");
				e.engineVersion = item.value("engineVersion", "");
				result.push_back(std::move(e));
			}
		}
	}
	catch (...) {}

	return result;
}

void Save(const std::vector<RecentProjectEntry>& entries)
{
	json arr = json::array();
	for (auto& e : entries)
	{
		json item;
		item["name"]          = e.name;
		item["path"]          = e.path;
		item["lastOpened"]    = e.lastOpened;
		item["engineVersion"] = e.engineVersion;
		arr.push_back(item);
	}

	json j;
	j["recentProjects"] = arr;
	j["maxRecentCount"] = kMaxRecent;

	std::string path = GetRecentProjectsFilePath();

	fs::path p(path);
	if (p.has_parent_path())
		fs::create_directories(p.parent_path());

	std::ofstream ofs(path);
	if (ofs.is_open())
		ofs << j.dump(4);
}

void AddOrUpdate(const std::string& name,
	const std::string& projectPath,
	const std::string& engineVersion)
{
	auto entries = Load();

	// Remove existing entry for this path (if any)
	entries.erase(
		std::remove_if(entries.begin(), entries.end(),
			[&](const RecentProjectEntry& e) { return e.path == projectPath; }),
		entries.end());

	// Insert at front
	RecentProjectEntry newEntry;
	newEntry.name = name;
	newEntry.path = projectPath;
	newEntry.lastOpened = NowISO8601();
	newEntry.engineVersion = engineVersion;
	entries.insert(entries.begin(), newEntry);

	// Trim
	if ((int)entries.size() > kMaxRecent)
		entries.resize(kMaxRecent);

	Save(entries);
}

} // namespace RecentProjects
} // namespace Vans
