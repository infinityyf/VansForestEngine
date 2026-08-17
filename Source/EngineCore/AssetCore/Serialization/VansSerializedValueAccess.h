#pragma once

#include "VansSerializedValue.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace Vans
{
namespace SerializedValueDetail
{
inline bool FitsBudget(
	const VansSerializedValue& value,
	std::size_t& remaining,
	std::size_t depth,
	std::size_t maximumDepth)
{
	if (depth > maximumDepth || remaining == 0) return false;
	--remaining;
	const auto consume = [&](std::size_t amount)
	{
		if (amount > remaining) return false;
		remaining -= amount;
		return true;
	};
	switch (value.kind)
	{
	case VansSerializedValue::Kind::Null: return true;
	case VansSerializedValue::Kind::Bool: return consume(1);
	case VansSerializedValue::Kind::Int:
	case VansSerializedValue::Kind::Float: return consume(8);
	case VansSerializedValue::Kind::String: return consume(value.stringValue.size());
	case VansSerializedValue::Kind::Array:
		for (const VansSerializedValue& item : value.arrayItems)
			if (!FitsBudget(item, remaining, depth + 1, maximumDepth)) return false;
		return true;
	case VansSerializedValue::Kind::Object:
		for (const auto& [name, item] : value.objectFields)
			if (!consume(name.size()) ||
				!FitsBudget(item, remaining, depth + 1, maximumDepth)) return false;
		return true;
	}
	return false;
}
}

inline bool SerializedValueFitsBudget(
	const VansSerializedValue& value,
	std::size_t maximumBytes,
	std::size_t maximumDepth = 64)
{
	std::size_t remaining = maximumBytes;
	return SerializedValueDetail::FitsBudget(value, remaining, 0, maximumDepth);
}

inline const VansSerializedValue* FindObjectField(
    const VansSerializedValue& value,
    const std::string& name)
{
    if (value.kind != VansSerializedValue::Kind::Object)
        return nullptr;
    for (const auto& [fieldName, fieldValue] : value.objectFields)
        if (fieldName == name)
            return &fieldValue;
    return nullptr;
}

inline VansSerializedValue* FindObjectField(
    VansSerializedValue& value,
    const std::string& name)
{
    if (value.kind != VansSerializedValue::Kind::Object)
        return nullptr;
    for (auto& [fieldName, fieldValue] : value.objectFields)
        if (fieldName == name)
            return &fieldValue;
    return nullptr;
}

inline void SetSerializedObjectField(
    VansSerializedValue& object,
    std::string name,
    VansSerializedValue fieldValue)
{
    if (object.kind != VansSerializedValue::Kind::Object)
    {
        object = VansSerializedValue::Object({});
    }
    if (VansSerializedValue* existing = FindObjectField(object, name))
    {
        *existing = std::move(fieldValue);
        return;
    }
    object.objectFields.emplace_back(std::move(name), std::move(fieldValue));
}

inline VansSerializedValue& EnsureSerializedObjectField(
    VansSerializedValue& object,
    std::string name)
{
    if (object.kind != VansSerializedValue::Kind::Object)
        object = VansSerializedValue::Object({});
    if (VansSerializedValue* existing = FindObjectField(object, name))
    {
        if (existing->kind != VansSerializedValue::Kind::Object)
            *existing = VansSerializedValue::Object({});
        return *existing;
    }
    object.objectFields.emplace_back(std::move(name), VansSerializedValue::Object({}));
    return object.objectFields.back().second;
}

inline bool EraseSerializedObjectField(
    VansSerializedValue& object,
    const std::string& name)
{
    if (object.kind != VansSerializedValue::Kind::Object)
        return false;
    for (auto iterator = object.objectFields.begin(); iterator != object.objectFields.end(); ++iterator)
    {
        if (iterator->first == name)
        {
            object.objectFields.erase(iterator);
            return true;
        }
    }
    return false;
}

inline bool SerializedValuesEqual(
    const VansSerializedValue& left,
    const VansSerializedValue& right)
{
    if (left.kind != right.kind)
        return false;
    switch (left.kind)
    {
    case VansSerializedValue::Kind::Null:
        return true;
    case VansSerializedValue::Kind::Bool:
        return left.boolValue == right.boolValue;
    case VansSerializedValue::Kind::Int:
        return left.intValue == right.intValue;
    case VansSerializedValue::Kind::Float:
        return left.floatValue == right.floatValue;
    case VansSerializedValue::Kind::String:
        return left.stringValue == right.stringValue;
    case VansSerializedValue::Kind::Array:
        if (left.arrayItems.size() != right.arrayItems.size())
            return false;
        for (std::size_t index = 0; index < left.arrayItems.size(); ++index)
        {
            if (!SerializedValuesEqual(left.arrayItems[index], right.arrayItems[index]))
                return false;
        }
        return true;
    case VansSerializedValue::Kind::Object:
        if (left.objectFields.size() != right.objectFields.size())
            return false;
        for (std::size_t index = 0; index < left.objectFields.size(); ++index)
        {
            if (left.objectFields[index].first != right.objectFields[index].first ||
                !SerializedValuesEqual(left.objectFields[index].second, right.objectFields[index].second))
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

inline const VansSerializedValue* FindArrayItem(
    const VansSerializedValue& value,
    std::size_t index)
{
    if (value.kind != VansSerializedValue::Kind::Array || index >= value.arrayItems.size())
        return nullptr;
    return &value.arrayItems[index];
}

inline std::string UnescapeSerializedPointerToken(std::string token)
{
    std::string result;
    result.reserve(token.size());
    for (std::size_t index = 0; index < token.size(); ++index)
    {
        if (token[index] == '~' && index + 1 < token.size())
        {
            if (token[index + 1] == '0')
            {
                result.push_back('~');
                ++index;
                continue;
            }
            if (token[index + 1] == '1')
            {
                result.push_back('/');
                ++index;
                continue;
            }
        }
        result.push_back(token[index]);
    }
    return result;
}

inline std::vector<std::string> SplitSerializedPointer(const std::string& pointer)
{
    std::vector<std::string> tokens;
    if (pointer.empty())
        return tokens;
    if (pointer[0] != '/')
        return tokens;

    std::size_t start = 1;
    while (start <= pointer.size())
    {
        const std::size_t slash = pointer.find('/', start);
        const std::size_t end = slash == std::string::npos ? pointer.size() : slash;
        tokens.push_back(UnescapeSerializedPointerToken(pointer.substr(start, end - start)));
        if (slash == std::string::npos)
            break;
        start = slash + 1;
    }
    return tokens;
}

inline bool TryParseSerializedArrayIndex(const std::string& token, std::size_t& index)
{
    if (token.empty())
        return false;

    std::size_t parsed = 0;
    for (const char ch : token)
    {
        if (ch < '0' || ch > '9')
            return false;

        const std::size_t digit = static_cast<std::size_t>(ch - '0');
        if (parsed > ((std::numeric_limits<std::size_t>::max)() - digit) / 10)
            return false;
        parsed = parsed * 10 + digit;
    }

    index = parsed;
    return true;
}

inline const VansSerializedValue* FindSerializedPointer(
    const VansSerializedValue& root,
    const std::string& pointer)
{
    if (pointer.empty())
        return &root;
    if (pointer[0] != '/')
        return nullptr;

    const VansSerializedValue* current = &root;
    for (const std::string& token : SplitSerializedPointer(pointer))
    {
        if (!current)
            return nullptr;

        if (current->kind == VansSerializedValue::Kind::Object)
        {
            current = FindObjectField(*current, token);
            continue;
        }

        if (current->kind == VansSerializedValue::Kind::Array)
        {
            std::size_t index = 0;
            if (!TryParseSerializedArrayIndex(token, index))
                return nullptr;
            current = FindArrayItem(*current, index);
            continue;
        }

        return nullptr;
    }
    return current;
}

inline VansSerializedValue* FindSerializedPointer(
    VansSerializedValue& root,
    const std::string& pointer)
{
    if (pointer.empty())
        return &root;
    if (pointer[0] != '/')
        return nullptr;

    VansSerializedValue* current = &root;
    for (const std::string& token : SplitSerializedPointer(pointer))
    {
        if (!current)
            return nullptr;

        if (current->kind == VansSerializedValue::Kind::Object)
        {
            current = FindObjectField(*current, token);
            continue;
        }

        if (current->kind == VansSerializedValue::Kind::Array)
        {
            std::size_t index = 0;
            if (!TryParseSerializedArrayIndex(token, index) || index >= current->arrayItems.size())
                return nullptr;
            current = &current->arrayItems[index];
            continue;
        }

        return nullptr;
    }
    return current;
}

inline bool SetSerializedPointer(
    VansSerializedValue& root,
    const std::string& pointer,
    VansSerializedValue value,
    std::string* error = nullptr)
{
    if (pointer.empty())
    {
        root = std::move(value);
        return true;
    }
    if (pointer[0] != '/')
    {
        if (error)
            *error = "Serialized value pointer must be a JSON Pointer";
        return false;
    }

    std::vector<std::string> tokens = SplitSerializedPointer(pointer);
    if (tokens.empty())
    {
        if (error)
            *error = "Serialized value pointer must not be empty";
        return false;
    }

    VansSerializedValue* current = &root;
    for (std::size_t tokenIndex = 0; tokenIndex + 1 < tokens.size(); ++tokenIndex)
    {
        const std::string& token = tokens[tokenIndex];
        if (current->kind == VansSerializedValue::Kind::Object)
        {
            VansSerializedValue* child = FindObjectField(*current, token);
            if (!child)
            {
                current->objectFields.emplace_back(token, VansSerializedValue::Object({}));
                child = &current->objectFields.back().second;
            }
            current = child;
            continue;
        }

        if (current->kind == VansSerializedValue::Kind::Array)
        {
            std::size_t index = 0;
            if (!TryParseSerializedArrayIndex(token, index) || index >= current->arrayItems.size())
            {
                if (error)
                    *error = "Invalid serialized value array index";
                return false;
            }
            current = &current->arrayItems[index];
            continue;
        }

        if (error)
            *error = "Serialized value pointer parent is not a container";
        return false;
    }

    const std::string& leaf = tokens.back();
    if (current->kind == VansSerializedValue::Kind::Object)
    {
        SetSerializedObjectField(*current, leaf, std::move(value));
        return true;
    }
    if (current->kind == VansSerializedValue::Kind::Array)
    {
        if (leaf == "-")
        {
            current->arrayItems.push_back(std::move(value));
            return true;
        }

        std::size_t index = 0;
        if (!TryParseSerializedArrayIndex(leaf, index) || index >= current->arrayItems.size())
        {
            if (error)
                *error = "Invalid serialized value array index";
            return false;
        }
        current->arrayItems[index] = std::move(value);
        return true;
    }

    if (error)
        *error = "Serialized value pointer parent is not a container";
    return false;
}

inline bool EraseSerializedPointer(
    VansSerializedValue& root,
    const std::string& pointer,
    std::string* error = nullptr)
{
    if (pointer.empty())
    {
        if (error)
            *error = "Cannot erase the serialized root";
        return false;
    }
    if (pointer[0] != '/')
    {
        if (error)
            *error = "Serialized value pointer must be a JSON Pointer";
        return false;
    }

    std::vector<std::string> tokens = SplitSerializedPointer(pointer);
    if (tokens.empty())
    {
        if (error)
            *error = "Serialized value pointer must not be empty";
        return false;
    }

    VansSerializedValue* current = &root;
    for (std::size_t tokenIndex = 0; tokenIndex + 1 < tokens.size(); ++tokenIndex)
    {
        const std::string& token = tokens[tokenIndex];
        if (current->kind == VansSerializedValue::Kind::Object)
        {
            current = FindObjectField(*current, token);
            if (!current)
            {
                if (error)
                    *error = "Serialized value pointer parent does not exist";
                return false;
            }
            continue;
        }
        if (current->kind == VansSerializedValue::Kind::Array)
        {
            std::size_t index = 0;
            if (!TryParseSerializedArrayIndex(token, index) || index >= current->arrayItems.size())
            {
                if (error)
                    *error = "Invalid serialized value array index";
                return false;
            }
            current = &current->arrayItems[index];
            continue;
        }
        if (error)
            *error = "Serialized value pointer parent is not a container";
        return false;
    }

    const std::string& leaf = tokens.back();
    if (current->kind == VansSerializedValue::Kind::Object)
        return EraseSerializedObjectField(*current, leaf);
    if (current->kind == VansSerializedValue::Kind::Array)
    {
        std::size_t index = 0;
        if (!TryParseSerializedArrayIndex(leaf, index) || index >= current->arrayItems.size())
        {
            if (error)
                *error = "Invalid serialized value array index";
            return false;
        }
        current->arrayItems.erase(current->arrayItems.begin() + static_cast<std::ptrdiff_t>(index));
        return true;
    }

    if (error)
        *error = "Serialized value pointer parent is not a container";
    return false;
}

inline bool ReadSerializedBool(const VansSerializedValue& value, bool fallback = false)
{
    return value.kind == VansSerializedValue::Kind::Bool ? value.boolValue : fallback;
}

inline bool ReadSerializedBoolField(
    const VansSerializedValue& object,
    const std::string& name,
    bool fallback = false)
{
    const VansSerializedValue* field = FindObjectField(object, name);
    return field ? ReadSerializedBool(*field, fallback) : fallback;
}

inline std::int64_t ReadSerializedInt(
    const VansSerializedValue& value,
    std::int64_t fallback = 0)
{
    return value.kind == VansSerializedValue::Kind::Int ? value.intValue : fallback;
}

inline std::int64_t ReadSerializedIntField(
    const VansSerializedValue& object,
    const std::string& name,
    std::int64_t fallback = 0)
{
    const VansSerializedValue* field = FindObjectField(object, name);
    return field ? ReadSerializedInt(*field, fallback) : fallback;
}

inline double ReadSerializedNumber(
    const VansSerializedValue& value,
    double fallback = 0.0)
{
    if (value.kind == VansSerializedValue::Kind::Float)
        return value.floatValue;
    if (value.kind == VansSerializedValue::Kind::Int)
        return static_cast<double>(value.intValue);
    return fallback;
}

inline std::string ReadSerializedString(
    const VansSerializedValue& value,
    std::string fallback = {})
{
    return value.kind == VansSerializedValue::Kind::String ? value.stringValue : std::move(fallback);
}

inline std::string ReadSerializedStringField(
    const VansSerializedValue& object,
    const std::string& name,
    std::string fallback = {})
{
    const VansSerializedValue* field = FindObjectField(object, name);
    return field ? ReadSerializedString(*field, std::move(fallback)) : std::move(fallback);
}
}
