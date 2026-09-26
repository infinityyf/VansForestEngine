#include "VansTransformStore.h"

#include <cassert>

std::vector<Vans::VansTransform> Vans::VansTransformStore::m_Transforms;
std::vector<std::uint32_t> Vans::VansTransformStore::m_Generations;
std::vector<std::uint64_t> Vans::VansTransformStore::m_TransformRevisions;
std::vector<std::uint8_t> Vans::VansTransformStore::m_Allocated;
std::vector<std::uint8_t> Vans::VansTransformStore::m_Dirty;
std::vector<std::uint32_t> Vans::VansTransformStore::m_DirtyIds;
std::queue<std::uint32_t> Vans::VansTransformStore::m_FreeIds;
std::uint64_t Vans::VansTransformStore::m_Revision = 1u;
#ifdef _DEBUG
std::unordered_set<std::uint32_t> Vans::VansTransformStore::m_AllocatedIds;
#endif

std::uint32_t Vans::VansTransformStore::Allocate()
{
	std::uint32_t id = 0;
	if (!m_FreeIds.empty())
	{
		id = m_FreeIds.front();
		m_FreeIds.pop();
		m_Transforms[id] = VansTransform();
		// Dirty state is frame-scoped and is cleared only by ClearDirty().
	}
	else
	{
		m_Transforms.emplace_back();
		m_Generations.push_back(1u);
		m_TransformRevisions.push_back(0u);
		m_Allocated.push_back(0);
		m_Dirty.push_back(0);
		id = static_cast<std::uint32_t>(m_Transforms.size() - 1);
	}
	m_Allocated[id] = 1;
	m_TransformRevisions[id] = AdvanceRevision();
#ifdef _DEBUG
	m_AllocatedIds.insert(id);
#endif
	return id;
}

void Vans::VansTransformStore::Release(std::uint32_t id)
{
	if (!IsAllocated(id))
		return;
#ifdef _DEBUG
	const auto erasedCount = m_AllocatedIds.erase(id);
	assert(erasedCount == 1 && "VansTransformStore::Release received an invalid id");
#endif
	std::uint32_t& generation = m_Generations[id];
	++generation;
	if (generation == 0)
		++generation;
	m_TransformRevisions[id] = AdvanceRevision();
	m_Allocated[id] = 0;
	// Preserve the frame-scoped dirty bit so same-frame slot reuse keeps the
	// established physics synchronization behavior.
	m_FreeIds.push(id);
}

const Vans::VansTransform& Vans::VansTransformStore::Read(std::uint32_t id)
{
	assert(IsAllocated(id) && "VansTransformStore::Read received an invalid id");
	return m_Transforms[id];
}

void Vans::VansTransformStore::Write(std::uint32_t id, const VansTransform& transform)
{
	assert(IsAllocated(id) && "VansTransformStore::Write received an invalid id");
	m_Transforms[id] = transform;
}

void Vans::VansTransformStore::MarkDirty(std::uint32_t id)
{
	if (!IsAllocated(id))
		return;
	m_TransformRevisions[id] = AdvanceRevision();
	if (m_Dirty[id] != 0)
		return;
	m_Dirty[id] = 1;
	m_DirtyIds.push_back(id);
}

bool Vans::VansTransformStore::IsDirty(std::uint32_t id)
{
	return IsAllocated(id) && m_Dirty[id] != 0;
}

void Vans::VansTransformStore::ClearDirty()
{
	for (const std::uint32_t id : m_DirtyIds)
		m_Dirty[id] = 0;
	m_DirtyIds.clear();
}

std::uint32_t Vans::VansTransformStore::GetGeneration(std::uint32_t id)
{
	return id < m_Generations.size() ? m_Generations[id] : 0u;
}

std::uint64_t Vans::VansTransformStore::GetRevision()
{
	return m_Revision;
}

std::uint64_t Vans::VansTransformStore::GetTransformRevision(std::uint32_t id)
{
	return id < m_TransformRevisions.size() ? m_TransformRevisions[id] : 0u;
}

std::uint64_t Vans::VansTransformStore::AdvanceRevision()
{
	++m_Revision;
	if (m_Revision == 0u)
		m_Revision = 1u;
	return m_Revision;
}

bool Vans::VansTransformStore::IsAllocated(std::uint32_t id)
{
	return id < m_Allocated.size() && m_Allocated[id] != 0;
}
