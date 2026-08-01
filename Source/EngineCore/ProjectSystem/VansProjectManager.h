#pragma once
// -----------------------------------------------------------------------
// VansProjectManager  –  Top-level singleton that owns the Project state
//
// Coupling policy:
//   - Depends on VansProjectConfig, VansPathResolver, VansSceneManager
//     (all within ProjectSystem/).
//   - Depends on VansLog for diagnostics.
//   - Does NOT depend on any rendering, physics, or editor types.
//   - The engine entry point is the only integration seam.
// -----------------------------------------------------------------------

#include "VansProjectConfig.h"
#include "VansProjectConfigValidator.h"
#include "VansProjectSettings.h"
#include "VansPathResolver.h"
#include "VansSceneManager.h"
#include "../AssetCore/VansAssetDatabase.h"

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans {

class VansAssetDatabase;

struct VansProjectOpenOptions
{
	bool updateLastOpenedAt = false;
	bool updateRecentProjects = true;
	bool loadProjectSettings = true;
	bool scanAssets = true;
	VansAssetOperationPolicy assetPolicy = VansAssetOperationPolicy::Authoring();
};

class VansProjectManager
{
public:
	static VansProjectManager& Get();
	~VansProjectManager();

	// ── Project lifecycle ─────────────────────────────────────────

	/// Create a brand-new project at `folderPath` with the given name.
	/// Creates the directory structure + ForestProject.json.
	bool CreateProject(const std::string& folderPath, const std::string& projectName);

	/// Open an existing project.  `projectRootPath` must contain
	/// a ForestProject.json file.
	bool OpenProject(const std::string& projectRootPath);
	bool OpenProject(const std::string& projectRootPath, const VansProjectOpenOptions& options);

	/// Close the currently loaded project (clear all state).
	void CloseProject();

	/// Is a project currently loaded?
	bool IsProjectLoaded() const { return m_Loaded; }

	// ── Path helpers (delegate to VansPathResolver) ───────────────

	std::string ResolveAssetPath(const std::string& relativePath) const;
	std::string MakeRelativePath(const std::string& absolutePath) const;
	bool        ValidateAssetPath(const std::string& relativePath) const;

	// ── Accessors ─────────────────────────────────────────────────

	const std::string&       GetProjectRootPath() const { return m_ProjectRootPath; }
	const std::string&       GetProjectName()     const { return m_Config.projectName; }
	const VansProjectConfig& GetConfig()           const { return m_Config; }
	VansProjectSettings&     GetProjectSettings()        { return m_ProjectSettings; }
	const VansProjectSettings& GetProjectSettings() const { return m_ProjectSettings; }
	VansSceneManager&        GetSceneManager()           { return m_SceneManager; }
	const VansPathResolver&  GetPathResolver()     const { return m_PathResolver; }
	VansAssetDatabase*       GetAssetDatabase()          { return m_AssetDatabase.get(); }
	const VansAssetDatabase* GetAssetDatabase() const    { return m_AssetDatabase.get(); }
	VansAssetDatabase*       GetBuiltInAssetDatabase()   { return m_BuiltInAssetDatabase.get(); }
	const VansAssetDatabase* GetBuiltInAssetDatabase() const { return m_BuiltInAssetDatabase.get(); }
	void SetPackagedAssetRecords(std::vector<VansAssetRecord> records);
	std::optional<VansAssetRecord> FindAssetRecord(VansAssetGuid guid) const;
	std::optional<VansAssetRecord> FindAssetRecordByPath(const std::filesystem::path& path) const;
	std::vector<VansAssetRecord> EnumerateAssetRecords() const;
	bool SaveProjectSettings() const;
	VansProjectConfigDiagnostics GetProjectConfigDiagnostics() const;
	bool SetProjectDefaultScene(const std::string& sceneRelativePath, std::string& error);
	bool SetProjectPathField(VansProjectConfigPathField field, const std::string& relativePath, std::string& error);
	bool SetProjectScriptSearchPaths(std::vector<std::string> paths, std::string& error);
	bool SetProjectAssetDirectory(const std::string& key, const std::string& relativePath, std::string& error);
	bool SaveProjectConfig(std::string& error) const;

	// ── Recent projects (delegates to RecentProjects namespace) ───

	std::vector<RecentProjectEntry> GetRecentProjects() const;
	void AddToRecentProjects(const std::string& path);

private:
	VansProjectManager();

	bool ValidateProjectStructure(const std::string& rootPath) const;
	void CreateDefaultDirectories(const std::string& rootPath);
	bool LoadProjectSettings();

	bool              m_Loaded = false;
	std::string       m_ProjectRootPath;  // absolute, trailing '/'
	VansProjectConfig m_Config;
	VansProjectSettings m_ProjectSettings;
	VansPathResolver  m_PathResolver;
	VansSceneManager  m_SceneManager;
	std::unique_ptr<VansAssetDatabase> m_AssetDatabase;
	std::unique_ptr<VansAssetDatabase> m_BuiltInAssetDatabase;
	std::vector<VansAssetRecord> m_PackagedAssetRecords;
	std::unordered_map<std::string, std::size_t> m_PackagedAssetRecordsByGuid;
	std::unordered_map<std::string, std::size_t> m_PackagedAssetRecordsByPath;
};

} // namespace Vans
