#pragma once

#include "VansPythonSerializedField.h"

#include <string>
#include <unordered_map>
#include <vector>

struct VansPythonScriptComponentDescriptor
{
	std::string componentGuid;
	std::string scriptPath;
	std::string scriptClassName;
	std::unordered_map<std::string, VansPythonSerializedFieldValue> serializedFields;
};

using VansPythonScriptComponentDescriptors = std::vector<VansPythonScriptComponentDescriptor>;
