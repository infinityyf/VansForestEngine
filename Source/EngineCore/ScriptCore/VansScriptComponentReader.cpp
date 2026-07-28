#include "VansScriptComponentReader.h"

#include "../AssetCore/Serialization/VansSerializedObjectReference.h"
#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace
{
std::string Lower(std::string value)
{
	std::transform(value.begin(), value.end(), value.begin(),
		[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
}

VansScriptLanguage InferLanguage(const std::string& language, const std::string& path)
{
	(void)path;
	const std::string normalizedLanguage = Lower(language);
	(void)normalizedLanguage;
	return VansScriptLanguage::Lua;
}

VansScriptSerializedFieldValue DecodeFieldValue(const Vans::VansSerializedValue& value)
{
	VansScriptSerializedFieldValue field;
	if (value.kind == Vans::VansSerializedValue::Kind::Bool)
	{
		field.type = VansScriptSerializedFieldType::Bool;
		field.boolValue = value.boolValue;
	}
	else if (value.kind == Vans::VansSerializedValue::Kind::Int)
	{
		field.type = VansScriptSerializedFieldType::Int;
		field.intValue = value.intValue;
	}
	else if (value.kind == Vans::VansSerializedValue::Kind::Float)
	{
		field.type = VansScriptSerializedFieldType::Float;
		field.floatValue = value.floatValue;
	}
	else if (value.kind == Vans::VansSerializedValue::Kind::String)
	{
		field.type = VansScriptSerializedFieldType::String;
		field.stringValue = value.stringValue;
	}
	else if (value.kind == Vans::VansSerializedValue::Kind::Object)
	{
		Vans::SerializedObjectReferenceValue reference;
		if (Vans::TryReadSerializedObjectReference(value, reference) &&
			Vans::HasSerializedObjectReferenceTarget(reference))
		{
			field.type = VansScriptSerializedFieldType::ObjectReference;
			field.objectReference = std::move(reference);
		}
	}
	return field;
}

void DecodeSerializedFields(
	VansScriptComponentDescriptor& descriptor,
	const Vans::VansSerializedValue& scriptEntry)
{
	descriptor.serializedFields.clear();
	const Vans::VansSerializedValue* fields = Vans::FindObjectField(scriptEntry, "fields");
	if (!fields || fields->kind != Vans::VansSerializedValue::Kind::Object)
		return;

	for (const auto& [fieldName, fieldValue] : fields->objectFields)
		descriptor.serializedFields[fieldName] = DecodeFieldValue(fieldValue);
}
}

bool VansScriptComponentReader::TryReadScriptComponent(
	const Vans::VansSerializedValue& scriptData,
	const std::string& componentGuid,
	bool enabled,
	VansScriptComponentDescriptor& outDescriptor)
{
	if (scriptData.kind != Vans::VansSerializedValue::Kind::Object)
		return false;

	const std::string path = Vans::ReadSerializedStringField(scriptData, "path");
	std::string entry = Vans::ReadSerializedStringField(scriptData, "entry");
	if (entry.empty())
		entry = Vans::ReadSerializedStringField(scriptData, "class");
	if (path.empty() || entry.empty())
		return false;

	VansScriptComponentDescriptor descriptor;
	descriptor.componentGuid = componentGuid;
	descriptor.scriptPath = path;
	descriptor.entryName = entry;
	descriptor.enabled = enabled;
	descriptor.language = InferLanguage(Vans::ReadSerializedStringField(scriptData, "language"), path);
	DecodeSerializedFields(descriptor, scriptData);
	outDescriptor = std::move(descriptor);
	return true;
}
