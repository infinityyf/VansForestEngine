#pragma once

#include "VansPythonScriptComponentDescriptor.h"

#include <string>

namespace Vans
{
struct VansSerializedValue;
}

class VansPythonScriptComponentReader
{
public:
	static bool TryReadScriptComponent(
		const Vans::VansSerializedValue& scriptData,
		const std::string& componentGuid,
		VansPythonScriptComponentDescriptor& outDescriptor);

	static VansPythonScriptComponentDescriptors ReadObjectScripts(
		const Vans::VansSerializedValue& objectValue);
};
