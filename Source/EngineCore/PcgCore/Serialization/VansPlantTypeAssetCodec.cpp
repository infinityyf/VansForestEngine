#include "VansPlantTypeAssetCodec.h"
#include "VansPcgValueCodec.h"

namespace Vans
{
namespace
{
using Value = VansSerializedValue;
using Fields = std::vector<std::pair<std::string, Value>>;
const std::pair<const char*, float VansPlantGrassSettings::*> GrassFields[] = {
	{ "bladeHeight", &VansPlantGrassSettings::bladeHeight }, { "leanDeviation", &VansPlantGrassSettings::leanDeviation },
	{ "restTipBendDegrees", &VansPlantGrassSettings::restTipBendDegrees }, { "restRootBendDegrees", &VansPlantGrassSettings::restRootBendDegrees },
	{ "scatterRadiusMin", &VansPlantGrassSettings::scatterRadiusMin }, { "scatterRadiusMax", &VansPlantGrassSettings::scatterRadiusMax },
	{ "windStrength", &VansPlantGrassSettings::windStrength }, { "windFrequency", &VansPlantGrassSettings::windFrequency },
	{ "windSpeed", &VansPlantGrassSettings::windSpeed }, { "windBendMultiplier", &VansPlantGrassSettings::windBendMultiplier },
	{ "stiffness", &VansPlantGrassSettings::stiffness }, { "damping", &VansPlantGrassSettings::damping },
	{ "softness", &VansPlantGrassSettings::softness }, { "simulationFullDistance", &VansPlantGrassSettings::simulationFullDistance },
	{ "simulationFadeDistance", &VansPlantGrassSettings::simulationFadeDistance },
	{ "subBladeLodMidDistance", &VansPlantGrassSettings::subBladeLodMidDistance },
	{ "subBladeLodFarDistance", &VansPlantGrassSettings::subBladeLodFarDistance }
};
}

bool VansPlantTypeAssetCodec::Decode(const Value& root, VansPlantTypeAsset& asset, std::string& error)
{
	error.clear();
	VansPlantTypeAsset decoded;
	PcgValue::Reader reader(&root, "plant", error);
	reader.String("name", decoded.name);
	reader.EnumField("category", decoded.category, { { "grass", VansPlantCategory::Grass }, { "tree", VansPlantCategory::Tree } });
	auto render = reader.Object("render");
	render.Bool("cullingEnabled", decoded.render.cullingEnabled);
	render.Bool("hizEnabled", decoded.render.hizEnabled);
	render.Float("cullDistance", decoded.render.cullDistance);
	render.Float("hizBias", decoded.render.hizBias);
	render.Bool("castShadows", decoded.render.castShadows);
	render.Finish();
	if (decoded.category == VansPlantCategory::Grass)
	{
		auto grass = reader.Object("grass");
		grass.IntegerField("boneCount", decoded.grass.boneCount);
		grass.IntegerField("subBladeCount", decoded.grass.subBladeCount);
		grass.IntegerField("scatterSeed", decoded.grass.scatterSeed);
		grass.Vector("windDirection", decoded.grass.windDirection);
		for (const auto& field : GrassFields) grass.Float(field.first, decoded.grass.*field.second);
		grass.Finish();
	}
	const auto* variants = reader.Array("variants");
	if (variants) for (std::size_t index = 0; index < variants->size(); ++index)
	{
		VansPlantVariant variant;
		const std::string path = "plant.variants[" + std::to_string(index) + "]";
		PcgValue::Reader item(&(*variants)[index], path, error);
		item.String("id", variant.id);
		item.String("name", variant.name);
		item.EnumField("geometry", variant.geometry, { { "unassigned", VansPlantGeometry::Unassigned },
			{ "mesh", VansPlantGeometry::Mesh }, { "proceduralBlade", VansPlantGeometry::ProceduralBlade } });
		item.Float("weight", variant.weight);
		item.Float("footprintRadius", variant.footprintRadius);
		item.Float("cullingRadius", variant.cullingRadius);
		item.Float("bladeWidth", variant.bladeWidth);
		item.Vector("offset", variant.offset);
		item.Vector("rotation", variant.rotation);
		item.Vector("scale", variant.scale);
		const auto* parts = item.Array("parts");
		if (parts) for (std::size_t partIndex = 0; partIndex < parts->size(); ++partIndex)
		{
			VansPlantPart part;
			PcgValue::Reader partReader(&(*parts)[partIndex], path + ".parts[" + std::to_string(partIndex) + "]", error);
			partReader.String("id", part.id);
			partReader.EnumField("kind", part.kind, { { "surface", VansPlantPartKind::Surface },
				{ "trunk", VansPlantPartKind::Trunk }, { "leaves", VansPlantPartKind::Leaves } });
			partReader.Reference("mesh", "model", part.mesh);
			partReader.IntegerField("submesh", part.submesh);
			partReader.Reference("material", "material", part.material);
			partReader.Finish();
			variant.parts.push_back(std::move(part));
		}
		item.Finish();
		decoded.variants.push_back(std::move(variant));
	}
	if (!reader.Finish()) return false;
	const auto errors = ValidatePlantTypeAsset(decoded, false);
	if (!errors.empty()) { error = errors.front(); return false; }
	asset = std::move(decoded);
	return true;
}

bool VansPlantTypeAssetCodec::Encode(const VansPlantTypeAsset& asset, Value& root, std::string& error)
{
	error.clear();
	const auto errors = ValidatePlantTypeAsset(asset, false);
	if (!errors.empty()) { error = errors.front(); return false; }
	std::vector<Value> variants;
	for (const auto& variant : asset.variants)
	{
		std::vector<Value> parts;
		for (const auto& part : variant.parts)
			parts.push_back(Value::Object({
				{ "id", Value::String(part.id) },
				{ "kind", Value::String(part.kind == VansPlantPartKind::Trunk ? "trunk" : part.kind == VansPlantPartKind::Leaves ? "leaves" : "surface") },
				{ "mesh", PcgValue::Reference(part.mesh, "model") }, { "submesh", Value::Int(part.submesh) },
				{ "material", PcgValue::Reference(part.material, "material") }
			}));
		variants.push_back(Value::Object({
			{ "id", Value::String(variant.id) }, { "name", Value::String(variant.name) },
			{ "geometry", Value::String(variant.geometry == VansPlantGeometry::Mesh ? "mesh" :
				variant.geometry == VansPlantGeometry::ProceduralBlade ? "proceduralBlade" : "unassigned") },
			{ "weight", Value::Float(variant.weight) }, { "footprintRadius", Value::Float(variant.footprintRadius) },
			{ "cullingRadius", Value::Float(variant.cullingRadius) }, { "bladeWidth", Value::Float(variant.bladeWidth) },
			{ "offset", PcgValue::Vector(variant.offset) }, { "rotation", PcgValue::Vector(variant.rotation) },
			{ "scale", PcgValue::Vector(variant.scale) }, { "parts", Value::Array(std::move(parts)) }
		}));
	}
	Fields fields{
		{ "name", Value::String(asset.name) }, { "category", Value::String(asset.category == VansPlantCategory::Grass ? "grass" : "tree") },
		{ "variants", Value::Array(std::move(variants)) },
		{ "render", Value::Object({
			{ "cullingEnabled", Value::Bool(asset.render.cullingEnabled) }, { "hizEnabled", Value::Bool(asset.render.hizEnabled) },
			{ "cullDistance", Value::Float(asset.render.cullDistance) }, { "hizBias", Value::Float(asset.render.hizBias) },
			{ "castShadows", Value::Bool(asset.render.castShadows) }
		}) }
	};
	if (asset.category == VansPlantCategory::Grass)
	{
		Fields grass{
			{ "boneCount", Value::Int(asset.grass.boneCount) }, { "subBladeCount", Value::Int(asset.grass.subBladeCount) },
			{ "scatterSeed", Value::Int(asset.grass.scatterSeed) },
			{ "windDirection", PcgValue::Vector(asset.grass.windDirection) }
		};
		for (const auto& field : GrassFields) grass.emplace_back(field.first, Value::Float(asset.grass.*field.second));
		fields.emplace_back("grass", Value::Object(std::move(grass)));
	}
	root = Value::Object(std::move(fields));
	return true;
}
}
