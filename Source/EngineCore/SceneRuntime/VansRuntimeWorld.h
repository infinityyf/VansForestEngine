#pragma once

#include "VansComponentStorage.h"
#include "VansEntityCommandBuffer.h"
#include "VansEntityRegistry.h"

#include <memory>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace Vans
{
enum class VansRuntimeCommandCommitPoint : std::uint8_t
{
	SceneAssembly,
	RuntimeFrame,
	AuthoringTransaction,
	SceneTeardown
};

struct VansRuntimeCommandCommitResult
{
	std::size_t consumedCommandCount = 0;
	bool hierarchyActivityChanged = false;
};

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
	VansComponentHandle FindComponentOwnedBy(VansEntityHandle entity, std::uint16_t typeId) const;
	std::vector<VansComponentHandle> CollectComponentsInSubtree(VansEntityHandle entity) const;
	bool SetParent(VansEntityHandle entity, VansEntityHandle parent);
	bool IsAlive(VansEntityHandle entity) const { return m_Entities.IsAlive(entity); }

	VansEntityRegistry& Entities() { return m_Entities; }
	const VansEntityRegistry& Entities() const { return m_Entities; }
	VansEntityCommandBuffer& Commands() { return m_Commands; }

	template <typename T>
	VansComponentStorage<T>* RegisterStorage(std::uint16_t typeId)
	{
		if (typeId == VansInvalidComponentTypeId ||
			!VansRuntimeComponentTypeMatches(typeId, typeid(T)) ||
			FindStorage(typeId) != nullptr)
			return nullptr;
		auto storage = std::make_unique<VansComponentStorage<T>>(typeId);
		VansComponentStorage<T>* raw = storage.get();
		m_ComponentStorages[typeId] = std::move(storage);
		return raw;
	}

	template <typename T>
	VansComponentStorage<T>* FindStorage(std::uint16_t typeId)
	{
		return dynamic_cast<VansComponentStorage<T>*>(FindStorage(typeId));
	}

	template <typename T>
	const VansComponentStorage<T>* FindStorage(std::uint16_t typeId) const
	{
		return dynamic_cast<const VansComponentStorage<T>*>(FindStorage(typeId));
	}

	template <typename T>
	VansComponentStorage<T>* GetOrRegisterStorage(std::uint16_t typeId)
	{
		if (IVansComponentStorage* storage = FindStorage(typeId))
			return dynamic_cast<VansComponentStorage<T>*>(storage);
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
		VansComponentStorage<T>* storage = GetOrRegisterStorage<T>(typeId);
		if (!storage)
			return VansComponentHandle{};
		return storage->Add(
			owner,
			std::move(value),
			std::move(stableGuid),
			enabled,
			m_Entities.IsHierarchyActive(owner));
	}

	VansRuntimeCommandCommitResult CommitCommands(VansRuntimeCommandCommitPoint commitPoint);
	void RecomputeComponentEffectiveEnabled();
	void Clear();

private:
	IVansComponentStorage* FindStorage(std::uint16_t typeId);
	const IVansComponentStorage* FindStorage(std::uint16_t typeId) const;

	void CollectEntitiesForDestroy(
		VansEntityHandle entity,
		VansDestroyChildrenPolicy childrenPolicy,
		std::vector<VansEntityHandle>& outEntities) const;
	void RemoveComponentsOwnedBy(VansEntityHandle entity);
	void RequestComponentEffectiveEnabledRecompute();

	VansEntityRegistry m_Entities;
	VansEntityCommandBuffer m_Commands;
	std::unordered_map<std::uint16_t, std::unique_ptr<IVansComponentStorage>> m_ComponentStorages;
	bool m_CommandCommitInProgress = false;
	bool m_ComponentEffectiveEnabledDirty = false;
};
}
