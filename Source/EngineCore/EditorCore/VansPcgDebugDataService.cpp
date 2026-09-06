#include "VansPcgDebugDataService.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../ProjectSystem/VansProjectManager.h"
#include "../RenderCore/PcgCore/VansPcgSceneConfigAdapter.h"
#include "../SceneCore/VansSceneDocument.h"
#include "../SceneCore/Serialization/VansVegetationConfigCodec.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <utility>

namespace VansGraphics
{
namespace
{
std::string PathString(const std::filesystem::path& path)
{
	return path.lexically_normal().string();
}

const Vans::VansSerializedValue* ReadSerializedObjectField(
	const Vans::VansSerializedValue& object,
	const char* key)
{
	const Vans::VansSerializedValue* field = Vans::FindObjectField(object, key);
	return field != nullptr && field->kind == Vans::VansSerializedValue::Kind::Object ? field : nullptr;
}

std::string MaskRefLabel(const Vans::VansSerializedValue& owner)
{
	const Vans::VansSerializedValue* mask = Vans::FindObjectField(owner, "mask");
	if (mask == nullptr)
		return {};

	if (mask->kind == Vans::VansSerializedValue::Kind::String)
		return mask->stringValue;
	if (mask->kind != Vans::VansSerializedValue::Kind::Object)
		return "<unsupported>";

	std::string guid = Vans::ReadSerializedStringField(*mask, "guid");
	if (guid.empty()) guid = Vans::ReadSerializedStringField(*mask, "textureGuid");
	if (guid.empty()) guid = Vans::ReadSerializedStringField(*mask, "assetGuid");
	if (!guid.empty())
		return guid;

	if (const Vans::VansSerializedValue* texture = ReadSerializedObjectField(*mask, "texture"))
	{
		const std::string textureGuid = Vans::ReadSerializedStringField(*texture, "guid");
		if (!textureGuid.empty())
			return textureGuid;
	}

	if (const Vans::VansSerializedValue* asset = ReadSerializedObjectField(*mask, "asset"))
	{
		const std::string assetGuid = Vans::ReadSerializedStringField(*asset, "guid");
		if (!assetGuid.empty())
			return assetGuid;
	}

	std::string ref = Vans::ReadSerializedStringField(*mask, "ref");
	if (ref.empty()) ref = Vans::ReadSerializedStringField(*mask, "id");
	if (!ref.empty() &&
		Vans::FindObjectField(*mask, "path") == nullptr &&
		Vans::FindObjectField(*mask, "texture") == nullptr)
	{
		return ref;
	}

	std::string path = Vans::ReadSerializedStringField(*mask, "path");
	if (path.empty()) path = Vans::ReadSerializedStringField(*mask, "texture");
	return path.empty() ? "<inline>" : path;
}

void AddVegetationEntry(
	const Vans::VansSerializedValue& vegetationData,
	const std::filesystem::path& projectRoot,
	const std::string& sourcePath,
	const std::string& jsonPath,
	std::vector<PcgVegetationDebugEntry>& entries)
{
	Vans::VansAssetGuid guid;
	const std::string guidText =
		Vans::VansVegetationConfigCodec::ReadReferenceGuid(vegetationData);
	if (!Vans::VansAssetGuid::TryParse(guidText, guid))
		return;
	const std::shared_ptr<const Vans::VansVegetationConfigAsset> asset =
		Vans::VansProjectManager::Get().GetAssetObjectRepository()
			.ResolveLatest<Vans::VansVegetationConfigAsset>(guid);
	if (!asset)
		return;

	Vans::VansSceneVegetationNodeConfig vegetationConfig;
	std::string error;
	if (!Vans::VansVegetationConfigCodec::ResolveReference(
		vegetationData, *asset, vegetationConfig, error))
		return;
	Vans::VansSerializedValue config;
	if (!Vans::VansVegetationConfigCodec::Encode(vegetationConfig, config, error))
		return;

	PcgVegetationDebugEntry entry;
	entry.sourcePath = sourcePath;
	if (const std::optional<Vans::VansAssetRecord> record =
		Vans::VansProjectManager::Get().FindAssetRecord(guid))
		entry.sourcePath = PathString(record->sourcePath);
	entry.jsonPath = jsonPath;

	entry.label = vegetationConfig.name.value_or(std::string());
	if (entry.label.empty())
		entry.label = jsonPath.empty() ? std::string("Vegetation") : jsonPath;
	entry.instanceCount = vegetationConfig.instanceCount.value_or(0u);

	const Vans::VansSerializedValue* placementValue = ReadSerializedObjectField(config, "placement");
	if (vegetationConfig.placement)
	{
		entry.hasPlacement = true;
		const Vans::VansSceneVegetationPlacementConfig& placement = *vegetationConfig.placement;
		if (placement.boundsMin) entry.placementMinXZ = ToPcgVec2(*placement.boundsMin);
		if (placement.boundsMax) entry.placementMaxXZ = ToPcgVec2(*placement.boundsMax);
		if (placementValue != nullptr)
			entry.grassMaskRef = MaskRefLabel(*placementValue);
	}

	VansPcgSystem pcgSystem;
	pcgSystem.Configure(
		ToPcgMaskConfigs(vegetationConfig.pcgMasks),
		PathString(projectRoot),
		entry.placementMinXZ,
		entry.placementMaxXZ);
	const auto& masks = pcgSystem.GetMaskRegistry().GetMasks();
	entry.configuredMasks.reserve(masks.size());
	for (const auto& mask : masks)
		entry.configuredMasks.push_back(mask.second);
	std::sort(entry.configuredMasks.begin(), entry.configuredMasks.end(),
		[](const PcgPlacementMask& lhs, const PcgPlacementMask& rhs)
		{
			return lhs.name < rhs.name;
		});

	if (vegetationConfig.placement && vegetationConfig.placement->mask)
	{
		entry.grassMask = pcgSystem.ResolvePlacementMask(
			ToPcgMaskReference(*vegetationConfig.placement->mask),
			"grass",
			entry.placementMinXZ, entry.placementMaxXZ);
	}

	if (const Vans::VansSerializedValue* trees = ReadSerializedObjectField(config, "trees"))
	{
		if (const Vans::VansSerializedValue* randomValue = ReadSerializedObjectField(*trees, "randomInstances"))
		{
			entry.hasRandomTrees = true;
			entry.treeMaskRef = MaskRefLabel(*randomValue);
			if (vegetationConfig.trees && vegetationConfig.trees->randomInstances)
			{
				const Vans::VansSceneVegetationRandomTreeConfig& randomConfig =
					*vegetationConfig.trees->randomInstances;
				const glm::vec2 treeMinXZ = randomConfig.boundsMin
					? ToPcgVec2(*randomConfig.boundsMin)
					: entry.placementMinXZ;
				const glm::vec2 treeMaxXZ = randomConfig.boundsMax
					? ToPcgVec2(*randomConfig.boundsMax)
					: entry.placementMaxXZ;
				if (randomConfig.mask)
				{
					entry.treeMask = pcgSystem.ResolvePlacementMask(
						ToPcgMaskReference(*randomConfig.mask),
						"trees",
						treeMinXZ,
						treeMaxXZ);
				}
			}
		}
	}

	entries.push_back(std::move(entry));
}

void CollectVegetationFromSerializedValue(
	const Vans::VansSerializedValue& node,
	const std::filesystem::path& projectRoot,
	const std::string& sourcePath,
	const std::string& jsonPath,
	std::vector<PcgVegetationDebugEntry>& entries)
{
	if (node.kind == Vans::VansSerializedValue::Kind::Object)
	{
		if (const Vans::VansSerializedValue* vegetation = Vans::FindObjectField(node, "vegetation"))
		{
			const std::string vegetationPath = jsonPath.empty()
				? std::string("/vegetation")
				: jsonPath + "/vegetation";
			AddVegetationEntry(*vegetation, projectRoot, sourcePath, vegetationPath, entries);
		}

		for (const auto& [key, value] : node.objectFields)
		{
			const std::string childPath = jsonPath + "/" + key;
			CollectVegetationFromSerializedValue(value, projectRoot, sourcePath, childPath, entries);
		}
	}
	else if (node.kind == Vans::VansSerializedValue::Kind::Array)
	{
		for (size_t i = 0; i < node.arrayItems.size(); ++i)
		{
			CollectVegetationFromSerializedValue(node.arrayItems[i], projectRoot, sourcePath,
				jsonPath + "/" + std::to_string(i), entries);
		}
	}
}
}

std::vector<PcgVegetationDebugEntry> VansPcgDebugDataService::Collect(
	const std::string& projectRootPath,
	const Vans::VansSceneDocument* document)
{
	std::vector<PcgVegetationDebugEntry> entries;
	std::filesystem::path projectRoot(projectRootPath);

	if (document)
	{
		if (projectRoot.empty() && !document->SourcePath().empty())
			projectRoot = document->SourcePath().parent_path().parent_path();

		CollectVegetationFromSerializedValue(
			document->SerializedRootSnapshot(),
			projectRoot,
			PathString(document->SourcePath()),
			"",
			entries);
	}

	return entries;
}

} // namespace VansGraphics
