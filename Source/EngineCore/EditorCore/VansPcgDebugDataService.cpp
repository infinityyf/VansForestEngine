#include "VansPcgDebugDataService.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../AssetCore/Storage/VansJsonFileStorage.h"
#include "../RenderCore/PcgCore/VansPcgSceneConfigAdapter.h"
#include "../SceneCore/VansSceneDocument.h"
#include "../SceneCore/VansSceneEnvironmentNodeConfigReader.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <utility>

namespace VansGraphics
{
namespace
{
using Json = Vans::VansJsonFileStorage::OrderedJson;

std::string PathString(const std::filesystem::path& path)
{
	return path.lexically_normal().string();
}

std::filesystem::path ResolveProjectPath(const std::filesystem::path& projectRoot, const std::string& path)
{
	if (path.empty())
		return {};

	std::filesystem::path resolved(path);
	if (resolved.is_relative() && !projectRoot.empty())
		resolved = projectRoot / resolved;
	return resolved.lexically_normal();
}

Json ReadJsonFile(const std::filesystem::path& path)
{
	Json root;
	std::string error;
	if (!Vans::VansJsonFileStorage::Read(path, root, error))
		return Json();
	return root;
}

Vans::VansSerializedValue EmptyObject()
{
	return Vans::VansSerializedValue::Object({});
}

Vans::VansSerializedValue ReadSerializedJsonFile(const std::filesystem::path& path)
{
	Json root = ReadJsonFile(path);
	if (!root.is_object())
		return EmptyObject();
	return Vans::DecodeSerializedValueJson(root);
}

const Vans::VansSerializedValue* ReadSerializedObjectField(
	const Vans::VansSerializedValue& object,
	const char* key)
{
	const Vans::VansSerializedValue* field = Vans::FindObjectField(object, key);
	return field != nullptr && field->kind == Vans::VansSerializedValue::Kind::Object ? field : nullptr;
}

std::string ReadVegetationConfigPath(const Vans::VansSerializedValue& vegetationData)
{
	if (vegetationData.kind == Vans::VansSerializedValue::Kind::String)
		return vegetationData.stringValue;
	if (vegetationData.kind != Vans::VansSerializedValue::Kind::Object)
		return {};

	std::string path = Vans::ReadSerializedStringField(vegetationData, "config");
	if (path.empty()) path = Vans::ReadSerializedStringField(vegetationData, "configPath");
	if (path.empty()) path = Vans::ReadSerializedStringField(vegetationData, "path");
	return path;
}

Vans::VansSerializedValue LoadVegetationConfigFromReference(
	const Vans::VansSerializedValue& vegetationData,
	const std::filesystem::path& projectRoot,
	std::string& resolvedSource)
{
	const std::string configPath = ReadVegetationConfigPath(vegetationData);
	if (configPath.empty())
		return vegetationData;

	const std::filesystem::path resolved = ResolveProjectPath(projectRoot, configPath);
	resolvedSource = PathString(resolved);

	Vans::VansSerializedValue loaded = ReadSerializedJsonFile(resolved);
	if (loaded.kind != Vans::VansSerializedValue::Kind::Object)
		return EmptyObject();

	if (vegetationData.kind == Vans::VansSerializedValue::Kind::Object)
	{
		for (const auto& [key, value] : vegetationData.objectFields)
		{
			if (key == "config" || key == "configPath" || key == "path")
				continue;
			Vans::SetSerializedObjectField(loaded, key, value);
		}
	}
	return loaded;
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

bool LooksLikeVegetationConfig(const Vans::VansSerializedValue& root)
{
	return root.kind == Vans::VansSerializedValue::Kind::Object
		&& (Vans::FindObjectField(root, "instanceCount") != nullptr
			|| Vans::FindObjectField(root, "bladeHeight") != nullptr
			|| Vans::FindObjectField(root, "placement") != nullptr
			|| Vans::FindObjectField(root, "trees") != nullptr
			|| Vans::FindObjectField(root, "pcg") != nullptr
			|| Vans::FindObjectField(root, "masks") != nullptr);
}

void AddVegetationEntry(
	const Vans::VansSerializedValue& vegetationData,
	const std::filesystem::path& projectRoot,
	const std::string& sourcePath,
	const std::string& jsonPath,
	std::vector<PcgVegetationDebugEntry>& entries)
{
	std::string resolvedSource = sourcePath;
	Vans::VansSerializedValue config = LoadVegetationConfigFromReference(vegetationData, projectRoot, resolvedSource);
	if (!LooksLikeVegetationConfig(config))
		return;

	PcgVegetationDebugEntry entry;
	entry.sourcePath = resolvedSource;
	entry.jsonPath = jsonPath;
	const Vans::VansSceneVegetationNodeConfig vegetationConfig =
		Vans::VansSceneEnvironmentNodeConfigReader::ReadVegetation(config, PathString(projectRoot));

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

	if (!entries.empty() || projectRoot.empty())
		return entries;

	const std::filesystem::path vegetationRoot = projectRoot / "Assets" / "Vegetation";
	if (!std::filesystem::exists(vegetationRoot))
		return entries;

	for (const auto& file : std::filesystem::recursive_directory_iterator(vegetationRoot))
	{
		if (!file.is_regular_file() || file.path().extension() != ".json")
			continue;

		Vans::VansSerializedValue root = ReadSerializedJsonFile(file.path());
		if (!LooksLikeVegetationConfig(root))
			continue;

		AddVegetationEntry(root, projectRoot, PathString(file.path()),
			PathString(std::filesystem::relative(file.path(), projectRoot)), entries);
	}
	return entries;
}

} // namespace VansGraphics
