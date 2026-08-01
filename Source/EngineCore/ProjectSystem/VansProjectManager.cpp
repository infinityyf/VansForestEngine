#include "VansProjectManager.h"
#include "../Util/VansLog.h"
#include "../Configration/VansConfigration.h"
#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/VansBuiltInAssetCatalog.h"
#include "Storage/VansProjectScaffoldStorage.h"

#include <filesystem>
#include <algorithm>
#include <cctype>

namespace fs = std::filesystem;

namespace Vans {
namespace
{
	std::string NormalizeAssetLookupPath(std::filesystem::path path)
	{
		std::string value = path.lexically_normal().generic_string();
		std::transform(value.begin(), value.end(), value.begin(),
			[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
		return value;
	}
}

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

VansProjectManager::~VansProjectManager() = default;

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

	m_ProjectSettings.SetDefaults();
	if (!m_ProjectSettings.SaveToProjectFiles(root, m_Config))
	{
		VANS_LOG_ERROR("[ProjectManager] Failed to write project settings files");
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
	return OpenProject(projectRootPath, VansProjectOpenOptions{});
}

bool VansProjectManager::OpenProject(const std::string& projectRootPath, const VansProjectOpenOptions& options)
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

	const VansProjectConfigDiagnostics diagnostics =
		VansProjectConfigValidator::Validate(m_Config);
	for (const VansProjectConfigDiagnostic& diagnostic : diagnostics)
	{
		const char* severity = diagnostic.severity == VansProjectConfigDiagnosticSeverity::Error
			? "Error"
			: (diagnostic.severity == VansProjectConfigDiagnosticSeverity::Warning ? "Warning" : "Info");
		VANS_LOG("[ProjectConfig][" << severity << "] "
			<< diagnostic.propertyPointer << " " << diagnostic.message);
	}
	if (VansProjectConfigValidator::HasErrors(diagnostics))
	{
		VANS_LOG_ERROR("[ProjectManager] ForestProject.json failed validation");
		return false;
	}

	m_ProjectRootPath = root;
	m_PathResolver.SetProjectRoot(root);
	m_Loaded = true;
	m_ProjectSettings.SetDefaults();

	if (options.loadProjectSettings && !LoadProjectSettings())
	{
		VANS_LOG_WARN("[ProjectManager] Falling back to default project settings");
	}

	if (options.updateLastOpenedAt)
		VANS_LOG("[ProjectManager] lastOpenedAt is stored in RecentProjects, not ForestProject.json");

	// Discover scenes
	m_SceneManager.Clear();
	m_SceneManager.SetDefaultScene(m_Config.defaultScene);
	m_SceneManager.DiscoverScenes(root + "Scenes");

	if (options.scanAssets)
	{
		m_PackagedAssetRecords.clear();
		m_PackagedAssetRecordsByGuid.clear();
		m_PackagedAssetRecordsByPath.clear();
		m_AssetDatabase = std::make_unique<VansAssetDatabase>(
			fs::path(root) / m_Config.assetsRoot,
			fs::path(root) / m_Config.importedArtifactRoot);
		const VansAssetScanResult assetScan = m_AssetDatabase->Scan(options.assetPolicy);
		for (const std::string& error : assetScan.errors)
			VANS_LOG_ERROR("[AssetDatabase] " << error);
		VANS_LOG("[AssetDatabase] Registered " << assetScan.registered
			<< " assets, generated " << assetScan.generatedMeta
			<< " meta files, cooked " << assetScan.cookedArtifacts << " artifacts");

		const fs::path builtInArtifactRoot =
			fs::path(root) / m_Config.importedArtifactRoot / "Engine";
		m_BuiltInAssetDatabase = std::make_unique<VansAssetDatabase>(
			fs::path(m_PathResolver.GetEngineRoot()) / "EngineAssets",
			builtInArtifactRoot);
		std::vector<std::string> builtInErrors;
		if (!VansBuiltInAssetCatalog::RegisterAssets(
			*m_BuiltInAssetDatabase,
			m_PathResolver.GetEngineRoot(),
			VansAssetOperationPolicy::ReadOnly(),
			builtInErrors))
		{
			for (const std::string& error : builtInErrors)
				VANS_LOG_ERROR("[BuiltInAssetDatabase] " << error);
			m_AssetDatabase.reset();
			m_BuiltInAssetDatabase.reset();
			m_Loaded = false;
			return false;
		}
		VANS_LOG("[BuiltInAssetDatabase] Registered "
			<< m_BuiltInAssetDatabase->All().size() << " required engine assets");
	}
	else
	{
		m_AssetDatabase.reset();
		m_BuiltInAssetDatabase.reset();
	}

	if (options.updateRecentProjects)
	{
		// Update recent list
		RecentProjects::AddOrUpdate(m_Config.projectName, root, m_Config.engineVersion);
	}

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
	m_AssetDatabase.reset();
	m_BuiltInAssetDatabase.reset();
	m_Config = {};
	m_ProjectSettings.SetDefaults();
	m_ProjectRootPath.clear();
	m_Loaded = false;
	m_PackagedAssetRecords.clear();
	m_PackagedAssetRecordsByGuid.clear();
	m_PackagedAssetRecordsByPath.clear();
}

void VansProjectManager::SetPackagedAssetRecords(std::vector<VansAssetRecord> records)
{
	m_PackagedAssetRecords = std::move(records);
	m_PackagedAssetRecordsByGuid.clear();
	m_PackagedAssetRecordsByPath.clear();
	for (std::size_t i = 0; i < m_PackagedAssetRecords.size(); ++i)
	{
		const VansAssetRecord& record = m_PackagedAssetRecords[i];
		m_PackagedAssetRecordsByGuid[record.guid.ToString()] = i;
		if (!record.sourcePath.empty())
			m_PackagedAssetRecordsByPath[NormalizeAssetLookupPath(record.sourcePath)] = i;
		if (!record.artifactPath.empty())
			m_PackagedAssetRecordsByPath[NormalizeAssetLookupPath(record.artifactPath)] = i;
	}
	VANS_LOG("[ProjectManager] Packaged asset index loaded: " << m_PackagedAssetRecords.size() << " records");
}

std::optional<VansAssetRecord> VansProjectManager::FindAssetRecord(VansAssetGuid guid) const
{
	if (m_AssetDatabase)
	{
		if (const auto projectRecord = m_AssetDatabase->Find(guid))
			return projectRecord;
		if (m_BuiltInAssetDatabase)
			return m_BuiltInAssetDatabase->Find(guid);
		return std::nullopt;
	}

	auto it = m_PackagedAssetRecordsByGuid.find(guid.ToString());
	if (it == m_PackagedAssetRecordsByGuid.end() || it->second >= m_PackagedAssetRecords.size())
		return std::nullopt;
	return m_PackagedAssetRecords[it->second];
}

std::optional<VansAssetRecord> VansProjectManager::FindAssetRecordByPath(const std::filesystem::path& path) const
{
	if (m_AssetDatabase)
	{
		if (const auto projectRecord = m_AssetDatabase->Find(path))
			return projectRecord;
		if (m_BuiltInAssetDatabase)
		{
			if (const auto builtInRecord = m_BuiltInAssetDatabase->Find(path))
				return builtInRecord;
		}
		const std::string wanted = NormalizeAssetLookupPath(path);
		for (const VansAssetRecord& record : EnumerateAssetRecords())
		{
			if (!record.artifactPath.empty() && NormalizeAssetLookupPath(record.artifactPath) == wanted)
				return record;
		}
		return std::nullopt;
	}

	auto it = m_PackagedAssetRecordsByPath.find(NormalizeAssetLookupPath(path));
	if (it == m_PackagedAssetRecordsByPath.end() || it->second >= m_PackagedAssetRecords.size())
		return std::nullopt;
	return m_PackagedAssetRecords[it->second];
}

std::vector<VansAssetRecord> VansProjectManager::EnumerateAssetRecords() const
{
	if (m_AssetDatabase)
	{
		std::vector<VansAssetRecord> records = m_AssetDatabase->All();
		if (m_BuiltInAssetDatabase)
		{
			std::vector<VansAssetRecord> builtInRecords = m_BuiltInAssetDatabase->All();
			records.insert(records.end(), builtInRecords.begin(), builtInRecords.end());
		}
		return records;
	}
	return m_PackagedAssetRecords;
}

bool VansProjectManager::SaveProjectSettings() const
{
	if (m_ProjectRootPath.empty())
	{
		VANS_LOG_WARN("[ProjectManager] Cannot save project settings without a loaded project");
		return false;
	}

	return m_ProjectSettings.SaveToProjectFiles(m_ProjectRootPath, m_Config);
}

VansProjectConfigDiagnostics VansProjectManager::GetProjectConfigDiagnostics() const
{
	return VansProjectConfigValidator::Validate(m_Config);
}

bool VansProjectManager::SetProjectDefaultScene(const std::string& sceneRelativePath, std::string& error)
{
	return SetProjectPathField(VansProjectConfigPathField::DefaultScene, sceneRelativePath, error);
}

bool VansProjectManager::SetProjectPathField(
	VansProjectConfigPathField field,
	const std::string& relativePath,
	std::string& error)
{
	if (!m_Loaded)
	{
		error = "No project is loaded";
		return false;
	}

	const std::string normalized =
		VansProjectConfigValidator::NormalizeProjectRelativePath(relativePath);
	if (!VansProjectConfigValidator::IsSafeProjectRelativePath(normalized))
	{
		error = "Invalid project-relative path: " + relativePath;
		return false;
	}

	switch (field)
	{
	case VansProjectConfigPathField::DefaultScene:
		m_Config.defaultScene = normalized;
		m_SceneManager.SetDefaultScene(m_Config.defaultScene);
		return true;
	case VansProjectConfigPathField::AssetsRoot:
		m_Config.assetsRoot = normalized;
		return true;
	case VansProjectConfigPathField::ImportedArtifactRoot:
		m_Config.importedArtifactRoot = normalized;
		return true;
	case VansProjectConfigPathField::RenderSettings:
		m_Config.renderSettings = normalized;
		return true;
	case VansProjectConfigPathField::PhysicsSettings:
		m_Config.physicsSettings = normalized;
		return true;
	case VansProjectConfigPathField::CollisionLayerSettings:
		m_Config.collisionLayerSettings = normalized;
		return true;
	default:
		error = "Unsupported project config path field";
		return false;
	}
}

bool VansProjectManager::SetProjectScriptSearchPaths(std::vector<std::string> paths, std::string& error)
{
	if (!m_Loaded)
	{
		error = "No project is loaded";
		return false;
	}

	for (std::string& path : paths)
	{
		path = VansProjectConfigValidator::NormalizeProjectRelativePath(path);
		if (!VansProjectConfigValidator::IsSafeProjectRelativePath(path))
		{
			error = "Invalid script search path: " + path;
			return false;
		}
	}

	m_Config.scriptSearchPaths = std::move(paths);
	return true;
}

bool VansProjectManager::SetProjectAssetDirectory(
	const std::string& key,
	const std::string& relativePath,
	std::string& error)
{
	if (!m_Loaded)
	{
		error = "No project is loaded";
		return false;
	}

	if (key.empty())
	{
		error = "Asset directory key must not be empty";
		return false;
	}

	const std::string normalized =
		VansProjectConfigValidator::NormalizeProjectRelativePath(relativePath);
	if (!VansProjectConfigValidator::IsSafeProjectRelativePath(normalized))
	{
		error = "Invalid asset directory path: " + relativePath;
		return false;
	}

	m_Config.assetDirectories[key] = normalized;
	return true;
}

bool VansProjectManager::SaveProjectConfig(std::string& error) const
{
	if (m_ProjectRootPath.empty())
	{
		error = "Cannot save project config without a loaded project";
		return false;
	}

	VansProjectConfigDiagnostics diagnostics;
	if (!VansProjectConfigValidator::ValidateForSave(m_Config, diagnostics, error))
		return false;

	return m_Config.SaveToFile(m_ProjectRootPath + "ForestProject.json");
}

bool VansProjectManager::LoadProjectSettings()
{
	if (m_ProjectRootPath.empty())
	{
		return false;
	}

	if (m_ProjectSettings.LoadFromProjectFiles(m_ProjectRootPath, m_Config))
	{
		return true;
	}

	return SaveProjectSettings();
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

	fs::path defaultScriptPath = fs::path(rootPath) / "Scripts" / "default.lua";
	bool createdDefaultScript = false;
	std::string scriptError;
	if (!VansProjectScaffoldStorage::EnsureDefaultLuaScriptFile(
		defaultScriptPath,
		createdDefaultScript,
		scriptError))
	{
		VANS_LOG_WARN("[ProjectManager] Cannot create default.lua at: "
			<< defaultScriptPath.string() << " (" << scriptError << ")");
	}
	else if (createdDefaultScript)
	{
		VANS_LOG("[ProjectManager] Created default Lua script");
	}
}

} // namespace Vans
