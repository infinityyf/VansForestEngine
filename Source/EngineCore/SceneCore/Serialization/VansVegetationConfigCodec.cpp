#include "VansVegetationConfigCodec.h"

#include "../VansSceneEnvironmentNodeConfigReader.h"
#include "../../AssetCore/Serialization/VansSerializedValue.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <unordered_set>
#include <utility>

namespace Vans
{
namespace
{
using Fields = std::vector<std::pair<std::string, VansSerializedValue>>;

template <typename Value, typename Convert>
void AddOptional(Fields& fields, const char* name, const std::optional<Value>& value, Convert convert)
{
	if (value)
		fields.emplace_back(name, convert(*value));
}

VansSerializedValue Float2Value(const VansSceneFloat2& value)
{
	return VansSerializedValue::Array({
		VansSerializedValue::Float(value[0]),
		VansSerializedValue::Float(value[1])
	});
}

VansSerializedValue Float3Value(const VansSceneFloat3& value)
{
	return VansSerializedValue::Array({
		VansSerializedValue::Float(value[0]),
		VansSerializedValue::Float(value[1]),
		VansSerializedValue::Float(value[2])
	});
}

void AddString(Fields& fields, const char* name, const std::optional<std::string>& value)
{
	AddOptional(fields, name, value,
		[](const std::string& item) { return VansSerializedValue::String(item); });
}

void AddFloat(Fields& fields, const char* name, const std::optional<float>& value)
{
	AddOptional(fields, name, value,
		[](float item) { return VansSerializedValue::Float(item); });
}

void AddInt(Fields& fields, const char* name, const std::optional<std::int32_t>& value)
{
	AddOptional(fields, name, value,
		[](std::int32_t item) { return VansSerializedValue::Int(item); });
}

void AddUInt(Fields& fields, const char* name, const std::optional<std::uint32_t>& value)
{
	AddOptional(fields, name, value,
		[](std::uint32_t item) { return VansSerializedValue::Int(item); });
}

void AddBool(Fields& fields, const char* name, const std::optional<bool>& value)
{
	AddOptional(fields, name, value,
		[](bool item) { return VansSerializedValue::Bool(item); });
}

void AddFloat2(Fields& fields, const char* name, const std::optional<VansSceneFloat2>& value)
{
	AddOptional(fields, name, value, Float2Value);
}

void AddFloat3(Fields& fields, const char* name, const std::optional<VansSceneFloat3>& value)
{
	AddOptional(fields, name, value, Float3Value);
}

VansSerializedValue EncodeMask(const VansScenePcgMaskConfig& mask, bool includeId)
{
	Fields fields;
	if (includeId) AddString(fields, "id", mask.id);
	AddString(fields, "path", mask.path);
	if (mask.assetGuid)
	{
		fields.emplace_back("texture", VansSerializedValue::Object({
			{ "guid", VansSerializedValue::String(*mask.assetGuid) }
		}));
	}
	else if (mask.textureValue)
	{
		fields.emplace_back("texture", VansSerializedValue::String(*mask.textureValue));
	}
	AddString(fields, "channel", mask.channel);
	AddFloat2(fields, "boundsMin", mask.boundsMin);
	AddFloat2(fields, "boundsMax", mask.boundsMax);
	AddFloat2(fields, "worldMin", mask.worldMin);
	AddFloat2(fields, "worldMax", mask.worldMax);
	AddFloat(fields, "threshold", mask.threshold);
	AddFloat(fields, "densityScale", mask.densityScale);
	AddBool(fields, "invert", mask.invert);
	return VansSerializedValue::Object(std::move(fields));
}

VansSerializedValue EncodeMaskReference(const VansScenePcgMaskReferenceConfig& mask)
{
	if (mask.ref)
		return VansSerializedValue::String(*mask.ref);
	return mask.inlineMask
		? EncodeMask(*mask.inlineMask, true)
		: VansSerializedValue::Object({});
}

VansSerializedValue EncodePlacement(const VansSceneVegetationPlacementConfig& placement)
{
	Fields fields;
	AddFloat2(fields, "boundsMin", placement.boundsMin);
	AddFloat2(fields, "boundsMax", placement.boundsMax);
	AddFloat(fields, "grassScaleMin", placement.grassScaleMin);
	AddFloat(fields, "grassScaleMax", placement.grassScaleMax);
	if (placement.mask)
		fields.emplace_back("mask", EncodeMaskReference(*placement.mask));
	return VansSerializedValue::Object(std::move(fields));
}

const char* TreePartTypeName(VansSceneVegetationTreePartType type)
{
	switch (type)
	{
	case VansSceneVegetationTreePartType::Trunk: return "trunk";
	case VansSceneVegetationTreePartType::Leaves: return "leaves";
	case VansSceneVegetationTreePartType::Custom: return "custom";
	}
	return "custom";
}

VansSerializedValue EncodeTrees(const VansSceneVegetationTreesConfig& trees)
{
	Fields fields;
	AddBool(fields, "enabled", trees.enabled);
	AddFloat(fields, "cullDistance", trees.cullDistance);
	AddBool(fields, "cullEnabled", trees.cullEnabled);
	AddBool(fields, "hizEnabled", trees.hizEnabled);

	std::vector<VansSerializedValue> species;
	species.reserve(trees.species.size());
	for (const VansSceneVegetationTreeSpeciesConfig& item : trees.species)
	{
		Fields speciesFields{ { "name", VansSerializedValue::String(item.name) } };
		AddFloat(speciesFields, "boundsRadius", item.boundsRadius);
		std::vector<VansSerializedValue> parts;
		parts.reserve(item.parts.size());
		for (const VansSceneVegetationTreePartConfig& part : item.parts)
		{
			Fields partFields{
				{ "type", VansSerializedValue::String(TreePartTypeName(part.type)) },
				{ "mesh", VansSerializedValue::String(part.mesh) },
				{ "material", VansSerializedValue::String(part.material) }
			};
			AddInt(partFields, "submeshIndex", part.submeshIndex);
			parts.push_back(VansSerializedValue::Object(std::move(partFields)));
		}
		speciesFields.emplace_back("parts", VansSerializedValue::Array(std::move(parts)));
		species.push_back(VansSerializedValue::Object(std::move(speciesFields)));
	}
	if (!species.empty())
		fields.emplace_back("species", VansSerializedValue::Array(std::move(species)));

	std::vector<VansSerializedValue> instances;
	instances.reserve(trees.instances.size());
	for (const VansSceneVegetationTreeInstanceConfig& instance : trees.instances)
	{
		Fields instanceFields;
		AddString(instanceFields, "species", instance.species);
		AddFloat3(instanceFields, "position", instance.position);
		AddFloat(instanceFields, "yaw", instance.yaw);
		AddFloat(instanceFields, "scale", instance.scale);
		AddInt(instanceFields, "submeshIndex", instance.submeshIndex);
		instances.push_back(VansSerializedValue::Object(std::move(instanceFields)));
	}
	if (!instances.empty())
		fields.emplace_back("instances", VansSerializedValue::Array(std::move(instances)));

	if (trees.randomInstances)
	{
		const VansSceneVegetationRandomTreeConfig& random = *trees.randomInstances;
		Fields randomFields;
		AddUInt(randomFields, "count", random.count);
		AddUInt(randomFields, "seed", random.seed);
		AddFloat(randomFields, "scaleMin", random.scaleMin);
		AddFloat(randomFields, "scaleMax", random.scaleMax);
		AddFloat2(randomFields, "boundsMin", random.boundsMin);
		AddFloat2(randomFields, "boundsMax", random.boundsMax);
		AddString(randomFields, "species", random.species);
		AddInt(randomFields, "submeshIndex", random.submeshIndex);
		if (random.mask)
			randomFields.emplace_back("mask", EncodeMaskReference(*random.mask));
		fields.emplace_back("randomInstances", VansSerializedValue::Object(std::move(randomFields)));
	}
	AddUInt(fields, "count", trees.fallbackCount);
	AddFloat(fields, "placementRadius", trees.placementRadius);
	AddFloat3(fields, "center", trees.center);
	return VansSerializedValue::Object(std::move(fields));
}

bool IsStrictlyIncreasingBounds(const VansSceneFloat2& minimum, const VansSceneFloat2& maximum)
{
	return minimum[0] < maximum[0] && minimum[1] < maximum[1];
}
}

bool VansVegetationConfigCodec::Decode(
	const VansSerializedValue& root,
	VansVegetationConfigAsset& asset,
	std::string& error)
{
	error.clear();
	asset = {};
	if (root.kind != VansSerializedValue::Kind::Object)
	{
		error = "Vegetation configuration root must be an object";
		return false;
	}
	asset.sourceRoot = root;
	asset.config = VansSceneEnvironmentNodeConfigReader::ReadVegetation(root);
	const std::vector<std::string> diagnostics = Validate(asset.config);
	if (!diagnostics.empty())
	{
		error = diagnostics.front();
		asset = {};
		return false;
	}
	return true;
}

bool VansVegetationConfigCodec::Encode(
	const VansSceneVegetationNodeConfig& config,
	VansSerializedValue& root,
	std::string& error)
{
	error.clear();
	const std::vector<std::string> diagnostics = Validate(config);
	if (!diagnostics.empty())
	{
		error = diagnostics.front();
		return false;
	}

	Fields fields;
	AddUInt(fields, "instanceCount", config.instanceCount);
	AddUInt(fields, "boneCount", config.boneCount);
	AddFloat(fields, "bladeHeight", config.bladeHeight);
	AddFloat(fields, "windDirX", config.windDirX);
	AddFloat(fields, "windDirZ", config.windDirZ);
	AddFloat(fields, "leanDeviation", config.leanDeviation);
	AddString(fields, "material", config.material);
	AddString(fields, "name", config.name);
	AddUInt(fields, "subBladeCount", config.subBladeCount);
	AddFloat(fields, "subBladeScatterRadiusMin", config.subBladeScatterRadiusMin);
	AddFloat(fields, "subBladeScatterRadiusMax", config.subBladeScatterRadiusMax);
	AddFloat(fields, "windStrength", config.windStrength);
	AddFloat(fields, "windFrequency", config.windFrequency);
	AddFloat(fields, "windSpeed", config.windSpeed);
	AddFloat(fields, "windBendMult", config.windBendMult);
	AddFloat(fields, "stiffness", config.stiffness);
	AddFloat(fields, "damping", config.damping);
	AddFloat(fields, "softness", config.softness);
	AddFloat(fields, "lodFullDist", config.lodFullDist);
	AddFloat(fields, "lodFadeDist", config.lodFadeDist);
	AddFloat(fields, "terrainMaxHeight", config.terrainMaxHeight);
	AddFloat(fields, "terrainHeightOffset", config.terrainHeightOffset);
	AddFloat(fields, "hizSampleBias", config.hizSampleBias);
	AddFloat(fields, "grassScaleMin", config.grassScaleMin);
	AddFloat(fields, "grassScaleMax", config.grassScaleMax);
	if (!config.pcgMasks.empty())
	{
		Fields masks;
		for (const VansScenePcgMaskConfig& mask : config.pcgMasks)
			masks.emplace_back(*mask.id, EncodeMask(mask, false));
		fields.emplace_back("pcg", VansSerializedValue::Object({
			{ "masks", VansSerializedValue::Object(std::move(masks)) }
		}));
	}
	if (config.placement)
		fields.emplace_back("placement", EncodePlacement(*config.placement));
	if (config.trees)
		fields.emplace_back("trees", EncodeTrees(*config.trees));
	if (!config.renderConfigs.empty())
	{
		std::vector<VansSerializedValue> renderConfigs;
		renderConfigs.reserve(config.renderConfigs.size());
		for (const VansSceneVegetationRenderConfig& render : config.renderConfigs)
		{
			Fields renderFields;
			AddString(renderFields, "mesh", render.mesh);
			AddString(renderFields, "material", render.material);
			AddFloat(renderFields, "percent", render.percent);
			renderConfigs.push_back(VansSerializedValue::Object(std::move(renderFields)));
		}
		fields.emplace_back("renderConfigs", VansSerializedValue::Array(std::move(renderConfigs)));
	}
	root = VansSerializedValue::Object(std::move(fields));
	return true;
}

std::vector<std::string> VansVegetationConfigCodec::Validate(
	const VansSceneVegetationNodeConfig& config)
{
	std::vector<std::string> diagnostics;
	if (!config.valid)
		diagnostics.push_back("Vegetation configuration is invalid");
	if (config.instanceCount && *config.instanceCount == 0)
		diagnostics.push_back("Vegetation instanceCount must be greater than zero");
	if (config.boneCount && *config.boneCount == 0)
		diagnostics.push_back("Vegetation boneCount must be greater than zero");
	if (config.placement && config.placement->boundsMin && config.placement->boundsMax &&
		!IsStrictlyIncreasingBounds(*config.placement->boundsMin, *config.placement->boundsMax))
		diagnostics.push_back("Vegetation placement bounds must have positive area");
	std::unordered_set<std::string> maskIds;
	for (const VansScenePcgMaskConfig& mask : config.pcgMasks)
	{
		if (!mask.id || mask.id->empty())
			diagnostics.push_back("Vegetation PCG masks require a stable id");
		else if (!maskIds.insert(*mask.id).second)
			diagnostics.push_back("Vegetation PCG mask id is duplicated: " + *mask.id);
	}
	if (config.trees)
	{
		std::unordered_set<std::string> speciesNames;
		for (const VansSceneVegetationTreeSpeciesConfig& species : config.trees->species)
		{
			if (species.name.empty())
				diagnostics.push_back("Vegetation tree species requires a name");
			else if (!speciesNames.insert(species.name).second)
				diagnostics.push_back("Vegetation tree species is duplicated: " + species.name);
			for (const VansSceneVegetationTreePartConfig& part : species.parts)
				if (part.mesh.empty() || part.material.empty())
					diagnostics.push_back("Vegetation tree parts require mesh and material GUIDs");
		}
		for (const VansSceneVegetationTreeInstanceConfig& instance : config.trees->instances)
			if (instance.species && speciesNames.find(*instance.species) == speciesNames.end())
				diagnostics.push_back("Vegetation tree instance refers to an unknown species: " + *instance.species);
	}
	return diagnostics;
}

std::string VansVegetationConfigCodec::ReadReferenceGuid(const VansSerializedValue& reference)
{
	if (reference.kind != VansSerializedValue::Kind::Object)
		return {};
	const VansSerializedValue* asset = FindObjectField(reference, "asset");
	return asset && asset->kind == VansSerializedValue::Kind::Object
		? ReadSerializedStringField(*asset, "guid")
		: std::string{};
}

bool VansVegetationConfigCodec::ResolveReference(
	const VansSerializedValue& reference,
	const VansVegetationConfigAsset& asset,
	VansSceneVegetationNodeConfig& config,
	std::string& error)
{
	error.clear();
	if (reference.kind != VansSerializedValue::Kind::Object ||
		ReadReferenceGuid(reference).empty())
	{
		error = "Vegetation reference must contain asset.guid";
		return false;
	}
	VansSerializedValue merged = asset.sourceRoot;
	for (const auto& [name, value] : reference.objectFields)
	{
		if (name != "asset")
			SetSerializedObjectField(merged, name, value);
	}
	VansVegetationConfigAsset resolved;
	if (!Decode(merged, resolved, error))
		return false;
	config = std::move(resolved.config);
	return true;
}
}
