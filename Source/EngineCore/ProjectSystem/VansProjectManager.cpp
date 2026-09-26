#include "VansProjectManager.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AssetCore/Storage/VansFileStorage.h"
#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/VansBuiltInAssetCatalog.h"
#include "../AssetCore/VansDerivedArtifactLayout.h"
#include "../AudioCore/Serialization/VansAudioMixConfigJsonCodec.h"
#include "../AudioCore/VansAudioMixConfig.h"
#include "../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../PhysicsCore/Serialization/VansCollisionLayerJsonCodec.h"
#include "../PhysicsCore/Storage/VansCollisionLayerStorage.h"
#include "../PhysicsCore/VansCollisionLayerConfig.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../SceneCore/VansAssetObjectBootstrapper.h"
#include "../Util/VansLog.h"
#include "Storage/VansProjectDocumentStorage.h"
#include "Storage/VansProjectScaffoldStorage.h"
#include "Storage/VansProjectSettingsStorage.h"
#include "VansProjectDocumentLoader.h"
#include "VansEnginePaths.h"
#include "VansProjectSettingsData.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

namespace Vans
{
namespace
{
std::string NormalizeAssetLookupPath(std::filesystem::path path)
{
	std::string value = path.lexically_normal().generic_string();
	std::transform(value.begin(), value.end(), value.begin(),
				   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

} // namespace

// -----------------------------------------------------------------------
// Singleton
// -----------------------------------------------------------------------
VansProjectManager::VansProjectManager() = default;

VansProjectManager::~VansProjectManager() = default;

VansProjectManager& VansProjectManager::Get()
{
	static VansProjectManager instance;
	return instance;
}

bool VansProjectManager::ConfigureEngineRoot(const std::filesystem::path& engineRoot, std::string& error)
{
	std::string normalized;
	if (!VansEnginePaths::NormalizeEngineRoot(engineRoot, normalized, error))
		return false;
	if (m_Loaded && !m_PathResolver.GetEngineRoot().empty() &&
		m_PathResolver.GetEngineRoot() != normalized)
	{
		error = "Cannot change the engine root while a project is loaded.";
		return false;
	}
	m_PathResolver.SetEngineRoot(normalized);
	error.clear();
	return true;
}

// -----------------------------------------------------------------------
// Create Project
// -----------------------------------------------------------------------
bool VansProjectManager::CreateProject(const std::string& folderPath, const std::string& projectName)
{
	VansScopedIOContext ioContext(VansIODomain::Authoring, "Project.Create", true);
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
	m_HasCollisionLayerDocument = false;
	m_HasAudioMixDocument = false;
	std::string projectSettingsError;
	if (!VansProjectSettingsStorage::SaveRenderSettings(
			root + m_Config.renderSettings, m_ProjectSettings.BuildRenderSettingsData(), projectSettingsError) ||
		!VansProjectSettingsStorage::SavePhysicsSettings(
			root + m_Config.physicsSettings, m_ProjectSettings.BuildPhysicsSettingsData(), projectSettingsError) ||
		!VansProjectSettingsStorage::SaveNavigationSettings(
			root + m_Config.navigationSettings,
			m_ProjectSettings.GetNavigationSettings(), projectSettingsError))
	{
		VANS_LOG_ERROR("[ProjectManager] Failed to write project settings files: " << projectSettingsError);
		return false;
	}
	VansEngine::VansCollisionLayerConfig collisionLayers;
	collisionLayers.ResetToDefaults();
	VansEngine::VansAudioMixConfig audioMix;
	audioMix.displayName = projectName + " Default Audio Mix";
	if (!VansEngine::VansCollisionLayerStorage::SaveAtomic(root + m_Config.collisionLayerSettings, collisionLayers,
														   projectSettingsError) ||
		!VansEngine::VansAudioMixConfigStorage::SaveAtomic(root + m_Config.audioSettings, audioMix,
														   projectSettingsError))
	{
		VANS_LOG_ERROR("[ProjectManager] Failed to write project settings files: " << projectSettingsError);
		return false;
	}
	std::string gafConfigurationError;
	if (!VansGAFProjectConfiguration::EnsureProjectFiles(
			fs::path(root) / "ProjectSettings",
			fs::path(m_PathResolver.GetEngineRoot()) / "EngineAssets/GAF/ProjectSettings", gafConfigurationError))
	{
		VANS_LOG_ERROR("[ProjectManager] Failed to initialize GAF project settings: " << gafConfigurationError);
		return false;
	}

	// Create a default empty scene
	m_SceneManager.CreateEmptyScene("MainScene", root);

	// Now open it
	return OpenProject({root, {}}).m_Opened;
}

// -----------------------------------------------------------------------
// Open Project
// -----------------------------------------------------------------------
VansProjectOpenResult VansProjectManager::OpenProject(const VansProjectOpenRequest& request)
{
	VansScopedIOContext ioContext(VansIODomain::Authoring, "Project.Open", false);
	const auto fail = [](VansProjectOpenFailure failure, std::string message)
	{
		VansProjectOpenResult result;
		result.m_Failure = failure;
		result.m_Message = std::move(message);
		return result;
	};

	std::string root = request.m_ProjectRootPath;
	std::replace(root.begin(), root.end(), '\\', '/');
	if (!root.empty() && root.back() != '/')
		root += '/';

	VANS_LOG("[ProjectManager] Opening project at " << root);

	// Validate
	if (!ValidateProjectStructure(root))
	{
		VANS_LOG_ERROR("[ProjectManager] Invalid project structure at " << root);
		return fail(VansProjectOpenFailure::InvalidStructure, "Invalid project structure: " + root);
	}

	// Load config
	std::string configPath = root + "ForestProject.json";
	VansProjectConfig projectConfig;
	if (!projectConfig.LoadFromFile(configPath))
	{
		VANS_LOG_ERROR("[ProjectManager] Failed to load ForestProject.json");
		return fail(VansProjectOpenFailure::ConfigRead, "Failed to load ForestProject.json");
	}

	const VansProjectConfigDiagnostics diagnostics = VansProjectConfigValidator::Validate(projectConfig);
	for (const VansProjectConfigDiagnostic& diagnostic : diagnostics)
	{
		const char* severity =
			diagnostic.severity == VansProjectConfigDiagnosticSeverity::Error
				? "Error"
				: (diagnostic.severity == VansProjectConfigDiagnosticSeverity::Warning ? "Warning" : "Info");
		VANS_LOG("[ProjectConfig][" << severity << "] " << diagnostic.propertyPointer << " " << diagnostic.message);
	}
	if (VansProjectConfigValidator::HasErrors(diagnostics))
	{
		VANS_LOG_ERROR("[ProjectManager] ForestProject.json failed validation");
		return fail(VansProjectOpenFailure::ConfigValidation, "ForestProject.json failed validation");
	}
	VansProjectDocumentLoadResult documentLoad;
	if (request.m_Options.m_LoadProjectSettings)
	{
		documentLoad = VansProjectDocumentLoader::Load(root, projectConfig, m_PathResolver.GetEngineRoot());
		if (!documentLoad.m_NavigationSettingsLoaded)
		{
			return fail(VansProjectOpenFailure::ProjectSettingsRead,
				"Failed to load navigation settings: " +
				documentLoad.m_NavigationSettingsError);
		}
	}
	else
	{
		documentLoad.m_Documents.m_Settings.SetDefaults();
	}

	VansSceneManager sceneManager;
	sceneManager.SetDefaultScene(projectConfig.defaultScene);
	sceneManager.DiscoverScenes(root + "Scenes");
	std::unique_ptr<VansAssetDatabase> assetDatabase;
	std::unique_ptr<VansAssetDatabase> builtInAssetDatabase;
	VansAssetObjectRepository assetObjectRepository;

	if (request.m_Options.m_ScanAssets)
	{
		assetDatabase = std::make_unique<VansAssetDatabase>(fs::path(root) / projectConfig.assetsRoot,
			fs::path(root) / projectConfig.importedArtifactRoot);
		const VansAssetScanResult assetScan = assetDatabase->Scan(request.m_Options.m_AssetPolicy);
		for (const std::string& error : assetScan.errors)
			VANS_LOG_ERROR("[AssetDatabase] " << error);
		VANS_LOG("[AssetDatabase] Registered " << assetScan.registered << " assets, generated "
											   << assetScan.generatedMeta << " meta files, cooked "
											   << assetScan.cookedArtifacts << " artifacts");

		const fs::path builtInArtifactRoot = VansDerivedArtifactLayout::ProjectBuiltInArtifactRoot(
			assetDatabase->ArtifactRoot()).path;
		builtInAssetDatabase = std::make_unique<VansAssetDatabase>(
			fs::path(m_PathResolver.GetEngineRoot()) / "EngineAssets", builtInArtifactRoot);
		std::vector<std::string> builtInErrors;
		if (!VansBuiltInAssetCatalog::RegisterAssets(*builtInAssetDatabase, m_PathResolver.GetEngineRoot(),
													 VansAssetOperationPolicy::ReadOnly(), builtInErrors))
		{
			for (const std::string& error : builtInErrors)
				VANS_LOG_ERROR("[BuiltInAssetDatabase] " << error);
			return fail(VansProjectOpenFailure::BuiltInAssets, "Required built-in asset registration failed");
		}
		VANS_LOG("[BuiltInAssetDatabase] Registered " << builtInAssetDatabase->All().size()
													  << " required engine assets");

		std::vector<VansAssetRecord> memoryRecords = assetDatabase->All();
		const std::vector<VansAssetRecord> builtInRecords = builtInAssetDatabase->All();
		memoryRecords.insert(memoryRecords.end(), builtInRecords.begin(), builtInRecords.end());
		const VansAssetObjectBootstrapResult memoryBootstrap =
			VansAssetObjectBootstrapper::Publish(memoryRecords, assetObjectRepository, {},
				documentLoad.m_Documents.m_Settings.GetNavigationSettings());
		if (!memoryBootstrap)
		{
			for (const std::string& error : memoryBootstrap.errors)
				VANS_LOG_ERROR("[AssetObjectRepository] " << error);
			return fail(VansProjectOpenFailure::AssetObjects, "Project asset object publication failed");
		}
		VANS_LOG("[AssetObjectRepository] Published " << memoryBootstrap.published
													  << " project/built-in memory objects");
	}

	std::string assetObjectReplaceError;
	if (!m_AssetObjectRepository.ReplaceWith(assetObjectRepository, assetObjectReplaceError))
	{
		VANS_LOG_ERROR("[AssetObjectRepository] " << assetObjectReplaceError);
		return fail(VansProjectOpenFailure::AssetObjects, "Project asset object publication failed");
	}

	m_Config = std::move(projectConfig);
	m_ProjectRootPath = root;
	m_PathResolver.SetProjectRoot(root);
	m_SceneManager = std::move(sceneManager);
	m_AssetDatabase = std::move(assetDatabase);
	m_BuiltInAssetDatabase = std::move(builtInAssetDatabase);
	if (request.m_Options.m_ScanAssets)
	{
		m_PackagedAssetRecords.clear();
		m_PackagedAssetRecordsByGuid.clear();
		m_PackagedAssetRecordsByPath.clear();
	}
	m_Loaded = true;
	ApplyProjectDocuments(std::move(documentLoad.m_Documents));
	if (request.m_Options.m_LoadProjectSettings && !documentLoad.m_LoadedAny)
	{
		VANS_LOG_WARN("[ProjectManager] One or more project settings documents could not be loaded; in-memory defaults "
					  "remain active");
	}
	m_ProjectDocumentDirtyMask = 0;
	m_ProjectDocumentStateId = 0;
	m_ProjectDocumentSavedStateId = 0;
	m_ProjectDocumentFingerprints = VansProjectDocumentStorage::CaptureFingerprints(m_ProjectRootPath, m_Config);

	if (request.m_Options.m_UpdateLastOpenedAt)
		VANS_LOG("[ProjectManager] lastOpenedAt is stored in RecentProjects, not ForestProject.json");

	if (request.m_Options.m_UpdateRecentProjects)
	{
		// Update recent list
		RecentProjects::AddOrUpdate(m_Config.projectName, root, m_Config.engineVersion);
	}

	VANS_LOG("[ProjectManager] Project '" << m_Config.projectName << "' loaded successfully");
	VansProjectOpenResult result;
	result.m_Opened = true;
	return result;
}

// -----------------------------------------------------------------------
// Close
// -----------------------------------------------------------------------
void VansProjectManager::CloseProject()
{
	if (m_Loaded)
		VANS_LOG("[ProjectManager] Closing project '" << m_Config.projectName << "'");

	m_SceneManager.Clear();
	m_AssetDatabase.reset();
	m_BuiltInAssetDatabase.reset();
	m_AssetObjectRepository.Clear();
	m_Config = {};
	m_ProjectSettings.SetDefaults();
	m_CollisionLayerDocument = {};
	m_AudioMixDocument = {};
	m_GAFProjectConfiguration = {};
	m_HasCollisionLayerDocument = false;
	m_HasAudioMixDocument = false;
	m_HasGAFProjectConfiguration = false;
	m_ProjectRootPath.clear();
	m_PathResolver.SetProjectRoot({});
	VansEngine::VansCollisionLayerManager::Get().ResetToDefaults();
	m_Loaded = false;
	m_PackagedAssetRecords.clear();
	m_PackagedAssetRecordsByGuid.clear();
	m_PackagedAssetRecordsByPath.clear();
	m_ProjectDocumentFingerprints.clear();
	m_ProjectDocumentDirtyMask = 0;
	m_ProjectDocumentStateId = 0;
	m_ProjectDocumentSavedStateId = 0;
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

void VansProjectManager::MarkProjectDocumentsDirty(std::uint8_t documentMask)
{
	m_ProjectDocumentDirtyMask |= documentMask;
	++m_ProjectDocumentStateId;
}

bool VansProjectManager::SetProjectPhysicsTiming(
	const VansEngine::VansPhysicsTiming& timing,
	std::string& error)

{
	if (!m_Loaded)
	{
		error = "No project is loaded";
		return false;
	}
	const VansEngine::VansPhysicsTiming& current = m_ProjectSettings.GetPhysicsTiming();
	if (current.fixedTimeStep == timing.fixedTimeStep &&
		current.maximumSubsteps == timing.maximumSubsteps &&
		current.clothFrameTime == timing.clothFrameTime &&
		current.clothSubsteps == timing.clothSubsteps)
	{
		error.clear();
		return true;
	}
	if (!m_ProjectSettings.SetPhysicsTiming(timing, error))
		return false;
	MarkProjectDocumentsDirty(ProjectDocumentMask(VansProjectDocumentDomain::PhysicsSettings));
	error.clear();
	return true;
}

bool VansProjectManager::ValidateProjectRenderSettings(const VansProjectUpscalerSettings& upscalerSettings,
													   const VansProjectRenderOutputSettings& outputSettings,
													   std::string& error) const
{
	VansProjectSettings candidate = m_ProjectSettings;
	if (!candidate.SetUpscalerSettings(upscalerSettings, &error))
		return false;
	return candidate.SetRenderOutputSettings(outputSettings, &error);
}

bool VansProjectManager::SetProjectRenderSettings(const VansProjectUpscalerSettings& upscalerSettings,
												  const VansProjectRenderOutputSettings& outputSettings,
												  std::string& error)
{
	if (!m_Loaded)
	{
		error = "No project is loaded";
		return false;
	}
	VansProjectSettings candidate = m_ProjectSettings;
	if (!candidate.SetUpscalerSettings(upscalerSettings, &error) ||
		!candidate.SetRenderOutputSettings(outputSettings, &error))
		return false;
	m_ProjectSettings = std::move(candidate);
	MarkProjectDocumentsDirty(ProjectDocumentMask(VansProjectDocumentDomain::RenderSettings));
	error.clear();
	return true;
}

void VansProjectManager::SetProjectCommandRecordingSettings(bool parallelEnabled, bool frameContextRingEnabled,
															std::uint32_t framesInFlight, bool asyncComputeEnabled)
{
	if (!m_Loaded)
		return;
	m_ProjectSettings.SetCommandRecordingSettings(parallelEnabled, frameContextRingEnabled, framesInFlight,
												  asyncComputeEnabled);
	MarkProjectDocumentsDirty(ProjectDocumentMask(VansProjectDocumentDomain::RenderSettings));
}

bool VansProjectManager::SaveProjectDocuments(std::string& error)
{
	VansScopedIOContext ioContext(VansIODomain::Authoring, "Project.SaveDocuments", true);
	if (!m_Loaded)
	{
		error = "Cannot save project documents without a loaded project";
		return false;
	}
	if (!HasDirtyProjectDocuments())
	{
		error.clear();
		return true;
	}

	VansProjectDocumentSaveRequest request;
	request.m_ProjectRootPath = m_ProjectRootPath;
	request.m_Config = m_Config;
	request.m_Documents.m_Settings = m_ProjectSettings;
	request.m_Documents.m_CollisionLayerDocument = m_CollisionLayerDocument;
	request.m_Documents.m_AudioMixDocument = m_AudioMixDocument;
	request.m_Documents.m_GAFConfiguration = m_GAFProjectConfiguration;
	request.m_Documents.m_HasCollisionLayerDocument = m_HasCollisionLayerDocument;
	request.m_Documents.m_HasAudioMixDocument = m_HasAudioMixDocument;
	request.m_Documents.m_HasGAFConfiguration = m_HasGAFProjectConfiguration;
	request.m_DirtyMask = m_ProjectDocumentDirtyMask;
	request.m_ExpectedFingerprints = m_ProjectDocumentFingerprints;

	VansProjectDocumentSaveResult result;
	if (!VansProjectDocumentStorage::Save(request, result, error))
		return false;

	m_ProjectDocumentFingerprints = std::move(result.m_Fingerprints);
	m_ProjectDocumentDirtyMask = 0;
	m_ProjectDocumentSavedStateId = m_ProjectDocumentStateId;
	error.clear();
	return true;
}

VansProjectConfigDiagnostics VansProjectManager::GetProjectConfigDiagnostics() const
{
	return VansProjectConfigValidator::Validate(m_Config);
}

bool VansProjectManager::SetProjectDefaultScene(const std::string& sceneRelativePath, std::string& error)
{
	return SetProjectPathField(VansProjectConfigPathField::DefaultScene, sceneRelativePath, error);
}

bool VansProjectManager::SetProjectPathField(VansProjectConfigPathField field, const std::string& relativePath,
											 std::string& error)
{
	if (!m_Loaded)
	{
		error = "No project is loaded";
		return false;
	}

	const std::string normalized = VansProjectConfigValidator::NormalizeProjectRelativePath(relativePath);
	if (!VansProjectConfigValidator::IsSafeProjectRelativePath(normalized))
	{
		error = "Invalid project-relative path: " + relativePath;
		return false;
	}
	std::uint8_t dirtyMask = ProjectDocumentMask(VansProjectDocumentDomain::ProjectConfig);

	switch (field)
	{
	case VansProjectConfigPathField::DefaultScene:
		if (m_Config.defaultScene == normalized)
			return true;
		m_Config.defaultScene = normalized;
		m_SceneManager.SetDefaultScene(m_Config.defaultScene);
		break;
	case VansProjectConfigPathField::AssetsRoot:
		if (m_Config.assetsRoot == normalized)
			return true;
		m_Config.assetsRoot = normalized;
		break;
	case VansProjectConfigPathField::ImportedArtifactRoot:
		if (m_Config.importedArtifactRoot == normalized)
			return true;
		m_Config.importedArtifactRoot = normalized;
		break;
	case VansProjectConfigPathField::RenderSettings:
		if (m_Config.renderSettings == normalized)
			return true;
		m_Config.renderSettings = normalized;
		dirtyMask |= ProjectDocumentMask(VansProjectDocumentDomain::RenderSettings);
		break;
	case VansProjectConfigPathField::PhysicsSettings:
		if (m_Config.physicsSettings == normalized)
			return true;
		m_Config.physicsSettings = normalized;
		dirtyMask |= ProjectDocumentMask(VansProjectDocumentDomain::PhysicsSettings);
		break;
	case VansProjectConfigPathField::CollisionLayerSettings:
		if (m_Config.collisionLayerSettings == normalized)
			return true;
		m_Config.collisionLayerSettings = normalized;
		break;
	default:
		error = "Unsupported project config path field";
		return false;
	}
	MarkProjectDocumentsDirty(dirtyMask);
	error.clear();
	return true;
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

	if (m_Config.scriptSearchPaths == paths)
		return true;
	m_Config.scriptSearchPaths = std::move(paths);
	MarkProjectDocumentsDirty(ProjectDocumentMask(VansProjectDocumentDomain::ProjectConfig));
	return true;
}

bool VansProjectManager::SetProjectAssetDirectory(const std::string& key, const std::string& relativePath,
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

	const std::string normalized = VansProjectConfigValidator::NormalizeProjectRelativePath(relativePath);
	if (!VansProjectConfigValidator::IsSafeProjectRelativePath(normalized))
	{
		error = "Invalid asset directory path: " + relativePath;
		return false;
	}

	const auto existing = m_Config.assetDirectories.find(key);
	if (existing != m_Config.assetDirectories.end() && existing->second == normalized)
		return true;
	m_Config.assetDirectories[key] = normalized;
	MarkProjectDocumentsDirty(ProjectDocumentMask(VansProjectDocumentDomain::ProjectConfig));
	return true;
}

bool VansProjectManager::SetCollisionLayerDocument(const VansSerializedValue& document, std::string& error)
{
	if (!m_Loaded)
	{
		error = "No project is loaded";
		return false;
	}
	const nlohmann::json root = EncodeSerializedValueJson<nlohmann::json>(document);
	VansEngine::VansCollisionLayerConfig config;
	if (!VansEngine::VansCollisionLayerJsonCodec::Decode(root, config, error))
		return false;
	const nlohmann::json normalized = VansEngine::VansCollisionLayerJsonCodec::Encode(config);
	if (m_HasCollisionLayerDocument &&
		EncodeSerializedValueJson<nlohmann::json>(m_CollisionLayerDocument) == normalized)
	{
		error.clear();
		return true;
	}
	m_CollisionLayerDocument = DecodeSerializedValueJson(normalized);
	m_HasCollisionLayerDocument = true;
	VansEngine::VansCollisionLayerManager::Get().ApplyConfig(config);
	MarkProjectDocumentsDirty(ProjectDocumentMask(VansProjectDocumentDomain::CollisionLayers));
	error.clear();
	return true;
}

bool VansProjectManager::SetAudioMixDocument(const VansSerializedValue& document, std::string& error)
{
	if (!m_Loaded)
	{
		error = "No project is loaded";
		return false;
	}
	const nlohmann::json root = EncodeSerializedValueJson<nlohmann::json>(document);
	VansEngine::VansAudioMixConfig config;
	if (!VansEngine::VansAudioMixConfigJsonCodec::Decode(root, config, error))
		return false;
	const nlohmann::json normalized = VansEngine::VansAudioMixConfigJsonCodec::Encode(config);
	if (m_HasAudioMixDocument && EncodeSerializedValueJson<nlohmann::json>(m_AudioMixDocument) == normalized)
	{
		error.clear();
		return true;
	}
	m_AudioMixDocument = DecodeSerializedValueJson(normalized);
	m_HasAudioMixDocument = true;
	MarkProjectDocumentsDirty(ProjectDocumentMask(VansProjectDocumentDomain::AudioMix));
	error.clear();
	return true;
}

bool VansProjectManager::GetAudioMixConfig(
	VansEngine::VansAudioMixConfig& config,
	std::string& error) const
{
	if (!m_HasAudioMixDocument)
	{
		config = {};
		error.clear();
		return true;
	}
	return VansEngine::VansAudioMixConfigJsonCodec::Decode(
		EncodeSerializedValueJson<nlohmann::json>(m_AudioMixDocument), config, error);
}

bool VansProjectManager::SetGAFProjectConfiguration(const VansGAFProjectConfiguration& configuration,
													std::string& error)
{
	if (!m_Loaded)
	{
		error = "No project is loaded";
		return false;
	}
	if (!configuration.Validate(error))
		return false;
	m_GAFProjectConfiguration = configuration;
	m_HasGAFProjectConfiguration = true;
	MarkProjectDocumentsDirty(ProjectDocumentMask(VansProjectDocumentDomain::GAFConfiguration));
	error.clear();
	return true;
}

void VansProjectManager::ApplyProjectDocuments(VansProjectDocumentSnapshot documents)
{
	m_ProjectSettings = std::move(documents.m_Settings);
	m_CollisionLayerDocument = std::move(documents.m_CollisionLayerDocument);
	m_AudioMixDocument = std::move(documents.m_AudioMixDocument);
	m_GAFProjectConfiguration = std::move(documents.m_GAFConfiguration);
	m_HasCollisionLayerDocument = documents.m_HasCollisionLayerDocument;
	m_HasAudioMixDocument = documents.m_HasAudioMixDocument;
	m_HasGAFProjectConfiguration = documents.m_HasGAFConfiguration;

	if (documents.m_CollisionLayers)
		VansEngine::VansCollisionLayerManager::Get().ApplyConfig(*documents.m_CollisionLayers);
	else
		VansEngine::VansCollisionLayerManager::Get().ResetToDefaults();
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
		"Assets",		   "Assets/Models", "Assets/Textures", "Assets/Materials", "Assets/Audio", "Scripts", "Scenes",
		"ProjectSettings", "Logs",
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
	if (!VansProjectScaffoldStorage::EnsureDefaultLuaScriptFile(defaultScriptPath, createdDefaultScript, scriptError))
	{
		VANS_LOG_WARN("[ProjectManager] Cannot create default.lua at: " << defaultScriptPath.string() << " ("
																		<< scriptError << ")");
	}
	else if (createdDefaultScript)
	{
		VANS_LOG("[ProjectManager] Created default Lua script");
	}
}

} // namespace Vans
