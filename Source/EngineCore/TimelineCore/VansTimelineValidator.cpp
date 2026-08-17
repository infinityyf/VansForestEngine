#include "VansTimelineValidator.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>

namespace Vans
{
namespace
{
void Add(
	VansTimelineDiagnostics& diagnostics,
	VansTimelineDiagnosticSeverity severity,
	std::string code,
	std::string objectId,
	std::string property,
	std::string message)
{
	diagnostics.push_back({ severity, std::move(code), {}, std::move(objectId),
		std::move(property), std::move(message) });
}

bool ValidRange(const VansTimelineTickRange& range, VansTimelineTick duration)
{
	return range.startTick >= 0 && range.endTick >= range.startTick && range.endTick <= duration;
}
}

VansTimelineDiagnostics VansTimelineValidator::Validate(
	const VansTimelineAsset& asset,
	const VansTimelineValidationContext& context)
{
	VansTimelineDiagnostics diagnostics;
	if (asset.assetKind != "Timeline")
		Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.AssetKindInvalid", {}, "assetKind", "Timeline assetKind must be Timeline");
	if (!context.extensions || !context.extensions->IsSealed())
		Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.TrackRegistryUnavailable", {}, {}, "Timeline track extension registry must be sealed before validation");
	if (asset.timebase.ticksPerSecond <= 0 || asset.timebase.displayRateNumerator <= 0 || asset.timebase.displayRateDenominator <= 0)
		Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.TimebaseInvalid", {}, "timebase", "Timeline timebase values must be positive");
	if (asset.durationTicks <= 0)
		Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.DurationInvalid", {}, "durationTicks", "Timeline duration must be positive");
	if (!ValidRange(asset.playbackRange, asset.durationTicks))
		Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.PlaybackRangeInvalid", {}, "playbackRange", "Timeline playback range is outside the asset duration");
	if (!ValidRange(asset.workRange, asset.durationTicks))
		Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.WorkRangeInvalid", {}, "workRange", "Timeline work range is outside the asset duration");

	std::unordered_set<VansTimelineId> ids;
	auto uniqueId = [&](const VansTimelineId& id, const char* kind)
	{
		if (id.empty()) Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.StableIdMissing", {}, kind, "Timeline object ID is empty");
		else if (!ids.insert(id).second) Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.StableIdDuplicate", id, "id", "Timeline object ID is duplicated");
	};

	std::unordered_set<VansTimelineParameterId> parameterIds;
	for (const auto& parameter : asset.parameters)
	{
		if (!parameter.id || !parameterIds.insert(parameter.id).second)
			Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.ParameterIdInvalid", parameter.name, "id", "Timeline parameter ID is missing or duplicated");
		if (VansTimelineTypeOf(parameter.defaultValue) != parameter.type &&
			!(parameter.type == VansTimelineValueType::Enum && VansTimelineTypeOf(parameter.defaultValue) == VansTimelineValueType::String))
			Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.ParameterTypeMismatch", parameter.name, "defaultValue", "Timeline parameter default value does not match its declared type");
	}

	std::unordered_map<VansTimelineId, const VansTimelineBinding*> bindings;
	for (const auto& binding : asset.bindings)
	{
		uniqueId(binding.id, "binding");
		if (!binding.stableId || binding.stableId != VansMakeStableId<VansTimelineBindingTag>(binding.id))
			Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.BindingIdMismatch", binding.id, "id", "Timeline binding stable ID does not match its authoring ID");
		bindings.emplace(binding.id, &binding);
	}

	std::unordered_set<VansTimelineId> groups;
	for (const auto& group : asset.groups) { uniqueId(group.id, "group"); groups.insert(group.id); }
	for (const auto& group : asset.groups)
		if (!group.parentId.empty() && groups.find(group.parentId) == groups.end())
			Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.GroupParentMissing", group.id, "parentId", "Timeline group parent does not exist");

	for (const auto& track : asset.tracks)
	{
		uniqueId(track.id, "track");
		if (!track.type.typeId || track.type.stableName.empty() ||
			track.type.typeId != VansMakeStableId<VansTimelineTrackTypeTag>(track.type.stableName))
		{
			Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.TrackTypeHashMismatch", track.id, "type", "Timeline track type ID does not match its canonical stable name");
			continue;
		}
		const VansTimelineTrackExtensionDescriptor* descriptor = context.extensions ? context.extensions->Resolve(track.type.typeId) : nullptr;
		if (!descriptor)
		{
			Add(diagnostics, context.runtimeValidation ? VansTimelineDiagnosticSeverity::Error : VansTimelineDiagnosticSeverity::Warning,
				"Timeline.TrackExtensionMissing", track.id, "type", "Timeline track extension is not registered: " + track.type.stableName);
			continue;
		}
		if (descriptor->binding == VansTimelineBindingRequirement::Required && track.bindingId.empty())
			Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.BindingMissing", track.id, "bindingId", "Timeline track requires a binding");
		if (!track.bindingId.empty() && bindings.find(track.bindingId) == bindings.end())
			Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.BindingMissing", track.id, "bindingId", "Timeline track binding does not exist");
		if (!track.groupId.empty() && groups.find(track.groupId) == groups.end())
			Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.GroupMissing", track.id, "groupId", "Timeline track group does not exist");
		if (track.condition.parameterId && parameterIds.find(track.condition.parameterId) == parameterIds.end())
			Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.ParameterMissing", track.id, "condition.parameterId", "Timeline track condition references an unknown parameter");
		if (context.rollbackCapable && !VansHasTimelineFlag(descriptor->flags, VansTimelineTrackFlags::Reversible))
			Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.NonReversiblePath", track.id, "type", "Rollback-capable Timeline contains a non-reversible extension");
		if (context.preview && VansHasTimelineFlag(descriptor->flags, VansTimelineTrackFlags::Destructive) &&
			!VansHasTimelineFlag(descriptor->flags, VansTimelineTrackFlags::PreviewSafeByDefault))
			Add(diagnostics, VansTimelineDiagnosticSeverity::Warning, "Timeline.PreviewDestructiveTrack", track.id, "type", "Destructive Timeline extension is disabled in preview");
		if (descriptor->validate) descriptor->validate(track, descriptor->sourceSchema, context, diagnostics);
		else VansValidateTimelineExtensionSchema(track, descriptor->sourceSchema, diagnostics);
		if (context.runtimeValidation && context.hasOutputApplier)
			for (const VansTimelineOutputDeclaration& output : descriptor->outputs)
				if (output.applierRequired && !context.hasOutputApplier(output.typeId))
					Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.ApplierMissing",
						track.id, "type", "Timeline track output has no registered runtime applier");

		for (const auto& section : track.sections)
		{
			uniqueId(section.id, "section");
			if (section.durationTicks <= 0 || section.startTick < 0 || section.startTick + section.durationTicks > asset.durationTicks)
				Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.SectionRangeInvalid", section.id, "durationTicks", "Timeline section is outside the asset duration");
			if (!VansHasTimelineFlag(descriptor->flags, VansTimelineTrackFlags::SupportsSections))
				Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.SectionsUnsupported", section.id, {}, "Timeline extension does not support sections");
			if (section.reverse && !VansHasTimelineFlag(descriptor->flags, VansTimelineTrackFlags::SupportsReverse))
				Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.ReverseUnsupported", section.id,
					"reverse", "Timeline extension does not support reversed section playback");
			for (const auto& channel : section.channels)
			{
				uniqueId(channel.id, "channel");
				if (!VansHasTimelineFlag(descriptor->flags, VansTimelineTrackFlags::SupportsChannels))
					Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.ChannelsUnsupported", channel.id, {}, "Timeline extension does not support channels");
				VansTimelineTick previous = -1;
				for (const auto& key : channel.keys)
				{
					uniqueId(key.id, "key");
					if (key.tick < 0 || key.tick >= section.durationTicks || key.tick == previous)
						Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.KeyTickInvalid", key.id, "tick", "Timeline key tick is outside the section or duplicated");
					if (VansTimelineTypeOf(key.value) != channel.type &&
						!(channel.type == VansTimelineValueType::Enum && VansTimelineTypeOf(key.value) == VansTimelineValueType::String))
						Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.KeyTypeMismatch", key.id, "value", "Timeline key value does not match the channel type");
					previous = key.tick;
				}
			}
			for (const auto& range : section.ranges)
			{
				uniqueId(range.id, "range");
				if (range.startTick < 0 || range.endTick <= range.startTick || range.endTick > section.durationTicks)
					Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.RangeInvalid", range.id, {}, "Timeline range bounds are invalid");
			}
		}
	}

	for (const auto& marker : asset.markers)
	{
		uniqueId(marker.id, "marker");
		if (marker.tick < 0 || marker.tick > asset.durationTicks)
			Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.MarkerTickInvalid", marker.id, "tick", "Timeline marker is outside the asset duration");
		if (marker.runtimeObservable && !marker.payloadType)
			Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.PayloadSchemaMissing", marker.id, "payloadType", "Observable Timeline marker requires a payload schema");
		if (marker.runtimeObservable && marker.payloadType && context.hasPayloadSchema && !context.hasPayloadSchema(marker.payloadType))
			Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.PayloadSchemaMissing", marker.id, "payloadType", "Timeline marker payload schema is not registered");
		if (marker.runtimeObservable && marker.payloadType && context.validatePayload)
		{
			std::string error;
			if (!context.validatePayload(marker.payloadType, marker.payload, error))
				Add(diagnostics, VansTimelineDiagnosticSeverity::Error, "Timeline.MarkerPayloadInvalid",
					marker.id, "payload", error.empty() ? "Timeline marker payload is invalid" : error);
		}
	}
	return diagnostics;
}

bool VansTimelineValidator::HasErrors(const VansTimelineDiagnostics& diagnostics)
{
	return std::any_of(diagnostics.begin(), diagnostics.end(), [](const auto& diagnostic)
	{
		return diagnostic.severity == VansTimelineDiagnosticSeverity::Error;
	});
}
}
