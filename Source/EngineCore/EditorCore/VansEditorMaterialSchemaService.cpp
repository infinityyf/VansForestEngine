#include "VansEditorMaterialSchemaService.h"

#include "../AssetCore/VansMaterialAuthoringAsset.h"
#include "../MaterialCore/VansClothMaterialParameters.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../EngineAPILayer/Public/IAssetAuthoringEditorAPI.h"
#include "../EngineAPILayer/Public/IAssetEditorAPI.h"

#include <nlohmann/json.hpp>
#include <algorithm>
#include <string>
#include <utility>

namespace Vans
{
namespace
{
VansSerializedValue LoadShaderAuthoringParameters(
    EditorAPI::IAssetAuthoringEditorAPI& api,
    const VansSerializedValue& materialRoot)
{
    if (materialRoot.kind != VansSerializedValue::Kind::Object ||
        !IsCustomShaderMaterialAuthoringType(ReadSerializedStringField(materialRoot, "materialType")))
    {
        return VansSerializedValue::Object({});
    }

    const VansSerializedValue* shaderReference = FindObjectField(materialRoot, "shader");
    if (!shaderReference || shaderReference->kind != VansSerializedValue::Kind::Object)
        return VansSerializedValue::Object({});

    const std::string shaderGuidText = ReadSerializedStringField(*shaderReference, "guid");
    const EditorAPI::ShaderAuthoringSchemaSnapshot schema =
        api.GetShaderAuthoringSchema(shaderGuidText);
    if (!schema.available || schema.parametersCanonicalJson.empty())
        return VansSerializedValue::Object({});

    const nlohmann::ordered_json parameters = nlohmann::ordered_json::parse(
        schema.parametersCanonicalJson, nullptr, false);
    if (parameters.is_discarded() || !parameters.is_object())
        return VansSerializedValue::Object({});
    return DecodeSerializedValueJson(parameters);
}
}

void MergeMaterialAuthoringSchema(
    EditorAPI::IAssetAuthoringEditorAPI& api,
    VansSerializedValue& materialRoot)
{
    if (materialRoot.kind != VansSerializedValue::Kind::Object)
        return;

    if (ReadSerializedStringField(materialRoot, "materialType") == "cloth")
    {
        auto& parameters = EnsureSerializedObjectField(materialRoot, "parameters");
        VansClothMaterialParameters parsed;
        std::string error;
        if (ReadClothMaterialParameters(parameters, parsed, error))
            parameters = WriteClothMaterialParameters(parsed);
        auto& textures = EnsureSerializedObjectField(materialRoot, "textures");
        for (const char* slot : VansClothTextureSlots)
            if (!FindObjectField(textures, slot))
                SetSerializedObjectField(textures, slot, VansSerializedValue::Object({
                    {"guid", VansSerializedValue::String("")}}));
        return;
    }

    const VansSerializedValue shaderParameters = LoadShaderAuthoringParameters(api, materialRoot);
    if (shaderParameters.kind != VansSerializedValue::Kind::Object)
        return;

    MergeMaterialAuthoringParameterSchema(materialRoot, shaderParameters);
}

bool PrepareClothMaterialAuthoringEdit(EditorAPI::IAssetEditorAPI& api,
    const VansSerializedValue& displayRoot, VansSerializedValue& sourceRoot,
    std::string& error)
{
    error.clear();
    auto candidate = displayRoot;
    if (auto* textures = FindObjectField(candidate, "textures");
        textures && textures->kind == VansSerializedValue::Kind::Object)
    {
        auto& slots = textures->objectFields;
        slots.erase(std::remove_if(slots.begin(), slots.end(), [](const auto& item)
        {
            const auto* guid = FindObjectField(item.second, "guid");
            return IsClothTextureSlot(item.first) && item.second.objectFields.size() == 1 &&
                guid && guid->kind == VansSerializedValue::Kind::String && guid->stringValue.empty();
        }), slots.end());
    }
    VansMaterialAuthoringAsset asset;
    if (!ReadMaterialAuthoringAsset(candidate, asset, error)) return false;
    // A material-type edit still uses the existing material parser. Cloth
    // display placeholders must not escape into the new material's source.
    if (asset.materialType == "cloth")
    for (const auto& [slot, reference] : asset.textures.objectFields)
    {
        const std::string guid = ReadSerializedStringField(reference, "guid");
        const auto resolved = api.ResolveAssetGuid(guid);
        const std::string context = "Cloth texture '" + slot + "' (" + guid + "): ";
        if (!resolved.found || resolved.asset.type != EditorAPI::AssetType::Texture)
        {
            error = context + "does not resolve to a Texture asset";
            return false;
        }
        if (!resolved.textureImport.available)
        {
            error = context + "import metadata is unavailable";
            return false;
        }
        std::string reason;
        if (!ValidateClothTextureImport(slot, VansClothTextureImportSettings{
            resolved.textureImport.linear, resolved.textureImport.channelCount}, reason))
        {
            error = context + reason;
            return false;
        }
    }
    SetSerializedObjectField(candidate, "parameters", asset.parameters);
    SetSerializedObjectField(candidate, "textures", asset.textures);
    sourceRoot = std::move(candidate);
    return true;
}

void MergeMaterialAuthoringParameterSchema(
    VansSerializedValue& materialRoot,
    const VansSerializedValue& shaderParameters)
{
    if (materialRoot.kind != VansSerializedValue::Kind::Object ||
        shaderParameters.kind != VansSerializedValue::Kind::Object)
    {
        return;
    }

    VansSerializedValue& materialParameters =
        EnsureSerializedObjectField(materialRoot, "parameters");

    for (const auto& [name, schema] : shaderParameters.objectFields)
    {
        if (schema.kind != VansSerializedValue::Kind::Object)
            continue;

        VansSerializedValue* materialParameter =
            FindObjectField(materialParameters, name);
        const bool wasMissing = !materialParameter;
        if (!materialParameter)
        {
            SetSerializedObjectField(
                materialParameters,
                name,
                VansSerializedValue::Object({}));
            materialParameter = FindObjectField(materialParameters, name);
        }
        else if (materialParameter->kind != VansSerializedValue::Kind::Object)
        {
            VansSerializedValue existingValue = *materialParameter;
            *materialParameter = VansSerializedValue::Object({
                { "value", std::move(existingValue) }
            });
        }

        if (!materialParameter || materialParameter->kind != VansSerializedValue::Kind::Object)
            continue;

        for (const auto& [field, schemaValue] : schema.objectFields)
        {
            if (!FindObjectField(*materialParameter, field))
                SetSerializedObjectField(*materialParameter, field, schemaValue);
        }
        if ((wasMissing || !FindObjectField(*materialParameter, "value")))
        {
            if (const VansSerializedValue* defaultValue = FindObjectField(schema, "default"))
                SetSerializedObjectField(*materialParameter, "value", *defaultValue);
        }
    }
}
}
