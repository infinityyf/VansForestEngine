#pragma once

#include "VansTimelineEvaluation.h"

#include <vector>

namespace Vans
{
class VansTimelineParameterBlock
{
public:
	bool Initialize(
		const VansCompiledTimeline& timeline,
		const std::vector<VansTimelineParameterOverride>& overrides,
		VansTimelineDiagnostics& diagnostics);
	bool Set(std::uint32_t slot, const VansTimelineValue& value);
	bool Set(const VansCompiledTimeline& timeline, VansTimelineParameterId id, const VansTimelineValue& value);
	const VansTimelineValue* Get(std::uint32_t slot) const;
	bool Dirty(std::uint32_t slot) const;
	void ClearDirty();
	std::size_t Size() const { return m_Values.size(); }

private:
	std::vector<VansTimelineValue> m_Values;
	std::vector<VansTimelineValueType> m_Types;
	std::vector<bool> m_Dirty;
};
}
