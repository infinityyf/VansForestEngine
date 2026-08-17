#pragma once

#include "VansTimelineAsset.h"

#include <string>
#include <vector>

namespace Vans
{
struct VansTimelineSourceField
{
	VansTimelineFieldId id;
	std::string name;
	VansTimelineValueType type = VansTimelineValueType::Null;
	VansTimelineValue defaultValue;
	bool required = false;
	std::vector<std::string> enumValues;
};

struct VansTimelineChannelSchema
{
	std::string name;
	VansTimelineValueType type = VansTimelineValueType::Float;
	bool required = false;
	// Optional enum/string source field whose value is a canonical Timeline
	// value-type name. typeCases lets a domain expose meaningful names such as
	// Trigger while still resolving to a concrete Timeline value type.
	std::string typeField;
	std::vector<std::pair<std::string, VansTimelineValueType>> typeCases;
};

struct VansTimelineSourceSchema
{
	std::vector<VansTimelineSourceField> fields;
	std::vector<VansTimelineChannelSchema> channels;
	bool allowAdditionalFields = false;
	bool allowAdditionalChannels = false;
};

const VansSerializedValue* VansTimelineFindSourceField(
	const VansSerializedValue& object,
	const std::string& name);
bool VansTimelineDecodeSourceValue(
	const VansSerializedValue& source,
	VansTimelineValueType type,
	VansTimelineValue& value);
VansSerializedValue VansTimelineEncodeSourceValue(const VansTimelineValue& value);
VansTimelineValueType VansTimelineResolveChannelType(
	const VansTimelineChannelSchema& channel,
	const VansSerializedValue& extensionData);
}
