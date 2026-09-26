#include "VansEventBus.h"

#include <atomic>

namespace Vans
{
	VansEventTypeId AllocateVansEventTypeId()
	{
		static std::atomic<VansEventTypeId> nextId{ 1 };
		return nextId.fetch_add(1, std::memory_order_relaxed);
	}

	VansEventBus& VansEventBus::Get()
	{
		static VansEventBus bus;
		return bus;
	}

	void VansEventBus::BeginFrame()
	{
		for (std::size_t index = 0; index < ToEventLaneIndex(VansEventLane::Count); ++index)
		{
			LaneQueue& deferred = m_NextFrameQueues[index];
			std::vector<std::unique_ptr<IQueuedEvent>> events;
			{
				std::lock_guard<std::mutex> lock(deferred.mutex);
				events.swap(deferred.events);
			}
			if (events.empty()) continue;
			LaneQueue& current = m_LaneQueues[index];
			std::lock_guard<std::mutex> lock(current.mutex);
			for (auto& event : events) current.events.push_back(std::move(event));
		}
	}

	void VansEventBus::Flush(VansEventLane lane)
	{
		LaneQueue& queue = m_LaneQueues[ToEventLaneIndex(lane)];
		std::vector<std::unique_ptr<IQueuedEvent>> events;
		{
			std::lock_guard<std::mutex> lock(queue.mutex);
			events.swap(queue.events);
		}

		for (std::unique_ptr<IQueuedEvent>& event : events)
			if (event)
				event->Dispatch(*this);
	}

}
