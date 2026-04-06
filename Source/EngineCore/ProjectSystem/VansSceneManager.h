#pragma once
// -----------------------------------------------------------------------
// VansSceneManager  –  Discover and manage scenes within a Project
//
// No coupling to VansScene or any rendering types.  Works purely with
// filesystem paths so the rendering layer can consume the result.
// -----------------------------------------------------------------------

#include <string>
#include <vector>

namespace Vans {

/// Lightweight descriptor of a scene file discovered on disk.
struct SceneInfo
{
	std::string name;           // Human-readable (filename stem)
	std::string relativePath;   // e.g. "Scenes/MainScene.json"
};

class VansSceneManager
{
public:
	/// Scan a directory (absolute) for .json scene files and
	/// populate the available-scenes list.
	void DiscoverScenes(const std::string& absoluteScenesDir);

	/// Set the default scene path (from ForestProject.json).
	void SetDefaultScene(const std::string& relativePath);

	// ── Queries ───────────────────────────────────────────────────
	const std::vector<SceneInfo>& GetAvailableScenes() const { return m_AvailableScenes; }
	const std::string& GetDefaultScenePath() const { return m_DefaultScenePath; }

	/// Get the current scene's project-relative path (empty if none).
	const std::string& GetCurrentScenePath() const { return m_CurrentScenePath; }

	/// Mark a scene as the currently loaded one (called externally
	/// after the rendering layer finishes loading).
	void SetCurrentScene(const std::string& relativePath);

	/// Find a SceneInfo by relative path.  Returns nullptr if not found.
	const SceneInfo* FindScene(const std::string& relativePath) const;

	// ── Mutations ─────────────────────────────────────────────────

	/// Create a minimal empty scene JSON file on disk.
	/// `projectRoot` is the absolute project root used to write the file.
	/// Returns the relative path to the new scene, or empty on failure.
	std::string CreateEmptyScene(const std::string& sceneName,
		const std::string& projectRoot);

	/// Clear all state (used when closing a project).
	void Clear();

private:
	std::vector<SceneInfo> m_AvailableScenes;
	std::string            m_DefaultScenePath;
	std::string            m_CurrentScenePath;
};

} // namespace Vans
