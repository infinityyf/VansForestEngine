#pragma once

#include "VansComponentStorage.h"
#include "VansEntityCommandBuffer.h"
#include "VansEntityRegistry.h"

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vans
{
class VansRuntimeWorld
{
public:
	VansEntityHandle CreateEntity(const VansEntityCreateDesc& desc);
	bool DestroyEntity(
		VansEntityHandle entity,
		VansDestroyChildrenPolicy childrenPolicy = VansDestroyChildrenPolicy::DestroyChildren);
	bool SetEntityName(VansEntityHandle entity, const std::string& name);
	bool SetEntityActive(VansEntityHandle entity, bool active);
	bool SetComponentEnabled(VansComponentHandle component, bool enabled);
	bool RemoveComponent(VansComponentHandle component);
	VansComponentHandle FindComponentByGuid(
		const std::string& componentGuid,
		std::uint16_t typeId = VansInvalidComponentTypeId) const;
	const VansComponentHeader* GetComponentHeader(VansComponentHandle component) const;
	bool IsComponentSelfEnabled(VansComponentHandle component) const;
	bool IsComponentEffectivelyEnabled(VansComponentHandle component) const;
	std::vector<VansComponentHandle> CollectComponentsOwnedBy(VansEntityHandle entity) const;
	std::vector<VansComponentHandle> CollectComponentsInSubtree(VansEntityHandle entity) const;
	bool SetParent(VansEntityHandle entity, VansEntityHandle parent);
	bool IsAlive(VansEntityHandle entity) const { return m_Entities.IsAlive(entity); }

	VansEntityRegistry& Entities() { return m_Entities; }
	const VansEntityRegistry& Entities() const { return m_Entities; }
	VansEntityCommandBuffer& Commands() { return m_Commands; }

	template <typename T>
	VansComponentStorage<T>& RegisterStorage(std::uint16_t typeId)
	{
		auto storage = std::make_unique<VansComponentStorage<T>>(typeId);
		VansComponentStorage<T>* raw = storage.get();
		m_ComponentStorages[typeId] = std::move(storage);
		return *raw;
	}

	template <typename T>
	VansComponentStorage<T>& GetOrRegisterStorage(std::uint16_t typeId)
	{
		if (IVansComponentStorage* storage = FindStorage(typeId))
			return static_cast<VansComponentStorage<T>&>(*storage);
		return RegisterStorage<T>(typeId);
	}

	template <typename T>
	VansComponentHandle AddComponent(
		VansEntityHandle owner,
		std::uint16_t typeId,
		T value,
		std::string stableGuid = std::string(),
		bool enabled = true)
	{
		if (!m_Entities.IsAlive(owner))
			return VansComponentHandle{};
		VansComponentStorage<T>& storage = GetOrRegisterStorage<T>(typeId);
		return storage.Add(
			owner,
			std::move(value),
			std::move(stableGuid),
			enabled,
			m_Entities.IsHierarchyActive(owner));
	}

	IVansComponentStorage* FindStorage(std::uint16_t typeId);
	const IVansComponentStorage* FindStorage(std::uint16_t typeId) const;

	void FlushCommands();
	void RecomputeComponentEffectiveEnabled();
	void Clear();

private:
	void CollectEntitiesForDestroy(
		VansEntityHandle entity,
		VansDestroyChildrenPolicy childrenPolicy,
		std::vector<VansEntityHandle>& outEntities) const;
	void RemoveComponentsOwnedBy(VansEntityHandle entity);

	VansEntityRegistry m_Entities;
	VansEntityCommandBuffer m_Commands;
	std::unordered_map<std::uint16_t, std::unique_ptr<IVansComponentStorage>> m_ComponentStorages;
};
}
