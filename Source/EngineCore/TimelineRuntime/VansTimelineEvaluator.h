#pragma once

#include "VansTimelineBindingResolver.h"

namespace Vans
{
class VansTimelineEvaluator
{
public:
	static double EvaluateLocalTimeScale(
		const VansCompiledTimeline& timeline,
		VansTimelineTick tick);
	static void Evaluate(
		const VansCompiledTimeline& timeline,
		VansTimelineEvaluationPhase phase,
		const std::vector<VansTimelineEvaluationSegment>& segments,
		const std::unordered_map<std::string, VansTimelineKeyValue>& parameters,
		VansTimelineBindingResolver& bindings,
		const std::string& writerId,
		std::vector<VansTimelineEvaluationOutput>& outputs,
		VansTimelineDiagnostics& diagnostics);
};
}
