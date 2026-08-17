#include "VansTimelineTrackExtension.h"

#include "../AssetCore/Serialization/VansSerializedValueAccess.h"

#include <algorithm>
#include <unordered_set>

namespace Vans
{
namespace
{
VansSerializedValue MergeTimelineExtensionData(
	const VansSerializedValue& base,
	const VansSerializedValue& overrides)
{
	if (base.kind != VansSerializedValue::Kind::Object ||
		overrides.kind != VansSerializedValue::Kind::Object) return overrides;
	VansSerializedValue result = base;
	for (const auto& [name, value] : overrides.objectFields)
	{
		auto found = std::find_if(result.objectFields.begin(), result.objectFields.end(),
			[&](const auto& field) { return field.first == name; });
		if (found == result.objectFields.end()) result.objectFields.emplace_back(name, value);
		else found->second = value;
	}
	return result;
}
}

void VansValidateTimelineExtensionSchema(
	const VansTimelineTrack& track,
	const VansTimelineSourceSchema& schema,
	VansTimelineDiagnostics& diagnostics)
{
	VansTimelineCompiledDataWriter writer;
	VansTimelineCompiledDataView ignored;
	writer.WriteSchema(schema, track.extensionData, ignored, diagnostics, track.id);
	std::unordered_set<std::string> allowedFields;
	for (const auto& field : schema.fields) allowedFields.insert(field.name);
	auto validateFields = [&](const VansSerializedValue& data, const VansTimelineId& objectId)
	{
		if (schema.allowAdditionalFields || data.kind != VansSerializedValue::Kind::Object) return;
		for (const auto& [name, value] : data.objectFields)
		{
			(void)value;
			if (allowedFields.find(name) == allowedFields.end())
				diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
					"Timeline.UnknownSourceField", {}, objectId, name,
					"Timeline extension field is not declared by its source schema" });
		}
	};
	validateFields(track.extensionData, track.id);
	for (const VansTimelineSection& section : track.sections)
	{
		const VansSerializedValue effectiveData = section.extensionData
			? MergeTimelineExtensionData(track.extensionData, *section.extensionData)
			: track.extensionData;
		if (section.extensionData)
		{
			validateFields(*section.extensionData, section.id);
			writer.WriteSchema(schema, effectiveData, ignored, diagnostics, section.id);
		}
		std::unordered_set<std::string> channelNames;
		for (const VansTimelineChannel& channel : section.channels)
		{
			if (!channelNames.insert(channel.name).second)
				diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
					"Timeline.ChannelNameDuplicate", {}, channel.id, "name",
					"Timeline section contains duplicate channel names" });
			bool declared = false;
			for (const VansTimelineChannelSchema& expected : schema.channels)
				if (expected.name == channel.name &&
					VansTimelineResolveChannelType(expected, effectiveData) == channel.type)
				{ declared = true; break; }
			if (!declared && !schema.allowAdditionalChannels)
					diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
						"Timeline.ChannelSchemaMismatch", {}, channel.id, "name",
						"Timeline channel is not declared by its extension schema" });
		}
		for (const VansTimelineChannelSchema& expected : schema.channels)
			if (expected.required && channelNames.find(expected.name) == channelNames.end())
				diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
					"Timeline.RequiredChannelMissing", {}, section.id, expected.name,
					"Timeline section is missing a required channel" });
	}
}

bool VansCompileTimelineExtensionSchema(
	const VansTimelineExtensionCompileContext& context,
	const VansTimelineTrack& track,
	const VansTimelineSourceSchema& schema,
	VansTimelineCompiledDataWriter& writer,
	VansTimelineCompiledDataView& trackData,
	std::vector<VansTimelineCompiledDataView>& sectionData,
	VansTimelineDiagnostics& diagnostics)
{
	(void)context;
	bool valid = writer.WriteSchema(schema, track.extensionData, trackData, diagnostics, track.id);
	sectionData.clear();
	sectionData.reserve(track.sections.size());
	for (const VansTimelineSection& section : track.sections)
	{
		VansTimelineCompiledDataView view = trackData;
		if (section.extensionData)
		{
			const VansSerializedValue merged = MergeTimelineExtensionData(
				track.extensionData, *section.extensionData);
			valid = writer.WriteSchema(schema, merged, view, diagnostics, section.id) && valid;
		}
		sectionData.push_back(view);
	}
	return valid;
}
}
