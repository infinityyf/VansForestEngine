#pragma once

#include "../RuntimeCore/VansGenerationPool.h"
#include "VansTimelineEvaluation.h"

#include <memory>
#include <string>
#include <unordered_map>

namespace Vans
{
namespace TimelineClockNames
{
inline constexpr std::string_view GameTime = "Timeline.Clock.GameTime";
inline constexpr std::string_view UnscaledTime = "Timeline.Clock.UnscaledTime";
inline constexpr std::string_view Manual = "Timeline.Clock.Manual";
inline constexpr std::string_view FixedTick = "Timeline.Clock.FixedTick";
}

struct VansTimelineClockSample
{
	VansTimelineTick absoluteTick = 0;
	VansTimelineTick deltaTick = 0;
	std::uint64_t discontinuitySerial = 1;
	bool paused = false;
	bool discontinuity = false;
};

class IVansTimelineClockSource
{
public:
	virtual ~IVansTimelineClockSource() = default;
	virtual VansTimelineClockSample Sample(VansTimelineClockHandle handle) const = 0;
	virtual bool Advance(VansTimelineClockHandle handle, VansTimelineTick deltaTick) = 0;
	virtual bool SetAbsolute(VansTimelineClockHandle handle, VansTimelineTick tick, bool discontinuity) = 0;
	virtual bool SetPaused(VansTimelineClockHandle handle, bool paused) = 0;
};

class VansTimelineClockRegistry
{
public:
	bool Register(VansTimelineClockTypeId type, std::string stableName,
		std::shared_ptr<IVansTimelineClockSource> source, std::string& error);
	bool Seal(std::string& error);
	bool IsSealed() const { return m_Sealed; }
	std::shared_ptr<IVansTimelineClockSource> Resolve(VansTimelineClockTypeId type) const;
	static VansTimelineClockRegistry& BuiltIns();

private:
	struct Entry { VansTimelineClockTypeId type; std::string name; std::shared_ptr<IVansTimelineClockSource> source; };
	bool m_Sealed = false;
	std::vector<Entry> m_Entries;
	std::unordered_map<VansTimelineClockTypeId, std::uint32_t> m_ByType;
};

class VansTimelineOwnedClockSource final : public IVansTimelineClockSource
{
public:
	VansTimelineClockHandle Create(VansTimelineTick initialTick = 0);
	bool Release(VansTimelineClockHandle handle);
	VansTimelineClockSample Sample(VansTimelineClockHandle handle) const override;
	bool Advance(VansTimelineClockHandle handle, VansTimelineTick deltaTick) override;
	bool SetAbsolute(VansTimelineClockHandle handle, VansTimelineTick tick, bool discontinuity) override;
	bool SetPaused(VansTimelineClockHandle handle, bool paused) override;

private:
	VansGenerationPool<VansTimelineClockSample> m_Clocks;
};
}
