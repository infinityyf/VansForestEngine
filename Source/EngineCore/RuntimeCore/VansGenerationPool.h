#pragma once

#include "VansStableIdentity.h"

#include <cstdint>
#include <deque>
#include <optional>
#include <utility>
#include <vector>

namespace Vans
{
template <typename Value>
class VansGenerationPool
{
public:
	template <typename... Arguments>
	VansGenerationHandle Emplace(Arguments&&... arguments)
	{
		std::uint32_t index = 0;
		if (!m_Free.empty())
		{
			index = m_Free.back();
			m_Free.pop_back();
		}
		else
		{
			index = static_cast<std::uint32_t>(m_Slots.size());
			m_Slots.emplace_back();
		}
		Slot& slot = m_Slots[index];
		if (slot.generation == 0) slot.generation = 1;
		slot.value.emplace(std::forward<Arguments>(arguments)...);
		return { index, slot.generation };
	}

	Value* Resolve(VansGenerationHandle handle)
	{
		if (!Contains(handle)) return nullptr;
		return &*m_Slots[handle.index].value;
	}

	const Value* Resolve(VansGenerationHandle handle) const
	{
		if (!Contains(handle)) return nullptr;
		return &*m_Slots[handle.index].value;
	}

	bool Contains(VansGenerationHandle handle) const
	{
		return handle.IsValid() && handle.index < m_Slots.size() &&
			m_Slots[handle.index].generation == handle.generation && m_Slots[handle.index].value.has_value();
	}

	bool Release(VansGenerationHandle handle)
	{
		if (!Contains(handle)) return false;
		Slot& slot = m_Slots[handle.index];
		slot.value.reset();
		++slot.generation;
		if (slot.generation == 0) slot.generation = 1;
		m_Free.push_back(handle.index);
		return true;
	}

	void Clear()
	{
		m_Free.clear();
		for (std::uint32_t index = 0; index < m_Slots.size(); ++index)
		{
			Slot& slot = m_Slots[index];
			if (slot.value)
			{
				slot.value.reset();
				++slot.generation;
				if (slot.generation == 0) slot.generation = 1;
			}
			m_Free.push_back(index);
		}
	}

	template <typename Function>
	void ForEach(Function&& function)
	{
		for (std::uint32_t index = 0; index < m_Slots.size(); ++index)
		{
			Slot& slot = m_Slots[index];
			if (slot.value) function(VansGenerationHandle{ index, slot.generation }, *slot.value);
		}
	}

	template <typename Function>
	void ForEach(Function&& function) const
	{
		for (std::uint32_t index = 0; index < m_Slots.size(); ++index)
		{
			const Slot& slot = m_Slots[index];
			if (slot.value) function(VansGenerationHandle{ index, slot.generation }, *slot.value);
		}
	}

	std::size_t ActiveCount() const
	{
		std::size_t count = 0;
		for (const Slot& slot : m_Slots) if (slot.value) ++count;
		return count;
	}

private:
	struct Slot
	{
		std::uint32_t generation = 1;
		std::optional<Value> value;
	};

	std::deque<Slot> m_Slots;
	std::vector<std::uint32_t> m_Free;
};
}
