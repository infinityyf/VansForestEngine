#include "VansSceneManager.h"
#include "../Util/VansLog.h"

#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace fs = std::filesystem;
using json = nlohmann::json;

namespace Vans {

// -----------------------------------------------------------------------
// Discovery
// -----------------------------------------------------------------------
void VansSceneManager::DiscoverScenes(const std::string& absoluteScenesDir)
{
	m_AvailableScenes.clear();

	if (!fs::exists(absoluteScenesDir) || !fs::is_directory(absoluteScenesDir))
	{
		VANS_LOG_WARN("[SceneManager] Scenes directory not found: " << absoluteScenesDir);
		return;
	}

	for (auto& entry : fs::directory_iterator(absoluteScenesDir))
	{
		if (!entry.is_regular_file())
			continue;

		auto ext = entry.path().extension().string();
		// case-insensitive compare
		std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
		if (ext != ".json")
			continue;

		SceneInfo info;
		info.name = entry.path().stem().string();

		// Build project-relative path: "Scenes/<filename>"
		info.relativePath = "Scenes/" + entry.path().filename().string();

		m_AvailableScenes.push_back(std::move(info));
	}

	// Sort alphabetically
	std::sort(m_AvailableScenes.begin(), m_AvailableScenes.end(),
		[](const SceneInfo& a, const SceneInfo& b) { return a.name < b.name; });

	VANS_LOG("[SceneManager] Discovered " << m_AvailableScenes.size() << " scene(s)");
}

// -----------------------------------------------------------------------
// Accessors / mutators
// -----------------------------------------------------------------------
void VansSceneManager::SetDefaultScene(const std::string& relativePath)
{
	m_DefaultScenePath = relativePath;
}

void VansSceneManager::SetCurrentScene(const std::string& relativePath)
{
	m_CurrentScenePath = relativePath;
}

const SceneInfo* VansSceneManager::FindScene(const std::string& relativePath) const
{
	for (auto& s : m_AvailableScenes)
		if (s.relativePath == relativePath)
			return &s;
	return nullptr;
}

// -----------------------------------------------------------------------
// Create empty scene
// -----------------------------------------------------------------------
std::string VansSceneManager::CreateEmptyScene(const std::string& sceneName,
	const std::string& projectRoot)
{
	std::string relPath = "Scenes/" + sceneName + ".json";
	std::string absPath = projectRoot + "/" + relPath;

	// Normalise
	std::replace(absPath.begin(), absPath.end(), '\\', '/');

	if (fs::exists(absPath))
	{
		VANS_LOG_WARN("[SceneManager] Scene already exists: " << relPath);
		return {};
	}

	// Ensure directory
	fs::path p(absPath);
	fs::create_directories(p.parent_path());

	// Minimal scene skeleton
	json scene;
	scene["scene"] = json::array();
	json sceneEntry;
	sceneEntry["rendernode"] = json::array();
	sceneEntry["light"] = json::array();
	sceneEntry["objects"] = json::array();
	scene["scene"].push_back(sceneEntry);
	scene["mesh"] = json::array();
	scene["texture"] = json::array();
	scene["material"] = json::array();

	std::ofstream ofs(absPath);
	if (!ofs.is_open())
	{
		VANS_LOG_ERROR("[SceneManager] Cannot create scene file: " << absPath);
		return {};
	}

	ofs << scene.dump(4);
	VANS_LOG("[SceneManager] Created empty scene: " << relPath);

	// Add to available list
	SceneInfo info;
	info.name = sceneName;
	info.relativePath = relPath;
	m_AvailableScenes.push_back(info);

	return relPath;
}

// -----------------------------------------------------------------------
// Clear
// -----------------------------------------------------------------------
void VansSceneManager::Clear()
{
	m_AvailableScenes.clear();
	m_DefaultScenePath.clear();
	m_CurrentScenePath.clear();
}

} // namespace Vans
