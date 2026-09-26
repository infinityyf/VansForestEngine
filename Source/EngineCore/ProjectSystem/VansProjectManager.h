#pragma once
// -----------------------------------------------------------------------
// VansProjectManager  –  Top-level singleton that owns the Project state
//
// Coupling policy:
//   - Owns project composition and staged publication of typed domain documents.
//   - Domain codecs stay in implementation files; public signatures expose only
//     the typed values required by composition callers.
//   - Does not depend on RenderCore, Vulkan or EditorCore implementation types.
// -----------------------------------------------------------------------

#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../AssetCore/VansAssetDatabase.h"
#include "../AssetCore/VansAssetObjectRepository.h"
#include "../GameplayActionSchema/VansGAFProjectConfiguration.h"
#include "VansPathResolver.h"
#include "VansProjectConfig.h"
#include "VansProjectConfigValidator.h"
#include "VansProjectDocumentSnapshot.h"
#include "VansProjectSettings.h"
#include "VansSceneManager.h"

#include <memory>
#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace VansEngine
{
	struct VansAudioMixConfig;
}

namespace Vans
{

class VansAssetDatabase;

struct VansProjectOpenOptions
{
	bool m_UpdateLastOpenedAt = false;
	bool m_UpdateRecentProjects = true;
	bool m_LoadProjectSettings = true;
	bool m_ScanAssets = true;
	VansAssetOperationPolicy m_AssetPolicy = VansAssetOperationPolicy::ReadOnly();
};

struct VansProjectOpenRequest
{
	std::string m_ProjectRootPath;
	VansProjectOpenOptions m_Options;
};

enum class VansProjectOpenFailure
{
	None,
	InvalidStructure,
	ConfigRead,
	ConfigValidation,
	ProjectSettingsRead,
	BuiltInAssets,
	AssetObjects,
};

struct VansProjectOpenResult
{
	bool m_Opened = false;
	VansProjectOpenFailure m_Failure = VansProjectOpenFailure::None;
	std::string m_Message;
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

	/// Open an existing project. `request.m_ProjectRootPath` must contain
	/// a ForestProject.json file.
	VansProjectOpenResult OpenProject(const VansProjectOpenRequest& request);

	/// Close the currently loaded project (clear all state).
	void CloseProject();

	/// Is a project currently loaded?
	bool IsProjectLoaded() const
	{
		return m_Loaded;
	}

	// ── Path helpers (delegate to VansPathResolver) ───────────────

	std::string ResolveAssetPath(const std::string& relativePath) const;
	std::string MakeRelativePath(const std::string& absolutePath) const;
	bool ValidateAssetPath(const std::string& relativePath) const;
	bool ConfigureEngineRoot(const std::filesystem::path& engineRoot, std::string& error);

	// ── Accessors ─────────────────────────────────────────────────

	const std::string& GetProjectRootPath() const
	{
		return m_ProjectRootPath;
	}
	const std::string& GetProjectName() const
	{
		return m_Config.projectName;
	}
	const VansProjectConfig& GetConfig() const
	{
		return m_Config;
	}
	const VansProjectSettings& GetProjectSettings() const
	{
		return m_ProjectSettings;
	}
	VansSceneManager& GetSceneManager()
	{
		return m_SceneManager;
	}
	const VansPathResolver& GetPathResolver() const
	{
		return m_PathResolver;
	}
	VansAssetDatabase* GetAssetDatabase()
	{
		return m_AssetDatabase.get();
	}
	const VansAssetDatabase* GetAssetDatabase() const
	{
		return m_AssetDatabase.get();
	}
	VansAssetDatabase* GetBuiltInAssetDatabase()
	{
		return m_BuiltInAssetDatabase.get();
	}
	const VansAssetDatabase* GetBuiltInAssetDatabase() const
	{
		return m_BuiltInAssetDatabase.get();
	}
	VansAssetObjectRepository& GetAssetObjectRepository()
	{
		return m_AssetObjectRepository;
	}
	const VansAssetObjectRepository& GetAssetObjectRepository() const
	{
		return m_AssetObjectRepository;
	}
	const VansSerializedValue* GetCollisionLayerDocument() const
	{
		return m_HasCollisionLayerDocument ? &m_CollisionLayerDocument : nullptr;
	}
	bool GetAudioMixConfig(VansEngine::VansAudioMixConfig& config, std::string& error) const;
	const VansGAFProjectConfiguration* GetGAFProjectConfiguration() const
	{
		return m_HasGAFProjectConfiguration ? &m_GAFProjectConfiguration : nullptr;
	}
	void SetPackagedAssetRecords(std::vector<VansAssetRecord> records);
	std::optional<VansAssetRecord> FindAssetRecord(VansAssetGuid guid) const;
	std::optional<VansAssetRecord> FindAssetRecordByPath(const std::filesystem::path& path) const;
	std::vector<VansAssetRecord> EnumerateAssetRecords() const;
	bool HasDirtyProjectDocuments() const
	{
		return m_ProjectDocumentDirtyMask != 0;
	}
	std::uint64_t GetProjectDocumentStateId() const
	{
		return m_ProjectDocumentStateId;
	}
	std::uint64_t GetProjectDocumentSavedStateId() const
	{
		return m_ProjectDocumentSavedStateId;
	}
	bool SetProjectPhysicsTiming(const VansEngine::VansPhysicsTiming& timing, std::string& error);
	bool ValidateProjectRenderSettings(const VansProjectUpscalerSettings& upscalerSettings,
									   const VansProjectRenderOutputSettings& outputSettings, std::string& error) const;
	bool SetProjectRenderSettings(const VansProjectUpscalerSettings& upscalerSettings,
								  const VansProjectRenderOutputSettings& outputSettings, std::string& error);
	void SetProjectCommandRecordingSettings(bool parallelEnabled, bool frameContextRingEnabled,
											std::uint32_t framesInFlight, bool asyncComputeEnabled);
	VansProjectConfigDiagnostics GetProjectConfigDiagnostics() const;
	bool SetProjectDefaultScene(const std::string& sceneRelativePath, std::string& error);
	bool SetProjectPathField(VansProjectConfigPathField field, const std::string& relativePath, std::string& error);
	bool SetProjectScriptSearchPaths(std::vector<std::string> paths, std::string& error);
	bool SetProjectAssetDirectory(const std::string& key, const std::string& relativePath, std::string& error);
	bool SetCollisionLayerDocument(const VansSerializedValue& document, std::string& error);
	bool SetAudioMixDocument(const VansSerializedValue& document, std::string& error);
	bool SetGAFProjectConfiguration(const VansGAFProjectConfiguration& configuration, std::string& error);
	bool SaveProjectDocuments(std::string& error);

	// ── Recent projects (delegates to RecentProjects namespace) ───

	std::vector<RecentProjectEntry> GetRecentProjects() const;
	void AddToRecentProjects(const std::string& path);

  private:
	VansProjectManager();

	bool ValidateProjectStructure(const std::string& rootPath) const;
	void CreateDefaultDirectories(const std::string& rootPath);
	void ApplyProjectDocuments(VansProjectDocumentSnapshot documents);
	void MarkProjectDocumentsDirty(std::uint8_t documentMask);

	bool m_Loaded = false;
	std::string m_ProjectRootPath; // absolute, trailing '/'
	VansProjectConfig m_Config;
	VansProjectSettings m_ProjectSettings;
	VansPathResolver m_PathResolver;
	VansSceneManager m_SceneManager;
	std::unique_ptr<VansAssetDatabase> m_AssetDatabase;
	std::unique_ptr<VansAssetDatabase> m_BuiltInAssetDatabase;
	VansAssetObjectRepository m_AssetObjectRepository;
	VansSerializedValue m_CollisionLayerDocument;
	VansSerializedValue m_AudioMixDocument;
	VansGAFProjectConfiguration m_GAFProjectConfiguration;
	bool m_HasCollisionLayerDocument = false;
	bool m_HasAudioMixDocument = false;
	bool m_HasGAFProjectConfiguration = false;
	std::vector<VansAssetRecord> m_PackagedAssetRecords;
	std::unordered_map<std::string, std::size_t> m_PackagedAssetRecordsByGuid;
	std::unordered_map<std::string, std::size_t> m_PackagedAssetRecordsByPath;
	std::unordered_map<std::string, VansProjectDocumentFingerprint> m_ProjectDocumentFingerprints;
	std::uint8_t m_ProjectDocumentDirtyMask = 0;
	std::uint64_t m_ProjectDocumentStateId = 0;
	std::uint64_t m_ProjectDocumentSavedStateId = 0;
};

} // namespace Vans
