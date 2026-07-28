#pragma once

#include "VansEventConnection.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace Vans
{
	template <typename... Args>
	class VansSignal
	{
	public:
		using Handler = std::function<void(Args...)>;

		VansSignal()
			: m_Data(std::make_shared<Data>())
		{
		}

		VansEventConnection Connect(Handler handler, int priority = 0)
		{
			if (!handler)
				return {};

			std::shared_ptr<Data> data = m_Data;
			const std::uint64_t id = data->nextId++;

			{
				std::lock_guard<std::mutex> lock(data->mutex);
				data->slots.push_back(Slot{ id, priority, true, std::move(handler) });
				data->sorted = false;
			}

			return VansEventConnection([weak = std::weak_ptr<Data>(data), id]()
			{
				if (std::shared_ptr<Data> locked = weak.lock())
					DisconnectSlot(*locked, id);
			});
		}

		void Emit(Args... args)
		{
			std::shared_ptr<Data> data = m_Data;
			std::size_t dispatchCount = 0;
			{
				std::lock_guard<std::mutex> lock(data->mutex);
				SortIfNeeded(*data);
				++data->dispatchDepth;
				dispatchCount = data->slots.size();
			}

			for (std::size_t index = 0; index < dispatchCount; ++index)
			{
				Handler handler;
				{
					std::lock_guard<std::mutex> lock(data->mutex);
					if (index >= data->slots.size())
						continue;
					Slot& slot = data->slots[index];
					if (!slot.connected)
						continue;
					handler = slot.handler;
				}

				if (handler)
					handler(args...);
			}

			{
				std::lock_guard<std::mutex> lock(data->mutex);
				if (data->dispatchDepth > 0)
					--data->dispatchDepth;
				if (data->dispatchDepth == 0)
					Compact(*data);
			}
		}

		void DisconnectAll()
		{
			std::lock_guard<std::mutex> lock(m_Data->mutex);
			for (Slot& slot : m_Data->slots)
				slot.connected = false;
			if (m_Data->dispatchDepth == 0)
				Compact(*m_Data);
		}

		std::size_t GetListenerCount() const
		{
			std::lock_guard<std::mutex> lock(m_Data->mutex);
			std::size_t count = 0;
			for (const Slot& slot : m_Data->slots)
				if (slot.connected)
					++count;
			return count;
		}

	private:
		struct Slot
		{
			std::uint64_t id = 0;
			int priority = 0;
			bool connected = false;
			Handler handler;
		};

		struct Data
		{
			mutable std::mutex mutex;
			std::vector<Slot> slots;
			std::uint64_t nextId = 1;
			std::size_t dispatchDepth = 0;
			bool sorted = true;
		};

		static void DisconnectSlot(Data& data, std::uint64_t id)
		{
			std::lock_guard<std::mutex> lock(data.mutex);
			for (Slot& slot : data.slots)
			{
				if (slot.id == id)
				{
					slot.connected = false;
					break;
				}
			}
			if (data.dispatchDepth == 0)
				Compact(data);
		}

		static void SortIfNeeded(Data& data)
		{
			if (data.sorted)
				return;
			std::stable_sort(data.slots.begin(), data.slots.end(),
				[](const Slot& lhs, const Slot& rhs)
				{
					return lhs.priority > rhs.priority;
				});
			data.sorted = true;
		}

		static void Compact(Data& data)
		{
			data.slots.erase(
				std::remove_if(data.slots.begin(), data.slots.end(),
					[](const Slot& slot) { return !slot.connected; }),
				data.slots.end());
		}

		std::shared_ptr<Data> m_Data;
	};
}
