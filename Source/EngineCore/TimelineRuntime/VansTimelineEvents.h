#pragma once

#include "../SceneRuntime/VansRuntimeHandle.h"
#include "../TimelineCore/VansTimelineTypes.h"

#include <string>

namespace Vans
{
struct VansTimelineSignalEvent
{
	std::string signalId;
	std::string displayName;
	std::string sourceTrackId;
	std::string writerId;
	VansEntityHandle targetEntity;
	VansTimelineKeyValue payload;
	VansTimelineTick tick = 0;
};
}
