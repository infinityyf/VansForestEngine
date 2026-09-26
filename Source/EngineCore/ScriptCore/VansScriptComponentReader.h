#pragma once

#include "VansScriptTypes.h"

namespace Vans
{
struct VansSerializedValue;
}

class VansScriptComponentReader
{
public:
	static bool CollectProjectAssetReferences(
		const Vans::VansSerializedValue& scriptData,
		std::vector<VansScriptSerializedObjectReference>& references,
		std::string& error);

	static bool TryReadScriptComponent(
		const Vans::VansSerializedValue& scriptData,
		const std::string& componentGuid,
		bool enabled,
		VansScriptComponentDescriptor& outDescriptor);
};
