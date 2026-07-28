#pragma once

#include "VansEventConnection.h"
#include "VansEventLane.h"
#include "VansEventStats.h"
#include "VansEventTypeId.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <typeinfo>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vans
{
	class VansEventBus
	{
	public:
		static VansEventBus& Get();

		template <typename EventT>
		VansEventConnection Subscribe(
			std::function<void(const EventT&)> handler,
			VansEventLane lane,
			int priority = 0,
			const char* debugName = nullptr)
		{
			if (!handler)
				return {};

			auto dispatcher = GetOrCreateDispatcher<EventT>();
			return dispatcher->Subscribe(std::move(handler), lane, priority, debugName);
		}

		template <typename EventT>
		void PublishNow(const EventT& event)
		{
			auto dispatcher = GetDispatcher<EventT>();
			RecordPublish(GetVansEventTypeId<EventT>(), GetDebugName<EventT>());
			if (dispatcher)
				dispatcher->Publish(event, nullptr);
		}

		template <typename EventT>
		void Enqueue(EventT event, VansEventLane lane)
		{
			const VansEventTypeId typeId = GetVansEventTypeId<EventT>();
			const char* debugName = GetDebugName<EventT>();
			RecordEnqueue(typeId, debugName, lane);

			auto queued = std::make_unique<QueuedEvent<EventT>>(std::move(event), lane);
			LaneQueue& queue = m_LaneQueues[ToEventLaneIndex(lane)];
			{
				std::lock_guard<std::mutex> lock(queue.mutex);
				queue.events.push_back(std::move(queued));
				if (queue.events.size() > queue.peakQueueLength)
					queue.peakQueueLength = queue.events.size();
			}
		}

		void Flush(VansEventLane lane);
		void FlushMainThreadLanes();
		VansEventStatsSnapshot GetStatsSnapshot() const;
		void ResetFrameStats();

	private:
		struct IEventDispatcher
		{
			virtual ~IEventDispatcher() = default;
			virtual std::size_t GetListenerCount() const = 0;
			virtual const char* GetDebugName() const = 0;
		};

		template <typename EventT>
		class EventDispatcher final : public IEventDispatcher
		{
		public:
			using Handler = std::function<void(const EventT&)>;

			explicit EventDispatcher(const char* debugName)
				: m_DebugName(debugName ? debugName : "UnnamedEvent")
			{
			}

			VansEventConnection Subscribe(Handler handler, VansEventLane lane, int priority, const char* debugName)
			{
				const std::uint64_t id = m_NextId++;
				{
					std::lock_guard<std::mutex> lock(m_Mutex);
					m_Slots.push_back(Slot{ id, lane, priority, true, debugName ? debugName : "", std::move(handler) });
					m_Sorted = false;
				}

				return VansEventConnection([weak = std::weak_ptr<EventDispatcher<EventT>>(m_Self), id]()
				{
					if (std::shared_ptr<EventDispatcher<EventT>> dispatcher = weak.lock())
						dispatcher->Disconnect(id);
				});
			}

			void BindSelf(const std::shared_ptr<EventDispatcher<EventT>>& self)
			{
				m_Self = self;
			}

			void Publish(const EventT& event, const VansEventLane* laneFilter)
			{
				std::size_t dispatchCount = 0;
				{
					std::lock_guard<std::mutex> lock(m_Mutex);
					SortIfNeeded();
					++m_DispatchDepth;
					dispatchCount = m_Slots.size();
				}

				for (std::size_t index = 0; index < dispatchCount; ++index)
				{
					Handler handler;
					{
						std::lock_guard<std::mutex> lock(m_Mutex);
						if (index >= m_Slots.size())
							continue;
						Slot& slot = m_Slots[index];
						if (!slot.connected)
							continue;
						if (laneFilter && slot.lane != *laneFilter)
							continue;
						handler = slot.handler;
					}

					if (handler)
						handler(event);
				}

				{
					std::lock_guard<std::mutex> lock(m_Mutex);
					if (m_DispatchDepth > 0)
						--m_DispatchDepth;
					if (m_DispatchDepth == 0)
						Compact();
				}
			}

			std::size_t GetListenerCount() const override
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				std::size_t count = 0;
				for (const Slot& slot : m_Slots)
					if (slot.connected)
						++count;
				return count;
			}

			const char* GetDebugName() const override
			{
				return m_DebugName.c_str();
			}

		private:
			struct Slot
			{
				std::uint64_t id = 0;
				VansEventLane lane = VansEventLane::MainThread;
				int priority = 0;
				bool connected = false;
				std::string debugName;
				Handler handler;
			};

			void Disconnect(std::uint64_t id)
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				for (Slot& slot : m_Slots)
				{
					if (slot.id == id)
					{
						slot.connected = false;
						break;
					}
				}
				if (m_DispatchDepth == 0)
					Compact();
			}

			void SortIfNeeded()
			{
				if (m_Sorted)
					return;
				std::stable_sort(m_Slots.begin(), m_Slots.end(),
					[](const Slot& lhs, const Slot& rhs)
					{
						return lhs.priority > rhs.priority;
					});
				m_Sorted = true;
			}

			void Compact()
			{
				m_Slots.erase(
					std::remove_if(m_Slots.begin(), m_Slots.end(),
						[](const Slot& slot) { return !slot.connected; }),
					m_Slots.end());
			}

			mutable std::mutex m_Mutex;
			std::vector<Slot> m_Slots;
			std::uint64_t m_NextId = 1;
			std::size_t m_DispatchDepth = 0;
			bool m_Sorted = true;
			std::string m_DebugName;
			std::weak_ptr<EventDispatcher<EventT>> m_Self;
		};

		struct IQueuedEvent
		{
			virtual ~IQueuedEvent() = default;
			virtual void Dispatch(VansEventBus& bus) = 0;
			virtual VansEventLane GetLane() const = 0;
			virtual VansEventTypeId GetTypeId() const = 0;
		};

		template <typename EventT>
		struct QueuedEvent final : IQueuedEvent
		{
			QueuedEvent(EventT value, VansEventLane targetLane)
				: event(std::move(value))
				, lane(targetLane)
			{
			}

			void Dispatch(VansEventBus& bus) override
			{
				if (auto dispatcher = bus.GetDispatcher<EventT>())
					dispatcher->Publish(event, &lane);
				bus.RecordDispatch(GetVansEventTypeId<EventT>(), GetDebugName<EventT>(), lane);
			}

			VansEventLane GetLane() const override { return lane; }
			VansEventTypeId GetTypeId() const override { return GetVansEventTypeId<EventT>(); }

			EventT event;
			VansEventLane lane = VansEventLane::MainThread;
		};

		struct LaneQueue
		{
			mutable std::mutex mutex;
			std::vector<std::unique_ptr<IQueuedEvent>> events;
			std::size_t peakQueueLength = 0;
			std::uint64_t enqueuedCount = 0;
			std::uint64_t flushedCount = 0;
		};

		struct TypeStats
		{
			std::string debugName;
			std::uint64_t publishNowCount = 0;
			std::uint64_t enqueueCount = 0;
			std::uint64_t dispatchCount = 0;
		};

		template <typename EventT>
		static const char* GetDebugName()
		{
			return typeid(EventT).name();
		}

		template <typename EventT>
		std::shared_ptr<EventDispatcher<EventT>> GetDispatcher() const
		{
			const VansEventTypeId typeId = GetVansEventTypeId<EventT>();
			std::lock_guard<std::mutex> lock(m_DispatchersMutex);
			auto it = m_Dispatchers.find(typeId);
			if (it == m_Dispatchers.end())
				return nullptr;
			return std::static_pointer_cast<EventDispatcher<EventT>>(it->second);
		}

		template <typename EventT>
		std::shared_ptr<EventDispatcher<EventT>> GetOrCreateDispatcher()
		{
			const VansEventTypeId typeId = GetVansEventTypeId<EventT>();
			std::lock_guard<std::mutex> lock(m_DispatchersMutex);
			auto it = m_Dispatchers.find(typeId);
			if (it != m_Dispatchers.end())
				return std::static_pointer_cast<EventDispatcher<EventT>>(it->second);

			auto dispatcher = std::make_shared<EventDispatcher<EventT>>(GetDebugName<EventT>());
			dispatcher->BindSelf(dispatcher);
			m_Dispatchers[typeId] = dispatcher;
			return dispatcher;
		}

		void RecordPublish(VansEventTypeId typeId, const char* debugName);
		void RecordEnqueue(VansEventTypeId typeId, const char* debugName, VansEventLane lane);
		void RecordDispatch(VansEventTypeId typeId, const char* debugName, VansEventLane lane);

		mutable std::mutex m_DispatchersMutex;
		std::unordered_map<VansEventTypeId, std::shared_ptr<IEventDispatcher>> m_Dispatchers;

		mutable std::mutex m_StatsMutex;
		std::unordered_map<VansEventTypeId, TypeStats> m_TypeStats;
		std::array<LaneQueue, ToEventLaneIndex(VansEventLane::Count)> m_LaneQueues;
	};
}
