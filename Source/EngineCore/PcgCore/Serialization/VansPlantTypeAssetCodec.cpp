#include "VansPlantTypeAssetCodec.h"
#include "VansPcgConfigurationFieldCodec.h"

namespace Vans
{
namespace
{
using Value = VansSerializedValue;
using Fields = std::vector<std::pair<std::string, Value>>;
}

bool VansPlantTypeAssetCodec::Decode(const Value& root, VansPlantTypeAsset& asset, std::string& error)
{
	error.clear();
	VansPlantTypeAsset decoded;
	PcgValue::Reader reader(&root, "plant", error);
	reader.String("name", decoded.name);
	reader.EnumField("category", decoded.category, { { "grass", VansPlantCategory::Grass }, { "tree", VansPlantCategory::Tree } });
	auto render = reader.Object("render");
	ReadPcgConfigurationFields(render, decoded.render, VansPlantRenderConfigurationFields,
		decoded.category == VansPlantCategory::Tree);
	render.Finish();
	if (decoded.category == VansPlantCategory::Grass)
	{
		auto grass = reader.Object("grass");
		ReadPcgConfigurationFields(grass, decoded.grass, VansPlantGrassConfigurationFields, false);
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

        if (decoded.category == VansPlantCategory::Tree) {
            auto lod=item.Object("lod");
            lod.FloatVector("ratios",variant.lodSettings.ratios,
                MinimumModelLodLevelCount,MaximumModelLodLevelCount);
            lod.Float("maximumError",variant.lodSettings.maximumError);
            lod.String("buildKey",variant.lod.buildKey);
            lod.Vector("centerRadius",variant.lod.centerRadius);
            const auto* levels=lod.Array("levels");
            if (levels) for (const auto& value:*levels) {
                VansModelLodLevel level;
                PcgValue::Reader levelReader(&value,path+".lod.levels",error);
                const auto* values=levelReader.Array("parts");
                if (values) for (const auto& valuePart:*values) {
                    VansModelLodPart part;
                    PcgValue::Reader p(&valuePart,path+".lod.levels.parts",error);
                    p.Reference("model","model",part.model);p.Reference("material","material",part.material);
                    p.IntegerField("submesh",part.submesh);p.IntegerField("sourcePart",part.sourcePart);
                    p.IntegerField("triangles",part.triangleCount);p.Float("error",part.error);p.Finish();
                    level.parts.push_back(part);
                }
                levelReader.Finish();variant.lod.levels.push_back(std::move(level));
            }
            lod.Finish();
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
        if (asset.category == VansPlantCategory::Tree) {
            std::vector<Value> levels;
            for (const auto& level:variant.lod.levels) {
                std::vector<Value> entries;
                for (const auto& part:level.parts) entries.push_back(Value::Object({
                    {"model",PcgValue::Reference(part.model,"model")},{"material",PcgValue::Reference(part.material,"material")},
                    {"submesh",Value::Int(part.submesh)},{"sourcePart",Value::Int(part.sourcePart)},
                    {"triangles",Value::Int(part.triangleCount)},{"error",Value::Float(part.error)}}));
                levels.push_back(Value::Object({{"parts",Value::Array(std::move(entries))}}));
            }
            variants.back().objectFields.emplace_back("lod",Value::Object({
                {"ratios",PcgValue::FloatVector(variant.lodSettings.ratios)},{"maximumError",Value::Float(variant.lodSettings.maximumError)},
                {"buildKey",Value::String(variant.lod.buildKey)},{"centerRadius",PcgValue::Vector(variant.lod.centerRadius)},
                {"levels",Value::Array(std::move(levels))}}));
        }

	}
	Fields render;
	WritePcgConfigurationFields(render, asset.render, VansPlantRenderConfigurationFields,
		asset.category == VansPlantCategory::Tree);
	Fields fields{
		{ "name", Value::String(asset.name) }, { "category", Value::String(asset.category == VansPlantCategory::Grass ? "grass" : "tree") },
		{ "variants", Value::Array(std::move(variants)) },
		{ "render", Value::Object(std::move(render)) }
	};
	if (asset.category == VansPlantCategory::Grass)
	{
		Fields grass;
		WritePcgConfigurationFields(grass, asset.grass, VansPlantGrassConfigurationFields, false);
		fields.emplace_back("grass", Value::Object(std::move(grass)));
	}
	root = Value::Object(std::move(fields));
	return true;
}
}
