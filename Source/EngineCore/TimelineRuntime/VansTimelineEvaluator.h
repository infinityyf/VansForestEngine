#pragma once

#include "VansTimelineBindingResolver.h"
#include "VansTimelineParameterBlock.h"

#include <optional>

namespace Vans
{
struct VansTimelineExtensionEvaluationContext
{
	const VansCompiledTimeline& timeline;
	const VansCompiledTimelineTrack& track;
	const VansCompiledTimelineSection* section = nullptr;
	const VansTimelineTraversalSegment& traversal;
	const VansTimelineCompiledDataReader& compiledData;
	const VansTimelineParameterBlock& parameters;
	const VansResolvedTimelineTarget& target;
	VansTimelineOutputArena& arena;
	std::vector<VansTimelineEvaluationOutput>& outputs;
	VansTimelineDiagnostics& diagnostics;
	VansTimelineSessionHandle session;
	VansTimelineSessionHandle root;
	VansTimelineWriterHandle writer;
	std::uint32_t trackIndex = UINT32_MAX;
	std::uint32_t sectionIndex = UINT32_MAX;
	std::int32_t hierarchicalBias = 0;
	std::uint64_t& sequence;

	template <typename Payload>
	void Emit(
		VansTimelineOutputTypeId typeId,
		std::uint32_t applierSlot,
		const Payload& payload,
		VansTimelineId elementId = {},
		VansTimelineCompletionMode completion = VansTimelineCompletionMode::ProjectDefault)
	{
		VansTimelineEvaluationOutput output;
		output.typeId = typeId;
		output.applierSlot = applierSlot;
		output.payload = arena.Write(payload);
		output.target = target;
		output.writer = writer;
		output.order = { hierarchicalBias, track.priority, track.order, sequence++ };
		output.blendMode = track.blendMode;
		output.completion = completion == VansTimelineCompletionMode::ProjectDefault
			? (track.completionMode == VansTimelineCompletionMode::ProjectDefault
				? timeline.DefaultCompletionMode() : track.completionMode)
			: completion;
		output.session = session;
		output.root = root;
		output.phase = track.phase;
		output.trackIndex = trackIndex;
		output.sectionIndex = sectionIndex;
		output.sourceTrackId = track.id;
		output.sourceElementId = std::move(elementId);
		outputs.push_back(std::move(output));
	}
};

class VansTimelineEvaluator
{
public:
	static void Evaluate(
		const VansCompiledTimeline& timeline,
		VansTimelineEvaluationPhase phase,
		const std::vector<VansTimelineTraversalSegment>& segments,
		const VansTimelineParameterBlock& parameters,
		VansTimelineBindingResolver& bindings,
		VansTimelineSessionHandle session,
		VansTimelineSessionHandle root,
		std::int32_t hierarchicalBias,
		VansTimelineOutputArena& arena,
		std::vector<VansTimelineEvaluationOutput>& outputs,
		VansTimelineDiagnostics& diagnostics);

	static bool ConditionPasses(
		const VansCompiledTimelineTrack& track,
		const VansTimelineParameterBlock& parameters);
	static bool IsInside(const VansCompiledTimelineSection& section, VansTimelineTick tick);
	static bool Crossed(const VansTimelineTraversalSegment& segment, VansTimelineTick tick);
	static std::vector<VansTimelineRangeCrossing> CrossRanges(
		const VansCompiledTimelineSection& section,
		const VansTimelineTraversalSegment& segment);
	static double SectionEnvelope(const VansCompiledTimelineSection& section, VansTimelineTick tick);
	static std::optional<VansTimelineValue> SampleChannel(
		const VansTimelineChannel& channel,
		VansTimelineTick tick);
};
}
