#include "VansAssetDatabase.h"
#include "VansDerivedArtifactLayout.h"
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
constexpr VansAssetTypeDescriptor AssetTypes[] =
{
	{ VansAssetType::Model, "model", ".fbx", "ModelImporter" },
	{ VansAssetType::Texture, "texture", ".png", "TextureImporter" },
	{ VansAssetType::IESProfile, "iesProfile", ".ies", "IESProfileImporter" },
	{ VansAssetType::Material, "material", ".mat", "MaterialImporter" },
	{ VansAssetType::Shader, "shader", ".vshader", "ShaderImporter" },
	{ VansAssetType::Audio, "audio", ".wav", "AudioImporter" },
	{ VansAssetType::Video, "video", ".mp4", "VideoImporter" },
	{ VansAssetType::Scene, "scene", ".vscene", "SceneImporter" },
	{ VansAssetType::Particle, "particle", ".particle", "ParticleImporter" },
	{ VansAssetType::AnimationClip, "animationClip", ".vclip", "AnimationClipImporter" },
	{ VansAssetType::AnimatorController, "animatorController", ".vanimator", "AnimatorControllerImporter" },
	{ VansAssetType::AnimationRig, "animationRig", ".vanimrig", "AnimationRigImporter" },
	{ VansAssetType::RetargetProfile, "retargetProfile", ".vretarget", "RetargetProfileImporter" },
	{ VansAssetType::BoneMask, "boneMask", ".vbonemask", "BoneMaskImporter" },
	{ VansAssetType::Timeline, "timeline", ".vtimeline", "TimelineImporter" },
	{ VansAssetType::NavigationMesh, "navigationMesh", ".vnavmesh", "NavigationMeshImporter" },
	{ VansAssetType::AIBehavior, "aiBehavior", ".vaibehavior", "AIBehaviorImporter" },
	{ VansAssetType::ActionDefinition, "actionDefinition", ".vaction", "GameplayActionImporter" },
	{ VansAssetType::ActionSet, "actionSet", ".vactionset", "GameplayActionSetImporter" },
	{ VansAssetType::GameplayEffect, "gameplayEffect", ".veffect", "GameplayEffectImporter" },
	{ VansAssetType::GameplayCue, "gameplayCue", ".vcue", "GameplayCueImporter" },
	{ VansAssetType::AttributeSet, "attributeSet", ".vattributeset", "GameplayAttributeSetImporter" },
	{ VansAssetType::TargetingPolicy, "targetingPolicy", ".vtargeting", "GameplayTargetingImporter" },
	{ VansAssetType::GameplayTagTree, "gameplayTagTree", ".vtagtree", "GameplayTagTreeImporter" },
	{ VansAssetType::PayloadSchema, "payloadSchema", ".vpayloadschema", "GameplayPayloadSchemaImporter" },
	{ VansAssetType::ActionGraph, "actionGraph", ".vactiongraph", "GameplayActionGraphImporter" },
	{ VansAssetType::CameraRigProfile, "cameraRigProfile", ".vcamerarig", "CameraRigProfileImporter" },
	{ VansAssetType::CameraShakeProfile, "cameraShakeProfile", ".vcamerashake", "CameraShakeProfileImporter" },
	{ VansAssetType::GAFEditorLayout, "gafEditorLayout", ".gafeditorlayout", "GAFEditorLayoutImporter" },
	{ VansAssetType::ClothProfile, "clothProfile", ".clothprofile", "ClothProfileImporter" },
	{ VansAssetType::SkinProfile, "skinProfile", ".skinprofile", "SkinProfileImporter" },
	{ VansAssetType::PostProcessProfile, "postProcessProfile", ".pprofile", "PostProcessProfileImporter" },
	{ VansAssetType::RagdollProfile, "ragdollProfile", ".vragdoll", "RagdollProfileImporter" },
	{ VansAssetType::AudioReverbPreset, "audioReverbPreset", ".vreverb", "AudioReverbPresetImporter" },
	{ VansAssetType::AudioBusSnapshot, "audioBusSnapshot", ".vaudiosnapshot", "AudioBusSnapshotImporter" },
	{ VansAssetType::AudioDuckingRules, "audioDuckingRules", ".vducking", "AudioDuckingRulesImporter" },
	{ VansAssetType::UIScreen, "uiScreen", ".vui.json", "UIScreenImporter" },
	{ VansAssetType::UIComponent, "uiComponent", ".vcomp.json", "UIComponentImporter" },
	{ VansAssetType::UIThemeTokens, "uiThemeTokens", ".tokens.json", "UIThemeTokensImporter" },
	{ VansAssetType::UILocalization, "uiLocalization", ".loc.json", "UILocalizationImporter" },
	{ VansAssetType::UIXaml, "uiXaml", ".xaml", "UIXamlImporter" },
	{ VansAssetType::VegetationConfig, "vegetationConfig", {}, "VegetationConfigImporter" },
	{ VansAssetType::Terrain, "terrain", ".vterrain", "TerrainImporter" },
	{ VansAssetType::PlantType, "plantType", ".vplant", "PlantTypeImporter" },
	{ VansAssetType::PcgMask, "pcgMask", ".vpcgmask", "PcgMaskImporter" },
	{ VansAssetType::PcgSpline, "pcgSpline", ".vpcgspline", "PcgSplineImporter" },
	{ VansAssetType::DamageProfile, "damageProfile", ".vdamage", "DamageProfileImporter" },
	{ VansAssetType::Prefab, "prefab", ".vprefab", "PrefabImporter" }
};

struct VansAssetExtensionAlias
{
	VansAssetType type;
	std::string_view extension;
};

constexpr VansAssetExtensionAlias AssetExtensionAliases[] =
{
	{ VansAssetType::Model, ".obj" },
	{ VansAssetType::Model, ".gltf" },
	{ VansAssetType::Model, ".glb" },
	{ VansAssetType::Texture, ".jpg" },
	{ VansAssetType::Texture, ".jpeg" },
	{ VansAssetType::Texture, ".tga" },
	{ VansAssetType::Texture, ".hdr" },
	{ VansAssetType::Texture, ".exr" },
	{ VansAssetType::Texture, ".cubemap" },
	{ VansAssetType::Shader, ".vshader.json" },
	{ VansAssetType::Audio, ".mp3" },
	{ VansAssetType::Audio, ".ogg" },
	{ VansAssetType::Audio, ".flac" },
	{ VansAssetType::Video, ".mkv" },
	{ VansAssetType::Video, ".avi" },
	{ VansAssetType::Video, ".mov" },
	{ VansAssetType::Video, ".webm" },
	{ VansAssetType::Scene, ".scene" },
};

std::wstring LowerFileName(const std::filesystem::path& path)
{
	std::wstring value = path.filename().wstring();
	std::transform(value.begin(), value.end(), value.begin(), [](wchar_t character)
	{
		return static_cast<wchar_t>(std::towlower(character));
	});
	return value;
}

bool EndsWith(std::wstring_view value, std::string_view asciiSuffix)
{
	if (value.size() < asciiSuffix.size()) return false;
	const std::size_t offset = value.size() - asciiSuffix.size();
	for (std::size_t index = 0; index < asciiSuffix.size(); ++index)
		if (value[offset + index] != static_cast<wchar_t>(asciiSuffix[index])) return false;
	return true;
}

bool HasPathComponent(const std::filesystem::path& path, const std::wstring& expected)
{
	for (const std::filesystem::path& component : path)
	{
		std::wstring value = component.wstring();
		std::transform(value.begin(), value.end(), value.begin(),
			[](wchar_t character) { return static_cast<wchar_t>(std::towlower(character)); });
		if (value == expected)
			return true;
	}
	return false;
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
		{
			if (record.memoryOnly)
				m_ByPath[PathKey(record.sourcePath)] = guid;
			else
				record.state = VansAssetState::Missing;
		}
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
        if (!it->is_regular_file(ec) || EndsWith(LowerFileName(it->path()), ".meta"))
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
		const VansDerivedArtifactLocation artifact =
			VansDerivedArtifactLayout::ImportedRuntimeCache(m_ArtifactRoot, type, meta.guid);
		if (artifact && std::filesystem::is_regular_file(artifact.path, ec))
			cookedArtifactPath = artifact.path;
	}
	else if (type == VansAssetType::Model && !m_ArtifactRoot.empty())
	{
		const VansDerivedArtifactLocation artifact =
			VansDerivedArtifactLayout::ImportedRuntimeCache(m_ArtifactRoot, type, meta.guid);
		if (artifact && std::filesystem::is_regular_file(artifact.path, ec))
			cookedArtifactPath = artifact.path;
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
		type == VansAssetType::AIBehavior || type == VansAssetType::Prefab ||
		type == VansAssetType::ActionSet || type == VansAssetType::GameplayEffect ||
		type == VansAssetType::GameplayCue || type == VansAssetType::AttributeSet ||
		type == VansAssetType::TargetingPolicy || type == VansAssetType::GameplayTagTree ||
		type == VansAssetType::PayloadSchema || type == VansAssetType::ActionGraph ||
		type == VansAssetType::CameraRigProfile || type == VansAssetType::CameraShakeProfile ||
		type == VansAssetType::DamageProfile || type == VansAssetType::GAFEditorLayout
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
	record.memoryOnly = false;
	if (type == VansAssetType::Texture)
	{
		std::string colorSpace = meta.ReadStringSetting("colorSpace");
		std::transform(colorSpace.begin(), colorSpace.end(), colorSpace.begin(),
			[](unsigned char value) { return static_cast<char>(std::tolower(value)); });
		record.textureImport.available = true;
		record.textureImport.linear = colorSpace.empty()
			? !meta.ReadBoolSetting("sRGB", true)
			: colorSpace == "linear";
		record.textureImport.compressed = meta.ReadBoolSetting("useCompress", true);
		record.textureImport.mipmapped = meta.ReadBoolSetting("needMip", true);
		record.textureImport.channelCount = meta.ReadIntSetting("importChannel", 4);
		record.textureImport.precision = meta.ReadStringSetting("precision", "low8");
	}
	else
	{
		record.textureImport = {};
	}
    record.state = record.artifactPath.empty()
        ? VansAssetState::Discovered
        : VansAssetState::CpuReady;
    ++record.generation;
    record.error = textureCookError;
    m_ByPath[key] = meta.guid;
    return true;
}

bool VansAssetDatabase::RegisterMemoryAsset(VansAssetRecord record, std::string& error)
{
	error.clear();
	if (!record.guid.IsValid() || record.type == VansAssetType::Unknown || record.sourcePath.empty())
	{
		error = "Memory asset record has no valid identity";
		return false;
	}
	record.sourcePath = Normalize(record.sourcePath);
	record.metaPath = VansAssetMeta::MetaPathFor(record.sourcePath);
	const std::filesystem::path relative = record.sourcePath.lexically_relative(m_AssetsRoot);
	bool insideAssets = record.sourcePath == m_AssetsRoot ||
		(!relative.empty() && !relative.is_absolute());
	for (const std::filesystem::path& part : relative)
		if (part == "..") insideAssets = false;
	if (!insideAssets || Classify(record.sourcePath) != record.type)
	{
		error = "Memory asset path/type is outside the active Assets root";
		return false;
	}

	std::unique_lock lock(m_Mutex);
	const std::wstring key = PathKey(record.sourcePath);
	if (const auto found = m_ByGuid.find(record.guid); found != m_ByGuid.end() &&
		found->second.state != VansAssetState::Missing &&
		PathKey(found->second.sourcePath) != key)
	{
		error = "Memory asset GUID collides with another asset";
		return false;
	}
	if (const auto found = m_ByPath.find(key); found != m_ByPath.end() && found->second != record.guid)
	{
		error = "Memory asset path collides with another asset";
		return false;
	}
	record.state = VansAssetState::CpuReady;
	record.memoryOnly = true;
	if (record.generation == 0) record.generation = 1;
	m_ByGuid[record.guid] = record;
	m_ByPath[key] = record.guid;
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
	const std::wstring fileName = LowerFileName(sourcePath);
	for (const VansAssetTypeDescriptor& descriptor : AssetTypes)
		if (!descriptor.canonicalExtension.empty() &&
			EndsWith(fileName, descriptor.canonicalExtension))
			return descriptor.type;
	for (const VansAssetExtensionAlias& alias : AssetExtensionAliases)
		if (EndsWith(fileName, alias.extension)) return alias.type;
	if (EndsWith(fileName, ".json") &&
		HasPathComponent(sourcePath.parent_path(), L"vegetation"))
		return VansAssetType::VegetationConfig;
	return VansAssetType::Unknown;
}

const VansAssetTypeDescriptor* VansAssetDatabase::Describe(VansAssetType type) noexcept
{
	for (const VansAssetTypeDescriptor& descriptor : AssetTypes)
		if (descriptor.type == type) return &descriptor;
	return nullptr;
}

std::string VansAssetDatabase::ImporterFor(VansAssetType type)
{
	const VansAssetTypeDescriptor* descriptor = Describe(type);
	return descriptor ? std::string(descriptor->importer) : std::string{};
}

std::string_view VansAssetDatabase::SerializedTypeName(VansAssetType type) noexcept
{
	const VansAssetTypeDescriptor* descriptor = Describe(type);
	return descriptor ? descriptor->serializedName : std::string_view("unknown");
}

VansAssetType VansAssetDatabase::ParseSerializedType(std::string_view value) noexcept
{
	for (const VansAssetTypeDescriptor& descriptor : AssetTypes)
		if (descriptor.serializedName == value) return descriptor.type;
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
