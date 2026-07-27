#include "VansEditorRuntimePreviewProjector.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <cstdint>
#include <limits>
#include <utility>
#include <variant>
#include <vector>

namespace Vans
{
namespace
{
const VansSerializedValue& UnwrapMaterialParameterValue(const VansSerializedValue& value)
{
    if (const VansSerializedValue* wrappedValue = FindObjectField(value, "value"))
        return *wrappedValue;
    if (const VansSerializedValue* defaultValue = FindObjectField(value, "default"))
        return *defaultValue;
    return value;
}

EditorAPI::PropertyValue ToMaterialPropertyValue(const VansSerializedValue& rawValue)
{
    const VansSerializedValue& value = UnwrapMaterialParameterValue(rawValue);
    switch (value.kind)
    {
    case VansSerializedValue::Kind::Bool:
        return value.boolValue;
    case VansSerializedValue::Kind::Int:
        if (value.intValue >= static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::min)()) &&
            value.intValue <= static_cast<std::int64_t>((std::numeric_limits<std::int32_t>::max)()))
        {
            return static_cast<std::int32_t>(value.intValue);
        }
        if (value.intValue >= 0 &&
            value.intValue <= static_cast<std::int64_t>((std::numeric_limits<std::uint32_t>::max)()))
        {
            return static_cast<std::uint32_t>(value.intValue);
        }
        break;
    case VansSerializedValue::Kind::Float:
        return static_cast<float>(value.floatValue);
    case VansSerializedValue::Kind::String:
        return value.stringValue;
    case VansSerializedValue::Kind::Array:
    {
        const auto read = [&](std::size_t index) -> float
        {
            const VansSerializedValue* item = FindArrayItem(value, index);
            if (!item)
                return 0.0f;
            return static_cast<float>(ReadSerializedNumber(*item));
        };
        if (value.arrayItems.size() >= 4)
            return EditorAPI::Vec4{ read(0), read(1), read(2), read(3) };
        if (value.arrayItems.size() == 3)
            return EditorAPI::Vec3{ read(0), read(1), read(2) };
        if (value.arrayItems.size() == 2)
            return EditorAPI::Vec2{ read(0), read(1) };
        break;
    }
    case VansSerializedValue::Kind::Null:
    case VansSerializedValue::Kind::Object:
    default:
        break;
    }
    return std::monostate{};
}

void AppendMaterialPreviewParameter(
    EditorAPI::RuntimeMaterialPreviewChange& change,
    std::string parameterPath,
    const VansSerializedValue& value)
{
    EditorAPI::PropertyValue typedValue = ToMaterialPropertyValue(value);
    if (std::holds_alternative<std::monostate>(typedValue))
        return;
    change.parameters.push_back({
        std::move(parameterPath),
        std::move(typedValue)
    });
}

std::string ReadAssetReferenceGuid(const VansSerializedValue& value)
{
    if (value.kind == VansSerializedValue::Kind::String)
        return value.stringValue;
    if (value.kind == VansSerializedValue::Kind::Object)
        return ReadSerializedStringField(value, "guid");
    return {};
}

const VansSerializedValue* UnwrapTextureReference(const VansSerializedValue& value)
{
    if (value.kind == VansSerializedValue::Kind::Object)
    {
        if (const VansSerializedValue* texture = FindObjectField(value, "texture"))
            return texture;
        if (const VansSerializedValue* unwrapped = FindObjectField(value, "value"))
            return unwrapped;
    }
    return &value;
}

void AppendMaterialPreviewTexture(
    EditorAPI::RuntimeMaterialPreviewChange& change,
    std::string slot,
    const VansSerializedValue& value)
{
    const VansSerializedValue* reference = UnwrapTextureReference(value);
    if (!reference || slot.empty())
        return;
    change.textures.push_back({
        std::move(slot),
        ReadAssetReferenceGuid(*reference)
    });
}

void AppendTextureObjectPreviewEdits(
    EditorAPI::RuntimeMaterialPreviewChange& change,
    const VansSerializedValue& textures,
    const std::string& changedSlot = {})
{
    if (textures.kind != VansSerializedValue::Kind::Object)
        return;
    for (const auto& [slot, value] : textures.objectFields)
    {
        if (!changedSlot.empty() && slot != changedSlot)
            continue;
        AppendMaterialPreviewTexture(change, slot, value);
    }
}

void AppendTransparentTextureArrayPreviewEdits(
    EditorAPI::RuntimeMaterialPreviewChange& change,
    const VansSerializedValue& textures,
    const std::vector<std::string>& pointerTokens)
{
    if (textures.kind != VansSerializedValue::Kind::Array)
        return;

    std::size_t onlyIndex = textures.arrayItems.size();
    if (pointerTokens.size() >= 4 && pointerTokens[1] == "textures")
    {
        try
        {
            onlyIndex = static_cast<std::size_t>(std::stoull(pointerTokens[2]));
        }
        catch (...)
        {
            onlyIndex = textures.arrayItems.size();
        }
    }

    for (std::size_t i = 0; i < textures.arrayItems.size(); ++i)
    {
        if (onlyIndex < textures.arrayItems.size() && i != onlyIndex)
            continue;
        const VansSerializedValue& entry = textures.arrayItems[i];
        const std::string slot = ReadSerializedStringField(entry, "slot");
        if (const VansSerializedValue* texture = FindObjectField(entry, "texture"))
            AppendMaterialPreviewTexture(change, slot, *texture);
    }
}
}

EditorAPI::RuntimeMaterialPreviewChange BuildRuntimeMaterialPreviewChange(
    const std::filesystem::path& assetPath,
    const VansSerializedValue& assetRoot,
    const std::string& changedPointer)
{
    EditorAPI::RuntimeMaterialPreviewChange change;
    change.assetPath = assetPath.string();
    const std::vector<std::string> tokens = SplitSerializedPointer(changedPointer);
    if (tokens.size() >= 3 && tokens[0] == "asset")
    {
        if (tokens[1] == "parameters")
        {
            if (const VansSerializedValue* parameters = FindObjectField(assetRoot, "parameters"))
            {
                if (const VansSerializedValue* parameter = FindObjectField(*parameters, tokens[2]))
                {
                    AppendMaterialPreviewParameter(change, tokens[2], *parameter);
                    return change;
                }
            }
        }
        if (tokens[1] == "customParameters")
        {
            if (const VansSerializedValue* customParameters = FindObjectField(assetRoot, "customParameters"))
            {
                if (const VansSerializedValue* parameter = FindObjectField(*customParameters, tokens[2]))
                {
                    AppendMaterialPreviewParameter(
                        change,
                        "customParameters/" + tokens[2],
                        *parameter);
                    return change;
                }
            }
        }
        if (tokens[1] == "textures")
        {
            if (const VansSerializedValue* textures = FindObjectField(assetRoot, "textures"))
            {
                if (textures->kind == VansSerializedValue::Kind::Object && tokens.size() >= 3)
                {
                    AppendTextureObjectPreviewEdits(change, *textures, tokens[2]);
                    return change;
                }
                if (textures->kind == VansSerializedValue::Kind::Array)
                {
                    AppendTransparentTextureArrayPreviewEdits(change, *textures, tokens);
                    return change;
                }
            }
        }
        if (tokens[1] == "customTextures")
        {
            if (const VansSerializedValue* customTextures = FindObjectField(assetRoot, "customTextures"))
            {
                if (tokens.size() >= 3)
                {
                    AppendTextureObjectPreviewEdits(change, *customTextures, tokens[2]);
                    return change;
                }
            }
        }
    }

    const VansSerializedValue* parameters = FindObjectField(assetRoot, "parameters");
    if (!parameters && assetRoot.kind == VansSerializedValue::Kind::Object)
        parameters = &assetRoot;

    if (parameters && parameters->kind == VansSerializedValue::Kind::Object)
        for (const auto& [name, value] : parameters->objectFields)
            AppendMaterialPreviewParameter(change, name, value);

    if (const VansSerializedValue* customParameters = FindObjectField(assetRoot, "customParameters"))
    {
        if (customParameters->kind == VansSerializedValue::Kind::Object)
        {
            for (const auto& [name, value] : customParameters->objectFields)
            {
                AppendMaterialPreviewParameter(
                    change,
                    "customParameters/" + name,
                    value);
            }
        }
    }
    if (const VansSerializedValue* textures = FindObjectField(assetRoot, "textures"))
    {
        AppendTextureObjectPreviewEdits(change, *textures);
        AppendTransparentTextureArrayPreviewEdits(change, *textures, {});
    }
    if (const VansSerializedValue* customTextures = FindObjectField(assetRoot, "customTextures"))
    {
        AppendTextureObjectPreviewEdits(change, *customTextures);
    }
    return change;
}
}
