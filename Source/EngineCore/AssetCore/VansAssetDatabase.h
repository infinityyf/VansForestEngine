#pragma once

#include "VansAssetMeta.h"

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace Vans
{
enum class VansAssetType
{
    Unknown,
    Model,
    Texture,
    Material,
    Shader,
	Audio,
	Video,
    Scene,
    Particle,
    AnimationClip,
    AnimatorController,
	AnimationRig,
	RetargetProfile,
	BoneMask,
	Timeline,
	NavigationMesh,
	AIBehavior,
	ActionDefinition,
	ActionSet,
	GameplayEffect,
	GameplayCue,
	AttributeSet,
	TargetingPolicy,
	GameplayTagTree,
	PayloadSchema,
	ActionGraph,
	CameraRigProfile,
	CameraShakeProfile,
	GAFEditorLayout,
    ClothProfile,
    SkinProfile,
    PostProcessProfile,
    RagdollProfile,
    AudioReverbPreset,
    AudioBusSnapshot,
    AudioDuckingRules,
	UIScreen,
	UIComponent,
	UIThemeTokens,
	UILocalization,
	UIXaml,
	VegetationConfig
};

enum class VansAssetState
{
    Discovered,
    Importing,
    CpuReady,
    GpuReady,
    Failed,
    Missing
};

enum class VansAssetArtifactFormat
{
    None,
    Imported,
    Source,
	Cooked
};

struct VansTextureAssetImportSettings
{
	bool available = false;
	bool linear = false;
	bool compressed = true;
	bool mipmapped = false;
	int channelCount = 0;
	std::string precision = "low8";
};

struct VansAssetRecord
{
    VansAssetGuid guid;
    VansAssetType type = VansAssetType::Unknown;
    VansAssetState state = VansAssetState::Discovered;
    std::filesystem::path sourcePath;
    std::filesystem::path metaPath;
	std::filesystem::path authoringPath;
    std::filesystem::path artifactPath;
    VansAssetArtifactFormat artifactFormat = VansAssetArtifactFormat::None;
    std::uint64_t sourceHash = 0;
    std::uint64_t metaHash = 0;
    std::uint64_t generation = 0;
    bool hasSkeletalMesh = false;
	bool memoryOnly = false;
	VansTextureAssetImportSettings textureImport;
    std::string error;
};

struct VansAssetScanResult
{
    std::size_t registered = 0;
    std::size_t generatedMeta = 0;
    std::size_t cookedArtifacts = 0;
    std::vector<std::string> errors;

    explicit operator bool() const { return errors.empty(); }
};

enum class VansTextureArtifactEnsureStatus
{
    NotEligible,
    UpToDate,
    Cooked,
    Failed
};

struct VansTextureArtifactEnsureResult
{
    VansTextureArtifactEnsureStatus status = VansTextureArtifactEnsureStatus::Failed;
    std::filesystem::path artifactPath;
    std::string error;

    bool HasArtifact() const
    {
        return (status == VansTextureArtifactEnsureStatus::UpToDate ||
            status == VansTextureArtifactEnsureStatus::Cooked) &&
            !artifactPath.empty();
    }
};

enum class VansAssetMetaPolicy
{
    RequireExisting,
    CreateMissing
};

enum class VansAssetArtifactPolicy
{
    ResolveExisting,
    CookIfNeeded
};

struct VansAssetOperationPolicy
{
    VansAssetMetaPolicy meta = VansAssetMetaPolicy::RequireExisting;
    VansAssetArtifactPolicy artifact = VansAssetArtifactPolicy::ResolveExisting;

    static VansAssetOperationPolicy ReadOnly();
    static VansAssetOperationPolicy Authoring();
    static VansAssetOperationPolicy Cooking();
};

class VansAssetDatabase
{
public:
    explicit VansAssetDatabase(
        std::filesystem::path assetsRoot,
        std::filesystem::path artifactRoot = {});

    VansAssetScanResult Scan(const VansAssetOperationPolicy& policy);
    bool RegisterOrRefresh(
        const std::filesystem::path& sourcePath,
        const VansAssetOperationPolicy& policy,
        std::string& error,
        bool* artifactCooked = nullptr);
	bool RegisterMemoryAsset(VansAssetRecord record, std::string& error);
    VansTextureArtifactEnsureResult EnsureTextureArtifact(VansAssetGuid guid);
    bool UpdateImportedArtifact(
        VansAssetGuid guid,
        const std::filesystem::path& artifactPath,
        std::string error = {});
    bool RemovePath(const std::filesystem::path& sourcePath);
    std::optional<VansAssetRecord> Find(VansAssetGuid guid) const;
    std::optional<VansAssetRecord> Find(const std::filesystem::path& sourcePath) const;
    std::vector<VansAssetRecord> All() const;
    const std::filesystem::path& AssetsRoot() const { return m_AssetsRoot; }
    const std::filesystem::path& ArtifactRoot() const { return m_ArtifactRoot; }

    static VansAssetType Classify(const std::filesystem::path& sourcePath);
    static std::string ImporterFor(VansAssetType type);
    static std::string_view SerializedTypeName(VansAssetType type) noexcept;
    static VansAssetType ParseSerializedType(std::string_view value) noexcept;

private:
    std::filesystem::path Normalize(const std::filesystem::path& path) const;
    static std::wstring PathKey(const std::filesystem::path& path);

    std::filesystem::path m_AssetsRoot;
    std::filesystem::path m_ArtifactRoot;
    mutable std::shared_mutex m_Mutex;
    std::unordered_map<VansAssetGuid, VansAssetRecord> m_ByGuid;
    std::unordered_map<std::wstring, VansAssetGuid> m_ByPath;
};
}
