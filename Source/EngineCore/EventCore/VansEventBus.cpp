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
			current.peakQueueLength = std::max(current.peakQueueLength, current.events.size());
		}
	}

	void VansEventBus::Flush(VansEventLane lane)
	{
		LaneQueue& queue = m_LaneQueues[ToEventLaneIndex(lane)];
		std::vector<std::unique_ptr<IQueuedEvent>> events;
		{
			std::lock_guard<std::mutex> lock(queue.mutex);
			events.swap(queue.events);
			queue.flushedCount += events.size();
		}

		for (std::unique_ptr<IQueuedEvent>& event : events)
			if (event)
				event->Dispatch(*this);
	}

	void VansEventBus::FlushMainThreadLanes()
	{
		Flush(VansEventLane::MainThread);
		Flush(VansEventLane::Input);
		Flush(VansEventLane::Physics);
		Flush(VansEventLane::GameLogic);
		Flush(VansEventLane::Script);
		Flush(VansEventLane::RenderPrep);
		Flush(VansEventLane::Diagnostics);
	}

	VansEventStatsSnapshot VansEventBus::GetStatsSnapshot() const
	{
		VansEventStatsSnapshot snapshot;
		{
			std::lock_guard<std::mutex> statsLock(m_StatsMutex);
			snapshot.eventTypes.reserve(m_TypeStats.size());
			for (const auto& [typeId, stats] : m_TypeStats)
			{
				VansEventTypeStatsSnapshot typeSnapshot;
				typeSnapshot.typeId = typeId;
				typeSnapshot.debugName = stats.debugName;
				typeSnapshot.publishNowCount = stats.publishNowCount;
				typeSnapshot.enqueueCount = stats.enqueueCount;
				typeSnapshot.dispatchCount = stats.dispatchCount;

				std::lock_guard<std::mutex> dispatcherLock(m_DispatchersMutex);
				auto dispatcherIt = m_Dispatchers.find(typeId);
				if (dispatcherIt != m_Dispatchers.end() && dispatcherIt->second)
					typeSnapshot.listenerCount = dispatcherIt->second->GetListenerCount();

				snapshot.eventTypes.push_back(std::move(typeSnapshot));
			}
		}

		snapshot.lanes.reserve(ToEventLaneIndex(VansEventLane::Count));
		for (std::size_t index = 0; index < ToEventLaneIndex(VansEventLane::Count); ++index)
		{
			const LaneQueue& queue = m_LaneQueues[index];
			std::lock_guard<std::mutex> lock(queue.mutex);
			VansEventLaneStatsSnapshot laneSnapshot;
			laneSnapshot.lane = static_cast<VansEventLane>(index);
			laneSnapshot.currentQueueLength = queue.events.size();
			laneSnapshot.peakQueueLength = queue.peakQueueLength;
			laneSnapshot.enqueuedCount = queue.enqueuedCount;
			laneSnapshot.flushedCount = queue.flushedCount;
			snapshot.lanes.push_back(laneSnapshot);
		}

		return snapshot;
	}

	void VansEventBus::ResetFrameStats()
	{
		std::lock_guard<std::mutex> statsLock(m_StatsMutex);
		for (auto& [typeId, stats] : m_TypeStats)
		{
			(void)typeId;
			stats.publishNowCount = 0;
			stats.enqueueCount = 0;
			stats.dispatchCount = 0;
		}

		for (LaneQueue& queue : m_LaneQueues)
		{
			std::lock_guard<std::mutex> lock(queue.mutex);
			queue.peakQueueLength = queue.events.size();
			queue.enqueuedCount = 0;
			queue.flushedCount = 0;
		}
	}

	void VansEventBus::RecordPublish(VansEventTypeId typeId, const char* debugName)
	{
		std::lock_guard<std::mutex> lock(m_StatsMutex);
		TypeStats& stats = m_TypeStats[typeId];
		if (stats.debugName.empty() && debugName)
			stats.debugName = debugName;
		++stats.publishNowCount;
	}

	void VansEventBus::RecordEnqueue(VansEventTypeId typeId, const char* debugName, VansEventLane lane)
	{
		{
			std::lock_guard<std::mutex> lock(m_StatsMutex);
			TypeStats& stats = m_TypeStats[typeId];
			if (stats.debugName.empty() && debugName)
				stats.debugName = debugName;
			++stats.enqueueCount;
		}

		LaneQueue& queue = m_LaneQueues[ToEventLaneIndex(lane)];
		std::lock_guard<std::mutex> lock(queue.mutex);
		++queue.enqueuedCount;
	}

	void VansEventBus::RecordDispatch(VansEventTypeId typeId, const char* debugName, VansEventLane)
	{
		std::lock_guard<std::mutex> lock(m_StatsMutex);
		TypeStats& stats = m_TypeStats[typeId];
		if (stats.debugName.empty() && debugName)
			stats.debugName = debugName;
		++stats.dispatchCount;
	}
}
