#include "VansPythonScriptComponentReader.h"

#include "../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <cstdint>
#include <utility>

namespace
{
VansPythonSerializedFieldValue DecodeFieldValue(const Vans::VansSerializedValue& value)
{
	VansPythonSerializedFieldValue field;
	if (value.kind == Vans::VansSerializedValue::Kind::Bool)
	{
		field.type = VansPythonSerializedFieldType::Bool;
		field.boolValue = value.boolValue;
	}
	else if (value.kind == Vans::VansSerializedValue::Kind::Int)
	{
		field.type = VansPythonSerializedFieldType::Int;
		field.intValue = value.intValue;
	}
	else if (value.kind == Vans::VansSerializedValue::Kind::Float)
	{
		field.type = VansPythonSerializedFieldType::Float;
		field.floatValue = value.floatValue;
	}
	else if (value.kind == Vans::VansSerializedValue::Kind::String)
	{
		field.type = VansPythonSerializedFieldType::String;
		field.stringValue = value.stringValue;
	}
	else if (value.kind == Vans::VansSerializedValue::Kind::Object)
	{
		Vans::SerializedObjectReferenceValue reference;
		if (Vans::TryReadSerializedObjectReference(value, reference) &&
			Vans::HasSerializedObjectReferenceTarget(reference))
		{
			field.type = VansPythonSerializedFieldType::ObjectReference;
			field.objectReference = std::move(reference);
		}
	}
	return field;
}

void DecodeSerializedFields(
	VansPythonScriptComponentDescriptor& descriptor,
	const Vans::VansSerializedValue& scriptEntry)
{
	descriptor.serializedFields.clear();
	const Vans::VansSerializedValue* fields = Vans::FindObjectField(scriptEntry, "fields");
	if (!fields || fields->kind != Vans::VansSerializedValue::Kind::Object)
	{
		return;
	}

	for (const auto& [fieldName, fieldValue] : fields->objectFields)
	{
		descriptor.serializedFields[fieldName] = DecodeFieldValue(fieldValue);
	}
}
}

bool VansPythonScriptComponentReader::TryReadScriptComponent(
	const Vans::VansSerializedValue& scriptData,
	const std::string& componentGuid,
	VansPythonScriptComponentDescriptor& outDescriptor)
{
	if (scriptData.kind != Vans::VansSerializedValue::Kind::Object)
	{
		return false;
	}

	const std::string path = Vans::ReadSerializedStringField(scriptData, "path");
	const std::string className = Vans::ReadSerializedStringField(scriptData, "class");
	if (path.empty() || className.empty())
	{
		return false;
	}

	VansPythonScriptComponentDescriptor descriptor;
	descriptor.componentGuid = componentGuid;
	descriptor.scriptPath = path;
	descriptor.scriptClassName = className;
	DecodeSerializedFields(descriptor, scriptData);
	outDescriptor = std::move(descriptor);
	return true;
}

VansPythonScriptComponentDescriptors VansPythonScriptComponentReader::ReadObjectScripts(
	const Vans::VansSerializedValue& objectValue)
{
	VansPythonScriptComponentDescriptors descriptors;
	const Vans::VansSerializedValue* scripts = Vans::FindObjectField(objectValue, "pyScripts");
	if (!scripts || scripts->kind != Vans::VansSerializedValue::Kind::Array)
	{
		return descriptors;
	}

	descriptors.reserve(scripts->arrayItems.size());
	for (const Vans::VansSerializedValue& scriptEntry : scripts->arrayItems)
	{
		VansPythonScriptComponentDescriptor descriptor;
		const std::string componentGuid = Vans::ReadSerializedStringField(scriptEntry, "componentGuid");
		if (TryReadScriptComponent(scriptEntry, componentGuid, descriptor))
		{
			descriptors.push_back(std::move(descriptor));
		}
	}
	return descriptors;
}
