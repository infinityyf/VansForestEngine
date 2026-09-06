#include "VansEditorMaterialSchemaService.h"

#include "../AssetCore/VansMaterialAuthoringAsset.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../AssetCore/Serialization/VansSerializedValueJsonAdapter.h"
#include "../EngineAPILayer/Public/IEngineEditorAPI.h"

#include <nlohmann/json.hpp>
#include <string>
#include <utility>

namespace Vans
{
namespace
{
VansSerializedValue LoadShaderAuthoringParameters(
    EditorAPI::IEngineEditorAPI& api,
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
    EditorAPI::IEngineEditorAPI& api,
    VansSerializedValue& materialRoot)
{
    if (materialRoot.kind != VansSerializedValue::Kind::Object)
        return;

    const VansSerializedValue shaderParameters = LoadShaderAuthoringParameters(api, materialRoot);
    if (shaderParameters.kind != VansSerializedValue::Kind::Object)
        return;

    MergeMaterialAuthoringParameterSchema(materialRoot, shaderParameters);
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
