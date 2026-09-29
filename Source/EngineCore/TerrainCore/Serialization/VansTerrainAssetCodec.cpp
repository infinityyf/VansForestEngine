#include "VansTerrainAssetCodec.h"

#include "../../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <cmath>
#include <utility>

namespace Vans
{
namespace
{
const VansSerializedValue* ObjectField(const VansSerializedValue& value, const char* name)
{
	const VansSerializedValue* field = FindObjectField(value, name);
	return field && field->kind == VansSerializedValue::Kind::Object ? field : nullptr;
}

bool ReadGuidReference(
	const VansSerializedValue& value,
	const char* expectedType,
	VansAssetGuid& guid,
	std::string& error)
{
	SerializedObjectReferenceValue reference;
	if (!TryReadSerializedObjectReference(value, reference) ||
		reference.domain != "ProjectAsset" || reference.assetType != expectedType ||
		!VansAssetGuid::TryParse(reference.guid, guid))
	{
		error = std::string("Terrain requires a ProjectAsset reference of type '") + expectedType + "'";
		return false;
	}
	return true;
}

bool ReadNumber(const VansSerializedValue& object, const char* name, float& result)
{
	const VansSerializedValue* field = FindObjectField(object, name);
	if (!field || (field->kind != VansSerializedValue::Kind::Float && field->kind != VansSerializedValue::Kind::Int))
		return false;
	result = static_cast<float>(ReadSerializedNumber(*field));
	return std::isfinite(result);
}

bool ReadBool(const VansSerializedValue& object, const char* name, bool& result)
{
	const VansSerializedValue* field = FindObjectField(object, name);
	if (!field || field->kind != VansSerializedValue::Kind::Bool)
		return false;
	result = field->boolValue;
	return true;
}

VansSerializedValue Reference(VansAssetGuid guid, const char* assetType)
{
	return MakeSerializedProjectAssetObjectReference(guid.ToString(), assetType);
}
}

bool VansTerrainAssetCodec::DecodeDefinition(
	const VansSerializedValue& root,
	VansTerrainAsset& asset,
	std::string& error)
{
	error.clear();
	asset = {};
	if (root.kind != VansSerializedValue::Kind::Object)
	{
		error = "Terrain asset root must be an object";
		return false;
	}
	asset.sourceRoot = root;
	const VansSerializedValue* heightmap = FindObjectField(root, "heightmap");
	const VansSerializedValue* splatmaps = FindObjectField(root, "splatmaps");
	const VansSerializedValue* lod = ObjectField(root, "lod");
	const VansSerializedValue* tessellation = ObjectField(root, "tessellation");
	const VansSerializedValue* heightDetail = tessellation ? ObjectField(*tessellation, "heightDetail") : nullptr;
	const VansSerializedValue* wetSurface = ObjectField(root, "wetSurface");
	const VansSerializedValue* puddle = ObjectField(root, "puddle");
	const VansSerializedValue* layers = FindObjectField(root, "layers");
	if (!heightmap || !splatmaps || splatmaps->kind != VansSerializedValue::Kind::Array ||
		splatmaps->arrayItems.size() != 2 || !lod || !tessellation || !heightDetail || !wetSurface || !puddle ||
		!layers || layers->kind != VansSerializedValue::Kind::Array)
	{
		error = "Terrain asset is missing required heightmap, splatmaps, lod, tessellation.heightDetail, wetSurface, puddle, or layers fields";
		return false;
	}
	if (!ReadGuidReference(*heightmap, "texture", asset.heightmap, error) ||
		!ReadGuidReference(splatmaps->arrayItems[0], "texture", asset.splatmaps[0], error) ||
		!ReadGuidReference(splatmaps->arrayItems[1], "texture", asset.splatmaps[1], error))
		return false;

	auto& settings = asset.settings;
	if (!ReadNumber(root, "terrainSize", settings.terrainSize) ||
		!ReadNumber(root, "maxHeight", settings.maxHeight) ||
		!ReadNumber(root, "heightOffset", settings.heightOffset) ||
		!ReadNumber(*lod, "baseDistance", settings.lodBaseDistance) ||
		!ReadNumber(*lod, "rangeRatio", settings.lodRangeRatio) ||
		!ReadNumber(*lod, "morphStartRatio", settings.morphStartRatio) ||
		!ReadBool(*tessellation, "enabled", settings.tessellationEnabled) ||
		!ReadNumber(*tessellation, "distance", settings.tessellationDistance) ||
		!ReadNumber(*tessellation, "maxLevel", settings.maxTessellationLevel) ||
		!ReadNumber(*tessellation, "targetPixels", settings.tessellationTargetPixels) ||
		!ReadBool(*heightDetail, "enabled", settings.heightDetailEnabled) ||
		!ReadNumber(*heightDetail, "strength", settings.heightDetailStrength) ||
		!ReadNumber(*heightDetail, "fadeStart", settings.heightDetailFadeStart) ||
		!ReadNumber(*wetSurface, "albedoScale", settings.wetSurface.albedoScale) ||
		!ReadNumber(*wetSurface, "roughness", settings.wetSurface.roughness) ||
		!ReadNumber(*puddle, "scaleMeters", settings.puddle.scaleMeters) ||
		!ReadNumber(*puddle, "detailScale", settings.puddle.detailScale) ||
		!ReadNumber(*puddle, "threshold", settings.puddle.threshold) ||
		!ReadNumber(*puddle, "softness", settings.puddle.softness) ||
		!ReadNumber(*puddle, "strength", settings.puddle.strength) ||
		!ReadNumber(*puddle, "seed", settings.puddle.seed))
	{
		error = "Terrain asset contains missing or invalid numeric settings";
		return false;
	}

	asset.layers.reserve(layers->arrayItems.size());
	for (const VansSerializedValue& value : layers->arrayItems)
	{
		if (value.kind != VansSerializedValue::Kind::Object)
		{
			error = "Terrain layer must be an object";
			return false;
		}
		VansTerrainLayerAsset layer;
		layer.id = ReadSerializedStringField(value, "id");
		layer.name = ReadSerializedStringField(value, "name");
		const VansSerializedValue* albedo = FindObjectField(value, "albedo");
		const VansSerializedValue* normal = FindObjectField(value, "normal");
		const VansSerializedValue* roughness = FindObjectField(value, "roughness");
		if (!albedo || !normal || !roughness || !ReadNumber(value, "tiling", layer.tiling) ||
			!ReadGuidReference(*albedo, "texture", layer.albedo, error) ||
			!ReadGuidReference(*normal, "texture", layer.normal, error) ||
			!ReadGuidReference(*roughness, "texture", layer.roughness, error))
			return false;
		asset.layers.push_back(std::move(layer));
	}
	const auto diagnostics = ValidateTerrainAsset(asset, false);
	if (!diagnostics.empty())
	{
		error = diagnostics.front();
		asset = {};
		return false;
	}
	return true;
}

bool VansTerrainAssetCodec::EncodeDefinition(
	const VansTerrainAsset& asset,
	VansSerializedValue& root,
	std::string& error)
{
	error.clear();
	const auto diagnostics = ValidateTerrainAsset(asset, false);
	if (!diagnostics.empty())
	{
		error = diagnostics.front();
		return false;
	}
	std::vector<VansSerializedValue> layerValues;
	layerValues.reserve(asset.layers.size());
	for (const VansTerrainLayerAsset& layer : asset.layers)
	{
		layerValues.push_back(VansSerializedValue::Object({
			{ "id", VansSerializedValue::String(layer.id) },
			{ "name", VansSerializedValue::String(layer.name) },
			{ "albedo", Reference(layer.albedo, "texture") },
			{ "normal", Reference(layer.normal, "texture") },
			{ "roughness", Reference(layer.roughness, "texture") },
			{ "tiling", VansSerializedValue::Float(layer.tiling) }
		}));
	}
	const auto& settings = asset.settings;
	root = VansSerializedValue::Object({
		{ "heightmap", Reference(asset.heightmap, "texture") },
		{ "splatmaps", VansSerializedValue::Array({
			Reference(asset.splatmaps[0], "texture"), Reference(asset.splatmaps[1], "texture") }) },
		{ "terrainSize", VansSerializedValue::Float(settings.terrainSize) },
		{ "maxHeight", VansSerializedValue::Float(settings.maxHeight) },
		{ "heightOffset", VansSerializedValue::Float(settings.heightOffset) },
		{ "lod", VansSerializedValue::Object({
			{ "baseDistance", VansSerializedValue::Float(settings.lodBaseDistance) },
			{ "rangeRatio", VansSerializedValue::Float(settings.lodRangeRatio) },
			{ "morphStartRatio", VansSerializedValue::Float(settings.morphStartRatio) }
		}) },
		{ "tessellation", VansSerializedValue::Object({
			{ "enabled", VansSerializedValue::Bool(settings.tessellationEnabled) },
			{ "distance", VansSerializedValue::Float(settings.tessellationDistance) },
			{ "maxLevel", VansSerializedValue::Float(settings.maxTessellationLevel) },
			{ "targetPixels", VansSerializedValue::Float(settings.tessellationTargetPixels) },
			{ "heightDetail", VansSerializedValue::Object({
				{ "enabled", VansSerializedValue::Bool(settings.heightDetailEnabled) },
				{ "strength", VansSerializedValue::Float(settings.heightDetailStrength) },
				{ "fadeStart", VansSerializedValue::Float(settings.heightDetailFadeStart) }
			}) }
		}) },
		{ "wetSurface", VansSerializedValue::Object({
			{ "albedoScale", VansSerializedValue::Float(settings.wetSurface.albedoScale) },
			{ "roughness", VansSerializedValue::Float(settings.wetSurface.roughness) }
		}) },
		{ "puddle", VansSerializedValue::Object({
			{ "scaleMeters", VansSerializedValue::Float(settings.puddle.scaleMeters) },
			{ "detailScale", VansSerializedValue::Float(settings.puddle.detailScale) },
			{ "threshold", VansSerializedValue::Float(settings.puddle.threshold) },
			{ "softness", VansSerializedValue::Float(settings.puddle.softness) },
			{ "strength", VansSerializedValue::Float(settings.puddle.strength) },
			{ "seed", VansSerializedValue::Float(settings.puddle.seed) }
		}) },
		{ "layers", VansSerializedValue::Array(std::move(layerValues)) }
	});
	return true;
}
}
