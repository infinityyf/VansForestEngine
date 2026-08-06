#pragma once

#include "VansRuntimeHandle.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace Vans
{
enum class VansDestroyChildrenPolicy
{
	DestroyChildren,
	ReparentToRoot,
	ReparentToParent
};

struct VansEntityCreateDesc
{
	std::string stableGuid;
	std::string name;
	VansEntityHandle parent;
	bool active = true;
};

struct VansEntityRecord
{
	std::string stableGuid;
	std::string name;
	VansEntityHandle parent;
	std::vector<VansEntityHandle> children;
	bool selfActive = true;
	bool hierarchyActive = true;
};

class VansEntityRegistry
{
public:
	VansEntityHandle CreateEntity(const VansEntityCreateDesc& desc);
	bool DestroyEntity(
		VansEntityHandle entity,
		VansDestroyChildrenPolicy childrenPolicy = VansDestroyChildrenPolicy::DestroyChildren);
	bool IsAlive(VansEntityHandle entity) const;

	VansEntityHandle FindByGuid(const std::string& guid) const;
	VansEntityHandle FindByName(const std::string& name) const;

	bool SetName(VansEntityHandle entity, const std::string& name);
	bool SetParent(VansEntityHandle child, VansEntityHandle parent);
	bool SetSelfActive(VansEntityHandle entity, bool active);
	bool IsHierarchyActive(VansEntityHandle entity) const;

	const VansEntityRecord* Get(VansEntityHandle entity) const;
	VansEntityRecord* Edit(VansEntityHandle entity);
	std::size_t AliveCount() const { return m_AliveCount; }
	std::vector<VansEntityHandle> CollectAliveEntities() const;
	void Clear();

private:
	struct Slot
	{
		VansEntityRecord record;
		std::uint32_t generation = 1;
		bool alive = false;
	};

	void RemoveFromParent(VansEntityHandle entity);
	void RefreshHierarchyActive(VansEntityHandle entity);
	void RemoveIndexes(const VansEntityRecord& record);

	std::vector<Slot> m_Slots;
	std::vector<std::uint32_t> m_FreeSlots;
	std::unordered_map<std::string, VansEntityHandle> m_GuidIndex;
	std::unordered_map<std::string, VansEntityHandle> m_NameIndex;
	std::size_t m_AliveCount = 0;
};
}
