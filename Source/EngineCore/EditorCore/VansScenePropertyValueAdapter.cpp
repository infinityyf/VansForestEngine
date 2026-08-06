#include "VansScenePropertyValueAdapter.h"

#include <utility>

namespace Vans
{
VansSerializedValue ToSerializedValue(const EditorAPI::ScenePropertyValue& value)
{
    using Value = EditorAPI::ScenePropertyValue;
    switch (value.kind)
    {
    case Value::Kind::Bool:
        return VansSerializedValue::Bool(value.boolValue);
    case Value::Kind::Int:
        return VansSerializedValue::Int(value.intValue);
    case Value::Kind::Float:
        return VansSerializedValue::Float(value.floatValue);
    case Value::Kind::String:
        return VansSerializedValue::String(value.stringValue);
    case Value::Kind::Array:
    {
        std::vector<VansSerializedValue> items;
        items.reserve(value.arrayItems.size());
        for (const Value& item : value.arrayItems)
            items.push_back(ToSerializedValue(item));
        return VansSerializedValue::Array(std::move(items));
    }
    case Value::Kind::Object:
    {
        std::vector<std::pair<std::string, VansSerializedValue>> fields;
        fields.reserve(value.objectFields.size());
        for (const auto& [name, field] : value.objectFields)
            fields.emplace_back(name, ToSerializedValue(field));
        return VansSerializedValue::Object(std::move(fields));
    }
    default:
        return VansSerializedValue::Null();
    }
}

EditorAPI::ScenePropertyValue FromSerializedValue(const VansSerializedValue& value)
{
    switch (value.kind)
    {
    case VansSerializedValue::Kind::Bool:
        return EditorAPI::ScenePropertyValue::Bool(value.boolValue);
    case VansSerializedValue::Kind::Int:
        return EditorAPI::ScenePropertyValue::Int(value.intValue);
    case VansSerializedValue::Kind::Float:
        return EditorAPI::ScenePropertyValue::Float(value.floatValue);
    case VansSerializedValue::Kind::String:
        return EditorAPI::ScenePropertyValue::String(value.stringValue);
    case VansSerializedValue::Kind::Array:
    {
        std::vector<EditorAPI::ScenePropertyValue> items;
        items.reserve(value.arrayItems.size());
        for (const VansSerializedValue& item : value.arrayItems)
            items.push_back(FromSerializedValue(item));
        return EditorAPI::ScenePropertyValue::Array(std::move(items));
    }
    case VansSerializedValue::Kind::Object:
    {
        std::vector<std::pair<std::string, EditorAPI::ScenePropertyValue>> fields;
        fields.reserve(value.objectFields.size());
        for (const auto& [name, field] : value.objectFields)
            fields.emplace_back(name, FromSerializedValue(field));
        return EditorAPI::ScenePropertyValue::Object(std::move(fields));
    }
    case VansSerializedValue::Kind::Null:
    default:
        return {};
    }
}
}
