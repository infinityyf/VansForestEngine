#include "VansProjectManager.h"
#include "../Util/VansLog.h"
#include "../Configration/VansConfigration.h"
#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/VansBuiltInAssetCatalog.h"
#include "../SceneCore/VansAssetObjectBootstrapper.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AudioCore/Serialization/VansAudioMixConfigJsonCodec.h"
#include "../AudioCore/VansAudioMixConfig.h"
#include "../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "../PhysicsCore/Serialization/VansCollisionLayerJsonCodec.h"
#include "../PhysicsCore/Storage/VansCollisionLayerStorage.h"
#include "../PhysicsCore/VansCollisionLayerConfig.h"
#include "../PhysicsCore/VansCollisionLayerManager.h"
#include "../AssetCore/Storage/VansFileStorage.h"
#include "../AssetCore/Storage/VansJsonFileStorage.h"
#include "../AssetCore/Storage/VansStagedFileTransaction.h"
#include "Serialization/VansProjectSettingsJsonCodec.h"
#include "Serialization/VansProjectConfigJsonCodec.h"
#include "Storage/VansProjectScaffoldStorage.h"
#include "Storage/VansProjectSettingsStorage.h"

#include <filesystem>
#include <algorithm>
#include <cctype>
#include <nlohmann/json.hpp>

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

	std::string ProjectDocumentKey(const std::filesystem::path& path)
	{
		std::error_code ec;
		std::filesystem::path normalized = std::filesystem::absolute(path, ec);
		if (ec)
			normalized = path;
		return NormalizeAssetLookupPath(normalized.lexically_normal());
	}

	std::uint64_t HashProjectDocumentBytes(const std::string& bytes)
	{
		constexpr std::uint64_t offset = 14695981039346656037ull;
		constexpr std::uint64_t prime = 1099511628211ull;
		std::uint64_t hash = offset;
		for (const unsigned char value : bytes)
		{
			hash ^= value;
			hash *= prime;
		}
		return hash;
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
	VansScopedIOContext ioContext(
		VansIODomain::Authoring, "Project.Create", true);
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
		root + m_Config.renderSettings,
		m_ProjectSettings.BuildRenderSettingsData(),
		projectSettingsError) ||
		!VansProjectSettingsStorage::SavePhysicsSettings(
			root + m_Config.physicsSettings,
			m_ProjectSettings.BuildPhysicsSettingsData(),
			projectSettingsError))
	{
		VANS_LOG_ERROR("[ProjectManager] Failed to write project settings files: "
			<< projectSettingsError);
		return false;
	}
	VansEngine::VansCollisionLayerConfig collisionLayers;
	collisionLayers.ResetToDefaults();
	VansEngine::AudioMixConfig audioMix;
	audioMix.displayName = projectName + " Default Audio Mix";
	if (!VansEngine::VansCollisionLayerStorage::SaveAtomic(
		root + m_Config.collisionLayerSettings,
		collisionLayers,
		projectSettingsError) ||
		!VansEngine::VansAudioMixConfigStorage::SaveAtomic(
			root + m_Config.audioSettings,
			audioMix,
			projectSettingsError))
	{
		VANS_LOG_ERROR("[ProjectManager] Failed to write project settings files: "
			<< projectSettingsError);
		return false;
	}
	std::string gafConfigurationError;
	if (!VansGAFProjectConfiguration::EnsureProjectFiles(
		fs::path(root) / "ProjectSettings",
		fs::path(m_PathResolver.GetEngineRoot()) / "EngineAssets/GAF/ProjectSettings",
		gafConfigurationError))
	{
		VANS_LOG_ERROR("[ProjectManager] Failed to initialize GAF project settings: "
			<< gafConfigurationError);
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
	VansScopedIOContext ioContext(
		VansIODomain::Authoring, "Project.Open", false);
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

	m_AssetObjectRepository.Clear();
	m_ProjectRootPath = root;
	m_PathResolver.SetProjectRoot(root);
	m_Loaded = true;
	m_ProjectSettings.SetDefaults();
	m_CollisionLayerDocument = {};
	m_AudioMixDocument = {};
	m_GAFProjectConfiguration = {};
	m_HasCollisionLayerDocument = false;
	m_HasAudioMixDocument = false;
	m_HasGAFProjectConfiguration = false;

	if (options.loadProjectSettings && !LoadProjectSettings())
	{
		VANS_LOG_WARN("[ProjectManager] One or more project settings documents could not be loaded; in-memory defaults remain active");
	}
	m_ProjectDocumentDirtyMask = 0;
	m_ProjectDocumentStateId = 0;
	m_ProjectDocumentSavedStateId = 0;
	CaptureProjectDocumentFingerprints();

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

		std::vector<VansAssetRecord> memoryRecords = m_AssetDatabase->All();
		const std::vector<VansAssetRecord> builtInRecords = m_BuiltInAssetDatabase->All();
		memoryRecords.insert(memoryRecords.end(), builtInRecords.begin(), builtInRecords.end());
		const VansAssetObjectBootstrapResult memoryBootstrap =
			VansAssetObjectBootstrapper::Publish(memoryRecords, m_AssetObjectRepository);
		if (!memoryBootstrap)
		{
			for (const std::string& error : memoryBootstrap.errors)
				VANS_LOG_ERROR("[AssetObjectRepository] " << error);
			m_AssetDatabase.reset();
			m_BuiltInAssetDatabase.reset();
			m_AssetObjectRepository.Clear();
			m_Loaded = false;
			return false;
		}
		VANS_LOG("[AssetObjectRepository] Published " << memoryBootstrap.published
			<< " project/built-in memory objects");
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
	if (!m_Loaded)
	{
		m_AssetObjectRepository.Clear();
		return;
	}

	VANS_LOG("[ProjectManager] Closing project '" << m_Config.projectName << "'");

	m_SceneManager.Clear();
	m_AssetDatabase.reset();
	m_BuiltInAssetDatabase.reset();
	m_AssetObjectRepository.Clear();
	m_Config = {};
	m_ProjectSettings.SetDefaults();
	m_CollisionLayerDocument = {};
	m_AudioMixDocument = {};
	m_HasCollisionLayerDocument = false;
	m_HasAudioMixDocument = false;
	m_ProjectRootPath.clear();
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

void VansProjectManager::MarkProjectDocumentDirty(std::uint8_t domain)
{
	m_ProjectDocumentDirtyMask |= domain;
	++m_ProjectDocumentStateId;
}

bool VansProjectManager::SetProjectPhysicsFixedTimeStep(
	float fixedTimeStep,
	std::string& error)

{
	if (!m_Loaded)
	{
		error = "No project is loaded";
		return false;
	}
	if (fixedTimeStep <= 0.0f)
	{
		error = "Physics fixed timestep must be greater than zero";
		return false;
	}
	if (m_ProjectSettings.GetFixedTimeStep() == fixedTimeStep)
	{
		error.clear();
		return true;
	}
	m_ProjectSettings.SetFixedTimeStep(fixedTimeStep);
	MarkProjectDocumentDirty(PhysicsSettingsDocument);
	error.clear();
	return true;
}

bool VansProjectManager::ValidateProjectRenderSettings(
	const VansProjectUpscalerSettings& upscalerSettings,
	const VansProjectRenderOutputSettings& outputSettings,
	std::string& error) const
{
	VansProjectSettings candidate = m_ProjectSettings;
	if (!candidate.SetUpscalerSettings(upscalerSettings, &error))
		return false;
	return candidate.SetRenderOutputSettings(outputSettings, &error);
}

bool VansProjectManager::SetProjectRenderSettings(
	const VansProjectUpscalerSettings& upscalerSettings,
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
	MarkProjectDocumentDirty(RenderSettingsDocument);
	error.clear();
	return true;
}

void VansProjectManager::SetProjectCommandRecordingSettings(
	bool parallelEnabled,
	bool frameContextRingEnabled,
	std::uint32_t framesInFlight,
	bool asyncComputeEnabled)
{
	if (!m_Loaded)
		return;
	m_ProjectSettings.SetCommandRecordingSettings(
		parallelEnabled,
		frameContextRingEnabled,
		framesInFlight,
		asyncComputeEnabled);
	MarkProjectDocumentDirty(RenderSettingsDocument);
}

void VansProjectManager::CaptureProjectDocumentFingerprint(
	const std::filesystem::path& path)
{
	ProjectDocumentFingerprint fingerprint;
	std::error_code ec;
	fingerprint.exists = std::filesystem::exists(path, ec);
	if (ec)
	{
		m_ProjectDocumentFingerprints[ProjectDocumentKey(path)] = fingerprint;
		return;
	}
	if (!fingerprint.exists)
	{
		fingerprint.valid = true;
		m_ProjectDocumentFingerprints[ProjectDocumentKey(path)] = fingerprint;
		return;
	}

	std::string bytes;
	std::string readError;
	if (!VansFileStorage::ReadAllBytes(path, bytes, readError))
	{
		m_ProjectDocumentFingerprints[ProjectDocumentKey(path)] = fingerprint;
		return;
	}
	fingerprint.size = bytes.size();
	fingerprint.contentHash = HashProjectDocumentBytes(bytes);
	fingerprint.writeTime = std::filesystem::last_write_time(path, ec);
	fingerprint.valid = !ec;
	m_ProjectDocumentFingerprints[ProjectDocumentKey(path)] = fingerprint;
}

void VansProjectManager::CaptureProjectDocumentFingerprints()
{
	m_ProjectDocumentFingerprints.clear();
	CaptureProjectDocumentFingerprint(fs::path(m_ProjectRootPath) / "ForestProject.json");
	if (!m_Config.renderSettings.empty())
		CaptureProjectDocumentFingerprint(fs::path(m_ProjectRootPath) / m_Config.renderSettings);
	if (!m_Config.physicsSettings.empty())
		CaptureProjectDocumentFingerprint(fs::path(m_ProjectRootPath) / m_Config.physicsSettings);
	if (!m_Config.collisionLayerSettings.empty())
		CaptureProjectDocumentFingerprint(fs::path(m_ProjectRootPath) / m_Config.collisionLayerSettings);
	if (!m_Config.audioSettings.empty())
		CaptureProjectDocumentFingerprint(fs::path(m_ProjectRootPath) / m_Config.audioSettings);
	for (const std::string_view fileName : VansGAFProjectConfiguration::DocumentFileNames())
		CaptureProjectDocumentFingerprint(
			fs::path(m_ProjectRootPath) / "ProjectSettings" / fileName);
}

bool VansProjectManager::EnsureProjectDocumentUnchanged(
	const std::filesystem::path& path,
	std::string& error) const
{
	const auto expected = m_ProjectDocumentFingerprints.find(ProjectDocumentKey(path));
	if (expected == m_ProjectDocumentFingerprints.end())
	{
		std::error_code ec;
		if (!std::filesystem::exists(path, ec) && !ec)
			return true;
		error = "Project document target was not loaded and cannot overwrite an existing file: " +
			path.string();
		return false;
	}
	if (!expected->second.valid)
	{
		error = "Project document has no valid load fingerprint: " + path.string();
		return false;
	}

	ProjectDocumentFingerprint current;
	std::error_code ec;
	current.exists = std::filesystem::exists(path, ec);
	if (ec)
	{
		error = "Cannot inspect project document before save: " + path.string();
		return false;
	}
	if (!current.exists)
	{
		current.valid = true;
	}
	else
	{
		std::string bytes;
		if (!VansFileStorage::ReadAllBytes(path, bytes, error))
			return false;
		current.size = bytes.size();
		current.contentHash = HashProjectDocumentBytes(bytes);
		current.writeTime = std::filesystem::last_write_time(path, ec);
		current.valid = !ec;
	}

	const ProjectDocumentFingerprint& loaded = expected->second;
	if (!current.valid || current.exists != loaded.exists ||
		current.size != loaded.size ||
		current.contentHash != loaded.contentHash ||
		(current.exists && current.writeTime != loaded.writeTime))
	{
		error = "Project document changed on disk after it was loaded: " + path.string();
		return false;
	}
	return true;
}

bool VansProjectManager::SaveProjectDocuments(std::string& error)
{
	VansScopedIOContext ioContext(
		VansIODomain::Authoring, "Project.SaveDocuments", true);
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

	const fs::path configPath = fs::path(m_ProjectRootPath) / "ForestProject.json";
	const fs::path renderSettingsPath = fs::path(m_ProjectRootPath) / m_Config.renderSettings;
	const fs::path physicsSettingsPath = fs::path(m_ProjectRootPath) / m_Config.physicsSettings;
	const fs::path collisionLayersPath = fs::path(m_ProjectRootPath) / m_Config.collisionLayerSettings;
	const fs::path audioMixPath = fs::path(m_ProjectRootPath) / m_Config.audioSettings;
	const fs::path projectSettingsDirectory = fs::path(m_ProjectRootPath) / "ProjectSettings";
	const auto gafFileNames = VansGAFProjectConfiguration::DocumentFileNames();
	std::array<fs::path, 4> gafPaths;
	for (std::size_t index = 0; index < gafPaths.size(); ++index)
		gafPaths[index] = projectSettingsDirectory / gafFileNames[index];
	std::vector<fs::path> dirtyPaths;
	if ((m_ProjectDocumentDirtyMask & ProjectConfigDocument) != 0)
		dirtyPaths.push_back(configPath);
	if ((m_ProjectDocumentDirtyMask & RenderSettingsDocument) != 0)
		dirtyPaths.push_back(renderSettingsPath);
	if ((m_ProjectDocumentDirtyMask & PhysicsSettingsDocument) != 0)
		dirtyPaths.push_back(physicsSettingsPath);
	if ((m_ProjectDocumentDirtyMask & CollisionLayersDocument) != 0)
		dirtyPaths.push_back(collisionLayersPath);
	if ((m_ProjectDocumentDirtyMask & AudioMixDocument) != 0)
		dirtyPaths.push_back(audioMixPath);
	if ((m_ProjectDocumentDirtyMask & GAFConfigurationDocuments) != 0)
		dirtyPaths.insert(dirtyPaths.end(), gafPaths.begin(), gafPaths.end());
	for (const fs::path& path : dirtyPaths)
		if (!EnsureProjectDocumentUnchanged(path, error))
			return false;

	if ((m_ProjectDocumentDirtyMask & ProjectConfigDocument) != 0)
	{
		VansProjectConfigDiagnostics diagnostics;
		if (!VansProjectConfigValidator::ValidateForSave(m_Config, diagnostics, error))
			return false;
	}

	VansStagedFileTransaction transaction;
	const auto stageJson = [&](const fs::path& path, const nlohmann::json& root)
	{
		VansStagedFile stage;
		if (!VansJsonFileStorage::StageWrite(path, root, stage, error))
			return false;
		transaction.Add(std::move(stage));
		return true;
	};
	if ((m_ProjectDocumentDirtyMask & ProjectConfigDocument) != 0 &&
		!stageJson(configPath, VansProjectConfigJsonCodec::EncodeProjectConfig(m_Config)))
		return false;
	if ((m_ProjectDocumentDirtyMask & RenderSettingsDocument) != 0 &&
		!stageJson(renderSettingsPath,
			VansProjectSettingsJsonCodec::EncodeRenderSettings(
				m_ProjectSettings.BuildRenderSettingsData())))
		return false;
	if ((m_ProjectDocumentDirtyMask & PhysicsSettingsDocument) != 0 &&
		!stageJson(physicsSettingsPath,
			VansProjectSettingsJsonCodec::EncodePhysicsSettings(
				m_ProjectSettings.BuildPhysicsSettingsData())))
		return false;
	if ((m_ProjectDocumentDirtyMask & CollisionLayersDocument) != 0 &&
		(!m_HasCollisionLayerDocument ||
		 !stageJson(collisionLayersPath,
			 EncodeSerializedValueJson<nlohmann::json>(m_CollisionLayerDocument))))
	{
		if (!m_HasCollisionLayerDocument)
			error = "Collision layer document is unavailable";
		return false;
	}
	if ((m_ProjectDocumentDirtyMask & AudioMixDocument) != 0 &&
		(!m_HasAudioMixDocument ||
		 !stageJson(audioMixPath,
			 EncodeSerializedValueJson<nlohmann::json>(m_AudioMixDocument))))
	{
		if (!m_HasAudioMixDocument)
			error = "Audio mix document is unavailable";
		return false;
	}
	if ((m_ProjectDocumentDirtyMask & GAFConfigurationDocuments) != 0)
	{
		if (!m_HasGAFProjectConfiguration || !m_GAFProjectConfiguration.Validate(error))
		{
			if (!m_HasGAFProjectConfiguration)
				error = "GAF project configuration is unavailable";
			return false;
		}
		const VansGAFProjectConfigurationDocuments documents =
			VansGAFProjectConfiguration::EncodeDocuments(m_GAFProjectConfiguration);
		const VansSerializedValue* roots[] = {
			&documents.settings, &documents.schemaRegistry,
			&documents.validationRules, &documents.templates
		};
		for (std::size_t index = 0; index < gafPaths.size(); ++index)
			if (!stageJson(gafPaths[index],
				EncodeSerializedValueJson<nlohmann::json>(*roots[index])))
				return false;
	}
	if (!transaction.Publish(error))
		return false;

	for (const fs::path& path : dirtyPaths)
		CaptureProjectDocumentFingerprint(path);
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
	std::uint32_t dirtyMask = ProjectConfigDocument;

	switch (field)
	{
	case VansProjectConfigPathField::DefaultScene:
		if (m_Config.defaultScene == normalized) return true;
		m_Config.defaultScene = normalized;
		m_SceneManager.SetDefaultScene(m_Config.defaultScene);
		break;
	case VansProjectConfigPathField::AssetsRoot:
		if (m_Config.assetsRoot == normalized) return true;
		m_Config.assetsRoot = normalized;
		break;
	case VansProjectConfigPathField::ImportedArtifactRoot:
		if (m_Config.importedArtifactRoot == normalized) return true;
		m_Config.importedArtifactRoot = normalized;
		break;
	case VansProjectConfigPathField::RenderSettings:
		if (m_Config.renderSettings == normalized) return true;
		m_Config.renderSettings = normalized;
		dirtyMask |= RenderSettingsDocument;
		break;
	case VansProjectConfigPathField::PhysicsSettings:
		if (m_Config.physicsSettings == normalized) return true;
		m_Config.physicsSettings = normalized;
		dirtyMask |= PhysicsSettingsDocument;
		break;
	case VansProjectConfigPathField::CollisionLayerSettings:
		if (m_Config.collisionLayerSettings == normalized) return true;
		m_Config.collisionLayerSettings = normalized;
		break;
	default:
		error = "Unsupported project config path field";
		return false;
	}
	MarkProjectDocumentDirty(dirtyMask);
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
	MarkProjectDocumentDirty(ProjectConfigDocument);
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

	const auto existing = m_Config.assetDirectories.find(key);
	if (existing != m_Config.assetDirectories.end() && existing->second == normalized)
		return true;
	m_Config.assetDirectories[key] = normalized;
	MarkProjectDocumentDirty(ProjectConfigDocument);
	return true;
}

bool VansProjectManager::SetCollisionLayerDocument(
	const VansSerializedValue& document,
	std::string& error)
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
	MarkProjectDocumentDirty(CollisionLayersDocument);
	error.clear();
	return true;
}

bool VansProjectManager::SetAudioMixDocument(
	const VansSerializedValue& document,
	std::string& error)
{
	if (!m_Loaded)
	{
		error = "No project is loaded";
		return false;
	}
	const nlohmann::json root = EncodeSerializedValueJson<nlohmann::json>(document);
	VansEngine::AudioMixConfig config;
	if (!VansEngine::VansAudioMixConfigJsonCodec::Decode(root, config, error))
		return false;
	const nlohmann::json normalized = VansEngine::VansAudioMixConfigJsonCodec::Encode(config);
	if (m_HasAudioMixDocument &&
		EncodeSerializedValueJson<nlohmann::json>(m_AudioMixDocument) == normalized)
	{
		error.clear();
		return true;
	}
	m_AudioMixDocument = DecodeSerializedValueJson(normalized);
	m_HasAudioMixDocument = true;
	MarkProjectDocumentDirty(AudioMixDocument);
	error.clear();
	return true;
}

bool VansProjectManager::SetGAFProjectConfiguration(
	const VansGAFProjectConfiguration& configuration,
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
	MarkProjectDocumentDirty(GAFConfigurationDocuments);
	error.clear();
	return true;
}

bool VansProjectManager::LoadProjectSettings()
{
	if (m_ProjectRootPath.empty())
		return false;

	bool loadedAnySettings = false;
	if (!m_Config.renderSettings.empty())
	{
		const std::string renderSettingsPath = m_ProjectRootPath + m_Config.renderSettings;
		VansProjectRenderSettingsData renderSettings;
		std::vector<std::string> warnings;
		std::string error;
		if (VansProjectSettingsStorage::LoadRenderSettings(
			renderSettingsPath, renderSettings, warnings, error))
		{
			for (const std::string& warning : warnings)
				VANS_LOG_WARN("[ProjectSettings] " << warning);
			if (!m_ProjectSettings.ApplyRenderSettingsData(renderSettings, error))
				VANS_LOG_ERROR("[ProjectSettings] Invalid render settings: " << error);
			else
			{
				loadedAnySettings = true;
				VANS_LOG("[ProjectSettings] Loaded render settings: " << renderSettingsPath);
			}
		}
		else
			VANS_LOG_WARN("[ProjectSettings] Cannot read render settings: "
				<< renderSettingsPath << " (" << error << ")");
	}

	if (!m_Config.physicsSettings.empty())
	{
		const std::string physicsSettingsPath = m_ProjectRootPath + m_Config.physicsSettings;
		VansProjectPhysicsSettingsData physicsSettings;
		std::string error;
		if (VansProjectSettingsStorage::LoadPhysicsSettings(
			physicsSettingsPath, physicsSettings, error))
		{
			m_ProjectSettings.ApplyPhysicsSettingsData(physicsSettings);
			loadedAnySettings = true;
			VANS_LOG("[ProjectSettings] Loaded physics settings: " << physicsSettingsPath);
		}
		else
			VANS_LOG_WARN("[ProjectSettings] Cannot read physics settings: "
				<< physicsSettingsPath << " (" << error << ")");
	}

	if (!m_Config.collisionLayerSettings.empty())
	{
		const std::string collisionLayerSettingsPath =
			m_ProjectRootPath + m_Config.collisionLayerSettings;
		VansEngine::VansCollisionLayerConfig collisionLayers;
		std::string error;
		const VansEngine::VansCollisionLayerLoadStatus status =
			VansEngine::VansCollisionLayerStorage::Load(
				collisionLayerSettingsPath, collisionLayers, error);
		if (status == VansEngine::VansCollisionLayerLoadStatus::Loaded)
		{
			VansEngine::VansCollisionLayerManager::Get().ApplyConfig(collisionLayers);
			m_CollisionLayerDocument = DecodeSerializedValueJson(
				VansEngine::VansCollisionLayerJsonCodec::Encode(collisionLayers));
			m_HasCollisionLayerDocument = true;
			loadedAnySettings = true;
			VANS_LOG("[ProjectSettings] Loaded collision layers: " << collisionLayerSettingsPath);
		}
		else
		{
			VansEngine::VansCollisionLayerManager::Get().ResetToDefaults();
			m_HasCollisionLayerDocument = false;
			VANS_LOG_WARN("[ProjectSettings] Cannot read collision layers: "
				<< collisionLayerSettingsPath << " (" << error << ")");
		}
	}

	if (!m_Config.audioSettings.empty())
	{
		const std::string audioMixPath = m_ProjectRootPath + m_Config.audioSettings;
		VansEngine::AudioMixConfig audioMix;
		std::string error;
		if (VansEngine::VansAudioMixConfigStorage::Load(audioMixPath, audioMix, error))
		{
			m_AudioMixDocument = DecodeSerializedValueJson(
				VansEngine::VansAudioMixConfigJsonCodec::Encode(audioMix));
			m_HasAudioMixDocument = true;
			loadedAnySettings = true;
			VANS_LOG("[ProjectSettings] Loaded audio mix: " << audioMixPath);
		}
		else
		{
			m_HasAudioMixDocument = false;
			VANS_LOG_WARN("[ProjectSettings] Cannot read audio mix: "
				<< audioMixPath << " (" << error << ")");
		}
	}

	{
		VansGAFProjectConfiguration configuration;
		std::string error;
		if (VansGAFProjectConfiguration::LoadForProject(
			m_ProjectRootPath, m_PathResolver.GetEngineRoot(), configuration, error))
		{
			m_GAFProjectConfiguration = std::move(configuration);
			m_HasGAFProjectConfiguration = true;
			loadedAnySettings = true;
			VANS_LOG("[ProjectSettings] Loaded GAF project configuration");
		}
		else
		{
			m_GAFProjectConfiguration = {};
			m_HasGAFProjectConfiguration = false;
			VANS_LOG_WARN("[ProjectSettings] Cannot read GAF configuration: " << error);
		}
	}
	return loadedAnySettings;
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
