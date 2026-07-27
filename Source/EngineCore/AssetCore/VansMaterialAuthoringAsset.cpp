#include "VansMaterialAuthoringAsset.h"

#include "Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace Vans
{
namespace
{
VansSerializedValue ReadObjectFieldCopy(const VansSerializedValue& root, const std::string& name)
{
    const VansSerializedValue* field = FindObjectField(root, name);
    return field ? *field : VansSerializedValue::Null();
}

std::string LowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value;
}
}

const char* DefaultMaterialAuthoringType()
{
    return "pbr";
}

std::string MaterialAuthoringTypeOrDefault(const std::string& materialType)
{
    return materialType.empty() ? DefaultMaterialAuthoringType() : materialType;
}

bool IsCustomShaderMaterialAuthoringType(const std::string& materialType)
{
    const std::string normalized = LowerAscii(MaterialAuthoringTypeOrDefault(materialType));
    return normalized == "customshader" || normalized == "custom";
}

bool IsTransparentMaterialAuthoringType(const std::string& materialType)
{
    return LowerAscii(MaterialAuthoringTypeOrDefault(materialType)) == "transparent";
}

VansSerializedValue CreatePbrMaterialAuthoringParameters(
    float albedoR,
    float albedoG,
    float albedoB,
    float metallic,
    float roughness,
    float ao)
{
    return VansSerializedValue::Object({
        { "albedo", VansSerializedValue::Array({
            VansSerializedValue::Float(albedoR),
            VansSerializedValue::Float(albedoG),
            VansSerializedValue::Float(albedoB) }) },
        { "metallic", VansSerializedValue::Float(metallic) },
        { "roughness", VansSerializedValue::Float(roughness) },
        { "ao", VansSerializedValue::Float(ao) }
    });
}

bool ReadMaterialAuthoringAsset(
    const VansSerializedValue& root,
    VansMaterialAuthoringAsset& asset,
    std::string& error)
{
    if (root.kind != VansSerializedValue::Kind::Object)
    {
        error = "Material asset root must be an object";
        return false;
    }

    VansMaterialAuthoringAsset parsed;
    parsed.guid = ReadSerializedStringField(root, "guid");
    parsed.materialType = MaterialAuthoringTypeOrDefault(ReadSerializedStringField(root, "materialType"));

    if (const VansSerializedValue* importSource = FindObjectField(root, "importSource"))
    {
        parsed.importSource = *importSource;
        parsed.preferredImportModel = ReadSerializedStringField(*importSource, "model");
    }

    parsed.shader = ReadObjectFieldCopy(root, "shader");
    parsed.shaderPasses = ReadObjectFieldCopy(root, "shaderPasses");
    parsed.textures = ReadObjectFieldCopy(root, "textures");

    if (const VansSerializedValue* parameters = FindObjectField(root, "parameters"))
        parsed.parameters = *parameters;
    if (const VansSerializedValue* customParameters = FindObjectField(root, "customParameters"))
        parsed.customParameters = *customParameters;
    if (const VansSerializedValue* customTextures = FindObjectField(root, "customTextures"))
        parsed.customTextures = *customTextures;

    asset = std::move(parsed);
    return true;
}

VansSerializedValue WriteMaterialAuthoringAssetRoot(const VansMaterialAuthoringAsset& asset)
{
    VansSerializedValue root = VansSerializedValue::Object({});
    if (!asset.guid.empty())
        SetSerializedObjectField(root, "guid", VansSerializedValue::String(asset.guid));
    SetSerializedObjectField(root, "materialType", VansSerializedValue::String(MaterialAuthoringTypeOrDefault(asset.materialType)));
    if (!asset.importSource.IsNull())
        SetSerializedObjectField(root, "importSource", asset.importSource);
    if (!asset.shader.IsNull())
        SetSerializedObjectField(root, "shader", asset.shader);
    if (!asset.shaderPasses.IsNull())
        SetSerializedObjectField(root, "shaderPasses", asset.shaderPasses);
    SetSerializedObjectField(root, "parameters", asset.parameters);
    if (!asset.textures.IsNull())
        SetSerializedObjectField(root, "textures", asset.textures);
    if (!asset.customParameters.IsNull())
        SetSerializedObjectField(root, "customParameters", asset.customParameters);
    if (!asset.customTextures.IsNull())
        SetSerializedObjectField(root, "customTextures", asset.customTextures);
    return root;
}
}
