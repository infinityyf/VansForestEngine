#include "VansScriptUIComponentReader.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <utility>

namespace
{
void ReadStringArrayField(
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
		if (value.kind == Vans::VansSerializedValue::Kind::String && !value.stringValue.empty())
			outValues.push_back(value.stringValue);
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
	ReadStringArrayField(uiData, "autoOpen", descriptor.autoOpenScreens);
	ReadStringArrayField(uiData, "preload", descriptor.preloadScreens);
	outDescriptor = std::move(descriptor);
	return !outDescriptor.autoOpenScreens.empty() || !outDescriptor.preloadScreens.empty();
}
