#include "VansTimelineParameterBlock.h"

#include <algorithm>

namespace Vans
{
bool VansTimelineParameterBlock::Initialize(
	const VansCompiledTimeline& timeline,
	const std::vector<VansTimelineParameterOverride>& overrides,
	VansTimelineDiagnostics& diagnostics)
{
	m_Values.clear(); m_Types.clear(); m_Dirty.clear();
	m_Values.reserve(timeline.Parameters().size());
	m_Types.reserve(timeline.Parameters().size());
	m_Dirty.resize(timeline.Parameters().size(), false);
	for (const auto& parameter : timeline.Parameters())
	{
		m_Values.push_back(parameter.defaultValue);
		m_Types.push_back(parameter.type);
	}
	bool valid = true;
	for (const auto& overrideValue : overrides)
	{
		const std::uint32_t slot = timeline.ParameterSlot(overrideValue.parameterId);
		if (slot == VansInvalidTimelineSlot)
		{
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.ParameterMissing", {}, {}, "parameterOverrides",
				"Timeline instance override references an unknown ParameterId" });
			valid = false; continue;
		}
		if (!Set(slot, overrideValue.value))
		{
			diagnostics.push_back({ VansTimelineDiagnosticSeverity::Error,
				"Timeline.ParameterTypeMismatch", {}, {}, "parameterOverrides",
				"Timeline instance override has the wrong value type" });
			valid = false;
		}
	}
	ClearDirty();
	return valid;
}

bool VansTimelineParameterBlock::Set(std::uint32_t slot, const VansTimelineValue& value)
{
	if (slot >= m_Values.size()) return false;
	const VansTimelineValueType actual = VansTimelineTypeOf(value);
	if (actual != m_Types[slot] && !(m_Types[slot] == VansTimelineValueType::Enum && actual == VansTimelineValueType::String))
		return false;
	m_Values[slot] = value; m_Dirty[slot] = true; return true;
}

bool VansTimelineParameterBlock::Set(
	const VansCompiledTimeline& timeline,
	VansTimelineParameterId id,
	const VansTimelineValue& value)
{
	return Set(timeline.ParameterSlot(id), value);
}

const VansTimelineValue* VansTimelineParameterBlock::Get(std::uint32_t slot) const
{
	return slot < m_Values.size() ? &m_Values[slot] : nullptr;
}

bool VansTimelineParameterBlock::Dirty(std::uint32_t slot) const
{
	return slot < m_Dirty.size() && m_Dirty[slot];
}

void VansTimelineParameterBlock::ClearDirty()
{
	std::fill(m_Dirty.begin(), m_Dirty.end(), false);
}
}
