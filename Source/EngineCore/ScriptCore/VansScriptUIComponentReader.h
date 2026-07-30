#pragma once

#include "VansScriptTypes.h"

namespace Vans
{
struct VansSerializedValue;
}

class VansScriptUIComponentReader
{
public:
	static bool TryReadUIComponent(
		const Vans::VansSerializedValue& uiData,
		const std::string& componentGuid,
		bool enabled,
		VansScriptUIComponentDescriptor& outDescriptor);
};
