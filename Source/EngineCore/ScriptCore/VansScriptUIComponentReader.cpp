#include "VansScriptUIComponentReader.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <utility>

namespace
{
void ReadAssetGuidArrayField(
	const Vans::VansSerializedValue& object,
	const char* key,
	std::vector<std::string>& outValues)
{
	outValues.clear();
	const Vans::VansSerializedValue* values = Vans::FindObjectField(object, key);
	if (!values || values->kind != Vans::VansSerializedValue::Kind::Array)
		return;

	for (const Vans::VansSerializedValue& value : values->arrayItems)
	{
		if (value.kind != Vans::VansSerializedValue::Kind::Object)
			continue;
		const std::string guid = Vans::ReadSerializedStringField(value, "guid");
		if (!guid.empty()) outValues.push_back(guid);
	}
}
}

bool VansScriptUIComponentReader::TryReadUIComponent(
	const Vans::VansSerializedValue& uiData,
	const std::string& componentGuid,
	bool enabled,
	VansScriptUIComponentDescriptor& outDescriptor)
{
	if (uiData.kind != Vans::VansSerializedValue::Kind::Object)
		return false;

	VansScriptUIComponentDescriptor descriptor;
	descriptor.componentGuid = componentGuid;
	descriptor.enabled = enabled;
	ReadAssetGuidArrayField(uiData, "autoOpen", descriptor.autoOpenScreenAssetGuids);
	ReadAssetGuidArrayField(uiData, "preload", descriptor.preloadScreenAssetGuids);
	outDescriptor = std::move(descriptor);
	return !outDescriptor.autoOpenScreenAssetGuids.empty() || !outDescriptor.preloadScreenAssetGuids.empty();
}
