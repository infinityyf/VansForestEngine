#pragma once

#include "../../TimelineCore/VansTimelineTypes.h"

#include <cstdint>
#include <string>

namespace Vans
{
enum class VansTimelineLifecycleKind : std::uint8_t
{
	SessionCreated,
	Started,
	Paused,
	Resumed,
	Seeked,
	Looped,
	Completed,
	Stopped,
	Failed,
	Released
};

struct VansTimelineEventContext
{
	VansTimelineSessionHandle session;
	VansTimelineSessionHandle root;
	VansTimelineSessionHandle parent;
	VansTimelineTick tick = 0;
	std::uint64_t contentHash = 0;
	std::uint64_t correlation = 0;
	std::uint64_t sequence = 0;
	VansTimelineEndReason reason = VansTimelineEndReason::Stopped;
};

struct VansTimelineSessionLifecycleEvent
{
	VansTimelineEventContext context;
	VansTimelineLifecycleKind kind = VansTimelineLifecycleKind::SessionCreated;
};

struct VansTimelineMarkerReachedEvent
{
	VansTimelineEventContext context;
	VansTimelineId markerId;
	std::string category;
	VansTimelinePayloadTypeId payloadType;
	VansSerializedValue payload;
};

struct VansTimelineSignalFiredEvent
{
	VansTimelineEventContext context;
	VansTimelineId signalId;
	VansTimelinePayloadTypeId payloadType;
	VansSerializedValue payload;
};
}
