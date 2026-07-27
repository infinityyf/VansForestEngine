#pragma once

#include "VansSerializedValue.h"

namespace Vans
{
template <typename Json>
Json EncodeSerializedValueLegacyJson(const VansSerializedValue& value)
{
    switch (value.kind)
    {
    case VansSerializedValue::Kind::Bool:
        return Json(value.boolValue);
    case VansSerializedValue::Kind::Int:
        return Json(value.intValue);
    case VansSerializedValue::Kind::Float:
        return Json(value.floatValue);
    case VansSerializedValue::Kind::String:
        return Json(value.stringValue);
    case VansSerializedValue::Kind::Array:
    {
        Json result = Json::array();
        for (const VansSerializedValue& item : value.arrayItems)
            result.push_back(EncodeSerializedValueLegacyJson<Json>(item));
        return result;
    }
    case VansSerializedValue::Kind::Object:
    {
        Json result = Json::object();
        for (const auto& [name, field] : value.objectFields)
            result[name] = EncodeSerializedValueLegacyJson<Json>(field);
        return result;
    }
    case VansSerializedValue::Kind::Null:
    default:
        return Json(nullptr);
    }
}

template <typename Json>
VansSerializedValue DecodeSerializedValueLegacyJson(const Json& value)
{
    if (value.is_boolean())
        return VansSerializedValue::Bool(value.template get<bool>());
    if (value.is_number_integer())
        return VansSerializedValue::Int(value.template get<std::int64_t>());
    if (value.is_number_unsigned())
        return VansSerializedValue::Int(static_cast<std::int64_t>(value.template get<std::uint64_t>()));
    if (value.is_number_float())
        return VansSerializedValue::Float(value.template get<double>());
    if (value.is_string())
        return VansSerializedValue::String(value.template get<std::string>());
    if (value.is_array())
    {
        std::vector<VansSerializedValue> items;
        items.reserve(value.size());
        for (const Json& item : value)
            items.push_back(DecodeSerializedValueLegacyJson(item));
        return VansSerializedValue::Array(std::move(items));
    }
    if (value.is_object())
    {
        std::vector<std::pair<std::string, VansSerializedValue>> fields;
        fields.reserve(value.size());
        for (auto entry = value.begin(); entry != value.end(); ++entry)
            fields.emplace_back(entry.key(), DecodeSerializedValueLegacyJson(entry.value()));
        return VansSerializedValue::Object(std::move(fields));
    }
    return VansSerializedValue::Null();
}
}
