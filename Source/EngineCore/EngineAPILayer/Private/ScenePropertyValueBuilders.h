#pragma once

#include "../../AssetCore/Serialization/VansSerializedValue.h"
#include "../Public/EngineDTOs.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace Vans::EditorAPI::ScenePropertyValues
{
inline ScenePropertyValue Bool(bool value)
{
    return ScenePropertyValue::Bool(value);
}

inline ScenePropertyValue Int(std::int64_t value)
{
    return ScenePropertyValue::Int(value);
}

inline ScenePropertyValue Float(double value)
{
    return ScenePropertyValue::Float(value);
}

inline ScenePropertyValue String(std::string value)
{
    return ScenePropertyValue::String(std::move(value));
}

inline ScenePropertyValue Array(std::initializer_list<ScenePropertyValue> items)
{
    return ScenePropertyValue::Array(std::vector<ScenePropertyValue>(items));
}

inline ScenePropertyValue Array(std::vector<ScenePropertyValue> items)
{
    return ScenePropertyValue::Array(std::move(items));
}

inline ScenePropertyValue Object(
    std::initializer_list<std::pair<std::string, ScenePropertyValue>> fields)
{
    return ScenePropertyValue::Object(
        std::vector<std::pair<std::string, ScenePropertyValue>>(fields));
}

inline ScenePropertyValue Object(std::vector<std::pair<std::string, ScenePropertyValue>> fields)
{
    return ScenePropertyValue::Object(std::move(fields));
}

inline ScenePropertyValue FromSerializedValue(const Vans::VansSerializedValue& value)
{
    switch (value.kind)
    {
    case Vans::VansSerializedValue::Kind::Bool:
        return Bool(value.boolValue);
    case Vans::VansSerializedValue::Kind::Int:
        return Int(value.intValue);
    case Vans::VansSerializedValue::Kind::Float:
        return Float(value.floatValue);
    case Vans::VansSerializedValue::Kind::String:
        return String(value.stringValue);
    case Vans::VansSerializedValue::Kind::Array:
    {
        std::vector<ScenePropertyValue> items;
        items.reserve(value.arrayItems.size());
        for (const Vans::VansSerializedValue& item : value.arrayItems)
            items.push_back(FromSerializedValue(item));
        return Array(std::move(items));
    }
    case Vans::VansSerializedValue::Kind::Object:
    {
        std::vector<std::pair<std::string, ScenePropertyValue>> fields;
        fields.reserve(value.objectFields.size());
        for (const auto& [name, field] : value.objectFields)
            fields.emplace_back(name, FromSerializedValue(field));
        return Object(std::move(fields));
    }
    case Vans::VansSerializedValue::Kind::Null:
    default:
        return {};
    }
}

inline Vans::VansSerializedValue ToSerializedValue(const ScenePropertyValue& value)
{
    switch (value.kind)
    {
    case ScenePropertyValue::Kind::Bool:
        return Vans::VansSerializedValue::Bool(value.boolValue);
    case ScenePropertyValue::Kind::Int:
        return Vans::VansSerializedValue::Int(value.intValue);
    case ScenePropertyValue::Kind::Float:
        return Vans::VansSerializedValue::Float(value.floatValue);
    case ScenePropertyValue::Kind::String:
        return Vans::VansSerializedValue::String(value.stringValue);
    case ScenePropertyValue::Kind::Array:
    {
        std::vector<Vans::VansSerializedValue> items;
        items.reserve(value.arrayItems.size());
        for (const ScenePropertyValue& item : value.arrayItems)
            items.push_back(ToSerializedValue(item));
        return Vans::VansSerializedValue::Array(std::move(items));
    }
    case ScenePropertyValue::Kind::Object:
    {
        std::vector<std::pair<std::string, Vans::VansSerializedValue>> fields;
        fields.reserve(value.objectFields.size());
        for (const auto& [name, field] : value.objectFields)
            fields.emplace_back(name, ToSerializedValue(field));
        return Vans::VansSerializedValue::Object(std::move(fields));
    }
    case ScenePropertyValue::Kind::Null:
    default:
        return Vans::VansSerializedValue::Null();
    }
}
}
