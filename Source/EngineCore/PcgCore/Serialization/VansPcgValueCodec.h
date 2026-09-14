#pragma once

#include "../../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../../AssetCore/Serialization/VansSerializedValueAccess.h"
#include "../../AssetCore/VansAssetGuid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <unordered_set>

namespace Vans::PcgValue
{
// PCG 作者文档严格读取唯一字段集合。遗漏、未知字段和错误类型都给出完整路径。
class Reader
{
public:
	Reader(const VansSerializedValue* value, std::string path, std::string& error)
		: m_Value(value), m_Path(std::move(path)), m_Error(error)
	{
		if (!m_Value || m_Value->kind != VansSerializedValue::Kind::Object) Fail("must be an object");
	}
	const VansSerializedValue* Field(const char* name)
	{
		m_Read.insert(name);
		const auto* field = m_Value ? FindObjectField(*m_Value, name) : nullptr;
		if (!field) Fail(std::string("is missing '") + name + "'");
		return field;
	}
	Reader Object(const char* name) { return Reader(Field(name), m_Path + "." + name, m_Error); }
	void String(const char* name, std::string& output)
	{
		const auto* field = Field(name);
		if (!field || field->kind != VansSerializedValue::Kind::String) { Fail(std::string(name) + " must be a string"); return; }
		output = field->stringValue;
	}
	void Float(const char* name, float& output)
	{
		const auto* field = Field(name);
		if (!field || (field->kind != VansSerializedValue::Kind::Float && field->kind != VansSerializedValue::Kind::Int))
		{ Fail(std::string(name) + " must be a number"); return; }
		output = static_cast<float>(ReadSerializedNumber(*field));
		if (!std::isfinite(output)) Fail(std::string(name) + " must be finite");
	}
	void Bool(const char* name, bool& output)
	{
		const auto* field = Field(name);
		if (!field || field->kind != VansSerializedValue::Kind::Bool) { Fail(std::string(name) + " must be a boolean"); return; }
		output = field->boolValue;
	}
	template <typename Integer> void IntegerField(const char* name, Integer& output)
	{
		const auto* field = Field(name);
		if (!field || field->kind != VansSerializedValue::Kind::Int ||
			static_cast<long double>(field->intValue) < std::numeric_limits<Integer>::lowest() ||
			static_cast<long double>(field->intValue) > std::numeric_limits<Integer>::max())
		{ Fail(std::string(name) + " must be an integer in range"); return; }
		output = static_cast<Integer>(field->intValue);
	}
	template <std::size_t N> void Vector(const char* name, std::array<float, N>& output)
	{
		const auto* field = Field(name);
		if (!field || field->kind != VansSerializedValue::Kind::Array || field->arrayItems.size() != N)
		{ Fail(std::string(name) + " has the wrong vector dimension"); return; }
		for (std::size_t i = 0; i < N; ++i)
		{
			const auto& component = field->arrayItems[i];
			if (component.kind != VansSerializedValue::Kind::Float && component.kind != VansSerializedValue::Kind::Int)
			{ Fail(std::string(name) + " requires numeric vector components"); return; }
			output[i] = static_cast<float>(ReadSerializedNumber(component));
			if (!std::isfinite(output[i])) Fail(std::string(name) + " requires finite vector components");
		}
	}
	void Reference(const char* name, const char* type, VansAssetGuid& output)
	{
		const auto* field = Field(name);
		if (!field || field->kind == VansSerializedValue::Kind::Null) { output = {}; return; }
		SerializedObjectReferenceValue reference;
		if (!TryReadSerializedObjectReference(*field, reference) || reference.domain != "ProjectAsset" ||
			reference.assetType != type || !VansAssetGuid::TryParse(reference.guid, output) || !output.IsValid())
			Fail(std::string(name) + " requires a typed ProjectAsset reference to " + type);
	}
	template <typename Enum> void EnumField(const char* name, Enum& output,
		std::initializer_list<std::pair<const char*, Enum>> choices)
	{
		std::string value;
		String(name, value);
		for (const auto& choice : choices) if (value == choice.first) { output = choice.second; return; }
		Fail(std::string(name) + " has an unknown value '" + value + "'");
	}
	const std::vector<VansSerializedValue>* Array(const char* name)
	{
		const auto* field = Field(name);
		if (!field || field->kind != VansSerializedValue::Kind::Array) { Fail(std::string(name) + " must be an array"); return nullptr; }
		return &field->arrayItems;
	}
	bool Finish()
	{
		std::unordered_set<std::string> seen;
		if (m_Value) for (const auto& field : m_Value->objectFields)
		{
			if (!seen.insert(field.first).second) Fail("duplicate field '" + field.first + "'");
			if (!m_Read.count(field.first)) Fail("unknown field '" + field.first + "'");
		}
		return m_Error.empty();
	}
private:
	void Fail(const std::string& message) { if (m_Error.empty()) m_Error = m_Path + ": " + message; }
	const VansSerializedValue* m_Value;
	std::string m_Path;
	std::string& m_Error;
	std::unordered_set<std::string> m_Read;
};

template <std::size_t N> VansSerializedValue Vector(const std::array<float, N>& value)
{
	std::vector<VansSerializedValue> items;
	for (float component : value) items.push_back(VansSerializedValue::Float(component));
	return VansSerializedValue::Array(std::move(items));
}

inline VansSerializedValue Reference(VansAssetGuid guid, const char* type)
{
	return guid.IsValid() ? MakeSerializedProjectAssetObjectReference(guid.ToString(), type) : VansSerializedValue::Null();
}
}
