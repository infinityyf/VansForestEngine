#include "VansEntityRegistry.h"

#include <algorithm>

namespace Vans
{
VansEntityHandle VansEntityRegistry::CreateEntity(const VansEntityCreateDesc& desc)
{
	std::uint32_t index = VansInvalidRuntimeIndex;
	if (!m_FreeSlots.empty())
	{
		index = m_FreeSlots.back();
		m_FreeSlots.pop_back();
	}
	else
	{
		index = static_cast<std::uint32_t>(m_Slots.size());
		m_Slots.push_back(Slot{});
	}

	Slot& slot = m_Slots[index];
	slot.alive = true;
	slot.record = VansEntityRecord{};
	slot.record.stableGuid = desc.stableGuid;
	slot.record.name = desc.name;
	slot.record.selfActive = desc.active;
	slot.record.hierarchyActive = desc.active;
	const VansEntityHandle handle{ index, slot.generation };

	if (!desc.stableGuid.empty())
		m_GuidIndex[desc.stableGuid] = handle;
	if (!desc.name.empty())
		m_NameIndex[desc.name] = handle;
	++m_AliveCount;

	if (desc.parent.IsValid())
		SetParent(handle, desc.parent);
	return handle;
}

bool VansEntityRegistry::DestroyEntity(VansEntityHandle entity, VansDestroyChildrenPolicy childrenPolicy)
{
	Slot* slot = nullptr;
	if (entity.index < m_Slots.size())
		slot = &m_Slots[entity.index];
	if (!slot || !IsAlive(entity))
		return false;

	std::vector<VansEntityHandle> children = slot->record.children;
	if (childrenPolicy == VansDestroyChildrenPolicy::DestroyChildren)
	{
		for (VansEntityHandle child : children)
			DestroyEntity(child, childrenPolicy);
	}
	else
	{
		const VansEntityHandle newParent =
			childrenPolicy == VansDestroyChildrenPolicy::ReparentToParent ? slot->record.parent : VansEntityHandle{};
		for (VansEntityHandle child : children)
			SetParent(child, newParent);
	}

	RemoveFromParent(entity);
	RemoveIndexes(slot->record);
	slot->record = VansEntityRecord{};
	slot->alive = false;
	++slot->generation;
	m_FreeSlots.push_back(entity.index);
	--m_AliveCount;
	return true;
}

bool VansEntityRegistry::IsAlive(VansEntityHandle entity) const
{
	return entity.index < m_Slots.size() &&
		m_Slots[entity.index].alive &&
		m_Slots[entity.index].generation == entity.generation;
}

VansEntityHandle VansEntityRegistry::FindByGuid(const std::string& guid) const
{
	const auto it = m_GuidIndex.find(guid);
	return it != m_GuidIndex.end() && IsAlive(it->second) ? it->second : VansEntityHandle{};
}

VansEntityHandle VansEntityRegistry::FindByName(const std::string& name) const
{
	const auto it = m_NameIndex.find(name);
	return it != m_NameIndex.end() && IsAlive(it->second) ? it->second : VansEntityHandle{};
}

bool VansEntityRegistry::SetName(VansEntityHandle entity, const std::string& name)
{
	VansEntityRecord* record = Edit(entity);
	if (!record)
		return false;
	if (record->name == name)
		return true;
	if (!record->name.empty())
	{
		const auto found = m_NameIndex.find(record->name);
		if (found != m_NameIndex.end() && found->second == entity)
			m_NameIndex.erase(found);
	}
	record->name = name;
	if (!record->name.empty())
		m_NameIndex[record->name] = entity;
	return true;
}

bool VansEntityRegistry::SetParent(VansEntityHandle child, VansEntityHandle parent)
{
	if (!IsAlive(child) || (parent.IsValid() && !IsAlive(parent)) || child == parent)
		return false;

	for (VansEntityHandle cursor = parent; cursor.IsValid();)
	{
		if (cursor == child)
			return false;
		const VansEntityRecord* parentRecord = Get(cursor);
		cursor = parentRecord ? parentRecord->parent : VansEntityHandle{};
	}

	RemoveFromParent(child);
	VansEntityRecord* childRecord = Edit(child);
	childRecord->parent = parent;
	if (parent.IsValid())
		Edit(parent)->children.push_back(child);
	RefreshHierarchyActive(child);
	return true;
}

bool VansEntityRegistry::SetSelfActive(VansEntityHandle entity, bool active)
{
	VansEntityRecord* record = Edit(entity);
	if (!record)
		return false;
	record->selfActive = active;
	RefreshHierarchyActive(entity);
	return true;
}

bool VansEntityRegistry::IsHierarchyActive(VansEntityHandle entity) const
{
	const VansEntityRecord* record = Get(entity);
	return record ? record->hierarchyActive : false;
}

const VansEntityRecord* VansEntityRegistry::Get(VansEntityHandle entity) const
{
	if (!IsAlive(entity))
		return nullptr;
	return &m_Slots[entity.index].record;
}

VansEntityRecord* VansEntityRegistry::Edit(VansEntityHandle entity)
{
	if (!IsAlive(entity))
		return nullptr;
	return &m_Slots[entity.index].record;
}

std::vector<VansEntityHandle> VansEntityRegistry::CollectAliveEntities() const
{
	std::vector<VansEntityHandle> entities;
	entities.reserve(m_AliveCount);
	for (std::uint32_t index = 0; index < m_Slots.size(); ++index)
	{
		const Slot& slot = m_Slots[index];
		if (slot.alive)
			entities.push_back(VansEntityHandle{ index, slot.generation });
	}
	return entities;
}

void VansEntityRegistry::Clear()
{
	m_Slots.clear();
	m_FreeSlots.clear();
	m_GuidIndex.clear();
	m_NameIndex.clear();
	m_AliveCount = 0;
}

void VansEntityRegistry::RemoveFromParent(VansEntityHandle entity)
{
	VansEntityRecord* record = Edit(entity);
	if (!record || !record->parent.IsValid())
		return;
	VansEntityRecord* parent = Edit(record->parent);
	if (parent)
	{
		auto& children = parent->children;
		children.erase(std::remove(children.begin(), children.end(), entity), children.end());
	}
	record->parent = VansEntityHandle{};
}

void VansEntityRegistry::RefreshHierarchyActive(VansEntityHandle entity)
{
	VansEntityRecord* record = Edit(entity);
	if (!record)
		return;
	const VansEntityRecord* parent = Get(record->parent);
	record->hierarchyActive = record->selfActive && (!parent || parent->hierarchyActive);
	const std::vector<VansEntityHandle> children = record->children;
	for (VansEntityHandle child : children)
		RefreshHierarchyActive(child);
}

void VansEntityRegistry::RemoveIndexes(const VansEntityRecord& record)
{
	if (!record.stableGuid.empty())
	{
		const auto it = m_GuidIndex.find(record.stableGuid);
		if (it != m_GuidIndex.end())
			m_GuidIndex.erase(it);
	}
	if (!record.name.empty())
	{
		const auto it = m_NameIndex.find(record.name);
		if (it != m_NameIndex.end())
			m_NameIndex.erase(it);
	}
}
}
