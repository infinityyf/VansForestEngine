#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace Vans
{
struct VansSerializedValue
{
    enum class Kind
    {
        Null,
        Bool,
        Int,
        Float,
        String,
        Array,
        Object
    };

    Kind kind = Kind::Null;
    bool boolValue = false;
    std::int64_t intValue = 0;
    double floatValue = 0.0;
    std::string stringValue;
    std::vector<VansSerializedValue> arrayItems;
    std::vector<std::pair<std::string, VansSerializedValue>> objectFields;

    bool IsNull() const { return kind == Kind::Null; }

    static VansSerializedValue Null()
    {
        return {};
    }

    static VansSerializedValue Bool(bool value)
    {
        VansSerializedValue result;
        result.kind = Kind::Bool;
        result.boolValue = value;
        return result;
    }

    static VansSerializedValue Int(std::int64_t value)
    {
        VansSerializedValue result;
        result.kind = Kind::Int;
        result.intValue = value;
        return result;
    }

    static VansSerializedValue Float(double value)
    {
        VansSerializedValue result;
        result.kind = Kind::Float;
        result.floatValue = value;
        return result;
    }

    static VansSerializedValue String(std::string value)
    {
        VansSerializedValue result;
        result.kind = Kind::String;
        result.stringValue = std::move(value);
        return result;
    }

    static VansSerializedValue Array(std::vector<VansSerializedValue> items)
    {
        VansSerializedValue result;
        result.kind = Kind::Array;
        result.arrayItems = std::move(items);
        return result;
    }

    static VansSerializedValue Object(std::vector<std::pair<std::string, VansSerializedValue>> fields)
    {
        VansSerializedValue result;
        result.kind = Kind::Object;
        result.objectFields = std::move(fields);
        return result;
    }
};
}
