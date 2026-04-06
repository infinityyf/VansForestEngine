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
#include "VansPathResolver.h"
#include "VansSceneManager.h"

#include <memory>
#include <string>
#include <vector>

namespace Vans {

class VansProjectManager
{
public:
	static VansProjectManager& Get();

	// ── Project lifecycle ─────────────────────────────────────────

	/// Create a brand-new project at `folderPath` with the given name.
	/// Creates the directory structure + ForestProject.json.
	bool CreateProject(const std::string& folderPath, const std::string& projectName);

	/// Open an existing project.  `projectRootPath` must contain
	/// a ForestProject.json file.
	bool OpenProject(const std::string& projectRootPath);

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
	VansSceneManager&        GetSceneManager()           { return m_SceneManager; }
	const VansPathResolver&  GetPathResolver()     const { return m_PathResolver; }

	// ── Recent projects (delegates to RecentProjects namespace) ───

	std::vector<RecentProjectEntry> GetRecentProjects() const;
	void AddToRecentProjects(const std::string& path);

private:
	VansProjectManager();

	bool ValidateProjectStructure(const std::string& rootPath) const;
	void CreateDefaultDirectories(const std::string& rootPath);

	bool              m_Loaded = false;
	std::string       m_ProjectRootPath;  // absolute, trailing '/'
	VansProjectConfig m_Config;
	VansPathResolver  m_PathResolver;
	VansSceneManager  m_SceneManager;
};

} // namespace Vans
