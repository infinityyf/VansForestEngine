#include "VansRenderWorld.h"

#include <algorithm>
#include <cassert>
#include <iterator>

namespace
{
	std::uint32_t NextGeneration(std::uint32_t generation)
	{
		assert(generation != (std::numeric_limits<std::uint32_t>::max)());
		++generation;
		if (generation == 0)
			generation = 1;
		return generation;
	}
}

void VansGraphics::VansRenderMutationBatch::AddCreate(
	VansRenderProxyHandle handle,
	VansRenderProxyStaticData staticData)
{
	m_Commands.emplace_back(VansCreateRenderProxy{ handle, staticData });
}

void VansGraphics::VansRenderMutationBatch::AddUpdate(
	VansRenderProxyHandle handle,
	VansRenderProxyStaticData staticData)
{
	m_Commands.emplace_back(VansUpdateRenderProxy{ handle, staticData });
}

void VansGraphics::VansRenderMutationBatch::AddDestroy(VansRenderProxyHandle handle)
{
	m_Commands.emplace_back(VansDestroyRenderProxy{ handle });
}

void VansGraphics::VansRenderMutationBatch::Append(VansRenderMutationBatch&& other)
{
	if (other.m_Commands.empty())
		return;
	m_Commands.reserve(m_Commands.size() + other.m_Commands.size());
	std::move(
		other.m_Commands.begin(),
		other.m_Commands.end(),
		std::back_inserter(m_Commands));
	other.m_Commands.clear();
}

VansGraphics::VansRenderProxyHandle
VansGraphics::VansRenderProxyHandleAllocator::Allocate()
{
	std::uint32_t index = 0;
	if (m_FreeIndices.empty())
	{
		assert(m_Generations.size() < (std::numeric_limits<std::uint32_t>::max)());
		index = static_cast<std::uint32_t>(m_Generations.size());
		m_Generations.push_back(1);
		m_Allocated.push_back(true);
	}
	else
	{
		index = m_FreeIndices.back();
		m_FreeIndices.pop_back();
		assert(index < m_Allocated.size() && !m_Allocated[index]);
		m_Allocated[index] = true;
	}
	++m_ActiveCount;
	return { index, m_Generations[index] };
}

bool VansGraphics::VansRenderProxyHandleAllocator::Release(VansRenderProxyHandle handle)
{
	if (!handle.IsValid() || handle.index >= m_Generations.size() ||
		!m_Allocated[handle.index] || m_Generations[handle.index] != handle.generation)
	{
		return false;
	}

	m_Allocated[handle.index] = false;
	m_Generations[handle.index] = NextGeneration(m_Generations[handle.index]);
	m_FreeIndices.push_back(handle.index);
	--m_ActiveCount;
	return true;
}

bool VansGraphics::VansRenderWorld::Apply(const VansRenderMutationBatch& batch)
{
	if (batch.Empty())
		return true;

	// 先在 value-only staging copy 上验证整个 batch，失败时不留下半应用状态。
	std::vector<Slot> stagedSlots = m_Slots;
	std::size_t stagedActiveCount = m_ActiveProxyCount;
	for (const VansRenderMutation& command : batch.Commands())
	{
		if (const auto* create = std::get_if<VansCreateRenderProxy>(&command))
		{
			if (!create->handle.IsValid())
				goto reject;
			if (create->handle.index >= stagedSlots.size())
				stagedSlots.resize(static_cast<std::size_t>(create->handle.index) + 1u);
			Slot& slot = stagedSlots[create->handle.index];
			if (slot.active || (slot.generation != 0 && slot.generation != create->handle.generation))
				goto reject;
			slot.generation = create->handle.generation;
			slot.active = true;
			slot.staticData = create->staticData;
			++stagedActiveCount;
			continue;
		}
		if (const auto* update = std::get_if<VansUpdateRenderProxy>(&command))
		{
			if (!update->handle.IsValid() || update->handle.index >= stagedSlots.size())
				goto reject;
			Slot& slot = stagedSlots[update->handle.index];
			if (!slot.active || slot.generation != update->handle.generation)
				goto reject;
			slot.staticData = update->staticData;
			continue;
		}

		const auto& destroy = std::get<VansDestroyRenderProxy>(command);
		if (!destroy.handle.IsValid() || destroy.handle.index >= stagedSlots.size())
			goto reject;
		Slot& slot = stagedSlots[destroy.handle.index];
		if (!slot.active || slot.generation != destroy.handle.generation)
			goto reject;
		slot.active = false;
		slot.generation = NextGeneration(slot.generation);
		--stagedActiveCount;
	}

	m_Slots = std::move(stagedSlots);
	m_ActiveProxyCount = stagedActiveCount;
	return true;

reject:
	++m_RejectedMutationBatchCount;
	return false;
}

const VansGraphics::VansRenderProxyStaticData*
VansGraphics::VansRenderWorld::Resolve(VansRenderProxyHandle handle) const
{
	if (!handle.IsValid() || handle.index >= m_Slots.size())
		return nullptr;
	const Slot& slot = m_Slots[handle.index];
	return slot.active && slot.generation == handle.generation ? &slot.staticData : nullptr;
}
