#include "VansPcgRecipeCodec.h"
#include "VansPcgValueCodec.h"

namespace Vans
{
namespace
{
using Value = VansSerializedValue;
using Fields = std::vector<std::pair<std::string, Value>>;
const std::pair<const char*, float VansPcgPlacementSettings::*> PlacementFloats[] = {
	{ "density", &VansPcgPlacementSettings::density }, { "positionJitter", &VansPcgPlacementSettings::positionJitter },
	{ "minimumSpacing", &VansPcgPlacementSettings::minimumSpacing }, { "yawMinDegrees", &VansPcgPlacementSettings::yawMinDegrees },
	{ "yawMaxDegrees", &VansPcgPlacementSettings::yawMaxDegrees }, { "normalAlignment", &VansPcgPlacementSettings::normalAlignment },
	{ "maximumTiltDegrees", &VansPcgPlacementSettings::maximumTiltDegrees }, { "rootOffset", &VansPcgPlacementSettings::rootOffset },
	{ "maskThreshold", &VansPcgPlacementSettings::maskThreshold }, { "maskMultiplier", &VansPcgPlacementSettings::maskMultiplier }
};

template <typename Instance> void ReadTransform(PcgValue::Reader& reader, Instance& instance)
{
	reader.Vector("position", instance.position);
	reader.Vector("rotation", instance.rotation);
	reader.Vector("scale", instance.scale);
}

template <typename Instance> void WriteTransform(Fields& fields, const Instance& instance)
{
	fields.emplace_back("position", PcgValue::Vector(instance.position));
	fields.emplace_back("rotation", PcgValue::Vector(instance.rotation));
	fields.emplace_back("scale", PcgValue::Vector(instance.scale));
}

void ReadInstances(PcgValue::Reader& reader, const char* field, const std::string& path,
	std::vector<VansPcgAuthoredInstance>& instances, std::string& error)
{
	const auto* values = reader.Array(field);
	if (!values) return;
	for (std::size_t i = 0; i < values->size(); ++i)
	{
		VansPcgAuthoredInstance instance;
		PcgValue::Reader item(&(*values)[i], path + "." + field + "[" + std::to_string(i) + "]", error);
		item.String("id", instance.id);
		item.String("variant", instance.variant);
		ReadTransform(item, instance);
		item.Finish();
		instances.push_back(std::move(instance));
	}
}

Value WriteInstances(const std::vector<VansPcgAuthoredInstance>& instances)
{
	std::vector<Value> result;
	for (const auto& instance : instances)
	{
		Fields fields{ { "id", Value::String(instance.id) }, { "variant", Value::String(instance.variant) } };
		WriteTransform(fields, instance);
		result.push_back(Value::Object(std::move(fields)));
	}
	return Value::Array(std::move(result));
}
}

bool VansPcgRecipeCodec::Decode(const Value& root, VansPcgRecipeAsset& recipe, std::string& error)
{
	error.clear();
	VansPcgRecipeAsset decoded;
	PcgValue::Reader reader(&root, "pcg", error);
	reader.String("name", decoded.name);
	const auto* regions = reader.Array("regions");
	if (regions) for (std::size_t r = 0; r < regions->size(); ++r)
	{
		VansPcgRegion region;
		const auto regionPath = "pcg.regions[" + std::to_string(r) + "]";
		PcgValue::Reader item(&(*regions)[r], regionPath, error);
		item.String("id", region.id);
		item.String("name", region.name);
		item.Bool("enabled", region.enabled);
		item.IntegerField("seed", region.seed);
		item.Float("cellSize", region.cellSize);
		auto bounds = item.Object("bounds");
		bounds.Vector("min", region.bounds.min);
		bounds.Vector("max", region.bounds.max);
		bounds.Finish();
		auto surface = item.Object("surface");
		surface.EnumField("kind", region.surface.kind, { { "unassigned", VansPcgSurfaceKind::Unassigned },
			{ "plane", VansPcgSurfaceKind::Plane }, { "terrain", VansPcgSurfaceKind::Terrain } });
		surface.Float("planeHeight", region.surface.planeHeight);
		surface.Reference("terrain", "terrain", region.surface.terrain);
		surface.Finish();
		const auto* layers = item.Array("layers");
		if (layers) for (std::size_t l = 0; l < layers->size(); ++l)
		{
			VansPcgLayer layer;
			const auto layerPath = regionPath + ".layers[" + std::to_string(l) + "]";
			PcgValue::Reader layerReader(&(*layers)[l], layerPath, error);
			layerReader.String("id", layer.id);
			layerReader.String("name", layer.name);
			layerReader.EnumField("category", layer.category, { { "grass", VansPlantCategory::Grass }, { "tree", VansPlantCategory::Tree } });
			layerReader.Bool("enabled", layer.enabled);
			layerReader.Bool("locked", layer.locked);
			layerReader.Reference("plant", "plantType", layer.plant);
			layerReader.Reference("densityMask", "pcgMask", layer.densityMask);
			layerReader.Reference("exclusionMask", "pcgMask", layer.exclusionMask);
			layerReader.IntegerField("seed", layer.seed);
			layerReader.EnumField("source", layer.source, { { "density", VansPcgSourceMode::Density },
				{ "count", VansPcgSourceMode::Count }, { "fixed", VansPcgSourceMode::Fixed } });
			if (layer.category == VansPlantCategory::Tree) layerReader.IntegerField("targetCount", layer.targetCount);
			auto budget = layerReader.Object("budget");
			budget.IntegerField("maxCandidates", layer.budget.maxCandidates);
			budget.IntegerField("maxInstances", layer.budget.maxInstances);
			budget.Finish();
			auto placement = layerReader.Object("placement");
			for (const auto& field : PlacementFloats) placement.Float(field.first, layer.placement.*field.second);
			placement.Vector("scaleMin", layer.placement.scaleMin);
			placement.Vector("scaleMax", layer.placement.scaleMax);
			placement.Bool("uniformScale", layer.placement.uniformScale);
			placement.Bool("invertMask", layer.placement.invertMask);
			placement.Finish();
			ReadInstances(layerReader, "fixedInstances", layerPath, layer.fixedInstances, error);
			ReadInstances(layerReader, "addedInstances", layerPath, layer.addedInstances, error);
			const auto* overrides = layerReader.Array("overrides");
			if (overrides) for (std::size_t o = 0; o < overrides->size(); ++o)
			{
				VansPcgInstanceOverride edit;
				PcgValue::Reader editReader(&(*overrides)[o], layerPath + ".overrides[" + std::to_string(o) + "]", error);
				editReader.String("target", edit.target);
				editReader.EnumField("kind", edit.kind, { { "remove", VansPcgOverrideKind::Remove }, { "transform", VansPcgOverrideKind::Transform }, { "lock", VansPcgOverrideKind::Lock } });
				if (edit.kind==VansPcgOverrideKind::Lock) editReader.String("variant",edit.variant);
				ReadTransform(editReader, edit);
				editReader.Finish();
				layer.overrides.push_back(std::move(edit));
			}
			layerReader.Finish();
			region.layers.push_back(std::move(layer));
		}
		item.Finish();
		decoded.regions.push_back(std::move(region));
	}
	if (!reader.Finish()) return false;
	const auto diagnostics = ValidatePcgRecipe(decoded, false);
	if (!diagnostics.empty()) { error = diagnostics.front(); return false; }
	recipe = std::move(decoded);
	return true;
}

bool VansPcgRecipeCodec::Encode(const VansPcgRecipeAsset& recipe, Value& root, std::string& error)
{
	error.clear();
	const auto diagnostics = ValidatePcgRecipe(recipe, false);
	if (!diagnostics.empty()) { error = diagnostics.front(); return false; }
	std::vector<Value> regions;
	for (const auto& region : recipe.regions)
	{
		std::vector<Value> layers;
		for (const auto& layer : region.layers)
		{
			Fields placement;
			for (const auto& field : PlacementFloats) placement.emplace_back(field.first, Value::Float(layer.placement.*field.second));
			placement.emplace_back("scaleMin", PcgValue::Vector(layer.placement.scaleMin));
			placement.emplace_back("scaleMax", PcgValue::Vector(layer.placement.scaleMax));
			placement.emplace_back("uniformScale", Value::Bool(layer.placement.uniformScale));
			placement.emplace_back("invertMask", Value::Bool(layer.placement.invertMask));
			std::vector<Value> overrides;
			for (const auto& edit : layer.overrides)
			{
				Fields fields{ { "target", Value::String(edit.target) },
					{ "kind", Value::String(edit.kind == VansPcgOverrideKind::Remove ? "remove" : edit.kind == VansPcgOverrideKind::Lock ? "lock" : "transform") } };
				if (edit.kind==VansPcgOverrideKind::Lock) fields.emplace_back("variant",Value::String(edit.variant));
				WriteTransform(fields, edit);
				overrides.push_back(Value::Object(std::move(fields)));
			}
			Fields layerFields{
				{ "id", Value::String(layer.id) }, { "name", Value::String(layer.name) },
				{ "category", Value::String(layer.category == VansPlantCategory::Grass ? "grass" : "tree") },
				{ "enabled", Value::Bool(layer.enabled) }, { "locked", Value::Bool(layer.locked) },
				{ "plant", PcgValue::Reference(layer.plant, "plantType") }, { "densityMask", PcgValue::Reference(layer.densityMask, "pcgMask") },
				{ "exclusionMask", PcgValue::Reference(layer.exclusionMask, "pcgMask") }, { "seed", Value::Int(layer.seed) },
				{ "source", Value::String(layer.source == VansPcgSourceMode::Density ? "density" : layer.source == VansPcgSourceMode::Count ? "count" : "fixed") },
				{ "placement", Value::Object(std::move(placement)) },
				{ "budget", Value::Object({ { "maxCandidates", Value::Int(layer.budget.maxCandidates) }, { "maxInstances", Value::Int(layer.budget.maxInstances) } }) },
				{ "fixedInstances", WriteInstances(layer.fixedInstances) }, { "addedInstances", WriteInstances(layer.addedInstances) },
				{ "overrides", Value::Array(std::move(overrides)) }
			};
			if (layer.category == VansPlantCategory::Tree) layerFields.emplace_back("targetCount", Value::Int(layer.targetCount));
			layers.push_back(Value::Object(std::move(layerFields)));
		}
		regions.push_back(Value::Object({
			{ "id", Value::String(region.id) }, { "name", Value::String(region.name) }, { "enabled", Value::Bool(region.enabled) },
			{ "seed", Value::Int(region.seed) }, { "cellSize", Value::Float(region.cellSize) },
			{ "bounds", Value::Object({ { "min", PcgValue::Vector(region.bounds.min) }, { "max", PcgValue::Vector(region.bounds.max) } }) },
			{ "surface", Value::Object({ { "kind", Value::String(region.surface.kind == VansPcgSurfaceKind::Plane ? "plane" :
				region.surface.kind == VansPcgSurfaceKind::Terrain ? "terrain" : "unassigned") },
				{ "planeHeight", Value::Float(region.surface.planeHeight) }, { "terrain", PcgValue::Reference(region.surface.terrain, "terrain") } }) },
			{ "layers", Value::Array(std::move(layers)) }
		}));
	}
	root = Value::Object({ { "name", Value::String(recipe.name) }, { "regions", Value::Array(std::move(regions)) } });
	return true;
}
}
