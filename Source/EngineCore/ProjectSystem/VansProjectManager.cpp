#include "VansProjectManager.h"
#include "../Util/VansLog.h"
#include "../Configration/VansConfigration.h"

#include <filesystem>
#include <algorithm>

namespace fs = std::filesystem;

namespace Vans {

// -----------------------------------------------------------------------
// Singleton
// -----------------------------------------------------------------------
VansProjectManager::VansProjectManager()
{
	// Initialise engine root from VansConfigration (the only coupling
	// point with the legacy engine configuration).
	auto* cfg = VansConfigration::GetInstance();
	if (cfg)
		m_PathResolver.SetEngineRoot(cfg->GetProjectRootPath());
}

VansProjectManager& VansProjectManager::Get()
{
	static VansProjectManager instance;
	return instance;
}

// -----------------------------------------------------------------------
// Create Project
// -----------------------------------------------------------------------
bool VansProjectManager::CreateProject(const std::string& folderPath,
	const std::string& projectName)
{
	std::string root = folderPath;
	std::replace(root.begin(), root.end(), '\\', '/');
	if (!root.empty() && root.back() != '/')
		root += '/';

	VANS_LOG("[ProjectManager] Creating project '" << projectName << "' at " << root);

	// Create the directory structure
	CreateDefaultDirectories(root);

	// Populate and save config
	m_Config.SetDefaults(projectName);
	std::string configPath = root + "ForestProject.json";
	if (!m_Config.SaveToFile(configPath))
	{
		VANS_LOG_ERROR("[ProjectManager] Failed to write ForestProject.json");
		return false;
	}

	// Create a default empty scene
	m_SceneManager.CreateEmptyScene("MainScene", root);

	// Now open it
	return OpenProject(root);
}

// -----------------------------------------------------------------------
// Open Project
// -----------------------------------------------------------------------
bool VansProjectManager::OpenProject(const std::string& projectRootPath)
{
	std::string root = projectRootPath;
	std::replace(root.begin(), root.end(), '\\', '/');
	if (!root.empty() && root.back() != '/')
		root += '/';

	VANS_LOG("[ProjectManager] Opening project at " << root);

	// Validate
	if (!ValidateProjectStructure(root))
	{
		VANS_LOG_ERROR("[ProjectManager] Invalid project structure at " << root);
		return false;
	}

	// Load config
	std::string configPath = root + "ForestProject.json";
	if (!m_Config.LoadFromFile(configPath))
	{
		VANS_LOG_ERROR("[ProjectManager] Failed to load ForestProject.json");
		return false;
	}

	m_ProjectRootPath = root;
	m_PathResolver.SetProjectRoot(root);
	m_Loaded = true;

	// Update last-opened timestamp and persist
	m_Config.lastOpenedAt = []() {
		auto now = std::chrono::system_clock::now();
		std::time_t t = std::chrono::system_clock::to_time_t(now);
		std::tm tm{};
#ifdef _WIN32
		localtime_s(&tm, &t);
#else
		localtime_r(&t, &tm);
#endif
		char buf[64];
		std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &tm);
		return std::string(buf);
	}();
	m_Config.SaveToFile(configPath);

	// Discover scenes
	m_SceneManager.Clear();
	m_SceneManager.SetDefaultScene(m_Config.defaultScene);
	m_SceneManager.DiscoverScenes(root + "Scenes");

	// Update recent list
	RecentProjects::AddOrUpdate(m_Config.projectName, root, m_Config.engineVersion);

	VANS_LOG("[ProjectManager] Project '" << m_Config.projectName << "' loaded successfully");
	return true;
}

// -----------------------------------------------------------------------
// Close
// -----------------------------------------------------------------------
void VansProjectManager::CloseProject()
{
	if (!m_Loaded) return;

	VANS_LOG("[ProjectManager] Closing project '" << m_Config.projectName << "'");

	m_SceneManager.Clear();
	m_Config = {};
	m_ProjectRootPath.clear();
	m_Loaded = false;
}

// -----------------------------------------------------------------------
// Path delegation
// -----------------------------------------------------------------------
std::string VansProjectManager::ResolveAssetPath(const std::string& relativePath) const
{
	return m_PathResolver.Resolve(relativePath);
}

std::string VansProjectManager::MakeRelativePath(const std::string& absolutePath) const
{
	return m_PathResolver.MakeRelative(absolutePath);
}

bool VansProjectManager::ValidateAssetPath(const std::string& relativePath) const
{
	return m_PathResolver.Validate(relativePath);
}

// -----------------------------------------------------------------------
// Recent projects delegation
// -----------------------------------------------------------------------
std::vector<RecentProjectEntry> VansProjectManager::GetRecentProjects() const
{
	return RecentProjects::Load();
}

void VansProjectManager::AddToRecentProjects(const std::string& path)
{
	RecentProjects::AddOrUpdate(m_Config.projectName, path, m_Config.engineVersion);
}

// -----------------------------------------------------------------------
// Validation / directory creation
// -----------------------------------------------------------------------
bool VansProjectManager::ValidateProjectStructure(const std::string& rootPath) const
{
	std::string configFile = rootPath + "ForestProject.json";
	if (!fs::exists(configFile))
	{
		VANS_LOG_ERROR("[ProjectManager] ForestProject.json not found in " << rootPath);
		return false;
	}
	return true;
}

void VansProjectManager::CreateDefaultDirectories(const std::string& rootPath)
{
	const char* dirs[] = {
		"Assets",
		"Assets/Models",
		"Assets/Textures",
		"Assets/Materials",
		"Assets/Audio",
		"Scripts",
		"Scenes",
		"ProjectSettings",
		"Logs",
	};

	for (auto d : dirs)
	{
		fs::path p = fs::path(rootPath) / d;
		if (!fs::exists(p))
		{
			fs::create_directories(p);
			VANS_LOG("[ProjectManager] Created directory: " << p.string());
		}
	}
}

} // namespace Vans
