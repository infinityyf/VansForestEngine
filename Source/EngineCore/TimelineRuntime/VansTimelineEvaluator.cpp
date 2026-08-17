#include "VansTimelineEvaluator.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace Vans
{
namespace
{
double ClampUnit(double value) { return std::clamp(value, 0.0, 1.0); }

template <std::size_t Count>
std::array<double, Count> LerpArray(
	const std::array<double, Count>& left,
	const std::array<double, Count>& right,
	double alpha)
{
	std::array<double, Count> result{};
	for (std::size_t index = 0; index < Count; ++index)
		result[index] = left[index] + (right[index] - left[index]) * alpha;
	return result;
}

VansTimelineQuaternion SlerpQuaternion(
	const VansTimelineQuaternion& left,
	const VansTimelineQuaternion& right,
	double alpha)
{
	auto a = left.value;
	auto b = right.value;
	double dot = 0.0;
	for (std::size_t index = 0; index < 4; ++index) dot += a[index] * b[index];
	if (dot < 0.0) { for (double& value : b) value = -value; dot = -dot; }
	dot = std::clamp(dot, -1.0, 1.0);
	std::array<double, 4> value{};
	if (dot > 0.9995) value = LerpArray(a, b, alpha);
	else
	{
		const double angle = std::acos(dot);
		const double denominator = std::sin(angle);
		const double leftWeight = std::sin((1.0 - alpha) * angle) / denominator;
		const double rightWeight = std::sin(alpha * angle) / denominator;
		for (std::size_t index = 0; index < 4; ++index)
			value[index] = a[index] * leftWeight + b[index] * rightWeight;
	}
	double length = 0.0;
	for (double component : value) length += component * component;
	length = std::sqrt(length);
	if (length > 0.0) for (double& component : value) component /= length;
	return { value };
}

VansTimelineValue Interpolate(
	const VansTimelineKey& left,
	const VansTimelineKey& right,
	double alpha)
{
	if (left.interpolation == VansTimelineInterpolation::Constant || left.value.index() != right.value.index())
		return left.value;
	alpha = ClampUnit(alpha);
	if (left.interpolation == VansTimelineInterpolation::Auto ||
		left.interpolation == VansTimelineInterpolation::ClampedAuto)
		alpha = alpha * alpha * (3.0 - 2.0 * alpha);
	if (const auto* value = std::get_if<float>(&left.value))
		return static_cast<float>(*value + (std::get<float>(right.value) - *value) * alpha);
	if (const auto* value = std::get_if<double>(&left.value))
		return *value + (std::get<double>(right.value) - *value) * alpha;
	if (const auto* value = std::get_if<VansTimelineVec2>(&left.value))
		return VansTimelineVec2{ LerpArray(value->value, std::get<VansTimelineVec2>(right.value).value, alpha) };
	if (const auto* value = std::get_if<VansTimelineVec3>(&left.value))
		return VansTimelineVec3{ LerpArray(value->value, std::get<VansTimelineVec3>(right.value).value, alpha) };
	if (const auto* value = std::get_if<VansTimelineVec4>(&left.value))
		return VansTimelineVec4{ LerpArray(value->value, std::get<VansTimelineVec4>(right.value).value, alpha) };
	if (const auto* value = std::get_if<VansTimelineQuaternion>(&left.value))
		return SlerpQuaternion(*value, std::get<VansTimelineQuaternion>(right.value), alpha);
	if (const auto* value = std::get_if<VansTimelineColorLinear>(&left.value))
		return VansTimelineColorLinear{ LerpArray(value->value, std::get<VansTimelineColorLinear>(right.value).value, alpha) };
	if (const auto* value = std::get_if<VansTimelineColorSrgb>(&left.value))
		return VansTimelineColorSrgb{ LerpArray(value->value, std::get<VansTimelineColorSrgb>(right.value).value, alpha) };
	return left.value;
}

bool OutputOrder(const VansTimelineEvaluationOutput& left, const VansTimelineEvaluationOutput& right)
{
	if (left.order.hierarchicalBias != right.order.hierarchicalBias)
		return left.order.hierarchicalBias < right.order.hierarchicalBias;
	if (left.order.priority != right.order.priority) return left.order.priority < right.order.priority;
	if (left.order.trackOrder != right.order.trackOrder) return left.order.trackOrder < right.order.trackOrder;
	if (left.sourceTrackId != right.sourceTrackId) return left.sourceTrackId < right.sourceTrackId;
	if (left.sourceElementId != right.sourceElementId) return left.sourceElementId < right.sourceElementId;
	return left.order.sequence < right.order.sequence;
}
}

bool VansTimelineEvaluator::ConditionPasses(
	const VansCompiledTimelineTrack& track,
	const VansTimelineParameterBlock& parameters)
{
	if (track.conditionParameterSlot == VansInvalidTimelineSlot) return true;
	const VansTimelineValue* actual = parameters.Get(track.conditionParameterSlot);
	const bool equal = actual && VansTimelineValuesEqual(*actual, track.conditionExpected);
	return track.conditionNegate ? !equal : equal;
}

bool VansTimelineEvaluator::IsInside(
	const VansCompiledTimelineSection& section,
	VansTimelineTick tick)
{
	return section.active && tick >= section.startTick && tick < section.startTick + section.durationTicks;
}

bool VansTimelineEvaluator::Crossed(
	const VansTimelineTraversalSegment& segment,
	VansTimelineTick tick)
{
	if (segment.seekPolicy == VansTimelineSeekPolicy::NoEdges ||
		segment.seekPolicy == VansTimelineSeekPolicy::ContinuousOnly) return false;
	if (segment.seekPolicy == VansTimelineSeekPolicy::ExactTick) return segment.currentTick == tick;
	if (segment.includesPreviousEndpoint && segment.previousTick == tick) return true;
	return VansTimelineEdgeCrossing::Crossed(segment.previousTick, segment.currentTick, tick);
}

std::vector<VansTimelineRangeCrossing> VansTimelineEvaluator::CrossRanges(
	const VansCompiledTimelineSection& section,
	const VansTimelineTraversalSegment& segment)
{
	std::vector<VansTimelineRangeCrossing> result;
	const VansTimelineTick previous = segment.previousTick - section.startTick;
	const VansTimelineTick current = segment.currentTick - section.startTick;
	for (const VansTimelineRange& range : section.ranges)
	{
		const bool wasInside = previous >= range.startTick && previous < range.endTick;
		const bool isInside = current >= range.startTick && current < range.endTick;
		const bool exactSeek = segment.seekPolicy == VansTimelineSeekPolicy::ExactTick;
		const bool crossedStart = VansTimelineEdgeCrossing::Crossed(previous, current,
			range.startTick, exactSeek) || (segment.includesPreviousEndpoint && previous == range.startTick);
		const bool crossedEnd = VansTimelineEdgeCrossing::Crossed(previous, current,
			range.endTick, exactSeek) || (segment.includesPreviousEndpoint && previous == range.endTick);
		if (segment.seekPolicy == VansTimelineSeekPolicy::NoEdges || segment.seekPolicy == VansTimelineSeekPolicy::ContinuousOnly)
		{
			if (isInside) result.push_back({ range.id, VansTimelineRangeEdge::Update, segment.currentTick });
			continue;
		}
		if (segment.playbackDirection >= 0)
		{
			if ((!wasInside && isInside) || crossedStart) result.push_back({ range.id, VansTimelineRangeEdge::Enter, section.startTick + range.startTick });
			if (isInside) result.push_back({ range.id, VansTimelineRangeEdge::Update, segment.currentTick });
			if ((wasInside && !isInside) || crossedEnd) result.push_back({ range.id, VansTimelineRangeEdge::Exit, section.startTick + range.endTick });
		}
		else
		{
			if ((!wasInside && isInside) || crossedEnd) result.push_back({ range.id, VansTimelineRangeEdge::Enter, section.startTick + range.endTick });
			if (isInside) result.push_back({ range.id, VansTimelineRangeEdge::Update, segment.currentTick });
			if ((wasInside && !isInside) || crossedStart) result.push_back({ range.id, VansTimelineRangeEdge::Exit, section.startTick + range.startTick });
		}
	}
	return result;
}

double VansTimelineEvaluator::SectionEnvelope(
	const VansCompiledTimelineSection& section,
	VansTimelineTick tick)
{
	if (!IsInside(section, tick)) return 0.0;
	const VansTimelineTick relative = tick - section.startTick;
	const VansTimelineTick remaining = section.durationTicks - relative;
	double weight = 1.0;
	if (section.easeInTicks > 0 && relative < section.easeInTicks)
		weight = std::min(weight, static_cast<double>(relative) / section.easeInTicks);
	if (section.easeOutTicks > 0 && remaining <= section.easeOutTicks)
		weight = std::min(weight, static_cast<double>(remaining) / section.easeOutTicks);
	return ClampUnit(weight);
}

std::optional<VansTimelineValue> VansTimelineEvaluator::SampleChannel(
	const VansTimelineChannel& channel,
	VansTimelineTick tick)
{
	if (channel.keys.empty()) return std::nullopt;
	const auto right = std::lower_bound(channel.keys.begin(), channel.keys.end(), tick,
		[](const VansTimelineKey& key, VansTimelineTick value) { return key.tick < value; });
	if (right == channel.keys.begin()) return right->value;
	if (right == channel.keys.end()) return channel.keys.back().value;
	if (right->tick == tick) return right->value;
	const VansTimelineKey& left = *(right - 1);
	const double alpha = static_cast<double>(tick - left.tick) /
		static_cast<double>(std::max<VansTimelineTick>(1, right->tick - left.tick));
	return Interpolate(left, *right, alpha);
}

void VansTimelineEvaluator::Evaluate(
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
	VansTimelineDiagnostics& diagnostics)
{
	outputs.clear();
	arena.Reset();
	bindings.Resolve(timeline, diagnostics);
	const auto& tracks = timeline.Tracks(phase);
	const VansTimelineCompiledDataReader reader(timeline.CompiledBytes(), timeline.CompiledValues());
	std::uint64_t sequence = 0;
	for (const VansTimelineTraversalSegment& segment : segments)
	{
		for (std::uint32_t trackIndex = 0; trackIndex < tracks.size(); ++trackIndex)
		{
			const VansCompiledTimelineTrack& track = tracks[trackIndex];
			if (!track.evaluate || !ConditionPasses(track, parameters)) continue;
			VansResolvedTimelineTarget target;
			if (track.bindingSlot != VansInvalidTimelineSlot)
			{
				const VansResolvedTimelineTarget* resolved = bindings.Find(track.bindingSlot);
				if (!resolved || !resolved->valid) continue;
				target = *resolved;
			}
			for (std::uint32_t sectionIndex = 0; sectionIndex < track.sections.size(); ++sectionIndex)
			{
				const VansCompiledTimelineSection& section = track.sections[sectionIndex];
				const bool inside = IsInside(section, segment.currentTick);
				const bool entered = Crossed(segment, section.startTick);
				const bool exited = Crossed(segment, section.startTick + section.durationTicks);
				if (!inside && !entered && !exited && !VansHasTimelineFlag(track.flags, VansTimelineTrackFlags::PointEdge)) continue;
				VansTimelineExtensionEvaluationContext context{
					timeline, track, &section, segment, reader, parameters, target, arena, outputs, diagnostics,
					session, root, {}, trackIndex, sectionIndex, hierarchicalBias, sequence };
				track.evaluate(context);
			}
		}
	}
	std::stable_sort(outputs.begin(), outputs.end(), OutputOrder);
}
}
