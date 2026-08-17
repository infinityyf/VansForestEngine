#include "VansTimelineBuiltInRegistry.h"

#include "VansTimelineSampleExtension.h"
#include "VansTimelineEvaluator.h"
#include "../TimelineCore/VansTimelineCompiler.h"
#include "../TimelineCore/VansTimelineDependencyBuilder.h"
#include "../TimelineCore/VansTimelineTrackExtensionRegistry.h"
#include "../TimelineCore/VansTimelineValidator.h"

#include <algorithm>

namespace Vans
{
namespace
{
void CollectEventSignalDependencies(
	const VansTimelineTrack& track,
	std::vector<VansTimelineDependency>& dependencies)
{
	auto collect = [&](const VansSerializedValue& data, const VansTimelineId& sourceId)
	{
		const VansSerializedValue* encoded = VansTimelineFindSourceField(data, "payloadType");
		if (!encoded || encoded->kind != VansSerializedValue::Kind::String || encoded->stringValue.empty()) return;
		dependencies.push_back({ VansTimelineDependencyKind::PayloadSchema,
			encoded->stringValue, {}, {}, sourceId });
	};
	collect(track.extensionData, track.id);
	for (const VansTimelineSection& section : track.sections)
		if (section.extensionData) collect(*section.extensionData, section.id);
}

void ValidateEventSignal(
	const VansTimelineTrack& track,
	const VansTimelineSourceSchema& schema,
	const VansTimelineValidationContext& context,
	VansTimelineDiagnostics& diagnostics)
{
	VansValidateTimelineExtensionSchema(track, schema, diagnostics);
	auto validate = [&](const VansSerializedValue& data, const VansTimelineId& sourceId)
	{
		const VansSerializedValue* typeValue = VansTimelineFindSourceField(data, "payloadType");
		const VansSerializedValue* payload = VansTimelineFindSourceField(data, "payload");
		if (!typeValue || typeValue->kind != VansSerializedValue::Kind::String || typeValue->stringValue.empty()) return;
		const VansTimelinePayloadTypeId type = VansMakeStableId<VansTimelinePayloadTypeTag>(typeValue->stringValue);
		if (context.hasPayloadSchema && !context.hasPayloadSchema(type))
		{
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.PayloadSchemaMissing", {}, sourceId, "payloadType",
				"Timeline signal payload schema is not registered" });
			return;
		}
		if (payload && context.validatePayload)
		{
			std::string error;
			if (!context.validatePayload(type, *payload, error))
				diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
					"Timeline.SignalPayloadInvalid", {}, sourceId, "payload",
					error.empty() ? "Timeline signal payload is invalid" : error });
		}
	};
	validate(track.extensionData, track.id);
	for (const VansTimelineSection& section : track.sections)
		if (section.extensionData) validate(*section.extensionData, section.id);
}

double SampleClockRate(
	const VansCompiledTimeline& timeline,
	const VansCompiledTimelineTrack& track,
	VansTimelineTick tick)
{
	const VansTimelineCompiledDataReader reader(timeline.CompiledBytes(), timeline.CompiledValues());
	for (const VansCompiledTimelineSection& section : track.sections)
	{
		if (!VansTimelineEvaluator::IsInside(section, tick)) continue;
		const VansTimelineSectionTimeMap mapped = VansTimelineSectionTimeMapper::Map(
			tick, section.startTick, section.durationTicks, section.sourceInTick, section.sourceOutTick,
			section.playRate, section.reverse, section.loopMode, section.loopCount);
		double rate = 1.0;
		if (!section.channels.empty())
			if (const auto value = VansTimelineEvaluator::SampleChannel(section.channels.front(), mapped.localTick))
			{
				if (const auto* number = std::get_if<float>(&*value)) rate = *number;
				else if (const auto* number = std::get_if<double>(&*value)) rate = *number;
			}
		const VansTimelineValue* minimum = reader.ValueAt(section.extensionData, 2);
		const VansTimelineValue* maximum = reader.ValueAt(section.extensionData, 3);
		const auto number = [](const VansTimelineValue* value, double fallback)
		{
			if (const auto* typed = value ? std::get_if<float>(value) : nullptr) return static_cast<double>(*typed);
			if (const auto* typed = value ? std::get_if<double>(value) : nullptr) return *typed;
			return fallback;
		};
		return std::clamp(rate, number(minimum, 0.0), number(maximum, 8.0));
	}
	return 1.0;
}
}

bool VansRegisterTimelineRuntimeExtensions(
	VansTimelineTrackExtensionRegistry& registry,
	std::string& error)
{
	auto add = [&](VansTimelineTrackExtensionDescriptor descriptor)
	{ return registry.Register(std::move(descriptor), error); };
	const auto post = VansTimelineEvaluationPhase::PostScript;
	const auto none = VansTimelineBindingRequirement::None;
	using F = VansTimelineValueType;
	auto eventSignal = VansMakeTimelineSampleExtension(
		TimelineNames::EventSignal, "Event / Signal", "Logic", post, none,
		VansTimelineTrackFlags::PointEdge | VansTimelineTrackFlags::SupportsSections |
		VansTimelineTrackFlags::SupportsReverse | VansTimelineTrackFlags::Reversible |
		VansTimelineTrackFlags::Deterministic,
		{ { VansMakeTimelineSourceField("signalId", F::String, std::string(), true),
			VansMakeTimelineSourceField("payloadType", F::String, std::string(), true),
			VansMakeTimelineSourceField("payload", F::Struct, VansTimelineStructValue{}),
			VansMakeTimelineSourceField("lane", F::Enum, std::string("GameLogic"), false,
				{ "GameLogic", "Script", "MainThread", "Editor", "Diagnostics", "RenderPrep" }),
			VansMakeTimelineSourceField("dispatchTiming", F::Enum, std::string("SameFrame"), false,
				{ "SameFrame", "NextFrame" }),
			VansMakeTimelineSourceField("firePolicy", F::Enum, std::string("EveryCrossing"), false,
				{ "ForwardOnly", "BackwardOnly", "Both", "ExactSeek", "OncePerPlayback", "OncePerLoop", "EveryCrossing" }),
			VansMakeTimelineSourceField("editorSafe", F::Bool, false) }, {}, false, false }, nullptr);
	eventSignal.outputs.front().applierRequired = false;
	eventSignal.validate = ValidateEventSignal;
	eventSignal.collectDependencies = CollectEventSignalDependencies;
	if (!add(std::move(eventSignal))) return false;
	auto subTimeline = VansMakeTimelineSampleExtension(
		TimelineNames::SubTimeline, "SubTimeline / Shot", "Cinematic", post, none,
		VansTimelineContinuousTrackFlags(false),
		{ { VansMakeTimelineSourceField("failurePolicy", F::Enum, std::string("FailParent"), false,
			{ "FailParent", "StopChild", "Ignore" }) }, {}, false, false });
	subTimeline.outputs.front().applierRequired = false;
	subTimeline.sectionAssetKind = "Timeline";
	if (!add(std::move(subTimeline))) return false;
	auto timeScale = VansMakeTimelineSampleExtension(
		TimelineNames::TimeScale, "Time Scale", "Time", post, none,
		VansTimelineContinuousTrackFlags(),
		{ { VansMakeTimelineSourceField("scope", F::Enum, std::string("LocalTimeWarp"), false,
			{ "LocalTimeWarp" }),
			VansMakeTimelineSourceField("affectParticles", F::Bool, true),
			VansMakeTimelineSourceField("minimum", F::Double, 0.0),
			VansMakeTimelineSourceField("maximum", F::Double, 8.0),
			VansMakeTimelineSourceField("pauseAtZero", F::Bool, true) },
			{ VansMakeTimelineChannelSchema("scale", F::Double, true) }, false, false }, nullptr);
	timeScale.outputs.front().applierRequired = false;
	timeScale.clockRate = SampleClockRate;
	return add(std::move(timeScale));
}
}
