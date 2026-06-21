#pragma once
// -----------------------------------------------------------------------
// VansProjectConfig  –  In-memory representation of ForestProject.json
//
// This class is self-contained: it depends only on the standard library
// and nlohmann/json.  No engine headers are required.
// -----------------------------------------------------------------------

#include <string>
#include <cstdint>
#include <vector>
#include <unordered_map>

namespace Vans {

/// Represents a single entry in the recent-projects list.
struct RecentProjectEntry
{
	std::string name;
	std::string path;
	std::string lastOpened;     // ISO 8601
	std::string engineVersion;
};

/// In-memory mirror of ForestProject.json.
struct VansProjectConfig
{
	std::string projectName;
	std::string engineVersion;
	std::string createdAt;      // ISO 8601
	std::string lastOpenedAt;   // ISO 8601
	std::string defaultScene;   // e.g. "Scenes/MainScene.json"
	std::uint32_t sceneSchemaVersion = 2;
	std::string assetsRoot = "Assets";
	std::string importedArtifactRoot = "Library/Artifacts";
	std::string metaExtension = ".meta";
	std::unordered_map<std::string, std::string> runtimeAssetBindings;

	// Logical directory names (relative to project root)
	std::unordered_map<std::string, std::string> assetDirectories;

	// Directories added to Python sys.path
	std::vector<std::string> scriptSearchPaths;

	std::string renderSettings;
	std::string physicsSettings;
	std::string collisionLayerSettings;

	// ── Serialisation ──────────────────────────────────────────────
	/// Load from a ForestProject.json file.  Returns true on success.
	bool LoadFromFile(const std::string& filePath);
	/// Save (create / overwrite) a ForestProject.json file.
	bool SaveToFile(const std::string& filePath) const;

	/// Populate with sensible defaults for a new project.
	void SetDefaults(const std::string& projectName);
};

/// Utility: load / save the global recent-projects list
/// stored in %APPDATA%/ForestEngine/RecentProjects.json.
namespace RecentProjects {
	std::vector<RecentProjectEntry> Load();
	void Save(const std::vector<RecentProjectEntry>& entries);
	void AddOrUpdate(const std::string& name,
		const std::string& path,
		const std::string& engineVersion);
	/// Returns the platform-specific path to the recent-projects file.
	std::string GetRecentProjectsFilePath();
}

} // namespace Vans
