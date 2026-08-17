#pragma once

#include "VansTimelineComponent.h"
#include "VansTimelineEvaluation.h"

#include <vector>

namespace Vans
{
class VansRuntimeWorld;

class VansTimelineBindingResolver
{
public:
	void BindWorld(VansRuntimeWorld* world, VansEntityHandle owner);
	void SetOverrides(const std::vector<VansTimelineBindingOverride>& overrides);
	void SetRuntimeBindings(const std::vector<VansTimelineRuntimeBinding>& bindings);
	void Resolve(const VansCompiledTimeline& timeline, VansTimelineDiagnostics& diagnostics);
	const VansResolvedTimelineTarget* Find(std::uint32_t bindingSlot);
	const VansResolvedTimelineTarget* Find(VansTimelineBindingId bindingId,
		const VansCompiledTimeline& timeline, VansTimelineDiagnostics& diagnostics);
	void Invalidate();

private:
	bool IsCachedTargetValid(const VansResolvedTimelineTarget& target) const;
	const VansTimelineBindingOverride* FindOverride(VansTimelineBindingId bindingId) const;
	const VansTimelineRuntimeBinding* FindRuntimeBinding(VansTimelineBindingId bindingId) const;
	VansResolvedTimelineTarget ResolveOne(
		const VansCompiledTimelineBinding& binding,
		VansTimelineDiagnostics& diagnostics);

	VansRuntimeWorld* m_World = nullptr;
	VansEntityHandle m_Owner;
	std::vector<VansTimelineBindingOverride> m_Overrides;
	std::vector<VansTimelineRuntimeBinding> m_RuntimeBindings;
	std::vector<VansResolvedTimelineTarget> m_Cache;
};
}
