#pragma once

#include "VansEventConnection.h"
#include "VansEventLane.h"
#include "VansEventTypeId.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <functional>
#include <iterator>
#include <memory>
#include <mutex>
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
			int priority = 0)
		{
			if (!handler)
				return {};

			auto dispatcher = GetOrCreateDispatcher<EventT>();
			return dispatcher->Subscribe(std::move(handler), lane, priority);
		}

		template <typename EventT>
		void PublishNow(const EventT& event)
		{
			auto dispatcher = GetDispatcher<EventT>();
			if (dispatcher)
				dispatcher->Publish(event, nullptr);
		}

		template <typename EventT>
		void Enqueue(EventT event, VansEventLane lane)
		{
			auto queued = std::make_unique<QueuedEvent<EventT>>(std::move(event), lane);
			LaneQueue& queue = m_LaneQueues[ToEventLaneIndex(lane)];
			{
				std::lock_guard<std::mutex> lock(queue.mutex);
				queue.events.push_back(std::move(queued));
			}
		}

		template <typename EventT>
		void EnqueueNextFrame(EventT event, VansEventLane lane)
		{
			auto queued = std::make_unique<QueuedEvent<EventT>>(std::move(event), lane);
			LaneQueue& queue = m_NextFrameQueues[ToEventLaneIndex(lane)];
			std::lock_guard<std::mutex> lock(queue.mutex);
			queue.events.push_back(std::move(queued));
		}

		void BeginFrame();
		void Flush(VansEventLane lane);

	private:
		struct IEventDispatcher
		{
			virtual ~IEventDispatcher() = default;
		};

		template <typename EventT>
		class EventDispatcher final : public IEventDispatcher
		{
		public:
			using Handler = std::function<void(const EventT&)>;

			VansEventConnection Subscribe(Handler handler, VansEventLane lane, int priority)
			{
				std::uint64_t id = 0;
				{
					std::lock_guard<std::mutex> lock(m_Mutex);
					id = m_NextId++;
					Slot slot{ id, lane, priority, true, std::move(handler) };
					if (m_DispatchDepth == 0)
					{
						m_Slots.push_back(std::move(slot));
						m_Sorted = false;
					}
					else
					{
						m_PendingSlots.push_back(std::move(slot));
					}
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
					if (m_DispatchDepth == 0)
						SortIfNeeded();
					assert(m_DispatchDepth < MaximumDispatchDepth);
					++m_DispatchDepth;
					dispatchCount = m_Slots.size();
				}

				try
				{
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
				}
				catch (...)
				{
					FinishDispatch();
					throw;
				}

				FinishDispatch();
			}

		private:
			struct Slot
			{
				std::uint64_t id = 0;
				VansEventLane lane = VansEventLane::MainThread;
				int priority = 0;
				bool connected = false;
				Handler handler;
			};

			void Disconnect(std::uint64_t id)
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				const auto disconnect = [id](std::vector<Slot>& slots)
				{
					for (Slot& slot : slots)
					{
						if (slot.id == id)
						{
							slot.connected = false;
							return true;
						}
					}
					return false;
				};
				if (!disconnect(m_Slots))
					disconnect(m_PendingSlots);
				if (m_DispatchDepth == 0)
					Compact();
			}

			void FinishDispatch()
			{
				std::lock_guard<std::mutex> lock(m_Mutex);
				assert(m_DispatchDepth > 0);
				--m_DispatchDepth;
				if (m_DispatchDepth != 0)
					return;
				if (!m_PendingSlots.empty())
				{
					m_Slots.insert(m_Slots.end(),
						std::make_move_iterator(m_PendingSlots.begin()),
						std::make_move_iterator(m_PendingSlots.end()));
					m_PendingSlots.clear();
					m_Sorted = false;
				}
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
			std::vector<Slot> m_PendingSlots;
			std::uint64_t m_NextId = 1;
			std::size_t m_DispatchDepth = 0;
			bool m_Sorted = true;
			std::weak_ptr<EventDispatcher<EventT>> m_Self;
			static constexpr std::size_t MaximumDispatchDepth = 16;
		};

		struct IQueuedEvent
		{
			virtual ~IQueuedEvent() = default;
			virtual void Dispatch(VansEventBus& bus) = 0;
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
			}

			EventT event;
			VansEventLane lane = VansEventLane::MainThread;
		};

		struct LaneQueue
		{
			mutable std::mutex mutex;
			std::vector<std::unique_ptr<IQueuedEvent>> events;
		};

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

			auto dispatcher = std::make_shared<EventDispatcher<EventT>>();
			dispatcher->BindSelf(dispatcher);
			m_Dispatchers[typeId] = dispatcher;
			return dispatcher;
		}

		mutable std::mutex m_DispatchersMutex;
		std::unordered_map<VansEventTypeId, std::shared_ptr<IEventDispatcher>> m_Dispatchers;

		std::array<LaneQueue, ToEventLaneIndex(VansEventLane::Count)> m_LaneQueues;
		std::array<LaneQueue, ToEventLaneIndex(VansEventLane::Count)> m_NextFrameQueues;
	};
}
