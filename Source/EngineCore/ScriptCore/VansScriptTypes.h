#pragma once

#include "../AssetCore/Serialization/VansSerializedObjectReference.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

enum class VansScriptLanguage : std::uint8_t
{
	Lua
};

enum class VansScriptSerializedFieldType : std::uint8_t
{
	Null,
	Bool,
	Int,
	Float,
	String,
	ObjectReference
};

using VansScriptSerializedObjectReference = Vans::SerializedObjectReferenceValue;

struct VansScriptSerializedFieldValue
{
	VansScriptSerializedFieldType type = VansScriptSerializedFieldType::Null;
	bool boolValue = false;
	std::int64_t intValue = 0;
	double floatValue = 0.0;
	std::string stringValue;
	VansScriptSerializedObjectReference objectReference;
};

struct VansScriptComponentDescriptor
{
	std::string componentGuid;
	VansScriptLanguage language = VansScriptLanguage::Lua;
	std::string scriptPath;
	std::string entryName;
	bool enabled = true;
	std::unordered_map<std::string, VansScriptSerializedFieldValue> serializedFields;
};

using VansScriptComponentDescriptors = std::vector<VansScriptComponentDescriptor>;

struct VansScriptUIComponentDescriptor
{
	std::string componentGuid;
	bool enabled = true;
	std::vector<std::string> autoOpenScreenAssetGuids;
	std::vector<std::string> preloadScreenAssetGuids;
};

using VansScriptUIComponentDescriptors = std::vector<VansScriptUIComponentDescriptor>;
