#include "VansModelImportReport.h"

#include "../Serialization/VansSerializedValueAccess.h"
#include "../VansAssetMeta.h"
#include "../VansModelAsset.h"

#include <utility>

namespace Vans
{
namespace
{
std::string ReadStringField(const VansSerializedValue& object, const char* key)
{
    return ReadSerializedStringField(object, key);
}

std::pair<std::string, VansSerializedValue> Field(std::string name, VansSerializedValue value)
{
    return { std::move(name), std::move(value) };
}

VansSerializedValue EncodeFloatArray(const std::array<float, 3>& values)
{
    return VansSerializedValue::Array({
        VansSerializedValue::Float(values[0]),
        VansSerializedValue::Float(values[1]),
        VansSerializedValue::Float(values[2])
    });
}

VansSerializedValue EncodeFloatArray(const std::array<float, 4>& values)
{
    return VansSerializedValue::Array({
        VansSerializedValue::Float(values[0]),
        VansSerializedValue::Float(values[1]),
        VansSerializedValue::Float(values[2]),
        VansSerializedValue::Float(values[3])
    });
}

VansSerializedValue EncodeModelImportReport(const VansModelAsset& asset)
{
    std::vector<VansSerializedValue> materials;
    materials.reserve(asset.materialSlots.size());
    for (const VansImportedMaterialSlot& slot : asset.materialSlots)
    {
        materials.push_back(VansSerializedValue::Object({
            Field("id", VansSerializedValue::String(slot.id.ToString())),
            Field("name", VansSerializedValue::String(slot.name)),
            Field("baseColor", EncodeFloatArray(slot.baseColor)),
            Field("specularColor", EncodeFloatArray(slot.specularColor)),
            Field("emissiveColor", EncodeFloatArray(slot.emissiveColor)),
            Field("opacity", VansSerializedValue::Float(slot.opacity)),
            Field("metallic", VansSerializedValue::Float(slot.metallic)),
            Field("roughness", VansSerializedValue::Float(slot.roughness)),
            Field("specularFactor", VansSerializedValue::Float(slot.specularFactor)),
            Field("shininess", VansSerializedValue::Float(slot.shininess)),
            Field("reflectionFactor", VansSerializedValue::Float(slot.reflectionFactor)),
            Field("transparent", VansSerializedValue::Bool(slot.transparent))
        }));
    }

    std::vector<VansSerializedValue> textures;
    textures.reserve(asset.textureReferences.size());
    for (const VansImportedTextureRef& ref : asset.textureReferences)
    {
        textures.push_back(VansSerializedValue::Object({
            Field("materialSlotId", VansSerializedValue::String(ref.materialSlotId.ToString())),
            Field("materialName", VansSerializedValue::String(ref.materialName)),
            Field("semantic", VansSerializedValue::String(ref.semantic)),
            Field("sourcePath", VansSerializedValue::String(ref.sourcePath.string())),
            Field("srgb", VansSerializedValue::Bool(ref.srgb))
        }));
    }
    return VansSerializedValue::Object({
        Field("materials", VansSerializedValue::Array(std::move(materials))),
        Field("textures", VansSerializedValue::Array(std::move(textures)))
    });
}
}

VansModelImportReport ReadModelImportReport(const VansAssetMeta& meta)
{
    VansModelImportReport report;
    if (!meta.HasObjectSettings())
        return report;

    const VansSerializedValue settings = meta.SerializedSettingsSnapshot();
    const VansSerializedValue* importReport = FindObjectField(settings, "importReport");
    if (importReport == nullptr || importReport->kind != VansSerializedValue::Kind::Object)
        return report;

    const VansSerializedValue* textures = FindObjectField(*importReport, "textures");
    if (textures != nullptr && textures->kind == VansSerializedValue::Kind::Array)
    {
        for (const VansSerializedValue& texture : textures->arrayItems)
        {
            if (texture.kind != VansSerializedValue::Kind::Object)
                continue;

            VansModelImportReportTexture parsedTexture;
            parsedTexture.guid = ReadStringField(texture, "guid");
            parsedTexture.name = ReadStringField(texture, "name");
            parsedTexture.path = ReadStringField(texture, "path");
            if (parsedTexture.path.empty())
                parsedTexture.path = ReadStringField(texture, "sourcePath");
            if (!parsedTexture.guid.empty() || !parsedTexture.name.empty() || !parsedTexture.path.empty())
                report.textures.push_back(std::move(parsedTexture));
        }
    }

    const VansSerializedValue* materials = FindObjectField(*importReport, "generatedMaterials");
    if (materials != nullptr && materials->kind == VansSerializedValue::Kind::Array)
    {
        for (const VansSerializedValue& material : materials->arrayItems)
        {
            if (material.kind != VansSerializedValue::Kind::Object)
                continue;

            VansModelImportReportGeneratedMaterial parsedMaterial;
            parsedMaterial.guid = ReadStringField(material, "guid");
            parsedMaterial.textureGuid = ReadStringField(material, "texture");
            if (parsedMaterial.textureGuid.empty())
                parsedMaterial.textureGuid = ReadStringField(material, "textureGuid");
            if (!parsedMaterial.guid.empty())
                report.generatedMaterials.push_back(std::move(parsedMaterial));
        }
    }

    return report;
}

void WriteModelImportMetaSettings(
    VansAssetMeta& meta,
    const VansModelImportSettings& settings,
    const VansModelAsset& asset)
{
    meta.SetSerializedSettings(VansSerializedValue::Object({
        Field("scaleFactor", VansSerializedValue::Float(settings.scaleFactor)),
        Field("generateNormals", VansSerializedValue::String(settings.generateNormalsIfMissing ? "ifMissing" : "never")),
        Field("generateTangents", VansSerializedValue::Bool(settings.generateTangents)),
        Field("flipUV", VansSerializedValue::Bool(settings.flipUV)),
        Field("importMaterials", VansSerializedValue::Bool(settings.importMaterials)),
        Field("materialMode", VansSerializedValue::String(settings.materialMode)),
        Field("textureRedirection", VansSerializedValue::String(settings.textureRedirection)),
        Field("defaultShader", VansSerializedValue::String(settings.defaultShader)),
        Field("redirectTextures", VansSerializedValue::Bool(settings.redirectTextures)),
        Field("importAnimations", VansSerializedValue::Bool(settings.importAnimations)),
        Field("keepCpuMeshData", VansSerializedValue::Bool(settings.keepCpuMeshData)),
        Field("buildRayTracingData", VansSerializedValue::Bool(settings.buildRayTracingData)),
        Field("importReport", EncodeModelImportReport(asset))
    }));
}
}
