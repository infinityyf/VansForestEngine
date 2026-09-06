#include "VansAssetObjectBootstrapper.h"

#include "../AICore/VansAIBehaviorAsset.h"
#include "../AICore/Serialization/VansAIBehaviorJsonCodec.h"
#include "../AnimationCore/VansAnimatorIO.h"
#include "../AnimationCore/VansAnimationClip.h"
#include "../AnimationCore/Serialization/VansRetargetProfileJsonCodec.h"
#include "../AnimationCore/Storage/VansAnimationRigStorage.h"
#include "../AnimationCore/Storage/VansBoneMaskStorage.h"
#include "../AnimationCore/Storage/VansRetargetProfileStorage.h"
#include "../AssetCore/Serialization/VansAssetMetaJsonCodec.h"
#include "../AssetCore/Serialization/VansClothProfileJsonCodec.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AssetCore/Serialization/VansSkinProfileJsonCodec.h"
#include "../AssetCore/Storage/VansClothProfileStorage.h"
#include "../AssetCore/Storage/VansAssetMetaStorage.h"
#include "../AssetCore/Storage/VansFileStorage.h"
#include "../AssetCore/Storage/VansMaterialAuthoringAssetStorage.h"
#include "../AssetCore/Storage/VansShaderAuthoringAssetStorage.h"
#include "../AssetCore/Storage/VansSkinProfileStorage.h"
#include "../AssetCore/VansAssetObjectRepository.h"
#include "../AssetCore/VansAssetMeta.h"
#include "../AudioCore/VansAudioBusSnapshotAsset.h"
#include "../AudioCore/VansAudioDuckingRulesAsset.h"
#include "../AudioCore/VansAudioReverbPresetAsset.h"
#include "../AudioCore/Storage/VansAudioBusSnapshotAssetStorage.h"
#include "../AudioCore/Storage/VansAudioDuckingRulesAssetStorage.h"
#include "../AudioCore/Storage/VansAudioReverbPresetAssetStorage.h"
#include "../GameplayActionSchema/VansGameplayAssetStorage.h"
#include "../NavigationCore/VansNavigationMesh.h"
#include "../ParticleCore/Serialization/VansParticleAssetJsonCodec.h"
#include "../ParticleCore/Storage/VansParticleAssetStorage.h"
#include "../PhysicsCore/Serialization/VansRagdollProfileJsonCodec.h"
#include "../PhysicsCore/Storage/VansRagdollProfileStorage.h"
#include "../RenderCore/Serialization/VansPostProcessProfileJsonCodec.h"
#include "../RenderCore/Storage/VansPostProcessProfileStorage.h"
#include "../RuntimeUI/Serialization/VansUIDocumentLoader.h"
#include "../RuntimeUI/VansUIAssetResolver.h"
#include "Serialization/VansVegetationConfigCodec.h"
#include "Storage/VansVegetationConfigStorage.h"
#include "../TimelineCore/VansTimelineSerialization.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <nlohmann/json.hpp>
#include <unordered_map>
#include <utility>

namespace Vans
{
namespace
{
	template <typename Asset, typename Loader>
	bool EnsurePublished(
		const VansAssetRecord& record,
		VansAssetObjectRepository& repository,
		Loader&& loader,
		std::vector<VansAssetGuid> dependencies,
		bool& published,
		std::string& error)
	{
		VansAssetObjectSnapshotInfo info;
		const std::uint64_t contentHash = AssetObjectContentHash(record);
		if (repository.FindInfo(record.guid, info) &&
			info.assetType == record.type && info.contentHash == contentHash &&
			repository.ResolveLatest<Asset>(record.guid))
		{
			published = false;
			return true;
		}

		auto object = std::make_shared<Asset>();
		if (!loader(*object, error)) return false;
		if (!repository.Publish<Asset>(
			record.guid,
			record.type,
			contentHash,
			std::move(object),
			std::move(dependencies),
			error).IsValid())
			return false;
		published = true;
		return true;
	}

	void CollectGuidDependencies(
		const VansSerializedValue& value,
		const std::unordered_map<std::string, VansAssetGuid>& indexedGuids,
		std::vector<VansAssetGuid>& dependencies)
	{
		if (value.kind == VansSerializedValue::Kind::String)
		{
			const auto found = indexedGuids.find(value.stringValue);
			if (found == indexedGuids.end()) return;
			if (std::find(dependencies.begin(), dependencies.end(), found->second) == dependencies.end())
				dependencies.push_back(found->second);
			return;
		}
		if (value.kind == VansSerializedValue::Kind::Array)
		{
			for (const VansSerializedValue& item : value.arrayItems)
				CollectGuidDependencies(item, indexedGuids, dependencies);
			return;
		}
		if (value.kind == VansSerializedValue::Kind::Object)
			for (const auto& [key, item] : value.objectFields)
			{
				(void)key;
				CollectGuidDependencies(item, indexedGuids, dependencies);
			}
	}

	bool IsUIJsonAsset(VansAssetType type)
	{
		return type == VansAssetType::UIScreen ||
			type == VansAssetType::UIComponent ||
			type == VansAssetType::UIThemeTokens ||
			 type == VansAssetType::UILocalization;
	}

	template <typename Asset>
	bool PublishDecoded(
		const VansAssetRecord& record,
		std::uint64_t contentHash,
		std::shared_ptr<Asset> object,
		std::vector<VansAssetGuid> dependencies,
		VansAssetObjectRepository& repository,
		std::string& error)
	{
		if (!object)
		{
			error = "Decoded asset object is null";
			return false;
		}
		return repository.Publish<Asset>(
			record.guid,
			record.type,
			contentHash == 0 ? 1 : contentHash,
			std::move(object),
			std::move(dependencies),
			error).IsValid();
	}

	void CollectParsedGuidDependencies(
		const VansSerializedValue& value,
		VansAssetGuid owner,
		std::vector<VansAssetGuid>& dependencies)
	{
		if (value.kind == VansSerializedValue::Kind::String)
		{
			VansAssetGuid parsed;
			if (VansAssetGuid::TryParse(value.stringValue, parsed) && parsed != owner &&
				std::find(dependencies.begin(), dependencies.end(), parsed) == dependencies.end())
				dependencies.push_back(parsed);
			return;
		}
		if (value.kind == VansSerializedValue::Kind::Array)
		{
			for (const VansSerializedValue& item : value.arrayItems)
				CollectParsedGuidDependencies(item, owner, dependencies);
			return;
		}
		if (value.kind == VansSerializedValue::Kind::Object)
			for (const auto& [key, item] : value.objectFields)
			{
				(void)key;
				CollectParsedGuidDependencies(item, owner, dependencies);
			}
	}
}

bool VansAssetObjectBootstrapper::PublishSerialized(
	const VansAssetRecord& record,
	const VansSerializedValue& sourceRoot,
	std::uint64_t contentHash,
	VansAssetObjectRepository& repository,
	std::string& error)
{
	std::vector<VansAssetGuid> dependencies;
	CollectParsedGuidDependencies(sourceRoot, record.guid, dependencies);
	const nlohmann::json json = EncodeSerializedValueJson<nlohmann::json>(sourceRoot);
	const nlohmann::ordered_json orderedJson =
		EncodeSerializedValueJson<nlohmann::ordered_json>(sourceRoot);
	if (VansGameplayAssetSchemaRegistry::IsGameplayAssetType(record.type))
	{
		auto asset = std::make_shared<VansGameplayAssetMemoryObject>();
		asset->sourcePath = !record.authoringPath.empty()
			? record.authoringPath : record.sourcePath;
		asset->sourceDocument = sourceRoot;
		return PublishDecoded(record, contentHash, std::move(asset),
			std::move(dependencies), repository, error);
	}

	if (record.type == VansAssetType::Material)
	{
		auto asset = std::make_shared<VansMaterialAuthoringAsset>();
		return ReadMaterialAuthoringAsset(sourceRoot, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::Shader)
	{
		auto asset = std::make_shared<VansShaderAuthoringAsset>();
		return ReadShaderAuthoringAsset(sourceRoot, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::SkinProfile)
	{
		auto asset = std::make_shared<VansSkinProfile>();
		return VansSkinProfileJsonCodec::Decode(orderedJson, record.sourcePath, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::PostProcessProfile)
	{
		auto asset = std::make_shared<VansGraphics::VansPostProcessProfile>();
		return VansGraphics::VansPostProcessProfileJsonCodec::Decode(orderedJson, record.sourcePath, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::VegetationConfig)
	{
		auto asset = std::make_shared<VansVegetationConfigAsset>();
		return VansVegetationConfigCodec::Decode(sourceRoot, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::AIBehavior)
	{
		auto asset = std::make_shared<VansAIBehaviorAsset>();
		return VansAIBehaviorJsonCodec::Decode(json, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::RetargetProfile)
	{
		auto asset = std::make_shared<VansGraphics::VansRetargetProfileAsset>();
		return VansGraphics::VansRetargetProfileJsonCodec::Decode(json, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::RagdollProfile)
	{
		auto asset = std::make_shared<VansEngine::RagdollProfile>();
		return VansEngine::VansRagdollProfileJsonCodec::Decode(json, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::AnimationRig)
	{
		auto asset = std::make_shared<VansGraphics::VansAnimationRigAsset>();
		return VansGraphics::VansAnimationRigStorage::DeserializeFromJsonObject(json, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::BoneMask)
	{
		auto asset = std::make_shared<VansGraphics::VansBoneMaskAsset>();
		return VansGraphics::VansBoneMaskStorage::DeserializeFromJsonObject(json, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::Timeline)
	{
		auto asset = std::make_shared<VansTimelineAsset>();
		return VansTimelineSerialization::DecodeSerialized(sourceRoot, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::AnimatorController)
	{
		auto asset = std::make_shared<VansGraphics::AnimatorAssetData>();
		return VansGraphics::VansAnimatorIO::DeserializeFromJsonObject(json, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::AudioReverbPreset)
	{
		auto asset = std::make_shared<VansAudioReverbPresetAsset>();
		return ReadAudioReverbPresetAsset(sourceRoot, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::AudioBusSnapshot)
	{
		auto asset = std::make_shared<VansAudioBusSnapshotAsset>();
		return ReadAudioBusSnapshotAsset(sourceRoot, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::AudioDuckingRules)
	{
		auto asset = std::make_shared<VansAudioDuckingRulesAsset>();
		return ReadAudioDuckingRulesAsset(sourceRoot, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::ClothProfile)
	{
		auto asset = std::make_shared<VansEngine::VansClothProfile>();
		return VansEngine::VansClothProfileJsonCodec::Decode(orderedJson, record.sourcePath, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (record.type == VansAssetType::Particle)
	{
		auto asset = std::make_shared<VansGraphics::VansParticleAsset>();
		return VansGraphics::VansParticleAssetJsonCodec::Decode(json, record.sourcePath, *asset, error) &&
			PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}
	if (IsUIJsonAsset(record.type))
	{
		auto asset = std::make_shared<VansRuntime::VansUIAssetDocument>();
		asset->root = sourceRoot;
		asset->sourcePath = record.sourcePath;
		asset->contentHash = contentHash == 0 ? 1 : contentHash;
		return PublishDecoded(record, contentHash, std::move(asset), std::move(dependencies), repository, error);
	}

	error = "Asset type does not have a serialized memory-object codec";
	return false;
}

bool VansAssetObjectBootstrapper::PublishMetadataSerialized(
	const VansAssetRecord& record,
	const VansSerializedValue& metaRoot,
	std::uint64_t contentHash,
	VansAssetObjectRepository& repository,
	std::string& error)
{
	VansAssetMeta meta;
	const nlohmann::ordered_json json =
		EncodeSerializedValueJson<nlohmann::ordered_json>(metaRoot);
	if (!VansAssetMetaJsonCodec::Decode(json, record.metaPath, meta, error))
		return false;
	if (meta.guid != record.guid)
	{
		error = "Working metadata GUID does not match the indexed asset GUID";
		return false;
	}
	if (!meta.HasObjectSettings())
	{
		error = "Asset metadata settings must be an object";
		return false;
	}
	if (!Supports(record.type))
	{
		return repository.Publish<VansAssetMeta>(
			record.guid,
			record.type,
			contentHash == 0 ? 1 : contentHash,
			std::make_shared<const VansAssetMeta>(std::move(meta)),
			{},
			error).IsValid();
	}
	return repository.PublishView<VansAssetMeta>(
		record.guid,
		record.type,
		contentHash == 0 ? 1 : contentHash,
		std::make_shared<const VansAssetMeta>(std::move(meta)),
		error).IsValid();
}

VansAssetObjectBootstrapResult VansAssetObjectBootstrapper::Publish(
	const std::vector<VansAssetRecord>& records,
	VansAssetObjectRepository& repository)
{
	VansAssetObjectBootstrapResult result;
	std::unordered_map<std::string, VansAssetGuid> indexedGuids;
	indexedGuids.reserve(records.size());
	for (const VansAssetRecord& record : records)
		if (record.state != VansAssetState::Missing)
			indexedGuids.emplace(record.guid.ToString(), record.guid);

	for (const VansAssetRecord& record : records)
	{
		if (record.state == VansAssetState::Missing) continue;
		bool published = false;
		bool handled = true;
		bool success = false;
		std::string error;

		if (record.type == VansAssetType::Material)
			success = EnsurePublished<VansMaterialAuthoringAsset>(record, repository,
				[&](VansMaterialAuthoringAsset& asset, std::string& loadError)
				{ return VansMaterialAuthoringAssetStorage::Load(record.authoringPath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::Shader)
			success = EnsurePublished<VansShaderAuthoringAsset>(record, repository,
				[&](VansShaderAuthoringAsset& asset, std::string& loadError)
				{ return VansShaderAuthoringAssetStorage::Load(record.authoringPath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::SkinProfile)
			success = EnsurePublished<VansSkinProfile>(record, repository,
				[&](VansSkinProfile& asset, std::string& loadError)
				{ return VansSkinProfileStorage::Load(record.sourcePath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::PostProcessProfile)
			success = EnsurePublished<VansGraphics::VansPostProcessProfile>(record, repository,
				[&](VansGraphics::VansPostProcessProfile& asset, std::string& loadError)
				{ return VansGraphics::VansPostProcessProfileStorage::Load(record.sourcePath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::VegetationConfig)
			success = EnsurePublished<VansVegetationConfigAsset>(record, repository,
				[&](VansVegetationConfigAsset& asset, std::string& loadError)
				{ return VansVegetationConfigStorage::Load(record.sourcePath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::AIBehavior)
			success = EnsurePublished<VansAIBehaviorAsset>(record, repository,
				[&](VansAIBehaviorAsset& asset, std::string& loadError)
				{ return VansAIBehaviorAssetStorage::Load(record.sourcePath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::NavigationMesh)
			success = EnsurePublished<VansNavigationMesh>(record, repository,
				[&](VansNavigationMesh& asset, std::string& loadError)
				{ return asset.Load(record.sourcePath, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::RetargetProfile)
			success = EnsurePublished<VansGraphics::VansRetargetProfileAsset>(record, repository,
				[&](VansGraphics::VansRetargetProfileAsset& asset, std::string& loadError)
				{ return VansGraphics::VansRetargetProfileStorage::Load(record.sourcePath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::RagdollProfile)
			success = EnsurePublished<VansEngine::RagdollProfile>(record, repository,
				[&](VansEngine::RagdollProfile& asset, std::string& loadError)
				{ return VansEngine::VansRagdollProfileStorage::Load(record.sourcePath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::AnimationRig)
			success = EnsurePublished<VansGraphics::VansAnimationRigAsset>(record, repository,
				[&](VansGraphics::VansAnimationRigAsset& asset, std::string& loadError)
				{ return VansGraphics::VansAnimationRigStorage::Load(record.sourcePath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::BoneMask)
			success = EnsurePublished<VansGraphics::VansBoneMaskAsset>(record, repository,
				[&](VansGraphics::VansBoneMaskAsset& asset, std::string& loadError)
				{ return VansGraphics::VansBoneMaskStorage::Load(record.sourcePath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::Timeline)
			success = EnsurePublished<VansTimelineAsset>(record, repository,
				[&](VansTimelineAsset& asset, std::string& loadError)
				{ return VansTimelineSerialization::Load(record.sourcePath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::AnimatorController)
			success = EnsurePublished<VansGraphics::AnimatorAssetData>(record, repository,
				[&](VansGraphics::AnimatorAssetData& asset, std::string& loadError)
				{
					if (VansGraphics::VansAnimatorIO::Load(record.sourcePath.string(), asset)) return true;
					loadError = "Animator definition cannot be decoded";
					return false;
				}, {}, published, error);
		else if (record.type == VansAssetType::AnimationClip)
			success = EnsurePublished<VansGraphics::VansAnimationClipAsset>(record, repository,
				[&](VansGraphics::VansAnimationClipAsset& asset, std::string& loadError)
				{
					std::string bytes;
					return VansFileStorage::ReadAllBytes(record.sourcePath, bytes, loadError) &&
						VansGraphics::VansAnimationClipBinaryCodec::Decode(
							bytes, asset.clip, asset.skeleton, loadError);
				}, {}, published, error);
		else if (record.type == VansAssetType::AudioReverbPreset)
			success = EnsurePublished<VansAudioReverbPresetAsset>(record, repository,
				[&](VansAudioReverbPresetAsset& asset, std::string& loadError)
				{ return VansAudioReverbPresetAssetStorage::Load(record.sourcePath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::AudioBusSnapshot)
			success = EnsurePublished<VansAudioBusSnapshotAsset>(record, repository,
				[&](VansAudioBusSnapshotAsset& asset, std::string& loadError)
				{ return VansAudioBusSnapshotAssetStorage::Load(record.sourcePath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::AudioDuckingRules)
			success = EnsurePublished<VansAudioDuckingRulesAsset>(record, repository,
				[&](VansAudioDuckingRulesAsset& asset, std::string& loadError)
				{ return VansAudioDuckingRulesAssetStorage::Load(record.sourcePath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::ClothProfile)
			success = EnsurePublished<VansEngine::VansClothProfile>(record, repository,
				[&](VansEngine::VansClothProfile& asset, std::string& loadError)
				{ return VansEngine::VansClothProfileStorage::Load(record.sourcePath, asset, loadError); },
				{}, published, error);
		else if (record.type == VansAssetType::Particle)
			success = EnsurePublished<VansGraphics::VansParticleAsset>(record, repository,
				[&](VansGraphics::VansParticleAsset& asset, std::string& loadError)
				{ return VansGraphics::VansParticleAssetStorage::Load(record.sourcePath, asset, loadError); },
				{}, published, error);
		else if (IsUIJsonAsset(record.type))
		{
			VansAssetObjectSnapshotInfo info;
			const std::uint64_t contentHash = AssetObjectContentHash(record);
			if (repository.FindInfo(record.guid, info) && info.assetType == record.type &&
				info.contentHash == contentHash &&
				repository.ResolveLatest<VansRuntime::VansUIAssetDocument>(record.guid))
			{
				success = true;
			}
			else
			{
				auto document = std::make_shared<VansRuntime::VansUIAssetDocument>();
				std::vector<VansAssetGuid> dependencies;
				success = VansRuntime::VansUIDocumentLoader::Load(
					record.sourcePath, *document, error);
				if (success)
				{
					CollectGuidDependencies(document->root, indexedGuids, dependencies);
					success = repository.Publish<VansRuntime::VansUIAssetDocument>(
						record.guid, record.type, contentHash, std::move(document),
						std::move(dependencies), error).IsValid();
					published = success;
				}
			}
		}
		else if (record.type == VansAssetType::UIXaml)
			success = EnsurePublished<VansRuntime::VansUIXamlAsset>(record, repository,
				[&](VansRuntime::VansUIXamlAsset& asset, std::string& loadError)
				{ return VansFileStorage::ReadAllBytes(record.sourcePath, asset.bytes, loadError) && !asset.bytes.empty(); },
				{}, published, error);
		else if (VansGameplayAssetSchemaRegistry::IsGameplayAssetType(record.type))
			success = EnsurePublished<VansGameplayAssetMemoryObject>(record, repository,
				[&](VansGameplayAssetMemoryObject& asset, std::string& loadError)
				{
					asset.sourcePath = !record.authoringPath.empty()
						? record.authoringPath : record.sourcePath;
					if (record.artifactFormat == VansAssetArtifactFormat::Cooked ||
						record.artifactPath.extension() == ".gafcooked")
					{
						asset.hasCookedAsset = true;
						return VansGameplayAssetStorage::LoadCooked(
							record.artifactPath, asset.cookedAsset, loadError);
					}
					return VansGameplayAssetStorage::LoadSource(
						asset.sourcePath, asset.sourceDocument, loadError);
				}, {}, published, error);
		else
			handled = false;

		if (!handled) continue;
		if (!success)
		{
			if (error.empty()) error = "asset decoder rejected the source document";
			result.errors.push_back(
				"Asset '" + record.guid.ToString() + "' cannot publish: " + error);
		}
		else if (published)
			++result.published;
	}

	for (const VansAssetRecord& record : records)
	{
		if (record.state == VansAssetState::Missing || record.metaPath.empty()) continue;
		std::error_code existsError;
		if (!std::filesystem::is_regular_file(record.metaPath, existsError)) continue;

		VansAssetMeta meta;
		std::string error;
		if (!VansAssetMetaStorage::Load(record.metaPath, meta, error) || !meta.HasObjectSettings())
		{
			if (error.empty()) error = "asset metadata settings must be an object";
			result.errors.push_back(
				"Asset metadata '" + record.guid.ToString() + "' cannot publish: " + error);
			continue;
		}

		const std::uint64_t contentHash = AssetObjectContentHash(record);
		VansAssetObjectSnapshotInfo info;
		if (repository.FindInfo(record.guid, info) &&
			info.assetType == record.type && info.contentHash == contentHash)
		{
			if (!repository.PublishView<VansAssetMeta>(
				record.guid, record.type, contentHash,
				std::make_shared<const VansAssetMeta>(std::move(meta)), error).IsValid())
				result.errors.push_back(
					"Asset metadata '" + record.guid.ToString() + "' cannot publish: " + error);
			else
				++result.published;
			continue;
		}

		if (!Supports(record.type) &&
			repository.Publish<VansAssetMeta>(
				record.guid, record.type, contentHash,
				std::make_shared<const VansAssetMeta>(std::move(meta)), {}, error).IsValid())
		{
			++result.published;
			continue;
		}
		if (error.empty()) error = "source memory snapshot is unavailable";
		result.errors.push_back(
			"Asset metadata '" + record.guid.ToString() + "' cannot publish: " + error);
	}
	return result;
}

bool VansAssetObjectBootstrapper::Supports(VansAssetType type)
{
	if (VansGameplayAssetSchemaRegistry::IsGameplayAssetType(type))
		return true;
	switch (type)
	{
	case VansAssetType::Material:
	case VansAssetType::Shader:
	case VansAssetType::SkinProfile:
	case VansAssetType::PostProcessProfile:
	case VansAssetType::VegetationConfig:
	case VansAssetType::AIBehavior:
	case VansAssetType::NavigationMesh:
	case VansAssetType::RetargetProfile:
	case VansAssetType::RagdollProfile:
	case VansAssetType::AnimationRig:
	case VansAssetType::BoneMask:
	case VansAssetType::Timeline:
	case VansAssetType::AnimatorController:
	case VansAssetType::AnimationClip:
	case VansAssetType::AudioReverbPreset:
	case VansAssetType::AudioBusSnapshot:
	case VansAssetType::AudioDuckingRules:
	case VansAssetType::ClothProfile:
	case VansAssetType::Particle:
	case VansAssetType::UIScreen:
	case VansAssetType::UIComponent:
	case VansAssetType::UIThemeTokens:
	case VansAssetType::UILocalization:
	case VansAssetType::UIXaml:
		return true;
	default:
		return false;
	}
}
}
