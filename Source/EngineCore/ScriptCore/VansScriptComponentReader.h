#pragma once

#include "VansScriptTypes.h"

namespace Vans
{
struct VansSerializedValue;
}

class VansScriptComponentReader
{
public:
	static bool TryReadScriptComponent(
		const Vans::VansSerializedValue& scriptData,
		const std::string& componentGuid,
		bool enabled,
		VansScriptComponentDescriptor& outDescriptor);
};
