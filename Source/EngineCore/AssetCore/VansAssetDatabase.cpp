#include "VansAssetDatabase.h"
#include "Importers/VansTextureCooker.h"
#include "Storage/VansAssetMetaStorage.h"
#include "../Util/VansFileFingerprint.h"
#include "../Util/VansLog.h"

#include <algorithm>
#include <cwctype>
#include <mutex>
#include <utility>

namespace Vans
{
namespace
{
struct SerializedAssetTypeEntry
{
    VansAssetType type;
    std::string_view name;
};

constexpr SerializedAssetTypeEntry SerializedAssetTypes[] = {
    { VansAssetType::Model, "model" },
    { VansAssetType::Texture, "texture" },
    { VansAssetType::Material, "material" },
    { VansAssetType::Shader, "shader" },
    { VansAssetType::Audio, "audio" },
    { VansAssetType::Video, "video" },
    { VansAssetType::Scene, "scene" },
    { VansAssetType::Particle, "particle" },
    { VansAssetType::AnimationClip, "animationClip" },
    { VansAssetType::AnimatorController, "animatorController" },
    { VansAssetType::AnimationRig, "animationRig" },
    { VansAssetType::BoneMask, "boneMask" },
    { VansAssetType::Timeline, "timeline" },
    { VansAssetType::NavigationMesh, "navigationMesh" },
    { VansAssetType::AIBehavior, "aiBehavior" },
    { VansAssetType::ActionDefinition, "actionDefinition" },
    { VansAssetType::ActionSet, "actionSet" },
    { VansAssetType::GameplayEffect, "gameplayEffect" },
    { VansAssetType::GameplayCue, "gameplayCue" },
    { VansAssetType::AttributeSet, "attributeSet" },
    { VansAssetType::TargetingPolicy, "targetingPolicy" },
    { VansAssetType::GameplayTagTree, "gameplayTagTree" },
    { VansAssetType::PayloadSchema, "payloadSchema" },
    { VansAssetType::ActionGraph, "actionGraph" },
    { VansAssetType::CameraRigProfile, "cameraRigProfile" },
    { VansAssetType::CameraShakeProfile, "cameraShakeProfile" },
    { VansAssetType::GAFEditorLayout, "gafEditorLayout" },
    { VansAssetType::ClothProfile, "clothProfile" },
    { VansAssetType::SkinProfile, "skinProfile" },
    { VansAssetType::PostProcessProfile, "postProcessProfile" },
    { VansAssetType::RagdollProfile, "ragdollProfile" },
    { VansAssetType::AudioReverbPreset, "audioReverbPreset" },
    { VansAssetType::AudioBusSnapshot, "audioBusSnapshot" },
    { VansAssetType::AudioDuckingRules, "audioDuckingRules" }
};

std::wstring LowerExtension(const std::filesystem::path& path)
{
    std::wstring extension = path.extension().wstring();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](wchar_t value) { return std::towlower(value); });
    return extension;
}
}

VansAssetOperationPolicy VansAssetOperationPolicy::ReadOnly()
{
    return {};
}

VansAssetOperationPolicy VansAssetOperationPolicy::Authoring()
{
    VansAssetOperationPolicy policy;
    policy.meta = VansAssetMetaPolicy::CreateMissing;
    return policy;
}

VansAssetOperationPolicy VansAssetOperationPolicy::Cooking()
{
    VansAssetOperationPolicy policy = Authoring();
    policy.artifact = VansAssetArtifactPolicy::CookIfNeeded;
    return policy;
}

VansAssetDatabase::VansAssetDatabase(
    std::filesystem::path assetsRoot,
    std::filesystem::path artifactRoot)
    : m_AssetsRoot(std::filesystem::absolute(std::move(assetsRoot)).lexically_normal())
    , m_ArtifactRoot(artifactRoot.empty()
        ? std::filesystem::path{}
        : std::filesystem::absolute(std::move(artifactRoot)).lexically_normal())
{
}

VansAssetScanResult VansAssetDatabase::Scan(const VansAssetOperationPolicy& policy)
{
    VansAssetScanResult result;
    std::error_code ec;
    if (!std::filesystem::is_directory(m_AssetsRoot, ec))
    {
        result.errors.push_back("Assets root is not a directory: " + m_AssetsRoot.string());
        return result;
    }

    {
        std::unique_lock lock(m_Mutex);
        m_ByPath.clear();
        for (auto& [guid, record] : m_ByGuid)
            record.state = VansAssetState::Missing;
    }

    for (std::filesystem::recursive_directory_iterator it(m_AssetsRoot, std::filesystem::directory_options::skip_permission_denied, ec), end;
         it != end; it.increment(ec))
    {
        if (ec)
        {
            result.errors.push_back(ec.message());
            ec.clear();
            continue;
        }
        if (!it->is_regular_file(ec) || LowerExtension(it->path()) == L".meta")
            continue;
        if (Classify(it->path()) == VansAssetType::Unknown)
            continue;
        const bool hadMeta = std::filesystem::exists(VansAssetMeta::MetaPathFor(it->path()));
        std::string error;
        bool artifactCooked = false;
        if (RegisterOrRefresh(it->path(), policy, error, &artifactCooked))
        {
            ++result.registered;
            if (!hadMeta)
                ++result.generatedMeta;
            if (artifactCooked)
                ++result.cookedArtifacts;
        }
        else
            result.errors.push_back(std::move(error));
    }
    return result;
}

bool VansAssetDatabase::RegisterOrRefresh(
    const std::filesystem::path& sourcePath,
    const VansAssetOperationPolicy& policy,
    std::string& error,
    bool* artifactCooked)
{
    if (artifactCooked)
        *artifactCooked = false;
    const std::filesystem::path normalized = Normalize(sourcePath);
    std::error_code ec;
    if (!std::filesystem::is_regular_file(normalized, ec))
    {
        error = "Asset source does not exist: " + normalized.string();
        return false;
    }
    const VansAssetType type = Classify(normalized);
    if (type == VansAssetType::Unknown)
    {
        error = "Unsupported asset type: " + normalized.string();
        return false;
    }

    const std::filesystem::path metaPath = VansAssetMeta::MetaPathFor(normalized);
    VansAssetMeta meta;
    if (std::filesystem::exists(metaPath, ec))
    {
        if (!VansAssetMetaStorage::Load(metaPath, meta, error))
            return false;
    }
    else
    {
        if (policy.meta != VansAssetMetaPolicy::CreateMissing)
        {
            error = "Asset has no meta: " + normalized.string();
            return false;
        }
        meta.guid = VansAssetGuid::New();
        meta.importer = ImporterFor(type);
        if (!VansAssetMetaStorage::SaveAtomic(metaPath, meta, error))
            return false;
    }
    if (meta.importer != ImporterFor(type))
    {
        error = "Asset importer does not match source extension: " + normalized.string();
        return false;
    }

    VansFileFingerprint sourceFingerprint;
    if (!ComputeFileFingerprint(normalized, sourceFingerprint, &error))
        return false;

    VansFileFingerprint metaFingerprint;
    if (!ComputeFileFingerprint(metaPath, metaFingerprint, &error))
        return false;

    std::filesystem::path cookedArtifactPath;
    std::string textureCookError;
    if (type == VansAssetType::Texture && policy.artifact == VansAssetArtifactPolicy::CookIfNeeded)
    {
        const VansTextureCookResult textureCook = VansTextureCooker::CookIfNeeded(
            normalized, metaPath, meta, m_ArtifactRoot);
        cookedArtifactPath = textureCook.artifactPath;
        textureCookError = textureCook.error;
        if (textureCook.status == VansTextureCookStatus::Cooked)
        {
            if (artifactCooked)
                *artifactCooked = true;
            VANS_LOG("[TextureCooker] Cooked " << normalized.string()
                << " -> " << textureCook.artifactPath.string());
        }
        else if (textureCook.status == VansTextureCookStatus::Failed)
        {
            VANS_LOG_WARN("[TextureCooker] " << textureCook.error
                << "; runtime source fallback remains enabled");
        }
    }
	else if (type == VansAssetType::Texture && !m_ArtifactRoot.empty())
	{
        const std::filesystem::path packagedTextureArtifact =
            m_ArtifactRoot / "Textures" / (meta.guid.ToString() + ".vtex");
		if (std::filesystem::is_regular_file(packagedTextureArtifact, ec))
			cookedArtifactPath = packagedTextureArtifact;
	}
	else if (type == VansAssetType::Model && !m_ArtifactRoot.empty())
	{
		const std::filesystem::path meshArtifact =
			m_ArtifactRoot / "Meshes" / (meta.guid.ToString() + ".vmesh");
		if (std::filesystem::is_regular_file(meshArtifact, ec))
			cookedArtifactPath = meshArtifact;
	}

    std::unique_lock lock(m_Mutex);
    const std::wstring key = PathKey(normalized);
    if (const auto existing = m_ByGuid.find(meta.guid); existing != m_ByGuid.end() &&
        existing->second.state != VansAssetState::Missing && PathKey(existing->second.sourcePath) != key)
    {
        error = "Duplicate asset guid in " + normalized.string() + " and " + existing->second.sourcePath.string();
        return false;
    }
    if (const auto existing = m_ByPath.find(key); existing != m_ByPath.end() && existing->second != meta.guid)
    {
        error = "Case-insensitive asset path collision: " + normalized.string();
        return false;
    }

    VansAssetRecord& record = m_ByGuid[meta.guid];
    record.guid = meta.guid;
    record.type = type;
    record.sourcePath = normalized;
    record.metaPath = metaPath;
	record.authoringPath = type == VansAssetType::Material || type == VansAssetType::Shader ||
		type == VansAssetType::Timeline || type == VansAssetType::ActionDefinition ||
		type == VansAssetType::AIBehavior ||
		type == VansAssetType::ActionSet || type == VansAssetType::GameplayEffect ||
		type == VansAssetType::GameplayCue || type == VansAssetType::AttributeSet ||
		type == VansAssetType::TargetingPolicy || type == VansAssetType::GameplayTagTree ||
		type == VansAssetType::PayloadSchema || type == VansAssetType::ActionGraph ||
		type == VansAssetType::CameraRigProfile || type == VansAssetType::CameraShakeProfile ||
		type == VansAssetType::GAFEditorLayout
		? normalized
		: std::filesystem::path{};
    record.artifactPath = cookedArtifactPath;
    record.artifactFormat = record.artifactPath.empty()
        ? VansAssetArtifactFormat::None
        : VansAssetArtifactFormat::Imported;
    record.sourceHash = sourceFingerprint.contentHash;
    record.metaHash = metaFingerprint.contentHash;
    record.hasSkeletalMesh = type == VansAssetType::Model && std::any_of(
        meta.subAssets.begin(), meta.subAssets.end(), [](const auto& subAsset) {
            return subAsset.first.rfind("bone:", 0) == 0;
        });
    record.state = record.artifactPath.empty()
        ? VansAssetState::Discovered
        : VansAssetState::CpuReady;
    ++record.generation;
    record.error = textureCookError;
    m_ByPath[key] = meta.guid;
    return true;
}

VansTextureArtifactEnsureResult VansAssetDatabase::EnsureTextureArtifact(VansAssetGuid guid)
{
    VansTextureArtifactEnsureResult result;
    const std::optional<VansAssetRecord> indexedRecord = Find(guid);
    if (!indexedRecord || indexedRecord->state == VansAssetState::Missing)
    {
        result.error = "Texture asset is not present in the authoring index: " + guid.ToString();
        return result;
    }
    if (indexedRecord->type != VansAssetType::Texture)
    {
        result.error = "Asset is not a texture: " + guid.ToString();
        return result;
    }

    VansAssetMeta meta;
    if (!VansAssetMetaStorage::Load(indexedRecord->metaPath, meta, result.error))
        return result;

    const VansTextureCookResult cook = VansTextureCooker::CookIfNeeded(
        indexedRecord->sourcePath,
        indexedRecord->metaPath,
        meta,
        m_ArtifactRoot);
    result.artifactPath = cook.artifactPath;
    result.error = cook.error;
    switch (cook.status)
    {
    case VansTextureCookStatus::NotEligible:
        result.status = VansTextureArtifactEnsureStatus::NotEligible;
        break;
    case VansTextureCookStatus::UpToDate:
        result.status = VansTextureArtifactEnsureStatus::UpToDate;
        break;
    case VansTextureCookStatus::Cooked:
        result.status = VansTextureArtifactEnsureStatus::Cooked;
        break;
    case VansTextureCookStatus::Failed:
    default:
        result.status = VansTextureArtifactEnsureStatus::Failed;
        break;
    }

    std::unique_lock lock(m_Mutex);
    const auto found = m_ByGuid.find(guid);
    if (found == m_ByGuid.end() || found->second.state == VansAssetState::Missing ||
        PathKey(found->second.sourcePath) != PathKey(indexedRecord->sourcePath))
    {
        result.status = VansTextureArtifactEnsureStatus::Failed;
        result.artifactPath.clear();
        result.error = "Texture asset changed while its cache was being prepared: " + guid.ToString();
        return result;
    }

    VansAssetRecord& record = found->second;
    const std::filesystem::path previousArtifactPath = record.artifactPath;
    const VansAssetState previousState = record.state;
    const std::string previousError = record.error;
    if (result.HasArtifact())
    {
        record.artifactPath = result.artifactPath;
        record.artifactFormat = VansAssetArtifactFormat::Imported;
        record.state = VansAssetState::CpuReady;
        record.error.clear();
    }
    else
    {
        // A stale cache must not remain readable after the source becomes
        // ineligible or a recook fails. Keep the file for diagnostics, but
        // remove it from the active authoring index so source fallback is used.
        record.artifactPath.clear();
        record.artifactFormat = VansAssetArtifactFormat::None;
        record.state = VansAssetState::Discovered;
        record.error = result.error;
    }
    if (record.artifactPath != previousArtifactPath || record.state != previousState ||
        record.error != previousError || result.status == VansTextureArtifactEnsureStatus::Cooked)
    {
        ++record.generation;
    }
    return result;
}

bool VansAssetDatabase::UpdateImportedArtifact(
    VansAssetGuid guid,
    const std::filesystem::path& artifactPath,
    std::string error)
{
    std::error_code ec;
    const bool artifactAvailable = !artifactPath.empty() &&
        std::filesystem::is_regular_file(artifactPath, ec);
    if (!artifactPath.empty() && !artifactAvailable)
        return false;

    std::unique_lock lock(m_Mutex);
    const auto found = m_ByGuid.find(guid);
    if (found == m_ByGuid.end() || found->second.state == VansAssetState::Missing)
        return false;

    VansAssetRecord& record = found->second;
    const std::filesystem::path normalizedArtifact = artifactAvailable
        ? std::filesystem::absolute(artifactPath).lexically_normal()
        : std::filesystem::path{};
    const VansAssetState nextState = artifactAvailable
        ? VansAssetState::CpuReady
        : VansAssetState::Discovered;
    const VansAssetArtifactFormat nextFormat = artifactAvailable
        ? VansAssetArtifactFormat::Imported
        : VansAssetArtifactFormat::None;
    if (record.artifactPath != normalizedArtifact || record.state != nextState ||
        record.artifactFormat != nextFormat || record.error != error)
    {
        record.artifactPath = normalizedArtifact;
        record.artifactFormat = nextFormat;
        record.state = nextState;
        record.error = std::move(error);
        ++record.generation;
    }
    return true;
}

bool VansAssetDatabase::RemovePath(const std::filesystem::path& sourcePath)
{
    std::unique_lock lock(m_Mutex);
    const auto path = m_ByPath.find(PathKey(Normalize(sourcePath)));
    if (path == m_ByPath.end())
        return false;
    if (auto record = m_ByGuid.find(path->second); record != m_ByGuid.end())
    {
        record->second.state = VansAssetState::Missing;
        ++record->second.generation;
    }
    m_ByPath.erase(path);
    return true;
}

std::optional<VansAssetRecord> VansAssetDatabase::Find(VansAssetGuid guid) const
{
    std::shared_lock lock(m_Mutex);
    const auto result = m_ByGuid.find(guid);
    return result == m_ByGuid.end() ? std::nullopt : std::optional<VansAssetRecord>(result->second);
}

std::optional<VansAssetRecord> VansAssetDatabase::Find(const std::filesystem::path& sourcePath) const
{
    std::shared_lock lock(m_Mutex);
    const auto path = m_ByPath.find(PathKey(Normalize(sourcePath)));
    if (path == m_ByPath.end())
        return std::nullopt;
    const auto record = m_ByGuid.find(path->second);
    return record == m_ByGuid.end() ? std::nullopt : std::optional<VansAssetRecord>(record->second);
}

std::vector<VansAssetRecord> VansAssetDatabase::All() const
{
    std::shared_lock lock(m_Mutex);
    std::vector<VansAssetRecord> result;
    result.reserve(m_ByGuid.size());
    for (const auto& [guid, record] : m_ByGuid)
        result.push_back(record);
    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) { return left.sourcePath < right.sourcePath; });
    return result;
}

VansAssetType VansAssetDatabase::Classify(const std::filesystem::path& sourcePath)
{
    const std::wstring extension = LowerExtension(sourcePath);
    if (extension == L".fbx" || extension == L".obj" || extension == L".gltf" || extension == L".glb") return VansAssetType::Model;
	if (extension == L".png" || extension == L".jpg" || extension == L".jpeg" || extension == L".tga" || extension == L".hdr" || extension == L".exr" || extension == L".cubemap") return VansAssetType::Texture;
    if (extension == L".mat") return VansAssetType::Material;
    if (extension == L".vshader") return VansAssetType::Shader;
    if (extension == L".json")
    {
        std::wstring fileName = sourcePath.filename().wstring();
        std::transform(fileName.begin(), fileName.end(), fileName.begin(), [](wchar_t c) { return static_cast<wchar_t>(std::towlower(c)); });
        const std::wstring suffix = L".vshader.json";
        if (fileName.size() >= suffix.size() &&
            fileName.compare(fileName.size() - suffix.size(), suffix.size(), suffix) == 0)
            return VansAssetType::Shader;
    }
	if (extension == L".wav" || extension == L".mp3" || extension == L".ogg" || extension == L".flac") return VansAssetType::Audio;
	if (extension == L".mp4" || extension == L".mkv" || extension == L".avi" || extension == L".mov" || extension == L".webm") return VansAssetType::Video;
    if (extension == L".scene" || extension == L".vscene") return VansAssetType::Scene;
    if (extension == L".particle") return VansAssetType::Particle;
    if (extension == L".vclip") return VansAssetType::AnimationClip;
    if (extension == L".vanimator") return VansAssetType::AnimatorController;
	if (extension == L".vanimrig") return VansAssetType::AnimationRig;
	if (extension == L".vbonemask") return VansAssetType::BoneMask;
    if (extension == L".vtimeline") return VansAssetType::Timeline;
	if (extension == L".vnavmesh") return VansAssetType::NavigationMesh;
	if (extension == L".vaibehavior") return VansAssetType::AIBehavior;
	if (extension == L".vaction") return VansAssetType::ActionDefinition;
	if (extension == L".vactionset") return VansAssetType::ActionSet;
	if (extension == L".veffect") return VansAssetType::GameplayEffect;
	if (extension == L".vcue") return VansAssetType::GameplayCue;
	if (extension == L".vattributeset") return VansAssetType::AttributeSet;
	if (extension == L".vtargeting") return VansAssetType::TargetingPolicy;
	if (extension == L".vtagtree") return VansAssetType::GameplayTagTree;
	if (extension == L".vpayloadschema") return VansAssetType::PayloadSchema;
	if (extension == L".vactiongraph") return VansAssetType::ActionGraph;
	if (extension == L".vcamerarig") return VansAssetType::CameraRigProfile;
	if (extension == L".vcamerashake") return VansAssetType::CameraShakeProfile;
	if (extension == L".gafeditorlayout") return VansAssetType::GAFEditorLayout;
    if (extension == L".clothprofile") return VansAssetType::ClothProfile;
    if (extension == L".skinprofile") return VansAssetType::SkinProfile;
    if (extension == L".pprofile") return VansAssetType::PostProcessProfile;
    if (extension == L".ragdoll") return VansAssetType::RagdollProfile;
    if (extension == L".vreverb") return VansAssetType::AudioReverbPreset;
    if (extension == L".vaudiosnapshot" || extension == L".vbusnapshot") return VansAssetType::AudioBusSnapshot;
    if (extension == L".vducking") return VansAssetType::AudioDuckingRules;
    return VansAssetType::Unknown;
}

std::string VansAssetDatabase::ImporterFor(VansAssetType type)
{
    switch (type)
    {
    case VansAssetType::Model: return "ModelImporter";
    case VansAssetType::Texture: return "TextureImporter";
    case VansAssetType::Material: return "MaterialImporter";
    case VansAssetType::Shader: return "ShaderImporter";
	case VansAssetType::Audio: return "AudioImporter";
	case VansAssetType::Video: return "VideoImporter";
    case VansAssetType::Scene: return "SceneImporter";
    case VansAssetType::Particle: return "ParticleImporter";
    case VansAssetType::AnimationClip: return "AnimationClipImporter";
    case VansAssetType::AnimatorController: return "AnimatorControllerImporter";
	case VansAssetType::AnimationRig: return "AnimationRigImporter";
	case VansAssetType::BoneMask: return "BoneMaskImporter";
    case VansAssetType::Timeline: return "TimelineImporter";
	case VansAssetType::NavigationMesh: return "NavigationMeshImporter";
	case VansAssetType::AIBehavior: return "AIBehaviorImporter";
	case VansAssetType::ActionDefinition: return "GameplayActionImporter";
	case VansAssetType::ActionSet: return "GameplayActionSetImporter";
	case VansAssetType::GameplayEffect: return "GameplayEffectImporter";
	case VansAssetType::GameplayCue: return "GameplayCueImporter";
	case VansAssetType::AttributeSet: return "GameplayAttributeSetImporter";
	case VansAssetType::TargetingPolicy: return "GameplayTargetingImporter";
	case VansAssetType::GameplayTagTree: return "GameplayTagTreeImporter";
	case VansAssetType::PayloadSchema: return "GameplayPayloadSchemaImporter";
	case VansAssetType::ActionGraph: return "GameplayActionGraphImporter";
	case VansAssetType::CameraRigProfile: return "CameraRigProfileImporter";
	case VansAssetType::CameraShakeProfile: return "CameraShakeProfileImporter";
	case VansAssetType::GAFEditorLayout: return "GAFEditorLayoutImporter";
    case VansAssetType::ClothProfile: return "ClothProfileImporter";
    case VansAssetType::SkinProfile: return "SkinProfileImporter";
    case VansAssetType::PostProcessProfile: return "PostProcessProfileImporter";
    case VansAssetType::RagdollProfile: return "RagdollProfileImporter";
    case VansAssetType::AudioReverbPreset: return "AudioReverbPresetImporter";
    case VansAssetType::AudioBusSnapshot: return "AudioBusSnapshotImporter";
    case VansAssetType::AudioDuckingRules: return "AudioDuckingRulesImporter";
    default: return {};
    }
}

std::string_view VansAssetDatabase::SerializedTypeName(VansAssetType type) noexcept
{
    for (const SerializedAssetTypeEntry& entry : SerializedAssetTypes)
        if (entry.type == type)
            return entry.name;
    return "unknown";
}

VansAssetType VansAssetDatabase::ParseSerializedType(std::string_view value) noexcept
{
    for (const SerializedAssetTypeEntry& entry : SerializedAssetTypes)
        if (entry.name == value)
            return entry.type;
    return VansAssetType::Unknown;
}

std::filesystem::path VansAssetDatabase::Normalize(const std::filesystem::path& path) const
{
    return std::filesystem::absolute(path).lexically_normal();
}

std::wstring VansAssetDatabase::PathKey(const std::filesystem::path& path)
{
    std::wstring key = path.generic_wstring();
    std::transform(key.begin(), key.end(), key.begin(), [](wchar_t value) { return std::towlower(value); });
    return key;
}
}
