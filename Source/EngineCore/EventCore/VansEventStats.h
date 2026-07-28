#pragma once

#include "VansEventLane.h"
#include "VansEventTypeId.h"

#include <cstdint>
#include <string>
#include <vector>

namespace Vans
{
	struct VansEventTypeStatsSnapshot
	{
		VansEventTypeId typeId = 0;
		std::string debugName;
		std::uint64_t publishNowCount = 0;
		std::uint64_t enqueueCount = 0;
		std::uint64_t dispatchCount = 0;
		std::size_t listenerCount = 0;
	};

	struct VansEventLaneStatsSnapshot
	{
		VansEventLane lane = VansEventLane::MainThread;
		std::size_t currentQueueLength = 0;
		std::size_t peakQueueLength = 0;
		std::uint64_t enqueuedCount = 0;
		std::uint64_t flushedCount = 0;
	};

	struct VansEventStatsSnapshot
	{
		std::vector<VansEventTypeStatsSnapshot> eventTypes;
		std::vector<VansEventLaneStatsSnapshot> lanes;
	};
}
