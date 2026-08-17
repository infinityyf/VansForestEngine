#pragma once

#include "../RuntimeCore/VansGenerationPool.h"

#include <unordered_map>

namespace Vans
{
inline std::uint64_t VansTimelineHandleKey(VansGenerationHandle handle)
{
	return (static_cast<std::uint64_t>(handle.generation) << 32) | handle.index;
}

template <typename State>
class VansTimelineModuleApplierState
{
public:
	template <typename Factory>
	std::pair<VansTimelineRestoreHandle, State*> Acquire(
		VansTimelineWriterHandle writer,
		Factory&& factory)
	{
		const std::uint64_t key = VansTimelineHandleKey(writer);
		const auto found = m_ByWriter.find(key);
		if (found != m_ByWriter.end()) return { found->second, m_States.Resolve(found->second) };
		VansTimelineRestoreHandle handle = m_States.Emplace(factory());
		m_ByWriter.emplace(key, handle);
		return { handle, m_States.Resolve(handle) };
	}

	State* Resolve(VansTimelineRestoreHandle handle) { return m_States.Resolve(handle); }
	State* ResolveWriter(VansTimelineWriterHandle writer)
	{
		const auto found = m_ByWriter.find(VansTimelineHandleKey(writer));
		return found == m_ByWriter.end() ? nullptr : m_States.Resolve(found->second);
	}

	template <typename Callback>
	void ForEach(Callback&& callback)
	{
		m_States.ForEach([&](VansGenerationHandle, State& state)
		{
			callback(state);
		});
	}

	bool Release(VansTimelineRestoreHandle handle)
	{
		State* state = m_States.Resolve(handle);
		if (!state) return false;
		m_ByWriter.erase(VansTimelineHandleKey(state->writer));
		return m_States.Release(handle);
	}

	void ReleaseWriter(VansTimelineWriterHandle writer)
	{
		const auto found = m_ByWriter.find(VansTimelineHandleKey(writer));
		if (found == m_ByWriter.end()) return;
		m_States.Release(found->second);
		m_ByWriter.erase(found);
	}

	void Clear()
	{
		m_ByWriter.clear();
		m_States.Clear();
	}

private:
	VansGenerationPool<State> m_States;
	std::unordered_map<std::uint64_t, VansTimelineRestoreHandle> m_ByWriter;
};
}
