#include "VansTimelineClockRegistry.h"

namespace Vans
{
bool VansTimelineClockRegistry::Register(
	VansTimelineClockTypeId type,
	std::string stableName,
	std::shared_ptr<IVansTimelineClockSource> source,
	std::string& error)
{
	error.clear();
	if (m_Sealed) { error = "Timeline.ClockRegistrySealed"; return false; }
	if (stableName.empty() || !source) { error = "Timeline.ClockRegistrationInvalid"; return false; }
	const VansTimelineClockTypeId expected = VansMakeStableId<VansTimelineClockTag>(stableName);
	if (!type) type = expected;
	if (type != expected) { error = "Timeline.ClockTypeHashMismatch"; return false; }
	if (m_ByType.find(type) != m_ByType.end()) { error = "Timeline.ClockTypeDuplicate"; return false; }
	m_ByType.emplace(type, static_cast<std::uint32_t>(m_Entries.size()));
	m_Entries.push_back({ type, std::move(stableName), std::move(source) });
	return true;
}

bool VansTimelineClockRegistry::Seal(std::string& error)
{
	error.clear();
	if (m_Entries.empty()) { error = "Timeline.ClockRegistryEmpty"; return false; }
	m_Sealed = true;
	return true;
}

std::shared_ptr<IVansTimelineClockSource> VansTimelineClockRegistry::Resolve(
	VansTimelineClockTypeId type) const
{
	const auto found = m_ByType.find(type);
	return found == m_ByType.end() ? nullptr : m_Entries[found->second].source;
}

VansTimelineClockRegistry& VansTimelineClockRegistry::BuiltIns()
{
	static VansTimelineClockRegistry registry;
	static bool initialized = false;
	if (!initialized)
	{
		initialized = true;
		std::string error;
		for (std::string_view name : { TimelineClockNames::GameTime, TimelineClockNames::UnscaledTime,
			TimelineClockNames::Manual, TimelineClockNames::FixedTick })
			registry.Register(VansMakeStableId<VansTimelineClockTag>(name), std::string(name),
				std::make_shared<VansTimelineOwnedClockSource>(), error);
		registry.Seal(error);
	}
	return registry;
}

VansTimelineClockHandle VansTimelineOwnedClockSource::Create(VansTimelineTick initialTick)
{
	VansTimelineClockSample sample;
	sample.absoluteTick = initialTick;
	return m_Clocks.Emplace(sample);
}

bool VansTimelineOwnedClockSource::Release(VansTimelineClockHandle handle) { return m_Clocks.Release(handle); }

VansTimelineClockSample VansTimelineOwnedClockSource::Sample(VansTimelineClockHandle handle) const
{
	const VansTimelineClockSample* sample = m_Clocks.Resolve(handle);
	return sample ? *sample : VansTimelineClockSample{};
}

bool VansTimelineOwnedClockSource::Advance(VansTimelineClockHandle handle, VansTimelineTick deltaTick)
{
	VansTimelineClockSample* sample = m_Clocks.Resolve(handle);
	if (!sample) return false;
	sample->deltaTick = sample->paused ? 0 : deltaTick;
	if (!sample->paused) sample->absoluteTick += deltaTick;
	sample->discontinuity = false;
	return true;
}

bool VansTimelineOwnedClockSource::SetAbsolute(
	VansTimelineClockHandle handle,
	VansTimelineTick tick,
	bool discontinuity)
{
	VansTimelineClockSample* sample = m_Clocks.Resolve(handle);
	if (!sample) return false;
	sample->deltaTick = tick - sample->absoluteTick;
	sample->absoluteTick = tick;
	sample->discontinuity = discontinuity;
	if (discontinuity) ++sample->discontinuitySerial;
	return true;
}

bool VansTimelineOwnedClockSource::SetPaused(VansTimelineClockHandle handle, bool paused)
{
	VansTimelineClockSample* sample = m_Clocks.Resolve(handle);
	if (!sample) return false;
	sample->paused = paused;
	sample->deltaTick = 0;
	return true;
}
}
