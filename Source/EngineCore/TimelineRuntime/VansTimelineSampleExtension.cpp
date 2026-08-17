#include "VansTimelineSampleExtension.h"

#include "VansTimelineEvaluator.h"
#include "../TimelineCore/VansTimelineDependencyBuilder.h"

namespace Vans
{
VansTimelineSourceField VansMakeTimelineSourceField(
	std::string name,
	VansTimelineValueType type,
	VansTimelineValue defaultValue,
	bool required,
	std::vector<std::string> enumValues)
{
	VansTimelineSourceField field;
	field.id = VansMakeStableId<VansTimelineFieldTag>(name);
	field.name = std::move(name);
	field.type = type;
	field.defaultValue = std::move(defaultValue);
	field.required = required;
	field.enumValues = std::move(enumValues);
	return field;
}

VansTimelineChannelSchema VansMakeTimelineChannelSchema(
	std::string name,
	VansTimelineValueType type,
	bool required,
	std::string typeField,
	std::vector<std::pair<std::string, VansTimelineValueType>> typeCases)
{
	return { std::move(name), type, required, std::move(typeField), std::move(typeCases) };
}

VansTimelineTrackFlags VansTimelineContinuousTrackFlags(bool supportsChannels)
{
	VansTimelineTrackFlags flags = VansTimelineTrackFlags::Continuous |
		VansTimelineTrackFlags::SupportsSections | VansTimelineTrackFlags::SupportsReverse |
		VansTimelineTrackFlags::SeekRebuildable | VansTimelineTrackFlags::Reversible |
		VansTimelineTrackFlags::Deterministic;
	if (supportsChannels) flags = flags | VansTimelineTrackFlags::SupportsChannels;
	return flags;
}

void VansEvaluateTimelineSampleExtension(VansTimelineExtensionEvaluationContext& context)
{
	if (!context.section) return;
	const VansCompiledTimelineSection& section = *context.section;
	const bool inside = VansTimelineEvaluator::IsInside(section, context.traversal.currentTick);
	const bool entered = VansTimelineEvaluator::Crossed(context.traversal, section.startTick);
	const bool exited = VansTimelineEvaluator::Crossed(
		context.traversal, section.startTick + section.durationTicks);
	if (!inside && !entered && !exited) return;
	const VansTimelineTick sampleTick = inside ? context.traversal.currentTick :
		(entered ? section.startTick : section.startTick + section.durationTicks - 1);
	const VansTimelineSectionTimeMap mapped = VansTimelineSectionTimeMapper::Map(sampleTick,
		section.startTick, section.durationTicks, section.sourceInTick, section.sourceOutTick,
		section.playRate, section.reverse, section.loopMode, section.loopCount);
	VansTimelineSampleOutput payload;
	payload.timelineTick = context.traversal.currentTick;
	payload.localTick = mapped.active ? mapped.localTick : 0;
	payload.weight = inside ? VansTimelineEvaluator::SectionEnvelope(section, sampleTick) : 0.0;
	payload.loopIteration = context.traversal.loopIteration;
	payload.direction = static_cast<std::int8_t>(context.traversal.playbackDirection);
	payload.active = inside;
	payload.entered = entered;
	payload.exited = exited;
	payload.rebuild = context.traversal.seekPolicy == VansTimelineSeekPolicy::RebuildActive;
	context.Emit(context.track.outputTypeId, VansInvalidTimelineRegistrySlot,
		payload, section.id, section.completionMode);
	context.outputs.back().retainsPreAnimatedState = inside;
}

namespace
{
void VansEvaluateTimelinePointExtension(VansTimelineExtensionEvaluationContext& context)
{
	if (!context.section || !VansTimelineEvaluator::Crossed(
		context.traversal, context.section->startTick)) return;
	VansTimelineSampleOutput payload;
	payload.timelineTick = context.traversal.currentTick;
	payload.localTick = context.section->sourceInTick;
	payload.loopIteration = context.traversal.loopIteration;
	payload.direction = static_cast<std::int8_t>(context.traversal.playbackDirection);
	payload.active = true;
	payload.entered = true;
	context.Emit(context.track.outputTypeId, VansInvalidTimelineRegistrySlot,
		payload, context.section->id, context.section->completionMode);
	context.outputs.back().retainsPreAnimatedState = false;
}
}

VansTimelineTrackExtensionDescriptor VansMakeTimelineSampleExtension(
	std::string_view stableName,
	std::string displayName,
	std::string category,
	VansTimelineEvaluationPhase phase,
	VansTimelineBindingRequirement binding,
	VansTimelineTrackFlags flags,
	VansTimelineSourceSchema schema,
	VansTimelineCollectDependenciesFn collectDependencies)
{
	VansTimelineTrackExtensionDescriptor descriptor;
	descriptor.typeId = VansMakeStableId<VansTimelineTrackTypeTag>(stableName);
	descriptor.stableName = stableName;
	descriptor.flags = flags;
	descriptor.phase = phase;
	descriptor.binding = binding;
	descriptor.sourceSchema = std::move(schema);
	descriptor.collectDependencies = collectDependencies;
	descriptor.compile = VansCompileTimelineExtensionSchema;
	descriptor.evaluate = VansEvaluateTimelineSampleExtension;
	const std::string outputName = std::string(stableName) + ".Output";
	descriptor.outputs.push_back({ VansMakeStableId<VansTimelineOutputTypeTag>(outputName),
		outputName, sizeof(VansTimelineSampleOutput), alignof(VansTimelineSampleOutput), true });
	descriptor.displayName = std::move(displayName);
	descriptor.category = std::move(category);
	return descriptor;
}

VansTimelineTrackExtensionDescriptor VansMakeTimelinePointExtension(
	std::string_view stableName,
	std::string displayName,
	std::string category,
	VansTimelineEvaluationPhase phase,
	VansTimelineBindingRequirement binding,
	VansTimelineSourceSchema schema,
	VansTimelineCollectDependenciesFn collectDependencies)
{
	auto descriptor = VansMakeTimelineSampleExtension(stableName, std::move(displayName),
		std::move(category), phase, binding,
		VansTimelineTrackFlags::PointEdge | VansTimelineTrackFlags::SupportsSections |
		VansTimelineTrackFlags::SupportsReverse | VansTimelineTrackFlags::Reversible |
		VansTimelineTrackFlags::Deterministic,
		std::move(schema), collectDependencies);
	descriptor.evaluate = VansEvaluateTimelinePointExtension;
	return descriptor;
}
}
