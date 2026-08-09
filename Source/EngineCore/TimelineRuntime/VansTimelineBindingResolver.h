#pragma once

#include "VansTimelineComponent.h"
#include "VansTimelineEvaluation.h"

#include <unordered_map>

namespace Vans
{
class VansRuntimeWorld;

class VansTimelineBindingResolver
{
public:
	void BindWorld(VansRuntimeWorld* world, VansEntityHandle owner);
	void SetOverrides(const std::vector<VansTimelineBindingOverride>& overrides);
	void Resolve(const std::vector<VansTimelineBinding>& bindings, VansTimelineDiagnostics& diagnostics);
	VansResolvedTimelineTarget ResolveOne(
		const VansTimelineBinding& binding,
		VansTimelineDiagnostics* diagnostics = nullptr);
	const VansResolvedTimelineTarget* Find(const VansTimelineId& bindingId);
	void Invalidate();

private:
	bool IsCachedTargetValid(const VansResolvedTimelineTarget& target) const;
	const VansTimelineBindingOverride* FindOverride(const VansTimelineId& bindingId) const;

	VansRuntimeWorld* m_World = nullptr;
	VansEntityHandle m_Owner;
	std::vector<VansTimelineBindingOverride> m_Overrides;
	std::unordered_map<VansTimelineId, VansResolvedTimelineTarget> m_Cache;
};
}
