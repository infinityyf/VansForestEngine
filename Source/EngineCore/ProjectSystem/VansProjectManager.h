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
#include "../AssetCore/VansAssetObjectRepository.h"
#include "../AssetCore/Serialization/VansSerializedValue.h"
#include "../GameplayActionSchema/VansGAFProjectConfiguration.h"

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
	VansAssetOperationPolicy assetPolicy = VansAssetOperationPolicy::ReadOnly();
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
	const VansProjectSettings& GetProjectSettings() const { return m_ProjectSettings; }
	VansSceneManager&        GetSceneManager()           { return m_SceneManager; }
	const VansPathResolver&  GetPathResolver()     const { return m_PathResolver; }
	VansAssetDatabase*       GetAssetDatabase()          { return m_AssetDatabase.get(); }
	const VansAssetDatabase* GetAssetDatabase() const    { return m_AssetDatabase.get(); }
	VansAssetDatabase*       GetBuiltInAssetDatabase()   { return m_BuiltInAssetDatabase.get(); }
	const VansAssetDatabase* GetBuiltInAssetDatabase() const { return m_BuiltInAssetDatabase.get(); }
	VansAssetObjectRepository& GetAssetObjectRepository() { return m_AssetObjectRepository; }
	const VansAssetObjectRepository& GetAssetObjectRepository() const { return m_AssetObjectRepository; }
	const VansSerializedValue* GetCollisionLayerDocument() const
	{
		return m_HasCollisionLayerDocument ? &m_CollisionLayerDocument : nullptr;
	}
	const VansSerializedValue* GetAudioMixDocument() const
	{
		return m_HasAudioMixDocument ? &m_AudioMixDocument : nullptr;
	}
	const VansGAFProjectConfiguration* GetGAFProjectConfiguration() const
	{
		return m_HasGAFProjectConfiguration ? &m_GAFProjectConfiguration : nullptr;
	}
	void SetPackagedAssetRecords(std::vector<VansAssetRecord> records);
	std::optional<VansAssetRecord> FindAssetRecord(VansAssetGuid guid) const;
	std::optional<VansAssetRecord> FindAssetRecordByPath(const std::filesystem::path& path) const;
	std::vector<VansAssetRecord> EnumerateAssetRecords() const;
	bool HasDirtyProjectDocuments() const { return m_ProjectDocumentDirtyMask != 0; }
	std::uint64_t GetProjectDocumentStateId() const { return m_ProjectDocumentStateId; }
	std::uint64_t GetProjectDocumentSavedStateId() const { return m_ProjectDocumentSavedStateId; }
	bool SetProjectPhysicsFixedTimeStep(float fixedTimeStep, std::string& error);
	bool ValidateProjectRenderSettings(
		const VansProjectUpscalerSettings& upscalerSettings,
		const VansProjectRenderOutputSettings& outputSettings,
		std::string& error) const;
	bool SetProjectRenderSettings(
		const VansProjectUpscalerSettings& upscalerSettings,
		const VansProjectRenderOutputSettings& outputSettings,
		std::string& error);
	void SetProjectCommandRecordingSettings(
		bool parallelEnabled,
		bool frameContextRingEnabled,
		std::uint32_t framesInFlight,
		bool asyncComputeEnabled);
	VansProjectConfigDiagnostics GetProjectConfigDiagnostics() const;
	bool SetProjectDefaultScene(const std::string& sceneRelativePath, std::string& error);
	bool SetProjectPathField(VansProjectConfigPathField field, const std::string& relativePath, std::string& error);
	bool SetProjectScriptSearchPaths(std::vector<std::string> paths, std::string& error);
	bool SetProjectAssetDirectory(const std::string& key, const std::string& relativePath, std::string& error);
	bool SetCollisionLayerDocument(const VansSerializedValue& document, std::string& error);
	bool SetAudioMixDocument(const VansSerializedValue& document, std::string& error);
	bool SetGAFProjectConfiguration(
		const VansGAFProjectConfiguration& configuration, std::string& error);
	bool SaveProjectDocuments(std::string& error);

	// ── Recent projects (delegates to RecentProjects namespace) ───

	std::vector<RecentProjectEntry> GetRecentProjects() const;
	void AddToRecentProjects(const std::string& path);

private:
	VansProjectManager();

	bool ValidateProjectStructure(const std::string& rootPath) const;
	void CreateDefaultDirectories(const std::string& rootPath);
	bool LoadProjectSettings();
	void MarkProjectDocumentDirty(std::uint8_t domain);
	void CaptureProjectDocumentFingerprints();
	bool EnsureProjectDocumentUnchanged(
		const std::filesystem::path& path,
		std::string& error) const;
	void CaptureProjectDocumentFingerprint(const std::filesystem::path& path);

	struct ProjectDocumentFingerprint
	{
		bool valid = false;
		bool exists = false;
		std::uintmax_t size = 0;
		std::uint64_t contentHash = 0;
		std::filesystem::file_time_type writeTime{};
	};

	static constexpr std::uint8_t ProjectConfigDocument = 1u << 0u;
	static constexpr std::uint8_t RenderSettingsDocument = 1u << 1u;
	static constexpr std::uint8_t PhysicsSettingsDocument = 1u << 2u;
	static constexpr std::uint8_t CollisionLayersDocument = 1u << 3u;
	static constexpr std::uint8_t AudioMixDocument = 1u << 4u;
	static constexpr std::uint8_t GAFConfigurationDocuments = 1u << 5u;

	bool              m_Loaded = false;
	std::string       m_ProjectRootPath;  // absolute, trailing '/'
	VansProjectConfig m_Config;
	VansProjectSettings m_ProjectSettings;
	VansPathResolver  m_PathResolver;
	VansSceneManager  m_SceneManager;
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
	std::unordered_map<std::string, ProjectDocumentFingerprint> m_ProjectDocumentFingerprints;
	std::uint8_t m_ProjectDocumentDirtyMask = 0;
	std::uint64_t m_ProjectDocumentStateId = 0;
	std::uint64_t m_ProjectDocumentSavedStateId = 0;
};

} // namespace Vans
