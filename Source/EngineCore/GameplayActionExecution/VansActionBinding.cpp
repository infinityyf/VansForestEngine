#include "VansActionBinding.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <unordered_map>

namespace Vans
{
namespace
{
bool DecodeValueType(std::string_view name, VansActionBindingValueType& type)
{
	static const std::unordered_map<std::string_view, VansActionBindingValueType> Types{
		{ "Any", VansActionBindingValueType::Any },
		{ "Bool", VansActionBindingValueType::Bool },
		{ "Int", VansActionBindingValueType::Int },
		{ "Float", VansActionBindingValueType::Float },
		{ "String", VansActionBindingValueType::String },
		{ "Object", VansActionBindingValueType::Object },
		{ "Array", VansActionBindingValueType::Array },
		{ "Entity", VansActionBindingValueType::Entity },
		{ "TargetData", VansActionBindingValueType::TargetData },
		{ "Resource", VansActionBindingValueType::Resource }
	};
	const auto found = Types.find(name);
	if (found == Types.end()) return false;
	type = found->second;
	return true;
}

VansSerializedValue HandleValue(VansGenerationHandle handle)
{
	return VansSerializedValue::Object({
		{ "index", VansSerializedValue::Int(handle.index) },
		{ "generation", VansSerializedValue::Int(handle.generation) }
	});
}

bool ContextValue(
	const VansActionValue& source,
	VansSerializedValue& value)
{
	switch (source.kind)
	{
	case VansActionValueKind::Serialized:
		value = source.serialized;
		return true;
	case VansActionValueKind::Entity:
		value = HandleValue({ source.entity.index, source.entity.generation });
		return true;
	case VansActionValueKind::TargetData:
		value = HandleValue(source.targetData.value);
		return true;
	case VansActionValueKind::Resource:
		value = HandleValue(source.resource);
		return true;
	}
	return false;
}
}

bool VansActionBindingValueMatches(
	VansActionBindingValueType type,
	const VansSerializedValue& value)
{
	switch (type)
	{
	case VansActionBindingValueType::Any: return true;
	case VansActionBindingValueType::Bool: return value.kind == VansSerializedValue::Kind::Bool;
	case VansActionBindingValueType::Int: return value.kind == VansSerializedValue::Kind::Int;
	case VansActionBindingValueType::Float:
		return value.kind == VansSerializedValue::Kind::Float ||
			value.kind == VansSerializedValue::Kind::Int;
	case VansActionBindingValueType::String: return value.kind == VansSerializedValue::Kind::String;
	case VansActionBindingValueType::Array: return value.kind == VansSerializedValue::Kind::Array;
	case VansActionBindingValueType::Object:
	case VansActionBindingValueType::Entity:
	case VansActionBindingValueType::TargetData:
	case VansActionBindingValueType::Resource:
		return value.kind == VansSerializedValue::Kind::Object;
	}
	return false;
}

bool VansDecodeActionInputBinding(
	const VansSerializedValue& source,
	VansActionInputBinding& binding,
	std::string& error)
{
	if (source.kind != VansSerializedValue::Kind::Object)
	{
		error = "Action input binding must be an object";
		return false;
	}
	const std::string sourceName = ReadSerializedStringField(source, "source");
	if (sourceName == "Literal") binding.source = VansActionBindingSource::Literal;
	else if (sourceName == "Context") binding.source = VansActionBindingSource::Context;
	else if (sourceName == "Variable") binding.source = VansActionBindingSource::Variable;
	else
	{
		error = "Action input binding source is invalid";
		return false;
	}
	const std::string typeName = ReadSerializedStringField(source, "type", "Any");
	binding.path.clear();
	if (const auto* path = FindObjectField(source, "path"))
	{
		if (path->kind != VansSerializedValue::Kind::Array) { error = "Action binding path must be an array"; return false; }
		for (const auto& field : path->arrayItems)
		{
			if (field.kind != VansSerializedValue::Kind::String || field.stringValue.empty())
			{ error = "Action binding path fields must be nonempty strings"; return false; }
			binding.path.push_back(field.stringValue);
		}
	}
	if (!DecodeValueType(typeName, binding.type))
	{
		error = "Action input binding type is invalid";
		return false;
	}
	if (binding.source == VansActionBindingSource::Literal)
	{
		const VansSerializedValue* literal = FindObjectField(source, "value");
		if (!literal)
		{
			error = "Literal Action input binding is missing value";
			return false;
		}
		binding.literal = *literal;
	}
	else
	{
		binding.name = ReadSerializedStringField(source,
			binding.source == VansActionBindingSource::Context ? "slot" : "name");
		if (binding.name.empty())
		{
			error = "Action input binding reference is empty";
			return false;
		}
	}
	return true;
}

bool VansDecodeActionOutputBinding(
	const VansSerializedValue& source,
	VansActionOutputBinding& binding,
	std::string& error)
{
	if (source.kind != VansSerializedValue::Kind::Object ||
		ReadSerializedStringField(source, "target") != "Variable")
	{
		error = "Action output binding target must be Variable";
		return false;
	}
	binding.name = ReadSerializedStringField(source, "name");
	const std::string typeName = ReadSerializedStringField(source, "type", "Any");
	if (binding.name.empty() || !DecodeValueType(typeName, binding.type))
	{
		error = "Action output binding name or type is invalid";
		return false;
	}
	return true;
}

bool VansResolveActionInputBinding(
	const VansActionInputBinding& binding,
	const VansActionExecutionContext& context,
	VansSerializedValue& value,
	std::string& error)
{
	if (binding.source == VansActionBindingSource::Literal) value = binding.literal;
	else if (binding.source == VansActionBindingSource::Variable)
	{
		const VansSerializedValue* found = context.variables
			? context.variables->Get(VansMakeStableId<VansActionFieldIdTag>(binding.name)) : nullptr;
		if (!found)
		{
			error = "Action input variable is unavailable: " + binding.name;
			return false;
		}
		value = *found;
	}
	else
	{
		const VansActionValue* found = context.context ? context.context->Find(binding.name) : nullptr;
		if (!found || !ContextValue(*found, value))
		{
			error = "Action context slot is unavailable: " + binding.name;
			return false;
		}
	}
	for (const auto& field : binding.path)
	{
		const auto* nested = FindObjectField(value, field);
		if (!nested) { error = "Action binding object field is unavailable: " + field; return false; }
		VansSerializedValue copy = *nested;
		value = std::move(copy);
	}
	if (!VansActionBindingValueMatches(binding.type, value))
	{
		error = "Action input binding type does not match its value: " + binding.name;
		return false;
	}
	return true;
}

bool VansWriteActionOutputBinding(
	const VansActionOutputBinding& binding,
	VansSerializedValue value,
	VansActionExecutionContext& context,
	std::string& error)
{
	if (!VansActionBindingValueMatches(binding.type, value))
	{
		error = "Action output binding type does not match its value: " + binding.name;
		return false;
	}
	if (!context.variables || !context.variables->Set(
		VansMakeStableId<VansActionFieldIdTag>(binding.name), std::move(value)))
	{
		error = "Action output variable is unavailable: " + binding.name;
		return false;
	}
	return true;
}
}
